# Pipeline 设计（Pipelines）

> Spec 001（001-project-architecture）| 与 `contracts/pipeline-contract.md`、`research.md §6` 一致

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
signals    ─ output:signals ──────────┐    overlay (video + signal)
                                      └──▶  │ → output:osd_frames
                                            ▼
                                        encoder → output:es_packets
                                            ▼
                                        recorder → output:clips
                                            ▼
                                        muxer（落盘 MP4）
```

**数据流**

1. `StreamInputNode`（cam_front / cam_rear）接收采集图像，输出 `Packet<VideoFrame>`。
2. `SignalSourceNode` 输出车辆/设备信号事件（旁路事件，见 §5）。
3. `MultiViewLayoutNode` 将多路画面按 `f` / `r` tag 排布为复合画面。
4. `UiOverlayNode` 叠加 OSD（时间戳 / 事件徽标），输入 tag 区分 `video`（画面）与 `signal`（事件）。
5. `VideoEncoderNode` 编码为 ES 流。
6. `RecorderNode` 缓存池 / 存储切换 / 防抖，输出分段 `clips`。
7. `MuxerSinkNode` 将 `clips` 封装落盘。

**streams[]**：`front_frames`、`rear_frames`、`signals`、`view_frames`、`osd_frames`、`es_packets`、`clips`（7 条）。

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

## 5. 旁路事件（Bypass Events）

`SignalSourceNode` 产生的信号事件属于**旁路事件**：不与视频帧走同一队列，而是按 tag 独立接入 `UiOverlayNode` 的 `signal` 输入，避免事件驱动 OSD 时阻塞视频主链。后续 feature 中，事件同样可旁路接入 `RecorderNode`（触发分段/防抖）与 `PreviewNode`（告警弹层）。

统一约定：事件流与视频流**命名空间分离**（`signals` vs `*_frames`），事件 tag 语义化（`signal`），互不共享队列。

## 6. 统一组合规则

| # | 规则 |
|---|------|
| R-1 | 节点名小写驼峰（`cam_front`）；流名小写蛇形（`front_frames`）；port tag 语义化（`video` / `signal` / `input` / `output`） |
| R-2 | `nodes[].type` 即 `NodeFactoryRegistry` 注册名；引用未注册类型时示例给出含节点名的可读错误（FR-009） |
| R-3 | 每个节点 `input_streams` / `output_streams` 用 `tag:stream_name`；tag 对应节点契约端口，stream_name 在图中全局唯一 |
| R-4 | 每个 input stream 必须由图内某节点的 output stream 产生（连通性，graph_runtime `ConfigValidator` 校验） |
| R-5 | 节点 `name` 全局唯一；`streams[]` 的 source/dest 节点与 port 必须匹配已定义节点（`pipeline_config_test` 校验） |
| R-6 | 通用格式：`输入 → [渲染/布局 → OSD] → 编码 → 输出（录像 / 推流 / 预览）`；编码产物为 ES 流，消费端决定封装（MP4 / RTMP / 屏幕） |

## 7. 模板索引与验证

- 模板：`src/examples/configs/{recorder,stream,preview}.json`
- 配置校验测试：`src/tests/pipeline_config_test.cc`（加载 + 解析 + 引用/port 校验 + type 白名单）
- 可运行示例：`src/examples/hello_graph.cc`（recorder 端到端执行；stream/preview 给出可定位缺失节点提示）
- 相关文档：`engineering-structure.md`（模块/目录/依赖规则）、`contracts/pipeline-contract.md`（schema 与验收）
