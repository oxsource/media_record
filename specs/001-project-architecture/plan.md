# Implementation Plan: 工程架构设计及基础框架搭建

**Branch**: `001-project-architecture` | **Date**: 2026-08-17 | **Spec**: [spec.md](spec.md)

**Input**: Feature specification from `/specs/001-project-architecture/spec.md`

**Note**: This template is filled in by the `/speckit.plan` command. See `.specify/templates/plan-template.md` for the execution workflow.

## Summary

搭建 media_record 工程基础骨架：以 Bazel 6.5 workspace 为主，通过 `local_repository` 在本地引用 graph_runtime / native_ui / video_codec 三个仓库的**内层 workspace 目录**；提供公共 umbrella 库目标（`//src/framework/public:media_record`）、节点注册骨架、**`src/nodes/` 全量节点骨架目录（10 个节点，可编译占位）**、平台选择机制、**三类 pipeline 预置 JSON 模板（记录仪 / 推流 / 预览）**与可运行示例、pipeline 配置校验测试，以及 `make verify` 一键验证入口。**架构文档**（工程结构设计 + pipeline 设计）落在 `media_record/doc/architecture/`，模块与目录结构遵循 `media_record/doc/project_bootstrap.md` 约定。

## Technical Context

**Language/Version**: C++17（Google C++ Style，与三个依赖库一致）

**Primary Dependencies**:
- `@graph_runtime//src/framework/public:runtime`（umbrella：`graph_runtime/graph_runtime.h`，workspace 位于 `graph_runtime/graph_runtime/`）
- `@native_ui//:native_ui`（root alias → `//src/framework/public:native_ui`，umbrella：`native_ui/*.h`，workspace 位于 `native_ui/native_ui/`）
- `@video_codec//src/framework/public:video_codec`（umbrella：`video_codec/video_codec.h`，workspace 位于 `video_codec/codec/`）

**Storage**: N/A（无持久化存储；录像存储能力属后续 feature）

**Testing**: Bazel `cc_test`（googletest，经三个仓库的 setup 引入）+ `make verify` 一键验证

**Target Platform**: macOS（默认开发宿主）、Linux x86_64、Android arm64（交叉编译）；平台选择用 `select()`

**Project Type**: library（跨平台摄像头记录仪框架组合层，Bazel workspace）

**Performance Goals**: 基础骨架无实时性要求；示例图能完整构建并运行（退出码 0）

**Constraints**: 三个仓库以本地路径解析、不依赖网络拉取；C++17；公共 API 仅经 umbrella header；host 构建对 Android-only 目标提供 stub

**Scale/Scope**: 单 Bazel workspace（`media_record/media_record/`）；含公共库、节点骨架、示例、测试、mk 验证、架构文档

## Constitution Check

*GATE: Must pass before Phase 0 research. Re-check after Phase 1 design.*

- **Gate 1 — Constitution 状态**: `.specify/memory/constitution.md` 仍为未填写的模板占位（无已批准原则）。**结论**：无强制 gate 可执行；本次以 spec 的 Success Criteria 为验收依据，并在 `AGENTS.md` 中登记指向。
- **Gate 2 — 可测试性**: 每项功能需求均可通过"从干净检出构建 / 运行示例 / make verify"验证。**PASS**。
- **Gate 3 — 复杂度最小化**: 不新增第三个仓库；不实现业务节点（仅可编译占位）；全量节点骨架 + 三类 pipeline 设计文档是为后续功能提供明确落点、属规划产物而非过度设计。**PASS**。

Phase 1 设计后再评估：结构仍为单 workspace + 基础模块，无 gate 违规。

## Project Structure

### Documentation (this feature)

```text
specs/001-project-architecture/
├── plan.md              # This file (/speckit.plan command output)
├── research.md          # Phase 0 output (/speckit.plan command)
├── data-model.md        # Phase 1 output (/speckit.plan command)
├── quickstart.md        # Phase 1 output (/speckit.plan command)
├── contracts/           # Phase 1 output (/speckit.plan command)
│   ├── public-api.md
│   ├── dependency-contract.md
│   └── pipeline-contract.md
└── tasks.md             # Phase 2 output (/speckit.tasks command - NOT created by /speckit.plan)
```

### Source Code (repository root)

```text
media_record/                         (repo root)
├── AGENTS.md
├── CHANGELOG.md
├── specs/
└── media_record/                     (源码根，workspace(name = "media_record"))
    ├── WORKSPACE
    ├── BUILD.bazel                   (root alias: //:media_record → //src/framework/public:media_record)
    ├── .bazelversion                 (6.5.0)
    ├── .bazelrc
    ├── media_record_deps.bzl         (local_repository + 三个仓库 setup 调用)
    ├── platforms/
    │   ├── BUILD
    │   └── platforms.bzl             (config_setting_and_platform + media_record_select)
    ├── src/
    │   ├── framework/
    │   │   ├── public/
    │   │   │   ├── BUILD             (name = "media_record" 汇总 target + "media_record_shared")
    │   │   │   └── include/media_record/
    │   │   │       ├── media_record.h         (umbrella header)
    │   │   │       ├── media_record_export.h  (MEDIA_RECORD_API 宏)
    │   │   │       └── node.h                 (Node 基类 + 注册机制骨架)
    │   │   └── node/
    │   │       └── BUILD.bazel        (节点注册基础实现占位)
    │   ├── nodes/                    (全量节点骨架，各目录：空 BUILD + 头部占位)
    │   │   ├── stream_input/
    │   │   ├── signal_source/
    │   │   ├── multi_view_layout/
    │   │   ├── ui_overlay/
    │   │   ├── video_encoder/
    │   │   ├── audio_encoder/
    │   │   ├── recorder/
    │   │   ├── muxer_sink/
    │   │   ├── stream_sink/
    │   │   └── preview/
    │   ├── examples/
    │   │   ├── BUILD.bazel
    │   │   ├── hello_graph.cc         (最小配置驱动示例，加载预置 pipeline 模板)
    │   │   └── configs/
    │   │       ├── recorder.json      (记录仪 pipeline 模板)
    │   │       ├── stream.json        (推流 pipeline 模板)
    │   │       └── preview.json       (预览 pipeline 模板)
    │   └── tests/
    │       ├── BUILD.bazel
    │       ├── deps_smoke_test.cc     (链接三个依赖库的冒烟测试)
    │       └── pipeline_config_test.cc (pipeline 模板配置校验)
    ├── mk/                           (AOSP 风格 make 模块)
    │   └── verify.mk
    ├── Makefile
    ├── scripts/
    │   └── verify/                   (验证脚本)
    └── doc/
        ├── project_bootstrap.md
        ├── architecture/
        │   ├── README.md             (架构总览 + 模块依赖图)
        │   ├── engineering-structure.md (工程结构设计：模块划分 / 目录约定 / 命名)
        │   └── pipelines.md          (pipeline 设计：记录仪 / 推流 / 预览 + 统一组合规则)
        └── api/                      (公共接口契约)
```

**Structure Decision**: 采用"单 workspace + 源码根内层目录"结构，与三个依赖仓库（graph_runtime / native_ui / video_codec）的布局对齐：源码根 `media_record/media_record/`、`doc/` 位于其下、`specs/` 位于仓库根。公共 target 采用 `src/framework/public` 汇总模式（对齐 graph_runtime 的 `src/framework/public` 与 native_ui / video_codec 的 `src/framework/public` 命名），并通过根 `BUILD.bazel` 提供 `//:media_record` 别名。`src/nodes/` 预置全部 10 个规划节点的骨架目录（空 BUILD + 头部占位），示例与测试覆盖三类 pipeline 模板。

## Complexity Tracking

> **Fill ONLY if Constitution Check has violations that must be justified**

无 gate 违规，此表留空。
