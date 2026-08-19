# Contract: Pipeline 运行（记录仪）

**Branch**: `002-dashcam-sim-recording` | **Date**: 2026-08-18 | **Spec**: [spec.md](../spec.md)

## 1. 运行模型

`PipelineRunner`（`src/framework/transport/`）以**同步 frame loop** 执行配置图：

1. 加载 + 校验 `PipelineConfig`（复用 001 `src/framework/config`）。
2. `NodeRegistry::Create(type)` 实例化全部节点。
3. 按 `streams[]` 连接：`stream_name → StreamBuffer`，节点读写各自声明的 input/output。
4. 逐帧驱动：source 节点（StreamInput 每帧产一帧、SignalSource 周期产事件）→ 下游节点消费/产出 → 达 `frame_count`（300）后逐节点 `Close()`（recorder finalize、muxer trailer + rename）。

**节点执行顺序**：按依赖拓扑序（`streams[]` 连通性），非配置书写序。

## 2. 模板清单

| id | 文件 | runnable（本期） | 说明 |
|----|------|------------------|------|
| recorder | `src/examples/configs/recorder.json` | 校验通过（双摄参考模板，8 nodes / 7 streams 断言不变） | 多路→布局→OSD→编码→录像 |
| dashcam_record | `src/examples/configs/dashcam_record.json` | **是（默认录制入口配置）** | 单路：input→layout→overlay→encoder→recorder→muxer |
| stream | `src/examples/configs/stream.json` | 配置校验通过 | 编码→推流（后续） |
| preview | `src/examples/configs/preview.json` | 配置校验通过 | 复合画面→屏幕（后续） |

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

**streams[]**：`frames`、`signals`、`view_frames`、`osd_frames`、`es_packets`、`clips`（6 条）。

## 4. 规则

| # | 规则 |
|---|------|
| P-1 | `nodes[].type` 即 `NodeRegistry` 注册名；未注册 type 报错含节点名 |
| P-2 | 节点 `name` 全局唯一；`streams[].source_node`/`dest_node` 引用已定义节点 |
| P-3 | port 用 `tag:stream_name`；tag 与节点契约端口匹配 |
| P-4 | 帧数据统一 `media::record::Packet`；单消费者语义 |
| P-5 | 默认配置单路输入（对齐「单张图片模拟单路输入」）；recorder.json 保留双摄参考 |
| P-6 | 失败不残留残缺产物：MP4 先写临时文件，成功 rename，失败删除（FR-009） |

## 5. 验收

- `dashcam_record.json` 被 `pipeline_config_test` 校验通过（加入模板清单）。
- 默认配置录制 300 帧，输出 MP4 可播放（`ftyp`/`moov`/`mdat` 存在），时长≈10s（误差 ≤5%，SC-002）。
