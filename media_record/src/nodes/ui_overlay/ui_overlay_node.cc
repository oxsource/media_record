#include "src/nodes/ui_overlay/ui_overlay_node.h"

#include <chrono>
#include <ctime>
#include <memory>
#include <string>

#include "native_ui/widgets.h"
#include "src/framework/node/graph_context.h"
#include "src/framework/node/node_contract.h"
#include "src/framework/node/node_options.h"
#include "src/framework/node/node_registry.h"
#include "src/framework/stream/packet.h"
#include "src/nodes/signal_source/signal_event.h"
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

UiOverlayNode::UiOverlayNode(const std::string& name,
                             const graph::runtime::NodeOptions& options)
    : Node(name) {
  if (const std::string* v = options.Get<std::string>("format")) {
    format_ = *v;
  }
}

absl::Status UiOverlayNode::GetContract(graph::runtime::NodeContract* c) {
  c->Inputs().Get("video").Set<video::codec::VideoFrame>();
  c->Inputs().Get("signal").Set<SignalEvent>();
  c->Outputs().Get("output").Set<video::codec::VideoFrame>();
  return absl::OkStatus();
}

absl::Status UiOverlayNode::Open(graph::runtime::GraphContext&) {
  return absl::OkStatus();
}

absl::Status UiOverlayNode::Close(graph::runtime::GraphContext&) {
  return absl::OkStatus();
}

absl::Status UiOverlayNode::Process(graph::runtime::GraphContext& ctx) {
  // Bypass signal events are consumed and ignored: this feature renders only
  // the real-clock timestamp OSD; events are a reserved extension point. The
  // runner moved all pending signal packets into this shard — ignoring them
  // keeps the signal stream drained.
  (void)ctx.Inputs().Get("signal");

  auto& in = ctx.Inputs().Get("video");
  if (in.IsEmpty()) return absl::OkStatus();  // nothing to overlay this pass
  auto frame_or = in.Value().Share<video::codec::VideoFrame>();
  if (!frame_or.ok()) {
    return absl::InvalidArgumentError(
        "ui_overlay: unexpected non-frame packet on 'video'");
  }
  const video::codec::VideoFrame& frame = **frame_or;

  const std::string ts = FormatTimestamp(format_);
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

  video::codec::VideoFrame out_frame;
  out_frame.format = frame.format;
  out_frame.width = frame.width;
  out_frame.height = frame.height;
  out_frame.stride[0] = frame.stride[0];
  out_frame.planes[0] = frame.planes[0];  // copy the base frame
  out_frame.timestamp_us = frame.timestamp_us;

  BitmapFont::Draw(out_frame.planes[0].data(), out_frame.width,
                   out_frame.height, x, y, ts, kFgColor, kBgColor);

  auto pkt = graph::runtime::Packet::MakePacket<video::codec::VideoFrame>(
                 std::move(out_frame))
                 .At(in.Value().timestamp());
  ctx.Outputs().Get("output").AddPacket(std::move(pkt));
  return absl::OkStatus();
}

namespace { using media::record::UiOverlayNode; }
GRAPH_RUNTIME_REGISTER_NODE("UiOverlayNode", UiOverlayNode);

}  // namespace media::record
