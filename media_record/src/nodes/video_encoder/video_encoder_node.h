#ifndef MEDIA_RECORD_NODES_VIDEO_ENCODER_VIDEO_ENCODER_NODE_H_
#define MEDIA_RECORD_NODES_VIDEO_ENCODER_VIDEO_ENCODER_NODE_H_

#include <memory>
#include <vector>

#include "graph_runtime/node.h"
#include "video_codec/video_codec.h"

#if defined(__ANDROID__)
#include "src/framework/lifecycle/lifecycle_context.h"
#endif

// VideoEncoderNode (spec 002): H.264 encodes the OSD frames.
//
// graph_runtime node: input port "input", output port "output" (stream
// "output:es_packets"). Two input modes, selected by the `input_surface`
// NodeOptions flag (spec 003):
//   - CPU (default, host): each incoming RGBA VideoFrame is converted to I420
//     (built-in software converter) and fed to video_codec's VideoEncoder
//     (H.264) in push mode; every produced VideoPacket is forwarded.
//   - Android surface mode (input_surface=true): the encoder is created in
//     Open() with input_surface=true; its CreateInputSurface() (ANativeWindow*)
//     is written into the shared LifecycleContext::input_surface. The render
//     node draws directly onto that surface (GPU, zero-copy) and sends a
//     PacketNotify; this node then Poll()s the hardware encoder to pump
//     out encoded data. No CPU VideoFrame flows render -> encoder in this mode.
// The encoder is created lazily on the first frame (CPU) or in Open (surface,
// dimensions come from NodeOptions), and flushed when the input is done.

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
#if defined(__ANDROID__)
  // Surface mode: create the encoder in Open(), wire its CreateInputSurface()
  // into LifecycleContext::input_surface, and attach the packet sink.
  absl::Status EnsureSurfaceEncoder(graph::runtime::GraphContext& ctx);
#endif

  class PacketSinkAdapter;  // PacketSink -> pending_ packets

  int fps_ = 30;
  int bitrate_ = 4'000'000;
  int width_ = 0;
  int height_ = 0;
  bool surface_mode_ = false;

  std::unique_ptr<video::codec::VideoEncoder> encoder_;
  std::unique_ptr<PacketSinkAdapter> sink_;
  std::vector<graph::runtime::Packet> pending_;  // encoded packets to emit
  bool flushed_ = false;
};

}  // namespace media::record

#endif  // MEDIA_RECORD_NODES_VIDEO_ENCODER_VIDEO_ENCODER_NODE_H_
