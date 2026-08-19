#ifndef MEDIA_RECORD_FRAMEWORK_STREAM_PACKET_H_
#define MEDIA_RECORD_FRAMEWORK_STREAM_PACKET_H_

#include <string>
#include <variant>

#include "video_codec/video_codec.h"

// Frame-transport packet (spec 002 / data-model.md §5).
//
// A packet is the unit of data moving between pipeline nodes: a variant
// payload (an RGBA VideoFrame, an encoded H.264 VideoPacket, or a bypass
// SignalEvent) plus routing metadata (stream name + presentation time). The
// node contracts (contracts/node-contract.md N-3) require every frame to travel
// as a media::record::Packet.

namespace media::record {

// Bypass signal event (spec 002): minimal event produced by SignalSourceNode
// and consumed (optionally) by UiOverlayNode / RecorderNode.
struct SignalEvent {
  enum class Type { kNone, kTick };
  Type type = Type::kTick;
  int64_t timestamp_us = 0;
};

class Packet {
 public:
  using Payload =
      std::variant<video::codec::VideoFrame, video::codec::VideoPacket, SignalEvent>;

  Packet() = default;
  Packet(std::string stream_name, Payload payload, int64_t pts_us = 0)
      : stream_name_(std::move(stream_name)),
        payload_(std::move(payload)),
        pts_us_(pts_us) {}

  const std::string& stream_name() const { return stream_name_; }
  int64_t pts_us() const { return pts_us_; }

  const Payload& payload() const { return payload_; }
  Payload& payload() { return payload_; }

  bool IsFrame() const { return std::holds_alternative<video::codec::VideoFrame>(payload_); }
  bool IsEncoded() const { return std::holds_alternative<video::codec::VideoPacket>(payload_); }
  bool IsSignal() const { return std::holds_alternative<SignalEvent>(payload_); }

 private:
  std::string stream_name_;
  Payload payload_;
  int64_t pts_us_ = 0;
};

}  // namespace media::record

#endif  // MEDIA_RECORD_FRAMEWORK_STREAM_PACKET_H_
