// dashcam_render_node.cc
#include "dashcam_render_node.h"

#include <chrono>
#include <cstdio>
#include <ctime>
#include <thread>
#include <utility>

#include "absl/status/statusor.h"
#include "graph_runtime/node_registry.h"
#include "graph_runtime/runtime.h"
#include "src/render/dashcam_renderer.h"
#include "video_codec/video_codec.h"

namespace media {
namespace record {

namespace {

int64_t NowUs() {
  return std::chrono::duration_cast<std::chrono::microseconds>(
             std::chrono::system_clock::now().time_since_epoch())
      .count();
}

std::string FormatTimestamp(const std::string& format) {
  std::time_t t = std::time(nullptr);
  std::tm tm_buf{};
  localtime_r(&t, &tm_buf);
  char buf[64];
  if (std::strftime(buf, sizeof(buf), format.c_str(), &tm_buf) == 0) {
    return "1970-01-01 00:00:00";
  }
  return std::string(buf);
}

}  // namespace

DashcamRenderNode::DashcamRenderNode(const std::string& name,
                                     const graph::runtime::NodeOptions& options)
    : Node(name) {
  if (const std::string* v = options.Get<std::string>("background"))
    background_path_ = *v;
  if (const std::string* v = options.Get<std::string>("car")) car_path_ = *v;
  if (const std::string* v = options.Get<std::string>("format"))
    timestamp_format_ = *v;
  if (timestamp_format_.empty()) timestamp_format_ = "%Y-%m-%d %H:%M:%S";
  if (const int* v = options.Get<int>("width")) width_ = *v;
  if (const int* v = options.Get<int>("height")) height_ = *v;
  if (const int* v = options.Get<int>("fps")) fps_ = *v;
  if (const int* v = options.Get<int>("frame_count")) frame_count_ = *v;
  if (fps_ <= 0) fps_ = 30;
  if (frame_count_ <= 0) frame_count_ = 300;
}

absl::Status DashcamRenderNode::GetContract(graph::runtime::NodeContract* c) {
  c->Outputs().Get("output").Set<video::codec::VideoFrame>();
  return absl::OkStatus();
}

absl::Status DashcamRenderNode::Open(graph::runtime::GraphContext&) {
  if (background_path_.empty() || car_path_.empty()) {
    return absl::InvalidArgumentError(
        "dashcam_render: 'background' and 'dog' image paths are required "
        "(NodeOptions)");
  }
  renderer_ = render::DashcamRenderer::Create(
      background_path_, car_path_, width_, height_);
  if (!renderer_) {
    return absl::InvalidArgumentError(
        "dashcam_render: failed to load/scale background/dog images "
        "(background='" +
        background_path_ + "', dog='" + car_path_ + "')");
  }
  width_ = renderer_->width();
  height_ = renderer_->height();
  // External-owned frame buffer; Surface decides how much to allocate and
  // records dimensions/format in the returned PixelBuffer.
  frame_ = native::ui::Surface::Allocate(width_, height_,
                                             native::ui::PixelFormat::kRGBA);
  if (frame_.empty()) {
    return absl::InternalError("dashcam_render: Surface::Allocate failed");
  }
  frame_index_ = 0;
  pacing_start_us_ = NowUs();
  return absl::OkStatus();
}

absl::Status DashcamRenderNode::Close(graph::runtime::GraphContext&) {
  renderer_.reset();
  return absl::OkStatus();
}

absl::Status DashcamRenderNode::Process(graph::runtime::GraphContext& ctx) {
  // NOTE — graph termination origin: DashcamRenderNode is the sole self-driven
  // source node in this pipeline. Once the configured frame budget
  // (`frame_count_`) is exhausted, returning StatusStop() is what terminates
  // the ENTIRE graph: the runtime treats a source node's Stop as the pipeline
  // completion signal, then propagates `done` downstream and drains all
  // remaining queued packets (encoder -> recorder -> muxer) before closing
  // every node. No other node initiates shutdown — this early-return is the
  // single place the recording actually ends. (See specs/002
  // contracts/dependency-contract.md D-6.)
  if (frame_index_ >= frame_count_) {
    return graph::runtime::StatusStop();
  }

  // Frame pacing (same shape as StreamInputNode).
  const int64_t now_us = NowUs();
  const int64_t target_us =
      pacing_start_us_ + frame_index_ * 1000000LL / fps_;
  if (target_us > now_us) {
    std::this_thread::sleep_for(
        std::chrono::microseconds(target_us - now_us));
  }

  // Compose one dashcam frame into the external-owned buffer (zero-copy via
  // Surface::CreateFromPixels). We then copy into the VideoFrame's own plane
  // because VideoFrame owns its pixel data.
  const std::string timestamp = FormatTimestamp(timestamp_format_);
  if (!renderer_->Render(frame_index_, timestamp, frame_)) {
    return absl::InternalError(
        "dashcam_render: DashcamRenderer::Render failed");
  }

  video::codec::VideoFrame frame;
  frame.format = video::codec::PixelFormat::kRGBA;
  frame.width = width_;
  frame.height = height_;
  frame.stride[0] = width_ * 4;
  frame.planes[0] = frame_.data;
  frame.timestamp_us = NowUs();

  const int64_t pts = frame_index_ * 1000000LL / fps_;
  auto pkt = graph::runtime::Packet::MakePacket<video::codec::VideoFrame>(
                 std::move(frame))
                 .At(graph::runtime::Timestamp(pts));
  ctx.Outputs().Get("output").AddPacket(std::move(pkt));
  ++frame_index_;
  return absl::OkStatus();
}

namespace { using media::record::DashcamRenderNode; }
GRAPH_RUNTIME_REGISTER_NODE("DashcamRenderNode", DashcamRenderNode);

}  // namespace record
}  // namespace media