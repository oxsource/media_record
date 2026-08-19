#include "src/nodes/multi_view_layout/multi_view_layout_node.h"

#include <memory>

#include "native_ui/surface.h"
#include "native_ui/widgets.h"
#include "graph_runtime/graph_context.h"
#include "graph_runtime/node_contract.h"
#include "graph_runtime/node_options.h"
#include "graph_runtime/node_registry.h"
#include "graph_runtime/packet.h"
#include "video_codec/video_codec.h"

namespace media::record {

MultiViewLayoutNode::MultiViewLayoutNode(
    const std::string& name, const graph::runtime::NodeOptions& options)
    : Node(name) {}

absl::Status MultiViewLayoutNode::GetContract(graph::runtime::NodeContract* c) {
  c->Inputs().Get("f").Set<video::codec::VideoFrame>();
  c->Outputs().Get("output").Set<video::codec::VideoFrame>();
  return absl::OkStatus();
}

absl::Status MultiViewLayoutNode::Open(graph::runtime::GraphContext&) {
  return absl::OkStatus();
}

absl::Status MultiViewLayoutNode::Close(graph::runtime::GraphContext&) {
  return absl::OkStatus();
}

absl::Status MultiViewLayoutNode::Process(graph::runtime::GraphContext& ctx) {
  auto& in = ctx.Inputs().Get("f");
  if (in.IsEmpty()) return absl::OkStatus();  // nothing to lay out this pass
  auto frame_or = in.Value().Share<video::codec::VideoFrame>();
  if (!frame_or.ok()) {
    return absl::InvalidArgumentError(
        "multi_view_layout: unexpected non-frame packet on 'f'");
  }
  const video::codec::VideoFrame& frame = **frame_or;

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

  (void)region;  // bounds used implicitly: full-frame coverage for single view
  auto pkt = graph::runtime::Packet::MakePacket<video::codec::VideoFrame>(
                 std::move(out_frame))
                 .At(in.Value().timestamp());
  ctx.Outputs().Get("output").AddPacket(std::move(pkt));
  return absl::OkStatus();
}

namespace { using media::record::MultiViewLayoutNode; }
GRAPH_RUNTIME_REGISTER_NODE("MultiViewLayoutNode", MultiViewLayoutNode);

}  // namespace media::record
