# Implementation Plan: 模拟行车记录仪录制（Dashcam Simulated Recording）

**Branch**: `002-dashcam-sim-recording` | **Date**: 2026-08-18 | **Spec**: [spec.md](spec.md)

**Input**: Feature specification from `/specs/002-dashcam-sim-recording/spec.md`

**Note**: This template is filled in by the `/speckit.plan` command. See `.specify/templates/plan-template.md` for the execution workflow.

## Summary

以一张内置默认图片模拟摄像头输入，经「输入 → 布局 → OSD 叠加（真实时钟时间戳）→ H.264 编码 → 录制会话 → MP4 封装」的端到端录制闭环，输出可播放视频文件；默认录制 10 秒（30fps，共 300 帧）后自动结束。本期将 recorder.json 引用的 **7 类节点**（StreamInput / SignalSource / MultiViewLayout / UiOverlay / VideoEncoder / Recorder / MuxerSink）实现为**可运行的真实实现**（替换 001 的占位骨架），其余节点（音频编码 / 推流 / 预览）保持骨架。

**执行模型 = graph_runtime 节点的同步 frame loop（对齐其 `src/examples/string_pipeline.cc` 模式）**：7 类节点实现为 `graph::runtime::Node` 子类，经 `GRAPH_RUNTIME_REGISTER_NODE` 注册；图拓扑由 **graph_runtime 自己的 `GraphConfig`（JSON schema，`"port:stream"` 命名）** 描述，media_record **不维护任何自有配置 schema**（删除 001 的 `src/framework/config` PipelineConfig，节点参数由入口程序化注入 `NodeDef.options`）；帧传输统一 **graph_runtime 的 `Packet` / `Timestamp` / `GraphContext`** 类型（不再有 media_record 自有 Packet/StreamBuffer/StreamNode/transport 层）。media_record 仅新增一个薄驱动器 `src/framework/runner/`：在调用线程上按拓扑序构造 `GraphContext` 驱动各节点 `Open/Process/Close`，并按 stream 名在节点间搬运 `Packet`（与 string_pipeline.cc 的手工搬运同构，改为配置驱动）。

**关键约束**：graph_runtime 的 `GraphRuntime` 执行类**不连接节点内部流**（`OutputStreamManager::AddMirror` 从未被调用；其公共面示例均为单节点图 + 外部注入），因此本图由 media_record 的同步驱动器执行，不依赖 `GraphRuntime::Schedule/Start`。编码复用 video_codec 公共面 `VideoEncoder`（H.264，FFmpeg backend），MP4 封装复用 video_codec 公共面 `Muxer`（经 `ByteSink`/`FileByteSink` 落盘）；画面组合使用 native_ui flex 布局（`Container` + `ExternalImage` + `Text`）确定结构与位置，最终图像与时间戳文字由 media_record 软件绘制进自有 RGBA 帧（host Surface 无像素回读）。media_record 自身不再引入 skia / ffmpeg / libyuv，RGBA→I420 以内置软件转换实现。默认输出 `out/dashcam.mp4`，覆盖旧文件并提示；失败给出可定位错误且不残留残缺产物。

## Technical Context

**Language/Version**: C++17（Google C++ Style，与三个依赖库及 001 骨架一致）

**Primary Dependencies**:
- `@graph_runtime//src/framework/public:runtime` — **完整运行时公共面**（对齐 graph_runtime 自身 `src/examples/*` 的单 target 依赖方式）：`Node` / `NodeRegistry`（`GRAPH_RUNTIME_REGISTER_NODE`）/ `GraphContext` / `Packet` / `Timestamp` / `GraphConfig` / `ConfigValidator`。内部头（`src/framework/node/node.h`、`graph_context.h`、`src/framework/stream/packet.h`、`src/framework/config/graph_config.h` 等）经公共 target 的传递依赖提供 include 路径（无 layering_check，与 graph_runtime 自身 examples 编译方式一致）
- `@video_codec//src/framework/public:video_codec` — `VideoEncoder`（H.264）/ `Muxer`（MP4）/ `ByteSink` / `FileByteSink` / `VideoFrame` / `VideoPacket` / `CodecFactory`（FFmpeg backend，公共 umbrella；本期前置：在 video_codec 公共面新增导出 io 模块，见 Project Structure 与 dependency-contract）
- `@native_ui//:native_ui` — `Image::FromFile` / `Image::CopyPixels`（默认图片解码为 RGBA）+ `Container` / `ExternalImage` / `Text`（flex 布局组合画面结构与位置）
- media_record 自有：`src/framework/runner/`（同步 frame-loop 驱动器，构建于 graph_runtime 类型之上）+ 001 骨架（`//src/framework/public:media_record` 的 `Node`/`NodeRegistry`、`hello_graph` 示例，保持不变）
- **无直接 vendored 依赖**：media_record pipeline 不直接链接 `@ffmpeg` / `@libyuv`；`third_party/` 下的 ffmpeg / libyuv BUILD 包装仅用于满足依赖库 `*_setup()` 的 http_archive label 解析，非本期消费目标

**Storage**: 文件系统（输出 `out/dashcam.mp4`；覆盖旧文件并提示）。无持久化数据库。

**Testing**: Bazel `cc_test`（googletest）：节点级单测 + 端到端录制测试（短帧数驱动，断言 MP4 ftyp/moov/mdat、帧数、时间戳逐帧变化）+ `make verify` 一键验证（编译 + 测试 + 产物检查）。

**Target Platform**: macOS（默认开发宿主）、Linux x86_64、Android arm64（交叉编译兼容；host 上 Android-only 能力 stub）。

**Project Type**: library（媒体记录框架组合层）+ CLI/示例录制入口（batch 录制）。

**Performance Goals**: 30fps 实时帧生成 + H.264 编码吞吐满足实时；默认 10 秒录制在约 10 秒墙钟内完成（按帧率节流）。

**Constraints**:
- 单会话单分段；无缓存池 / 多分段切换 / 防抖（按澄清）。
- 默认值运行：10 秒 / 30fps / 分辨率跟随输入图片 / 时间戳默认格式与位置 / 默认输入图片 / 默认输出路径；可配置能力留待后续提案。
- 时间戳为**真实时钟**（显示录制时刻的真实日期时间），随录制逐帧自然递增。
- 失败（输入缺失 / 格式不支持 / 输出不可写 / 编码失败）→ 可定位错误 + 退出非零 + 不残留残缺文件。
- 仅消费三库的**公共 umbrella 头文件**（graph_runtime 经 `@graph_runtime//src/framework/public:runtime` 一个 target 获得节点/配置/类型全部能力，内部头经传递依赖可见，与 graph_runtime 自身 examples 一致）；video_codec 公共 umbrella 本期做**最小前置扩展**（导出 io 的 `ByteSink` / `FileByteSink`，跨仓协作），media_record 自身不直接引入 skia / ffmpeg / libyuv。
- **配置唯一性**：图拓扑只存于 graph_runtime `GraphConfig`（JSON 文件为 graph_runtime schema；media_record 的最小 JSON 读取器只负责产出该类型，不定义新 schema）；节点参数（image/output/fps/duration）由入口程序化设置 `NodeDef.options`（graph_runtime JsonParser 不解析 node options）。

**Scale/Scope**: 单 workspace（`media_record/media_record/`）；7 类节点真实实现（graph_runtime 节点）+ 同步驱动器 + 录制入口示例 + 测试 + `make verify` 扩展。

## Constitution Check

*GATE: Must pass before Phase 0 research. Re-check after Phase 1 design.*

- **Gate 1 — Constitution 状态**: `.specify/memory/constitution.md` 仍为未填写的模板占位（无已批准原则，与 001 相同）。**结论**：无强制 gate 可执行；本次以 spec 的 Success Criteria 为验收依据，并在 `AGENTS.md` 中登记指向 002 plan。
- **Gate 2 — 可测试性**: 每项功能需求均可通过「运行录制入口 / 检查输出产物 / 失败场景」验证（SC-001~SC-005）。**PASS**。
- **Gate 3 — 复杂度最小化**: 不新增第四仓库；不实现多路拼接业务逻辑（单路输入）；单会话单分段；不引入独立调度器（同步 frame loop）；不维护第二份配置（拓扑/校验语义全部复用 graph_runtime `GraphConfig`/`ConfigValidator`）。**PASS**。
- **Gate 4 — 公共 API 契约**: 仅消费三库公共 umbrella 头文件（graph_runtime 经 `:runtime` 单 target，与 graph_runtime 自身 examples 同构）；为复用 codec `Muxer`，本期对 video_codec 公共 umbrella 做**最小扩展**（导出 io 的 `ByteSink`/`FileByteSink`，作为前置任务，见 Project Structure），media_record 不触及三库内部接口。**PASS**。

Phase 1 设计后再评估：结构仍为单 workspace + 既有模块扩展，无 gate 违规。

## Project Structure

### Documentation (this feature)

```text
specs/002-dashcam-sim-recording/
├── plan.md              # This file (/speckit.plan command output)
├── research.md          # Phase 0 output (/speckit.plan command)
├── data-model.md        # Phase 1 output (/speckit.plan command)
├── quickstart.md        # Phase 1 output (/speckit.plan command)
├── contracts/           # Phase 1 output (/speckit.plan command)
│   ├── node-contract.md
│   ├── pipeline-contract.md
│   ├── public-api.md
│   └── dependency-contract.md
└── tasks.md             # Phase 2 output (/speckit.tasks command - NOT created by /speckit.plan)
```

### Source Code (repository root)

```text
media_record/                              (repo root)
├── AGENTS.md
├── CHANGELOG.md
├── specs/
└── media_record/                          (源码根，workspace(name = "media_record"))
    ├── src/
    │   ├── framework/
    │   │   ├── public/                    (001 骨架，保持不变：Node/NodeRegistry 骨架 + hello_graph 用)
    │   │   ├── node/                      (001 骨架，保持不变，空 BUILD)
    │   │   └── runner/                    (本期新增：同步 frame-loop 驱动器，构建于 graph_runtime 类型之上，见下)
    │   │       ├── BUILD.bazel
    │   │       └── pipeline_runner.{h,cc} (GraphConfig 驱动：拓扑排序 + GraphContext Open/Process/Close + Packet 按流搬运)
    │   ├── nodes/                         (7 类节点真实实现：graph::runtime::Node 子类，替换 001 占位)
    │   │   ├── stream_input/              (StreamInputNode → Packet<VideoFrame>，source 节点)
    │   │   ├── signal_source/             (SignalSourceNode → Packet<SignalEvent>，source 节点)
    │   │   ├── multi_view_layout/         (MultiViewLayoutNode：native_ui flex 布局确定画面结构与位置，软件渲染基帧)
    │   │   ├── ui_overlay/                (UiOverlayNode：flex 定位 + 软件位图字体绘制时间戳 OSD)
    │   │   ├── video_encoder/             (VideoEncoderNode：软件 RGBA→I420 + H.264 编码)
    │   │   ├── recorder/                  (RecorderNode：会话生命周期，单分段)
    │   │   ├── muxer_sink/                (MuxerSinkNode：codec Muxer + FileByteSink 写 MP4)
    │   │   ├── audio_encoder/             (骨架不变)
    │   │   ├── stream_sink/               (骨架不变)
    │   │   └── preview/                   (骨架不变)
    │   ├── examples/
    │   │   ├── assets/                    (本期新增：默认图片 dashcam_default.png)
    │   │   ├── configs/
    │   │   │   ├── recorder.json          (不变：双摄参考模板，改为 graph_runtime JSON schema 校验，8 nodes/7 streams 断言不变)
    │   │   │   ├── stream.json / preview.json  (不变)
    │   │   │   └── dashcam_record.json    (本期新增：单路可运行默认配置，graph_runtime JSON schema)
    │   │   ├── hello_graph.cc             (不变：001 骨架示例，仍用 media_record 占位节点跑 recorder.json)
    │   │   └── dashcam_record.cc          (本期新增：录制入口——解析 GraphConfig JSON + 程序化注入节点参数 + 同步驱动器)
    │   └── tests/
    │       ├── BUILD.bazel
    │       ├── pipeline_config_test.cc    (扩展：dashcam_record.json 以 graph_runtime GraphConfig 校验清单)
    │       └── dashcam_record_test.cc     (本期新增：端到端录制 + 产物断言)
    ├── third_party/                       (ffmpeg / libyuv / skia 等 BUILD 包装仅用于依赖库 *_setup() label 解析；本期 pipeline 不直接消费)
    ├── mk/verify.mk                       (扩展：录制产物检查步骤)
    ├── Makefile
    └── doc/architecture/                  (更新 pipelines.md 的记录仪运行拓扑)
```

**Structure Decision**: 在 001 既有骨架内做**增量实现**，但**不新增帧传输层**：数据通路直接复用 graph_runtime 的 `Node` / `GraphContext` / `Packet` / `Timestamp` 类型（消费其公共 umbrella `@graph_runtime//src/framework/public:runtime`，与 graph_runtime 自身 `src/examples/*` 的用法一致）；media_record 只新增 `src/framework/runner/` 一个薄驱动器，在调用线程上按图拓扑驱动各节点并搬运 Packet（对齐 `string_pipeline.cc` 的手工 GraphContext 驱动模式）。7 个节点目录由「空 BUILD + 占位头」升级为「真实 cc_library + graph::runtime::Node 实现」，命名与 target 命名沿用 001 约定；录制入口与默认配置放 `src/examples/`，端到端测试放 `src/tests/`。配置**只用 graph_runtime 的 `GraphConfig`**（JSON 文件为其 schema；media_record 的最小 JSON 读取器仅产出 `GraphConfig` 类型，不定义新 schema；节点参数由入口程序化设置 `NodeDef.options`）。公共 target（`//src/framework/public:media_record`）保持 header-only 不变（001 骨架），7 类节点不再经其注册——经 `GRAPH_RUNTIME_REGISTER_NODE` 注册到 graph_runtime 的 `NodeFactoryRegistry`，跨库链接由节点库 / 示例 / 测试承担（延续 001 决策）。MP4 封装复用 video_codec 公共面 `Muxer` + `FileByteSink`；为此需在 video_codec 公共 umbrella 增加 io 导出（`ByteSink` / `FileByteSink`，public BUILD deps + io 可见性 + dist 头文件拷贝）作为**前置跨仓任务**（见 `contracts/dependency-contract.md` D-1）。**命名避让**：media_record 不得在 `src/framework/stream/`、`src/framework/config/`（graph_runtime 同名目录）下放置文件，避免主 workspace include 遮蔽 graph_runtime 内部头（T004 deps_smoke_test 验证）。

## Complexity Tracking

> **Fill ONLY if Constitution Check has violations that must be justified**

| Violation | Why Needed | Simpler Alternative Rejected Because |
|-----------|------------|-------------------------------------|
| `src/framework/runner/` 同步驱动器（≈150 行，构建于 graph_runtime 类型之上） | graph_runtime 的 `GraphRuntime` 执行类不连接节点内部流（`AddMirror` 无调用点；其 examples 均为单节点图）；7 类节点需由配置驱动地串成链 | 直接用 `GraphRuntime::Schedule/Start`（内部流不连通，数据无法在节点间流动，不可行）；节点间直接传参耦合（破坏 config 驱动拓扑，弃）；跨仓扩展 graph_runtime 增加链式运行器（第二处跨仓改动，超出本期，记录为后续项） |
| UiOverlay 用软件位图字体绘制 OSD（位置由 native_ui flex 布局给出） | native_ui host 的 Surface 无公共像素回读（仅 Dump→PNG；`CreateFromBuffer` Android-only stub），无法经 Canvas 取回 RGBA | 每帧 Dump PNG 再解码（性能不可行）；扩展 native_ui 增加 host 像素回读（第二处跨仓改动，超出本期，记录为后续项） |
| video_codec 公共 umbrella 前置扩展（导出 `ByteSink`/`FileByteSink`） | codec `Muxer` 输出依赖 `io::ByteSink`，而 io 模块不在公共 umbrella 导出且可见性受限（`io/BUILD.bazel`） | media_record 直用 vendored FFmpeg 封装（违背「不再引入 ffmpeg」澄清，弃）；自研 MP4 muxer（重复实现，弃）；直接消费 video_codec io 内部 target（可见性受限且违背公共面契约，不可行） |
| MuxerSink 经 `FileByteSink` 临时文件 + 原子 rename 落盘 | 失败不残留残缺产物（FR-009）且输出可覆盖旧文件 | 直接写目标路径（失败残留半成品）；同步 rename 不可（跨平台保证，弃） |
