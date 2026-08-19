// Bitmap-font timestamp renderer unit tests (spec 002, T009).
//
// Two different clock times must produce different pixels, the rendered width
// must match MeasureWidth, and drawing must not overrun the buffer bounds.

#include <cstdint>
#include <string>
#include <vector>

#include "gtest/gtest.h"
#include "src/nodes/ui_overlay/bitmap_font.h"

namespace media::record {
namespace {

constexpr uint32_t kFg = 0xFFFFFFFFu;  // white
constexpr uint32_t kBg = 0x000000A0u;  // semi-transparent black

std::vector<uint8_t> MakeCanvas(int w, int h) {
  return std::vector<uint8_t>(static_cast<size_t>(w) * h * 4, 0);
}

TEST(BitmapFontTest, MeasureWidthMatchesGlyphCount) {
  EXPECT_EQ(BitmapFont::MeasureWidth("12:00"),
            5 * 5 + 4 * BitmapFont::kGlyphSpacing);  // 5 glyphs
  EXPECT_EQ(BitmapFont::MeasureWidth(""), 0);
}

TEST(BitmapFontTest, DifferentTimesProduceDifferentPixels) {
  const int w = 160;
  const int h = 32;
  auto a = MakeCanvas(w, h);
  auto b = MakeCanvas(w, h);
  BitmapFont::Draw(a.data(), w, h, 8, 8, "12:00", kFg, kBg);
  BitmapFont::Draw(b.data(), w, h, 8, 8, "12:01", kFg, kBg);
  EXPECT_NE(a, b);
}

TEST(BitmapFontTest, DrawsPixelsInExpectedRegion) {
  const int w = 160;
  const int h = 32;
  auto canvas = MakeCanvas(w, h);
  BitmapFont::Draw(canvas.data(), w, h, 8, 8, "8", kFg, 0);
  bool any_set = false;
  for (int y = 8; y < 8 + BitmapFont::kGlyphHeight; ++y) {
    for (int x = 8; x < 8 + BitmapFont::kGlyphWidth; ++x) {
      const size_t i = (static_cast<size_t>(y) * w + x) * 4;
      if (canvas[i] != 0) any_set = true;
    }
  }
  EXPECT_TRUE(any_set);
}

TEST(BitmapFontTest, OutOfBoundsDrawingIsClipped) {
  const int w = 20;
  const int h = 20;
  auto canvas = MakeCanvas(w, h);
  // Drawing far out of bounds must not write outside the buffer (would ASAN).
  BitmapFont::Draw(canvas.data(), w, h, -100, -100, "1234567890", kFg, kBg);
  BitmapFont::Draw(canvas.data(), w, h, 1000, 1000, "1234567890", kFg, kBg);
  SUCCEED();
}

}  // namespace
}  // namespace media::record
