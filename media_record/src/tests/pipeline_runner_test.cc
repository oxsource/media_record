// GraphRuntime-driven async execution unit tests (spec 002, Phase 2
// foundation).
//
// Exercises graph_runtime's own async runtime over registered nodes: a source
// that emits N packets then returns StatusStop(), a sink that drains them,
// error propagation (a failing node aborts the run with its name), and a cycle
// case that must be rejected. The graph is driven inline (Initialize → Start →
// WaitUntilDone → Shutdown) with no dedicated runner module; frame pacing and
// the frame budget live in the source node (spec 002 execution model
// 2026-08-19). Real pipeline nodes land in Phase 3 (US1).

#include <string>

#include "gtest/gtest.h"
#include "src/framework/node/graph_context.h"
#include "src/framework/node/node.h"
#include "src/framework/node/node_contract.h"
#include "src/framework/node/node_options.h"
#include "src/framework/node/node_registry.h"
#include "src/framework/public/graph_runtime.h"
#include "src/framework/stream/packet.h"

namespace media::record {
namespace {

int g_sink_frames = 0;  // observed by tests (nodes are created by the runtime)

// Runs the graph with graph_runtime's own async runtime (the same inline
// lifecycle the dashcam_record entry uses): Initialize → (error callback) →
// Start → WaitUntilDone → Shutdown. Returns ok=false with a locatable message
// on any init/run failure.
struct RunnerError {
  bool ok = true;
  std::string message;
};

RunnerError RunRuntime(graph::runtime::GraphConfig config) {
  graph::runtime::GraphRuntime runtime;
  absl::Status status = runtime.Initialize(config);
  if (!status.ok()) return RunnerError{false, status.ToString()};

  std::string execution_error;
  runtime.SetErrorCallback([&](const absl::Status& s) {
    if (execution_error.empty()) execution_error = s.ToString();
  });

  status = runtime.Start();
  if (!status.ok()) return RunnerError{false, status.ToString()};
  status = runtime.WaitUntilDone();
  if (!status.ok() || runtime.HasError()) {
    return RunnerError{false, !execution_error.empty()
                                  ? execution_error
                                  : (status.ok()
                                         ? absl::InternalError(
                                               "graph execution failed")
                                         : status)
                                        .ToString()};
  }
  runtime.Shutdown();
  return RunnerError{true, ""};
}

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

TEST(GraphRuntimeDriverTest, RunsLineTopologyToCompletion) {
  g_sink_frames = 0;
  // The source's own emit_count bounds the run (the frame budget lives in the
  // source node), so the config carries it and no runner-side budget exists.
  RunnerError status = RunRuntime(MakeLineConfig());
  ASSERT_TRUE(status.ok) << status.message;
  EXPECT_EQ(g_sink_frames, 5);  // source stopped at 5; drained
}

TEST(GraphRuntimeDriverTest, FrameBudgetBoundsSource) {
  graph::runtime::GraphConfig config = MakeLineConfig();
  // The frame budget lives in the source node (StreamInputNode "frame_count"
  // / stub "emit_count"): bound the source directly. It stops after 2 packets
  // and the graph completes once the source stops.
  config.nodes[0].options.Set("emit_count", 2);
  g_sink_frames = 0;
  RunnerError status = RunRuntime(std::move(config));
  ASSERT_TRUE(status.ok) << status.message;
  EXPECT_EQ(g_sink_frames, 2);
}

TEST(GraphRuntimeDriverTest, UnregisteredTypeIsLocatable) {
  graph::runtime::GraphConfig config;
  graph::runtime::GraphConfig::NodeDef def;
  def.name = "ghost";
  def.type = "NoSuchNode";
  config.nodes.push_back(def);

  RunnerError status = RunRuntime(std::move(config));
  ASSERT_FALSE(status.ok);
  EXPECT_NE(status.message.find("ghost"), std::string::npos);
  EXPECT_NE(status.message.find("NoSuchNode"), std::string::npos);
}

TEST(GraphRuntimeDriverTest, FirstNodeErrorAbortsAndNamesNode) {
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

  RunnerError status = RunRuntime(std::move(config));
  ASSERT_FALSE(status.ok);
  EXPECT_NE(status.message.find("fail"), std::string::npos);
  EXPECT_NE(status.message.find("boom"), std::string::npos);
}

TEST(GraphRuntimeDriverTest, CycleIsRejected) {
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

  RunnerError status = RunRuntime(std::move(config));
  ASSERT_FALSE(status.ok);
  EXPECT_NE(status.message.find("cycle"), std::string::npos);
}

}  // namespace
}  // namespace media::record
