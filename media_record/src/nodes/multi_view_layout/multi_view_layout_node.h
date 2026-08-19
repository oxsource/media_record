#ifndef MEDIA_RECORD_NODES_MULTI_VIEW_LAYOUT_MULTI_VIEW_LAYOUT_NODE_H_
#define MEDIA_RECORD_NODES_MULTI_VIEW_LAYOUT_MULTI_VIEW_LAYOUT_NODE_H_

#include "src/framework/transport/stream_node.h"

// MultiViewLayoutNode (spec 002): composes the input frame into the recording
// canvas using native_ui flex layout (Container + ExternalImage) and renders
// the base frame via software blit into its own RGBA buffer (host Surface has
// no pixel readback). This feature implements single-view composition: the
// input image fills the whole frame; multi-input (f/r) layout is reserved.

namespace media::record {

class MultiViewLayoutNode : public StreamNode {
 public:
  NodeStatus Process() override;
};

}  // namespace media::record

#endif  // MEDIA_RECORD_NODES_MULTI_VIEW_LAYOUT_MULTI_VIEW_LAYOUT_NODE_H_
