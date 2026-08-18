#ifndef MEDIA_RECORD_FRAMEWORK_STREAM_PIPELINE_RUNNER_H_
#define MEDIA_RECORD_FRAMEWORK_STREAM_PIPELINE_RUNNER_H_

// Config-driven synchronous pipeline runner (spec 002). Scaffold only: the real
// implementation (topological node ordering + synchronous frame loop + Close
// teardown) lands in task T007.

namespace media::record {

class PipelineRunner;  // TODO(T007): implement config-driven frame-loop runner

}  // namespace media::record

#endif  // MEDIA_RECORD_FRAMEWORK_STREAM_PIPELINE_RUNNER_H_
