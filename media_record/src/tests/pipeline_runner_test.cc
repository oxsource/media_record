// PipelineRunner unit tests (spec 002, Phase 2 foundation).
//
// Exercises the synchronous frame loop with fake StreamNodes: a source that
// emits one RGBA frame per Process, a sink that drains them, and a cycle case
// that must be rejected. Real pipeline nodes land in Phase 3 (US1).

#include <string>

#include "gtest/gtest.h"
#include "media_record/node.h"
#include "src/framework/config/pipeline_config.h"
#include "src/framework/transport/pipeline_runner.h"
#include "src/framework/transport/stream_node.h"
#include "video_codec/video_codec.h"

namespace media::record {
namespace {

int g_sink_frames = 0;

class TestSourceNode : public StreamNode {
 public:
  NodeStatus Process() override {
    StreamBuffer* out = Output("frames");
    if (!out) return NodeStatus{false, "missing output stream 'frames'"};
    if (out->eos()) return NodeStatus{};  // EOS: stop producing
    video::codec::VideoFrame frame;
    frame.format = video::codec::PixelFormat::kRGBA;
    frame.width = 4;
    frame.height = 4;
    Packet pkt("frames", std::move(frame));
    if (!out->Push(std::move(pkt))) {
      return NodeStatus{false, "push failed (buffer full or EOS)"};
    }
    return NodeStatus{};
  }
};

class TestSinkNode : public StreamNode {
 public:
  NodeStatus Process() override {
    StreamBuffer* in = Input("frames");
    if (!in) return NodeStatus{false, "missing input stream 'frames'"};
    Packet pkt;
    while (in->Pop(&pkt)) {
      if (pkt.IsFrame()) ++g_sink_frames;
    }
    return NodeStatus{};
  }
};

REGISTER_NODE("TestSourceNode", TestSourceNode);
REGISTER_NODE("TestSinkNode", TestSinkNode);

config::PipelineConfig MakeLineConfig() {
  config::PipelineConfig cfg;
  config::PipelineNodeDef src;
  src.name = "src";
  src.type = "TestSourceNode";
  src.output_streams = {"output:frames"};
  config::PipelineNodeDef sink;
  sink.name = "sink";
  sink.type = "TestSinkNode";
  sink.input_streams = {"input:frames"};
  cfg.nodes = {src, sink};

  config::PipelineStreamDef s;
  s.name = "frames";
  s.source_node = "src";
  s.source_port = "output";
  s.dest_node = "sink";
  s.dest_port = "input";
  cfg.streams = {s};
  return cfg;
}

TEST(PipelineRunnerTest, RunsFrameLoopAndDrains) {
  g_sink_frames = 0;
  PipelineRunner runner(MakeLineConfig(), /*frame_count=*/5);
  NodeStatus status = runner.Run();
  ASSERT_TRUE(status.ok) << status.message;
  EXPECT_EQ(g_sink_frames, 5);
}

TEST(PipelineRunnerTest, RejectsCyclicTopology) {
  config::PipelineConfig cfg;
  config::PipelineNodeDef a;
  a.name = "a";
  a.type = "TestSourceNode";
  a.output_streams = {"output:x"};
  config::PipelineNodeDef b;
  b.name = "b";
  b.type = "TestSinkNode";
  b.input_streams = {"input:x"};
  b.output_streams = {"output:y"};
  a.input_streams = {"input:y"};
  cfg.nodes = {a, b};

  config::PipelineStreamDef x;
  x.name = "x";
  x.source_node = "a";
  x.source_port = "output";
  x.dest_node = "b";
  x.dest_port = "input";
  config::PipelineStreamDef y;
  y.name = "y";
  y.source_node = "b";
  y.source_port = "output";
  y.dest_node = "a";
  y.dest_port = "input";
  cfg.streams = {x, y};

  PipelineRunner runner(cfg, /*frame_count=*/1);
  NodeStatus status = runner.Run();
  ASSERT_FALSE(status.ok);
  EXPECT_NE(status.message.find("cycle"), std::string::npos);
}

}  // namespace
}  // namespace media::record
