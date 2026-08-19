#ifndef MEDIA_RECORD_FRAMEWORK_RUNNER_RUNNER_STATE_H_
#define MEDIA_RECORD_FRAMEWORK_RUNNER_RUNNER_STATE_H_

// Process-wide runtime signal shared between PipelineRunner and sink nodes
// (spec 002). NOT a config schema: node params live in the graph config JSON
// (per-node "options", parsed by graph_runtime's JsonParser into
// NodeDef.options). This state only carries runtime plumbing that nodes must
// observe during Close().

namespace media::record {

struct RunnerState {
  // Set by PipelineRunner before Close(): true when the run failed, so sink
  // nodes (MuxerSink) discard partial output instead of finalizing it.
  bool pipeline_failed = false;
};

inline RunnerState& RunnerStateGlobal() {
  static RunnerState state;
  return state;
}

}  // namespace media::record

#endif  // MEDIA_RECORD_FRAMEWORK_RUNNER_RUNNER_STATE_H_
