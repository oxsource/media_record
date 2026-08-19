#ifndef MEDIA_RECORD_NODES_STREAM_INPUT_STREAM_INPUT_NODE_H_
#define MEDIA_RECORD_NODES_STREAM_INPUT_STREAM_INPUT_NODE_H_

#include <cstdint>
#include <vector>

#include "src/framework/transport/stream_node.h"

// StreamInputNode (spec 002): simulates a camera input from a static image.
//
// Open() decodes the configured default image (RecordingDefaults::input_image)
// into an RGBA buffer; each Process() emits one Packet<VideoFrame> on
// "frames" with a real-clock timestamp and a monotonic presentation time.
// Stops producing once the output stream is marked EOS (end of recording).

namespace media::record {

class StreamInputNode : public StreamNode {
 public:
  NodeStatus Open() override;
  NodeStatus Process() override;

 private:
  std::vector<uint8_t> image_;  // decoded RGBA pixels
  int width_ = 0;
  int height_ = 0;
  int64_t frame_index_ = 0;
};

}  // namespace media::record

#endif  // MEDIA_RECORD_NODES_STREAM_INPUT_STREAM_INPUT_NODE_H_
