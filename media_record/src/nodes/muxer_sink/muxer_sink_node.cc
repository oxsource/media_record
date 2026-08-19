#include "src/nodes/muxer_sink/muxer_sink_node.h"

#include <cstdio>
#include <string>
#include <utility>

#include "src/framework/transport/packet.h"
#include "src/framework/transport/recording_defaults.h"

namespace media::record {

namespace {

std::string StatusName(video::codec::Status s) {
  return video::codec::StatusToString(s);
}

}  // namespace

NodeStatus MuxerSinkNode::Open() {
  StreamBuffer* in = Input("clips");
  if (in == nullptr) {
    return NodeStatus{false, "muxer_sink: missing input stream 'clips'"};
  }

  const RecordingDefaults& d = Defaults();
  target_path_ = d.output_file;
  if (target_path_.empty()) {
    return NodeStatus{false,
                      "muxer_sink: no output file configured "
                      "(RecordingDefaults::output_file)"};
  }
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
  cfg.width = d.width;
  cfg.height = d.height;
  cfg.fps = d.fps > 0 ? d.fps : 30;
  cfg.backend = video::codec::Backend::kAuto;
  if (!cfg.IsValid()) {
    return NodeStatus{false,
                      "muxer_sink: invalid muxer config (width/height unset; "
                      "set RecordingDefaults::width/height)"};
  }

  muxer_ = video::codec::CodecFactory::CreateMuxer(cfg);
  if (!muxer_) {
    return NodeStatus{false, "muxer_sink: muxer unavailable (no backend)"};
  }
  sink_ = std::make_unique<video::codec::FileByteSink>(temp_path_);
  if (!sink_->IsOpen()) {
    return NodeStatus{false, "muxer_sink: cannot open output for writing: '" +
                                 temp_path_ + "'"};
  }
  if (muxer_->SetOutput(sink_.get()) != video::codec::Status::kOk) {
    return NodeStatus{false, "muxer_sink: Muxer::SetOutput failed"};
  }
  return NodeStatus{};
}

NodeStatus MuxerSinkNode::Process() {
  StreamBuffer* in = Input("clips");
  if (in == nullptr) {
    return NodeStatus{false, "muxer_sink: missing input stream 'clips'"};
  }

  Packet pkt;
  if (in->Pop(&pkt)) {
    if (!pkt.IsEncoded()) {
      return NodeStatus{false,
                        "muxer_sink: unexpected non-encoded packet on 'clips'"};
    }
    const video::codec::VideoPacket& src =
        std::get<video::codec::VideoPacket>(pkt.payload());
    video::codec::VideoPacket copy = src;
    const video::codec::Status s = muxer_->Push(std::move(copy));
    if (s != video::codec::Status::kOk) {
      return NodeStatus{false,
                        "muxer_sink: Muxer::Push failed (" + StatusName(s) + ")"};
    }
  }
  return NodeStatus{};
}

NodeStatus MuxerSinkNode::Close() {
  // Finalize only on a successful run: Finish() writes the trailer, then the
  // temp file is atomically renamed to the target. On failure (or a partial
  // run) the temp file is removed so no broken artifact remains (FR-009).
  if (muxer_ && sink_ && !Defaults().pipeline_failed && !finished_) {
    finished_ = true;
    const video::codec::Status s = muxer_->Finish();
    if (s != video::codec::Status::kOk) {
      std::remove(temp_path_.c_str());
      return NodeStatus{false,
                        "muxer_sink: Muxer::Finish failed (" + StatusName(s) + ")"};
    }
    if (std::rename(temp_path_.c_str(), target_path_.c_str()) != 0) {
      std::remove(temp_path_.c_str());
      return NodeStatus{false, "muxer_sink: cannot rename '" + temp_path_ +
                                   "' to '" + target_path_ + "'"};
    }
    std::printf("[muxer_sink] wrote %s\n", target_path_.c_str());
  }

  if (muxer_) muxer_->Release();
  muxer_.reset();
  sink_.reset();
  if (!finished_ && !temp_path_.empty()) {
    std::remove(temp_path_.c_str());  // no partial artifacts (FR-009)
  }
  return NodeStatus{};
}

REGISTER_NODE("MuxerSinkNode", MuxerSinkNode);

}  // namespace media::record
