# Research: Dashcam 管线 Android 适配

**Created**: 2026-08-19

## 1. 目标

将 media_record 的 Dashcam 渲染 + 编码管线适配到 Android：

1. 背景图使用 AHardwareBuffer (AHWB) 加载，并由 native_ui `ExternalImage` widget 显示。
2. 编码器使用 `video_codec` 的 MediaCodec backend，渲染的 surface 创建依赖于 encoder 提供的 `CreateInputSurface`。

## 2. 三仓库底层能力盘点

调研结论：**三仓库的 Android 底层能力已基本齐备**，本 feature 主要工作是 media_record 节点接线 + native_ui `ExternalImage` GPU 扩展。

### 2.1 native_ui

| 模块 | 现状 | 复用点 |
|------|------|--------|
| `AHwb`（`src/framework/surface/ahwb.h`） | Android-only；host stub 返回负状态码 | `AllocateRGBA` / `WriteRGBA` / `ToGpuImage`（零拷贝 GPU 导入）/ `ToCpuImage` / `Describe` / `Lock`/`Unlock` |
| `Image::FromBuffer(HardwareBuffer, RenderBackend, RenderContext*)` | Android 加载入口；CPU→`ToCpuImage(copy=true)`，GPU→`ToGpuImage(ctx->gr)`；仅 R8G8B8A8_UNORM | 背景图 AHWB 加载 |
| `ExternalImage`（`src/framework/widgets/external_image.cc`） | **硬编码 `RenderBackend::kCPU`** | **缺口：需扩展支持 GPU（RenderBackend::kGPU + RenderContext）** |
| `RenderContext`（`src/framework/surface/render_context.h`） | `CreateFromNativeWindow(surface, w, h)`；`MakeCurrent`/`SwapBuffers`；持有 GrDirectContext + EGL | 在 encoder input surface 上建 EGL 上下文 |
| `Surface::Create(RenderContext*)` | Android：从 encoder input surface（FBO 0）创建 render target | 渲染目标 |
| `external_image_demo.cc` | **完整参考闭环**：PNG→RGBA→AHWB→ExternalImage→canvas(on input surface)→SwapBuffers→编码 | 端到端蓝本 |

### 2.2 video_codec

| 模块 | 现状 | 复用点 |
|------|------|--------|
| `VideoEncoder`（`src/framework/api/video_encoder.h`） | `CreateInputSurface()`（返回 `ANativeWindow*`）、`Poll()`、`Encode(const NativeBuffer&)` | 已预留 surface 接口 |
| `VideoConfig.input_surface` | 声明 surface 模式（与 `Encode(VideoFrame)` 互斥） | 模式切换 |
| `MediaCodecVideoEncoder`（`src/framework/backend/android/mediacodec_video.cc`） | **已实现 `CreateInputSurface()`**：`AMediaCodec_createInputSurface`，surface 模式配置 `COLOR_FormatSurface`；`Poll()` 泵输出 | 已完全可用 |
| `mediacodec_muxer` | Android MP4 mux | MP4 封装 |

### 2.3 media_record

| 模块 | 现状 | 改动 |
|------|------|------|
| `DashcamRenderer`（`src/render/dashcam_renderer.cc`） | 纯 CPU RGBA：`Surface::CreateFromPixels(buffer)` 零拷贝绘制到外部 PixelBuffer | 需支持平台分支：Android 在 encoder input surface 上绘制 |
| `DashcamRenderNode`（`src/nodes/dashcam_render/`） | 自驱动，输出 RGBA `VideoFrame` | 需与 encoder 交互获取 `CreateInputSurface`；Android 改为 surface 输出 |
| `VideoEncoderNode`（`src/nodes/video_encoder/`） | 调用 `video_codec` encoder，输入 RGBA→I420 | Android 需暴露 `CreateInputSurface` 给 render 节点 |

## 3. 关键技术链路

### 3.1 背景图 AHWB + ExternalImage

```
背景图 PNG --Image::FromFile--> RGBA --AHwb::AllocateRGBA + WriteRGBA--> AHardwareBuffer
  --> HardwareBuffer::FromAHardwareBuffer --> ExternalImage(hb) [GPU: Image::FromBuffer(hb, kGPU, ctx)]
```

### 3.2 MediaCodec surface 渲染链路

```
VideoEncoder(config.input_surface=true)
  --> Init() 内 AMediaCodec_createInputSurface
  --> CreateInputSurface() 返回 ANativeWindow*
  --> RenderContext::CreateFromNativeWindow(win, w, h)   // EGL + GrDirectContext
  --> Surface::Create(ctx)                                // FBO 0 render target
  --> Canvas draw (背景 ExternalImage + 小狗 + 时间戳)
  --> ctx->gr->flush() + ctx->SwapBuffers()              // 交付编码器
  --> encoder->Poll()                                     // 泵出编码输出
```

## 4. 关键缺口与决策

### 4.1 `ExternalImage` GPU 扩展（native_ui 改动）

当前 `ExternalImage::UpdateBuffer` 硬编码 `RenderBackend::kCPU`：

```cpp
image_ = native::ui::Image::FromBuffer(buffer_, RenderBackend::kCPU);
```

**决策**：扩展 `ExternalImage` 支持 GPU。方案：新增构造参数 / setter 传入 `RenderBackend` + `RenderContext*`（可选），`UpdateBuffer` 时按后端选择 `FromBuffer(buffer_, backend, ctx)`。CPU 为默认，行为不变；GPU 仅在提供 RenderContext 时启用。

**接口建议**：
```cpp
class ExternalImage : public Widget {
  // 新增：构造时可注入 GPU 渲染上下文（nullptr = 默认 CPU）
  ExternalImage(Args&&... args, RenderContext* gpu_ctx = nullptr); // 或独立 setter
  void SetRenderContext(RenderContext* ctx);   // 可选：运行期切换后端
  ...
};
```

**注意**：GPU 路径下 `AHwb::ToGpuImage` 需非空 `GrDirectContext`（来自 RenderContext），且只支持 R8G8B8A8_UNORM。

### 4.2 `DashcamRenderer` 平台分支（media_record 改动）

**决策**：按平台分支，Android 用新的 surface 创建范式，否则默认 `Surface::CreateFromPixels`。

设计：`DashcamRenderer` 增加一个"渲染目标"抽象，或直接 ifdef：

- **host**：保持现状，`Render(frame_index, ts, PixelBuffer&)` 内部 `Surface::CreateFromPixels(buffer)`。
- **Android surface**：渲染目标来自 encoder 的 `CreateInputSurface`（经 RenderContext + Surface::Create）。此时 renderer 不接收 PixelBuffer，而是接收 `RenderContext*`。

**建议接口**：为隔离，引入 `RenderTarget` 概念（见 contracts），DashcamRenderer 构造/渲染时按目标选择。最小改动为 ifdef + 两个渲染入口。

### 4.3 节点接线（media_record 改动）

`DashcamRenderNode`（自驱动）与 `VideoEncoderNode` 需要协作。参考 MediaPipe 的 GPU 机制（见 §7），并复用现有 `pipeline_failed`（`bool*`）侧边包范式，**推荐方案**：

- **方案 B（推荐，MediaPipe 风格 + `LifecycleContext`）**：定义全局 `LifecycleContext` 结构体（`pipeline_failed` + `input_surface`），runner 经 `SetInputSidePacket("lifecycle_ctx", MakePacket<LifecycleContext*>(&ctx))` 以**指针**注入。`VideoEncoderNode::Open` 写 `ctx->input_surface`；`DashcamRenderNode` **首次 `Process`** lazy 读 `ctx->input_surface` 构造共享 `RenderContext` + renderer。共享上下文所有权属 graph 生命周期（runner 持有结构体）。
- ~~方案 A：DashcamRenderNode 内部创建 encoder（耦合较重）。~~

**关键设计点**：共享 `RenderContext` 需 host 在 encoder 的 input surface 上创建，因此其构造时机在 encoder Open 之后、render 使用之前。`LifecycleContext` 承载**指针**（Open 前注入、字段运行时可写）而非值，天然满足该时序，无需 graph 层额外"延迟求值"。

### 4.3.1 时序：为何 `LifecycleContext*` 侧边包能规避 Open 顺序问题

graph_runtime 的 input side packet 由外部在 Open 前注入（`SetInputSidePacket`）。若注入的是**值**，则在 Open 前必须就绪——与"encoder surface 是 Open 时才创建"冲突。

**解决**：注入的是**指针**（`Packet::MakePacket<LifecycleContext*>(&ctx)`），指针在 Open 前有效，结构体**字段**可在运行时可写：
- `VideoEncoderNode::Open` 写 `ctx->input_surface`。
- `DashcamRenderNode` **首次 `Process`**（此时所有节点 Open 已完成）读 `ctx->input_surface`，构造共享 `RenderContext` 并创建 renderer。

由于 input side packet 承载指针而非值，字段读写天然规避时序；且"Open 全部先于源节点 Process"保证首次 Process 时 `input_surface` 已就绪。这是 MediaPipe 中 graph service 在初始化阶段解析的等价实现，并复用现有 `pipeline_failed`（`bool*`）范式。

## 5. 参考实现

- `native_ui/examples/external_image_demo.cc`：AHWB→ExternalImage→encoder input surface→编码的完整闭环。
- `video_codec` `MediaCodecVideoEncoder`：`CreateInputSurface()`/`Poll()`/surface 模式契约。
- `media_record` 现有 `DashcamRenderer`/`DashcamRenderNode`：CPU 路径基线。
- MediaPipe GPU 官方文档（§7）。

## 7. MediaPipe GPU 上下文参考（Graph Service 机制）

MediaPipe 对"跨 GPU 计算器共享 GL 上下文"的标准做法（官方文档确认）：

### 7.1 机制要点

1. **图服务（graph service）注册**：所有 GPU 计算器共享的数据（`GpuResources`，内含 `GlContext`）作为 **graph service** 绑定到**计算图生命周期**注册，而非进程级全局单例。由 `GlCalculatorHelper` 管理。
2. **共享粒度可选**：可每个计算器独立 context（多线程并行），或全部共享一个 context（串行）——由 helper 管理，对计算器透明。
3. **计算器获取**：低层创建并持有 `GlCalculatorHelper` 实例；高层继承 `GlSimpleCalculator` 只覆写 `GlSetup/GlRender/GlTeardown`。
4. **共享数据类型**：`GpuBuffer`（GPU 图像，零拷贝跨计算器传递）；CPU↔GPU 桥接用 `GpuBufferToImageFrameCalculator` / `ImageFrameToGpuBufferCalculator`（零拷贝）。
5. **线程模型**：一个 GL context = 一条串行命令队列，**每 context 一个专用线程**；多 context 并行避免阻塞渲染路径。

### 7.2 对本文档的指导

- **不是裸全局单例**：共享 GPU 上下文绑定 graph 生命周期（graph service / side packet），而非进程级 static 单例。
- **共享状态图级持有，非进程级单例**：共享状态（`LifecycleContext`）绑定 graph 生命周期（runner 持有、经 side packet 指针注入），而非进程级 static 单例——这规避了"render 先 Open、拿不到 encoder surface"的时序问题。
- **side packet（指针）是正确载体**：MediaPipe 的 GPU 资源正是通过 graph 外部输入（side packet）注入计算图，印证本方案方向；graph_runtime 用指针承载运行时可写字段。
- **上下文所有权**：共享状态由 graph（runner）持有，节点只读写字段；`RenderContext` 由 render 节点（唯一 GL 绘制者）在首次 Process 构造，生命周期随 graph。

## 6. 风险与开放问题

- **节点协作顺序**：render 节点何时获取 encoder 的 `CreateInputSurface`？已定案：`LifecycleContext` 指针侧边包 + render 首次 `Process` lazy 读（encoder `Open` 写、render 首次 `Process` 读，由"Open 全部先于源节点 Process"保证）。实现时需验证 graph_runtime 确实满足该时序（Phase 0）。
- **分辨率对齐**：硬件编码器要求 16/256 对齐，背景图/小狗缩放需按 encoder native stride。
- **多线程/上下文**：EGL 上下文为单上下文规则，render 与编码必须共享同一 RenderContext，避免 GL 上下文切换。
- **host 验证**：Android 路径无法在 host 上运行，需 Android 设备/模拟器验证；host 侧仅保证编译 + 原路径无回归。
