#ifndef MEDIA_RECORD_NODES_SIGNAL_SOURCE_SIGNAL_SOURCE_NODE_H_
#define MEDIA_RECORD_NODES_SIGNAL_SOURCE_SIGNAL_SOURCE_NODE_H_

#include <cstdint>

#include "src/framework/node/node.h"

// SignalSourceNode (spec 002): emits one bypass SignalEvent (kTick) per
// Process on port "output" (stream "output:signals", data-model.md §3), and
// returns StatusStop() once the tick budget (NodeOptions "frame_count") is
// exhausted so the graph terminates in lockstep with StreamInputNode.

namespace media::record {

class SignalSourceNode : public graph::runtime::Node {
 public:
  SignalSourceNode(const std::string& name,
                   const graph::runtime::NodeOptions& options);

  static absl::Status GetContract(graph::runtime::NodeContract* c);

  absl::Status Open(graph::runtime::GraphContext& ctx) override;
  absl::Status Close(graph::runtime::GraphContext& ctx) override;
  absl::Status Process(graph::runtime::GraphContext& ctx) override;

 private:
  int64_t frame_count_ = 300;
  int64_t tick_index_ = 0;
};

}  // namespace media::record

#endif  // MEDIA_RECORD_NODES_SIGNAL_SOURCE_SIGNAL_SOURCE_NODE_H_
