#ifndef MEDIA_RECORD_NODES_UI_OVERLAY_BITMAP_FONT_H_
#define MEDIA_RECORD_NODES_UI_OVERLAY_BITMAP_FONT_H_

#include <cstdint>
#include <string>

// Software bitmap font renderer for the OSD timestamp (spec 002).
//
// native_ui host Surface has no pixel readback, so the timestamp text is drawn
// into media_record's own RGBA buffer with an embedded 5x7 glyph table (digits
// + '-', ' ', ':'). White-on-semi-transparent-black keeps it legible on any
// background. Deterministic and unit-testable (research.md §4).

namespace media::record {

class BitmapFont {
 public:
  static constexpr int kGlyphWidth = 5;
  static constexpr int kGlyphHeight = 7;
  static constexpr int kGlyphSpacing = 1;
  static constexpr int kPadding = 4;

  // Pixel width needed to render |text| (glyph width + spacing between glyphs).
  static int MeasureWidth(const std::string& text);
  static int Height() { return kGlyphHeight + 2 * kPadding; }

  // Draws |text| at (x, y) into an RGBA buffer (row stride = frame_w * 4).
  // |fg| / |bg| are packed RGBA; when |bg| is nonzero a padded background
  // rectangle is drawn first. Out-of-bounds pixels are clipped.
  static void Draw(uint8_t* rgba, int frame_w, int frame_h, int x, int y,
                   const std::string& text, uint32_t fg, uint32_t bg);

 private:
  // 5x7 glyph bitmaps for '0'-'9', '-', ' ', ':'; nullptr when unsupported.
  static const uint8_t* Glyph(char c);
};

}  // namespace media::record

#endif  // MEDIA_RECORD_NODES_UI_OVERLAY_BITMAP_FONT_H_
