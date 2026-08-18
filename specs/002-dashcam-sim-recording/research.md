# Research: 模拟行车记录仪录制（Dashcam Simulated Recording）

**Branch**: `002-dashcam-sim-recording` | **Date**: 2026-08-18 | **Spec**: [spec.md](spec.md)

## 1. 节点实现范围（7 类，FR-010）

### Decision

recorder.json 引用 8 个节点实例、7 类节点类型。本期将 7 类节点全部实现为可运行的真实实现；AudioEncoder / StreamSink / Preview 3 类节点保持 001 骨架：

| 节点目录 | 注册名（type） | 职责（本期真实实现） |
|----------|---------------|----------------------|
| `stream_input` | `StreamInputNode` | 解码默认图片 → 逐帧输出 `Packet<VideoFrame>`（RGBA），带真实时钟时间戳 |
| `signal_source` | `SignalSourceNode` | 输出 `Packet<SignalEvent>`（本期输出最小事件序列，旁路给 OSD） |
| `multi_view_layout` | `MultiViewLayoutNode` | 用 native_ui flex 布局（`Container` + `ExternalImage`）确定画面结构与位置，软件渲染基帧（本期单视图；不实现多路拼接） |
| `ui_overlay` | `UiOverlayNode` | 以 flex 布局定位 + 软件位图字体绘制时间戳文字（真实时钟，逐帧更新），叠加于画面固定角落 |
| `video_encoder` | `VideoEncoderNode` | 软件 RGBA→I420（media_record 内置）+ video_codec `VideoEncoder`（H.264）编码 |
| `recorder` | `RecorderNode` | 录制会话生命周期（单会话单分段），帧计数 / 时长收敛，转发包给 muxer |
| `muxer_sink` | `MuxerSinkNode` | 用 video_codec 公共面 `Muxer`（`FileByteSink`）封装 H.264 → MP4 |

### Rationale

- spec FR-010 明确本期交付范围为 recorder.json 引用的 7 类节点；其余 3 类不在本期范围。
- recorder.json（双摄参考模板）与 pipeline_config_test 的断言（8 nodes / 7 streams）**保持不变**；本期新增**单路可运行默认配置** `dashcam_record.json`（1×StreamInput + SignalSource + Layout + Overlay + Encoder + Recorder + Muxer），符合「以单张图片模拟单路输入」的假设。

### Alternatives considered

- 直接改 recorder.json 为单路：破坏既有测试断言与「双摄参考模板」定位。**拒绝**，另立单路默认配置。

## 2. 编码路径（video_codec 公共面）

### Decision

`VideoEncoderNode` 复用 `@video_codec//src/framework/public:video_codec` 的公共 API：

- 构造：`CodecFactory::CreateVideo(VideoConfig)`，`cfg = { codec: kH264, width, height, fps: 30, input_format: kI420, backend: kAuto }`。
- 编码：`Init()` → 每帧 `Encode(VideoFrame)` → `Result<VideoPacket>`（Annex-B）→ `Flush()` → `Release()`。
- **FFmpeg backend 只接受 I420 / NV12**（`ffmpeg_video.cc: ToAvPixFmt` 对 `kRGBA` 返回 `AV_PIX_FMT_NONE` → `kUnsupportedFormat`）。因此节点内需先做 RGBA→I420 转换。
- 转换使用 **media_record 内置软件转换**（`VideoEncoderNode` 内实现的 `ARGBToI420`，纯 C++，~30 行：每行 RGB→YUV BT.601 转换 + 2x2 下采样 UV）；不引入 libyuv。

### Rationale

- 与 001 冒烟测试（deps_smoke_test 链接三库 umbrella）一致：只经公共 umbrella 消费 video_codec。
- 拉取模式（`Encode` 返回 packet）比 push 模式（需 `PacketSink` / queue，且 `PacketQueue` 不在 video_codec 公共 umbrella 导出）更简单、少一层依赖。
- 澄清明确 media_record **不再引入 skia / ffmpeg / libyuv**；RGBA→I420 是确定性的窄转换，内置软件实现可单测、跨平台、无额外依赖。

### Alternatives considered

- push 模式 + `PacketSink`：`PacketSink` 在 core 公共模块，但 `PacketQueue` 实现不在公共 umbrella（queue 模块可见性受限）。**拒绝**，用 pull 模式。
- `@libyuv//:libyuv` 的 `ARGBToI420`：libyuv 是中性依赖，但违背「不再引入 libyuv」的澄清。**拒绝**，内置软件转换。

## 3. 封装路径（MP4 输出）

### Decision

`MuxerSinkNode` 使用 **video_codec 公共面 `Muxer`**（`CodecFactory::CreateMuxer`，`MuxFormat::kMp4`，FFmpeg/libavformat mov muxer backend）：

- 输出：`Muxer::SetOutput(ByteSink*)` 挂接 `FileByteSink`（写临时文件 `out/.dashcam.mp4.tmp`）→ 逐包 `Push(VideoPacket)` → `Flush()` / `Finish()` 写 trailer → 成功后**原子 rename** 到 `out/dashcam.mp4`；失败删除临时文件（FR-009 不残留残缺产物）。
- 已存在则覆盖，并打日志提示（澄清：覆盖并继续）。
- MP4 可选项：`MuxerConfig.fragmented`。默认 `true`（fMP4）便于流式；本期为本地文件、需最大兼容，可设 `fragmented = false`（`av_interleaved_write_frame`，moov 尾写）。**实现时以可播放性为准**，二者均满足 SC-002。

### Rationale

- **前置依赖**：codec `Muxer` 的输出接口是 `io::ByteSink`（`muxer.h` 仅前向声明），`ByteSink` / `FileByteSink` 实现在 `io` 模块，**不在** `@video_codec//src/framework/public:video_codec` 公共导出内（public BUILD deps 仅 core/api/utils），且 io target 可见性受限（`io/BUILD.bazel`），外部 workspace 无法直接消费。故本期**前置任务**：将 `io`（`ByteSink` / `FileByteSink`）纳入 video_codec 公共 umbrella（public BUILD 增加 io deps + 放开 io 可见性 + dist/host/include 拷贝头文件），见 `contracts/dependency-contract.md` D-1。
- 复用 codec `Muxer` 后，media_record 无需直接链接 FFmpeg，编码器与封装器均来自 video_codec 同一公共面，符合澄清「使用 codec 的 VideoEncoder 及 Muxer 实现编码及 MP4」。

### Alternatives considered

- media_record 自带 vendored FFmpeg（`@ffmpeg//:ffmpeg_codec`，libavformat mov muxer）封装：违背「不再引入 ffmpeg」的澄清。**拒绝**。
- 直接消费 video_codec `io` 内部 target：可见性受限（`@video_codec//src/framework:__subpackages__`），外部 workspace 无法引用。**拒绝**。
- 自研最小 MP4 muxer：重复实现 libavformat。**拒绝**。

## 4. OSD 时间戳渲染与画面组合（UiOverlayNode / MultiViewLayoutNode）

### Decision

画面组合与 OSD 统一使用 **native_ui flex 布局 + media_record 软件绘制**：

- `MultiViewLayoutNode`：构建 native_ui widget 树 `Container`（flex）挂 `ExternalImage`（输入图片）+ `Text`（时间戳文本，本期由 OSD 绘制），`Layout(width, height)` 后读子组件 bounds → 将输入图片软件 blit 进 media_record 自有 RGBA 帧缓冲（铺满整帧，作为底层）。
- `UiOverlayNode`：以 flex 布局给出的时间戳位置，用**软件位图字体**（内嵌 5×7 或 8×13 数字/分隔符字形）将真实时钟时间戳文字绘制进 RGBA 帧缓冲（默认右下角，白字 + 半透明黑底衬底，保证可辨）。
- 文字内容：真实时钟 `YYYY-MM-DD HH:MM:SS`（`std::chrono::system_clock` + `strftime`），每帧取当前时间 → 随录制逐帧递增。
- 输入 tag `video` 接画面帧、`signal` 接事件（本期 OSD 只渲染时间戳，事件可忽略/可叠加简单标记）。

### Rationale

- 澄清明确：布局/时间戳**直接使用 native_ui 的 canvas/flex 布局组合**（底部/底层为 ExternalImage，上方叠加时间戳文本）。
- native_ui host 的 `Surface`（`surface.cc`）只公开 `Dump(path)` 写 PNG，**无公共像素回读**；`Surface::CreateFromBuffer`（CPU 回读路径）是 Android-only，host 上 stub 返回 nullptr（`surface_test.cc: CreateFromBufferHostStubNull`）。故无法用 native_ui Canvas 绘制后取回 RGBA 像素 → 最终像素绘制在 media_record 自有 RGBA 缓冲中完成。
- native_ui 公共面提供 `Container`（flex/Yoga）、`ExternalImage`、`Text`、`Image::FromFile`/`CopyPixels`，足以支撑「flex 布局组合 + 软件绘制」；位图字体绘制自包含、确定性、可单测，host/CI 可运行。

### Alternatives considered

- 扩展 native_ui 公共面增加 host 像素回读（`Surface::CopyPixels` 或 host `CreateFromBuffer`）：第二处跨仓改动，超出本期范围。**记录为后续依赖改进项**。
- native_ui Canvas → Surface → `Dump` PNG → 再解码：性能不可行（每帧一次 PNG 编解码）。**拒绝**。
- FFmpeg drawtext 滤镜：需额外字体资源，且 media_record 不再引入 FFmpeg。**拒绝**。

## 5. 帧传输与运行器（src/framework/stream/）

### Decision

新增 `src/framework/stream/` 模块：

- `packet.h`：`media::record::Packet`，轻量持有 `video::codec::VideoFrame` / `video::codec::VideoPacket` / `SignalEvent`（v1 用 `std::any` 或 union 风格包装，单消费者语义）。
- `stream_buffer.h`：按流名的有界信箱（`stream_name → deque<Packet>`），供节点 input/output 读取与写入。
- `pipeline_runner.{h,cc}`：读取 `PipelineConfig` → 经 `NodeRegistry::Create(type)` 实例化节点 → 按 `streams[]` 连接 input/output → **同步 frame loop**：按拓扑序逐帧驱动 source 节点（StreamInput 每帧产出一帧，SignalSource 周期产出事件），下游节点消费输入、产输出，直到 300 帧（10s×30fps）结束 → 逐节点 `Close()`（recorder 收尾、muxer 写 trailer）。

### Rationale

- 001 的 `Node` 骨架（`Open/Process/Close` + `NodeRegistry`）没有数据通路；帧传输层是本 feature 打通「输入→组合→OSD→编码→录制→封装」的粘合层，保持 config 驱动拓扑（不硬编码节点顺序）。
- graph_runtime 公共 umbrella 只导出值类型 / schema，不导出 `GraphRuntime` 执行类 / `Node` / scheduler（public `BUILD.bazel` hdrs 仅 include/graph_runtime/*.h）。故**不能**用 graph_runtime 运行时执行本图，需 media_record 自建轻量运行器。
- 同步 batch（FR-009 / 假设「同步、有限时长批处理」）→ 用简单 frame loop，不引入线程池 / 背压调度。

### Alternatives considered

- 直接调用 graph_runtime 的 `GraphRuntime` 类执行：公共 umbrella 未导出该类（`src/framework/public/graph_runtime.h` 类实现不在 include/graph_runtime/ 导出头中）。**拒绝**。
- 每个节点内部用独立线程 + queue：超出「同步批处理」范围。**拒绝**。

## 6. 录制会话（RecorderNode）

### Decision

单会话单分段：`RecorderNode` 管理会话状态（`RecordingSession`：开始/进行/结束），帧计数到 `duration_seconds × fps` 后结束，将编码包（`VideoPacket`）逐包转发给 `MuxerSinkNode`；`Close()` 时收敛（触发 muxer finish + 原子落盘）。

### Rationale

- 澄清明确：单会话单分段，无缓存池 / 多分段切换 / 防抖。

## 7. 默认值（FR-005/006/007、Assumptions）

| 项 | 默认值 |
|----|--------|
| 录制时长 | 10 秒（300 帧 @ 30fps） |
| 帧率 | 30fps |
| 输入图片 | `src/examples/assets/dashcam_default.png`（内置一张示例图，如 1280×720；分辨率跟随输入） |
| 时间戳格式 | `YYYY-MM-DD HH:MM:SS`（真实时钟） |
| 时间戳位置 | 画面右下角 |
| 输出位置 | `out/dashcam.mp4`（`out/` 为约定产物目录，不存在则创建） |
| 编码 | H.264，30fps，分辨率=输入图片，bitrate 取 video_codec 默认（或按分辨率估算） |

## 8. 错误处理与失败可定位（FR-008/009、Edge Cases）

- 输入图片缺失 / 路径无效 → 报错包含路径，退出非零。
- 图片格式不支持 → 解码失败报可定位错误。
- 输出目录不可写 / 不存在且无法创建 → 报错包含路径，退出非零。
- 输出文件已存在 → 覆盖 + 日志提示。
- 编码中断 / muxer 失败 → 删除临时文件，不残留半成品，退出非零。

## 9. 测试与验证（SC-001~005）

- 节点单测：软件位图字体绘制（时间戳文字出现在预期区域且逐帧变化）、软件 RGBA→I420 转换（尺寸/格式正确）、SignalSource 事件计数。
- 端到端 `dashcam_record_test.cc`：短帧数（如 60 帧）驱动 `PipelineRunner`，断言输出 MP4 存在、含 `ftyp` / `moov` / `mdat`、帧数匹配、时间戳文字逐帧递增（经 codec `Muxer` + `FileByteSink` 落盘）。
- `pipeline_config_test.cc`：把 `dashcam_record.json` 加入模板校验清单。
- `make verify`：`bazel build //...` + `bazel test //src/tests:all` + 运行录制入口并检查产物存在（SC-004）。

## 10. 前置任务与待确认事项

- **前置任务（跨仓）**：video_codec 公共 umbrella 导出 `io`（`ByteSink` / `FileByteSink`）——public BUILD 增加 io deps、`io/BUILD.bazel` 放开对外可见性、`dist/host/include/video_codec/` 拷贝两个头文件，并在 video_codec 侧补一条 umbrella 编译冒烟（`tests/`）。这是本 feature 开始实现前的依赖项。
- 无阻塞开放项。非阻塞默认值：bitrate 按分辨率固定（如 1280×720 → 2~4 Mbps）；默认图片分辨率与内容以 `dashcam_default.png` 实际文件为准；MP4 `fragmented` 以可播放性为准（默认 `false`，普通文件 MP4）。
