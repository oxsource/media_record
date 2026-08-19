#include "src/framework/runner/pipeline_runner.h"

#include <chrono>
#include <cstdio>
#include <thread>

#include "src/framework/config/config_validator.h"
#include "src/framework/node/graph_context.h"
#include "src/framework/node/node_registry.h"
#include "src/framework/public/types.h"
#include "src/framework/runner/runner_state.h"

namespace media::record {

namespace {

// Splits "port:stream" at the first ':'. Mirrors graph_runtime's StreamName().
void SplitPortStream(const std::string& port_stream, std::string* port,
                     std::string* stream) {
  const size_t pos = port_stream.find(':');
  if (pos == std::string::npos) {
    *port = port_stream;
    stream->clear();
    return;
  }
  *port = port_stream.substr(0, pos);
  *stream = port_stream.substr(pos + 1);
}

}  // namespace

PipelineRunner::PipelineRunner(graph::runtime::GraphConfig config,
                               int frame_count, int64_t frame_interval_us)
    : config_(std::move(config)),
      frame_count_(frame_count),
      frame_interval_us_(frame_interval_us) {}

std::string PipelineRunner::StreamName(const std::string& port_stream) {
  std::string port, stream;
  SplitPortStream(port_stream, &port, &stream);
  return stream;
}

std::string PipelineRunner::PortName(const std::string& port_stream) {
  std::string port, stream;
  SplitPortStream(port_stream, &port, &stream);
  return port;
}

bool PipelineRunner::TopologicalOrder(std::vector<int>* order) const {
  const int n = static_cast<int>(config_.nodes.size());
  std::vector<int> indegree(n, 0);
  std::vector<std::vector<int>> deps(n);

  // Map: stream name -> producer node index.
  std::map<std::string, std::vector<int>> producers;
  for (int i = 0; i < n; ++i) {
    for (const std::string& os : config_.nodes[i].output_streams) {
      producers[StreamName(os)].push_back(i);
    }
  }
  // Consumers depend on producers of their input streams.
  for (int i = 0; i < n; ++i) {
    for (const std::string& is : config_.nodes[i].input_streams) {
      const std::string stream = StreamName(is);
      auto it = producers.find(stream);
      if (it == producers.end()) continue;  // external/graph input
      for (int producer : it->second) {
        deps[i].push_back(producer);
        ++indegree[i];
      }
    }
  }

  // Kahn's algorithm; stable tie-break keeps config order.
  std::vector<int> queue;
  for (int i = 0; i < n; ++i) {
    if (indegree[i] == 0) queue.push_back(i);
  }
  std::vector<bool> visited(n, false);
  order->clear();
  while (!queue.empty()) {
    int next = -1;
    for (size_t k = 0; k < queue.size(); ++k) {
      if (!visited[queue[k]]) {
        next = queue[k];
        break;
      }
    }
    if (next < 0) break;
    visited[next] = true;
    order->push_back(next);
    for (int consumer = 0; consumer < n; ++consumer) {
      for (int producer : deps[consumer]) {
        if (producer == next) {
          --indegree[consumer];
          if (indegree[consumer] == 0 && !visited[consumer]) {
            queue.push_back(consumer);
          }
          break;
        }
      }
    }
  }
  return order->size() == static_cast<size_t>(n);
}

RunnerError PipelineRunner::ProcessNode(NodeInstance* inst, bool inputs_done,
                                        bool* produced, int64_t frame_index) {
  graph::runtime::InputStreamShardSet inputs;
  graph::runtime::OutputStreamShardSet outputs;
  graph::runtime::NodeOptions opts;

  // Populate input shards from the per-stream queues (single consumer). At
  // most ONE packet per stream per pass is moved into the shard: nodes consume
  // one packet per Process call and leftovers must stay queued for the next
  // pass (multi-packet shards would be dropped when the shard is discarded).
  for (const std::string& is : inst->def->input_streams) {
    const std::string stream = StreamName(is);
    const std::string port = PortName(is);
    auto& shard = inputs.Get(port);
    auto it = stream_queues_.find(stream);
    if (it != stream_queues_.end() && !it->second.empty()) {
      shard.PushPacket(std::move(it->second.front()));
      it->second.pop_front();
    }
    if (inputs_done || done_streams_.count(stream) > 0) {
      shard.SetDone(true);
    }
  }

  graph::runtime::Timestamp ts(frame_index);
  graph::runtime::GraphContext ctx(inst->name, static_cast<int64_t>(frame_index),
                                   inst->type, ts, &inputs, &outputs, &opts);
  const absl::Status status = inst->node->Process(ctx);
  if (!status.ok() && !graph::runtime::IsStopStatus(status)) {
    return RunnerError::Fail("node '" + inst->name + "': " + status.ToString());
  }
  if (graph::runtime::IsStopStatus(status) && inst->def->input_streams.empty()) {
    inst->active = false;  // source finished
  }

  CollectOutputs(inst, ctx, produced);
  return RunnerError::Ok();
}

void PipelineRunner::CollectOutputs(NodeInstance* inst,
                                    graph::runtime::GraphContext& ctx,
                                    bool* produced) {
  for (const std::string& os : inst->def->output_streams) {
    const std::string stream = StreamName(os);
    const std::string port = PortName(os);
    auto& shard = ctx.Outputs().Get(port);
    auto& queue = shard.OutputQueue();
    if (queue.empty()) continue;
    if (produced != nullptr) *produced = true;
    auto& target = stream_queues_[stream];
    target.splice(target.end(), queue);
  }
}

RunnerError PipelineRunner::Run() {
  // Validate the graph config (graph_runtime's own validator semantics).
  const absl::Status validation = graph::runtime::ConfigValidator::Validate(config_);
  if (!validation.ok()) {
    return RunnerError::Fail(validation.ToString());
  }

  const int n = static_cast<int>(config_.nodes.size());
  if (n == 0) {
    return RunnerError::Fail("pipeline config has no nodes");
  }

  // Instantiate nodes through graph_runtime's registry.
  nodes_.clear();
  nodes_.reserve(n);
  for (const graph::runtime::GraphConfig::NodeDef& def : config_.nodes) {
    std::unique_ptr<graph::runtime::Node> node =
        graph::runtime::NodeFactoryRegistry::CreateByName(def.type, def.name,
                                                          def.options);
    if (node == nullptr) {
      return RunnerError::Fail("node '" + def.name + "': type '" + def.type +
                               "' is not registered");
    }
    NodeInstance inst;
    inst.name = def.name;
    inst.type = def.type;
    inst.def = &def;  // config_ owns the defs; valid for the runner lifetime
    inst.node = std::move(node);
    nodes_.push_back(std::move(inst));
  }

  std::vector<int> order;
  if (!TopologicalOrder(&order)) {
    return RunnerError::Fail("pipeline config contains a cycle");
  }
  std::vector<NodeInstance*> ordered;
  ordered.reserve(n);
  for (int idx : order) ordered.push_back(&nodes_[idx]);

  // Open every node in topological order.
  {
    graph::runtime::InputStreamShardSet inputs;
    graph::runtime::OutputStreamShardSet outputs;
    graph::runtime::NodeOptions opts;
    for (NodeInstance* inst : ordered) {
      graph::runtime::GraphContext ctx(
          inst->name, static_cast<int64_t>(&inst - ordered.data()),
          inst->type, graph::runtime::Timestamp::Unstarted(), &inputs, &outputs,
          &opts);
      const absl::Status status = inst->node->Open(ctx);
      if (!status.ok()) {
        return RunnerError::Fail("node '" + inst->name + "' Open: " +
                                 status.ToString());
      }
    }
  }

  RunnerStateGlobal().pipeline_failed = false;
  stream_queues_.clear();
  done_streams_.clear();

  bool any_source_active = true;
  RunnerError error = RunnerError::Ok();

  // Main frame loop.
  for (int64_t frame = 0; frame < frame_count_ && any_source_active; ++frame) {
    bool produced_this_frame = false;
    for (NodeInstance* inst : ordered) {
      if (inst->def->input_streams.empty() && !inst->active) continue;
      RunnerError st = ProcessNode(inst, /*inputs_done=*/false,
                                   &produced_this_frame, frame);
      if (!st.ok) {
        error = st;
        break;
      }
    }
    if (!error.ok) break;
    // All sources stopped (e.g. frame budget consumed by the sources).
    any_source_active = false;
    for (NodeInstance* inst : ordered) {
      if (inst->def->input_streams.empty() && inst->active) {
        any_source_active = true;
        break;
      }
    }
    if (frame_interval_us_ > 0 && frame + 1 < frame_count_) {
      std::this_thread::sleep_for(
          std::chrono::microseconds(frame_interval_us_));
    }
    (void)produced_this_frame;
  }

  // Drain: mark every declared stream done, then keep processing until all
  // queues are empty (nodes flush/finalize on done+empty input).
  if (error.ok) {
    for (const NodeInstance& inst : nodes_) {
      for (const std::string& is : inst.def->input_streams) {
        done_streams_.insert(StreamName(is));
      }
      for (const std::string& os : inst.def->output_streams) {
        done_streams_.insert(StreamName(os));
      }
    }
    bool any_queue_nonempty = true;
    int pass = 0;
    const int max_passes = n * 16 + 64;  // generous: queues drain 1/pass/node
    while (pass < max_passes && any_queue_nonempty) {
      bool produced = false;
      for (NodeInstance* inst : ordered) {
        if (inst->def->input_streams.empty()) continue;  // sources already done
        RunnerError st = ProcessNode(inst, /*inputs_done=*/true, &produced,
                                     frame_count_ + pass);
        if (!st.ok) {
          error = st;
          break;
        }
      }
      if (!error.ok) break;
      any_queue_nonempty = false;
      for (const auto& kv : stream_queues_) {
        if (!kv.second.empty()) {
          any_queue_nonempty = true;
          break;
        }
      }
      ++pass;
      (void)produced;
    }
  }

  if (!error.ok) RunnerStateGlobal().pipeline_failed = true;

  // Close every node in reverse topological order (cleanup always runs).
  {
    graph::runtime::InputStreamShardSet inputs;
    graph::runtime::OutputStreamShardSet outputs;
    graph::runtime::NodeOptions opts;
    for (auto it = ordered.rbegin(); it != ordered.rend(); ++it) {
      NodeInstance* inst = *it;
      graph::runtime::GraphContext ctx(
          inst->name, static_cast<int64_t>(&inst - ordered.data()),
          inst->type, graph::runtime::Timestamp::Done(), &inputs, &outputs,
          &opts);
      const absl::Status status = inst->node->Close(ctx);
      if (error.ok && !status.ok()) {
        error = RunnerError::Fail("node '" + inst->name + "' Close: " +
                                  status.ToString());
      }
    }
  }

  return error;
}

}  // namespace media::record
