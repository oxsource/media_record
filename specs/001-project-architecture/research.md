# Research: 工程架构设计及基础框架搭建

**Branch**: `001-project-architecture` | **Date**: 2026-08-17 | **Spec**: [spec.md](spec.md)

## 1. 依赖仓库解析（local_repository）

### Decision

三个仓库以 `local_repository` 引用，且路径必须指向各仓库的**内层 WORKSPACE 目录**（不是仓库根）：

| 外部名 | WORKSPACE name | local_repository path | 公共消费 target |
|--------|----------------|------------------------|-----------------|
| `graph_runtime` | `graph_runtime` | `codes/graph_runtime/graph_runtime` | `@graph_runtime//src/framework/public:runtime` |
| `native_ui` | `native_ui` | `codes/native_ui/native_ui` | `@native_ui//:native_ui`（alias → `//src/framework/public:native_ui`） |
| `video_codec` | `video_codec` | `codes/video_codec/codec` | `@video_codec//src/framework/public:video_codec` |

### Rationale

- 实测三个仓库的 `WORKSPACE` 都位于内层目录（`graph_runtime/graph_runtime/`、`native_ui/native_ui/`、`video_codec/codec/`），`local_repository` 需要指向含 `WORKSPACE` 文件的目录，否则 bazel 报 "no WORKSPACE file found"。
- 公共 umbrella target 名称已逐一核实（见上文表格），可直接作为本工程 `deps` 使用。
- 依赖前缀统一为 `@media_record//`（对齐各库的 `@<repo>//` 前缀约定）。

### Alternatives considered

- `http_archive`：需要网络 + sha256 固定版本，与"离线 / 本地开发优先"冲突，且三个仓库尚未发布 tag。**拒绝**。
- `new_local_repository`：需要手工提供 BUILD 文件，会绕开各仓库自带 BUILD。**拒绝**。

### 注意：transitive 依赖仍需网络（首次）

`local_repository` 只解决"三个仓库本体本地解析"；各仓库 `*_setup()` 仍会拉取其自身 http_archive 依赖（graph_runtime → absl/googletest；native_ui → skia/yoga/googletest；video_codec → ffmpeg-6.1/googletest）。首次构建需要网络；后续依赖 `bazel` repository cache 或 video_codec 的 `--repository_cache` 复用。

**Action**：`media_record_deps.bzl` 中 local_repository 之后调用 `graph_runtime_setup()` / `native_ui_setup()` / `video_codec_setup()`，保证外部依赖完整。路径放入 `.user.bazelrc` 或 `media_record_deps.bzl` 的变量，允许按机器覆盖。

## 2. 构建约定对齐（从三个仓库提取）

### Decision

本工程构建约定统一对齐三个仓库的公共模式：

| 约定 | 值 |
|------|----|
| Bazel 版本 | 6.5.0（`.bazelversion`） |
| C++ 标准 | C++17（`.bazelrc`: `--cxxopt=-std=c++17` + `--host_cxxopt=-std=c++17`） |
| 符号可见性 | 默认 `--features=visibility=hidden`；公共导出用 `MEDIA_RECORD_API` 宏 + `-DMEDIA_RECORD_SHARED_LIBRARY` |
| 平台别名 | `.bazelrc` 定义 `--config=macos_arm64` / `linux_x86_64` / `android_arm64`，`--platforms=//platforms:...` |
| 默认开发平台 | `build --platforms=//platforms:macos_arm64_platform` |
| 本地覆盖 | `try-import %workspace%/.user.bazelrc`（git-ignored） |
| 平台宏 | `config_setting_and_platform`（config_setting `name` + platform `name_platform`）+ `media_record_select` |
| Android NDK | `--config=android_arm64` + `@androidndk` + `--incompatible_enable_cc_toolchain_resolution=true`（对齐 native_ui / video_codec） |

### Rationale

- `--features=visibility=hidden` + export 宏是三个仓库统一的共享库导出模式，保证公共符号干净。
- native_ui 的 platform 命名（`name_platform` 后缀）最直观，且与 video_codec 的 `darwin_arm64_platform` 风格一致，选择之。
- `try-import .user.bazelrc` 让本地依赖路径覆盖不进入版本库。

### Alternatives considered

- graph_runtime 的 platform 命名（`//platforms:macos_arm64`，config_setting 即平台、无 `_platform` 后缀）：与本工程默认平台引用方式存在歧义。**拒绝**，统一用 `_platform` 后缀。

## 3. 公共 API 骨架（最小可行）

### Decision

公共面按三个库的 umbrella 模式提供：

- `include/media_record/media_record_export.h`：`MEDIA_RECORD_API` 宏（Windows dllexport/dllimport + GCC visibility 分支，复制 graph_runtime / native_ui 模式）。
- `include/media_record/node.h`：`Node` 基类 + `REGISTER_NODE` 注册宏声明（骨架，仅声明接口与注册表占位，业务节点在后续 feature 实现）。
- `include/media_record/media_record.h`：umbrella，包含 export.h + node.h。
- `src/framework/public/BUILD`：`cc_library(name = "media_record", hdrs=glob(...), strip_include_prefix="include", alwayslink=1)` + `cc_binary(name = "media_record_shared", linkshared=True)`；deps 仅为骨架自身，暂不链接三个依赖库（冒烟测试中验证跨库链接）。

### Rationale

- 公共 target 暂不依赖三个库：骨架期保持轻量；跨库链接在 `tests/deps_smoke_test.cc` 中显式验证，避免公共 target 过早耦合。
- `REGISTER_NODE` 宏设计为注册表字符串键（对齐 graph_runtime `NodeFactoryRegistry`），后续业务节点无需改 runtime。

### Alternatives considered

- 公共 target 直接聚合三个库依赖：会让所有消费者强制链接 ffmpeg/skia，构建变重。**拒绝**（跨库链接由示例/测试承担）。

## 4. 冒烟示例与验证

### Decision

- `examples/hello_graph.cc`：构建一个最小 graph_runtime 图（含一个打印节点的示例配置），链接 `@graph_runtime//src/framework/public:runtime` 验证图运行时可用。
- `tests/deps_smoke_test.cc`：同时 `#include` 三个库的 umbrella header（`graph_runtime/graph_runtime.h`、`native_ui/core.h`、`video_codec/video_codec.h`），并调用最小编译可触达符号（如实例化 `VideoEncoder::Create` 之外的简单类型）验证链接完整。
- `Makefile` + `mk/verify.mk`：`make verify` 聚合 bazel build + bazel test + 示例运行（对齐 native_ui / video_codec 的 mk 机制）。

### Rationale

- 冒烟测试是"三个依赖库 + 本工程骨架"组合链路的唯一客观验收点（spec FR-006 / SC-003）。
- host 上 Android-only 能力（input surface / MediaCodec）用条件编译 stub，保持 host 构建可用。

## 5. 待确认事项

- 无（Technical Context 中无未解析项；spec 无 [NEEDS CLARIFICATION]）。
- 开放项（非阻塞，记录默认值）：本地依赖路径通过 `media_record_deps.bzl` 常量 + `.user.bazelrc` 覆盖；三个仓库路径默认取 `/Users/moks/Develop/docker/ubuntu24/codes/<repo>/<inner>`。

## 6. Pipeline 模板设计（澄清后新增）

### Decision

三类 pipeline（记录仪 / 推流 / 预览）以 **graph_runtime 原生 JSON 配置 schema** 表达（非自定义扩展），模板放在 `src/examples/configs/`，示例与配置校验测试直接消费同一 schema。

### Rationale

- 实测 graph_runtime `json_parser.cc` 的字段为：`nodes[].{name, type, input_streams, output_streams, input_side_packets, output_side_packets, options, executor, max_in_flight, source_layer}` + 顶层 `streams[].{source_node, source_port, dest_node, dest_port}`（见 `config/json/testdata/string_pipeline.json`）。
- `input_streams` / `output_streams` 采用 `tag:stream_name` 形式；`type` 即节点注册名（对齐 `NodeFactoryRegistry`）。
- **注意**：bootstrap spec 示例中使用的 `calculator` / `from` / `to` 字段**不匹配** graph_runtime 实际 schema，pipeline 模板契约必须以本 section 为准，spec 示例仅作示意图。

### 模板结构（record 记录仪 pipeline 示意）

```json
{
  "nodes": [
    { "name": "cam_front", "type": "StreamInputNode", "output_streams": ["output:front_frames"], "options": { "source": "image:front.jpg", "width": 1280, "height": 720, "fps": 30 } },
    { "name": "cam_rear",  "type": "StreamInputNode", "output_streams": ["output:rear_frames"],  "options": { "source": "image:rear.jpg", "width": 1280, "height": 720, "fps": 30 } },
    { "name": "signals",   "type": "SignalSourceNode", "output_streams": ["output:signals"] },
    { "name": "layout",    "type": "MultiViewLayoutNode", "input_streams": ["f:front_frames", "r:rear_frames"], "output_streams": ["output:view_frames"] },
    { "name": "overlay",   "type": "UiOverlayNode", "input_streams": ["video:view_frames", "signal:signals"], "output_streams": ["output:osd_frames"] },
    { "name": "encoder",   "type": "VideoEncoderNode", "input_streams": ["input:osd_frames"], "output_streams": ["output:es_packets"] },
    { "name": "recorder",  "type": "RecorderNode", "input_streams": ["input:es_packets"], "output_streams": ["output:clips"] },
    { "name": "muxer",     "type": "MuxerSinkNode", "input_streams": ["input:clips"] }
  ],
  "streams": [
    { "name": "front_frames", "source_node": "cam_front", "source_port": "output", "dest_node": "layout", "dest_port": "f" },
    { "name": "rear_frames",  "source_node": "cam_rear",  "source_port": "output", "dest_node": "layout", "dest_port": "r" },
    { "name": "signals",      "source_node": "signals",   "source_port": "output", "dest_node": "overlay", "dest_port": "signal" },
    { "name": "view_frames",  "source_node": "layout",    "source_port": "output", "dest_node": "overlay", "dest_port": "video" },
    { "name": "osd_frames",   "source_node": "overlay",   "source_port": "output", "dest_node": "encoder", "dest_port": "input" },
    { "name": "es_packets",   "source_node": "encoder",   "source_port": "output", "dest_node": "recorder", "dest_port": "input" },
    { "name": "clips",        "source_node": "recorder",  "source_port": "output", "dest_node": "muxer", "dest_port": "input" }
  ]
}
```

### 全量节点骨架（src/nodes/）

| 节点目录 | 对应 type | 说明 |
|----------|-----------|------|
| `stream_input` | `StreamInputNode` | 接收流接口（图像模拟 / 相机适配） |
| `signal_source` | `SignalSourceNode` | 信号模拟 / 透传 |
| `multi_view_layout` | `MultiViewLayoutNode` | 多视口排布 |
| `ui_overlay` | `UiOverlayNode` | OSD 叠加（时间戳 / 事件） |
| `video_encoder` | `VideoEncoderNode` | 编码 + surface 反馈 |
| `audio_encoder` | `AudioEncoderNode` | 音频编码 |
| `recorder` | `RecorderNode` | 缓存池 / 存储切换 / 防抖 |
| `muxer_sink` | `MuxerSinkNode` | MP4 落盘 |
| `stream_sink` | `StreamSinkNode` | 推流服务 |
| `preview` | `PreviewNode` | 屏幕预览 |

每目录交付：空 `BUILD.bazel`（`package(default_visibility=...)` + 空 `cc_library` 或注释占位）+ 头部占位（`.h` 声明注释），保证 `bazel build //...` 通过（FR-003 / SC-007）。

### 架构 / pipeline 设计文档落点

`media_record/doc/architecture/` 下新增：
- `engineering-structure.md`：模块划分、目录约定、命名规范、依赖规则。
- `pipelines.md`：三类 pipeline 的节点连线、数据流、旁路事件、统一组合规则（节点 / 流 / tag 命名约定），并以 `## 6` 的 schema 为准给出模板索引。
- `README.md` 更新指向上述文档。
