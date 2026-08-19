#include "src/nodes/video_encoder/video_encoder_node.h"

#include <string>
#include <utility>

#include "graph_runtime/graph_context.h"
#include "graph_runtime/node_contract.h"
#include "graph_runtime/node_options.h"
#include "graph_runtime/node_registry.h"
#include "graph_runtime/packet.h"
#include "src/framework/lifecycle/lifecycle_context.h"
#include "src/nodes/video_encoder/rgba_i420.h"

namespace media::record {

namespace {

std::string StatusName(video::codec::Status s) {
  return video::codec::StatusToString(s);
}

}  // namespace

// Collects encoded packets from the push-mode encoder into the node's pending_
// list; Process() emits them into the output shard afterwards.
class VideoEncoderNode::PacketSinkAdapter : public video::codec::PacketSink {
 public:
  explicit PacketSinkAdapter(VideoEncoderNode* node) : node_(node) {}

  video::codec::Status Push(video::codec::VideoPacket&& pkt) override {
    node_->pending_.push_back(
        graph::runtime::Packet::MakePacket<video::codec::VideoPacket>(
            std::move(pkt)));
    return video::codec::Status::kOk;
  }

  video::codec::Status Push(video::codec::AudioPacket&&) override {
    return video::codec::Status::kUnsupportedOperation;
  }

 private:
  VideoEncoderNode* node_;
};

VideoEncoderNode::VideoEncoderNode(
    const std::string& name, const graph::runtime::NodeOptions& options)
    : Node(name) {
  if (const int* v = options.Get<int>("fps")) fps_ = *v;
  if (const int* v = options.Get<int>("bitrate")) bitrate_ = *v;
  if (const bool* v = options.Get<bool>("input_surface")) surface_mode_ = *v;
  if (const int* v = options.Get<int>("width")) width_ = *v;
  if (const int* v = options.Get<int>("height")) height_ = *v;
  if (fps_ <= 0) fps_ = 30;
  if (bitrate_ <= 0) bitrate_ = 4'000'000;
}

// The input stream carries a CPU VideoFrame (host) OR a PacketNotify
// (Android surface mode) — declared SetAny() so both route through the same
// port; Process() branches on the runtime type (see Process).
absl::Status VideoEncoderNode::GetContract(graph::runtime::NodeContract* c) {
  c->Inputs().Get("input").SetAny();
  c->Outputs().Get("output").Set<video::codec::VideoPacket>();
  return absl::OkStatus();
}

absl::Status VideoEncoderNode::EnsureEncoder(
    const video::codec::VideoFrame& frame) {
  if (encoder_) return absl::OkStatus();

  video::codec::VideoConfig cfg;
  cfg.codec = video::codec::VideoCodecType::kH264;
  cfg.width = frame.width;
  cfg.height = frame.height;
  cfg.fps = fps_;
  cfg.bitrate = bitrate_;
  cfg.input_format = video::codec::PixelFormat::kI420;
  cfg.backend = video::codec::Backend::kAuto;

  encoder_ = video::codec::CodecFactory::CreateVideo(cfg);
  if (!encoder_) {
    return absl::InternalError(
        "video_encoder: video encoder unavailable (no backend)");
  }
  if (encoder_->Init() != video::codec::Status::kOk) {
    return absl::InternalError("video_encoder: encoder Init failed");
  }
  sink_ = std::make_unique<PacketSinkAdapter>(this);
  if (encoder_->SetOutputSink(sink_.get()) != video::codec::Status::kOk) {
    return absl::InternalError("video_encoder: push-mode wiring failed");
  }
  return absl::OkStatus();
}

void VideoEncoderNode::EmitPending(graph::runtime::GraphContext& ctx) {
  if (pending_.empty()) return;
  auto& out = ctx.Outputs().Get("output");
  for (auto& pkt : pending_) {
    out.AddPacket(std::move(pkt));
  }
  pending_.clear();
}

absl::Status VideoEncoderNode::Open(graph::runtime::GraphContext& ctx) {
#if defined(__ANDROID__)
  // Android/surface mode: create the encoder NOW (Open, not first Process) so
  // its CreateInputSurface() is ready for the render node to draw onto on its
  // first Process — the render node Open()s before this node, but all Open()
  // calls finish before any Process() (scheduler guarantees), so writing the
  // surface here is always visible to the render node's first Process.
  if (surface_mode_) {
    return EnsureSurfaceEncoder(ctx);
  }
#endif
  return absl::OkStatus();
}

#if defined(__ANDROID__)
// Surface mode: create the encoder (input_surface=true) in Open(), write its
// CreateInputSurface() into the shared LifecycleContext::input_surface, and
// attach the packet sink. Dimensions come from NodeOptions (must be set).
absl::Status VideoEncoderNode::EnsureSurfaceEncoder(
    graph::runtime::GraphContext& ctx) {
  if (width_ <= 0 || height_ <= 0) {
    return absl::InvalidArgumentError(
        "video_encoder: surface mode requires NodeOptions 'width'/'height'");
  }
  video::codec::VideoConfig cfg;
  cfg.codec = video::codec::VideoCodecType::kH264;
  cfg.width = width_;
  cfg.height = height_;
  cfg.fps = fps_;
  cfg.bitrate = bitrate_;
  cfg.input_surface = true;
  cfg.backend = video::codec::Backend::kAuto;

  encoder_ = video::codec::CodecFactory::CreateVideo(cfg);
  if (!encoder_) {
    return absl::InternalError(
        "video_encoder: surface-mode video encoder unavailable (no backend)");
  }
  if (encoder_->Init() != video::codec::Status::kOk) {
    return absl::InternalError("video_encoder: surface encoder Init failed");
  }
  sink_ = std::make_unique<PacketSinkAdapter>(this);
  if (encoder_->SetOutputSink(sink_.get()) != video::codec::Status::kOk) {
    return absl::InternalError("video_encoder: push-mode wiring failed");
  }

  // Publish the input surface for the render node (spec 003 contracts D-2).
  const graph::runtime::Packet sp =
      ctx.InputSidePackets().Get(media::record::LifecycleContext::kSidePacketTag);
  if (!sp.IsEmpty()) {
    auto lc = sp.Get<media::record::LifecycleContext*>();
    if (lc.ok() && lc.value() != nullptr) {
      lc.value()->input_surface = encoder_->CreateInputSurface();
    }
  }
  return absl::OkStatus();
}
#endif

absl::Status VideoEncoderNode::Process(graph::runtime::GraphContext& ctx) {
  auto& in = ctx.Inputs().Get("input");
  if (!in.IsEmpty()) {
#if defined(__ANDROID__)
    if (surface_mode_) {
      // Android/surface mode: the render node drew onto our input surface and
      // sent a PacketNotify; Poll() pumps the hardware encoder so it keeps
      // consuming input-surface frames (back-pressure). No CPU VideoFrame here.
      auto notify_or = in.Value().Share<media::record::PacketNotify>();
      if (!notify_or.ok()) {
        return absl::InvalidArgumentError(
            "video_encoder: surface mode expects PacketNotify on 'input'");
      }
      if (encoder_) encoder_->Poll();
      EmitPending(ctx);
    } else
#endif
    {
      auto frame_or = in.Value().Share<video::codec::VideoFrame>();
      if (!frame_or.ok()) {
        return absl::InvalidArgumentError(
            "video_encoder: unexpected non-frame packet on 'input'");
      }
      const video::codec::VideoFrame& rgba = **frame_or;

      absl::Status status = EnsureEncoder(rgba);
      if (!status.ok()) return status;

      video::codec::VideoFrame i420;
      i420.format = video::codec::PixelFormat::kI420;
      i420.width = rgba.width;
      i420.height = rgba.height;
      i420.stride[0] = rgba.width;
      i420.stride[1] = rgba.width / 2;
      i420.stride[2] = rgba.width / 2;
      i420.planes[0].resize(static_cast<size_t>(rgba.width) * rgba.height);
      i420.planes[1].resize(static_cast<size_t>(rgba.width / 2) *
                            (rgba.height / 2));
      i420.planes[2].resize(static_cast<size_t>(rgba.width / 2) *
                            (rgba.height / 2));
      i420.timestamp_us = rgba.timestamp_us;

      RgbaToI420({rgba.planes[0].data(), static_cast<size_t>(rgba.stride[0]),
                  rgba.width, rgba.height},
                 {i420.planes[0].data(), static_cast<size_t>(i420.stride[0]),
                  i420.planes[1].data(), static_cast<size_t>(i420.stride[1]),
                  i420.planes[2].data(), static_cast<size_t>(i420.stride[2])});

      const auto result = encoder_->Encode(i420);
      if (!result.ok()) {
        return absl::InternalError("video_encoder: Encode failed (" +
                                   StatusName(result.status()) + ")");
      }
      EmitPending(ctx);
    }
  }

  if (in.IsDone() && !flushed_) {
    flushed_ = true;
    if (encoder_) {
      const auto result = encoder_->Flush();
      if (!result.ok()) {
        return absl::InternalError("video_encoder: Flush failed (" +
                                   StatusName(result.status()) + ")");
      }
      EmitPending(ctx);
    }
  }
  return absl::OkStatus();
}

absl::Status VideoEncoderNode::Close(graph::runtime::GraphContext&) {
  if (encoder_) encoder_->Release();
  encoder_.reset();
  sink_.reset();
  pending_.clear();
  return absl::OkStatus();
}

namespace { using media::record::VideoEncoderNode; }
GRAPH_RUNTIME_REGISTER_NODE("VideoEncoderNode", VideoEncoderNode);

}  // namespace media::record
