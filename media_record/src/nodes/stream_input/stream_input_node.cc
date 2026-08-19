#include "src/nodes/stream_input/stream_input_node.h"

#include <chrono>
#include <memory>
#include <string>

#include "native_ui/render.h"
#include "src/framework/node/graph_context.h"
#include "src/framework/node/node_contract.h"
#include "src/framework/node/node_options.h"
#include "src/framework/node/node_registry.h"
#include "src/framework/public/types.h"
#include "src/framework/stream/packet.h"
#include "video_codec/video_codec.h"

namespace media::record {

namespace {

int64_t NowUs() {
  using namespace std::chrono;
  return duration_cast<microseconds>(system_clock::now().time_since_epoch())
      .count();
}

}  // namespace

StreamInputNode::StreamInputNode(const std::string& name,
                                 const graph::runtime::NodeOptions& options)
    : Node(name) {
  if (const std::string* v = options.Get<std::string>("image")) {
    image_path_ = *v;
  }
  if (const int* v = options.Get<int>("width")) width_ = *v;
  if (const int* v = options.Get<int>("height")) height_ = *v;
  if (const int* v = options.Get<int>("fps")) fps_ = *v;
  if (const int* v = options.Get<int>("frame_count")) frame_count_ = *v;
  if (fps_ <= 0) fps_ = 30;
  if (frame_count_ <= 0) frame_count_ = 300;
}

absl::Status StreamInputNode::GetContract(graph::runtime::NodeContract* c) {
  c->Outputs().Get("output").Set<video::codec::VideoFrame>();
  return absl::OkStatus();
}

absl::Status StreamInputNode::Open(graph::runtime::GraphContext&) {
  if (image_path_.empty()) {
    return absl::InvalidArgumentError(
        "stream_input: no input image configured (NodeOptions 'image')");
  }
  std::unique_ptr<native::ui::Image> image =
      native::ui::Image::FromFile(image_path_.c_str());
  if (!image) {
    return absl::InvalidArgumentError(
        "stream_input: cannot decode input image '" + image_path_ +
        "' (missing or unsupported format)");
  }
  if (width_ <= 0) width_ = image->width();
  if (height_ <= 0) height_ = image->height();
  image_.resize(static_cast<size_t>(width_) * height_ * 4);
  if (!image->CopyPixels(width_, height_, static_cast<size_t>(width_) * 4,
                         image_.data())) {
    return absl::InvalidArgumentError(
        "stream_input: cannot read pixels of input image '" + image_path_ +
        "'");
  }
  frame_index_ = 0;
  return absl::OkStatus();
}

absl::Status StreamInputNode::Close(graph::runtime::GraphContext&) {
  return absl::OkStatus();
}

absl::Status StreamInputNode::Process(graph::runtime::GraphContext& ctx) {
  if (frame_index_ >= frame_count_) {
    return graph::runtime::StatusStop();
  }

  video::codec::VideoFrame frame;
  frame.format = video::codec::PixelFormat::kRGBA;
  frame.width = width_;
  frame.height = height_;
  frame.stride[0] = width_ * 4;
  frame.planes[0] = image_;
  frame.timestamp_us = NowUs();

  const int64_t pts = frame_index_ * 1000000LL / fps_;
  auto pkt = graph::runtime::Packet::MakePacket<video::codec::VideoFrame>(
                 std::move(frame))
                 .At(graph::runtime::Timestamp(pts));
  ctx.Outputs().Get("output").AddPacket(std::move(pkt));
  ++frame_index_;
  return absl::OkStatus();
}

namespace { using media::record::StreamInputNode; }
GRAPH_RUNTIME_REGISTER_NODE("StreamInputNode", StreamInputNode);

}  // namespace media::record
