#ifndef MEDIA_RECORD_NODES_VIDEO_ENCODER_VIDEO_ENCODER_NODE_H_
#define MEDIA_RECORD_NODES_VIDEO_ENCODER_VIDEO_ENCODER_NODE_H_

#include <memory>
#include <vector>

#include "src/framework/node/node.h"
#include "video_codec/video_codec.h"

// VideoEncoderNode (spec 002): H.264 encodes the OSD frames.
//
// graph_runtime node: input port "input" (stream "input:osd_frames"), output
// port "output" (stream "output:es_packets"). Converts each incoming RGBA
// frame to I420 with the built-in software converter (dependency-contract
// D-3), then feeds video_codec's VideoEncoder (H.264, FFmpeg backend) in push
// mode: every produced VideoPacket is collected and forwarded as a Packet.
// The encoder is created lazily on the first frame (dimensions known) and
// flushed when the input stream is done so buffered packets are not lost.

namespace media::record {

class VideoEncoderNode : public graph::runtime::Node {
 public:
  VideoEncoderNode(const std::string& name,
                   const graph::runtime::NodeOptions& options);

  static absl::Status GetContract(graph::runtime::NodeContract* c);

  absl::Status Open(graph::runtime::GraphContext& ctx) override;
  absl::Status Process(graph::runtime::GraphContext& ctx) override;
  absl::Status Close(graph::runtime::GraphContext& ctx) override;

 private:
  absl::Status EnsureEncoder(const video::codec::VideoFrame& frame);
  void EmitPending(graph::runtime::GraphContext& ctx);

  class PacketSinkAdapter;  // PacketSink -> pending_ packets

  int fps_ = 30;
  int bitrate_ = 4'000'000;

  std::unique_ptr<video::codec::VideoEncoder> encoder_;
  std::unique_ptr<PacketSinkAdapter> sink_;
  std::vector<graph::runtime::Packet> pending_;  // encoded packets to emit
  bool flushed_ = false;
};

}  // namespace media::record

#endif  // MEDIA_RECORD_NODES_VIDEO_ENCODER_VIDEO_ENCODER_NODE_H_
