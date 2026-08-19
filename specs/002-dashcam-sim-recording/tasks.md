---

description: "Task list for feature 002-dashcam-sim-recording implementation"
---

# Tasks: 模拟行车记录仪录制（Dashcam Simulated Recording）

**Input**: Design documents from `/specs/002-dashcam-sim-recording/`

**Prerequisites**: plan.md (required), spec.md (required for user stories), research.md, data-model.md, contracts/

**Tests**: Test tasks are included because spec.md's User Stories define Independent Tests and Success Criteria SC-004 requires automated verification (`make verify` covers build + tests + artifact check).

**Organization**: Tasks are grouped by user story to enable independent implementation and testing of each story.

> **架构基线（2026-08-18 重设计）**：本列表以「graph_runtime 节点 + 其 GraphConfig 配置 + media_record 同步驱动器」为准（见 plan.md / contracts/）。**不**再实现 media_record 自有帧传输层（transport/）与自有配置（framework/config）。

## Format: `[ID] [P?] [Story] Description`

- **[P]**: Can run in parallel (different files, no dependencies)
- **[Story]**: Which user story this task belongs to (e.g., US1, US3)
- Include exact file paths in descriptions

## Path Conventions

- Source root is the Bazel workspace `media_record/media_record/`; `src/...` paths below are relative to it.
- Task T004 (video_codec umbrella prerequisite) targets the sibling repo `/Users/moks/Develop/docker/ubuntu24/codes/video_codec/codec`.

---

## Phase 1: Setup (Shared Infrastructure)

**Purpose**: Scaffold the new feature's shared pieces so story work can start.

- [x] T001 Create sync-runner module scaffold: `src/framework/runner/BUILD.bazel` declaring `cc_library(name = "runner")` (headers: `pipeline_runner.h`), dep on `@graph_runtime//src/framework/public:runtime` (nodes/config/types via the single public umbrella, same as graph_runtime's own `src/examples/*` deps). **Removed**: `src/framework/transport/` and `src/framework/config/` (media_record 自有帧传输与配置不再存在；数据通路用 graph_runtime 的 `Node`/`GraphContext`/`Packet`，配置用其 `GraphConfig`)
- [x] T002 [P] Add default input image `src/examples/assets/dashcam_default.png` (1280×720 RGBA-friendly PNG) + `src/examples/assets/BUILD.bazel` exposing it as a `filegroup`/`exports_files` so the example can locate it via `$(location)`
- [x] T003 [P] Add default runnable config `src/examples/configs/dashcam_record.json` (**graph_runtime JSON schema**: 6 nodes with `input_streams`/`output_streams` in `"port:stream"` form; no `streams[]` section, no per-node `options`; 5 implicit streams: `frames`, `view_frames`, `osd_frames`, `es_packets`, `clips`) and register it in `src/examples/configs/BUILD.bazel`; convert `recorder.json` / `stream.json` / `preview.json` to the same schema

---

## Phase 2: Foundational (Blocking Prerequisites)

**Purpose**: Core infrastructure that MUST be complete before ANY user story can be implemented

**⚠️ CRITICAL**: No user story work can begin until this phase is complete

- [x] T004 [P] PREREQUISITE (cross-repo): export video_codec `io` into the public umbrella in `/Users/moks/Develop/docker/ubuntu24/codes/video_codec/codec`: add `@video_codec//src/framework/io` to the deps of `video_codec` and `video_codec_hdrs` in `src/framework/public/BUILD.bazel`, relax visibility in `src/framework/io/BUILD.bazel` to allow the public target, copy `byte_sink.h` / `file_byte_sink.h` into `dist/host/include/video_codec/` (and `dist/android-arm64/include/video_codec/`), and add an umbrella compile smoke test under `tests/`; verify with `bazel build //src/framework/public:video_codec` + `bazel test //tests:all` in that repo (see `specs/002-dashcam-sim-recording/contracts/dependency-contract.md` D-1)
- [x] T005 [P] Implement `PipelineRunner` in `src/framework/runner/pipeline_runner.{h,cc}`: accepts `graph::runtime::GraphConfig` (+ per-node `NodeOptions` merged by the caller), creates nodes via `NodeFactoryRegistry::CreateByName(type, name, options)`, topological order from `"port:stream"` connectivity, synchronous frame loop with `GraphContext` (`Open`/`Process`/`Close`), Packet routing between nodes by stream name, source `StatusStop()` end detection, EOS + drain, first-error abort; unit test `src/tests/pipeline_runner_test.cc` with stub `graph::runtime::Node`s (no media_record transport/config involved)

**Checkpoint**: Foundation ready - user story implementation can now begin

---

## Phase 3: User Story 1 - 一键生成模拟行车记录仪视频 (Priority: P1) 🎯 MVP

**Goal**: Running `bazel run //src/examples:dashcam_record` produces a playable 10s H.264+MP4 at `out/dashcam.mp4` containing the default image scaled to the frame plus a real-clock OSD timestamp that increments each frame, then exits 0.

**Independent Test**: `bazel run //src/examples:dashcam_record` → after ~10s, exit code 0; `out/dashcam.mp4` exists, is playable, duration ≈10s (≤5% error), and frames show the input image with a corner timestamp that changes over time (SC-001/002/003).

### Tests for User Story 1 (write first, expect FAIL before implementation) ⚠️

- [x] T008 [P] [US1] Unit test for software RGBA→I420 conversion in `src/tests/rgba_i420_test.cc` (known RGBA pattern → expected Y/U/V planes, BT.601, correct dimensions)
- [x] T009 [P] [US1] Unit test for bitmap-font timestamp renderer in `src/tests/bitmap_font_test.cc` (glyphs blit at expected region for `YYYY-MM-DD HH:MM:SS`, two different times produce different pixels)
- [x] T010 [P] [US1] Unit tests for source nodes in `src/tests/source_nodes_test.cc`: `StreamInputNode` (graph_runtime node) decodes `dashcam_default.png` and emits one RGBA `VideoFrame` per Process with real-clock pts

### Implementation for User Story 1

- [x] T011 [P] [US1] Implement software RGBA→I420 converter `src/nodes/video_encoder/rgba_i420.{h,cc}` (per-row RGB→YUV BT.601 + 2×2 chroma downsample; `VideoFrame` in `kRGBA`, out `kI420`) + register in `src/nodes/video_encoder/BUILD.bazel`
- [x] T012 [P] [US1] Implement bitmap-font glyph table + renderer `src/nodes/ui_overlay/bitmap_font.{h,cc}` (embedded 5×7/8×13 digits + separators; draws white-on-semi-transparent-black into an RGBA buffer at a given rect) + register in `src/nodes/ui_overlay/BUILD.bazel`
- [x] T013 [P] [US1] Implement `StreamInputNode` in `src/nodes/stream_input/stream_input_node.{h,cc}` as `graph::runtime::Node` (source, no input ports; output `output:frames`): `Open` decodes the default image via native_ui `Image::FromFile` + `CopyPixels` into RGBA; each `Process` emits `Packet::MakePacket<video::codec::VideoFrame>(...)` with real-clock timestamp + `At(Timestamp)`; returns `StatusStop()` after `frame_count` (from `NodeOptions`) + register via `GRAPH_RUNTIME_REGISTER_NODE` in `src/nodes/stream_input/BUILD.bazel`
- [x] T015 [P] [US1] Implement `MultiViewLayoutNode` in `src/nodes/multi_view_layout/multi_view_layout_node.{h,cc}` as `graph::runtime::Node` (input `f:frames`, output `output:view_frames`): build native_ui flex tree (`Container` + `ExternalImage`, fill frame), `Layout(w,h)`, software-blit the input frame pixels into the node's own RGBA `VideoFrame` buffer (frame fills whole frame; single-view only, multi-input reserved) + register in `src/nodes/multi_view_layout/BUILD.bazel` with `@native_ui` dep
- [x] T016 [US1] Implement `UiOverlayNode` in `src/nodes/ui_overlay/ui_overlay_node.{h,cc}` as `graph::runtime::Node` (input `video:view_frames`, output `output:osd_frames`): compute timestamp position via native_ui flex (`Container` + `Text`, default bottom-right), render real-clock `%Y-%m-%d %H:%M:%S` text with `bitmap_font` into the incoming RGBA frame's top layer + register in `src/nodes/ui_overlay/BUILD.bazel` (depends on T012)
- [x] T017 [US1] Implement `VideoEncoderNode` in `src/nodes/video_encoder/video_encoder_node.{h,cc}` as `graph::runtime::Node` (input `input:osd_frames`, output `output:es_packets`): software RGBA→I420 (`rgba_i420`) then `CodecFactory::CreateVideo` (H.264, `input_format: kI420`, 30fps) → `Init` → per-frame pull-mode `Encode(VideoFrame)` → `Packet<VideoPacket>` (Annex-B) → `Flush` at input end + register in `src/nodes/video_encoder/BUILD.bazel` with `@video_codec//src/framework/public:video_codec` dep (depends on T011)
- [x] T018 [P] [US1] Implement `RecorderNode` in `src/nodes/recorder/recorder_node.{h,cc}` as `graph::runtime::Node` (input `input:es_packets`, output `output:clips`): single-session single-segment lifecycle, counts frames to `duration_seconds × fps` (300), forwards `VideoPacket`s, finalizes on input-done + register in `src/nodes/recorder/BUILD.bazel`
- [x] T019 [US1] Implement `MuxerSinkNode` in `src/nodes/muxer_sink/muxer_sink_node.{h,cc}` as `graph::runtime::Node` (input `input:clips`): `CodecFactory::CreateMuxer` (`MuxFormat::kMp4`, `fragmented=false` for file output) + `SetOutput(FileByteSink)` on temp file `out/.dashcam.mp4.tmp`, `Push(VideoPacket)` per packet, `Finish()` writes trailer, then atomic rename to `out/dashcam.mp4`; log when overwriting an existing file + register in `src/nodes/muxer_sink/BUILD.bazel` with `@video_codec` public umbrella dep (depends on T004)
- [x] T020 [US1] Wire the runnable entry `src/examples/dashcam_record.cc`: parse `dashcam_record.json` (graph_runtime schema) into `GraphConfig` via the minimal JSON reader, inject node params into `NodeDef.options` from CLI/defaults (reusing `src/framework/runner/config_options.{h,cc}` `SetNodeOption`/`GetNodeOption*`), run `PipelineRunner` (frame pacing 30fps), exit 0 on success; add target to `src/examples/BUILD.bazel` with runfiles for the config + asset (depends on T005, T013–T019)
- [x] T021 [US1] End-to-end test `src/tests/dashcam_record_test.cc`: drive `PipelineRunner` with a short frame count (e.g. 60 frames) via a test config, assert output MP4 exists with `ftyp`/`moov`/`mdat` boxes, expected frame count/duration, timestamp pixels change across frames, and overwrite of an existing file succeeds; register in `src/tests/BUILD.bazel` (depends on T020)
- [x] T022 [US1] Verify existing dual-cam reference `src/examples/configs/recorder.json` (converted to graph_runtime schema) still passes template validation (7 nodes / 6 streams after removing the SignalSource node); 001 `hello_graph.cc` (media_record skeleton + placeholder nodes) unchanged

**Checkpoint**: At this point, User Story 1 is fully functional and independently testable (MVP)

---

## Phase 4: User Story 3 - 录制失败可定位 (Priority: P3)

**Goal**: Every failure (missing/unsupported input image, unwritable output, encode failure) yields a locatable error naming the node/path, exits non-zero, and leaves no partial artifact (FR-008/009).

**Independent Test**: Construct failure scenarios (input path missing, output dir unwritable, forced encode error) and assert each prints a specific error (containing the path/node) and exits non-zero without leaving `out/dashcam.mp4` (or a `.tmp` leftover).

### Implementation for User Story 3

- [x] T023 [US3] Harden the entry-point error contract in `src/examples/dashcam_record.cc`: map node failures (non-OK `absl::Status`) to exit code 1 with node-name + reason in stderr, config/argument errors to exit 2; success → 0
- [x] T024 [P] [US3] Add input-missing / unsupported-format error path in `src/nodes/stream_input/stream_input_node.cc`: on decode failure return non-OK status including the image path (FR-008)
- [x] T025 [P] [US3] Add output-unwritable detection + temp-file cleanup in `src/nodes/muxer_sink/muxer_sink_node.cc`: open failure or muxer `Finish` failure → report path, delete `out/.dashcam.mp4.tmp`, never leave a partial `out/dashcam.mp4` (FR-009)
- [x] T026 [P] [US3] Add encode-failure propagation in `src/nodes/video_encoder/video_encoder_node.cc`: `CodecFactory::CreateVideo`/`Init`/`Encode`/`Flush` failures → named-node error mapping `kEncodeFailed` / `kPlatformUnsupported` / `kUnsupportedFormat`
- [x] T027 [US3] Failure-path tests in `src/tests/dashcam_record_test.cc`: (a) missing input image → non-zero exit, stderr contains path, no output file; (b) unwritable output dir → non-zero exit, stderr contains path, no temp file left; (c) encode failure → non-zero exit, no partial `out/dashcam.mp4` (depends on T023–T026)

**Checkpoint**: At this point, User Stories 1 and 3 both work; all SC-005 failure scenarios pass

---

## Phase 5: Polish & Cross-Cutting Concerns

**Purpose**: Improvements that affect multiple user stories

- [x] T028 [P] Extend `mk/verify.mk` (and Makefile wiring) with a recording-artifact check step: build `//...`, run `bazel test //src/tests:all`, run `dashcam_record` and assert `out/dashcam.mp4` exists and is non-empty (SC-004)
- [x] T029 [P] Update `media_record/doc/architecture/pipelines.md`: recorder topology reflects the real 6-node graph_runtime implementation, muxer writes via codec `Muxer` + `FileByteSink`, layout/OSD via native_ui flex layout + software draw, execution via `src/framework/runner/` sync driver over `GraphConfig`
- [x] T030 Run `quickstart.md` validation end-to-end from `media_record/media_record/`: `bazel build //...`, `bazel test //src/tests:all`, `make verify`; confirm default run exits 0 and `out/dashcam.mp4` is playable

---

## Dependencies & Execution Order

### Phase Dependencies

- **Setup (Phase 1)**: No dependencies - can start immediately
- **Foundational (Phase 2)**: Depends on Setup completion - BLOCKS all user stories (T004 is the cross-repo prerequisite for T019)
- **User Stories (Phase 3+)**: All depend on Foundational phase completion
  - User Story 1 (P1) is the MVP; User Story 3 (P3) depends on US1 (needs the nodes to exercise failure paths)
  - User Story 2 (可配置录制参数) is **Priority: Deferred** per spec — NOT in scope this feature (explicitly excluded by Clarifications 2026-08-18)
- **Polish (Final Phase)**: Depends on US1 + US3 being complete

### User Story Dependencies

- **User Story 1 (P1)**: Can start after Foundational (Phase 2) - no dependencies on other stories
- **User Story 3 (P3)**: Depends on User Story 1 completion (nodes + entry must exist to test failure paths)
- **User Story 2 (P2, deferred)**: Not in this feature's scope

### Within Each User Story

- Tests MUST be written and FAIL before implementation
- Helpers (RGBA→I420, bitmap font) before their consuming nodes
- Source nodes before layout/overlay before encoder/recorder/muxer
- Node implementations before entry wiring before end-to-end test

### Parallel Opportunities

- Setup: T002, T003 run in parallel after T001
- Foundational: T004 (cross-repo) and T005 (runner, depends only on graph_runtime public umbrella) in parallel
- User Story 1: tests T008–T010 in parallel; helpers T011–T012 in parallel; independent nodes T013, T015, T018 in parallel; then T016 (needs T012), T017 (needs T011), T019 (needs T004); then T020, T021, T022 sequential
- User Story 3: T024, T025, T026 in parallel after T023's contract is fixed; T027 after them

---

## Parallel Example: User Story 1

```bash
# Launch all US1 tests together (write-first, expect FAIL):
Task: "T008 Unit test software RGBA->I420 in src/tests/rgba_i420_test.cc"
Task: "T009 Unit test bitmap-font timestamp renderer in src/tests/bitmap_font_test.cc"
Task: "T010 Unit tests for StreamInputNode in src/tests/source_nodes_test.cc"

# Launch all independent source nodes together:
Task: "T013 Implement StreamInputNode (graph::runtime::Node) in src/nodes/stream_input/stream_input_node.{h,cc}"
Task: "T015 Implement MultiViewLayoutNode (graph::runtime::Node) in src/nodes/multi_view_layout/multi_view_layout_node.{h,cc}"
Task: "T018 Implement RecorderNode (graph::runtime::Node) in src/nodes/recorder/recorder_node.{h,cc}"
```

---

## Implementation Strategy

### MVP First (User Story 1 Only)

1. Complete Phase 1: Setup (T001–T003)
2. Complete Phase 2: Foundational (T004–T005) — **CRITICAL**: T004 unblocks MuxerSinkNode
3. Complete Phase 3: User Story 1 (T008–T022)
4. **STOP and VALIDATE**: `bazel run //src/examples:dashcam_record` + `out/dashcam.mp4` checks + `bazel test //src/tests:all`

### Incremental Delivery

1. Complete Setup + Foundational → runner (graph_runtime nodes/config) + video_codec umbrella export ready
2. Add User Story 1 → playable dashcam.mp4 with OSD timestamp (MVP) → validate independently
3. Add User Story 3 → all failure scenarios locatable + no partial artifacts → validate independently
4. Polish → `make verify` covers build + tests + artifact check

### Parallel Team Strategy

With multiple developers:

1. Team completes Setup + Foundational together (T004 cross-repo can be done by one dev in the video_codec repo)
2. Once Foundational is done:
   - Developer A: source + layout + overlay nodes (T013–T016)
   - Developer B: encoder + recorder + muxer nodes (T017–T019)
   - Developer C: entry wiring + tests (T020–T022, after A/B land)
3. US3 (T023–T027) can then proceed

---

## Notes

- [P] tasks = different files, no dependencies
- [Story] label maps task to specific user story for traceability
- Each user story is independently completable and testable
- Verify tests fail before implementing
- Commit after each task or logical group
- Stop at any checkpoint to validate story independently
- Avoid: vague tasks, same file conflicts, cross-story dependencies that break independence
- All `src/...` paths are relative to the Bazel workspace `media_record/media_record/`; the build/test/verify commands run there
- **架构红线**：不新增 `src/framework/transport/`、不恢复 media_record 自有配置（`framework/config`）；数据通路 = graph_runtime `Packet`/`GraphContext`，配置 = `GraphConfig`，执行 = `src/framework/runner/` 同步驱动器
