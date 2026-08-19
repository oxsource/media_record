#ifndef MEDIA_RECORD_NODES_VIDEO_ENCODER_RGBA_I420_H_
#define MEDIA_RECORD_NODES_VIDEO_ENCODER_RGBA_I420_H_

#include <cstddef>
#include <cstdint>

// Software BT.601 RGBA -> I420 converter (spec 002).
//
// The video_codec FFmpeg backend only accepts I420 / NV12 input
// (ffmpeg_video.cc: kRGBA -> kUnsupportedFormat), so frames must be converted
// before Encode(). media_record implements this conversion itself to avoid a
// third-party color-conversion dependency (dependency-contract D-3). |width|
// and |height| must be even for 4:2:0 chroma subsampling.

namespace media::record {

struct RgbaToI420Input {
  const uint8_t* rgba = nullptr;
  size_t rgba_stride = 0;  // bytes per source row (>= width * 4)
  int width = 0;
  int height = 0;
};

struct I420Output {
  uint8_t* y = nullptr;
  size_t y_stride = 0;
  uint8_t* u = nullptr;
  size_t u_stride = 0;
  uint8_t* v = nullptr;
  size_t v_stride = 0;
};

// Converts one RGBA frame. Y holds width*height bytes; U/V each hold
// (width/2)*(height/2) bytes.
void RgbaToI420(const RgbaToI420Input& in, const I420Output& out);

}  // namespace media::record

#endif  // MEDIA_RECORD_NODES_VIDEO_ENCODER_RGBA_I420_H_
