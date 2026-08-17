# Contract: Public API — media_record

**Branch**: `001-project-architecture` | **Date**: 2026-08-17 | **Spec**: [spec.md](../spec.md)

外部消费者（Bazel 项目）接入 media_record 的方式。

## 1. 依赖声明

```python
# 消费者 WORKSPACE
local_repository(
    name = "media_record",
    path = "/path/to/codes/media_record/media_record",   # 内层 workspace 目录
)
load("@media_record//:media_record_deps.bzl", "media_record_setup")
media_record_setup()
```

## 2. 消费 target

```python
deps = [
    "@media_record//:media_record",                 # root alias → //src/framework/public:media_record
]
```

## 3. Include 约定（umbrella）

```cpp
#include "media_record/media_record.h"
```

- 消费者**只允许**包含 umbrella header，禁止依赖内部头文件。
- 公共符号以 `MEDIA_RECORD_API` 导出；`-fvisibility=hidden` 下未标记符号不导出。
- 共享库构建：`bazel build //:media_record_shared --config=shared`（对齐 video_codec `--config=shared`）。

## 4. Node 注册机制（骨架）

```cpp
// media_record/node.h（骨架，仅声明）
class Node { /* Open / Process / Close 生命周期 */ };
#define REGISTER_NODE(name, Type) /* 注册表：string → factory */
```

- 业务节点实现 `Node` 契约并通过 `REGISTER_NODE` 注册，以字符串名在配置图中引用。
- 本 feature 仅交付声明与注册表占位，业务节点由后续 feature 实现。

## 5. 兼容性

- 平台相关能力（如 Android input surface）在 host 上以 stub 形式存在，host 构建不因平台差异失败。
- 公共 API 变更需遵循 Conventional Commits + semver（见 bootstrap 文档）。

## 6. 验收

- 示例消费者程序 `#include "media_record/media_record.h"` 编译链接通过（spec SC-003）。
