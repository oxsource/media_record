# Quickstart: 工程架构设计及基础框架搭建

**Branch**: `001-project-architecture` | **Date**: 2026-08-17 | **Spec**: [spec.md](spec.md)

## 前置条件

- Bazel 6.5.x（推荐 bazelisk）。
- C++17 工具链（macOS Xcode CLT / Linux gcc）。
- 三个依赖仓库已存在于本机（默认路径见 `media_record_deps.bzl`）。

## 构建

```bash
cd media_record/media_record

# 全量构建（含示例与测试目标）
bazel build //...

# 公共库 + 共享库
bazel build //src/framework/public:media_record
bazel build //src/framework/public:media_record_shared

# 指定平台
bazel build //... --config=macos_arm64
bazel build //... --config=linux_x86_64
```

> 首次构建会拉取三个仓库的 transitive 依赖（absl / skia / yoga / ffmpeg 等），需要网络；之后由 bazel cache 复用。

## 运行示例

```bash
# 加载预置 pipeline 模板（记录仪 / 推流 / 预览）
bazel run //src/examples:hello_graph -- --config=src/examples/configs/recorder.json
bazel run //src/examples:hello_graph -- --config=src/examples/configs/stream.json
bazel run //src/examples:hello_graph -- --config=src/examples/configs/preview.json
```

预期：recorder 模板启动最小节点图并正常退出（退出码 0）；stream / preview 模板解析校验通过（节点未实现时给出可定位提示）。

## 测试

```bash
bazel test //src/tests:all      # 冒烟测试（三个依赖库 umbrella 链接）+ pipeline 模板配置校验
```

## 架构 / pipeline 设计文档

- `media_record/doc/architecture/engineering-structure.md`：工程结构（模块划分 / 目录 / 命名）
- `media_record/doc/architecture/pipelines.md`：三类 pipeline 设计 + 统一组合规则

## 一键验证

```bash
make verify    # 编译 + 测试 + 示例运行，聚合结果
```

## 本地路径覆盖

机器级覆盖不进入版本库：

```bash
# media_record/media_record/.user.bazelrc（git-ignored）
build --define=media_record_graph_runtime=/custom/path/to/graph_runtime/graph_runtime
```

（或直接编辑 `media_record_deps.bzl` 中的路径常量。）

## 下一步

- 查看架构文档：`media_record/doc/architecture/README.md`
- 查看公共接口：`media_record/doc/api/` 与 `specs/001-project-architecture/contracts/public-api.md`
