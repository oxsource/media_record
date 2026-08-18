# 工程结构设计（Engineering Structure）

> Spec 001（001-project-architecture）| 与 `doc/project_bootstrap.md` 约定一致

## 1. 仓库布局

```
media_record/                         (repo root)
├── AGENTS.md
├── CHANGELOG.md
├── specs/                            (spec-kit 规格目录)
└── media_record/                     (源码根，workspace(name = "media_record"))
```

源码根为仓库同名内层目录（对齐 graph_runtime / native_ui / video_codec 布局），`doc/` 与 `specs/` 分别位于源码根与仓库根。

## 2. 模块划分

| 目录 | 职责 | 依赖 |
|------|------|------|
| `src/framework/public/` | 公共 API（umbrella + export 宏 + node.h） | 无（自包含骨架） |
| `src/framework/node/` | 节点注册基础 | `public` |
| `src/framework/config/` | pipeline 模板加载 / 校验（JSON 解析 + 引用/port 校验） | 无（stdlib 自包含） |
| `src/nodes/<name>/` | 业务节点骨架（10 个） | `public`（后续 feature 接三库） |
| `src/examples/` | 示例（consumer_demo / hello_graph）+ pipeline 模板 | `public` + `config` |
| `src/tests/` | 冒烟 / 配置校验测试 | `public` + `config` + 三库 |
| `platforms/` | 平台 config_setting + platform | — |
| `mk/` + `Makefile` | make 验证入口 | — |
| `doc/architecture/` | 架构文档 | — |

## 3. 目录与命名约定

- **文件**：`lowercase` + 下划线（`stream_input_node.h`）；BUILD 一律 `BUILD.bazel`。
- **节点目录**：`src/nodes/<snake_name>/`，头文件 `<name>_node.h`，target 名为目录名。
- **公共头文件**：`src/framework/public/include/media_record/`，`strip_include_prefix = "include"`，消费者 `#include "media_record/..."`。
- **命名空间**：`namespace media::record`。
- **导出宏**：`MEDIA_RECORD_API`（仅公共符号）。

## 4. 依赖规则

- 所有 BUILD `deps` 使用 `@media_record//` 前缀。
- 只依赖三库公共 umbrella target：`@graph_runtime//src/framework/public:runtime`、`@native_ui//:native_ui`、`@video_codec//src/framework/public:video_codec`。
- 平台相关能力使用 `media_record_select()` 按平台分支；host 上 Android-only 能力提供 stub。

## 5. 节点骨架约定（本期交付）

每个节点骨架 = `BUILD.bazel`（空 `cc_library`）+ `<name>_node.h`（占位声明）。业务实现由后续 feature 补齐：
1. 实现 `Node` 契约（`src/framework/public/include/media_record/node.h`）。
2. 通过 `REGISTER_NODE("NodeType", ClassName)` 注册。
3. 按需依赖三库公共 target 与 `//src/framework/public:media_record`。
