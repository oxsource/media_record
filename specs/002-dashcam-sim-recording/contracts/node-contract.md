# Contract: 7 类节点实现

**Branch**: `002-dashcam-sim-recording` | **Date**: 2026-08-18 | **Spec**: [spec.md](../spec.md)

## 1. 范围

recorder.json 引用的 7 类节点实现为**可运行的真实实现**（替换 001 占位骨架）；`AudioEncoder` / `StreamSink` / `Preview` 保持骨架不变。

## 2. 通用规则

| # | 规则 |
|---|------|
| N-1 | 每个节点实现 `graph::runtime::Node`（`Open(GraphContext&)` / `Process(GraphContext&)` / `Close(GraphContext&)` + `GetContract(NodeContract*)`），经 `GRAPH_RUNTIME_REGISTER_NODE("NodeType", Class)` 注册到 graph_runtime 的 `NodeFactoryRegistry`，type 名与 dashcam_record.json / recorder.json 完全一致（对齐 graph_runtime `src/examples/*` 的节点写法） |
| N-2 | 节点经 `PipelineRunner`（`src/framework/runner/`）编排：只通过 `ctx.Inputs().Get(port)` / `ctx.Outputs().Get(port)` 读写自己声明的端口（`"port:stream"` 中冒号前的 port 名），不做跨节点直接调用 |
| N-3 | 帧数据统一 `graph::runtime::Packet` 传输（`Packet::MakePacket<T>()`，`Timestamp` 语义）；视频帧载荷为 `video::codec::VideoFrame`（RGBA） |
| N-4 | 失败必须在错误信息中**命名节点 + 具体原因**（FR-008 可定位错误），返回非 `absl::OkStatus`（source 结束返回 `StatusStop()`） |
| N-5 | source 节点（无输入端口：StreamInput / SignalSource）由驱动器**每帧调用一次** `Process`，产出数据或返回 `StatusStop()` 表明结束 |

## 3. 节点契约

### 3.1 StreamInputNode

| 项 | 值 |
|----|-----|
| output | `output:frames` → `Packet<video::codec::VideoFrame>`（RGBA） |
| options（程序化注入 `NodeDef.options`） | `image`（默认 `assets/dashcam_default.png`）、`width`/`height`（默认跟随图片）、`fps`（30）、`frame_count`（默认 300 = 10s×30fps） |
| 行为 | `Open` 解码图片并校验；每 `Process` 产出一帧（复制图片 + 打真实时钟时间戳 + pts），达 `frame_count` 后返回 `StatusStop()` |
| 失败 | 图片缺失 / 格式不支持 → 非 OK 状态，报错含路径 |

### 3.2 SignalSourceNode

| 项 | 值 |
|----|-----|
| output | `output:signals` → `Packet<SignalEvent>` |
| options | 无（纯生成器；`kTick` 事件带真实时钟 `timestamp_us`） |
| 行为 | 每 `Process` 产出一个最小 `SignalEvent`（`kTick`），旁路给 OSD；达帧数（与 StreamInput 相同的 `frame_count`，经 options 注入）后返回 `StatusStop()` |
| 失败 | 无（纯生成器） |

### 3.3 MultiViewLayoutNode

| 项 | 值 |
|----|-----|
| input | `f:frames`（本期单路，f/r 预留） |
| output | `output:view_frames` → `Packet<VideoFrame>`（RGBA） |
| 行为 | 构建 native_ui flex 树（`Container` + `ExternalImage`，铺满整帧），`Layout(w,h)` 后读子组件 bounds → 将输入帧软件 blit 进自有 RGBA 帧缓冲（尺寸=配置分辨率）；不实现多路拼接 |
| 失败 | 输入缺失 → 非 OK 状态，报错命名节点 |

### 3.4 UiOverlayNode

| 项 | 值 |
|----|-----|
| input | `video:view_frames`、`signal:signals` |
| output | `output:osd_frames` → `Packet<VideoFrame>`（RGBA） |
| options | `format`（默认 `%Y-%m-%d %H:%M:%S`）、`position`（默认右下角） |
| 行为 | 以 native_ui flex 布局（`Container` + `Text`）计算时间戳位置，用软件位图字体在帧上绘制真实时钟时间戳（每帧取 `system_clock`）；时间戳叠加于画面固定角落（图片铺满整帧为底层）；事件输入本期不渲染（扩展点，消耗即丢弃） |
| 失败 | 输入缺失 → 非 OK 状态，报错命名节点 |

### 3.5 VideoEncoderNode

| 项 | 值 |
|----|-----|
| input | `input:osd_frames` |
| output | `output:es_packets` → `Packet<VideoPacket>`（Annex-B） |
| options | `fps`（30）、`bitrate`（默认 4Mbps）、`width`/`height` |
| 行为 | 软件 RGBA→I420（media_record 内置）→ `CodecFactory::CreateVideo`（H.264, I420, 30fps）→ `Init` → 逐帧 pull 模式 `Encode` → 输入结束时 `Flush` |
| 失败 | 编码器不可用 / 编码失败 → 非 OK 状态，命名节点 |

### 3.6 RecorderNode

| 项 | 值 |
|----|-----|
| input | `input:es_packets` |
| output | `output:clips` → `Packet<VideoPacket>`（透传） |
| options | `duration_seconds`（10）、`fps`（30） |
| 行为 | 会话生命周期（单会话单分段）：帧计数达标后触发 finalize；转发包给 muxer |
| 失败 | 会话异常 → 非 OK 状态 + 命名节点 |

### 3.7 MuxerSinkNode

| 项 | 值 |
|----|-----|
| input | `input:clips` → `Packet<VideoPacket>` |
| options | `output`（默认 `out/dashcam.mp4`） |
| 行为 | 用 video_codec 公共面 `Muxer`（`CodecFactory::CreateMuxer`，`MuxFormat::kMp4`）写 MP4：`SetOutput(FileByteSink)` 写临时文件 → 逐包 `Push` → `Finish` 写 trailer → 原子 rename；覆盖旧文件并提示 |
| 失败 | 输出目录不可写 / muxer 失败 → 非 OK 状态，报错含路径 + 删除临时文件 + 退出非零（FR-009） |

## 4. 验收

- `bazel build //...` 全部节点目标编译链接通过（7 类节点为 `graph::runtime::Node`，经 graph_runtime `NodeFactoryRegistry` 可 `CreateByName(type, name, options)`）。
- 默认配置录制产物为可播放 MP4，含 `ftyp` / `moov` / `mdat`（端到端测试断言）。
- 任一失败场景返回非零并给出含节点名/路径的可定位错误。
