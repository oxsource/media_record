<!-- SPECKIT START -->
For additional context about technologies to be used, project structure,
shell commands, and other important information, read the current plan
at `specs/002-dashcam-sim-recording/plan.md`, the design docs at
`specs/002-dashcam-sim-recording/` (research.md, data-model.md, quickstart.md,
contracts/), and the project bootstrap at `media_record/doc/project_bootstrap.md`.
Feature 001 design docs remain at `specs/001-project-architecture/`.

Active feature: dashcam-sim-recording (002) — implement the 6 recorder pipeline
node types as real implementations (StreamInput / MultiViewLayout / UiOverlay /
VideoEncoder / Recorder / MuxerSink) + a config-driven pipeline runner + recording
entry. Default run: `bazel run //src/examples:dashcam_record`
(10s, 30fps, image + real-clock OSD timestamp -> out/dashcam.mp4).

Execution model (2026-08-19): the 6 node types are `graph::runtime::Node`
subclasses registered via GRAPH_RUNTIME_REGISTER_NODE; graph topology lives ONLY in
graph_runtime's `GraphConfig`, parsed by graph_runtime's OWN `JsonParser`
(@graph_runtime//src/framework/config/json:json_parser, exported via the public
runtime target; no media_record-owned config module/parser, no
framework/transport/ frame-transport layer). The graph is driven by graph_runtime's
OWN runtime: media_record contributed the missing internal stream wiring back into
graph_runtime — `GraphRuntime::Initialize` parses "port:stream" names
(`src/framework/config/stream_name.h` `PortName`/`StreamName`), registers input
ports by port name, wires producer→consumer via `OutputStreamManager::AddMirror`,
and counts only `config.input_streams` towards graph completion; the async path
closes all nodes on completion and propagates node Process errors (see
specs/002-dashcam-sim-recording/contracts/dependency-contract.md D-6).
There is NO media_record-owned runner module: the recording entry
(`src/examples/dashcam_record.cc`) drives graph_runtime's own async runtime inline
(GraphRuntime Initialize → Start → WaitUntilDone → Shutdown); frame pacing and the
frame budget live in StreamInputNode (NodeOptions "fps"/"frame_count"). Node params
(image/output/fps/duration/bitrate/format) live in each node's "options" object of
the config JSON and are parsed by graph_runtime's JsonParser into NodeDef.options;
nodes read them into their own data structures (NodeOptions) at construction.
Optional CLI flags (--image/--output/--frames) patch the matching node options on
top of the config via graph_runtime's own node-option injection
(`GraphRuntime::Options`, a parameter-object with a per-node-type `nodes` map that
`Initialize(config, options)` merges into the config before building).

Dependencies:
- graph_runtime: /Users/moks/Develop/docker/ubuntu24/codes/graph_runtime/graph_runtime -> @graph_runtime//src/framework/public:runtime (Node/NodeRegistry/GraphContext/Packet/Timestamp/GraphConfig/ConfigValidator — full runtime public umbrella, same single-target dep as graph_runtime's own examples)
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
