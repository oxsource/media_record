// Source-node unit tests (spec 002, T010; graph_runtime node model).
//
// StreamInputNode decodes the default asset once and emits one RGBA frame per
// Process with real-clock timing. It is driven directly through a GraphContext
// (same pattern as graph_runtime's src/examples/string_pipeline.cc), reading
// the output shard after each Process.

#include <cstdlib>
#include <cstdio>
#include <string>

#include "gtest/gtest.h"
#include "graph_runtime/graph_context.h"
#include "graph_runtime/node_contract.h"
#include "graph_runtime/node_options.h"
#include "graph_runtime/packet.h"
#include "src/nodes/stream_input/stream_input_node.h"
#include "video_codec/video_codec.h"

namespace media::record {
namespace {

std::string ResolveAsset() {
  if (const char* src = std::getenv("TEST_SRCDIR")) {
    std::string candidate = std::string(src) +
                            "/media_record/src/examples/assets/dashcam_default.png";
    if (FILE* f = std::fopen(candidate.c_str(), "rb")) {
      std::fclose(f);
      return candidate;
    }
  }
  return "src/examples/assets/dashcam_default.png";
}

graph::runtime::NodeOptions MakeInputOptions(const std::string& image,
                                             int frame_count) {
  graph::runtime::NodeOptions options;
  options.Set("image", image);
  options.Set("fps", 30);
  options.Set("frame_count", frame_count);
  return options;
}

TEST(StreamInputNodeTest, EmitsOneRgbaFramePerProcess) {
  graph::runtime::NodeOptions options = MakeInputOptions(ResolveAsset(), 300);
  StreamInputNode node("input", options);

  graph::runtime::InputStreamShardSet inputs;
  graph::runtime::OutputStreamShardSet outputs;
  graph::runtime::GraphContext open_ctx("input", 0, "StreamInputNode",
                                        graph::runtime::Timestamp::Unstarted(),
                                        &inputs, &outputs, &options);
  ASSERT_TRUE(node.Open(open_ctx).ok());

  graph::runtime::GraphContext ctx("input", 1, "StreamInputNode",
                                   graph::runtime::Timestamp(0), &inputs,
                                   &outputs, &options);
  ASSERT_TRUE(node.Process(ctx).ok());

  auto& queue = outputs.Get("output").OutputQueue();
  ASSERT_FALSE(queue.empty());
  graph::runtime::Packet pkt = std::move(queue.front());
  queue.pop_front();
  auto frame_or = pkt.Share<video::codec::VideoFrame>();
  ASSERT_TRUE(frame_or.ok());
  EXPECT_EQ(frame_or->get()->format, video::codec::PixelFormat::kRGBA);
  EXPECT_GT(frame_or->get()->width, 0);
  EXPECT_GT(frame_or->get()->height, 0);
  EXPECT_FALSE(frame_or->get()->planes[0].empty());
  EXPECT_GT(frame_or->get()->timestamp_us, 0);

  // Second Process emits a second frame.
  ASSERT_TRUE(node.Process(ctx).ok());
  ASSERT_FALSE(outputs.Get("output").OutputQueue().empty());
}

TEST(StreamInputNodeTest, StopsAfterFrameBudget) {
  graph::runtime::NodeOptions options = MakeInputOptions(ResolveAsset(), 1);
  StreamInputNode node("input", options);

  graph::runtime::InputStreamShardSet inputs;
  graph::runtime::OutputStreamShardSet outputs;
  graph::runtime::GraphContext open_ctx("input", 0, "StreamInputNode",
                                        graph::runtime::Timestamp::Unstarted(),
                                        &inputs, &outputs, &options);
  ASSERT_TRUE(node.Open(open_ctx).ok());

  graph::runtime::GraphContext ctx("input", 1, "StreamInputNode",
                                   graph::runtime::Timestamp(0), &inputs,
                                   &outputs, &options);
  EXPECT_TRUE(node.Process(ctx).ok());
  EXPECT_FALSE(outputs.Get("output").OutputQueue().empty());

  // Budget exhausted -> StatusStop.
  EXPECT_TRUE(graph::runtime::IsStopStatus(node.Process(ctx)));
}

}  // namespace
}  // namespace media::record
