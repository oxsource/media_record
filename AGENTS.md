<!-- SPECKIT START -->
For additional context about technologies to be used, project structure,
shell commands, and other important information, read the current plan
at `specs/001-project-architecture/plan.md`, the design docs at
`specs/001-project-architecture/` (research.md, data-model.md, quickstart.md,
contracts/), and the project bootstrap at `media_record/doc/project_bootstrap.md`.

Active feature: project-architecture (001) — Bazel workspace scaffold that
references the three repos via local_repository (paths in media_record_deps.bzl):
- graph_runtime: /Users/moks/Develop/docker/ubuntu24/codes/graph_runtime/graph_runtime -> @graph_runtime//src/framework/public:runtime
- native_ui:     /Users/moks/Develop/docker/ubuntu24/codes/native_ui/native_ui     -> @native_ui//:native_ui
- video_codec:   /Users/moks/Develop/docker/ubuntu24/codes/video_codec/codec       -> @video_codec//src/framework/public:video_codec
Build: bazel build //... ; Test: bazel test //src/tests:all ; Verify: make verify (in media_record/media_record/).
Consume only public umbrella headers of the three libraries.
<!-- SPECKIT END -->
