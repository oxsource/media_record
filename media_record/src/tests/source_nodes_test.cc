// Source-node unit tests (spec 002, T010).
//
// StreamInputNode decodes the default asset once and emits one RGBA frame per
// Process with real-clock timing; SignalSourceNode emits one kTick per Process.
// Both are wired with a StreamBuffer directly (no runner).

#include <cstdlib>
#include <memory>
#include <string>

#include "gtest/gtest.h"
#include "src/framework/transport/packet.h"
#include "src/framework/transport/recording_defaults.h"
#include "src/framework/transport/stream_buffer.h"
#include "src/framework/transport/stream_node.h"
#include "src/nodes/signal_source/signal_source_node.h"
#include "src/nodes/stream_input/stream_input_node.h"
#include "video_codec/video_codec.h"

namespace media::record {
namespace {

std::string ResolveAsset() {
  if (const char* src = std::getenv("TEST_SRCDIR")) {
    std::string candidate = std::string(src) +
                            "/media_record/src/examples/assets/dashcam_default.png";
    // Path exists check without <filesystem> gymnastics: try opening it.
    if (FILE* f = std::fopen(candidate.c_str(), "rb")) {
      std::fclose(f);
      return candidate;
    }
  }
  return "src/examples/assets/dashcam_default.png";
}

TEST(StreamInputNodeTest, EmitsOneRgbaFramePerProcess) {
  RecordingDefaults& d = Defaults();
  d.input_image = ResolveAsset();
  d.fps = 30;

  StreamBuffer out("frames", 16);
  StreamInputNode node;
  node.AttachStreams({{"frames", &out}});

  NodeStatus status = node.Open();
  ASSERT_TRUE(status.ok) << status.message;

  status = node.Process();
  ASSERT_TRUE(status.ok) << status.message;

  Packet pkt;
  ASSERT_TRUE(out.Pop(&pkt));
  EXPECT_TRUE(pkt.IsFrame());
  const auto& frame = std::get<video::codec::VideoFrame>(pkt.payload());
  EXPECT_EQ(frame.format, video::codec::PixelFormat::kRGBA);
  EXPECT_GT(frame.width, 0);
  EXPECT_GT(frame.height, 0);
  EXPECT_FALSE(frame.planes[0].empty());
  EXPECT_GT(frame.timestamp_us, 0);

  // Second Process emits a second frame.
  ASSERT_TRUE(node.Process().ok);
  ASSERT_TRUE(out.Pop(&pkt));
  EXPECT_TRUE(pkt.IsFrame());
}

TEST(SignalSourceNodeTest, EmitsTickEvents) {
  StreamBuffer out("signals", 16);
  SignalSourceNode node;
  node.AttachStreams({{"signals", &out}});
  ASSERT_TRUE(node.Process().ok);

  Packet pkt;
  ASSERT_TRUE(out.Pop(&pkt));
  EXPECT_TRUE(pkt.IsSignal());
  const auto& ev = std::get<SignalEvent>(pkt.payload());
  EXPECT_EQ(ev.type, SignalEvent::Type::kTick);
  EXPECT_GT(ev.timestamp_us, 0);
}

}  // namespace
}  // namespace media::record
