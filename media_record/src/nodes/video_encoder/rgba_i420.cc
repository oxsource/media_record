#include "src/nodes/video_encoder/rgba_i420.h"

namespace media::record {

void RgbaToI420(const RgbaToI420Input& in, const I420Output& out) {
  const int width = in.width;
  const int height = in.height;
  for (int y = 0; y < height; ++y) {
    const uint8_t* src = in.rgba + static_cast<size_t>(y) * in.rgba_stride;
    uint8_t* yrow = out.y + static_cast<size_t>(y) * out.y_stride;
    for (int x = 0; x < width; ++x) {
      const int r = src[x * 4 + 0];
      const int g = src[x * 4 + 1];
      const int b = src[x * 4 + 2];
      yrow[x] = static_cast<uint8_t>(((66 * r + 129 * g + 25 * b + 128) >> 8) + 16);
    }
    if ((y & 1) == 0) {
      const int cy = y >> 1;
      uint8_t* urow = out.u + static_cast<size_t>(cy) * out.u_stride;
      uint8_t* vrow = out.v + static_cast<size_t>(cy) * out.v_stride;
      for (int x = 0; x < width; x += 2) {
        const int r = src[x * 4 + 0];
        const int g = src[x * 4 + 1];
        const int b = src[x * 4 + 2];
        urow[x >> 1] =
            static_cast<uint8_t>(((-38 * r - 74 * g + 112 * b + 128) >> 8) + 128);
        vrow[x >> 1] =
            static_cast<uint8_t>(((112 * r - 94 * g - 18 * b + 128) >> 8) + 128);
      }
    }
  }
}

}  // namespace media::record
