// PipelineRunner unit tests (spec 002, Phase 2 foundation).
//
// Exercises the synchronous frame loop over graph_runtime nodes: a source that
// emits N packets per frame then returns StatusStop(), a sink that drains them,
// error propagation (a failing node aborts the run with its name), and a cycle
// case that must be rejected. Real pipeline nodes land in Phase 3 (US1).

#include <string>

#include "gtest/gtest.h"
#include "src/framework/node/graph_context.h"
#include "src/framework/node/node_contract.h"
#include "src/framework/node/node_options.h"
#include "src/framework/node/node_registry.h"
#include "src/framework/runner/pipeline_runner.h"
#include "src/framework/stream/packet.h"

namespace media::record {
namespace {

int g_sink_frames = 0;  // observed by tests (nodes are created by the runner)

// --- Stub graph_runtime nodes ---------------------------------------------

class TestSourceNode : public graph::runtime::Node {
 public:
  TestSourceNode(const std::string& name,
                 const graph::runtime::NodeOptions& options)
      : Node(name) {
    if (const int* v = options.Get<int>("emit_count")) emit_count_ = *v;
  }

  static absl::Status GetContract(graph::runtime::NodeContract* c) {
    c->Outputs().Get("output").Set<int>();
    return absl::OkStatus();
  }

  absl::Status Open(graph::runtime::GraphContext&) override { return absl::OkStatus(); }
  absl::Status Close(graph::runtime::GraphContext&) override { return absl::OkStatus(); }
  absl::Status Process(graph::runtime::GraphContext& ctx) override {
    if (sent_ >= emit_count_) return graph::runtime::StatusStop();
    ctx.Outputs().Get("output").AddPacket(
        graph::runtime::Packet::MakePacket<int>(sent_));
    ++sent_;
    return absl::OkStatus();
  }

  int sent_ = 0;
  int emit_count_ = 5;
};

class TestSinkNode : public graph::runtime::Node {
 public:
  TestSinkNode(const std::string& name,
               const graph::runtime::NodeOptions& options)
      : Node(name) {}

  static absl::Status GetContract(graph::runtime::NodeContract* c) {
    c->Inputs().Get("input").Set<int>();
    return absl::OkStatus();
  }

  absl::Status Open(graph::runtime::GraphContext&) override { return absl::OkStatus(); }
  absl::Status Close(graph::runtime::GraphContext&) override { return absl::OkStatus(); }
  absl::Status Process(graph::runtime::GraphContext& ctx) override {
    auto& in = ctx.Inputs().Get("input");
    if (!in.IsEmpty()) ++g_sink_frames;
    return absl::OkStatus();
  }
};

class TestFailNode : public graph::runtime::Node {
 public:
  TestFailNode(const std::string& name,
               const graph::runtime::NodeOptions& options)
      : Node(name) {}

  static absl::Status GetContract(graph::runtime::NodeContract* c) {
    c->Inputs().Get("input").Set<int>();
    return absl::OkStatus();
  }

  absl::Status Open(graph::runtime::GraphContext&) override { return absl::OkStatus(); }
  absl::Status Close(graph::runtime::GraphContext&) override { return absl::OkStatus(); }
  absl::Status Process(graph::runtime::GraphContext& ctx) override {
    return absl::InternalError("boom");
  }
};

}  // namespace
}  // namespace media::record

namespace { using media::record::TestSourceNode; }
GRAPH_RUNTIME_REGISTER_NODE("TestSourceNode", TestSourceNode);
namespace { using media::record::TestSinkNode; }
GRAPH_RUNTIME_REGISTER_NODE("TestSinkNode", TestSinkNode);
namespace { using media::record::TestFailNode; }
GRAPH_RUNTIME_REGISTER_NODE("TestFailNode", TestFailNode);

namespace media::record {
namespace {

graph::runtime::GraphConfig MakeLineConfig() {
  graph::runtime::GraphConfig config;
  graph::runtime::GraphConfig::NodeDef src;
  src.name = "src";
  src.type = "TestSourceNode";
  src.output_streams = {"output:frames"};
  src.options.Set("emit_count", 5);
  graph::runtime::GraphConfig::NodeDef sink;
  sink.name = "sink";
  sink.type = "TestSinkNode";
  sink.input_streams = {"input:frames"};
  config.nodes.push_back(src);
  config.nodes.push_back(sink);
  return config;
}

TEST(PipelineRunnerTest, RunsLineTopologyToCompletion) {
  g_sink_frames = 0;
  PipelineRunner runner(MakeLineConfig(), /*frame_count=*/20);
  RunnerError status = runner.Run();
  ASSERT_TRUE(status.ok) << status.message;
  EXPECT_EQ(g_sink_frames, 5);  // source stopped at 5; drained
}

TEST(PipelineRunnerTest, FrameBudgetBoundsSource) {
  graph::runtime::GraphConfig config = MakeLineConfig();
  // Source is configured for 5 packets, but the frame budget is 2: the runner
  // must not call the source beyond the budget.
  g_sink_frames = 0;
  PipelineRunner runner(std::move(config), /*frame_count=*/2);
  RunnerError status = runner.Run();
  ASSERT_TRUE(status.ok) << status.message;
  EXPECT_EQ(g_sink_frames, 2);
}

TEST(PipelineRunnerTest, UnregisteredTypeIsLocatable) {
  graph::runtime::GraphConfig config;
  graph::runtime::GraphConfig::NodeDef def;
  def.name = "ghost";
  def.type = "NoSuchNode";
  config.nodes.push_back(def);

  PipelineRunner runner(std::move(config), /*frame_count=*/1);
  RunnerError status = runner.Run();
  ASSERT_FALSE(status.ok);
  EXPECT_NE(status.message.find("ghost"), std::string::npos);
  EXPECT_NE(status.message.find("NoSuchNode"), std::string::npos);
}

TEST(PipelineRunnerTest, FirstNodeErrorAbortsAndNamesNode) {
  graph::runtime::GraphConfig config;
  graph::runtime::GraphConfig::NodeDef src;
  src.name = "src";
  src.type = "TestSourceNode";
  src.output_streams = {"output:frames"};
  src.options.Set("emit_count", 5);
  graph::runtime::GraphConfig::NodeDef fail;
  fail.name = "fail";
  fail.type = "TestFailNode";
  fail.input_streams = {"input:frames"};
  config.nodes.push_back(src);
  config.nodes.push_back(fail);

  PipelineRunner runner(std::move(config), /*frame_count=*/5);
  RunnerError status = runner.Run();
  ASSERT_FALSE(status.ok);
  EXPECT_NE(status.message.find("fail"), std::string::npos);
  EXPECT_NE(status.message.find("boom"), std::string::npos);
}

TEST(PipelineRunnerTest, CycleIsRejected) {
  graph::runtime::GraphConfig config;
  graph::runtime::GraphConfig::NodeDef a;
  a.name = "a";
  a.type = "TestSinkNode";
  a.input_streams = {"input:y"};
  a.output_streams = {"output:x"};
  graph::runtime::GraphConfig::NodeDef b;
  b.name = "b";
  b.type = "TestSinkNode";
  b.input_streams = {"input:x"};
  b.output_streams = {"output:y"};
  config.nodes.push_back(a);
  config.nodes.push_back(b);

  PipelineRunner runner(std::move(config), /*frame_count=*/1);
  RunnerError status = runner.Run();
  ASSERT_FALSE(status.ok);
  EXPECT_NE(status.message.find("cycle"), std::string::npos);
}

}  // namespace
}  // namespace media::record
