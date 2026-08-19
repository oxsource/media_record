#ifndef MEDIA_RECORD_FRAMEWORK_RUNNER_RECORDING_DEFAULTS_H_
#define MEDIA_RECORD_FRAMEWORK_RUNNER_RECORDING_DEFAULTS_H_

#include <string>

// Runtime defaults shared by the recording pipeline (spec 002).
//
// These are PROGRAMMATIC runtime parameters, not a config schema: the graph
// topology lives only in graph_runtime's GraphConfig (contracts/pipeline-
// contract.md §2); the recording entry fills these defaults from CLI flags /
// decoded input-image dimensions and injects them into each node's
// NodeOptions before running (graph_runtime JsonParser does not carry per-node
// options). Tests override them per scenario.

namespace media::record {

struct RecordingDefaults {
  std::string input_image;         // resolved default input image path
  std::string output_file;         // e.g. "out/dashcam.mp4"
  std::string timestamp_format = "%Y-%m-%d %H:%M:%S";
  int duration_seconds = 10;
  int fps = 30;
  int width = 0;                   // 0 = follow the input image
  int height = 0;
  int bitrate = 4'000'000;         // H.264 bitrate (bps)
  // Set by PipelineRunner before Close(): true when the run failed, so sink
  // nodes (MuxerSink) can discard partial output instead of finalizing it.
  bool pipeline_failed = false;
};

inline RecordingDefaults& Defaults() {
  static RecordingDefaults defaults;
  return defaults;
}

}  // namespace media::record

#endif  // MEDIA_RECORD_FRAMEWORK_RUNNER_RECORDING_DEFAULTS_H_
