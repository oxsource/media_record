# Data Model: 模拟行车记录仪录制（Dashcam Simulated Recording）

**Branch**: `002-dashcam-sim-recording` | **Date**: 2026-08-18 | **Spec**: [spec.md](spec.md)

本 feature 无持久化数据；「数据模型」描述**运行期传递的媒体/事件数据**、**录制会话状态**与**默认配置模型**。

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
| position | enum/rect | 默认右下角（画面一角） |
| font | bitmap glyph table | 内嵌位图字体（5×7 或 8×13 数字 + 分隔符），白字 + 半透明黑底 |
| source_clock | `std::chrono::system_clock` | 每帧取当前真实时间 |

**状态转移**：无（纯渲染属性）。

## 3. SignalEvent（信号事件，旁路）

| 字段 | 类型 | 约束 / 规则 |
|------|------|-------------|
| type | enum | 本期最小集（如 `kTick` / `kNone`） |
| timestamp_us | int64 | 事件发生时刻 |
| payload | optional | 本期不使用（扩展点） |

**来源**：`SignalSourceNode` 周期性产出；经 `signal` tag 旁路输入 `UiOverlayNode`（本期 OSD 只渲染时间戳，事件可忽略）。

## 4. Recording Session（录制会话）

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

## 5. Packet（节点间传输包）

| 字段 | 类型 | 约束 / 规则 |
|------|------|-------------|
| payload | variant | `VideoFrame`（RGBA）/ `VideoPacket`（H.264 Annex-B）/ `SignalEvent` |
| stream_name | string | 图内全局唯一（对齐 `streams[].name`） |
| pts_us | int64 | 帧序号 × 1e6 / fps（编码时间戳） |

**规则**：单消费者语义；`pipeline_runner` 按 `streams[]` 路由；节点只读写自己声明的 input/output 流。

## 6. 默认配置模型（dashcam_record.json）

| 项 | 值 |
|----|-----|
| 节点 | 7 个实例（1×StreamInput + 1×SignalSource + Layout + Overlay + Encoder + Recorder + Muxer） |
| 输入图片 | `src/examples/assets/dashcam_default.png` |
| 分辨率 | 跟随输入图片 |
| 时长 / 帧率 | 10s / 30fps |
| 输出 | `out/dashcam.mp4` |

**校验规则**：节点名唯一、type 已注册、stream 引用合法、port tag 匹配（复用 001 `pipeline_config_test` 校验器）。

## 7. 媒体格式链

```
default.png ──decode──▶ RGBA(VideoFrame) ──layout──▶ RGBA ──overlay──▶ RGBA
      │                                                              │
      └── 时间戳文字（位图字体）──────────────────────────▶ ARGBToI420（libyuv）
      ──▶ VideoEncoder(H.264) ──▶ VideoPacket(Annex-B) ──▶ libavformat mov muxer ──▶ out/dashcam.mp4
```
