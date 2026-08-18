# Tasks: 工程架构设计及基础框架搭建

**Input**: Design documents from `/specs/001-project-architecture/`

**Prerequisites**: plan.md, spec.md, research.md, data-model.md, contracts/

**Tests**: 测试任务仅用于 US3 的验证交付物（deps_smoke_test / pipeline_config_test），非 TDD 强制。

**Organization**: Tasks are grouped by user story to enable independent implementation and testing of each story.

## Format: `[ID] [P?] [Story] Description`

- **[P]**: Can run in parallel (different files, no dependencies)
- **[Story]**: Which user story this task belongs to (e.g., US1, US2, US3)
- Include exact file paths in descriptions

## Path Conventions

- 仓库根：`media_record/`（git repo root，含 `specs/`、`AGENTS.md`）
- 源码根（Bazel workspace）：`media_record/media_record/`，任务路径中的 `workspace/` 表示该目录（例如 `workspace/WORKSPACE` = `media_record/media_record/WORKSPACE`）
- 三个依赖仓库消费 target：`@graph_runtime//src/framework/public:runtime`、`@native_ui//:native_ui`、`@video_codec//src/framework/public:video_codec`

---

## Phase 1: Setup (Shared Infrastructure)

**Purpose**: Bazel workspace 初始化（local_repository 依赖、平台配置、验证入口骨架）

- [X] T001 创建源码根目录并初始化 `workspace/WORKSPACE`：`workspace(name = "media_record")` + `load("//:media_record_deps.bzl", "media_record_setup")` + `media_record_setup()`
- [X] T002 创建 `workspace/.bazelversion`，内容 `6.5.0`
- [X] T003 创建 `workspace/.bazelrc`：`--cxxopt=-std=c++17`、`--host_cxxopt=-std=c++17`、`--features=visibility=hidden`、`--enable_platform_specific_config`、平台别名（macos_arm64/linux_x86_64/android_arm64）、默认 `build --platforms=//platforms:macos_arm64_platform`、`try-import %workspace%/.user.bazelrc`
- [X] T004 [P] 创建 `workspace/media_record_deps.bzl`：定义 `media_record_deps()`（三个 `local_repository`：graph_runtime → `codes/graph_runtime/graph_runtime`、native_ui → `codes/native_ui/native_ui`、video_codec → `codes/video_codec/codec`）与 `media_record_setup()`（调用三库的 `*_setup()`），路径做成可覆盖常量（对齐 contracts/dependency-contract.md D-1~D-5）
- [X] T005 [P] 创建 `workspace/platforms/platforms.bzl` + `workspace/platforms/BUILD`：`config_setting_and_platform(name, constraint_values)`（config_setting `name` + platform `name_platform`）+ `media_record_select(select_map)` 宏；定义 macos_arm64 / macos_x86_64 / linux_x86_64 / linux_aarch64 / android_arm64 五个平台
- [X] T006 [P] 创建 `workspace/Makefile` + `workspace/mk/verify.mk` + `workspace/scripts/verify/`：`make verify` 骨架（占位，后续挂 bazel build/test/example）

---

## Phase 2: Foundational (Blocking Prerequisites)

**Purpose**: 公共 API（umbrella + export 宏 + Node 注册骨架）—— US2/US3 的公共基础

**⚠️ CRITICAL**: US2/US3 依赖此阶段的公共 target 存在

- [X] T007 创建 `workspace/src/framework/public/include/media_record/media_record_export.h`：定义 `MEDIA_RECORD_API` 宏（Windows dllexport/dllimport + GCC visibility 分支，对齐 contracts/public-api.md）
- [X] T008 创建 `workspace/src/framework/public/include/media_record/node.h`：声明 `Node` 基类（Open/Process/Close 生命周期）与 `REGISTER_NODE(name, Type)` 注册宏骨架（对齐 graph_runtime `NodeFactoryRegistry` 语义）
- [X] T009 创建 `workspace/src/framework/public/include/media_record/media_record.h`：umbrella header，仅包含 `media_record_export.h` + `node.h`
- [X] T010 创建 `workspace/src/framework/public/BUILD`：`cc_library(name = "media_record", hdrs=glob(["include/media_record/*.h"]), strip_include_prefix="include", alwayslink=1, visibility=["//visibility:public"])` + `cc_binary(name = "media_record_shared", linkshared=True, deps=[":media_record"])`（骨架自身不链接三库，跨库链接由 US3 测试承担）
- [X] T011 创建 `workspace/src/framework/node/BUILD.bazel`：节点注册基础实现占位（空 cc_library，供后续节点骨架引用）
- [X] T012 创建 `workspace/BUILD.bazel`：root alias `//:media_record → //src/framework/public:media_record`

**Checkpoint**: `bazel build //:media_record` 可编译通过（US1 的公共库产物就绪）

---

## Phase 3: User Story 1 - 可构建的工程骨架 (Priority: P1) 🎯 MVP

**Goal**: 干净检出上一键构建成功；三库本地解析、离线可用；公共库 target 可消费

**Independent Test**: 在干净检出执行 `bazel build //...` 全部成功；断网（依赖已缓存）后仍成功；`bazel build //:media_record` 产出可消费公共库

- [ ] T013 [US1] 执行首次全量构建 `bazel build //...` 并修复依赖解析 / 平台 / 头文件可见性问题，直到零失败（对齐 research.md §2 构建约定）
- [ ] T014 [US1] 验证离线构建：在依赖已入 bazel repository cache 后，模拟离线执行 `bazel build //...`，确认三个仓库全部走本地解析、无网络拉取（对齐 spec SC-002）
- [ ] T015 [US1] 验证公共库消费：在 `workspace/examples/` 放一个最小 include `media_record/media_record.h` 的编译目标，确认 `bazel build //examples:...` 无缺失符号（对齐 spec SC-003）

**Checkpoint**: US1 验收通过——可构建、可离线、公共库可消费

---

## Phase 4: User Story 2 - 公共 API 与基础框架模块 (Priority: P1)

**Goal**: `src/nodes/` 全量节点骨架 + 工程结构设计文档

**Independent Test**: `src/nodes/` 下 10 个节点目录全部可编译；目录结构与 bootstrap/plan 一致（spec SC-006/SC-007）

- [X] T016 [P] [US2] 创建节点骨架目录与占位文件：`workspace/src/nodes/stream_input/`（BUILD.bazel 空 cc_library + `stream_input_node.h` 头部占位）
- [X] T017 [P] [US2] 创建 `workspace/src/nodes/signal_source/` 骨架（BUILD.bazel + 头部占位）
- [X] T018 [P] [US2] 创建 `workspace/src/nodes/multi_view_layout/` 骨架（BUILD.bazel + 头部占位）
- [X] T019 [P] [US2] 创建 `workspace/src/nodes/ui_overlay/` 骨架（BUILD.bazel + 头部占位）
- [X] T020 [P] [US2] 创建 `workspace/src/nodes/video_encoder/` 与 `workspace/src/nodes/audio_encoder/` 骨架（各 BUILD.bazel + 头部占位）
- [X] T021 [P] [US2] 创建 `workspace/src/nodes/recorder/`、`workspace/src/nodes/muxer_sink/`、`workspace/src/nodes/stream_sink/`、`workspace/src/nodes/preview/` 骨架（各 BUILD.bazel + 头部占位）
- [X] T022 [US2] 编写工程结构设计文档 `workspace/doc/architecture/engineering-structure.md`：模块划分（public/node/nodes/examples/tests）、目录约定、命名规范、依赖规则（对齐 research.md §6 与 bootstrap）
- [X] T023 [US2] 编写架构总览 `workspace/doc/architecture/README.md`：模块依赖图 + 指向 engineering-structure.md / pipelines.md
- [X] T024 [US2] 全量构建 `bazel build //...`，确认 10 个节点骨架目录全部编译通过（spec SC-007）

**Checkpoint**: 公共 API + 全量节点骨架就绪，US2 验收通过

---

## Phase 5: User Story 3 - 冒烟验证与可运行示例 (Priority: P2)

**Goal**: 三类 pipeline JSON 模板 + 可运行示例 + pipeline 配置校验 + `make verify` 闭环

**Independent Test**: `hello_graph` 加载 recorder.json 正常退出（退出码 0）；stream/preview 模板配置校验通过；`make verify` 编译+测试+示例全绿（spec SC-008）

### Tests for User Story 3（验证交付物，非 TDD 强制）

- [X] T025 [P] [US3] 创建 `workspace/src/tests/pipeline_config_test.cc` + `workspace/src/tests/BUILD.bazel`：加载三个 JSON 模板，校验解析成功、节点 type 已注册、stream source/dest 引用有效（对齐 contracts/pipeline-contract.md P-1~P-4）
- [X] T026 [P] [US3] 创建 `workspace/src/tests/deps_smoke_test.cc`：同时 `#include` 三个库 umbrella（`graph_runtime/graph_runtime.h`、`native_ui/core.h`、`video_codec/video_codec.h`）并最小链接验证（对齐 research.md §4）

### Implementation for User Story 3

- [X] T027 [P] [US3] 创建记录仪 pipeline 模板 `workspace/src/examples/configs/recorder.json`：多路输入→布局→OSD→编码→录像的节点连线（graph_runtime 原生 schema，`type` + `streams[]`，对齐 research.md §6）
- [X] T028 [P] [US3] 创建推流 pipeline 模板 `workspace/src/examples/configs/stream.json`：编码→推流节点连线
- [X] T029 [P] [US3] 创建预览 pipeline 模板 `workspace/src/examples/configs/preview.json`：复合画面→屏幕节点连线
- [X] T030 [US3] 创建 `workspace/src/examples/hello_graph.cc` + `workspace/src/examples/BUILD.bazel`：加载命令行指定的 pipeline JSON → 构建并执行节点图 → 正常退出；未注册节点 type 时给出含节点名的可读错误（spec FR-009）
- [X] T031 [US3] 编写 pipeline 设计文档 `workspace/doc/architecture/pipelines.md`：三类 pipeline 的节点连线、数据流、旁路事件、统一组合规则（对齐 contracts/pipeline-contract.md）
- [X] T032 [US3] 完成 `make verify` 接线：`workspace/mk/verify.mk` 聚合 `bazel build //...` + `bazel test //src/tests:all` + 运行 hello_graph（recorder.json），输出汇总（对齐 research.md §4）

**Checkpoint**: 示例 + 模板 + 测试 + make verify 全绿，US3 验收通过

---

## Phase N: Polish & Cross-Cutting Concerns

**Purpose**: 收尾与可维护性

- [ ] T033 [P] 创建 `workspace/.gitignore`：排除 `bazel-*`、`.user.bazelrc`、`out/`、构建产物
- [ ] T034 [P] 按 `specs/001-project-architecture/quickstart.md` 逐条验证所有命令（构建 / 运行 / 测试 / make verify），修正文档与实际不符处
- [ ] T035 更新 `media_record/CHANGELOG.md`：记录本期架构与骨架交付
- [ ] T036 最终核对：`specs/001-project-architecture/` 下 spec/plan/research/data-model/contracts/quickstart 与实现一致，AGENTS.md 指引准确

---

## Dependencies & Execution Order

### Phase Dependencies

- **Setup (Phase 1)**: No dependencies - can start immediately
- **Foundational (Phase 2)**: Depends on Setup (WORKSPACE/deps.bzl) - BLOCKS US1/US2/US3 的构建验证
- **User Stories (Phase 3+)**: All depend on Foundational
  - US1（Phase 3）：验证构建闭环，在 Setup+Foundational 之后即可独立完成
  - US2（Phase 4）：依赖 Foundational 的公共 target；与 US1 无相互依赖
  - US3（Phase 5）：依赖 Foundational（node.h / 公共库）与 US2 的节点骨架目录（模板引用节点 type）；模板与示例文件相互独立
- **Polish (Final Phase)**: Depends on all desired user stories complete

### User Story Dependencies

- **User Story 1 (P1)**: 可构建骨架（本地解析 / 离线 / 公共库可消费）——最早验证闭环
- **User Story 2 (P1)**: 公共 API + 全量节点骨架——不依赖 US1 验证结果，可与 US1 并行（T013-T015 与 T016-T024 不同文件）
- **User Story 3 (P2)**: 示例/模板/校验——依赖 US2 的节点目录（模板 type 命名一致性），但 T025/T026 测试可与 US2 并行

### Within Each User Story

- 骨架/模板/文档之间文件独立，按 [P] 标记并行
- 示例（T030）依赖三个模板（T027-T029）完成
- 测试（T025-T026）与模板（T027-T029）可并行，均不依赖示例实现

### Parallel Opportunities

- Phase 1: T004/T005/T006 可并行
- Phase 2: T007-T011 逻辑顺序依赖（export→node→umbrella→BUILD），T012 依赖 T010
- Phase 3: 全部 US1 任务顺序（构建→离线→消费验证）
- Phase 4: T016-T021 十个节点骨架目录全部 [P] 并行；T022/T023 文档与骨架并行
- Phase 5: T025/T026 测试与 T027-T029 模板全部 [P] 并行；T030 依赖模板；T031/T032 独立

---

## Parallel Example: User Story 2

```bash
# 并行创建 10 个节点骨架目录（各为独立目录/文件）：
Task: "创建 workspace/src/nodes/stream_input/ 骨架"
Task: "创建 workspace/src/nodes/signal_source/ 骨架"
...
Task: "创建 workspace/src/nodes/preview/ 骨架"

# 并行编写工程结构文档：
Task: "编写 workspace/doc/architecture/engineering-structure.md"
Task: "编写 workspace/doc/architecture/README.md"
```

## Parallel Example: User Story 3

```bash
# 并行创建三类 pipeline 模板 + 两个测试：
Task: "创建 workspace/src/examples/configs/recorder.json"
Task: "创建 workspace/src/examples/configs/stream.json"
Task: "创建 workspace/src/examples/configs/preview.json"
Task: "创建 workspace/src/tests/pipeline_config_test.cc"
Task: "创建 workspace/src/tests/deps_smoke_test.cc"

# 模板完成后创建示例：
Task: "创建 workspace/src/examples/hello_graph.cc"
```

---

## Implementation Strategy

### MVP First (User Story 1 Only)

1. 完成 Phase 1: Setup（WORKSPACE / deps.bzl / platforms / .bazelrc）
2. 完成 Phase 2: Foundational（公共 API + 节点注册骨架）→ `bazel build //:media_record` 通过
3. 完成 Phase 3: User Story 1（全量构建 / 离线 / 消费验证）
4. **STOP and VALIDATE**: US1 独立可验收——可构建、离线可用、公共库可消费
5. 若 US2/US3 暂缓，MVP 已成立（工程可编译、依赖打通）

### Incremental Delivery

1. Setup + Foundational → 工作区可编译
2. US1 → 构建闭环（MVP）→ 验证
3. US2 → 全量节点骨架 + 工程结构文档 → 验证
4. US3 → 模板 + 示例 + 测试 + make verify → 验证
5. 每阶段增量交付，不破坏前一阶段

### Parallel Team Strategy

1. 团队 A/B 先共同完成 Setup + Foundational
2. 之后：A 做 US1（构建验证），B 做 US2（节点骨架 + 文档），可并行
3. US2 完成后 C 做 US3（模板/示例/测试）
4. 各 story 独立验收后再合并集成

---

## Notes

- [P] tasks = different files, no dependencies
- [Story] label maps task to specific user story for traceability
- 每个用户故事独立可完成、可测试
- 提交粒度：每个 task 或逻辑组提交一次，遵循 Conventional Commits（见 project_bootstrap.md）
- 所有 BUILD `deps` 使用 `@media_record//` 前缀；仅依赖三库公共 umbrella target
- 平台相关能力在 host 上用 stub 占位（spec FR-005）
