# Implementation Plan: Dashcam 管线 Android 适配

**Created**: 2026-08-19

## 概览

按平台分支把 Dashcam 渲染 + 编码管线适配到 Android：渲染器提供双渲染目标（host CPU PixelBuffer / Android encoder input surface），背景图复用现有 `DrawImage`（GPU 后端上传），编码走 MediaCodec backend + CreateInputSurface（`ExternalImage` 保持 CPU/仅 SetBuffer）。host（CPU RGBA）路径保持不变。

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

### Phase 2: media_record — 渲染器平台分支（渲染目标抽象）

**定位**：Phase 2 交付**渲染层的平台分支能力**——`DashcamRenderer` 提供两种渲染目标（CPU PixelBuffer / GPU input-surface），供 Phase 3 的节点平台分支使用。Phase 3 的 `DashcamRenderNode` surface 分支调用 `DashcamRenderer::Render(frame_index, ts, RenderContext*)`，因此 Phase 2 必须先于 Phase 3。

- [x] **native_ui 前置**：导出 `render_context.h` 到 public umbrella（新增 `include/native_ui/render_context.h` 转发头）——media_record 需访问 `RenderContext`（`SwapBuffers`）以交付编码器。
- [x] `DashcamRenderer` 新增 Android surface 渲染路径 `Render(frame_index, ts, RenderContext*)`（`#ifdef __ANDROID__`）：
  - host：保持 `Surface::CreateFromPixels(buffer)` CPU 路径。
  - Android：`Surface::Create(ctx)`（FBO 0，encoder input surface）+ `Surface::Flush()` + `ctx->SwapBuffers()` 交付编码器；小狗/时间戳逻辑复用现有实现。
- [x] **逻辑复用**：抽取 `DrawFrame(canvas, ...)` 共享场景绘制（背景 + 小狗 + 时间戳），CPU/Android 两条路径仅"获得 canvas 方式"不同，其余逻辑完全一致（背景经现有 `DrawImage` 绘制）。

**验收**：同一渲染内容，host 走 CPU，Android 走 surface，逻辑复用；host 编译无回归。渲染器层能力就绪，供 Phase 3 节点接线调用。

### Phase 3: media_record — LifecycleContext + 节点接线

- [x] 定义 `LifecycleContext`（`pipeline_failed` + `input_surface`）+ `PacketNotify` 通用通知类型，放 `src/framework/lifecycle/`（header-only target `//src/framework/lifecycle:lifecycle`）。runner（dashcam_record.cc）持有并经 `SetInputSidePacket("lifecycle_ctx", MakePacket<LifecycleContext*>(&ctx))` 注入；host 上 `pipeline_failed` 从独立 `bool*` 侧边包迁移为 `LifecycleContext` 字段（行为不变）。
- [x] `VideoEncoderNode` 平台分支：构造函数读 `input_surface`/`width`/`height`；Android surface 模式 `Open` 调 `EnsureSurfaceEncoder`（`cfg.input_surface=true`，创建 encoder，`CreateInputSurface()` 写入 `LifecycleContext::input_surface`）；`Process` 收 `PacketNotify` → `encoder_->Poll()`。host 走原 CPU RGBA→I420→Encode 路径。GetContract 输入改 `SetAny()`（VideoFrame / PacketNotify）。
- [x] `DashcamRenderNode` 平台分支：Android surface 模式 `Open` 不分配 CPU buffer；首次 `Process` 调 `EnsureSurfaceRenderer`（读 `input_surface` → `RenderContext::CreateFromNativeWindow` + `DashcamRenderer`），每帧 `Render(frame_index, ts, render_ctx_)` 渲染到 input surface 并输出 `PacketNotify`。host 走原 CPU PixelBuffer 路径。GetContract 输出改 `SetAny()`。
- [x] `MuxerSinkNode::Close` 改读 `LifecycleContext::pipeline_failed`（迁移自独立 `bool*` 侧边包，行为不变）。
- [x] 每帧：绘制 → `Surface::Flush()` + `ctx->SwapBuffers()` → encoder `Poll()`（封装在 renderer `Render(ctx)` 与 encoder `Process`）。
- [x] `input_surface` 为 null（非 surface 模式 / 创建失败）：`EnsureSurfaceRenderer` 返回可定位错误，不绘制空指针。

**验收**：DashcamRenderNode 在 Android surface 模式下通过 `LifecycleContext` 拿到 encoder input surface，渲染到该 surface（GPU，无 CPU VideoFrame 流转），encoder `Poll` 编码；host 路径无回归，`pipeline_failed` 语义不变。

### Phase 4: dashcam_record.cc 改造为多端统一入口 + 验证

**方向**（2026-08-19 确认）：不在 `dashcam_record.cc` 之外另建独立 Android demo，而是**改造 `dashcam_record.cc` 为多端统一入口**，按平台分支：

- host（默认）：现有 CPU 渲染路径 + encoder（软件编码）→ MP4，行为不变。
- Android（`__ANDROID__` / `--surface`）：走 surface 模式——`LifecycleContext` 注入（`pipeline_failed` + `input_surface`）→ `VideoEncoderNode::Open` 写 `input_surface` → `DashcamRenderNode` 首次 `Process` lazy 获取 → `RenderContext` 渲染到 encoder input surface → MediaCodec 编码 → MP4。

实施项：
- [x] `dashcam_record.cc` 基础改造（Phase 3 已完成）：定义并注入 `LifecycleContext`（`SetInputSidePacket(kSidePacketTag, ...)`），`pipeline_failed` 从独立 `bool*` 侧边包迁移为 `LifecycleContext` 字段（行为不变）。Android 的 `input_surface` 由 encoder 节点 Open 写入、render 节点惰性读取（节点平台分支已在 Phase 3 实现）。
- [x] **config 从外部传入**：`dashcam_record.cc` 不按平台硬编码默认 config——统一默认 host config（`dashcam_record.json`），`--config=FILE` 显式传入（Android 端显式传 android config）。
- [x] **Android /data/local 测试路径 config** `dashcam_record_android.json`：encoder 加 `input_surface=true` + `width`/`height`，render 加 `input_surface=true`（输出 PacketNotify 而非 VideoFrame）；asset/output 路径指向 `/data/local/tmp/media_record/`（设备测试布局，脚本 push 保持该结构）。
- [x] **Android 构建配置**：`.bazelrc` `--config=android_arm64`（rules_android_ndk toolchain + `//platforms:android_arm64_platform`，与 native_ui/video_codec 对齐）；`WORKSPACE` 补齐 NDK 注册（`rules_android_ndk` http_archive + `android_ndk_repository(name = "androidndk")`，镜像 video_codec 的 WORKSPACE，NDK 路径取自 `ANDROID_NDK_HOME`，host 构建不受影响）。
- [x] **Android 一键验证 target**（`mk/android.mk` + `scripts/verify/android_dashcam.sh`，对齐 video_codec 的 android 模块）：`android-build`（跨编译）/ `android-push`（push 二进制 + 图片 + android config 到 `/data/local/tmp/media_record/`）/ `android-run`（设备上跑 surface 模式）/ `android-verify`（+ pull MP4 到 `out/` + ffprobe/decode 校验）。
- [ ] 实测验证：host 上 `make verify` 全绿（原路径无回归）；连接 Android 设备/模拟器跑 `make android-verify` 验证 surface 路径产出 H.264 MP4。

**验收**：同一 `dashcam_record.cc` 入口——host 跑 CPU 路径产出 MP4（无回归），Android 跑 surface 路径经 MediaCodec 产出可播放 H.264 MP4。多端统一入口，不另建 demo。

## 风险缓解

- **节点协作顺序**：`DashcamRenderNode` 依赖 `VideoEncoderNode` 的 input surface，但 render 先 Open。解法：通过 `LifecycleContext`（`SetInputSidePacket` 注入指针）承载 `input_surface` 字段，encoder `Open` 写、render **首次 `Process`** lazy 读（调度器保证"Open 全部先于源节点 Process"，见 contracts D.3）。
- **GL 上下文单上下文规则**：render 是唯一 GL 绘制者，其 `RenderContext` 由 render 在首次 Process 构造（`CreateFromNativeWindow` + `Surface::Create(ctx)`）；encoder 只消费 surface 缓冲、不直接绘制。
- **host 无法验证 Android**：Android 路径用设备/模拟器验证；host 仅保证编译 + 原路径回归。

## 交付物

- 代码改动：media_record 渲染/节点分支 + `dashcam_record.cc` 多端统一入口（host CPU / Android surface），native_ui 仅导出 `render_context.h` 转发头。
- 多端验证记录：host `make verify` 全绿 + Android 设备/模拟器产出 H.264 MP4。
