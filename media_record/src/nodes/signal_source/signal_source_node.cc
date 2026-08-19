#include "src/nodes/signal_source/signal_source_node.h"

#include <chrono>

#include "src/framework/node/graph_context.h"
#include "src/framework/node/node_contract.h"
#include "src/framework/node/node_options.h"
#include "src/framework/node/node_registry.h"
#include "src/framework/public/types.h"
#include "src/framework/stream/packet.h"
#include "src/nodes/signal_source/signal_event.h"

namespace media::record {

namespace {

int64_t NowUs() {
  using namespace std::chrono;
  return duration_cast<microseconds>(system_clock::now().time_since_epoch())
      .count();
}

}  // namespace

SignalSourceNode::SignalSourceNode(const std::string& name,
                                   const graph::runtime::NodeOptions& options)
    : Node(name) {
  if (const int* v = options.Get<int>("frame_count")) frame_count_ = *v;
  if (frame_count_ <= 0) frame_count_ = 300;
}

absl::Status SignalSourceNode::GetContract(graph::runtime::NodeContract* c) {
  c->Outputs().Get("output").Set<SignalEvent>();
  return absl::OkStatus();
}

absl::Status SignalSourceNode::Open(graph::runtime::GraphContext&) {
  return absl::OkStatus();
}

absl::Status SignalSourceNode::Close(graph::runtime::GraphContext&) {
  return absl::OkStatus();
}

absl::Status SignalSourceNode::Process(graph::runtime::GraphContext& ctx) {
  if (tick_index_ >= frame_count_) {
    return graph::runtime::StatusStop();
  }
  SignalEvent event;
  event.type = SignalEvent::Type::kTick;
  event.timestamp_us = NowUs();
  auto pkt = graph::runtime::Packet::MakePacket<SignalEvent>(std::move(event));
  ctx.Outputs().Get("output").AddPacket(std::move(pkt));
  ++tick_index_;
  return absl::OkStatus();
}

namespace { using media::record::SignalSourceNode; }
GRAPH_RUNTIME_REGISTER_NODE("SignalSourceNode", SignalSourceNode);

}  // namespace media::record
