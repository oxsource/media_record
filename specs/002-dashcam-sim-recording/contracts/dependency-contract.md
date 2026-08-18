# Contract: Dependency — 三库消费与 vendored FFmpeg/libyuv

**Branch**: `002-dashcam-sim-recording` | **Date**: 2026-08-18 | **Spec**: [spec.md](../spec.md)

## 1. 三库消费（仅公共 umbrella）

| 库 | 消费 target | 本期用途 |
|----|-------------|----------|
| graph_runtime | `@graph_runtime//src/framework/public:runtime` | 值类型 / schema 语义（不执行图；公共 umbrella 不含 GraphRuntime 执行类） |
| native_ui | `@native_ui//:native_ui` | `Image::FromFile` / `CopyPixels` 解码默认图片（host Surface 无像素回读，OSD 用软件位图字体） |
| video_codec | `@video_codec//src/framework/public:video_codec` | `VideoEncoder`（H.264, I420）/ `VideoFrame` / `VideoPacket` / `CodecFactory` |

## 2. vendored 依赖（media_record 自带构建）

| 依赖 | target | 用途 |
|------|--------|------|
| FFmpeg | `@ffmpeg//:ffmpeg_codec`（`third_party/ffmpeg`） | libavformat mov muxer 写 MP4（MuxerSinkNode） |
| libyuv | `@libyuv//:libyuv`（`third_party/libyuv`） | `ARGBToI420`（RGBA→I420，编码前转换） |

## 3. 关键约束（研究 §2/§3 结论）

| # | 约束 |
|---|------|
| D-1 | video_codec **公共 umbrella 不导出** io（`ByteSink`/`FileByteSink`）与 queue（`PacketQueue`）模块 → 编码走 `VideoEncoder::Encode` pull 模式；MP4 封装用 media_record 自带 vendored FFmpeg |
| D-2 | 若后续需经 video_codec 公共面封装，需先扩展其 public BUILD（加入 io），属跨仓改进项，不在本期 |
| D-3 | native_ui host `Surface` 无公共像素回读（`CreateFromBuffer` Android-only）→ OSD 时间戳用软件位图字体绘制 |
| D-4 | graph_runtime 公共 umbrella 不含执行类 → 本图由 media_record `PipelineRunner` 同步执行 |
| D-5 | 路径可覆盖沿用 001：`media_record_deps.bzl` 常量 / `.user.bazelrc` |

## 4. 验收

- 7 类节点 + 录制入口在 host 全量构建通过（`bazel build //...`）。
- 录制产物可播放（H.264 + MP4）。
- Android 交叉编译保持兼容（host 对 Android-only 能力 stub，不因本 feature 引入平台硬依赖）。
