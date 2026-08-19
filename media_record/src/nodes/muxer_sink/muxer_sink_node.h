#ifndef MEDIA_RECORD_NODES_MUXER_SINK_MUXER_SINK_NODE_H_
#define MEDIA_RECORD_NODES_MUXER_SINK_MUXER_SINK_NODE_H_

#include <memory>
#include <string>

#include "src/framework/node/node.h"
#include "video_codec/video_codec.h"

// MuxerSinkNode (spec 002): writes the encoded packets into an MP4 file.
//
// graph_runtime node: input port "input" (stream "input:clips"). Uses
// video_codec's public Muxer (MP4, FFmpeg/libavformat backend) writing through
// a FileByteSink to a temporary file; on Finish the trailer is written and the
// file is atomically renamed to the target path (NodeOptions "output"). A
// stale temporary file is removed on failure (FR-009 no partial artifacts;
// contracts/node-contract.md §3.7). Existing files are overwritten with a log
// notice. Stream metadata (width/height/fps) comes from NodeOptions, filled by
// the recording entry from the decoded input image.

namespace media::record {

class MuxerSinkNode : public graph::runtime::Node {
 public:
  MuxerSinkNode(const std::string& name,
                const graph::runtime::NodeOptions& options);

  static absl::Status GetContract(graph::runtime::NodeContract* c);

  absl::Status Open(graph::runtime::GraphContext& ctx) override;
  absl::Status Process(graph::runtime::GraphContext& ctx) override;
  absl::Status Close(graph::runtime::GraphContext& ctx) override;

 private:
  std::string output_file_;
  int width_ = 0;
  int height_ = 0;
  int fps_ = 30;

  std::unique_ptr<video::codec::Muxer> muxer_;
  std::unique_ptr<video::codec::FileByteSink> sink_;
  std::string target_path_;
  std::string temp_path_;
  bool finished_ = false;
};

}  // namespace media::record

#endif  // MEDIA_RECORD_NODES_MUXER_SINK_MUXER_SINK_NODE_H_
