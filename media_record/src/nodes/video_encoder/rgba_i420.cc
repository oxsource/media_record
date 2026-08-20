#include "src/nodes/video_encoder/rgba_i420.h"

#include "libyuv/convert.h"

namespace media::record {

void RgbaToI420(const RgbaToI420Input& in, const I420Output& out) {
  const int width = in.width;
  const int height = in.height;
  // libyuv ABGRToI420 is SIMD-accelerated (NEON on aarch64); the previous
  // scalar loop cost ~52ms/frame at 1280x720 on device, dominating the CPU
  // pipeline. NOTE: our in-memory layout is R,G,B,A which is libyuv's "ABGR"
  // pixel (uint32 0xAABBGGRR little-endian) — NOT ARGB (0xAARRGGBB = B,G,R,A
  // in memory). Same BT.601 coefficients as the scalar version.
  libyuv::ABGRToI420(in.rgba, in.rgba_stride, out.y, out.y_stride, out.u,
                     out.u_stride, out.v, out.v_stride, width, height);
}

}  // namespace media::record
