# Data Model: 模拟行车记录仪录制（Dashcam Simulated Recording）

**Branch**: `002-dashcam-sim-recording` | **Date**: 2026-08-18 | **Spec**: [spec.md](spec.md)

本 feature 无持久化数据；「数据模型」描述**运行期传递的媒体数据**、**录制会话状态**与**图配置模型**。

## 1. Simulated Frame（模拟视频帧）

| 字段 | 类型 | 约束 / 规则 |
|------|------|-------------|
| pixels | `video::codec::VideoFrame`（RGBA） | 画布与输入图片同尺寸；`planes[0]` 存 RGBA |
| timestamp | wall-clock（`std::chrono::system_clock`） | 录制时刻真实时间，随帧递增 |

**来源**：`StreamInputNode` 每帧由默认图片复制出帧；`MultiViewLayoutNode` 排布；`UiOverlayNode` 叠字后进入编码。

## 2. Timestamp Overlay（时间戳叠加层）

| 字段 | 类型 | 约束 / 规则 |
|------|------|-------------|
| format | string | 默认 `"YYYY-MM-DD HH:MM:SS"`（真实时钟） |
| position | rect | 默认右下角（画面一角），由 native_ui flex 布局（`Container` + `Text`）计算得出 |
| font | bitmap glyph table | 内嵌位图字体（5×7 或 8×13 数字 + 分隔符），白字 + 半透明黑底 |
| source_clock | `std::chrono::system_clock` | 每帧取当前真实时间 |

**状态转移**：无（纯渲染属性）。绘制由 media_record 软件位图字体完成（native_ui host Surface 无像素回读）。

## 3. Recording Session（录制会话）

| 字段 | 类型 | 约束 / 规则 |
|------|------|-------------|
| state | enum | `kIdle → kRecording → kFinalizing → kDone | kFailed` |
| duration_seconds | int | 默认 10 |
| fps | int | 默认 30 |
| frame_count | int | `duration_seconds × fps`（300） |
| frames_produced | int | 会话累计帧数 |
| output_target | path | `out/dashcam.mp4` |
| result_status | enum | `kOk / kFailed` + message |

**状态转移**：`kIdle → (Open) kRecording → (帧数达标) kFinalizing → (muxer trailer + 原子 rename) kDone`；任一步失败 → `kFailed`（删除临时文件，退出非零）。

## 4. Packet（节点间传输包）

| 字段 | 类型 | 约束 / 规则 |
|------|------|-------------|
| payload | 任意 C++ 类型 | 本期载荷：`video::codec::VideoFrame`（RGBA）/ `video::codec::VideoPacket`（H.264 Annex-B） |
| timestamp | `graph::runtime::Timestamp` | 帧序号驱动的递增时间戳（`Packet::MakePacket<T>().At(ts)`） |
| routing | stream 名（`"port:stream"` 冒号后） | 图内全局唯一；由 `PipelineRunner` 按 stream 路由，节点只读写自己声明的端口 |

**类型**：`graph::runtime::Packet`（graph_runtime 公共面类型，非 media_record 自有）。**规则**：单消费者语义；`PipelineRunner` 按 `GraphConfig` 的 stream 声明路由；节点只读写自己声明的 input/output 端口。

## 5. 图配置模型（GraphConfig）

| 项 | 值 |
|----|-----|
| 配置类型 | `graph::runtime::GraphConfig`（graph_runtime 唯一配置模型；JSON 为其 schema，由 graph_runtime `JsonParser` 解析，节点参数在每节点 `"options"` 对象，不定义新 schema） |
| 节点 | 6 个实例（StreamInput + Layout + Overlay + Encoder + Recorder + Muxer） |
| 输入图片 | `src/examples/assets/dashcam_default.png`（StreamInput 节点 options） |
| 分辨率 | 1280×720（配置在 StreamInput / Muxer 节点 options） |
| 时长 / 帧率 | 10s / 30fps（`frame_count = 300` 配置在 StreamInput 节点 options） |
| 输出 | `out/dashcam.mp4`（MuxerSink 节点 options） |

**校验规则**：节点名唯一、type 已注册、stream 引用合法、无环（复用 graph_runtime `ConfigValidator` 语义；`pipeline_config_test` 断言更新为 graph_runtime schema）。

## 6. 媒体格式链

```
default.png ──decode(Image::FromFile/CopyPixels)──▶ RGBA(VideoFrame) ──flex布局──▶ RGBA(铺满整帧)
      │                                                                    │
      └── 时间戳文字（软件位图字体，flex 定位，叠加角落）────▶ RGBA
      ──▶ 软件 ARGBToI420 ──▶ VideoEncoder(H.264) ──▶ VideoPacket(Annex-B)
      ──▶ codec Muxer（FileByteSink 临时文件）──原子 rename──▶ out/dashcam.mp4
```
