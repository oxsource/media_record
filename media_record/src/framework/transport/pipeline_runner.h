#ifndef MEDIA_RECORD_FRAMEWORK_STREAM_PIPELINE_RUNNER_H_
#define MEDIA_RECORD_FRAMEWORK_STREAM_PIPELINE_RUNNER_H_

#include <map>
#include <memory>
#include <string>
#include <vector>

#include "media_record/node.h"
#include "src/framework/config/pipeline_config.h"
#include "stream_buffer.h"

// Config-driven synchronous pipeline runner (spec 002 / contracts/
// pipeline-contract.md).
//
// Loads a parsed PipelineConfig, instantiates every node through
// NodeRegistry::Create(type), connects the config's streams[] to StreamBuffers
// (data-model.md §5), and drives a synchronous frame loop:
//
//   1. Open() every node in topological order.
//   2. For frame_count iterations, Process() every node in topological order
//      (sources run first, downstream nodes consume in the same pass).
//   3. Mark every stream EOS, then drain: keep Process()ing until all buffers
//      are empty (nodes detect EOS + empty input to flush/finalize, e.g.
//      encoder drain, recorder session end, muxer trailer).
//   4. Close() every node in reverse topological order.
//
// The first failing NodeStatus aborts the run and is returned. The runner is
// synchronous and single-threaded (spec assumption "同步、有限时长的批处理").

namespace media::record {

class PipelineRunner {
 public:
  // |config| is copied; |frame_count| drives the main loop (e.g. 300 =
  // 10s × 30fps).
  PipelineRunner(const config::PipelineConfig& config, int frame_count);
  ~PipelineRunner() = default;

  PipelineRunner(const PipelineRunner&) = delete;
  PipelineRunner& operator=(const PipelineRunner&) = delete;

  NodeStatus Run();

 private:
  struct NodeInstance {
    std::string name;
    const config::PipelineNodeDef* def = nullptr;
    std::unique_ptr<Node> node;  // owns the node instance
  };

  // Returns node names in topological order; false on a cycle.
  bool TopologicalOrder(std::vector<std::string>* order) const;
  NodeStatus CreateAndWire();
  NodeStatus Open();
  NodeStatus ProcessAll();
  NodeStatus Drain();
  NodeStatus CloseAll();

  config::PipelineConfig config_;
  int frame_count_;
  std::map<std::string, StreamBuffer> buffers_;  // stream_name -> buffer
  std::vector<NodeInstance> nodes_;              // in topological order
};

}  // namespace media::record

#endif  // MEDIA_RECORD_FRAMEWORK_STREAM_PIPELINE_RUNNER_H_
