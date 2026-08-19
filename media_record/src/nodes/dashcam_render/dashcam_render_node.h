#pragma once

#include <memory>
#include <string>
#include <vector>

#include "graph_runtime/node.h"
#include "native_ui/surface.h"
#include "src/render/dashcam_renderer.h"

namespace media {
namespace record {

// DashcamRenderNode: the single compositing node for the dashcam pipeline.
// Combines a road+sky background, an animated car (driven by frame_index), and
// a timestamp overlay (top-left) using media_record::render::DashcamRenderer,
// which is built on native::ui Canvas + Surface::CreateFromPixels (zero-copy
// compositing into the caller's RGBA buffer).
//
// Self-driven (no input streams). Frames are paced to the configured FPS.
// Output: 'output:frames' -- video::codec::VideoFrame (RGBA, 1280x720 by
// default), suitable for VideoEncoderNode which performs RGBA->I420.
class DashcamRenderNode : public graph::runtime::Node {
 public:
  DashcamRenderNode(const std::string& name,
                    const graph::runtime::NodeOptions& options);

  static absl::Status GetContract(graph::runtime::NodeContract* c);
  absl::Status Open(graph::runtime::GraphContext& ctx) override;
  absl::Status Close(graph::runtime::GraphContext& ctx) override;
  absl::Status Process(graph::runtime::GraphContext& ctx) override;

 private:
  std::string background_path_;
  std::string car_path_;
  std::string timestamp_format_;
  int width_ = 1280;
  int height_ = 720;
  int fps_ = 30;
  int frame_count_ = 300;

  int frame_index_ = 0;
  int64_t pacing_start_us_ = 0;
  std::unique_ptr<render::DashcamRenderer> renderer_;
  // External-owned RGBA frame buffer (allocated by Surface::Allocate).
  native::ui::PixelBuffer frame_;
};

}  // namespace record
}  // namespace media