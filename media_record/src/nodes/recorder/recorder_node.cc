#include "src/nodes/recorder/recorder_node.h"

#include <cstdio>

#include "src/framework/transport/packet.h"

namespace media::record {

NodeStatus RecorderNode::Open() {
  frames_recorded_ = 0;
  finalized_ = false;
  return NodeStatus{};
}

NodeStatus RecorderNode::Process() {
  StreamBuffer* in = Input("es_packets");
  StreamBuffer* out = Output("clips");
  if (in == nullptr || out == nullptr) {
    return NodeStatus{false, "recorder: missing input 'es_packets' or output "
                             "'clips'"};
  }

  Packet pkt;
  if (in->Pop(&pkt)) {
    if (!pkt.IsEncoded()) {
      return NodeStatus{false,
                        "recorder: unexpected non-encoded packet on 'es_packets'"};
    }
    ++frames_recorded_;
    Packet clip("clips", pkt.payload(), pkt.pts_us());
    if (!out->Push(std::move(clip))) {
      return NodeStatus{false, "recorder: output buffer full or EOS"};
    }
  }
  return NodeStatus{};
}

NodeStatus RecorderNode::Close() {
  if (!finalized_) {
    finalized_ = true;
    std::printf("[recorder] session finalized: %lld encoded frame(s)\n",
                static_cast<long long>(frames_recorded_));
  }
  return NodeStatus{};
}

REGISTER_NODE("RecorderNode", RecorderNode);

}  // namespace media::record
