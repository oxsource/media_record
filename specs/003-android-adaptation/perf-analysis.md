# Performance Analysis: Dashcam Pipeline On-Device Timing

**Date**: 2026-08-20
**Scope**: Android CPU-mode dashcam recording (1280x720@30fps), device `an4009056e01d0d04` (Amlogic be11, 1.9GHz ARM, NEON `fp asimd`, no i8mm).

## 1. Instrumentation

Permanent per-stage timing lives in `src/framework/debug/perf_timer.h` (`StageTimer`,
header-only). Each dashcam node timestamps its work boundaries and prints a
summary (total / avg / max / count, wall clock) **plus the set of OS thread ids
it ran on** in `Close()`:

- `DashcamRenderNode` → `render` (node-level render + copy), `renderer` (internal
  Skia steps: clear / background / dog / timestamp)
- `VideoEncoderNode` → `convert` (RGBA→I420 via libyuv), `encode` (MediaCodec)
- `MuxerSinkNode` → `push` (Muxer::Push)

The thread-id set makes pipeline parallelism visible: distinct thread ids across
nodes prove the multi-executor config is actually running stages on different
threads. Cost: one `steady_clock` read per boundary + one stderr line per node
at shutdown.

Sample output (60 frames, `dashcam_record_android_parallel.json`):

```
[render] timing:
  render      total   2613ms  avg   43566us  max     46ms  x60
  threads:  510625168560  (1 thread)
[renderer] timing:
  background  total   2051ms  avg   34193us  max     36ms  x60
  clear       total    183ms  avg    3053us  max      3ms  x60
  dog         total    365ms  avg    6098us  max      6ms  x60
  timestamp   total     10ms  avg     174us  max      1ms  x60
  threads:  510625168560  (1 thread)
[encoder] timing:
  ... convert/encode ...  threads: <id A> <id B>  (2 threads)
[muxer_sink] timing:
  push  ...  threads: <id C>  (1 thread)
```

## 2. Measured per-frame costs (CPU mode, 1280x720)

| Stage | avg / frame | notes |
|-------|-------------|-------|
| render — Skia raster draw | **43.6ms** | dominates the pipeline |
| ├ clear | 3.1ms | |
| ├ **background** (DrawImage) | **34.2ms** | **78% of render** |
| ├ dog (DrawImage, scaled) | 6.1ms | |
| └ timestamp (DrawText) | 0.2ms | |
| encoder — convert (libyuv ABGRToI420) | ~38ms | NEON; ~52ms scalar before |
| encoder — encode (MediaCodec CPU) | ~15ms | |
| muxer — push (FFmpeg) | ~0.2ms | buffered; disk write in Finish |

Wall clock: ~91–106ms/frame (6.4s for 60 frames). Sum of stages ≈ 54ms, so
~40ms/frame is scheduler/inter-stage queueing.

## 3. Bottleneck findings

1. **background draw is the render bottleneck (34ms = 78% of render).**
   The background is pre-scaled to exactly (1280x720) in `Create()`, so the draw
   is a 1:1 blit. Replacing `DrawImage`'s `drawImageRect` + bilinear sampling
   with a no-resample `DrawImage1to1` (new native_ui Canvas API, `drawImage`)
   did **not** reduce the time (~34ms → ~34ms): the cost is Skia CPU-raster
   pixel throughput for a full 1280x720 RGBA frame (3.7MB), not sampling.

2. **Render cost scales with pixel count.** Reducing the target to 640x360
   (¼ pixels) drops render to **11.6ms** and background to **8.5ms** (both ≈¼).
   → The only effective render optimization is down-sampled composition
   (draw at 640x360, upscale to 1280x720), at the cost of an upscale pass.

3. **Frame pacing adds zero time in CPU mode.** `sleep_for` only fires when
   per-frame work is FASTER than the frame budget (33ms @ 30fps); with render at
   43ms, `target_us <= now_us` always, so pacing is a no-op (verified: 0 sleeps).

4. **Pipeline parallelism is real but bounded.** render / encoder / muxer run on
   different executor threads (encoder shows 2 thread ids). Throughput is capped
   by the slowest stage chain (render 43ms serializes against encoder ~53ms that
   itself parallelizes across 2 threads). Multi-executor config does not beat
   single-executor wall time because the CPU encoder is the hard floor.

## 4. Down-sampled rendering (implemented 2026-08-20)

`DashcamRenderer::Create` accepts optional `render_width`/`render_height`
(internal composition resolution; 0 = full target). When set smaller than the
target, the CPU `Render()` applies a `Canvas::Scale(tw/rw, th/rh)` transform so
the whole scene is drawn upscaled in one canvas transform — no separate
composition buffer, and per-draw cost tracks the render pixel count.

Config-driven via the render node's `render_width`/`render_height` NodeOptions;
the parallel android config uses 640x360 → 1280x720.

Measured (device, 60 frames):

| metric | 1280x720 draw | 640x360 + upscale |
|--------|---------------|-------------------|
| render node | 43.6ms | **3.5ms** |
| background | 34.2ms | 0.3ms |
| dog | 6.1ms | 0.1ms |
| clear | 3.1ms | 2.8ms (full-frame, unchanged) |
| output | 1280x720 | 1280x720 (decode-clean) |

Render is no longer the bottleneck; the CPU encoder (convert ~38ms + encode
~15ms ≈ 53ms) is now the hard floor, so wall time per frame stays ~53-106ms
depending on queueing. Clear is the largest remaining render cost (2.8ms).

## 5. Optimization directions (not yet implemented)

- **clear skip**: the first full-frame clear is ~2.8ms; the background already
  covers the whole frame, so the clear could be dropped when the background is
  opaque and full-frame (saves ~3ms/frame).
- **GPU surface mode** is already the production fast path (40ms/frame incl.
  encode, GPU-assisted).
- Reducing output resolution / fps is the simplest lever for CPU mode.

## 6. Historical improvements

| change | effect |
|--------|--------|
| scalar RGBA→I420 → libyuv `ABGRToI420` | convert 52ms → 38ms |
| remove per-frame `Image::Scale()` for dog | per-frame allocation removed |
| graph_runtime source self re-scheduling (MediaPipe EndScheduling) | frame interval 245ms → ~47ms (pipeline overlap) |
| graph_runtime: retain non-default executors + thread-safe InputStreamManager | multi-executor config no longer segfaults |
| down-sampled render (640x360 → 1280x720 canvas scale) | render 43.6ms → 3.5ms |

## 7. How to reproduce

```
make android-verify          # default: parallel config, CPU mode, timings printed
make android-verify SURFACE=true   # surface mode
```
Timing summaries print to stderr at graph close; `make android-verify` also
reports the device wall time via `time` in the verify script.
