#ifndef MEDIA_RECORD_NODES_MULTI_VIEW_LAYOUT_MULTI_VIEW_LAYOUT_NODE_H_
#define MEDIA_RECORD_NODES_MULTI_VIEW_LAYOUT_MULTI_VIEW_LAYOUT_NODE_H_

#include "graph_runtime/node.h"

// MultiViewLayoutNode (spec 002): composes the input frame into the recording
// canvas using native_ui flex layout (Container + ExternalImage) and renders
// the base frame via software blit into its own RGBA buffer (host Surface has
// no pixel readback). This feature implements single-view composition: the
// input image fills the whole frame; multi-input (f/r) layout is reserved.
// graph_runtime node: input port "f" (stream "f:frames"), output port "output"
// (stream "output:view_frames").

namespace media::record {

class MultiViewLayoutNode : public graph::runtime::Node {
 public:
  MultiViewLayoutNode(const std::string& name,
                      const graph::runtime::NodeOptions& options);

  static absl::Status GetContract(graph::runtime::NodeContract* c);

  absl::Status Open(graph::runtime::GraphContext& ctx) override;
  absl::Status Close(graph::runtime::GraphContext& ctx) override;
  absl::Status Process(graph::runtime::GraphContext& ctx) override;
};

}  // namespace media::record

#endif  // MEDIA_RECORD_NODES_MULTI_VIEW_LAYOUT_MULTI_VIEW_LAYOUT_NODE_H_
