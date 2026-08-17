<!-- SPECKIT START -->
For additional context about technologies to be used, project structure,
shell commands, and other important information, read the current plan
and the project bootstrap at `media_camera/doc/project_bootstrap.md`.

Media Camera composes three external Bazel repos into a cross-platform
camera recorder framework (configurable node graph):
- graph_runtime: /Users/moks/Develop/docker/ubuntu24/codes/graph_runtime (stream-based graph runtime, Node/Packet/Scheduler)
- native_ui:     /Users/moks/Develop/docker/ubuntu24/codes/native_ui     (UI framework: Widget/Layout/Render/Surface)
- video_codec:   /Users/moks/Develop/docker/ubuntu24/codes/video_codec   (encoding framework: VideoEncoder/AudioEncoder/Muxer)
Nodes: camera_source -> ui_overlay -> video_encoder -> muxer_sink (storage) / stream_sink (streaming).
Consume only public umbrella headers of the three libraries.
<!-- SPECKIT END -->
