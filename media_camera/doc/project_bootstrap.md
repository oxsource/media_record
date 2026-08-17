# Media Camera - Project Bootstrap

> Version: 0.2
>
> Status: Draft
>
> Purpose: Initial project definition for AI-assisted specification-driven development.

---

# 1. Vision (Why)

## 1.1 Project Vision

Media Camera 是一套**跨平台多路摄像头记录仪框架（Cross-Platform Multi-Camera Recorder Framework）**，面向行车记录仪（DVR）、环视（AVM）、运动相机等场景。

项目**组合复用**现有三个独立 Bazel 仓库的能力，通过 **graph_runtime 配置驱动的节点图（Configurable Node Graph）** 编排出一条完整的记录仪流水线：

| 仓库 | 提供能力 | 目录 |
|------|----------|------|
| **graph_runtime** | Stream-Based 图运行时：`Node` / `Stream` / `Packet` / `Scheduler`，配置驱动 Graph 构建 | `/Users/moks/Develop/docker/ubuntu24/codes/graph_runtime` |
| **native_ui** | 原生 UI 框架：`Widget` / `Layout` / `Render(Skia)` / `Surface` / `State`，用于多路预览与 OSD 绘制 | `/Users/moks/Develop/docker/ubuntu24/codes/native_ui` |
| **video_codec** | 音视频编码框架：`VideoEncoder` / `AudioEncoder` / `Muxer` / `PacketConsumer`，按平台选择 MediaCodec / FFmpeg 后端 | `/Users/moks/Develop/docker/ubuntu24/codes/video_codec` |

分工：

```
graph_runtime      —— 调度骨架（谁执行、何时执行、数据如何流动），节点在此组合
video_codec        —— 编码与存储（帧 → H.264/HEVC → MP4 / 推流）
native_ui          —— 渲染与交互（多路预览、OSD 叠加、事件指示）
```

## 1.2 核心设计原则

- **核心实现全部为 Node**：框架的一切能力都以 graph_runtime 节点形式提供，由用户通过 JSON 配置自行组合成任意流水线。
- **摄像头采集不是本仓库核心**：本项目**不实现也不管理摄像头设备**，只提供**接收流的接口**。输入源既可以是图像模拟输入（测试 / 调试），也可以是外部相机适配器（平台采集后端把帧灌入本框架）。
- **多路输入**：流水线天然支持多路流（如前 / 后 / 左 / 右四路行车记录仪透传），渲染布局对应支持多路排布。
- **事件与状态可模拟**：提供信号模拟输入节点，产出转向、刹车等各类事件，驱动 OSD 指示与事件录像。
- **可靠存储**：存储支持切换、内存缓存池兜底、磁盘不稳定 / 阻塞保护、超时停止、防抖恢复。
- **零拷贝优先**：Android 上编码器 input surface 反馈给上层作为渲染 surface，避免 CPU 回读。
- **单例全局上下文共享跨层状态**：跨节点的共享状态（encoder surface、信号当前状态、存储健康度等）不经过 Stream / 图 side packet，统一放在单例 `MediaCameraContext`，节点按需读写，简化连线与时序。

## 1.3 Goals

- 提供一套跨平台（Android / macOS / Linux）的多路摄像头记录仪框架骨架。
- 以 graph_runtime 为骨架，核心能力全部抽象为**可配置的图节点**：

  | 节点 | 能力来源 | 职责 |
  |------|----------|------|
  | `stream_input` | 本仓库（接收接口） | 接收外部注入的流（图像模拟 / 相机适配），统一为 `Packet<VideoFrame>` |
  | `signal_source` | 本仓库 | 模拟 / 转发信号事件：转向、刹车、挡位等 |
  | `multi_view_layout` | native_ui | 多路帧 → 多视口排布（2×2 / 画中画 / 单路切换） |
  | `ui_overlay` | native_ui | OSD 叠加：时间戳、速度、事件指示 |
  | `video_encoder` | video_codec `VideoEncoder` | H.264 / HEVC 编码；暴露 input surface 给上游渲染 |
  | `audio_encoder` | video_codec `AudioEncoder` | AAC 编码 |
  | `recorder` | video_codec + 本仓库 | 循环录像 / 事件录像 / 存储切换 / 缓存池 / 防抖 |
  | `muxer_sink` | video_codec `Muxer` / `Mp4MuxConsumer` | 封装 MP4 并落盘 |
  | `stream_sink` | video_codec 扩展 | RTMP / WebRTC 推流 |
  | `preview` | native_ui Surface / Render | 屏幕预览渲染 |

- 支持 **JSON 配置驱动**的流水线组装，业务不写胶水代码。
- 输出两个示例：`recorder_demo`（多路采集 → 布局 → OSD → 编码 → 可靠存储）与 `stream_demo`（推流及相关服务）。
- 保持三个库的完全解耦：本项目只依赖各自的公共 API（umbrella header）。

## 1.4 Non-Goals (Phase 1)

- **不实现摄像头设备管理与采集**：仅提供 `stream_input` 接收接口 + 图像模拟源；真实相机由外部适配器灌入。
- 不重复实现图调度、编码或渲染逻辑（全部复用三个库）。
- 不实现滤镜 / AI 前处理流水线（人脸、车道线检测，Phase 2，可复用 graph_runtime 节点）。
- 不实现录音混音 / 多音轨（Phase 2）。
- 不实现分布式 / 远程预览。
- 不实现 Windows / iOS 专属相机适配（接口预留，按平台 `select()` 扩展）。

---

# 2. Requirements (What)

## 2.1 Functional Requirements

### FR-001 Configurable Node Graph

流水线完全由 JSON 配置描述，格式与 graph_runtime 的 `GraphConfig` 对齐。核心 = 节点，组合方式 = 配置：

```json
{
  "graph": {
    "nodes": [
      { "name": "cam_front", "calculator": "StreamInputNode",     "options": { "source": "image:front.jpg",  "width": 1280, "height": 720, "fps": 30 } },
      { "name": "cam_rear",  "calculator": "StreamInputNode",     "options": { "source": "image:rear.jpg",   "width": 1280, "height": 720, "fps": 30 } },
      { "name": "cam_left",  "calculator": "StreamInputNode",     "options": { "source": "image:left.jpg",   "width": 1280, "height": 720, "fps": 30 } },
      { "name": "cam_right", "calculator": "StreamInputNode",     "options": { "source": "image:right.jpg",  "width": 1280, "height": 720, "fps": 30 } },
      { "name": "signals",   "calculator": "SignalSourceNode",    "options": { "simulate": true, "events": ["turn_left", "brake"] } },
      { "name": "layout",    "calculator": "MultiViewLayoutNode", "options": { "mode": "grid_2x2", "out_width": 2560, "out_height": 1440 } },
      { "name": "overlay",   "calculator": "UiOverlayNode",       "options": { "show_timestamp": true, "show_events": true } },
      { "name": "encoder",   "calculator": "VideoEncoderNode",    "options": { "codec": "H264", "bitrate": 8000000, "fps": 30, "input_surface": true } },
      { "name": "recorder",  "calculator": "RecorderNode",        "options": { "cache_pool_mb": 256, "storage": "sdcard", "timeout_ms": 5000, "debounce_ms": 10000, "loop_seconds": 60 } },
      { "name": "muxer",     "calculator": "MuxerSinkNode",       "options": { "path": "out/recording.mp4" } }
    ],
    "streams": [
      { "name": "f", "from": "cam_front", "to": "layout" },
      { "name": "r", "from": "cam_rear",  "to": "layout" },
      { "name": "l", "from": "cam_left",  "to": "layout" },
      { "name": "e", "from": "cam_right", "to": "layout" },
      { "name": "sig", "from": "signals",  "to": "overlay" },
      { "name": "view", "from": "layout",  "to": "overlay" },
      { "name": "osd",  "from": "overlay", "to": "encoder" },
      { "name": "es",   "from": "encoder", "to": "recorder" },
      { "name": "out",  "from": "recorder","to": "muxer" }
    ]
  }
}
```

### FR-002 Stream Input Interface（不实现摄像头，仅接收流）

- 本仓库只提供 `StreamInputNode` 接收接口，摄像头设备管理与采集**不在本项目实现**。
- 两种输入源：
  - **图像模拟输入**：静态图 / 图像序列 / 视频文件循环回放，用于测试与调试，默认提供。
  - **相机适配器输入**：外部平台采集后端（Android Camera2、AVCaptureDevice、V4L2 等）通过本接口把帧灌入，本项目仅定义数据契约（分辨率、帧率、格式、时间戳）。

### FR-003 Multi-Stream Input（多路输入）

- 流水线支持任意多路流同时注入（典型：行车记录仪前 / 后 / 左 / 右四路）。
- 每路流独立携带源标识（`camera_id` / `view_point`），下游据此路由到对应视口。
- 各路上游帧率 / 分辨率可以不一致，由下游布局节点统一对齐。

### FR-004 Multi-View Render Layout（多路排布）

- `MultiViewLayoutNode` 将多路输入帧排布为一个复合画面，支持：
  - `grid_2x2`（四路 2×2 网格）、`grid_1x2`、`pip`（画中画）、`single`（单路切换）。
  - 按摄像头视角预设视口映射：前 = 主画面，后 / 左 / 右 = 附属画面（DVR 典型布局）。
- 复用 native_ui：`Stack` + `Container`（Flex）组合视口，`State` 驱动布局切换。

### FR-005 Signal Simulation Input（信号模拟）

- `SignalSourceNode` 产生 / 转发事件信号：转向灯（左 / 右）、刹车、挡位、倒车、碰撞等。
- 两种模式：
  - **模拟模式**：按脚本 / 时间轴自动产生事件（测试用）。
  - **透传模式**：接收外部 CAN / OBD / 私有协议事件，转为统一事件包。
- 事件作为 `Packet<SignalEvent>` 分发给下游：
  - `UiOverlayNode` 绘制转向 / 刹车指示；
  - `RecorderNode` 触发事件录像（碰撞锁定）。

### FR-006 Timestamp Overlay（时间戳绘制）

- `UiOverlayNode` 必须支持在画面叠加时间戳（时钟 + 日期，可配置格式与位置）。
- 时间戳与事件指示共用一个 OSD 渲染通道，native_ui 绘制。

### FR-007 Encoder Surface Feedback（编码器 surface 反馈）

- 当编码器使用 input surface 零拷贝输入（Android `MediaCodec.createInputSurface`）时，**编码器节点必须把其 input surface 反馈给上一层**，作为上层渲染的 surface。
- **模式由配置决定**：`VideoEncoderNode` 配置 `input_surface: true/false` 声明是否启用零拷贝。`false`（默认）→ 渲染节点 Open 不等待，走 CPU 帧路径；`true` → 走下面 Open 握手。
- 即：`VideoEncoderNode` 把 `input_surface()` 发布到**单例 `MediaCameraContext`**，`MultiViewLayoutNode` / `UiOverlayNode` 将 native_ui `RenderContext` 建立在**该 surface 上**直接绘制，编码器消费绘制结果（闭环：布局 → OSD → 编码零拷贝）。
- 时序用 **Open 握手**：`VideoEncoderNode::Open()` 创建 encoder + surface 后写入 context 并 `notify()`；`UiOverlayNode::Open()` 经 `MediaCameraContext::WaitEncoderInputSurface()` 等待（未配置 input surface 时立即返回、不阻塞），拿到后再创建 `RenderContext`。
- 流程参考 native_ui `external_image_demo` 已验证闭环（PNG → AHardwareBuffer → widget → encoder surface → MediaCodec → MP4）。

### FR-008 Storage（存储切换 / 缓存池 / 保护策略）

`RecorderNode` 负责可靠录像，必须支持：

- **循环录像**：按 `loop_seconds` 分段；未发生事件时覆盖最旧分段。
- **事件录像**：收到 `SignalEvent` 时锁定前后窗口（事件前 n 秒 + 事件后 m 秒）为不可覆盖的分段。
- **内存缓存池**：编码包先写入有界内存池（`cache_pool_mb`），后台线程异步落盘；磁盘不可用时数据滞留内存，缓解瞬时抖动。
- **存储切换**：运行时可切换目标存储（sdcard / internal / usb），切换期间缓存池继续承接，落盘后台平滑迁移。
- **磁盘不稳定 / 阻塞保护**：落盘策略具备背压（背压上限、降级丢弃非关键分段、跳过校验）、写失败重试与告警；写超时超过 `timeout_ms` 则**停止录制**并上报，避免阻塞主链路。
- **防抖机制（debounce）**：磁盘恢复后不立即恢复录制，等待 `debounce_ms` 稳定期，防止存储反复抖动导致录/停抖动（flapping）。

### FR-009 Streaming & Services（推流与服务）

- `StreamSinkNode`：消费与存储相同的编码包流，支持 RTMP / WebRTC（Phase 1 实现至少一种，Phase 2 扩展）。
- 提供 `stream_demo` 示例：多路采集 → 布局 → OSD → 编码 → 推流，并演示服务化形态（后台常驻、断线重连、录制与推流并行）。

### FR-010 Public Library

本项目作为独立 Bazel Library 提供，三个库均为外部依赖：

```python
deps = [
    "@graph_runtime//src/framework/public:runtime",
    "@native_ui//native_ui:ui",
    "@video_codec//src/framework/public:video_codec",
]
```

### FR-011 Examples

- `recorder_demo`：四路图像模拟输入 → 2×2 布局 → OSD（时间戳 + 事件）→ H.264 → 循环 / 事件录像 → MP4。
- `stream_demo`：同上 → 推流服务。

## 2.2 Non-Functional Requirements

- **跨平台**：Android（API 29+）/ macOS / Linux，构建按平台 `select()`。
- **零拷贝优先**：Android 采集 → 渲染 → 编码链路走 AHardwareBuffer / encoder input surface 反馈，避免 CPU 回读。
- **低耦合**：只依赖三个库的公共 API，禁止依赖内部实现。
- **可扩展**：新节点 = 实现 `Node` 契约 + 注册到 `NodeFactoryRegistry`，不改 Runtime。
- **易测试**：图像模拟输入 + 信号模拟使整条链路可在无硬件环境下端到端测试。
- **稳定优先**：存储链路任何故障不得阻塞采集 / 编码主链路。

---

# 3. Design (How)

## 3.1 Overall Architecture

```
┌──────────────────────────────────────────────────────────────────────┐
│                 media_camera (Composition Layer — 全部为 Node)         │
│                                                                       │
│   JSON GraphConfig ──► GraphBuilder ──► GraphRuntime (scheduling)     │
│                                        │                              │
│                                        ▼                              │
│   stream_input(f) ──┐                                                 │
│   stream_input(r) ──┼─► multi_view_layout ─► ui_overlay ─► encoder   │
│   stream_input(l) ──┤        (2×2 / pip)     (timestamp/event OSD)    │
│   stream_input(e) ──┘                    ▲             │              │
│                                           │             │ input_surface│
│   signal_source ──(SignalEvent)───────────┘             ▼ feedback    │
│   (turn/brake)                       encoder ◄────── (render target)  │
│                                            │                          │
│                                            ▼                          │
│   recorder (cache pool / switch / debounce / timeout stop)            │
│      ├───────────────► muxer_sink ──► MP4 (storage)                   │
│      └───────────────► stream_sink ──► RTMP/WebRTC (service)          │
│                                            │                          │
│   multi_view_layout ──► preview (native_ui 屏幕预览)                  │
└──────────────────────────────────────────────────────────────────────┘
```

## 3.2 Node Catalog

| 节点 | 输入 | 输出 | 核心职责 |
|------|------|------|----------|
| `StreamInputNode` | 外部注入（图像/相机适配器） | `Packet<VideoFrame>` | 统一流接收接口，模拟 / 相机两种源 |
| `SignalSourceNode` | 脚本 / 外部 CAN 等 | `Packet<SignalEvent>` | 模拟或透传事件（转向 / 刹车 / 挡位 / 碰撞） |
| `MultiViewLayoutNode` | N 路 `Packet<VideoFrame>` | `Packet<VideoFrame>` | 多视口排布（grid / pip / single） |
| `UiOverlayNode` | 画面 + `Packet<SignalEvent>` | `Packet<VideoFrame>` / surface | 时间戳、事件指示、速度等 OSD |
| `VideoEncoderNode` | 帧 / input surface | `Packet<VideoPacket>` | 编码；暴露 `input_surface()` 给上游 |
| `AudioEncoderNode` | `Packet<AudioFrame>` | `Packet<AudioPacket>` | AAC 编码 |
| `RecorderNode` | 编码包 + 事件 | 分段数据 | 循环 / 事件录像、缓存池、存储切换、防抖 |
| `MuxerSinkNode` | 分段数据 | 文件 | MP4 封装落盘 |
| `StreamSinkNode` | 编码包 | 网络 | RTMP / WebRTC 推流服务 |
| `PreviewNode` | 复合画面 | 屏幕 | native_ui 预览渲染 |

## 3.3 多路输入与多路排布

- 每路输入以独立 Stream 进入 `MultiViewLayoutNode`，节点依据 `stream` 名 / 帧携带的 `view_point` 元数据路由到对应视口。
- 视口模型（对齐 DVR 语义）：

```
grid_2x2（默认）        pip（画中画）
┌─────┬─────┐          ┌─────────────────┐
│ FRONT│ REAR │         │      FRONT      │
├─────┼─────┤          │   ┌────┐        │
│ LEFT │ RIGHT│         │   │REAR│  PIP   │
└─────┴─────┘          └───┴────┴────────┘
```

- 布局配置即视口映射表：`{ "FRONT": "main", "REAR": "grid", "LEFT": "grid", "RIGHT": "grid" }`，可运行时通过 `State` 切换（倒车自动切 REAR 为主画面等）。

## 3.4 信号与事件流

```
signal_source ── Packet<SignalEvent> ──► ui_overlay    （绘制转向 / 刹车指示）
                │
                └─────────────────────► recorder       （触发事件录像 / 碰撞锁定）
```

`SignalEvent` 载荷：`type`（turn_left / turn_right / brake / gear / collision ...）、`timestamp`、`payload`。事件流是旁路流，不参与画面主链，避免时序耦合。

## 3.5 存储链路（可靠录制）

```
encoder ── Packet<VideoPacket> ──► RecorderNode ──► [内存缓存池 ring] ──(异步)──► 分段落盘
                                        │                  │
                                        │            (磁盘故障/阻塞/超时)
                                        ▼                  ▼
                                   存储切换           停止录制 + 上报
                                   debounce 恢复 ◄──────────┘
```

关键机制：

1. **内存缓存池**：有界环形缓冲（`cache_pool_mb`），编码包先入池，落盘线程异步消费；磁盘瞬时抖动时数据滞留池中。
2. **背压与保护**：池满 → 降级（丢弃最旧非关键分段）；写失败 → 重试 + 告警；写超时超 `timeout_ms` → 停止录制，绝不让存储问题阻塞采集 / 编码。
3. **存储切换**：目标存储列表 + 当前选择；切换时后台把待写分段平滑迁移到新存储，期间录制不中断。
4. **防抖（debounce）**：磁盘恢复后等待 `debounce_ms` 稳定期才恢复录制，防止反复抖动的录/停振荡。
5. **循环 / 事件窗口**：事件锁定的前后窗口分段标记 `protected`，循环覆盖跳过。

## 3.6 编码器 surface 反馈（零拷贝闭环）

### 3.6.1 单例 MediaCameraContext

跨层共享状态统一放在**单例 `MediaCameraContext`**（不经过 Stream / 图 side packet）：

```cpp
class MediaCameraContext {          // 进程内单例
 public:
  static MediaCameraContext& Instance();

  // input surface 模式：由组合层启动时按配置设置（encoder.input_surface）
  void SetInputSurfaceMode(bool enabled);      // 组合层调用
  bool IsInputSurfaceMode() const;

  // encoder surface：codec 发布，render 订阅
  void SetEncoderInputSurface(void* surface);          // VideoEncoderNode 调用
  void* WaitEncoderInputSurface(std::chrono::milliseconds timeout); // render 阻塞等待
  void NotifyEncoderInputSurface();                    // surface 就绪通知

  // 其他共享状态：信号当前状态 / 存储健康度 / 录制状态 / 统一帧时钟
  SignalState& signal_state();
  StorageStatus& storage_status();
  Clock& frame_clock();
};
```

- 读端拿到的都是**最新值**（latest-value），节点间不直接引用。
- 跨线程安全：写方更新 + `notify()`（条件变量），读方阻塞等待或订阅回调。
- **是否等待由配置决定，不是平台决定**：组合层启动时读取 `VideoEncoderNode` 配置中的 `input_surface` 字段调用 `SetInputSurfaceMode()`。未配置 input surface（CPU 帧路径）时，`WaitEncoderInputSurface()` **立即返回 `nullptr`**，渲染节点不进入等待。

### 3.6.2 Open 握手时序

```
组合层启动：mode = config.encoder.input_surface → ctx.SetInputSurfaceMode(mode)

调度器 Open 阶段（配置序：encoder 先于 overlay）：
  VideoEncoderNode::Open()  ──► mode ? VideoEncoder::CreateInputSurface() : (不创建)
                                │
                                ├─► ctx.SetEncoderInputSurface(surface)   // CPU 模式为 nullptr
                                └─► ctx.NotifyEncoderInputSurface()
  UiOverlayNode::Open()      ──► surface = ctx.WaitEncoderInputSurface(timeout)
                                │          ├─ mode=false → 立即返回 nullptr（不阻塞）
                                │          └─ mode=true  → 阻塞等到通知（超时则失败上报）
                                └─► surface ? RenderContext(surface)   // 零拷贝渲染
                                           : CPU 帧渲染路径
```

- **配置驱动**：`encoder.input_surface: false`（默认）→ 渲染节点 Open 不等 surface，直接 CPU 帧路径；`true` → 等待 encoder 就绪后把 `RenderContext` 建在其 input surface 上。
- **顺序保证**：JSON 配置中 `encoder` 节点必须排在渲染节点之前，调度器按配置序 Open。
- **超时保护**：`WaitEncoderInputSurface` 带超时（默认对齐编码器初始化上限），超时返回空 → 渲染节点降级为 CPU 帧路径并告警，避免 Open 死等。
- host（macOS / Linux）配置 `input_surface: true` 但平台无该能力：`VideoEncoder::CreateInputSurface()` 返回 `nullptr`，`SetEncoderInputSurface(nullptr)` + 立即 `NotifyEncoderInputSurface()`，渲染节点同样走 CPU 帧路径（FFmpeg 后端），行为一致。

```
ui_overlay ◄──(RenderContext on)── ctx.encoder_input_surface()
    │                                        ▲
    └── draw() ──────────────────────────────┤ (系统送帧给编码器，零拷贝)
```

## 3.7 推流与服务

- `stream_demo` 部署形态：后台常驻服务，`StreamSinkNode` 建立 RTMP / WebRTC 会话，断线自动重连，带宽自适应降级；录制与推流并行（同一编码包流分发到 `recorder` 与 `stream_sink`）。

## 3.8 Directory Structure

与 graph_runtime / native_ui / video_codec 的仓库布局对齐：源码根为仓库同名内层目录（`media_camera/media_camera/`），`doc/` 位于其下，`specs/` 位于仓库根：

```
media_camera/                         (repo root)
├── AGENTS.md
├── CHANGELOG.md
├── specs/                            (spec-kit 规格目录，001-xxx)
│   └── 001-xxx/
│       ├── plan.md / spec.md / tasks.md ...
│       ├── contracts/
│       ├── checklists/
│       └── research.md / data-model.md / quickstart.md
└── media_camera/                     (源码根，workspace(name = "media_camera"))
    ├── WORKSPACE
    ├── BUILD.bazel                   (root alias: //:camera → //src/public:camera)
    ├── .bazelversion                 (6.5.0)
    ├── .bazelrc
    ├── media_camera_deps.bzl         (外部依赖 bootstrap：graph_runtime / native_ui / video_codec)
    ├── platforms/
    │   ├── BUILD
    │   └── platforms.bzl             (config_setting + platform + select 宏)
    ├── src/
    │   ├── public/
    │   │   ├── BUILD                 (汇总 target，strip_include_prefix)
    │   │   └── include/
    │   │       └── media_camera/
    │   │           ├── media_camera.h (umbrella header)
    │   │           ├── media_camera_export.h
    │   │           └── node.h        (节点基类与公共节点工厂)
    │   ├── nodes/
    │   │   ├── stream_input/         (接收接口 + 图像模拟源)
    │   │   ├── signal_source/        (信号模拟 / 透传)
    │   │   ├── multi_view_layout/    (native_ui 多视口排布)
    │   │   ├── ui_overlay/           (native_ui OSD：时间戳 / 事件指示)
    │   │   ├── encoder/              (video_codec 封装 + surface 反馈)
    │   │   ├── recorder/             (缓存池 / 存储切换 / 防抖 / 超时停止)
    │   │   ├── muxer_sink/           (MP4 落盘)
    │   │   ├── stream_sink/          (推流服务)
    │   │   └── preview/              (native_ui 预览)
    │   ├── examples/
    │   │   ├── recorder_demo.cc      (端到端 MVP：四路 → 布局 → OSD → 编码 → 录像)
    │   │   ├── stream_demo.cc        (推流及服务)
    │   │   └── configs/
    │   │       ├── recorder.json
    │   │       └── stream.json
    │   └── tests/
    ├── mk/                           (AOSP 风格 make 模块，参考 native_ui/mk)
    ├── scripts/
    │   └── verify/
    └── doc/
        ├── project_bootstrap.md      (本文档)
        ├── architecture/             (模块依赖、线程模型、生命周期、错误处理)
        └── api/                      (各节点接口契约)
```

## 3.9 Composition Mapping

### 3.9.1 graph_runtime 的使用

- 直接消费公共 API `graph_runtime/graph_runtime.h`。
- `Node` 基类：每个节点实现 `OpenProcessClose` 生命周期（`NodeContract`）。
- `Packet` / `Stream`：帧与码流以 `Packet` 在节点间流动；事件走旁路流。
- `NodeFactoryRegistry`：节点通过自注册模式加入（`REGISTER_NODE("StreamInputNode", ...)`），配置中仅以字符串引用。
- `Scheduler`：默认线程池调度；`Start()` 异步模式支持运行时注入 / 关闭输入流。

### 3.9.2 native_ui 的使用

- 消费公共 API：`native_ui/widgets.h`、`native_ui/render.h`、`native_ui/surface.h`、`native_ui/state.h`。
- `MultiViewLayoutNode` / `UiOverlayNode`：构建 Widget 树（`Stack` / `Container` / `Text`）→ 在 `RenderContext` 上绘制。
- Android 零拷贝：`RenderContext`（EGL）建在 encoder input surface 上——surface 来自 `MediaCameraContext::WaitEncoderInputSurface()`；`Surface::CreateFromBuffer` / `Image::FromBuffer` + AHardwareBuffer（对齐 native_ui `external_image_demo` 已验证闭环）。
- `PreviewNode`：创建 platform surface + widget 树，帧循环渲染预览。

### 3.9.3 video_codec 的使用

- 消费公共 API `video_codec/video_codec.h`。
- `VideoEncoderNode`：`VideoEncoder::Create(VideoConfig{...})`，平台自动选后端（MediaCodec / FFmpeg）；`CreateInputSurface()` 获得 input surface 供上层渲染。
- `MuxerSinkNode`：复用 `Mp4MuxConsumer`（`PacketConsumer`），Annex-B → AVCC，写入标准 MP4。
- 音频：`AudioEncoder::Create(AudioConfig{...})`，AAC，后续接入混音节点。
- 帧格式：上游以 video_codec `VideoFrame` 载荷与下游衔接（graph_runtime `Packet<VideoFrame>`）。

## 3.10 Packet / Frame 数据模型

```
stream_input ── Packet<VideoFrame> ──► multi_view_layout ──► ui_overlay ──► video_encoder
                                        ▲                                        │
   signal_source ── Packet<SignalEvent> │ (事件旁路流)                            │
                                        │                                    Packet<VideoPacket>
                                      preview ◄───────────────────────────────► recorder ─► muxer/stream_sink
```

- **VideoFrame**：来自 video_codec `types.h`，携带像素格式、尺寸、stride、时间戳、`view_point`。
- **VideoPacket**：来自 video_codec，携带 Annex-B 码流、PTS、关键帧标记。
- **SignalEvent**：本仓库定义，携带事件类型、时间戳、payload。
- graph_runtime `Packet` 作为传输容器，node 间不直接引用。

## 3.11 Threading Model

- graph_runtime `Scheduler` 决定节点执行线程；多路输入可并行调度。
- native_ui 渲染约定主线程渲染 + 工作线程逻辑（对齐 native_ui `doc/architecture/threading.md`）。
- `RecorderNode` 落盘线程独立于采集 / 编码主链路，保证存储故障不阻塞。
- 事件流 `SignalSourceNode` 独立调度，旁路注入，不阻塞画面主链。
- **Open 握手**：`UiOverlayNode::Open()` 通过 `MediaCameraContext::WaitEncoderInputSurface(timeout)` 阻塞等待 encoder surface，`VideoEncoderNode::Open()` 发布后 `notify()`；配置须保证 encoder 先于渲染节点 Open，等待带超时防死锁。

## 3.12 Extension Points

- **新输入源**：实现 `StreamInputNode` 的源接口（图像模拟 / 相机适配器 / 网络流）。
- **新信号源**：`SignalSourceNode` 透传模式对接 CAN / OBD / 私有协议。
- **新布局模式**：`MultiViewLayoutNode` 注册新视口排布（如 1+3 环视）。
- **新推流协议**：`StreamSinkNode` 注册新传输后端。
- **新存储策略**：`RecorderNode` 可插拔策略（缓存、防抖、切换、保护）。

---

# 4. MVP Deliverables

一期完成后应具备：

- media_camera 组合层 Library（节点库 + 汇总 public target）。
- JSON 配置驱动：`recorder.json` / `stream.json` 描述记录仪与推流流水线。
- 基础节点：`StreamInputNode`（含图像模拟源）、`SignalSourceNode`、`MultiViewLayoutNode`、`UiOverlayNode`（时间戳 / 事件）、`VideoEncoderNode`（含 surface 反馈）、`RecorderNode`（缓存池 / 切换 / 防抖 / 超时停止）、`MuxerSinkNode`、`StreamSinkNode`（一种协议）、`PreviewNode`。
- 端到端示例 `recorder_demo`：四路模拟输入 → 2×2 布局 → OSD → H.264 → 循环 / 事件录像 → MP4。
- 端到端示例 `stream_demo`：同上 → 推流服务。
- 单元测试（节点级）+ 集成验证（`make verify`）。
- Developer Documentation（`doc/architecture` + `doc/api`）。

---

# 5. Success Criteria

一期完成时，应满足以下目标：

- 通过 JSON 配置即可组装多路记录仪 / 推流流水线，无需修改 C++ 代码。
- 四路图像模拟输入能在 Android 与 macOS / Linux 上产出可播放的多路合成 MP4。
- 时间戳与转向 / 刹车事件指示正确出现在成片中。
- 事件录像正确锁定前后窗口；模拟磁盘故障时触发缓存池兜底、超时停止与防抖恢复。
- Android 编码走 encoder input surface 反馈零拷贝闭环，host 回退 CPU 帧路径且行为一致。
- 推流服务示例可独立运行并与录制并行。
- media_camera 只依赖三个库的公共 umbrella header，无内部实现耦合。
- 摄像头设备管理不进入本仓库（仅接收流接口 + 模拟源）。

---

# 6. Code Style & Commit Convention

## 6.1 Google C++ Coding Style

本项目遵循 Google C++ Style Guide，与 graph_runtime / native_ui / video_codec 保持一致。

### Naming

| Category        | Style              | Example              |
|-----------------|--------------------|----------------------|
| File names      | `lowercase`        | `recorder_node.cc`   |
| Type/Class      | `PascalCase`       | `class RecorderNode` |
| Function        | `PascalCase`       | `void Open()`        |
| Variable        | `snake_case`       | `int stream_index`   |
| Member variable | `snake_case_`      | `int stream_index_`  |
| Constant        | `kPascalCase`      | `const int kCachePoolMb` |
| Namespace       | `snake_case`       | `namespace media::camera` |
| Macro           | `UPPER_SNAKE_CASE` | `MEDIA_CAMERA_API`   |

### Formatting

- Indentation: 2 spaces (no tabs)
- Line length: 80 characters
- Prefer `std::unique_ptr` over raw pointers; avoid `std::shared_ptr` unless ownership is shared
- Include order: related header, C++ stdlib, third-party, project headers

## 6.2 Commit Convention

采用 [Conventional Commits](https://www.conventionalcommits.org/) 规范：

```
<type>(<scope>): <description>
```

Types: `feat` / `fix` / `docs` / `style` / `refactor` / `perf` / `test` / `build` / `ci` / `chore`

Scopes: `nodes` / `stream_input` / `signal_source` / `multi_view` / `ui_overlay` / `encoder` / `recorder` / `sink` / `public` / `config` / `example` / `docs` / `build`

### Examples

```
feat(nodes): add stream input node with image simulation source
feat(recorder): add cache pool, storage switch and debounce
feat(encoder): feedback encoder input surface to render layer
feat(example): add recorder_demo multi-camera pipeline
docs(bootstrap): update project bootstrap
```

## 6.3 Public API Export Macro

参考三个库的导出宏模式（`GRAPH_RUNTIME_API` / `NATIVE_UI_API` / `VIDEO_CODEC_API`），本项目使用 `MEDIA_CAMERA_API` 控制符号可见性。

### Public Header Layout

```
src/public/include/media_camera/
├── media_camera.h             (umbrella header)
├── media_camera_export.h      (export macro)
└── node.h                     (节点基类与公共节点工厂)
```

外部消费者只需 `#include "media_camera/media_camera.h"`。

---

# 7. Build System (Bazel 6.5)

## 7.1 依赖声明

三个仓库以 Bazel 外部依赖形式引入，统一在 `media_camera_deps.bzl` 中管理（参考各库自身的 `*_deps.bzl`）：

```python
# media_camera_deps.bzl
def media_camera_deps():
    if not native.existing_rule("graph_runtime"):
        http_archive(name = "graph_runtime", ...)
    if not native.existing_rule("native_ui"):
        http_archive(name = "native_ui", ...)
    if not native.existing_rule("video_codec"):
        http_archive(name = "video_codec", ...)
```

## 7.2 平台配置

参考三个库的 `platforms/` 结构，提供 `config_setting_and_platform` + `select()`：

- `macos_arm64` / `macos_x86_64`
- `linux_x86_64` / `linux_aarch64`
- `android_arm64`（NDK toolchain，参考 native_ui `--config=android_arm64`）

## 7.3 常用命令

```bash
bazel build //src/public:camera            # 组合层 Library
bazel build //src/examples:recorder_demo   # 记录仪示例
bazel build //src/examples:stream_demo     # 推流服务示例
bazel test //...                           # 全部测试
make verify                                # 分类验证（参考 native_ui / video_codec 的 mk 机制）
```

## 7.4 与三个仓库的构建约定对齐

- 所有 BUILD.bazel `deps` 使用外部仓库前缀：`@graph_runtime//`、`@native_ui//`、`@video_codec//`。
- 只依赖公共 target（umbrella header），禁止直接引用内部模块。
- 汇总 target 使用 `alwayslink = 1` 与 `strip_include_prefix`。
- 平台相关依赖使用 `select()`，宿主构建对 Android-only 目标提供 stub（对齐 native_ui `external_image_demo` 的宿主守卫模式）。

---

# 8. Reference Repositories

| 仓库 | 公共入口 | 关键参考文档 |
|------|----------|--------------|
| graph_runtime | `graph_runtime/graph_runtime.h` | `graph_runtime/docs/project_bootstrap.md`、`specs/002-scheduler-stream-packet/` |
| native_ui | `native_ui/native_ui.h`（core/event/layout/render/state/surface/widgets） | `native_ui/doc/architecture/README.md`、`specs/011-ahwb-external-image/`（零拷贝闭环示例 `external_image_demo`） |
| video_codec | `video_codec/video_codec.h` | `video_codec/codec/doc/project_bootstrap.md`、`specs/005-muxer-encoder-layering/`、`specs/006-android-mediacodec-backend/`、`specs/007-input-surface-api/` |
