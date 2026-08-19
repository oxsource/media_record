#include "src/nodes/ui_overlay/ui_overlay_node.h"

#include <chrono>
#include <ctime>
#include <memory>
#include <string>

#include "native_ui/widgets.h"
#include "src/framework/transport/packet.h"
#include "src/framework/transport/recording_defaults.h"
#include "src/nodes/ui_overlay/bitmap_font.h"
#include "video_codec/video_codec.h"

namespace media::record {

namespace {

constexpr uint32_t kFgColor = 0xFFFFFFFFu;  // white
constexpr uint32_t kBgColor = 0x000000A0u;  // semi-transparent black

std::string FormatTimestamp(const std::string& format) {
  const std::time_t now = std::time(nullptr);
  std::tm tm{};
#if defined(_WIN32)
  localtime_s(&tm, &now);
#else
  localtime_r(&now, &tm);
#endif
  char buf[64] = {0};
  std::strftime(buf, sizeof(buf), format.c_str(), &tm);
  return std::string(buf);
}

}  // namespace

NodeStatus UiOverlayNode::Process() {
  StreamBuffer* in = Input("view_frames");
  StreamBuffer* out = Output("osd_frames");
  if (in == nullptr || out == nullptr) {
    return NodeStatus{false, "ui_overlay: missing input 'view_frames' or output "
                             "'osd_frames'"};
  }

  // Consume (and ignore) bypass signal events: this feature renders only the
  // real-clock timestamp OSD; events are a reserved extension point. Draining
  // keeps the signal stream empty so the pipeline drain converges.
  if (StreamBuffer* signals = Input("signals")) {
    Packet event;
    while (signals->Pop(&event)) {
    }
  }

  Packet pkt;
  if (!in->Pop(&pkt)) return NodeStatus{};  // nothing to overlay this pass
  if (!pkt.IsFrame()) {
    return NodeStatus{false,
                      "ui_overlay: unexpected non-frame packet on 'view_frames'"};
  }
  video::codec::VideoFrame frame =
      std::get<video::codec::VideoFrame>(std::move(pkt.payload()));

  const std::string ts = FormatTimestamp(Defaults().timestamp_format);
  const int text_w = BitmapFont::MeasureWidth(ts);
  const int box_w = text_w + 2 * BitmapFont::kPadding;
  const int box_h = BitmapFont::Height();

  // Flex-anchor the timestamp box to the bottom-right corner of the frame.
  native::ui::Container canvas(
      native::ui::Width{static_cast<float>(frame.width)},
      native::ui::Height{static_cast<float>(frame.height)},
      native::ui::Direction{native::ui::Direction::kColumn},
      native::ui::JustifyContent{native::ui::JustifyContent::kFlexEnd},
      native::ui::AlignItems{native::ui::AlignItems::kFlexEnd});
  auto text_widget = std::make_unique<native::ui::Text>(
      native::ui::Content{ts}, native::ui::Width{static_cast<float>(box_w)},
      native::ui::Height{static_cast<float>(box_h)});
  native::ui::Text* text_ptr = text_widget.get();
  canvas.AddChild(std::move(text_widget));
  canvas.Layout();

  const native::ui::Rect box = text_ptr->bounds();
  const int x = static_cast<int>(box.x) + BitmapFont::kPadding;
  const int y = static_cast<int>(box.y) + BitmapFont::kPadding;

  BitmapFont::Draw(frame.planes[0].data(), frame.width, frame.height, x, y, ts,
                   kFgColor, kBgColor);

  Packet out_pkt("osd_frames", std::move(frame), pkt.pts_us());
  if (!out->Push(std::move(out_pkt))) {
    return NodeStatus{false, "ui_overlay: output buffer full or EOS"};
  }
  return NodeStatus{};
}

REGISTER_NODE("UiOverlayNode", UiOverlayNode);

}  // namespace media::record
