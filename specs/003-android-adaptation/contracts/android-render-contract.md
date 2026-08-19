# Contract: Android 渲染 + 编码协作契约

**Created**: 2026-08-19

## 1. 目标

定义 Android 平台下，media_record 的渲染器（DashcamRenderer）与编码器（video_codec MediaCodec backend）之间如何通过 `CreateInputSurface` 协作，以及 `ExternalImage` 的职责边界（保持 CPU、仅 SetBuffer）。

## 2. ExternalImage 契约（native_ui，保持不变）

### 2.1 结论

**GPU 不是 ExternalImage 的关注点**。`ExternalImage` 仅支持 `SetBuffer(HardwareBuffer)`（及构造传 `HardwareBuffer`）创建，保持 **CPU（raster）加载显示**，不引入 `RenderContext`/`RenderBackend` 依赖。GPU 零拷贝导入已由 **Surface 抽象** 支持（`Surface::Create(ctx)` + `Image::FromBuffer(buffer, kGPU, ctx)` + `RenderContext`），并在 Android surface 模式下由 `DashcamRenderer` 处理。

**因此 native_ui 的 `ExternalImage` 无需任何改动**（无 Phase 1 改动）。

### 2.2 职责划分

- **A-1（ExternalImage 职责）**: 仅 `SetBuffer(HardwareBuffer)` 创建，CPU 加载显示，作为布局/显示 widget。不新增 GPU 接口。
- **A-2（GPU 归属）**: GPU 零拷贝导入由 `Surface` 抽象承担（`Surface::Create(ctx)`、`RenderContext`、`Image::FromBuffer(kGPU)`），由 `DashcamRenderer` 在 Android surface 模式调用。
- **A-3（保持）**: `ExternalImage::UpdateBuffer` 固定 `RenderBackend::kCPU`，行为不变，无回归。

## 3. VideoEncoder surface 契约（video_codec，已存在，引用确认）

## 3. VideoEncoder surface 契约（video_codec，已存在，引用确认）

- **B-1**: `VideoConfig.input_surface=true` 声明 surface 模式；该模式下不得调用 `Encode(VideoFrame)`。
- **B-2**: `Init()` 成功后在 surface 模式下经 `AMediaCodec_createInputSurface` 创建 input surface。
- **B-3**: `CreateInputSurface()` 返回 `ANativeWindow*`（`void*`），由 encoder 拥有，调用方不得 release；`Release()` 后返回 nullptr。
- **B-4**: `Poll()` 在 surface 模式下泵出编码输出；渲染每帧后应调用一次。

## 4. 节点协作契约（media_record）

### 4.1 渲染器平台分支

- **C-1**: `DashcamRenderer` 提供两个渲染入口：
  - host（默认）：`Render(int frame_index, const std::string& timestamp, native::ui::PixelBuffer& buffer)` —— `Surface::CreateFromPixels` CPU 路径，行为不变。
  - Android：`Render(int frame_index, const std::string& timestamp, native::ui::RenderContext* ctx)` —— 在 encoder input surface 上绘制（`Surface::Create(ctx)`）。
- **C-2**: 小狗跳动（`UpdateDogBounce`）与时间戳绘制逻辑两端复用，仅渲染目标不同。
- **C-3**: 渲染尺寸按 encoder native stride 对齐（16/256）。

### 4.2 节点协作

#### D.1 依赖事实：DashcamRenderNode 创建依赖 VideoEncoderNode

`DashcamRenderNode` 的渲染器在 Android surface 模式下，其 `RenderContext`/`Surface` 必须在 `VideoEncoderNode` 提供的 `CreateInputSurface()`（`ANativeWindow*`）上创建。因此 **render 节点的渲染目标创建依赖 encoder 节点的 input surface**。

graph_runtime 的调度模型决定了该依赖的现实约束（见 specs/002 contracts/dependency-contract.md D-6）：

- **Open 顺序 = config.nodes 声明顺序**。dashcam_record.json 中顺序为 `render → encoder → recorder → muxer`，故 `DashcamRenderNode::Open` 先于 `VideoEncoderNode::Open`。
- `CreateInputSurface()` 在 encoder 的 `Open`（内部 `Init()` 调 `AMediaCodec_createInputSurface`）时才有效。**因此 render 节点无法在自身 `Open` 阶段获取 input surface**（此时 encoder 尚未 Open）。

#### D.2 共享机制：全局 `LifecycleContext` 结构体 + `SetInputSidePacket`

参考 MediaPipe 对"跨 GPU 计算器共享 GL 上下文"的做法（graph service，见 research.md §7），并结合现有 `pipeline_failed` 侧边包机制，采用 **全局 `LifecycleContext` 结构体 + `SetInputSidePacket`**：

**关键洞察**：graph_runtime 的 input side packet 承载的是**指针而非值**（现有 `pipeline_failed` 即传 `bool*`）。因此结构体指针可在 Open 前注入，其**字段**可在运行时被各节点读写——这天然规避了"render 先 Open、拿不到 encoder surface"的时序问题（注入的是可变的指针，不是 Open 前就必须就绪的值）。

```cpp
// 跨节点共享的图级运行上下文（graph 生命周期持有，dashcam_record.cc 局部定义）
struct LifecycleContext {
  bool pipeline_failed = false;   // 首错中止标记（复用现有 FR-009 机制）
  void* input_surface = nullptr;  // VideoEncoderNode::Open 写入（ANativeWindow*）
  // 可扩展：native::ui::RenderContext* shared_render_context; 共享 GL 上下文等
};

// 注入（graph Start 前，与现有 pipeline_failed 一致）：
//   LifecycleContext lifecycle_ctx;
//   runtime.SetInputSidePacket("lifecycle_ctx",
//       graph::runtime::Packet::MakePacket<LifecycleContext*>(&lifecycle_ctx));
```

选型理由：

| 机制 | 是否适合 | 原因 |
|------|----------|------|
| 裸全局单例 | △ 兜底 | 进程级耦合隐式，不符合 graph 范式 |
| input side packet 传**值** | ✗ | Open 前绑定值，无法承载 Open 后才创建的 surface |
| **input side packet 传 `LifecycleContext*`（指针）** | ✅ 推荐 | 指针 Open 前注入，字段运行时读写；与现有 `pipeline_failed`（`bool*`）机制完全一致；所有跨节点共享状态收敛到一个结构体 |

**机制要点**（对齐 MediaPipe graph service 精神 + 复用现有 side packet 范式）：
- 共享上下文所有权属 **graph 生命周期**（由 runner 持有 `LifecycleContext`），节点只读写字段。
- `VideoEncoderNode::Open` 创建 input surface 后写 `ctx_->input_surface`。
- `DashcamRenderNode` **首次 `Process`**（而非 `Open`）读取 `ctx_->input_surface`，构造共享 `RenderContext` + `DashcamRenderer`。
- `MuxerSinkNode::Close` 读 `ctx_->pipeline_failed`（现有逻辑迁移到新结构体）。

```cpp
// 节点内读取（各节点通过 input side packet 拿到同一 LifecycleContext*）
LifecycleContext* ctx = nullptr;
auto p = graph_input.InputSidePackets().Get("lifecycle_ctx");
if (!p.IsEmpty()) {
  auto v = p.Get<LifecycleContext*>();
  if (v.ok() && v.value()) ctx = v.value();
}
```

- **D-1**: `dashcam_record.cc`（runner）定义 `LifecycleContext lifecycle_ctx`，经 `SetInputSidePacket("lifecycle_ctx", MakePacket<LifecycleContext*>(&lifecycle_ctx))` 注入。
- **D-2**: `VideoEncoderNode::Open` 时创建 input surface（内部 `Init()` 调 `AMediaCodec_createInputSurface`），成功后写 `ctx_->input_surface = CreateInputSurface()`（host / 非 surface 模式保持 nullptr）。
- **D-3**: `DashcamRenderNode` 在**首次 `Process`**（而非 `Open`）时 **lazy 获取** `ctx_->input_surface`，据此构造 `RenderContext`（`CreateFromNativeWindow` + `Surface::Create(ctx)`）+ `DashcamRenderer`；render 是唯一 GL 绘制者（单上下文规则），encoder 只消费 surface 缓冲、不直接绘制；`input_surface` 为 null 则返回可定位错误。
- **D-4**: `MuxerSinkNode::Close` 读 `ctx_->pipeline_failed`（由 `bool*` 侧边包迁移到 `LifecycleContext*` 字段，行为不变）。
- **D-5**: `VideoEncoderNode` 在 surface 模式 `Open` 调 `CreateInputSurface()` 并把结果写 `ctx_->input_surface`（surface 模式返回 `ANativeWindow*`，否则 nullptr）。

> **与纯 input side packet（传值）的边界**：若未来 graph_runtime 支持"节点 output side packet 跨节点传播"，可将 `input_surface` 由 encoder 的 **output side packet** 声明式发布、render 以 **input side packet** 声明依赖，替代"共享指针字段"。本期用 `LifecycleContext*` 指针字段（复用现有范式，最小改动）。

#### D.3 全局同步保证（why it is safe without a lock）

依赖 `graph_runtime` 的**同步执行模型**，读写 `LifecycleContext` 字段不需要跨线程加锁：

- `Schedule()`（或 async `Start` 的 Open 阶段）**先 Open 全部节点，再进入源节点 Process 循环**（scheduler.cc：Open 循环在 Process 循环之前）。
- 因此 `DashcamRenderNode` 首次 `Process` 时，`VideoEncoderNode::Open` 必然已经完成，`ctx_->input_surface` 必然已写入 → 读取一定非空。
- **写入（encoder Open 时写 `input_surface`）与读取（render 首次 Process 时读）发生在同一执行阶段的不同节点**，由调度器保证 happens-before，无需 mutex。
- 未来若进入多线程/并发 Process（async 多 executor），`LifecycleContext` 需加轻量锁或将共享字段改为 `std::atomic`；本期同步模式单写单读。

#### D.4 GPU 数据流（surface 模式下节点间不传 CPU VideoFrame）

**关键澄清**（2026-08-19）：一旦使用 MediaCodec 作 backend（`input_surface=true`），render→encoder 之间**不再是 CPU 数据流转**。render 节点直接渲染到 encoder 的 input surface（GPU，经 `Surface::Create(ctx)` 零拷贝），再发一个轻量 `PacketNotify`（通用通知，时间戳）给 encoder，encoder 收到后 `Poll()` 泵出编码输出。参考 `video_codec` examples 的 surface 路径（`encode_file.cc`）。

因此 graph 拓扑中 render→encoder 的 `frames` 流在 surface 模式下携带的是 `PacketNotify`，而非 `VideoFrame`；节点输入/输出端口用 `SetAny()` 声明（运行时按 `surface_mode_` 分支）。

#### D.5 每帧渲染交付流程

- **D-6**: 每帧流程（surface 模式）：render 在 encoder input surface 上 Canvas 绘制（背景 + 小狗 + 时间戳）→ `Surface::Flush()` + `ctx->SwapBuffers()`（交付编码器）→ render 发 `PacketNotify` → encoder `Poll()` 泵输出。

#### D.6 初始化顺序兜底

- **D-7**: 若共享 `RenderContext` 获取失败（`ctx_->input_surface` 为 null / 非 surface 模式 / 创建失败），render 节点 MUST 返回可定位错误，不得绘制到空指针。
- **D-8**: render 节点的 renderer 创建**从 `Open` 移到首次 `Process`**（惰性），以规避 Open 顺序依赖；host CPU 路径不受影响（`Open` 无需 encoder）。

## 5. 失败与清理

- **E-1**: AHWB 分配 / Surface GPU 导入 / RenderContext 创建失败：返回可定位错误，安全释放已分配资源，不产生残缺产物。
- **E-2**: encoder 不支持 surface 模式（`CreateInputSurface` 返回 nullptr）：render 节点不得绘制到 nullptr，应报错或回退。
- **E-3**: host 上调用 Android-only 接口：返回 nullptr / 退出非零，不崩溃。

## 6. 验证

- **V-1**: host `make verify` 全绿（原 CPU 路径无回归）。
- **V-2**: Android 端到端 demo 产出可播放 H.264 MP4。
- **V-3**: `ExternalImage` 保持原样（仅 SetBuffer/CPU）无回归；GPU 导入在 Android demo 验证。
