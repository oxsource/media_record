# Contract: Public API — 录制入口与节点数据通路

**Branch**: `002-dashcam-sim-recording` | **Date**: 2026-08-18 | **Spec**: [spec.md](../spec.md)

## 1. 录制入口（Runnable）

```text
bazel run //src/examples:dashcam_record            # 默认配置，录制 10s
bazel run //src/examples:dashcam_record -- --help  # 用法
```

**行为**：
- 加载默认单路配置 `dashcam_record.json`（graph_runtime schema）+ 默认图片，程序化注入节点参数（image/output/fps/duration），同步驱动器录制 300 帧，输出 `out/dashcam.mp4`。
- 成功后退出码 0；失败打印可定位错误到 stderr，退出非零，不残留残缺文件。

**退出码约定**：
| 码 | 含义 |
|----|------|
| 0 | 录制成功，产物已原子落盘 |
| 1 | 输入图片缺失/格式不支持/输出不可写/编码或封装失败（stderr 含路径或节点名） |
| 2 | 参数错误（`--help` 正常显示） |

## 2. 节点数据通路（内部接口）

```cpp
// src/framework/runner/pipeline_runner.{h,cc}（media_record 内部）
namespace media::record {
class PipelineRunner;   // 同步 frame loop：GraphConfig 驱动 graph_runtime 节点
}
```

- 7 类节点实现 **`graph::runtime::Node`**（graph_runtime 公共面类型），经 `GRAPH_RUNTIME_REGISTER_NODE` 注册到 graph_runtime `NodeFactoryRegistry`；帧数据为 `graph::runtime::Packet`（`video::codec::VideoFrame` / `VideoPacket` / `SignalEvent` 载荷）；由 `PipelineRunner` 实例化（`NodeFactoryRegistry::CreateByName(type, name, options)`）与编排（`GraphContext` Open/Process/Close + Packet 按流搬运）。
- 配置唯一性：图拓扑只存于 graph_runtime `GraphConfig`（JSON 为其 schema）；节点参数由入口程序化设置 `NodeDef.options`。
- **外部消费者**（Bazel 项目）仍只消费 `@media_record//:media_record`（公共 umbrella，见 001 `contracts/public-api.md`）；录制入口、节点实现与 `PipelineRunner` 为内部可执行/内部目标。

## 3. 公共面不变式

- `//src/framework/public:media_record`（001 umbrella + `Node`/`NodeRegistry` 骨架）**保持不变**，本 feature 不修改公共头；001 的 `hello_graph.cc` 仍以其骨架 + 占位节点运行 recorder.json。
- 新模块 `src/framework/runner/`、节点实现、示例、测试均以 `//` 前缀内部消费；三库仅经公共 umbrella 消费（graph_runtime 经 `@graph_runtime//src/framework/public:runtime` 单 target，内部头经传递依赖可见，与 graph_runtime 自身 examples 一致）。

## 4. 验收

- `dashcam_record` 默认运行退出码 0，产物存在且可播放（SC-001/002）。
- `make verify` 覆盖编译 + 测试 + 录制产物检查（SC-004）。
