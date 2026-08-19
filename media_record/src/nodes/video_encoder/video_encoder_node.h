#ifndef MEDIA_RECORD_NODES_VIDEO_ENCODER_VIDEO_ENCODER_NODE_H_
#define MEDIA_RECORD_NODES_VIDEO_ENCODER_VIDEO_ENCODER_NODE_H_

#include <memory>

#include "src/framework/transport/stream_node.h"
#include "video_codec/video_codec.h"

// VideoEncoderNode (spec 002): H.264 encodes the OSD frames.
//
// Converts each incoming RGBA frame to I420 with the built-in software
// converter (dependency-contract D-3), then feeds video_codec's VideoEncoder
// (H.264, FFmpeg backend) in push mode: every produced VideoPacket is routed
// through a PacketSink adapter into the "es_packets" stream buffer (research.md
// §2). The encoder is created lazily on the first frame (dimensions known) and
// flushed on EOS so buffered packets are not lost.

namespace media::record {

class VideoEncoderNode : public StreamNode {
 public:
  NodeStatus Process() override;
  NodeStatus Close() override;

 private:
  NodeStatus EnsureEncoder(const video::codec::VideoFrame& frame);

  class StreamBufferSink;  // PacketSink adapter -> "es_packets" buffer
  std::unique_ptr<video::codec::VideoEncoder> encoder_;
  std::unique_ptr<StreamBufferSink> sink_;
  bool flushed_ = false;
};

}  // namespace media::record

#endif  // MEDIA_RECORD_NODES_VIDEO_ENCODER_VIDEO_ENCODER_NODE_H_
