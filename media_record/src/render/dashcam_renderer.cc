// dashcam_renderer.cc
#include "dashcam_renderer.h"

#include <cmath>
#include <cstdio>
#include <memory>
#include <utility>

#include "libyuv/scale_argb.h"
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
  // Down-sampled composition buffer + surface/canvas: the scene is drawn at
  // render_* resolution here, then libyuv ARGBScale enlarges it onto the output
  // buffer. Owned by the renderer (created once in Create()).
  native::ui::PixelBuffer comp_buffer;
  std::unique_ptr<native::ui::Surface> comp_surface;
  std::unique_ptr<native::ui::Canvas> comp_canvas;
  // Timestamp text paint — configured once and reused across frames.
  native::ui::Paint text_paint;
  // Timestamp draw position + font size — fixed once and reused across frames
  // (scaled to render resolution when down-sampling).
  native::ui::Point text_pos{24.0f, 48.0f};
  float text_size = 36.0f;
  // Fixed background dest rect (full render frame).
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

DashcamRenderer::DashcamRenderer(int target_width, int target_height,
                                 int render_width, int render_height)
    : target_width_(target_width),
      target_height_(target_height),
      render_width_(render_width > 0 ? render_width : target_width),
      render_height_(render_height > 0 ? render_height : target_height) {}

DashcamRenderer::~DashcamRenderer() = default;

std::unique_ptr<DashcamRenderer> DashcamRenderer::Create(
    const std::string& background_image_path,
    const std::string& car_image_path,
    int target_width, int target_height,
    int render_width, int render_height) {
  if (target_width <= 0 || target_height <= 0) return nullptr;
  const int rw = render_width > 0 ? render_width : target_width;
  const int rh = render_height > 0 ? render_height : target_height;
  if (rw <= 0 || rh <= 0 || rw > target_width || rh > target_height) {
    return nullptr;
  }

  auto bg = native::ui::Image::FromFile(background_image_path.c_str());
  if (!bg) {
    std::fprintf(stderr,
                 "DashcamRenderer: failed to load background '%s'\n",
                 background_image_path.c_str());
    return nullptr;
  }
  // Scale the background to the RENDER dimensions (the scene is composed at
  // render resolution, then upscaled to the target on output).
  bg = bg->Scale(rw, rh);
  if (!bg) {
    std::fprintf(stderr,
                 "DashcamRenderer: failed to scale background to %dx%d\n",
                 rw, rh);
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
      new DashcamRenderer(target_width, target_height, rw, rh));
  r->ctx_ = std::make_unique<Context>();
  r->ctx_->background = std::move(bg);
  r->ctx_->car = std::move(car);
  // Configure the shared timestamp paint once (white, anti-aliased).
  r->ctx_->text_paint
      .SetColor(native::ui::Color{255, 255, 255, 255})
      .SetAntiAlias(true);
  // Pre-compute the fixed draw rects once (at RENDER resolution); Render() only
  // updates the dog's y each frame (bounce), so no per-frame Rect construction.
  r->ctx_->bg_dest =
      native::ui::Rect{0.0f, 0.0f, static_cast<float>(rw),
                       static_cast<float>(rh)};
  const int dog_h = rh * 50 / 100;
  const int dog_w = dog_h;
  const int cx = (rw - dog_w) / 2;
  r->ctx_->dog_dest =
      native::ui::Rect{static_cast<float>(cx), 0.0f,
                       static_cast<float>(dog_w), static_cast<float>(dog_h)};
  // Timestamp geometry scaled to render resolution.
  r->ctx_->text_pos = native::ui::Point{24.0f * rw / target_width,
                                        48.0f * rh / target_height};
  r->ctx_->text_size = 36.0f * rw / target_width;

  // Down-sampled composition buffer + surface/canvas (owned by the renderer).
  r->ctx_->comp_buffer =
      native::ui::Surface::Allocate(rw, rh, native::ui::PixelFormat::kRGBA);
  if (r->ctx_->comp_buffer.empty()) return nullptr;
  r->ctx_->comp_surface =
      native::ui::Surface::CreateFromPixels(r->ctx_->comp_buffer);
  if (!r->ctx_->comp_surface) return nullptr;
  r->ctx_->comp_canvas =
      std::make_unique<native::ui::Canvas>(*r->ctx_->comp_surface);
  return r;
}

bool DashcamRenderer::Render(int frame_index, const std::string& timestamp,
                             native::ui::PixelBuffer& buffer) {
  if (!ctx_ || !ctx_->comp_canvas) return false;

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

  // 1. Draw the shared scene at RENDER resolution into the off-screen comp
  //    buffer (Skia cost tracks render pixel count, not the target).
  if (!DrawFrame(*ctx_->comp_canvas, frame_index, timestamp)) return false;

  // 2. Upscale the composition to the caller's target buffer via libyuv
  //    ARGBScale (bi-linear, SIMD on aarch64). This replaces the earlier
  //    canvas-scale transform, which distorted the scene layout.
  libyuv::ARGBScale(ctx_->comp_buffer.data.data(),
                    static_cast<int>(ctx_->comp_buffer.width) * 4,
                    ctx_->comp_buffer.width, ctx_->comp_buffer.height,
                    buffer.data.data(), static_cast<int>(buffer.width) * 4,
                    buffer.width, buffer.height,
                    libyuv::kFilterBilinear);
  return true;
}

// Shared scene: clear + background + bouncing dog + timestamp. Used by both the
// CPU (PixelBuffer) and Android (RenderContext surface) Render paths — the only
// thing that differs between them is how the canvas is acquired.
bool DashcamRenderer::DrawFrame(native::ui::Canvas& canvas, int frame_index,
                                const std::string& timestamp) {
  // Clear to opaque black so uncovered edges (if any) don't carry transparency
  // into the encoded video.
  auto t0 = timer_.Begin();
  canvas.Clear(native::ui::Color{0, 0, 0, 255});
  timer_.Accumulate("clear", t0);

  // Background: tile the loaded image across the entire frame. The background
  // was pre-scaled to exactly (target_width, target_height) in Create(), and
  // bg_dest is the full frame, so this is a 1:1 blit — use DrawImage1to1 (no
  // resampling) instead of DrawImage's drawImageRect + bilinear sampling, which
  // cost ~34ms/frame at 1280x720 and dominated the render stage.
  t0 = timer_.Begin();
  canvas.DrawImage1to1(*ctx_->background,
                       native::ui::Point{ctx_->bg_dest.x, ctx_->bg_dest.y});
  timer_.Accumulate("background", t0);

  // Dog (flydog): fixed size, centered horizontally; the bounce animation
  // updates dog_dest.y each frame (x/w/h were pre-computed at Create()).
  // DrawImage(image, dest) scales at draw time (drawImageRect) — no per-frame
  // Scale() allocation.
  UpdateDogBounce(ctx_->dog_dest, frame_index, render_height_);
  t0 = timer_.Begin();
  canvas.DrawImage(*ctx_->car, ctx_->dog_dest);
  timer_.Accumulate("dog", t0);

  // Timestamp: top-left, large white font for readability.
  t0 = timer_.Begin();
  canvas.DrawText(timestamp, ctx_->text_pos, ctx_->text_paint, ctx_->text_size);
  timer_.Accumulate("timestamp", t0);
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
// NOTE: the background is drawn from the raster image (GPU backend uploads it
// via Skia — correct, non zero-copy). Using an AHardwareBuffer-backed
// ExternalImage for the background (to simulate camera image data) is deferred
// to a later iteration (spec 003 follow-up); this keeps the shared DrawFrame
// path intact for both platforms.
bool DashcamRenderer::Render(int frame_index, const std::string& timestamp,
                             int64_t pts_us, native::ui::RenderContext* ctx) {
  if (!ctx_ || !ctx || !ctx->gr) return false;

  auto surface = native::ui::Surface::Create(ctx);
  if (!surface) {
    std::fprintf(stderr, "DashcamRenderer: Surface::Create(ctx) failed\n");
    return false;
  }

  native::ui::Canvas canvas(*surface);

  // Shared scene drawing (background + bouncing dog + timestamp) — same as CPU.
  if (!DrawFrame(canvas, frame_index, timestamp)) return false;

  // Submit GPU work and present the frame to the encoder. Stamp the
  // presentation timestamp (µs -> ns) on the input surface FIRST so MediaCodec
  // produces monotonic dts/pts (spec 003 D.6); without it the encoder receives
  // a stale/system timestamp and most input-surface frames are dropped.
  surface->Flush();
  if (!ctx->SetPresentationTimeNs(pts_us * 1000)) {
    std::fprintf(stderr,
                 "DashcamRenderer: eglPresentationTimeANDROID unavailable "
                 "(non-monotonic encoder timestamps likely)\n");
  }
  ctx->SwapBuffers();
  return true;
}
#endif  // __ANDROID__

}  // namespace render
}  // namespace record
}  // namespace media