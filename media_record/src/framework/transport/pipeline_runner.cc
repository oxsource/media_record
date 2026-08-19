#include "pipeline_runner.h"

#include <algorithm>
#include <queue>
#include <set>
#include <utility>

#include "recording_defaults.h"
#include "stream_node.h"

namespace media::record {

namespace {

constexpr size_t kDefaultBufferCapacity = 100;
constexpr int kMaxDrainPasses = 128;

}  // namespace

PipelineRunner::PipelineRunner(const config::PipelineConfig& config, int frame_count)
    : config_(config), frame_count_(frame_count) {}

bool PipelineRunner::TopologicalOrder(std::vector<std::string>* order) const {
  // Map node name -> set of producer node names (all unique input producers).
  std::map<std::string, std::set<std::string>> producers;
  std::map<std::string, std::vector<std::string>> consumers;
  for (const config::PipelineNodeDef& node : config_.nodes) {
    producers.emplace(node.name, std::set<std::string>{});
    consumers.emplace(node.name, std::vector<std::string>{});
  }
  for (const config::PipelineStreamDef& stream : config_.streams) {
    if (producers.count(stream.dest_node) == 0 ||
        consumers.count(stream.source_node) == 0) {
      continue;  // structural validity is checked by ValidatePipelineConfig
    }
    producers[stream.dest_node].insert(stream.source_node);
    consumers[stream.source_node].push_back(stream.dest_node);
  }

  std::map<std::string, int> indegree;
  std::queue<std::string> ready;
  for (const auto& [name, deps] : producers) {
    indegree[name] = static_cast<int>(deps.size());
    if (deps.empty()) ready.push(name);  // source node: produces without inputs
  }

  order->clear();
  while (!ready.empty()) {
    std::string name = ready.front();
    ready.pop();
    order->push_back(name);
    for (const std::string& consumer : consumers[name]) {
      if (--indegree[consumer] == 0) ready.push(consumer);
    }
  }
  return order->size() == config_.nodes.size();  // false => cycle
}

NodeStatus PipelineRunner::CreateAndWire() {
  for (const config::PipelineNodeDef& def : config_.nodes) {
    std::unique_ptr<Node> node = NodeRegistry::Instance().Create(def.type);
    if (!node) {
      return NodeStatus{false,
                        "unregistered node type '" + def.type + "' (node '" +
                            def.name + "')"};
    }
    nodes_.push_back(NodeInstance{def.name, &def, std::move(node)});
  }

  // Wire StreamNode implementers with their declared input/output buffers.
  for (NodeInstance& inst : nodes_) {
    StreamNode* stream_node = dynamic_cast<StreamNode*>(inst.node.get());
    if (!stream_node) continue;
    std::map<std::string, StreamBuffer*> wiring;
    std::string tag;
    std::string stream_name;
    for (const std::string& port : inst.def->input_streams) {
      config::SplitPortStream(port, &tag, &stream_name);
      auto it = buffers_.find(stream_name);
      if (it != buffers_.end()) wiring[stream_name] = &it->second;
    }
    for (const std::string& port : inst.def->output_streams) {
      config::SplitPortStream(port, &tag, &stream_name);
      auto it = buffers_.find(stream_name);
      if (it != buffers_.end()) wiring[stream_name] = &it->second;
    }
    stream_node->AttachStreams(wiring);
  }
  return NodeStatus{};
}

NodeStatus PipelineRunner::Open() {
  for (NodeInstance& inst : nodes_) {
    NodeStatus status = inst.node->Open();
    if (!status.ok) {
      return NodeStatus{false, "Open() failed on node '" + inst.name + "': " +
                                   status.message};
    }
  }
  return NodeStatus{};
}

NodeStatus PipelineRunner::ProcessAll() {
  for (NodeInstance& inst : nodes_) {
    NodeStatus status = inst.node->Process();
    if (!status.ok) {
      return NodeStatus{false, "Process() failed on node '" + inst.name + "': " +
                                   status.message};
    }
  }
  return NodeStatus{};
}

NodeStatus PipelineRunner::Drain() {
  bool drained = false;
  for (int pass = 0; pass < kMaxDrainPasses && !drained; ++pass) {
    NodeStatus status = ProcessAll();
    if (!status.ok) return status;
    drained = true;
    for (const auto& [name, buffer] : buffers_) {
      (void)name;
      if (!buffer.Empty()) {
        drained = false;
        break;
      }
    }
  }
  if (!drained) {
    return NodeStatus{false,
                      "pipeline drain did not converge (buffers not empty after " +
                          std::to_string(kMaxDrainPasses) + " passes)"};
  }
  return NodeStatus{};
}

NodeStatus PipelineRunner::CloseAll() {
  for (auto it = nodes_.rbegin(); it != nodes_.rend(); ++it) {
    NodeStatus status = it->node->Close();
    if (!status.ok) {
      return NodeStatus{false, "Close() failed on node '" + it->name + "': " +
                                   status.message};
    }
  }
  return NodeStatus{};
}

NodeStatus PipelineRunner::Run() {
  if (frame_count_ < 0) {
    return NodeStatus{false, "negative frame_count"};
  }
  Defaults().pipeline_failed = true;  // flipped to false only on full success

  std::vector<std::string> topo;
  if (!TopologicalOrder(&topo)) {
    return NodeStatus{false, "pipeline graph contains a cycle"};
  }

  // Reorder config_.nodes to match topological order.
  std::map<std::string, config::PipelineNodeDef> by_name;
  for (const config::PipelineNodeDef& def : config_.nodes) by_name[def.name] = def;
  config::PipelineConfig ordered;
  for (const std::string& name : topo) ordered.nodes.push_back(by_name[name]);
  ordered.streams = config_.streams;
  config_ = std::move(ordered);

  // One bounded buffer per stream (data-model.md §5).
  for (const config::PipelineStreamDef& stream : config_.streams) {
    buffers_.emplace(stream.name,
                     StreamBuffer(stream.name, kDefaultBufferCapacity));
  }

  NodeStatus status = CreateAndWire();
  if (!status.ok) return status;

  status = Open();
  if (!status.ok) {
    CloseAll();  // best-effort teardown (e.g. muxer tmp cleanup, FR-009)
    return status;
  }

  for (int i = 0; i < frame_count_; ++i) {
    status = ProcessAll();
    if (!status.ok) {
      CloseAll();
      return status;
    }
  }

  // End-of-stream: no more data will be produced; nodes flush/finalize.
  for (auto& [name, buffer] : buffers_) {
    (void)name;
    buffer.MarkEos();
  }
  status = Drain();
  if (!status.ok) {
    CloseAll();
    return status;
  }

  Defaults().pipeline_failed = false;  // success: sinks may finalize
  return CloseAll();
}

}  // namespace media::record
