// dashcam_renderer.cc
#include "dashcam_renderer.h"

#include <cmath>
#include <cstdio>
#include <memory>
#include <utility>

#include "native_ui/core.h"
#include "native_ui/render.h"
#include "native_ui/surface.h"

#if defined(__ANDROID__)
#include "native_ui/render_context.h"
#endif

namespace media {
namespace record {
namespace render {

// Holds the renderer's persistent state: the loaded + pre-scaled images and the
// zero-copy surface/canvas pair (which wrap the caller's pixel buffer and are
// reused across frames). The native::ui types are kept inside the .cc so Skia
// does not leak into the public header (Skia isolation). Defined in this
// namespace (not an anonymous namespace) so the out-of-line definition
// `DashcamRenderer::Context` can name the enclosing class.
struct DashcamRenderer::Context {
  std::unique_ptr<native::ui::Image> background;
  std::unique_ptr<native::ui::Image> car;
  // Zero-copy surface + canvas, bound to the caller's pixel buffer. Created
  // once per buffer and reused across frames (the pipeline reuses the same
  // buffer every frame), avoiding per-frame WrapPixels + canvas setup.
  std::unique_ptr<native::ui::Surface> surface;
  std::unique_ptr<native::ui::Canvas> canvas;
  const uint8_t* surface_buffer = nullptr;  // data ptr of the buffer the surface wraps
  // Timestamp text paint — configured once and reused across frames.
  native::ui::Paint text_paint;
  // Timestamp draw position + font size — fixed once and reused across frames.
  native::ui::Point text_pos{24.0f, 48.0f};
  float text_size = 36.0f;
  // Fixed background dest rect (full frame).
  native::ui::Rect bg_dest{0.0f, 0.0f, 0.0f, 0.0f};
  // Dog draw rect — x/w/h are fixed; only y changes per frame (bounce).
  native::ui::Rect dog_dest{0.0f, 0.0f, 0.0f, 0.0f};
};

namespace {

// Dog bounce animation: computes the dog's vertical top-edge y for a given
// frame and writes it into `dog_dest` (x/w/h are already fixed at Create()).
// The dog bounces on a sine wave about a baseline at ~60% of frame height,
// amplitude 24px, one cycle per second at 30fps. |sin| keeps the dog on/above
// the baseline (no clipping below it).
void UpdateDogBounce(native::ui::Rect& dog_dest, int frame_index,
                     int target_height) {
  const int center_y = target_height * 60 / 100;   // baseline center
  const int amplitude = 24;                        // bounce amplitude (px)
  const int period = 30;                           // 1s @ 30fps
  const float phase = static_cast<float>(frame_index % period) *
                      (2.0f * static_cast<float>(M_PI) /
                       static_cast<float>(period));
  const int offset =
      -static_cast<int>(amplitude * std::fabs(std::sin(phase)));
  const int dog_h = static_cast<int>(dog_dest.height);
  dog_dest.y = static_cast<float>(center_y + offset - dog_h / 2);
}

}  // namespace

DashcamRenderer::DashcamRenderer(int target_width, int target_height)
    : target_width_(target_width), target_height_(target_height) {}

DashcamRenderer::~DashcamRenderer() = default;

std::unique_ptr<DashcamRenderer> DashcamRenderer::Create(
    const std::string& background_image_path,
    const std::string& car_image_path,
    int target_width, int target_height) {
  if (target_width <= 0 || target_height <= 0) return nullptr;

  auto bg = native::ui::Image::FromFile(background_image_path.c_str());
  if (!bg) {
    std::fprintf(stderr,
                 "DashcamRenderer: failed to load background '%s'\n",
                 background_image_path.c_str());
    return nullptr;
  }
  // Scale the background to the target dimensions.
  bg = bg->Scale(target_width, target_height);
  if (!bg) {
    std::fprintf(stderr,
                 "DashcamRenderer: failed to scale background to %dx%d\n",
                 target_width, target_height);
    return nullptr;
  }

  auto car = native::ui::Image::FromFile(car_image_path.c_str());
  if (!car) {
    std::fprintf(stderr, "DashcamRenderer: failed to load car '%s'\n",
                 car_image_path.c_str());
    return nullptr;
  }
  // Keep the car image at native resolution; Render() scales it per frame to
  // give the "near-large / far-small" perspective as the car drives away.

  auto r = std::unique_ptr<DashcamRenderer>(
      new DashcamRenderer(target_width, target_height));
  r->ctx_ = std::make_unique<Context>();
  r->ctx_->background = std::move(bg);
  r->ctx_->car = std::move(car);
  // Configure the shared timestamp paint once (white, anti-aliased).
  r->ctx_->text_paint
      .SetColor(native::ui::Color{255, 255, 255, 255})
      .SetAntiAlias(true);
  // Pre-compute the fixed draw rects once; Render() only updates the dog's y
  // each frame (bounce), so no per-frame Rect construction is needed.
  r->ctx_->bg_dest =
      native::ui::Rect{0.0f, 0.0f, static_cast<float>(target_width),
                       static_cast<float>(target_height)};
  const int dog_h = target_height * 50 / 100;
  const int dog_w = dog_h;
  const int cx = (target_width - dog_w) / 2;
  r->ctx_->dog_dest =
      native::ui::Rect{static_cast<float>(cx), 0.0f,
                       static_cast<float>(dog_w), static_cast<float>(dog_h)};
  return r;
}

bool DashcamRenderer::Render(int frame_index, const std::string& timestamp,
                             native::ui::PixelBuffer& buffer) {
  if (!ctx_) return false;

  const size_t needed =
      static_cast<size_t>(target_width_) * static_cast<size_t>(target_height_) *
      4;
  if (buffer.data.size() < needed) {
    std::fprintf(stderr,
                 "DashcamRenderer: buffer too small (%zu < %zu); allocate via "
                 "Surface::Allocate\n",
                 buffer.data.size(), needed);
    return false;
  }

  // The surface zero-copy wraps the caller's `buffer`; it does not own it. The
  // surface + canvas are created once per buffer and reused across frames (the
  // pipeline reuses the same buffer every frame), avoiding per-frame WrapPixels
  // and canvas setup. If the caller hands us a different buffer, we rebuild.
  const uint8_t* buf_ptr = buffer.data.data();
  if (!ctx_->surface || ctx_->surface_buffer != buf_ptr) {
    // Reset the canvas FIRST (it holds a raw SkCanvas pointer into the old
    // surface); otherwise its destructor would touch a freed surface.
    ctx_->canvas.reset();
    ctx_->surface = native::ui::Surface::CreateFromPixels(buffer);
    if (!ctx_->surface) {
      std::fprintf(stderr,
                   "DashcamRenderer: Surface::CreateFromPixels failed\n");
      ctx_->surface_buffer = nullptr;
      return false;
    }
    ctx_->canvas = std::make_unique<native::ui::Canvas>(*ctx_->surface);
    ctx_->surface_buffer = buf_ptr;
  }
  // Shared scene drawing (background + bouncing dog + timestamp).
  return DrawFrame(*ctx_->canvas, frame_index, timestamp);
}

// Shared scene: clear + background + bouncing dog + timestamp. Used by both the
// CPU (PixelBuffer) and Android (RenderContext surface) Render paths — the only
// thing that differs between them is how the canvas is acquired.
bool DashcamRenderer::DrawFrame(native::ui::Canvas& canvas, int frame_index,
                                const std::string& timestamp) {
  // Clear to opaque black so uncovered edges (if any) don't carry transparency
  // into the encoded video.
  canvas.Clear(native::ui::Color{0, 0, 0, 255});

  // Background: tile the loaded image across the entire frame.
  canvas.DrawImage(*ctx_->background, ctx_->bg_dest);

  // Dog (flydog): fixed size, centered horizontally; the bounce animation
  // updates dog_dest.y each frame (x/w/h were pre-computed at Create()).
  UpdateDogBounce(ctx_->dog_dest, frame_index, target_height_);
  // Scale() returns nullptr when the source is already at/below the target
  // size (native_ui "no resize needed" contract); in that case fall back to the
  // original image and let Skia scale it into the dest rect.
  auto dog_frame = ctx_->car->Scale(static_cast<int>(ctx_->dog_dest.width),
                                    static_cast<int>(ctx_->dog_dest.height));
  const native::ui::Image& to_draw = dog_frame ? *dog_frame : *ctx_->car;
  canvas.DrawImage(to_draw, ctx_->dog_dest);

  // Timestamp: top-left, large white font for readability.
  canvas.DrawText(timestamp, ctx_->text_pos, ctx_->text_paint, ctx_->text_size);
  return true;
}

#if defined(__ANDROID__)
// Android/surface mode: compose directly onto the encoder input surface.
//
// `Surface::Create(ctx)` hosts the canvas on the MediaCodec input surface
// (FBO 0, GLES/EGL, single-context rule). The canvas is created per frame
// (Skia draw state), which matches the native_ui external_image_demo pattern.
// Background/dog/timestamp share the exact same geometry + animation as the
// CPU path — only the render target differs. After drawing we flush the GPU
// work and present (SwapBuffers) so the encoder receives the frame zero-copy.
//
// NOTE: true zero-copy AHWB->GPU background import (Surface::CreateFromBuffer
// with RenderBackend::kGPU) is a later optimization; for now the GPU backend
// uploads the raster background image via Skia (correct, non zero-copy).
bool DashcamRenderer::Render(int frame_index, const std::string& timestamp,
                             native::ui::RenderContext* ctx) {
  if (!ctx_ || !ctx || !ctx->gr) return false;

  auto surface = native::ui::Surface::Create(ctx);
  if (!surface) {
    std::fprintf(stderr, "DashcamRenderer: Surface::Create(ctx) failed\n");
    return false;
  }

  native::ui::Canvas canvas(*surface);

  // Shared scene drawing (background + bouncing dog + timestamp) — same as CPU.
  if (!DrawFrame(canvas, frame_index, timestamp)) return false;

  // Submit GPU work and present the frame to the encoder.
  surface->Flush();
  ctx->SwapBuffers();
  return true;
}
#endif  // __ANDROID__

}  // namespace render
}  // namespace record
}  // namespace media