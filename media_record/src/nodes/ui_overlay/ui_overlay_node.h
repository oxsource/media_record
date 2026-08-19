#ifndef MEDIA_RECORD_NODES_UI_OVERLAY_UI_OVERLAY_NODE_H_
#define MEDIA_RECORD_NODES_UI_OVERLAY_UI_OVERLAY_NODE_H_

#include "src/framework/transport/stream_node.h"

// UiOverlayNode (spec 002): overlays the real-clock timestamp on the frame.
//
// The timestamp text is positioned with native_ui flex layout (Container +
// Text, anchored to the bottom-right corner) and drawn into the frame's RGBA
// buffer with the software bitmap font (research.md §4). Inputs: "view_frames"
// (video) and "signals" (bypass events, ignored this feature).

namespace media::record {

class UiOverlayNode : public StreamNode {
 public:
  NodeStatus Process() override;
};

}  // namespace media::record

#endif  // MEDIA_RECORD_NODES_UI_OVERLAY_UI_OVERLAY_NODE_H_
