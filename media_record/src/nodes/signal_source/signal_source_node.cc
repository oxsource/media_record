#include "src/nodes/signal_source/signal_source_node.h"

#include <chrono>

#include "src/framework/transport/packet.h"

namespace media::record {

namespace {

int64_t NowUs() {
  using namespace std::chrono;
  return duration_cast<microseconds>(system_clock::now().time_since_epoch())
      .count();
}

}  // namespace

NodeStatus SignalSourceNode::Process() {
  StreamBuffer* out = Output("signals");
  if (out == nullptr) {
    return NodeStatus{false, "signal_source: missing output stream 'signals'"};
  }
  if (out->eos()) return NodeStatus{};  // recording ended; stop producing

  SignalEvent ev;
  ev.type = SignalEvent::Type::kTick;
  ev.timestamp_us = NowUs();
  Packet pkt("signals", ev);
  if (!out->Push(std::move(pkt))) {
    return NodeStatus{false, "signal_source: output buffer full or EOS"};
  }
  return NodeStatus{};
}

REGISTER_NODE("SignalSourceNode", SignalSourceNode);

}  // namespace media::record
