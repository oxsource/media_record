#ifndef MEDIA_RECORD_NODES_RECORDER_RECORDER_NODE_H_
#define MEDIA_RECORD_NODES_RECORDER_RECORDER_NODE_H_

#include <cstdint>

#include "src/framework/transport/stream_node.h"

// RecorderNode (spec 002): single-session single-segment recording.
//
// Tracks the session lifecycle and forwards every encoded VideoPacket from
// "es_packets" to "clips" (the muxer input). The frame budget is enforced by
// the PipelineRunner (duration_seconds x fps); RecorderNode stays a pass-through
// that counts frames and finalizes the session on Close (data-model.md §4).

namespace media::record {

class RecorderNode : public StreamNode {
 public:
  NodeStatus Open() override;
  NodeStatus Process() override;
  NodeStatus Close() override;

 private:
  int64_t frames_recorded_ = 0;
  bool finalized_ = false;
};

}  // namespace media::record

#endif  // MEDIA_RECORD_NODES_RECORDER_RECORDER_NODE_H_
