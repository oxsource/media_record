#pragma once

#include <memory>
#include <string>
#include <vector>

#include "native_ui/surface.h"

#if defined(__ANDROID__)
#include "native_ui/render_context.h"
#endif

namespace native::ui {
class Canvas;  // fwd: DrawFrame takes a Canvas& (Skia isolated in the .cc)
}  // namespace native::ui

namespace media {
namespace record {
namespace render {

// DashcamRenderer: composes a single RGBA frame for the dashcam pipeline. It
// draws a road+sky background, an animated dog (flydog) bouncing up and down
// based on frame_index, and a timestamp overlay (top-left, larger font).
//
// Memory ownership is EXTERNAL (the caller owns the pixel buffer). Surface
// decides how much to allocate via Surface::Allocate(width, height,
// kRGBA); the caller hands that buffer to Render(), which wraps it with
// Surface::CreateFromPixels so the canvas draws straight into it (zero-copy,
// no readback). The caller keeps the buffer alive across the frame and reuses
// it for the VideoFrame handed to the codec.
//
// It is intentionally a single reusable unit so that:
//  - the dashcam graph node reuses it directly,
//  - the examples/standalone debug program drives it without graph_runtime.
//
// Inputs to Render(): frame_index drives the dog's vertical bounce (a
// frame-driven sine wave); the timestamp string is drawn as-is.
class DashcamRenderer {
 public:
  // Loads the background and dog images from disk and pre-sizes them for the
  // target output dimensions. Returns nullptr on asset load failure.
  static std::unique_ptr<DashcamRenderer> Create(
      const std::string& background_image_path,
      const std::string& dog_image_path,
      int target_width, int target_height);

  ~DashcamRenderer();

  DashcamRenderer(const DashcamRenderer&) = delete;
  DashcamRenderer& operator=(const DashcamRenderer&) = delete;

  int width() const { return target_width_; }
  int height() const { return target_height_; }

  // Composes one frame into `buffer` (caller-owned, from
  // native::ui::Surface::Allocate). The canvas draws directly into
  // buffer.data via Surface::CreateFromPixels (zero-copy). Returns false on
  // asset/setup/size mismatch.
  bool Render(int frame_index, const std::string& timestamp,
              native::ui::PixelBuffer& buffer);

#if defined(__ANDROID__)
  // Android/surface mode: composes one frame directly onto the MediaCodec
  // encoder input surface hosted by `ctx` (Surface::Create(ctx), FBO 0).
  // Background is imported zero-copy via Surface::CreateFromBuffer(kGPU) when
  // available (else falls back to CPU raster). Same bounce/timestamp logic as
  // the CPU path; the frame is flushed + swapped (presented) to the encoder
  // inside this call, with `pts_us` stamped on the input surface so MediaCodec
  // produces monotonic dts/pts. Returns false on setup/render failure.
  bool Render(int frame_index, const std::string& timestamp, int64_t pts_us,
              native::ui::RenderContext* ctx);
#endif

 private:
  DashcamRenderer(int target_width, int target_height);

  // Draws the common scene (clear + background + bouncing dog + timestamp) onto
  // `canvas`. Shared by the CPU (PixelBuffer) and Android (RenderContext
  // surface) Render paths — only the canvas acquisition differs between them.
  bool DrawFrame(native::ui::Canvas& canvas, int frame_index,
                 const std::string& timestamp);

  int target_width_ = 0;
  int target_height_ = 0;

  // native::ui types are forward-declared in the .cc to keep Skia out of this
  // header (Skia isolation).
  struct Context;
  std::unique_ptr<Context> ctx_;
};

}  // namespace render
}  // namespace record
}  // namespace media