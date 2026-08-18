# Media Record — Architecture Overview

**Version**: 0.1 | **Spec**: 001-project-architecture

## Purpose

本目录集中 media_record 架构文档：工程结构、pipeline 设计以及模块依赖关系。

## Documents

| Document | Covers |
|----------|--------|
| [engineering-structure.md](engineering-structure.md) | 工程结构：模块划分、目录约定、命名、依赖规则 |
| [pipelines.md](pipelines.md) | pipeline 设计：记录仪 / 推流 / 预览 + 统一组合规则 |

## Module Dependency Graph

```
src/examples, src/tests
      │  依赖 public + 三库公共 umbrella
      ▼
src/nodes/<name>          src/framework/public:media_record (umbrella + node.h)
      │  （业务节点，后续 feature）            ▲
      │                                      │
      ▼                                      │
src/framework/node:node（注册基础）───────────┘
      │
      ▼
三个能力仓库（graph_runtime / native_ui / video_codec）—— 仅经公共 umbrella 消费
```

## Key Decisions

- **单 workspace + 内层源码根**：源码根 `media_record/media_record/`，对齐三个依赖仓库。
- **local_repository**：三库本地路径引用，路径可覆盖（`media_record_deps.bzl` 常量 / `.user.bazelrc`）。
- **公共 API 自包含**：`src/framework/public:media_record` 为 header-only 骨架，不链接三库；跨库链接由示例 / 测试承担。
- **平台 select**：`platforms/` 提供 config_setting + platform，host 对 Android-only 能力 stub。

## See Also

- 项目 bootstrap：`doc/project_bootstrap.md`
- 规格：`specs/001-project-architecture/`
