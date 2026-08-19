#include "src/nodes/video_encoder/video_encoder_node.h"

#include <string>
#include <utility>

#include "src/framework/transport/packet.h"
#include "src/framework/transport/recording_defaults.h"
#include "src/nodes/video_encoder/rgba_i420.h"

namespace media::record {

namespace {

std::string StatusName(video::codec::Status s) {
  return video::codec::StatusToString(s);
}

}  // namespace

// Forwards every encoded packet into the "es_packets" StreamBuffer. Push mode
// guarantees no packets are dropped on Flush (pull mode returns only the last
// drained packet).
class VideoEncoderNode::StreamBufferSink : public video::codec::PacketSink {
 public:
  explicit StreamBufferSink(StreamBuffer* out) : out_(out) {}

  video::codec::Status Push(video::codec::VideoPacket&& pkt) override {
    media::record::Packet p("es_packets", std::move(pkt));
    return out_->Push(std::move(p)) ? video::codec::Status::kOk
                                    : video::codec::Status::kEncodeFailed;
  }

  video::codec::Status Push(video::codec::AudioPacket&&) override {
    return video::codec::Status::kUnsupportedOperation;
  }

 private:
  StreamBuffer* out_;
};

NodeStatus VideoEncoderNode::EnsureEncoder(const video::codec::VideoFrame& frame) {
  if (encoder_) return NodeStatus{};
  StreamBuffer* out = Output("es_packets");
  if (out == nullptr) {
    return NodeStatus{false, "video_encoder: missing output stream 'es_packets'"};
  }

  video::codec::VideoConfig cfg;
  cfg.codec = video::codec::VideoCodecType::kH264;
  cfg.width = frame.width;
  cfg.height = frame.height;
  cfg.fps = Defaults().fps > 0 ? Defaults().fps : 30;
  cfg.bitrate = Defaults().bitrate > 0 ? Defaults().bitrate : 4'000'000;
  cfg.input_format = video::codec::PixelFormat::kI420;
  cfg.backend = video::codec::Backend::kAuto;

  encoder_ = video::codec::CodecFactory::CreateVideo(cfg);
  if (!encoder_) {
    return NodeStatus{false, "video_encoder: video encoder unavailable (no backend)"};
  }
  if (encoder_->Init() != video::codec::Status::kOk) {
    return NodeStatus{false, "video_encoder: encoder Init failed"};
  }
  sink_ = std::make_unique<StreamBufferSink>(out);
  if (encoder_->SetOutputSink(sink_.get()) != video::codec::Status::kOk) {
    return NodeStatus{false, "video_encoder: push-mode wiring failed"};
  }
  return NodeStatus{};
}

NodeStatus VideoEncoderNode::Process() {
  StreamBuffer* in = Input("osd_frames");
  StreamBuffer* out = Output("es_packets");
  if (in == nullptr || out == nullptr) {
    return NodeStatus{false,
                      "video_encoder: missing input 'osd_frames' or output "
                      "'es_packets'"};
  }

  Packet pkt;
  if (in->Pop(&pkt)) {
    if (!pkt.IsFrame()) {
      return NodeStatus{false,
                        "video_encoder: unexpected non-frame packet on 'osd_frames'"};
    }
    video::codec::VideoFrame rgba =
        std::get<video::codec::VideoFrame>(std::move(pkt.payload()));

    NodeStatus status = EnsureEncoder(rgba);
    if (!status.ok) return status;

    video::codec::VideoFrame i420;
    i420.format = video::codec::PixelFormat::kI420;
    i420.width = rgba.width;
    i420.height = rgba.height;
    i420.stride[0] = rgba.width;
    i420.stride[1] = rgba.width / 2;
    i420.stride[2] = rgba.width / 2;
    i420.planes[0].resize(static_cast<size_t>(rgba.width) * rgba.height);
    i420.planes[1].resize(static_cast<size_t>(rgba.width / 2) * (rgba.height / 2));
    i420.planes[2].resize(static_cast<size_t>(rgba.width / 2) * (rgba.height / 2));
    i420.timestamp_us = rgba.timestamp_us;

    RgbaToI420({rgba.planes[0].data(), static_cast<size_t>(rgba.stride[0]),
                rgba.width, rgba.height},
               {i420.planes[0].data(), static_cast<size_t>(i420.stride[0]),
                i420.planes[1].data(), static_cast<size_t>(i420.stride[1]),
                i420.planes[2].data(), static_cast<size_t>(i420.stride[2])});

    const auto result = encoder_->Encode(i420);
    if (!result.ok()) {
      return NodeStatus{false,
                        "video_encoder: Encode failed (" + StatusName(result.status()) +
                            ")"};
    }
    return NodeStatus{};
  }

  if (in->eos() && !flushed_) {
    flushed_ = true;
    if (encoder_) {
      const auto result = encoder_->Flush();
      if (!result.ok()) {
        return NodeStatus{false,
                          "video_encoder: Flush failed (" +
                              StatusName(result.status()) + ")"};
      }
    }
  }
  return NodeStatus{};
}

NodeStatus VideoEncoderNode::Close() {
  if (encoder_) encoder_->Release();
  encoder_.reset();
  sink_.reset();
  return NodeStatus{};
}

  static int& kDbg() { static int v = 0; return v; }

REGISTER_NODE("VideoEncoderNode", VideoEncoderNode);

}  // namespace media::record
