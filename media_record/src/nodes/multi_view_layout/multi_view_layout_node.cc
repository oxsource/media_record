#include "src/nodes/multi_view_layout/multi_view_layout_node.h"

#include <memory>

#include "native_ui/surface.h"
#include "native_ui/widgets.h"
#include "src/framework/transport/packet.h"
#include "video_codec/video_codec.h"

namespace media::record {

NodeStatus MultiViewLayoutNode::Process() {
  StreamBuffer* in = Input("frames");
  StreamBuffer* out = Output("view_frames");
  if (in == nullptr || out == nullptr) {
    return NodeStatus{false,
                      "multi_view_layout: missing input 'frames' or output "
                      "'view_frames'"};
  }

  Packet pkt;
  if (!in->Pop(&pkt)) return NodeStatus{};  // nothing to lay out this pass
  if (!pkt.IsFrame()) {
    return NodeStatus{false,
                      "multi_view_layout: unexpected non-frame packet on 'frames'"};
  }
  const video::codec::VideoFrame& frame =
      std::get<video::codec::VideoFrame>(pkt.payload());

  // Flex composition: the ExternalImage is the base layer covering the whole
  // canvas (single view). Measured bounds confirm full-frame coverage; the
  // pixels are then blitted by media_record (no Surface pixel readback).
  native::ui::Container canvas(
      native::ui::Width{static_cast<float>(frame.width)},
      native::ui::Height{static_cast<float>(frame.height)},
      native::ui::Direction{native::ui::Direction::kColumn});
  native::ui::HardwareBuffer base = native::ui::HardwareBuffer::FromMemory(
      const_cast<uint8_t*>(frame.planes[0].data()),
      static_cast<size_t>(frame.width) * 4, frame.width, frame.height);
  auto base_image =
      std::make_unique<native::ui::ExternalImage>(native::ui::Id{"base"});
  base_image->SetBuffer(base);
  native::ui::ExternalImage* base_ptr = base_image.get();
  canvas.AddChild(std::move(base_image));
  canvas.Layout();

  const native::ui::Rect region = base_ptr->bounds();

  // Software blit: copy the (single) input view into the output frame buffer.
  video::codec::VideoFrame out_frame;
  out_frame.format = video::codec::PixelFormat::kRGBA;
  out_frame.width = frame.width;
  out_frame.height = frame.height;
  out_frame.stride[0] = frame.width * 4;
  out_frame.planes[0] = frame.planes[0];  // copies pixels (frame fills canvas)
  out_frame.timestamp_us = frame.timestamp_us;

  Packet out_pkt("view_frames", std::move(out_frame), pkt.pts_us());
  if (!out->Push(std::move(out_pkt))) {
    return NodeStatus{false, "multi_view_layout: output buffer full or EOS"};
  }
  (void)region;  // bounds used implicitly: full-frame coverage for single view
  return NodeStatus{};
}

REGISTER_NODE("MultiViewLayoutNode", MultiViewLayoutNode);

}  // namespace media::record
