#ifndef MEDIA_RECORD_FRAMEWORK_TRANSPORT_RECORDING_DEFAULTS_H_
#define MEDIA_RECORD_FRAMEWORK_TRANSPORT_RECORDING_DEFAULTS_H_

#include <string>

// Runtime defaults shared by the recording pipeline nodes (spec 002).
//
// The recording entry (src/examples/dashcam_record.cc) populates these before
// running; tests override them. Configurable recording parameters (duration /
// input image / output path / timestamp format) are a later-proposal feature
// (spec FR-005/006/007), so this feature runs on defaults only — the 001
// PipelineConfig parser does not carry node options.

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

#endif  // MEDIA_RECORD_FRAMEWORK_TRANSPORT_RECORDING_DEFAULTS_H_
