# Feature Specification: Dashcam 管线 Android 适配（Android Adaptation）

**Feature Branch**: `003-android-adaptation`

**Created**: 2026-08-19

**Status**: Draft

**Input**: User description: "接下来是该考虑这套流程在 android 平台适配了：1. 背景图使用 ahwb 加载并使用 ExternalImage 加载显示；2. 编码器使用 mediacodec 作为 backend，同时要注意这次 render 的 surface 创建依赖于 video_encoder 提供的 CreateInputSurface"

## Clarifications

### Session 2026-08-19

- Q: 这套适配涉及三个仓库（native_ui / video_codec / media_record），按什么方式推进？ → A: 先写设计文档/spec，评审后再实现。
- Q: native_ui `ExternalImage` 当前硬编码 kCPU（无 GPU 零拷贝路径），如何支持 Android surface 模式？ → A: 见下条最终澄清。
- Q: `DashcamRenderer` 当前是纯 CPU RGBA（`Surface::CreateFromPixels` 零拷贝到外部 PixelBuffer），Android surface 模式如何处理双路径？ → A: 按平台分支——Android 采用新的创建范式（在 encoder 的 `CreateInputSurface` 上创建 RenderContext + Surface），否则保留默认 `Surface::CreateFromPixels`。
- Q: GPU 零拷贝导入应归属哪一层？ → A: **GPU 不是 ExternalImage 的关注点**——`ExternalImage` 仅支持 `SetBuffer(HardwareBuffer)` 创建（CPU 加载），GPU 导入已由 **Surface 抽象** 支持（`Surface::Create(ctx)` + `Image::FromBuffer(kGPU)` + `RenderContext`）。背景图 GPU 导入由 `DashcamRenderer` 在 Android surface 模式处理（基于 Surface/RenderContext），`ExternalImage` 保持原样。

## User Scenarios & Testing *(mandatory)*

### User Story 1 - 背景图经 AHWB + ExternalImage 显示（Priority: P1）

在 Android 上，Dashcam 背景图不再通过 CPU 解码 → RGBA 拷贝路径，而是：解码为 RGBA → 写入 `AHardwareBuffer`（`AHwb::AllocateRGBA` + `WriteRGBA`）→ surface 模式下由 `DashcamRenderer` 基于 **Surface 抽象**（`Surface::Create(ctx)` + `Image::FromBuffer(kGPU)`）经 `AHwb::ToGpuImage` 零拷贝纹理导入，避免 CPU 回读。`ExternalImage` widget 仅用于布局/预览（`SetBuffer` CPU 加载），不参与 GPU 导入。

**Why this priority**: 这是 Android 适配的基础——背景图作为画面底层，通过硬件缓冲 + 零拷贝 GPU 导入是 surface 编码路径的核心前提。

**Independent Test**: 在 Android 设备/模拟器上运行适配后的渲染 demo，检查背景图正确铺满画面，且无 CPU 回读拷贝。

**Acceptance Scenarios**:

1. **Given** 背景图解码成功，**When** 写入 AHardwareBuffer 并由 ExternalImage 加载显示，**Then** 画面显示完整背景图，尺寸正确
2. **Given** Android surface 模式（GPU），**When** DashcamRenderer 基于 Surface/RenderContext 渲染一帧，**Then** 背景图经 `ToGpuImage` 零拷贝导入，无逐帧 CPU 拷贝
3. **Given** CPU 模式（host），**When** 渲染一帧，**Then** 保持原 `Surface::CreateFromPixels` 路径，行为与之前一致

---

### User Story 2 - 编码器使用 MediaCodec backend + CreateInputSurface（Priority: P1）

Android 上视频编码不再用 host 的软件编码路径，而是使用 `video_codec` 的 MediaCodec backend。渲染直接在 encoder 提供的 `CreateInputSurface()`（`ANativeWindow*`）上绘制：`DashcamRenderNode` 从 `VideoEncoderNode` 获取 input surface → `native::ui::RenderContext::CreateFromNativeWindow` → `Surface::Create(ctx)` → Canvas 绘制 → `eglSwapBuffers` 交付给 encoder（零拷贝）。

**Why this priority**: 这是 Android 适配的核心——利用硬件编码器 + 硬件输入 surface，实现真正零拷贝的渲染→编码链路，是移动端性能的关键。

**Independent Test**: 在 Android 设备上运行端到端录制 demo，检查输出 MP4 可播放、内容正确，编码由 MediaCodec 完成。

**Acceptance Scenarios**:

1. **Given** encoder 配置 `input_surface=true`，**When** `VideoEncoder::Init()`，**Then** 经 `AMediaCodec_createInputSurface` 创建 input surface，`CreateInputSurface()` 返回 `ANativeWindow*`
2. **Given** `DashcamRenderNode` 运行，**When** 渲染每帧，**Then** 在 encoder input surface 上绘制并 `SwapBuffers` 交付，`Poll()` 泵出编码输出
3. **Given** 录制结束，**When** 检查产物，**Then** 输出可播放 MP4，编码为 H.264，帧内容正确（背景 + 跳动小狗 + 时间戳）

---

### User Story 3 - 双平台路径共存（Priority: P2）

host 构建（macOS/Linux）保持现有 CPU RGBA 路径不变；Android 构建启用 surface 路径。二者通过平台分支选择，host 上不引入 Android-only 依赖，保证 CI 可编译可测。

**Why this priority**: 保证现有 host 管线与验证不受影响，同时为 Android 提供新路径，二者隔离、互不破坏。

**Independent Test**: host 上运行 `make verify` 全绿；Android 上运行 Android demo 通过。

**Acceptance Scenarios**:

1. **Given** host 构建，**When** 运行 `make verify`，**Then** 全部通过，行为与适配前一致
2. **Given** Android 构建，**When** 运行录制 demo，**Then** 走 surface 路径成功编码
3. **Given** 任一平台渲染器创建失败，**When** 运行，**Then** 输出可定位错误，退出非零

---

### Edge Cases

- AHardwareBuffer 分配失败 / 格式不支持（非 R8G8B8A8_UNORM）：给出可定位错误。
- MediaCodec 不支持 surface 模式（`input_surface` 创建失败）：`CreateInputSurface` 返回 nullptr，渲染器回退或报错。
- 编码器未提供 input surface（非 surface 模式）：渲染器不得调用 `CreateInputSurface`。
- surface 生命周期：`CreateInputSurface` 返回的 handle 由 encoder 拥有，渲染器不得 release（契约 C-004）。
- 分辨率对齐：硬件 AVC 编码器要求 16/256 对齐，渲染尺寸需按 encoder native stride 对齐。
- host 上调用 Android-only 接口：返回 nullptr / 退出非零，不得崩溃。

## Requirements *(mandatory)*

### Functional Requirements

- **FR-001**: Android 上背景图 MUST 通过 AHardwareBuffer（`AHwb::AllocateRGBA` + `WriteRGBA`）加载，并由 `ExternalImage` widget 显示。
- **FR-002**: `ExternalImage` MUST 保持仅 `SetBuffer(HardwareBuffer)` 创建（CPU 加载显示），不引入 GPU/RenderContext 依赖。GPU 零拷贝导入由 **Surface 抽象** 支持（`Surface::Create(ctx)` + `Image::FromBuffer(kGPU)` + `RenderContext`），并在 Android surface 模式下由 `DashcamRenderer` 处理，`ExternalImage` 本身不关注 GPU。
- **FR-003**: Android 上视频编码 MUST 使用 `video_codec` 的 MediaCodec backend，配置 `input_surface=true`。
- **FR-004**: 渲染器 MUST 在 encoder 提供的 `CreateInputSurface()`（`ANativeWindow*`）上绘制：经 `RenderContext::CreateFromNativeWindow` + `Surface::Create(ctx)` 创建画布，绘制后 `SwapBuffers` 交付、`Poll()` 泵出编码输出。
- **FR-005**: `DashcamRenderer` 按平台分支：Android 使用新的 surface 创建范式，否则保留默认 `Surface::CreateFromPixels` CPU 路径。
- **FR-006**: 渲染尺寸 MUST 对齐硬件编码器 native stride（16/256 对齐）。
- **FR-007**: host 构建 MUST 保持现有 CPU RGBA 路径与 `make verify` 全部通过，不引入 Android-only 破坏性依赖。
- **FR-008**: 任何平台上的资源创建失败（AHWB / MediaCodec surface / RenderContext）MUST 给出可定位错误并安全清理，不产生残缺产物。
- **FR-009**: `DashcamRenderNode` 的渲染目标创建依赖 `VideoEncoderNode` 的 `CreateInputSurface`，MUST 通过**全局 `LifecycleContext` 结构体 + `SetInputSidePacket`**（runner 注入 `LifecycleContext*`，`VideoEncoderNode::Open` 写 `input_surface` 字段、`DashcamRenderNode` 首次 `Process` 惰性读取）实现，并依赖 graph_runtime"Open 全部先于 Process"的同步模型保证时序，避免锁竞争（详见 contracts §4.2）。

### Key Entities *(include if feature involves data)*

- **AHardwareBuffer (AHWB)**: Android 硬件缓冲，承载背景图 RGBA，支持 GPU 零拷贝纹理导入。
- **ExternalImage**: native_ui widget，加载并显示 AHWB 图像（`SetBuffer` CPU 加载），仅用于布局/预览，不参与 GPU 导入。
- **RenderContext**: native_ui EGL/GLES 上下文包，host on encoder input surface。
- **CreateInputSurface**: `VideoEncoder` 提供的硬件输入 surface（`ANativeWindow*`），零拷贝交付编码。
- **Input Surface 渲染目标**: 渲染器绘制目标，Android 上为 encoder input surface（FBO 0）。

## Success Criteria *(mandatory)*

### Measurable Outcomes

- **SC-001**: Android 上背景图经 AHWB + Surface GPU 零拷贝导入显示，无逐帧 CPU 拷贝。
- **SC-002**: Android 上端到端录制产出可播放 H.264 MP4，画面含背景 + 跳动小狗 + 时间戳。
- **SC-003**: host 上 `make verify` 全部通过，行为与适配前一致。
- **SC-004**: `ExternalImage` 保持仅 `SetBuffer`（CPU）不变，无回归；GPU 导入由 Surface 层提供。
- **SC-005**: 任一失败场景（AHWB / MediaCodec / RenderContext 创建失败）返回可定位错误，安全退出。

## Assumptions

- 三仓库已具备 Android 底层能力：native_ui 已有 `AHwb`/`RenderContext`/`Surface::Create(ctx)`，video_codec 已有 MediaCodec backend 与 `CreateInputSurface()`/`Poll()`/`input_surface` 契约。本 feature 聚焦 media_record 节点接线（DashcamRenderer 平台分支 + encoder surface 协作），`ExternalImage` 无需改动。
- Android 适配以新增 Android demo/入口验证为主，host 端通过 `make verify` 保证无回归。
- surface 模式与 CPU 模式互斥：`VideoConfig.input_surface` 声明 surface 模式后，不得再调用 `Encode(VideoFrame)`。
- 背景图分辨率与硬件编码器对齐要求：按 encoder native stride 对齐（16/256）。
- 时间戳 / 小狗动画绘制逻辑与 host 一致，仅渲染目标不同（Android 直接画到 input surface，host 画到 PixelBuffer）。
- 本 feature 不涉及音频、推流、预览等；聚焦视频渲染 + 编码的 Android 硬件路径。
- Android 上背景图的 GPU 零拷贝导入为本次目标（经 Surface 抽象）；若个别设备不支持 GPU 导入，可回退 CPU 路径（功能正确优先，性能优化次之）。
