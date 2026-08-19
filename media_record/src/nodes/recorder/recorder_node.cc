#include "src/nodes/recorder/recorder_node.h"

#include <cstdio>

#include "src/framework/node/graph_context.h"
#include "src/framework/node/node_contract.h"
#include "src/framework/node/node_options.h"
#include "src/framework/node/node_registry.h"
#include "src/framework/stream/packet.h"
#include "video_codec/video_codec.h"

namespace media::record {

RecorderNode::RecorderNode(const std::string& name,
                           const graph::runtime::NodeOptions& options)
    : Node(name) {}

absl::Status RecorderNode::GetContract(graph::runtime::NodeContract* c) {
  c->Inputs().Get("input").Set<video::codec::VideoPacket>();
  c->Outputs().Get("output").Set<video::codec::VideoPacket>();
  return absl::OkStatus();
}

absl::Status RecorderNode::Open(graph::runtime::GraphContext&) {
  return absl::OkStatus();
}

absl::Status RecorderNode::Process(graph::runtime::GraphContext& ctx) {
  auto& in = ctx.Inputs().Get("input");
  if (in.IsEmpty()) return absl::OkStatus();
  auto pkt_or = in.Value().Share<video::codec::VideoPacket>();
  if (!pkt_or.ok()) {
    return absl::InvalidArgumentError(
        "recorder: unexpected non-encoded packet on 'input'");
  }
  ++frames_recorded_;
  auto out_pkt =
      graph::runtime::Packet::MakePacket<video::codec::VideoPacket>(**pkt_or)
          .At(in.Value().timestamp());
  ctx.Outputs().Get("output").AddPacket(std::move(out_pkt));
  return absl::OkStatus();
}

absl::Status RecorderNode::Close(graph::runtime::GraphContext&) {
  if (!finalized_) {
    finalized_ = true;
    std::printf("[recorder] session finalized: %lld encoded frame(s)\n",
                static_cast<long long>(frames_recorded_));
  }
  return absl::OkStatus();
}

namespace { using media::record::RecorderNode; }
GRAPH_RUNTIME_REGISTER_NODE("RecorderNode", RecorderNode);

}  // namespace media::record
