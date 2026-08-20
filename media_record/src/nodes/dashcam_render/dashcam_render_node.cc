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
#include "src/framework/lifecycle/lifecycle_context.h"
#include "src/render/dashcam_renderer.h"
#include "video_codec/video_codec.h"

#if defined(__ANDROID__)
#include "native_ui/render_context.h"
#endif

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
  if (const bool* v = options.Get<bool>("input_surface")) surface_mode_ = *v;
  if (fps_ <= 0) fps_ = 30;
  if (frame_count_ <= 0) frame_count_ = 300;
}

// The output stream carries a CPU VideoFrame (host AND Android CPU mode) OR a
// PacketNotify (Android surface mode) through the same port. The contract does
// not pin a concrete type (SetNone): both payloads legitimately flow here
// depending on the configured mode, and graph_runtime's best-effort runtime
// type check would otherwise warn (cosmetic only) for whichever type is not
// declared. Process() branches on surface_mode_ (Android + input_surface=true
// -> surface path; otherwise -> CPU path).
absl::Status DashcamRenderNode::GetContract(graph::runtime::NodeContract* c) {
  c->Outputs().Get("output").SetNone();
  return absl::OkStatus();
}

absl::Status DashcamRenderNode::Open(graph::runtime::GraphContext& ctx) {
  if (background_path_.empty() || car_path_.empty()) {
    return absl::InvalidArgumentError(
        "dashcam_render: 'background' and 'dog' image paths are required "
        "(NodeOptions)");
  }
#if defined(__ANDROID__)
  if (surface_mode_) {
    // Surface mode: renderer + RenderContext are built lazily on first
    // Process() (the encoder's input surface is written to LifecycleContext
    // during the encoder's Open, which completes before any source Process).
    // Capture the LifecycleContext pointer NOW: side packets ARE available in
    // Open(), but async Process contexts are built without them (graph_runtime
    // SchedulerQueue::RunNode), so EnsureSurfaceRenderer must read the pointer
    // from this member, not ctx.InputSidePackets().
    const graph::runtime::Packet sp =
        ctx.InputSidePackets().Get(media::record::LifecycleContext::kSidePacketTag);
    if (!sp.IsEmpty()) {
      auto lc = sp.Get<media::record::LifecycleContext*>();
      if (lc.ok()) lifecycle_ctx_ = lc.value();
    }
    frame_index_ = 0;
    pacing_start_us_ = NowUs();
    return absl::OkStatus();
  }
#endif
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

#if defined(__ANDROID__)
// Surface mode: lazily (first Process) acquire the encoder input surface from
// the shared LifecycleContext, host a RenderContext on it, and build the
// surface-mode DashcamRenderer. Called once; subsequent frames reuse the setup.
absl::Status DashcamRenderNode::EnsureSurfaceRenderer(
    graph::runtime::GraphContext&) {
  if (surface_renderer_) return absl::OkStatus();

  // Use the LifecycleContext pointer captured in Open() — async Process
  // contexts do not carry input side packets (graph_runtime
  // SchedulerQueue::RunNode builds a fresh context), so reading the pointer
  // back from ctx.InputSidePackets() here would always find it empty.
  void* input_surface = nullptr;
  if (lifecycle_ctx_) input_surface = lifecycle_ctx_->input_surface;
  if (!input_surface) {
    return absl::InternalError(
        "dashcam_render: encoder input surface not ready (LifecycleContext "
        "'input_surface')");
  }

  render_ctx_ = native::ui::RenderContext::CreateFromNativeWindow(
      input_surface, width_, height_);
  if (!render_ctx_) {
    return absl::InternalError(
        "dashcam_render: RenderContext::CreateFromNativeWindow failed");
  }
  surface_renderer_ = render::DashcamRenderer::Create(
      background_path_, car_path_, width_, height_);
  if (!surface_renderer_) {
    return absl::InvalidArgumentError(
        "dashcam_render: failed to load/scale background/dog images "
        "(background='" +
        background_path_ + "', dog='" + car_path_ + "')");
  }
  return absl::OkStatus();
}
#endif

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
    std::this_thread::sleep_for(std::chrono::microseconds(target_us - now_us));
  }

  const std::string timestamp = FormatTimestamp(timestamp_format_);
  // PTS for this frame in µs, starting at 1/fps (a zero PTS can be rejected by
  // some MediaCodec encoders — video_codec's surface example uses (i+1)*1e6/fps).
  const int64_t pts = (frame_index_ + 1) * 1000000LL / fps_;

#if defined(__ANDROID__)
  if (surface_mode_) {
    // Android/surface mode: lazily build the surface renderer (reads the
    // encoder input surface from LifecycleContext), compose directly onto the
    // encoder input surface (GPU), then notify the encoder to Poll(). No CPU
    // VideoFrame is produced (spec 003: nodes don't exchange CPU frames here).
    absl::Status status = EnsureSurfaceRenderer(ctx);
    if (!status.ok()) return status;
    if (!surface_renderer_->Render(frame_index_, timestamp, pts,
                                   render_ctx_.get())) {
      return absl::InternalError(
          "dashcam_render: surface DashcamRenderer::Render failed");
    }
    media::record::PacketNotify notify;
    notify.timestamp_us = NowUs();
    auto pkt = graph::runtime::Packet::MakePacket<media::record::PacketNotify>(
                   std::move(notify))
                   .At(graph::runtime::Timestamp(pts));
    ctx.Outputs().Get("output").AddPacket(std::move(pkt));
    ++frame_index_;
    return absl::OkStatus();
  }
#endif

  // Compose one dashcam frame into the external-owned buffer (zero-copy via
  // Surface::CreateFromPixels). We then copy into the VideoFrame's own plane
  // because VideoFrame owns its pixel data.
  if (!renderer_->Render(frame_index_, timestamp, frame_)) {
    return absl::InternalError("dashcam_render: DashcamRenderer::Render failed");
  }

  video::codec::VideoFrame frame;
  frame.format = video::codec::PixelFormat::kRGBA;
  frame.width = width_;
  frame.height = height_;
  frame.stride[0] = width_ * 4;
  frame.planes[0] = frame_.data;
  frame.timestamp_us = NowUs();

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