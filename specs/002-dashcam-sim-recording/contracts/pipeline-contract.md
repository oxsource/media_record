# Contract: Pipeline 运行（记录仪）

**Branch**: `002-dashcam-sim-recording` | **Date**: 2026-08-18 | **Spec**: [spec.md](../spec.md)

## 1. 运行模型

media_record **无独立 runner 模块**：录制入口 `src/examples/dashcam_record.cc`（及测试）直接驱动 **graph_runtime 自身运行时**（async 执行路径），内联生命周期（`GraphRuntime::Initialize` → `SetInputSidePacket` → `SetErrorCallback` → `Start` → `WaitUntilDone` → `Shutdown`）：

1. 加载 + 校验 graph_runtime `GraphConfig`（JSON 采用 graph_runtime schema，由 graph_runtime `JsonParser` 解析，节点 `"options"` 对象进入 `NodeDef::options`；复用 `ConfigValidator` 语义）。可选 CLI 补丁经 `GraphRuntime::Options`（`nodes` map 按节点类型覆盖）打到匹配节点 options。
2. `GraphRuntime::Initialize`：实例化节点 + 建流管理器；输入端口按 **port 名**（`"port:stream"` 冒号前）注册；**内部流接线**——按 stream 名（冒号后）把 producer 的 `OutputStreamManager` `AddMirror` 到消费者输入 handler（本 feature 补足 graph_runtime 的 `AddMirror` 调用点）；仅 `config.input_streams`（图级外部输入）计入完成计数。
3. `GraphRuntime::Start`：`Open()` 全部节点，source 节点进入调度；任一节点 Open 失败 → 中止 run 并返回可定位错误（含节点名 + 原因）。
4. 调度执行：source（`StreamInputNode`）按 `NodeOptions` `fps` 帧率产包（进程内墙钟 pacing，对齐「10 秒录制 ≈ 10 秒墙钟」目标），包经 mirror 流入下游输入队列 → 下游节点由 arrival 调度消费；`frame_count` 帧后 source 返回 `StatusStop()`。
5. 完成：全部 source 停止（且图级输入流关闭）→ scheduler 关闭全部节点（`CloseAllNodes`）→ muxer trailer + rename（FR-009）。
6. 首错中止：执行中任一节点返回非 OK 状态 → 经 `SchedulerQueue` error callback 标记失败并转发原始错误 → 节点关闭前回调录制入口（更新局部 `pipeline_failed`，经 side packet `"pipeline_failed"` 传给 muxer）→ muxer 丢弃部分产物；入口返回含节点名 + 原因的错误。

## 2. 模板清单

| id | 文件 | runnable（本期） | 说明 |
|----|------|------------------|------|
| recorder | `src/examples/configs/recorder.json` | 校验通过（双摄参考模板，8 nodes / 7 streams 断言不变） | 多路→布局→OSD→编码→录像 |
| dashcam_record | `src/examples/configs/dashcam_record.json` | **是（默认录制入口配置）** | 单路：input→layout→overlay→encoder→recorder→muxer |
| stream | `src/examples/configs/stream.json` | 配置校验通过 | 编码→推流（后续） |
| preview | `src/examples/configs/preview.json` | 配置校验通过 | 复合画面→屏幕（后续） |

模板 JSON 均为 **graph_runtime schema**（`nodes[]` + `input_streams`/`output_streams`，`"port:stream"` 命名，无独立 `streams[]` 段；节点参数配置在每节点 `"options"` 对象里，由 graph_runtime `JsonParser` 解析进 `NodeDef::options`）。

## 3. dashcam_record.json 拓扑（默认）

```
input(image) ─ output:frames ─ f → layout ─ output:view_frames
                                          │
                                          ▼
                                      overlay(video) ─ output:osd_frames
                                          │
                                          ▼
                                      encoder ─ output:es_packets
                                          │
                                          ▼
                                      recorder ─ output:clips
                                          │
                                          ▼
                                      muxer（写 out/dashcam.mp4）
```

**streams**（隐式，由 `port:stream` 声明）：`frames`、`view_frames`、`osd_frames`、`es_packets`、`clips`（5 条）。

## 4. 规则

| # | 规则 |
|---|------|
| P-1 | `nodes[].type` 即 graph_runtime `NodeFactoryRegistry` 注册名（`GRAPH_RUNTIME_REGISTER_NODE`）；未注册 type 报错含节点名 |
| P-2 | 节点 `name` 全局唯一；stream 引用合法（`ConfigValidator` 校验连通性/无环） |
| P-3 | 端口用 `"port:stream"`；port 名与节点契约端口匹配 |
| P-4 | 帧数据统一 `graph::runtime::Packet`；单消费者语义 |
| P-5 | 默认配置单路输入（对齐「单张图片模拟单路输入」）；recorder.json 保留双摄参考 |
| P-6 | 失败不残留残缺产物：MP4 先写临时文件，成功 rename，失败删除（FR-009） |

## 5. 验收

- `dashcam_record.json` 以 graph_runtime `GraphConfig` 校验通过（加入模板清单断言）。
- 默认配置录制 300 帧，输出 MP4 可播放（`ftyp`/`moov`/`mdat` 存在），时长≈10s（误差 ≤5%，SC-002）。
- 驱动测试（短帧数 + 桩节点）覆盖：内部流接线（producer→consumer mirror 转发）、`StatusStop` 结束、首错中止（错误传播到 runner 且 muxer 丢弃部分产物）。
