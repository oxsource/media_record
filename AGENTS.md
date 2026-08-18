<!-- SPECKIT START -->
For additional context about technologies to be used, project structure,
shell commands, and other important information, read the current plan
at `specs/002-dashcam-sim-recording/plan.md`, the design docs at
`specs/002-dashcam-sim-recording/` (research.md, data-model.md, quickstart.md,
contracts/), and the project bootstrap at `media_record/doc/project_bootstrap.md`.
Feature 001 design docs remain at `specs/001-project-architecture/`.

Active feature: dashcam-sim-recording (002) — implement the 7 recorder pipeline
node types as real implementations (StreamInput / SignalSource / MultiViewLayout
/ UiOverlay / VideoEncoder / Recorder / MuxerSink) + a config-driven synchronous
pipeline runner + recording entry. Default run: `bazel run //src/examples:dashcam_record`
(10s, 30fps, image + real-clock OSD timestamp -> out/dashcam.mp4).
Dependencies:
- graph_runtime: /Users/moks/Develop/docker/ubuntu24/codes/graph_runtime/graph_runtime -> @graph_runtime//src/framework/public:runtime (value types/schema only)
- native_ui:     /Users/moks/Develop/docker/ubuntu24/codes/native_ui/native_ui     -> @native_ui//:native_ui (Image decode + flex layout: Container/ExternalImage/Text)
- video_codec:   /Users/moks/Develop/docker/ubuntu24/codes/video_codec/codec       -> @video_codec//src/framework/public:video_codec (VideoEncoder H.264 + Muxer MP4)
Encode + MP4 mux both reuse video_codec's public umbrella (VideoEncoder + Muxer via ByteSink/FileByteSink);
that requires a small PREREQUISITE cross-repo change: export video_codec's io module (byte_sink.h,
file_byte_sink.h) into the public umbrella (public BUILD deps + io visibility + dist headers copy) —
see specs/002-dashcam-sim-recording/contracts/dependency-contract.md D-1. Frame composition uses
native_ui flex layout (Container + ExternalImage + Text) for structure/measurement, then media_record
draws the pixels itself into its own RGBA buffer (host Surface has no pixel readback); RGBA->I420 is a
software converter inside media_record. media_record does NOT directly depend on skia/ffmpeg/libyuv
(third_party BUILD wrappers exist only for the dep repos' *_setup() http_archive label resolution).
Consume only public umbrella headers of the three libraries.
Build: bazel build //... ; Test: bazel test //src/tests:all ; Verify: make verify (in media_record/media_record/).
<!-- SPECKIT END -->
