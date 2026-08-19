#include "src/nodes/ui_overlay/bitmap_font.h"

#include <cstddef>

namespace media::record {

namespace {

// 5x7 glyphs, one row per byte (bit 4..0 = x 0..4). Standard numeric font.
constexpr uint8_t kDigitGlyphs[10][7] = {
    {0b01110, 0b10001, 0b10011, 0b10101, 0b11001, 0b10001, 0b01110},  // 0
    {0b00100, 0b01100, 0b00100, 0b00100, 0b00100, 0b00100, 0b01110},  // 1
    {0b01110, 0b10001, 0b00001, 0b00010, 0b00100, 0b01000, 0b11111},  // 2
    {0b11111, 0b00010, 0b00100, 0b00010, 0b00001, 0b10001, 0b01110},  // 3
    {0b00010, 0b00110, 0b01010, 0b10010, 0b11111, 0b00010, 0b00010},  // 4
    {0b11111, 0b10000, 0b11110, 0b00001, 0b00001, 0b10001, 0b01110},  // 5
    {0b00110, 0b01000, 0b10000, 0b11110, 0b10001, 0b10001, 0b01110},  // 6
    {0b11111, 0b00001, 0b00010, 0b00100, 0b01000, 0b01000, 0b01000},  // 7
    {0b01110, 0b10001, 0b10001, 0b01110, 0b10001, 0b10001, 0b01110},  // 8
    {0b01110, 0b10001, 0b10001, 0b01111, 0b00001, 0b00010, 0b01100},  // 9
};

constexpr uint8_t kDashGlyph[7] = {0b00000, 0b00000, 0b00000, 0b01110,
                                   0b00000, 0b00000, 0b00000};
constexpr uint8_t kSpaceGlyph[7] = {0, 0, 0, 0, 0, 0, 0};
constexpr uint8_t kColonGlyph[7] = {0b00000, 0b00100, 0b00100, 0b00000,
                                    0b00100, 0b00100, 0b00000};

void PutPixel(uint8_t* rgba, int frame_w, int frame_h, int x, int y, uint32_t fg) {
  if (x < 0 || y < 0 || x >= frame_w || y >= frame_h) return;
  uint8_t* p = rgba + (static_cast<size_t>(y) * frame_w + x) * 4;
  p[0] = static_cast<uint8_t>(fg >> 24);
  p[1] = static_cast<uint8_t>(fg >> 16);
  p[2] = static_cast<uint8_t>(fg >> 8);
  p[3] = static_cast<uint8_t>(fg);
}

}  // namespace

int BitmapFont::MeasureWidth(const std::string& text) {
  if (text.empty()) return 0;
  return static_cast<int>(text.size()) * kGlyphWidth +
         static_cast<int>(text.size() - 1) * kGlyphSpacing;
}

const uint8_t* BitmapFont::Glyph(char c) {
  if (c >= '0' && c <= '9') return kDigitGlyphs[c - '0'];
  if (c == '-') return kDashGlyph;
  if (c == ' ') return kSpaceGlyph;
  if (c == ':') return kColonGlyph;
  return nullptr;
}

void BitmapFont::Draw(uint8_t* rgba, int frame_w, int frame_h, int x, int y,
                      const std::string& text, uint32_t fg, uint32_t bg) {
  if (rgba == nullptr || text.empty()) return;

  if (bg != 0) {
    const int bw = MeasureWidth(text) + 2 * kPadding;
    const int bh = kGlyphHeight + 2 * kPadding;
    for (int py = y - kPadding; py < y - kPadding + bh; ++py) {
      for (int px = x - kPadding; px < x - kPadding + bw; ++px) {
        PutPixel(rgba, frame_w, frame_h, px, py, bg);
      }
    }
  }

  int cx = x;
  for (char c : text) {
    const uint8_t* glyph = Glyph(c);
    if (glyph != nullptr) {
      for (int row = 0; row < kGlyphHeight; ++row) {
        for (int col = 0; col < kGlyphWidth; ++col) {
          if (glyph[row] & (1 << (4 - col))) {
            PutPixel(rgba, frame_w, frame_h, cx + col, y + row, fg);
          }
        }
      }
    }
    cx += kGlyphWidth + kGlyphSpacing;
  }
}

}  // namespace media::record
