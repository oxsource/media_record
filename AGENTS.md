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
- native_ui:     /Users/moks/Develop/docker/ubuntu24/codes/native_ui/native_ui     -> @native_ui//:native_ui (Image decode)
- video_codec:   /Users/moks/Develop/docker/ubuntu24/codes/video_codec/codec       -> @video_codec//src/framework/public:video_codec (VideoEncoder H.264)
MP4 muxing uses media_record's vendored FFmpeg (@ffmpeg//:ffmpeg_codec, libavformat);
RGBA->I420 via vendored libyuv (@libyuv//:libyuv). OSD timestamp drawn with software
bitmap font (native_ui host Surface has no pixel readback). Consume only public
umbrella headers of the three libraries.
Build: bazel build //... ; Test: bazel test //src/tests:all ; Verify: make verify (in media_record/media_record/).
<!-- SPECKIT END -->
