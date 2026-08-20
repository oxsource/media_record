#pragma once

#include <memory>
#include <string>
#include <vector>

#include "graph_runtime/node.h"
#include "native_ui/surface.h"
#include "src/framework/lifecycle/lifecycle_context.h"
#include "src/render/dashcam_renderer.h"

#if defined(__ANDROID__)
#include "native_ui/render_context.h"
#endif

namespace media {
namespace record {

// DashcamRenderNode: the single compositing node for the dashcam pipeline.
// Combines a road+sky background, an animated car (driven by frame_index), and
// a timestamp overlay (top-left) using media_record::render::DashcamRenderer.
//
// Self-driven (no input streams). Frames are paced to the configured FPS.
// Two output modes (spec 003), selected by the `input_surface` NodeOptions flag
// (surface mode only valid on Android):
//   - CPU (default, host AND Android): composes into a caller-owned RGBA
//     PixelBuffer and emits video::codec::VideoFrame on 'output:frames' for
//     VideoEncoderNode. On Android this is the CPU-memory render+encode path
//     (input_surface unset/false).
//   - Android surface mode (input_surface=true): lazily (first Process) reads
//     the encoder's input surface from LifecycleContext::input_surface, hosts a
//     RenderContext on it, and composes directly onto the surface (GPU) via
//     DashcamRenderer::Render(ctx); emits PacketNotify (not a CPU frame).
class DashcamRenderNode : public graph::runtime::Node {
 public:
  DashcamRenderNode(const std::string& name,
                    const graph::runtime::NodeOptions& options);

  static absl::Status GetContract(graph::runtime::NodeContract* c);
  absl::Status Open(graph::runtime::GraphContext& ctx) override;
  absl::Status Close(graph::runtime::GraphContext& ctx) override;
  absl::Status Process(graph::runtime::GraphContext& ctx) override;

 private:
#if defined(__ANDROID__)
  // Lazy: acquire the encoder input surface from LifecycleContext, host a
  // RenderContext on it, and build the surface-mode renderer (first Process).
  absl::Status EnsureSurfaceRenderer(graph::runtime::GraphContext& ctx);
#endif

  std::string background_path_;
  std::string car_path_;
  std::string timestamp_format_;
  int width_ = 1280;
  int height_ = 720;
  int fps_ = 30;
  int frame_count_ = 300;
  bool surface_mode_ = false;

  int frame_index_ = 0;
  int64_t pacing_start_us_ = 0;
  std::unique_ptr<render::DashcamRenderer> renderer_;
  // External-owned RGBA frame buffer (allocated by Surface::Allocate).
  native::ui::PixelBuffer frame_;
#if defined(__ANDROID__)
  // LifecycleContext pointer captured in Open() (side packets are available
  // there, but not in async Process contexts); used by EnsureSurfaceRenderer.
  media::record::LifecycleContext* lifecycle_ctx_ = nullptr;
  std::unique_ptr<render::DashcamRenderer> surface_renderer_;
  std::unique_ptr<native::ui::RenderContext> render_ctx_;
#endif
};

}  // namespace record
}  // namespace media