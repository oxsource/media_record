#ifndef MEDIA_RECORD_NODES_UI_OVERLAY_UI_OVERLAY_NODE_H_
#define MEDIA_RECORD_NODES_UI_OVERLAY_UI_OVERLAY_NODE_H_

#include <string>

#include "src/framework/node/node.h"

// UiOverlayNode (spec 002): overlays the real-clock timestamp on the frame.
//
// The timestamp text is positioned with native_ui flex layout (Container +
// Text, anchored to the bottom-right corner) and drawn into the frame's RGBA
// buffer with the software bitmap font (research.md §4). Input: "video"
// (stream "video:view_frames"). graph_runtime node; timestamp format from
// NodeOptions "format".

namespace media::record {

class UiOverlayNode : public graph::runtime::Node {
 public:
  UiOverlayNode(const std::string& name,
                const graph::runtime::NodeOptions& options);

  static absl::Status GetContract(graph::runtime::NodeContract* c);

  absl::Status Open(graph::runtime::GraphContext& ctx) override;
  absl::Status Close(graph::runtime::GraphContext& ctx) override;
  absl::Status Process(graph::runtime::GraphContext& ctx) override;

 private:
  std::string format_ = "%Y-%m-%d %H:%M:%S";
};

}  // namespace media::record

#endif  // MEDIA_RECORD_NODES_UI_OVERLAY_UI_OVERLAY_NODE_H_
