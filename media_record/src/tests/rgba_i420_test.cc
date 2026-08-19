// Software RGBA -> I420 conversion unit tests (spec 002, T008).
//
// A pure-color RGBA frame converts to known BT.601 luma/chroma values: pure
// red (255,0,0) -> Y=82, U=90, V=240 (see rgba_i420.cc for the constants).

#include <cstdint>
#include <vector>

#include "gtest/gtest.h"
#include "src/nodes/video_encoder/rgba_i420.h"

namespace media::record {
namespace {

TEST(RgbaToI420Test, PureRedConvertsToKnownValues) {
  const int w = 8;
  const int h = 8;
  std::vector<uint8_t> rgba(static_cast<size_t>(w) * h * 4, 0);
  for (auto& byte : rgba) byte = 0;
  for (size_t i = 0; i < rgba.size(); i += 4) {
    rgba[i + 0] = 255;  // R
    rgba[i + 1] = 0;    // G
    rgba[i + 2] = 0;    // B
    rgba[i + 3] = 255;  // A
  }

  std::vector<uint8_t> y(static_cast<size_t>(w) * h, 0);
  std::vector<uint8_t> u(static_cast<size_t>(w) * h / 4, 0);
  std::vector<uint8_t> v(static_cast<size_t>(w) * h / 4, 0);

  RgbaToI420({rgba.data(), static_cast<size_t>(w) * 4, w, h},
             {y.data(), static_cast<size_t>(w), u.data(),
              static_cast<size_t>(w) / 2, v.data(),
              static_cast<size_t>(w) / 2});

  for (uint8_t byte : y) EXPECT_EQ(byte, 82);
  for (uint8_t byte : u) EXPECT_EQ(byte, 90);
  for (uint8_t byte : v) EXPECT_EQ(byte, 240);
}

TEST(RgbaToI420Test, ChromaPlanesAreQuarterSize) {
  const int w = 8;
  const int h = 4;
  std::vector<uint8_t> rgba(static_cast<size_t>(w) * h * 4, 0);
  std::vector<uint8_t> y(static_cast<size_t>(w) * h, 0);
  std::vector<uint8_t> u(static_cast<size_t>(w) * h / 4, 0);
  std::vector<uint8_t> v(static_cast<size_t>(w) * h / 4, 0);
  RgbaToI420({rgba.data(), static_cast<size_t>(w) * 4, w, h},
             {y.data(), static_cast<size_t>(w), u.data(),
              static_cast<size_t>(w) / 2, v.data(),
              static_cast<size_t>(w) / 2});
  EXPECT_EQ(y.size(), static_cast<size_t>(w) * h);
  EXPECT_EQ(u.size(), static_cast<size_t>(w) * h / 4);
  EXPECT_EQ(v.size(), static_cast<size_t>(w) * h / 4);
}

}  // namespace
}  // namespace media::record
