# Contract: Public API — 录制入口与节点数据通路

**Branch**: `002-dashcam-sim-recording` | **Date**: 2026-08-18 | **Spec**: [spec.md](../spec.md)

## 1. 录制入口（Runnable）

```text
bazel run //src/examples:dashcam_record            # 默认配置，录制 10s
bazel run //src/examples:dashcam_record -- --help  # 用法
```

**行为**：
- 加载默认单路配置 `dashcam_record.json` + 默认图片，录制 300 帧，输出 `out/dashcam.mp4`。
- 成功后退出码 0；失败打印可定位错误到 stderr，退出非零，不残留残缺文件。

**退出码约定**：
| 码 | 含义 |
|----|------|
| 0 | 录制成功，产物已原子落盘 |
| 1 | 输入图片缺失/格式不支持/输出不可写/编码或封装失败（stderr 含路径或节点名） |
| 2 | 参数错误（`--help` 正常显示） |

## 2. 节点数据通路（内部接口）

```cpp
// src/framework/transport/packet.h（media_record 内部，非公共 umbrella）
namespace media::record {
class Packet;                        // 持有 VideoFrame / VideoPacket / SignalEvent
class StreamBuffer;                  // 按流名的有界信箱
class PipelineRunner;                // 同步 frame loop 执行 PipelineConfig
}
```

- 节点实现 `media::record::Node`（公共骨架）+ 通过 `REGISTER_NODE` 注册；由 `PipelineRunner` 实例化与编排。
- **外部消费者**（Bazel 项目）仍只消费 `@media_record//:media_record`（公共 umbrella，见 001 `contracts/public-api.md`）；录制入口与节点实现为内部可执行目标。

## 3. 公共面不变式

- `//src/framework/public:media_record`（umbrella + `Node` 骨架 + `NodeRegistry`）**保持不变**，本 feature 不修改公共头。
- 新模块 `src/framework/transport/`、节点实现、示例、测试均以 `//` 前缀内部消费；三库仅经公共 umbrella 消费。

## 4. 验收

- `dashcam_record` 默认运行退出码 0，产物存在且可播放（SC-001/002）。
- `make verify` 覆盖编译 + 测试 + 录制产物检查（SC-004）。
