#ifndef MEDIA_RECORD_FRAMEWORK_RUNNER_PIPELINE_RUNNER_H_
#define MEDIA_RECORD_FRAMEWORK_RUNNER_PIPELINE_RUNNER_H_

#include <cstdint>
#include <list>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

#include "src/framework/config/graph_config.h"
#include "src/framework/node/node.h"
#include "src/framework/runner/recording_defaults.h"
#include "src/framework/stream/packet.h"

// Config-driven synchronous frame-loop driver over graph_runtime nodes
// (spec 002 / contracts/pipeline-contract.md).
//
// Executes a graph::runtime::GraphConfig (parsed by graph_runtime's own
// JsonParser) on the calling thread, following the same pattern as
// graph_runtime's src/examples/string_pipeline.cc: each node is driven through
// a GraphContext (Open / Process / Close) and packets are moved between nodes
// by stream name via the shared shard queues. The graph_runtime GraphRuntime
// executor class does NOT wire internal node-to-node streams, so this thin
// driver is the execution path (contracts/dependency-contract.md D-6).
//
// Run sequence:
//   1. Create every node via graph_runtime::NodeFactoryRegistry::CreateByName
//      (type registered by GRAPH_RUNTIME_REGISTER_NODE; error names node+type).
//   2. Topological order from "port:stream" connectivity (Kahn).
//   3. Open() every node in topological order.
//   4. Main loop: up to |frame_count| frames — Process() every node in
//      topological order (sources first, downstream consumes in the same
//      pass); a source returning StatusStop() is deactivated; the loop ends
//      when the frame budget is reached or all sources stopped. Optional
//      per-frame pacing (|frame_interval_us|) keeps real-time recording.
//   5. Mark every stream done (EOS), then drain: keep Process()ing until all
//      stream queues are empty (nodes flush on done+empty input, e.g. encoder
//      Flush, recorder finalize, muxer trailer + atomic rename).
//   6. Close() every node in reverse topological order.
//
// The first failing node status aborts the run and is returned with the node
// name; MuxerSinkNode discards partial output when the run failed (FR-009).

namespace media::record {

// Fills each node's NodeOptions by node type from |d| (spec 002 / plan.md
// "配置唯一性"): node params are PROGRAMMATIC runtime values injected by the
// caller (CLI flags + defaults), not part of the GraphConfig JSON (graph_runtime
// JsonParser does not parse per-node options).
void ApplyRecordingOptions(graph::runtime::GraphConfig* config,
                           const RecordingDefaults& d);

struct RunnerError {
  bool ok = true;
  std::string message;

  static RunnerError Ok() { return RunnerError(); }
  static RunnerError Fail(std::string message) {
    RunnerError error;
    error.ok = false;
    error.message = std::move(message);
    return error;
  }
};

class PipelineRunner {
 public:
  // |config| is copied; |frame_count| drives the main loop (e.g. 300 =
  // 10s × 30fps); |frame_interval_us| > 0 paces frames (e.g. 33333 ≈ 30fps).
  PipelineRunner(graph::runtime::GraphConfig config, int frame_count,
                 int64_t frame_interval_us = 0);

  RunnerError Run();

 private:
  struct NodeInstance {
    std::string name;
    std::string type;
    const graph::runtime::GraphConfig::NodeDef* def = nullptr;
    std::unique_ptr<graph::runtime::Node> node;
    bool active = true;  // false once a source returned StatusStop()
  };

  // Stream name = part after the first ':' in "port:stream".
  static std::string StreamName(const std::string& port_stream);
  // Port name = part before the first ':' (whole string when no colon).
  static std::string PortName(const std::string& port_stream);

  bool TopologicalOrder(std::vector<int>* order) const;

  // Processes |inst| once with shards populated from the per-stream queues.
  // |inputs_done| marks every declared input stream done (drain phase).
  // On success sets |*produced| when the node emitted packets.
  RunnerError ProcessNode(NodeInstance* inst, bool inputs_done,
                          bool* produced, int64_t frame_index);

  void CollectOutputs(NodeInstance* inst, graph::runtime::GraphContext& ctx,
                      bool* produced);

  graph::runtime::GraphConfig config_;
  int frame_count_;
  int64_t frame_interval_us_;
  std::vector<NodeInstance> nodes_;           // in topological order
  std::map<std::string, std::list<graph::runtime::Packet>> stream_queues_;
  std::set<std::string> done_streams_;
};

}  // namespace media::record

#endif  // MEDIA_RECORD_FRAMEWORK_RUNNER_PIPELINE_RUNNER_H_
