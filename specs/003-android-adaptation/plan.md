# Implementation Plan: Dashcam 管线 Android 适配

**Created**: 2026-08-19

## 概览

按平台分支把 Dashcam 渲染 + 编码管线适配到 Android：背景图走 AHWB + Surface GPU 零拷贝导入（ExternalImage 保持 CPU/仅 SetBuffer），编码走 MediaCodec backend + CreateInputSurface。host（CPU RGBA）路径保持不变。

涉及仓库与改动粒度：

| 仓库 | 改动 | 目标 |
|------|------|------|
| native_ui | 无改动（`ExternalImage` 保持 CPU/仅 SetBuffer；GPU 已由 Surface 抽象支持） | 背景图零拷贝 GPU 显示（经 Surface） |
| media_record | `DashcamRenderer`/`DashcamRenderNode`/`VideoEncoderNode` 平台分支 + Android demo | Android 端到端硬件路径 |

（video_codec 的 MediaCodec backend + `CreateInputSurface` 已实现，无需改动，仅需确认契约对接。）

## 实施阶段

### Phase 0: 前置确认（不改代码）

- [ ] 确认 `video_codec` MediaCodec backend 的 `CreateInputSurface`/`Poll`/`input_surface` 契约与 public 导出（`@video_codec//src/framework/public:video_codec`）。
- [ ] 确认 native_ui 的 `RenderContext`/`Surface::Create(ctx)`/`Image::FromBuffer(kGPU)` 在 public umbrella 中的导出与 Android Bazel 配置（`--config=android_arm64`）。
- [ ] **确认 graph_runtime 节点依赖与同步模型**：验证（1）Open 顺序 = config.nodes 声明顺序（render 先于 encoder）；（2）`Schedule`/async 中 **所有节点 Open 完成后才进入源节点 Process 循环**。这是"全局同步机制"可行的前提（render 首次 Process 时 encoder 必已 Open）。

### Phase 0.5: 节点依赖与 LifecycleContext 共享机制设计

**依赖事实**：`DashcamRenderNode`（源节点，先 Open）的渲染目标依赖 `VideoEncoderNode`（后 Open）的 `CreateInputSurface`，因此 render 不能在 `Open` 阶段获取 surface（encoder 尚未 Open），需在**首次 `Process`** lazy 获取。

**机制**（详见 `contracts/android-render-contract.md` §4.2；参考 MediaPipe GPU 图服务 research.md §7）：
- **全局 `LifecycleContext` 结构体 + `SetInputSidePacket`（推荐）**：把所有跨节点共享状态（`pipeline_failed` + `input_surface`，可扩展共享 `RenderContext`）收敛到一个结构体，runner 经 `SetInputSidePacket("lifecycle_ctx", MakePacket<LifecycleContext*>(&ctx))` 以**指针**注入。节点 Open 前即可拿到指针，字段在运行时可读写——规避"Open 前值就绪"限制。与现有 `pipeline_failed`（`bool*`）范式一致。
- **字段读写**：`VideoEncoderNode::Open` 写 `ctx->input_surface`；`DashcamRenderNode` 首次 `Process` 读 `ctx->input_surface` 构造共享 `RenderContext`；`MuxerSinkNode::Close` 读 `ctx->pipeline_failed`（迁移自独立 `bool*` 侧边包）。
- **惰性创建**：render 的 renderer 从 `Open` 移到**首次 `Process`**，此时 encoder 已 Open（调度器保证 happens-before）。
- **同步保证**：单写（encoder Open 写 `input_surface`）单读（render 首次 Process 读），由调度模型保证顺序，无需 mutex；未来多线程场景改为 `std::atomic` 字段。
- **升级方向**：若未来 graph_runtime 支持"节点 output→input side packet 传播"，`input_surface` 可由 encoder 以 output side packet 声明式发布、render 以 input side packet 声明依赖，替代共享指针字段。

### Phase 1: native_ui — 确认 GPU 由 Surface 承担（ExternalImage 无改动）

- [x] 澄清：GPU 不是 ExternalImage 的关注点——`ExternalImage` 仅 `SetBuffer(HardwareBuffer)` 创建（CPU 加载），不新增 GPU 接口（已回滚误加的 `SetRenderContext`）。
- [x] 确认 GPU 零拷贝导入已由 **Surface 抽象** 支持（`Surface::Create(ctx)` + `Image::FromBuffer(kGPU)` + `RenderContext`）。
- [ ] native_ui 无代码改动，host 编译 + 测试通过（无回归）。

**验收**：`ExternalImage` 保持原样（仅 SetBuffer/CPU），无回归；GPU 导入能力由 Surface 层提供。

### Phase 2: media_record — 渲染器平台分支

- [x] **native_ui 前置**：导出 `render_context.h` 到 public umbrella（新增 `include/native_ui/render_context.h` 转发头）——media_record 需访问 `RenderContext`（`SwapBuffers`）以交付编码器。
- [x] `DashcamRenderer` 新增 Android surface 渲染路径 `Render(frame_index, ts, RenderContext*)`（`#ifdef __ANDROID__`）：
  - host：保持 `Surface::CreateFromPixels(buffer)` CPU 路径。
  - Android：`Surface::Create(ctx)`（FBO 0，encoder input surface）+ `Surface::Flush()` + `ctx->SwapBuffers()` 交付编码器；小狗/时间戳逻辑复用现有实现。
- [ ] 背景 GPU 零拷贝导入（Phase 2b，需导出 `ahwb.h`）：背景写入 AHWB → `Surface::CreateFromBuffer(hb, kGPU, ctx)` 经 `ToGpuImage` 零拷贝导入。当前用 GPU 后端上传 CPU 背景图（正确、非零拷贝），零拷贝作为后续优化。
- [ ] 按 encoder native stride 对齐渲染尺寸（16/256，在节点/runner 层配置）。

**验收**：同一渲染内容，host 走 CPU，Android 走 surface，逻辑复用；host 编译无回归。

### Phase 3: media_record — LifecycleContext + 节点接线

- [ ] 定义 `LifecycleContext` 结构体（`pipeline_failed` + `input_surface`，可扩展共享 `RenderContext`），runner（dashcam_record.cc）持有并经 `SetInputSidePacket("lifecycle_ctx", MakePacket<LifecycleContext*>(&ctx))` 注入。
- [ ] `VideoEncoderNode` 暴露 `CreateInputSurface()`（surface 模式下返回 `ANativeWindow*`，否则 nullptr）；`Open` 成功创建 input surface 后写 `ctx_->input_surface`。
- [ ] `DashcamRenderNode` 改为**首次 `Process` 惰性创建 renderer**：读 `ctx_->input_surface` → `RenderContext::CreateFromNativeWindow` + `Surface::Create(ctx)` 建渲染目标 → 创建 `DashcamRenderer`（Android surface 分支）。
- [ ] `MuxerSinkNode::Close` 改读 `ctx_->pipeline_failed`（迁移自独立 `bool*` 侧边包，行为不变）。
- [ ] 每帧：绘制 → 共享 `RenderContext` 的 `gr->flush()` → `SwapBuffers()` → encoder `Poll()`。
- [ ] 若 `ctx_->input_surface` 为 null（非 surface 模式 / 创建失败），返回可定位错误（不绘制空指针）。

**验收**：DashcamRenderNode 在 Android surface 模式下通过 `LifecycleContext` 拿到 encoder input surface，渲染到该 surface，编码输出正确；host 路径无回归，`pipeline_failed` 语义不变。

### Phase 4: Android demo + 验证

- [ ] 新增 Android 端到端 demo（仿 `external_image_demo`）：PNG 背景 → AHWB → ExternalImage → encoder input surface 渲染 → MediaCodec 编码 → MP4。
- [ ] 新增/复用 Android 构建配置与脚本（`--config=android_arm64`）。
- [ ] host 上 `make verify` 全绿（原路径无回归）。

**验收**：Android 设备/模拟器产出可播放 H.264 MP4，host 验证全绿。

## 风险缓解

- **节点协作顺序**：`DashcamRenderNode` 依赖 `VideoEncoderNode` 的 input surface，但 render 先 Open。解法：通过 `LifecycleContext`（`SetInputSidePacket` 注入指针）承载 `input_surface` 字段，encoder `Open` 写、render **首次 `Process`** lazy 读（调度器保证"Open 全部先于源节点 Process"，见 contracts D.3）。
- **GL 上下文单上下文规则**：render 是唯一 GL 绘制者，其 `RenderContext` 由 render 在首次 Process 构造（`CreateFromNativeWindow` + `Surface::Create(ctx)`）；encoder 只消费 surface 缓冲、不直接绘制。
- **host 无法验证 Android**：Android 路径用设备/模拟器验证；host 仅保证编译 + 原路径回归。

## 交付物

- 代码改动：media_record 渲染/节点分支 + Android demo（native_ui 无改动）。
- Android 端到端 demo + host `make verify` 验证记录。
