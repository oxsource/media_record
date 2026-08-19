#ifndef MEDIA_RECORD_NODES_SIGNAL_SOURCE_SIGNAL_SOURCE_NODE_H_
#define MEDIA_RECORD_NODES_SIGNAL_SOURCE_SIGNAL_SOURCE_NODE_H_

#include "src/framework/transport/stream_node.h"

// SignalSourceNode (spec 002): emits one bypass SignalEvent (kTick) per
// Process on "signals" (data-model.md §3). This feature renders only the
// timestamp OSD, so consumers may ignore the events.

namespace media::record {

class SignalSourceNode : public StreamNode {
 public:
  NodeStatus Process() override;
};

}  // namespace media::record

#endif  // MEDIA_RECORD_NODES_SIGNAL_SOURCE_SIGNAL_SOURCE_NODE_H_
