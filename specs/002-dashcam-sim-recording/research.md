# Research: 模拟行车记录仪录制（Dashcam Simulated Recording）

**Branch**: `002-dashcam-sim-recording` | **Date**: 2026-08-18 | **Spec**: [spec.md](spec.md)

## 1. 节点实现范围（7 类，FR-010）

### Decision

recorder.json 引用 8 个节点实例、7 类节点类型。本期将 7 类节点全部实现为可运行的真实实现；AudioEncoder / StreamSink / Preview 3 类节点保持 001 骨架：

| 节点目录 | 注册名（type） | 职责（本期真实实现） |
|----------|---------------|----------------------|
| `stream_input` | `StreamInputNode` | 解码默认图片 → 逐帧输出 `Packet<VideoFrame>`（RGBA），带真实时钟时间戳 |
| `signal_source` | `SignalSourceNode` | 输出 `Packet<SignalEvent>`（本期输出最小事件序列，旁路给 OSD） |
| `multi_view_layout` | `MultiViewLayoutNode` | 将单路输入排布为整帧画面（本期单视图；不实现多路拼接） |
| `ui_overlay` | `UiOverlayNode` | 在画面角落以位图字体绘制时间戳文字（真实时钟，逐帧更新） |
| `video_encoder` | `VideoEncoderNode` | RGBA→I420（libyuv）+ video_codec `VideoEncoder`（H.264）编码 |
| `recorder` | `RecorderNode` | 录制会话生命周期（单会话单分段），帧计数 / 时长收敛，转发包给 muxer |
| `muxer_sink` | `MuxerSinkNode` | 用 media_record 自带 FFmpeg（libavformat mov muxer）封装 H.264 → MP4 |

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
- 转换使用 media_record 自带 `@libyuv//:libyuv` 的 `ARGBToI420`（`third_party/libyuv` 已有 vendored BUILD，libyuv 是中性依赖，跨平台可编译）。

### Rationale

- 与 001 冒烟测试（deps_smoke_test 链接三库 umbrella）一致：只经公共 umbrella 消费 video_codec。
- 拉取模式（`Encode` 返回 packet）比 push 模式（需 `PacketSink` / queue，且 `PacketQueue` 不在 video_codec 公共 umbrella 导出）更简单、少一层依赖。

### Alternatives considered

- push 模式 + `PacketSink`：`PacketSink` 在 core 公共模块，但 `PacketQueue` 实现不在公共 umbrella（queue 模块可见性受限）。**拒绝**，用 pull 模式。
- 用 video_codec 的 `Muxer`（api 公共）：`Muxer::SetOutput(ByteSink*)` 依赖 `io::ByteSink`，而 **io / consumer 模块不导出到公共 umbrella**（`public/BUILD.bazel` deps 仅 core/api/utils），外部 workspace 无法消费 FileByteSink。**拒绝**，见 §3。

## 3. 封装路径（MP4 输出）

### Decision

`MuxerSinkNode` 使用 **media_record 自带 vendored FFmpeg**（`third_party/ffmpeg`，`@ffmpeg//:ffmpeg_codec`）：

- 配置已内置 `--enable-muxer=mov`（mov muxer 即写 `.mp4`）、`libavformat`、libx264 编码器（`third_party/ffmpeg/BUILD.bazel`）。
- 流程：`avformat_alloc_output_context2`（"mp4"）→ 建 video stream（H.264，从首个关键帧解析 SPS/PPS 作 extradata）→ `avio_open` → `avformat_write_header` → 逐包 `av_write_frame`（Annex-B → length-prefixed）→ `av_write_trailer` → close。
- 输出到 `out/dashcam.mp4`；**写临时文件 + 成功后原子 rename**，失败即删除临时文件（FR-009 不残留残缺产物）。
- 已存在则覆盖，并打日志提示（澄清：覆盖并继续）。

### Rationale

- video_codec 的 `Muxer` 接口是公共的，但其输出依赖 `io::ByteSink`/`FileByteSink`，这两个模块**不在** `@video_codec//src/framework/public:video_codec` 的公共导出内（其 deps 仅 core/api/utils + backend select）。外部 workspace 无法经公共 umbrella 拿到 FileByteSink 的实现，也无法在 BUILD 中引用 io（可见性受限）。故 video_codec Muxer 无法从公共面直接落盘。
- media_record 的 `third_party/ffmpeg` 已是为 @ffmpeg http_archive 准备的 vendored BUILD（含 libavformat + mov muxer + libx264），本 workspace 可直接消费 `@ffmpeg//:ffmpeg_codec`，不新增仓库、不触及 video_codec 内部。

### Alternatives considered

- 修改 video_codec 公共面（把 io 加进 umbrella）：跨仓库变更、超出本期 feature 范围。**记录为后续依赖改进项**。
- 输出裸 H.264 流（`.h264`，不封装）：FR-004 要求「通用、可播放的视频格式」，且假设明确 MP4。**拒绝**。
- 自研最小 MP4 muxer：重复实现 libavformat。**拒绝**。

## 4. OSD 时间戳渲染（UiOverlayNode）

### Decision

`UiOverlayNode` 以**软件位图字体**（内嵌 5×7 或 8×13 数字/分隔符字形）将时间戳文字直接绘制进 RGBA 帧缓冲，位于默认位置（右下角），颜色白字 + 半透明黑底衬底，保证可辨。

- 文字内容：真实时钟 `YYYY-MM-DD HH:MM:SS`（`std::chrono::system_clock` + `strftime`），每帧取当前时间 → 随录制逐帧递增。
- 输入 tag `video` 接画面帧、`signal` 接事件（本期 OSD 只渲染时间戳，事件可忽略/可叠加简单标记）。

### Rationale

- native_ui host 的 `Surface`（`surface.cc`）只公开 `Dump(path)` 写 PNG，**无公共像素回读**；`Surface::CreateFromBuffer`（CPU 回读路径）是 Android-only，host 上 stub 返回 nullptr。故无法用 native_ui Canvas 绘制后取回 RGBA 像素。
- 位图字体绘制是自包含、确定性、可单测的软件渲染，host/CI 可运行，不依赖字体文件与 GPU。

### Alternatives considered

- native_ui Canvas → Surface → `Dump` PNG → 再解码：性能不可行（每帧一次 PNG 编解码）。**拒绝**。
- FFmpeg drawtext 滤镜：需额外字体文件，且我们已走 vendored FFmpeg 的 libavformat 只封装不滤镜。**拒绝**。

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

- 节点单测：位图字体绘制（时间戳文字出现在预期区域且逐帧变化）、RGBA→I420 转换（尺寸/格式正确）、SignalSource 事件计数。
- 端到端 `dashcam_record_test.cc`：短帧数（如 60 帧）驱动 `PipelineRunner`，断言输出 MP4 存在、含 `ftyp` / `moov` / `mdat`、帧数匹配、时间戳文字逐帧递增。
- `pipeline_config_test.cc`：把 `dashcam_record.json` 加入模板校验清单。
- `make verify`：`bazel build //...` + `bazel test //src/tests:all` + 运行录制入口并检查产物存在（SC-004）。

## 10. 待确认事项

- 无阻塞项。开放项（非阻塞，记录默认值）：bitrate 默认值待实现时按分辨率固定一个合理值（如 2~4 Mbps）；默认图片分辨率与内容以 `dashcam_default.png` 实际文件为准。
