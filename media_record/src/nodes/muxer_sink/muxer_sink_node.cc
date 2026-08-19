#include "src/nodes/muxer_sink/muxer_sink_node.h"

#include <cstdio>
#include <string>
#include <utility>

#include "src/framework/node/graph_context.h"
#include "src/framework/node/node_contract.h"
#include "src/framework/node/node_options.h"
#include "src/framework/node/node_registry.h"
#include "src/framework/runner/recording_defaults.h"
#include "src/framework/stream/packet.h"

namespace media::record {

namespace {

std::string StatusName(video::codec::Status s) {
  return video::codec::StatusToString(s);
}

}  // namespace

MuxerSinkNode::MuxerSinkNode(const std::string& name,
                             const graph::runtime::NodeOptions& options)
    : Node(name) {
  if (const std::string* v = options.Get<std::string>("output")) {
    output_file_ = *v;
  }
  if (const int* v = options.Get<int>("width")) width_ = *v;
  if (const int* v = options.Get<int>("height")) height_ = *v;
  if (const int* v = options.Get<int>("fps")) fps_ = *v;
  if (fps_ <= 0) fps_ = 30;
}

absl::Status MuxerSinkNode::GetContract(graph::runtime::NodeContract* c) {
  c->Inputs().Get("input").Set<video::codec::VideoPacket>();
  return absl::OkStatus();
}

absl::Status MuxerSinkNode::Open(graph::runtime::GraphContext&) {
  if (output_file_.empty()) {
    return absl::InvalidArgumentError(
        "muxer_sink: no output file configured (NodeOptions 'output')");
  }
  target_path_ = output_file_;
  temp_path_ = target_path_ + ".tmp";

  // Overwrite notice (clarification: overwrite and continue).
  if (FILE* existing = std::fopen(target_path_.c_str(), "rb")) {
    std::fclose(existing);
    std::printf("[muxer_sink] overwriting existing file: %s\n",
                target_path_.c_str());
  }

  video::codec::MuxerConfig cfg;
  cfg.format = video::codec::MuxFormat::kMp4;
  // Plain (non-fragmented) MP4 with moov at the end: the most widely playable
  // layout. Requires the muxer's avio to be seekable, which the codec muxer
  // provides through ByteSink Seek/Tell (FileByteSink).
  cfg.fragmented = false;
  cfg.width = width_;
  cfg.height = height_;
  cfg.fps = fps_;
  cfg.backend = video::codec::Backend::kAuto;
  if (!cfg.IsValid()) {
    return absl::InvalidArgumentError(
        "muxer_sink: invalid muxer config (width/height unset; set NodeOptions "
        "'width'/'height')");
  }

  muxer_ = video::codec::CodecFactory::CreateMuxer(cfg);
  if (!muxer_) {
    return absl::InternalError("muxer_sink: muxer unavailable (no backend)");
  }
  sink_ = std::make_unique<video::codec::FileByteSink>(temp_path_);
  if (!sink_->IsOpen()) {
    return absl::InvalidArgumentError(
        "muxer_sink: cannot open output for writing: '" + temp_path_ + "'");
  }
  if (muxer_->SetOutput(sink_.get()) != video::codec::Status::kOk) {
    return absl::InternalError("muxer_sink: Muxer::SetOutput failed");
  }
  return absl::OkStatus();
}

absl::Status MuxerSinkNode::Process(graph::runtime::GraphContext& ctx) {
  auto& in = ctx.Inputs().Get("input");
  if (in.IsEmpty()) return absl::OkStatus();
  auto pkt_or = in.Value().Share<video::codec::VideoPacket>();
  if (!pkt_or.ok()) {
    return absl::InvalidArgumentError(
        "muxer_sink: unexpected non-encoded packet on 'input'");
  }
  const video::codec::VideoPacket& src = **pkt_or;
  video::codec::VideoPacket copy = src;
  const video::codec::Status s = muxer_->Push(std::move(copy));
  if (s != video::codec::Status::kOk) {
    return absl::InternalError("muxer_sink: Muxer::Push failed (" +
                               StatusName(s) + ")");
  }
  return absl::OkStatus();
}

absl::Status MuxerSinkNode::Close(graph::runtime::GraphContext&) {
  // Finalize only on a successful run: Finish() writes the trailer, then the
  // temp file is atomically renamed to the target. On failure (or a partial
  // run) the temp file is removed so no broken artifact remains (FR-009).
  if (muxer_ && sink_ && !Defaults().pipeline_failed && !finished_) {
    finished_ = true;
    const video::codec::Status s = muxer_->Finish();
    if (s != video::codec::Status::kOk) {
      std::remove(temp_path_.c_str());
      return absl::InternalError("muxer_sink: Muxer::Finish failed (" +
                                 StatusName(s) + ")");
    }
    if (std::rename(temp_path_.c_str(), target_path_.c_str()) != 0) {
      std::remove(temp_path_.c_str());
      return absl::InternalError("muxer_sink: cannot rename '" + temp_path_ +
                                 "' to '" + target_path_ + "'");
    }
    std::printf("[muxer_sink] wrote %s\n", target_path_.c_str());
  }

  if (muxer_) muxer_->Release();
  muxer_.reset();
  sink_.reset();
  if (!finished_ && !temp_path_.empty()) {
    std::remove(temp_path_.c_str());  // no partial artifacts (FR-009)
  }
  return absl::OkStatus();
}

namespace { using media::record::MuxerSinkNode; }
GRAPH_RUNTIME_REGISTER_NODE("MuxerSinkNode", MuxerSinkNode);

}  // namespace media::record
