# Contract: Pipeline 运行（记录仪）

**Branch**: `002-dashcam-sim-recording` | **Date**: 2026-08-18 | **Spec**: [spec.md](../spec.md)

## 1. 运行模型

`PipelineRunner`（`src/framework/runner/`）以**同步 frame loop** 在调用线程上执行 graph_runtime 节点图（对齐 graph_runtime `src/examples/string_pipeline.cc` 的手工 `GraphContext` 驱动模式，改为配置驱动）：

1. 加载 + 校验 graph_runtime `GraphConfig`（JSON 采用 graph_runtime schema，由 graph_runtime `JsonParser` 解析，节点 `"options"` 对象进入 `NodeDef::options`；复用 `ConfigValidator` 语义）。
2. `NodeFactoryRegistry::CreateByName(type, name, options)` 实例化全部节点（`options` 来自配置 JSON，节点构造时读入自己的数据结构）。
3. 按 stream 名（`"port:stream"` 冒号后的部分）建立流路由：每个节点 `output_streams` 声明 `port:stream`，下游 `input_streams` 引用同名 stream。
4. 逐帧驱动：先 `Open()` 全部节点（拓扑序）；每帧按拓扑序 `Process()`——source 节点产包 → 包按 stream 路由进入下游输入 shard → 下游节点消费/产出；source 返回 `StatusStop()` 后标记结束；达 `frame_count`（300）或全部 source 结束 → 标记流结束（EOS）并 drain（节点在输入结束 + 空时 flush/finalize，如 encoder `Flush`、recorder finalize、muxer trailer + rename）→ 逆拓扑序 `Close()` 全部节点。
5. 首错中止：任一节点返回非 OK 状态 → 中止 run 并返回可定位错误（含节点名 + 原因）。

**节点执行顺序**：按依赖拓扑序（stream 连通性），非配置书写序。帧率节流（30fps → 每帧约 33ms）由驱动器按墙钟 pacing 实现（对齐「10 秒录制 ≈ 10 秒墙钟」目标）。

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
input(image) ─ output:frames ─┐
                              ├─ f → layout ─ output:view_frames
signals ───── output:signals ─┤         │
                              ▼
                          overlay(video+signal) ─ output:osd_frames
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

**streams**（隐式，由 `port:stream` 声明）：`frames`、`signals`、`view_frames`、`osd_frames`、`es_packets`、`clips`（6 条）。

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
- 驱动器单元测试（短帧数 + 桩节点）覆盖：拓扑序执行、`StatusStop` 结束、EOS drain、首错中止。
