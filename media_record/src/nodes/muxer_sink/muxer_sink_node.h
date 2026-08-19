#ifndef MEDIA_RECORD_NODES_MUXER_SINK_MUXER_SINK_NODE_H_
#define MEDIA_RECORD_NODES_MUXER_SINK_MUXER_SINK_NODE_H_

#include <memory>
#include <string>

#include "src/framework/transport/stream_node.h"
#include "video_codec/video_codec.h"

// MuxerSinkNode (spec 002): writes the encoded packets into an MP4 file.
//
// Uses video_codec's public Muxer (MP4, FFmpeg/libavformat backend) writing
// through a FileByteSink to a temporary file; on Finish the trailer is written
// and the file is atomically renamed to the target path. A stale temporary file
// is removed on failure (FR-009 no partial artifacts; contracts/node-contract.md
// §3.7). Existing files are overwritten with a log notice.

namespace media::record {

class MuxerSinkNode : public StreamNode {
 public:
  NodeStatus Open() override;
  NodeStatus Process() override;
  NodeStatus Close() override;

 private:
  std::unique_ptr<video::codec::Muxer> muxer_;
  std::unique_ptr<video::codec::FileByteSink> sink_;
  std::string target_path_;
  std::string temp_path_;
  bool finished_ = false;
};

}  // namespace media::record

#endif  // MEDIA_RECORD_NODES_MUXER_SINK_MUXER_SINK_NODE_H_
