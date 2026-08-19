#ifndef MEDIA_RECORD_NODES_RECORDER_RECORDER_NODE_H_
#define MEDIA_RECORD_NODES_RECORDER_RECORDER_NODE_H_

#include <cstdint>

#include "src/framework/node/node.h"

// RecorderNode (spec 002): single-session single-segment recording.
//
// graph_runtime node: input port "input" (stream "input:es_packets"), output
// port "output" (stream "output:clips"). Forwards every encoded VideoPacket
// to the muxer and counts frames; the frame budget is enforced by the source
// nodes (duration_seconds x fps via NodeOptions "frame_count"), and the
// session finalizes on Close (data-model.md §4).

namespace media::record {

class RecorderNode : public graph::runtime::Node {
 public:
  RecorderNode(const std::string& name,
               const graph::runtime::NodeOptions& options);

  static absl::Status GetContract(graph::runtime::NodeContract* c);

  absl::Status Open(graph::runtime::GraphContext& ctx) override;
  absl::Status Process(graph::runtime::GraphContext& ctx) override;
  absl::Status Close(graph::runtime::GraphContext& ctx) override;

 private:
  int64_t frames_recorded_ = 0;
  bool finalized_ = false;
};

}  // namespace media::record

#endif  // MEDIA_RECORD_NODES_RECORDER_RECORDER_NODE_H_
