# Data Model: Dashcam 管线 Android 适配

**Created**: 2026-08-19

## 1. 核心数据实体

### 1.1 AHardwareBuffer (AHWB)

Android 硬件缓冲，承载背景图 RGBA 像素。

```cpp
// native_ui::AHwb 工具类（Android-only）
AHardwareBuffer* AHwb::AllocateRGBA(uint32_t w, uint32_t h);          // R8G8B8A8_UNORM
int  AHwb::WriteRGBA(AHardwareBuffer*, const uint8_t* src, size_t row);
sk_sp<SkImage> AHwb::ToGpuImage(AHardwareBuffer*, GrDirectContext*);  // 零拷贝
sk_sp<SkImage> AHwb::ToCpuImage(AHardwareBuffer*, bool copy);
```

- 格式约束：仅 `R8G8B8A8_UNORM`（FR-006）。
- 生命周期：由 media_record 持有，GPU 路径下 buffer 需在 SkImage 使用期间存活。

### 1.2 HardwareBuffer（native_ui 包装）

```cpp
struct HardwareBuffer {
  // FromAHardwareBuffer(AHardwareBuffer*) 创建，非 owning 包装
  bool IsValid() const;
  int kind() const;   // kAHardwareBuffer
  int width()/height()/format() const;
  bool operator==(const HardwareBuffer&) const;  // handle 比较
};
```

### 1.3 RenderContext（渲染上下文）

```cpp
struct RenderContext {
  GrDirectContext* gr;   // Skia GPU context on shared EGL
  void* display/context/surface;  // EGL 句柄
  int width, height;
  static std::unique_ptr<RenderContext> CreateFromNativeWindow(void* surface, int w, int h);
  void MakeCurrent();   // eglMakeCurrent
  void SwapBuffers();   // 交付 encoder
};
```

- 单上下文规则：render 与编码共享同一 EGL 上下文。
- Android-only；host 返回 nullptr。

### 1.4 CreateInputSurface（编码器输入面）

```cpp
// video::codec::VideoEncoder
void* CreateInputSurface();   // surface 模式返回 ANativeWindow*（void*）
Status Poll();                 // surface 模式泵输出
```

- 生命周期：由 encoder 拥有，调用方不得 release。
- surface 模式（`VideoConfig.input_surface=true`）与 `Encode(VideoFrame)` 互斥。

### 1.5 LifecycleContext（跨节点共享的图级运行上下文）

跨节点共享状态收敛到一个全局结构体，经 `SetInputSidePacket` 以**指针**注入（与现有 `pipeline_failed` 的 `bool*` 范式一致），字段在运行时可读写。

```cpp
// runner（dashcam_record.cc）局部定义，graph 生命周期持有
struct LifecycleContext {
  bool pipeline_failed = false;   // 首错中止标记（复用现有 FR-009 机制）
  void* input_surface = nullptr;  // VideoEncoderNode::Open 写入（ANativeWindow*）
  // 可扩展：native::ui::RenderContext* shared_render_context; 等
};

// 注入：
//   LifecycleContext lifecycle_ctx;
//   runtime.SetInputSidePacket("lifecycle_ctx",
//       graph::runtime::Packet::MakePacket<LifecycleContext*>(&lifecycle_ctx));
```

- 所有权：runner 持有，节点只读写字段（不释放）。
- 同步：单写（encoder Open 写 `input_surface`）/单读（render 首次 Process 读），由调度模型保证 happens-before，无需锁；多线程场景改为 `std::atomic`。
- 与 `pipeline_failed`：迁移到结构体字段（`ctx_->pipeline_failed`），替换原独立 `bool*` 侧边包，行为不变。

## 2. 渲染目标抽象

DashcamRenderer 按平台选择渲染目标：

| 平台 | 渲染目标 | 创建方式 | 绘制入口 |
|------|----------|----------|----------|
| host | 外部 PixelBuffer | `Surface::CreateFromPixels(buffer)` | `Render(idx, ts, PixelBuffer&)` |
| Android | encoder input surface | `Surface::Create(RenderContext*)` | `Render(idx, ts, RenderContext*)` |

两者共用动画/时间戳逻辑，仅目标 Surface 不同。

## 3. 背景图数据流

```
host:   PNG --Image::FromFile--> RGBA buffer --Surface::CreateFromPixels--> Canvas
Android: PNG --Image::FromFile--> RGBA --AllocateRGBA+WriteRGBA--> AHWB
        --HardwareBuffer::FromAHardwareBuffer--> ExternalImage(hb, ctx->gr)
        --Image::FromBuffer(kGPU)--> GPU texture --Canvas draw--> input surface
```

## 4. 编码数据流

```
Android: Canvas 绘制到 input surface
  --> ctx->gr->flush()      // 提交 GPU 绘制
  --> ctx->SwapBuffers()    // eglSwapBuffers，交付编码器
  --> encoder->Poll()       // 泵出 MediaCodec 编码输出
  --> muxer（MP4）
```
