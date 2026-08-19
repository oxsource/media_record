# Pipeline 设计（Pipelines）

> Spec 001（001-project-architecture）| 与 `contracts/pipeline-contract.md`、`research.md §6` 一致
> 002（dashcam-sim-recording）实现记录仪 6 类节点：布局/OSD 用 native_ui flex 布局 + media_record 软件绘制进自有 RGBA 帧，编码复用 video_codec `VideoEncoder`，封装复用 video_codec `Muxer`（`FileByteSink`）；见 `specs/002-dashcam-sim-recording/`。

## 1. 概述

media_record 覆盖三类核心 pipeline，均以 **graph_runtime 原生 JSON schema** 表达（见 `contracts/pipeline-contract.md` §1），模板预置于 `src/examples/configs/`：

| id | 模板 | 拓扑 | 本期运行性（spec SC-008） |
|----|------|------|--------------------------|
| recorder | `recorder.json` | 多路输入 → 布局 → OSD → 编码 → 录像 | 端到端可运行（占位节点，`hello_graph` 演示） |
| stream | `stream.json` | 编码 → 推流 | 仅配置校验 |
| preview | `preview.json` | 复合画面 → 屏幕 | 仅配置校验 |

Schema 以实际 `graph_runtime` 解析器为准（`json_parser.cc` 字段：`nodes[].{name, type, input_streams, output_streams, ...}`，stream 连接用 `tag:stream_name` 表达）；顶层 `streams[]` 为设计自描述信息，由本工程 `src/framework/config` 校验（引用合法性与 port tag 匹配），graph_runtime 解析器对其忽略不计。

## 2. 记录仪 pipeline（recorder.json）

**连线**

```
cam_front  ─ output:front_frames ─┐
                                  ├─ f/r → layout → output:view_frames
cam_rear   ─ output:rear_frames ──┘          │
                                             ▼
                                         overlay(video) → output:osd_frames
                                             ▼
                                         encoder → output:es_packets
                                             ▼
                                         recorder → output:clips
                                             ▼
                                         muxer（落盘 MP4）
```

**数据流**

1. `StreamInputNode`（cam_front / cam_rear）接收采集图像，输出 `Packet<VideoFrame>`。
2. `MultiViewLayoutNode` 将多路画面按 `f` / `r` tag 排布为复合画面。
3. `UiOverlayNode` 叠加 OSD（时间戳），输入 tag `video`（画面）。
4. `VideoEncoderNode` 编码为 ES 流。
5. `RecorderNode` 缓存池 / 存储切换 / 防抖，输出分段 `clips`。
6. `MuxerSinkNode` 将 `clips` 封装落盘。

**streams[]**：`front_frames`、`rear_frames`、`view_frames`、`osd_frames`、`es_packets`、`clips`（6 条）。

### 2.1 002 真实实现（dashcam-sim-recording）

001 的占位节点在 002 中实现为可运行的 6 类节点，默认配置为单路 `dashcam_record.json`（StreamInput → MultiViewLayout → UiOverlay → VideoEncoder → Recorder → MuxerSink，5 条 stream）：

| 节点 | 002 实现 |
|------|----------|
| `StreamInputNode` | `Image::FromFile` + `CopyPixels` 将默认图片解码为 RGBA；每 `Process` 产出一帧（真实时钟 + pts） |
| `MultiViewLayoutNode` | native_ui flex 布局（`Container` + `ExternalImage`）确定结构；图像软件 blit 进自有 RGBA 帧（单视图，`f`/`r` 预留） |
| `UiOverlayNode` | flex 布局把时间戳框锚定到右下角，软件位图字体（5×7）绘制真实时钟 `%Y-%m-%d %H:%M:%S` |
| `VideoEncoderNode` | 软件 RGBA→I420（内置转换）→ video_codec `VideoEncoder`（H.264，push 模式经 `PacketSink` 适配器入流） |
| `RecorderNode` | 单会话单分段：帧计数、转发编码包，`Close` 时收敛会话 |
| `MuxerSinkNode` | video_codec `Muxer`（非分片 MP4，moov 尾写）+ `FileByteSink` 临时文件 → 成功后原子 rename；失败删除临时文件（FR-009） |

入口 `bazel run //src/examples:dashcam_record`（默认 10s @ 30fps → `out/dashcam.mp4`）直接驱动 graph_runtime 自身的异步运行时（无独立 runner 模块）：`dashcam_record.cc` 内联 `GraphRuntime` 生命周期（Initialize → Start → WaitUntilDone → Shutdown）；CLI 覆盖（`--image/--output/--frames`）经 graph_runtime 的 `GraphRuntime::Options` 注入（参数对象，`nodes` map 按节点类型覆盖）打到匹配节点的 options。双摄 `recorder.json` 仍保持「配置校验通过 + `hello_graph` 占位运行」定位（移除 SignalSource 后 7 nodes / 6 streams）。

## 3. 推流 pipeline（stream.json）

**连线**

```
source      → output:src_frames
                │
                ▼
encoder     → output:es_packets
                │
                ▼
stream_sink（RTMP/WebRTC 推流服务）
```

**数据流**

1. `StreamInputNode` 提供编码源画面。
2. `VideoEncoderNode` 编码为 ES 流。
3. `StreamSinkNode` 对接推流服务（RTMP / WebRTC，后续 feature 实现）。

**streams[]**：`src_frames`、`es_packets`（2 条）。

> 注意：推流端到端运行依赖后续节点实现，本期仅配置校验通过（spec SC-008）。

## 4. 预览 pipeline（preview.json）

**连线**

```
cam_front ─ output:front_frames ─┐
                                  ├─ f/r → layout → output:view_frames
cam_rear  ─ output:rear_frames ──┘          │
                                            ▼
                                        preview（屏幕）
```

**数据流**

1. 多路 `StreamInputNode` 输出采集画面。
2. `MultiViewLayoutNode` 排布复合画面。
3. `PreviewNode` 上屏（host 桌面 / Android surface，平台 stub 覆盖，spec FR-005）。

**streams[]**：`front_frames`、`rear_frames`、`view_frames`（3 条）。

> 预览端到端运行依赖后续节点实现，本期仅配置校验通过。

## 5. 统一组合规则

| # | 规则 |
|---|------|
| R-1 | 节点名小写驼峰（`cam_front`）；流名小写蛇形（`front_frames`）；port tag 语义化（`video` / `input` / `output`） |
| R-2 | `nodes[].type` 即 `NodeFactoryRegistry` 注册名；引用未注册类型时示例给出含节点名的可读错误（FR-009） |
| R-3 | 每个节点 `input_streams` / `output_streams` 用 `tag:stream_name`；tag 对应节点契约端口，stream_name 在图中全局唯一 |
| R-4 | 每个 input stream 必须由图内某节点的 output stream 产生（连通性，graph_runtime `ConfigValidator` 校验） |
| R-5 | 节点 `name` 全局唯一；`streams[]` 的 source/dest 节点与 port 必须匹配已定义节点（`pipeline_config_test` 校验） |
| R-6 | 通用格式：`输入 → [渲染/布局 → OSD] → 编码 → 输出（录像 / 推流 / 预览）`；编码产物为 ES 流，消费端决定封装（MP4 / RTMP / 屏幕） |

## 6. 模板索引与验证

- 模板：`src/examples/configs/{recorder,stream,preview}.json`
- 配置校验测试：`src/tests/pipeline_config_test.cc`（加载 + 解析 + 引用/port 校验 + type 白名单）
- 可运行示例：`src/examples/hello_graph.cc`（recorder 端到端执行；stream/preview 给出可定位缺失节点提示）
- 相关文档：`engineering-structure.md`（模块/目录/依赖规则）、`contracts/pipeline-contract.md`（schema 与验收）
