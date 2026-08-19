#include "src/nodes/stream_input/stream_input_node.h"

#include <chrono>
#include <memory>
#include <string>

#include "native_ui/render.h"
#include "src/framework/transport/packet.h"
#include "src/framework/transport/recording_defaults.h"
#include "video_codec/video_codec.h"

namespace media::record {

namespace {

int64_t NowUs() {
  using namespace std::chrono;
  return duration_cast<microseconds>(system_clock::now().time_since_epoch())
      .count();
}

}  // namespace

NodeStatus StreamInputNode::Open() {
  const std::string& path = Defaults().input_image;
  if (path.empty()) {
    return NodeStatus{false,
                      "stream_input: no input image configured "
                      "(RecordingDefaults::input_image)"};
  }
  std::unique_ptr<native::ui::Image> image = native::ui::Image::FromFile(path.c_str());
  if (!image) {
    return NodeStatus{false, "stream_input: cannot decode input image '" + path +
                                 "' (missing or unsupported format)"};
  }
  width_ = image->width();
  height_ = image->height();
  image_.resize(static_cast<size_t>(width_) * height_ * 4);
  if (!image->CopyPixels(width_, height_, static_cast<size_t>(width_) * 4,
                         image_.data())) {
    return NodeStatus{false, "stream_input: cannot read pixels of input image '" +
                                 path + "'"};
  }
  frame_index_ = 0;
  return NodeStatus{};
}

NodeStatus StreamInputNode::Process() {
  StreamBuffer* out = Output("frames");
  if (out == nullptr) {
    return NodeStatus{false, "stream_input: missing output stream 'frames'"};
  }
  if (out->eos()) return NodeStatus{};  // recording ended; stop producing

  video::codec::VideoFrame frame;
  frame.format = video::codec::PixelFormat::kRGBA;
  frame.width = width_;
  frame.height = height_;
  frame.stride[0] = width_ * 4;
  frame.planes[0] = image_;
  frame.timestamp_us = NowUs();

  const int64_t pts = frame_index_ * 1000000LL /
                      (Defaults().fps > 0 ? Defaults().fps : 30);
  Packet pkt("frames", std::move(frame), pts);
  if (!out->Push(std::move(pkt))) {
    return NodeStatus{false, "stream_input: output buffer full or EOS"};
  }
  ++frame_index_;
  return NodeStatus{};
}

REGISTER_NODE("StreamInputNode", StreamInputNode);

}  // namespace media::record
