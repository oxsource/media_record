# Contract: Dependency — 三库公共面消费与 video_codec umbrella 前置扩展

**Branch**: `002-dashcam-sim-recording` | **Date**: 2026-08-18 | **Spec**: [spec.md](../spec.md)

## 1. 三库消费（仅公共 umbrella）

| 库 | 消费 target | 本期用途 |
|----|-------------|----------|
| graph_runtime | `@graph_runtime//src/framework/public:runtime`（及根别名 `@graph_runtime//:runtime`） | **完整节点运行时公共面**：`Node` / `NodeRegistry`（`GRAPH_RUNTIME_REGISTER_NODE`）/ `GraphContext` / `Packet` / `Timestamp` / `GraphConfig` / `ConfigValidator`。7 类节点实现为其 `graph::runtime::Node` 子类，图拓扑存于 `GraphConfig`。内部头（`src/framework/node/*.h`、`src/framework/stream/*.h`、`src/framework/config/*.h`）经公共 target 传递依赖提供 include 路径（与 graph_runtime 自身 `src/examples/*` 单 target 依赖方式一致；未启用 layering_check）。**注意**：`GraphRuntime` 执行类不连接节点内部流（`OutputStreamManager::AddMirror` 无调用点），本图由 media_record 的同步驱动器执行（见 contracts/pipeline-contract.md） |
| native_ui | `@native_ui//:native_ui` | `Image::FromFile` / `CopyPixels` 解码默认图片为 RGBA + `Container` / `ExternalImage` / `Text` flex 布局组合画面结构与位置（像素由 media_record 软件绘制，host Surface 无像素回读） |
| video_codec | `@video_codec//src/framework/public:video_codec` | `VideoEncoder`（H.264, I420）/ `Muxer`（MP4）/ `ByteSink` / `FileByteSink` / `VideoFrame` / `VideoPacket` / `CodecFactory` |

## 2. 配置唯一性（不维护第二份配置）

- 图拓扑**只存于 graph_runtime 的 `GraphConfig`**；JSON 配置文件（`dashcam_record.json` / `recorder.json` 等）采用 graph_runtime JSON schema（`nodes[]` + `input_streams`/`output_streams`，`"port:stream"` 命名，无独立 `streams[]` 段）。
- **解析只用 graph_runtime 的 `JsonParser`**（`@graph_runtime//src/framework/config/json:json_parser`，经 graph_runtime 公共 `runtime` target 导出，媒体 `JsonParser` 可见性放开、纳入公共面——同 video_codec `io` 导出先例）。media_record **不维护任何自有 JSON 读取器**；入口/测试统一 `graph::runtime::JsonParser::Parse(path)` → `GraphConfig`，错误语义（缺文件 / JSON 语法 / 缺 `type`）即 graph_runtime 原样报错。`JsonParser` **不解析 per-node `options`**，因此：
  - 节点参数（`image` / `output` / `fps` / `duration` 等）不入 JSON，由录制入口程序化设置 `GraphConfig::NodeDef::options`（CLI 参数 + 默认值），对齐 `add_packet_demo` 的程序化建图方式。
- 001 的 `src/framework/config`（`PipelineConfig` + 校验器）**本期删除**，不再存在 media_record 自有配置模型。

## 3. vendored 依赖（media_record 不再直接消费）

media_record pipeline **不直接链接** `@ffmpeg` / `@libyuv`（不引入 skia / ffmpeg / libyuv，按澄清）。`third_party/` 下 ffmpeg / libyuv / skia 等的 BUILD 包装仅用于满足 graph_runtime / native_ui / video_codec 的 `*_setup()` 对 http_archive `build_file` label 的解析（媒体 `ffmpeg`/`libyuv` 源码仍由 video_codec 的 setup 拉取构建，供 video_codec 内部 backend 使用）。RGBA→I420 由 media_record 内置软件转换实现。

## 4. 关键约束（研究 §2/§3/§4 结论）

| # | 约束 |
|---|------|
| D-1 | **前置跨仓任务**：video_codec 公共 umbrella 本期导出 `io`（`ByteSink` / `FileByteSink`）。改动：`src/framework/public/BUILD.bazel` 的 `video_codec` / `video_codec_hdrs` target 增加 `@video_codec//src/framework/io` deps；`io/BUILD.bazel` 放开对 `@video_codec//src/framework/public` 的可见性；`dist/host/include/video_codec/`（及 android-arm64）拷贝 `byte_sink.h` / `file_byte_sink.h`；video_codec 侧补一条 umbrella 头文件编译冒烟 |
| D-2 | 编码走 `VideoEncoder::Encode` pull 模式（`VideoPacket` 拉取）；push 模式依赖的 `PacketQueue` 不在公共 umbrella 导出，不采用 |
| D-3 | `VideoEncoder` FFmpeg backend 仅接受 I420 / NV12（`kRGBA` → `kUnsupportedFormat`）→ RGBA→I420 由 media_record 内置软件转换完成 |
| D-4 | `Muxer::SetOutput(ByteSink*)` 为公共接口；`Muxer` 输出经 `FileByteSink` 写临时文件，成功后原子 rename 落盘（FR-009） |
| D-5 | native_ui host `Surface` 无公共像素回读（`CreateFromBuffer` Android-only stub）→ 画面像素由 media_record 软件绘制（flex 布局定位 + 位图字体） |
| D-6 | graph_runtime `GraphRuntime` 执行类不连接节点内部流（`AddMirror` 无调用点）→ 本图由 media_record `PipelineRunner`（`src/framework/runner/`）在调用线程上按拓扑序驱动 `GraphContext` + 手工搬运 `Packet` 执行（对齐 `string_pipeline.cc` 模式） |
| D-7 | **解析复用 graph_runtime `JsonParser`**（经公共 `runtime` target 导出，media_record 不再有自有 JSON 读取器）；节点参数程序化注入 `NodeDef.options` |
| D-8 | 路径可覆盖沿用 001：`media_record_deps.bzl` 常量 / `.user.bazelrc` |
| D-9 | **命名避让**：media_record 不得在 `src/framework/stream/`、`src/framework/config/` 下放置文件（graph_runtime 同名目录会被主 workspace include 遮蔽） |

## 5. 验收

- video_codec 公共 umbrella 扩展后，media_record 仅经 `@video_codec//src/framework/public:video_codec` 一个 target 获得编码 + 封装 + ByteSink 能力；仅经 `@graph_runtime//src/framework/public:runtime` 一个 target 获得节点/配置/类型全部能力。7 类节点 + 录制入口在 host 全量构建通过（`bazel build //...`）。
- 录制产物可播放（H.264 + MP4，`ftyp`/`moov`/`mdat` 存在）。
- Android 交叉编译保持兼容（host 对 Android-only 能力 stub，不因本 feature 引入平台硬依赖）。
