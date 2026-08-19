#ifndef MEDIA_RECORD_NODES_STREAM_INPUT_STREAM_INPUT_NODE_H_
#define MEDIA_RECORD_NODES_STREAM_INPUT_STREAM_INPUT_NODE_H_

#include <cstdint>
#include <string>
#include <vector>

#include "src/framework/node/node.h"

// StreamInputNode (spec 002): simulates a camera input from a static image.
//
// graph_runtime source node (no input ports): Open() decodes the configured
// image (NodeOptions "image") into an RGBA buffer; each Process() emits one
// Packet<video::codec::VideoFrame> on port "output" (stream "output:frames")
// with a real-clock timestamp, and returns StatusStop() once the frame budget
// (NodeOptions "frame_count") is exhausted.

namespace media::record {

class StreamInputNode : public graph::runtime::Node {
 public:
  StreamInputNode(const std::string& name,
                  const graph::runtime::NodeOptions& options);

  static absl::Status GetContract(graph::runtime::NodeContract* c);

  absl::Status Open(graph::runtime::GraphContext& ctx) override;
  absl::Status Close(graph::runtime::GraphContext& ctx) override;
  absl::Status Process(graph::runtime::GraphContext& ctx) override;

 private:
  std::string image_path_;
  int width_ = 0;   // 0 = follow the input image
  int height_ = 0;
  int fps_ = 30;
  int64_t frame_count_ = 300;

  std::vector<uint8_t> image_;  // decoded RGBA pixels
  int64_t frame_index_ = 0;
};

}  // namespace media::record

#endif  // MEDIA_RECORD_NODES_STREAM_INPUT_STREAM_INPUT_NODE_H_
