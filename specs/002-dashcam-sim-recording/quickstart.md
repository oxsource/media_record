# Quickstart: 模拟行车记录仪录制

**Branch**: `002-dashcam-sim-recording` | **Date**: 2026-08-18 | **Spec**: [spec.md](spec.md)

## 前置条件

- Bazel 6.5.x（推荐 bazelisk）。
- C++17 工具链（macOS Xcode CLT / Linux gcc）。
- 三个依赖仓库已存在于本机（默认路径见 `media_record_deps.bzl`）。

## 一键录制

```bash
cd media_record/media_record

# 默认录制：单路图片 + 真实时钟 OSD，10 秒后自动结束，输出 out/dashcam.mp4
bazel run //src/examples:dashcam_record
```

预期：运行约 10 秒后退出码 0，生成 `out/dashcam.mp4`（H.264 + MP4，含输入图片与逐帧递增的时间戳）。

执行模型：7 类节点为 `graph::runtime::Node`（经 `GRAPH_RUNTIME_REGISTER_NODE` 注册），拓扑存于 graph_runtime `GraphConfig`（JSON 为其 schema，`"port:stream"` 命名），由 `src/framework/runner/` 同步驱动器在调用线程上驱动（对齐 graph_runtime `src/examples/string_pipeline.cc` 模式）。

## 产物检查

```bash
# 文件存在且为 MP4 容器
ls -lh out/dashcam.mp4
# 可用 ffprobe 验证时长与编码（若安装了 FFmpeg 工具）
ffprobe -v error -show_entries format=duration,format_name -of default=noprint_wrappers=1 out/dashcam.mp4
```

## 构建 / 测试

```bash
bazel build //...               # 全量构建（含 7 类节点、录制入口、测试）
bazel test //src/tests:all      # 配置校验 + 端到端录制测试
```

## 一键验证

```bash
make verify    # 编译 + 测试 + 录制产物检查，聚合结果
```

## 相关模板

| 模板 | 文件 | 说明 |
|------|------|------|
| 默认录制配置 | `src/examples/configs/dashcam_record.json` | 单路可运行 |
| 双摄参考 | `src/examples/configs/recorder.json` | 8 nodes / 7 streams，配置校验 |
| 推流 / 预览 | `stream.json` / `preview.json` | 配置校验（节点后续实现） |

## 下一步

- 节点契约：`specs/002-dashcam-sim-recording/contracts/node-contract.md`
- 数据流：`specs/002-dashcam-sim-recording/contracts/pipeline-contract.md`
- 架构文档：`media_record/doc/architecture/pipelines.md`（运行拓扑更新）
