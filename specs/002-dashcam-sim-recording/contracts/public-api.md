# Contract: Public API — 录制入口与节点数据通路

**Branch**: `002-dashcam-sim-recording` | **Date**: 2026-08-18 | **Spec**: [spec.md](../spec.md)

## 1. 录制入口（Runnable）

```text
bazel run //src/examples:dashcam_record            # 默认配置，录制 10s
bazel run //src/examples:dashcam_record -- --help  # 用法
```

**行为**：
- 加载默认单路配置 `dashcam_record.json`（graph_runtime schema，节点参数在每节点 `"options"` 对象里），由 graph_runtime 自身运行时驱动录制配置帧数（默认 300 = 10s×30fps），输出 `out/dashcam.mp4`。
- 成功后退出码 0；失败打印可定位错误到 stderr，退出非零，不残留残缺文件。

**退出码约定**：
| 码 | 含义 |
|----|------|
| 0 | 录制成功，产物已原子落盘 |
| 1 | 输入图片缺失/格式不支持/输出不可写/编码或封装失败（stderr 含路径或节点名） |
| 2 | 参数错误（`--help` 正常显示） |

## 2. 节点数据通路（内部接口）

media_record **无独立 runner 模块**：录制入口 `src/examples/dashcam_record.cc` 直接驱动 graph_runtime 自身的异步运行时，内联生命周期（`GraphRuntime::Initialize` → `SetInputSidePacket` → `SetErrorCallback` → `Start` → `WaitUntilDone` → `Shutdown`）。可选 CLI 补丁（`--image/--output/--frames`）经 graph_runtime 的节点注入 `GraphRuntime::Options`（参数对象，`nodes` map 按节点类型覆盖，`Initialize(config, options)` 在构建前合并）打到匹配节点的 options。

- 6 类节点实现 **`graph::runtime::Node`**（graph_runtime 公共面类型），经 `GRAPH_RUNTIME_REGISTER_NODE` 注册到 graph_runtime `NodeFactoryRegistry`；帧数据为 `graph::runtime::Packet`（`video::codec::VideoFrame` / `VideoPacket` 载荷）；由 graph_runtime 运行时实例化与编排（`GraphRuntime::Initialize` 接线内部流 + scheduler async 调度），录制入口做生命周期驱动。
- 配置唯一性：图拓扑只存于 graph_runtime `GraphConfig`（JSON 为其 schema）；节点参数配置在每节点 `"options"` 对象里，经 graph_runtime `JsonParser` 解析进 `NodeDef::options`；可选 CLI 参数仅补丁对应节点 options（`GraphRuntime::Options`）。
- **外部消费者**（Bazel 项目）仍只消费 `@media_record//:media_record`（公共 umbrella，见 001 `contracts/public-api.md`）；录制入口、节点实现为内部可执行/内部目标。

## 3. 公共面不变式

- `//src/framework/public:media_record`（001 umbrella + `Node`/`NodeRegistry` 骨架）**保持不变**，本 feature 不修改公共头；001 的 `hello_graph.cc` 仍以其骨架 + 占位节点运行 recorder.json。
- 节点实现、示例、测试均以 `//` 前缀内部消费；三库仅经公共 umbrella 消费（graph_runtime 经 `@graph_runtime//src/framework/public:runtime` 单 target，内部头经传递依赖可见，与 graph_runtime 自身 examples 一致）。

## 4. 验收

- `dashcam_record` 默认运行退出码 0，产物存在且可播放（SC-001/002）。
- `make verify` 覆盖编译 + 测试 + 录制产物检查（SC-004）。
