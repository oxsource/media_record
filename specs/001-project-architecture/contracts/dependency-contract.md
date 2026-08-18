# Contract: Dependency — 本地仓库引用

**Branch**: `001-project-architecture` | **Date**: 2026-08-17 | **Spec**: [spec.md](../spec.md)

## 1. 三个外部仓库的本地引用

> Bazel 不允许在函数体内 `load()`，故仓库注册与各库 setup 拆成两个文件：
> `media_record_deps.bzl`（注册 local_repository）→ `media_record_setup.bzl`（调三库 `*_setup()`）。
> WORKSPACE 必须先调 `media_record_deps()`，再 load `media_record_setup.bzl`。

```python
# WORKSPACE
workspace(name = "media_record")

load("//:media_record_deps.bzl", "media_record_deps")

media_record_deps()

load("//:media_record_setup.bzl", "media_record_setup")

media_record_setup()
```

```python
# media_record_deps.bzl（路径可通过 .user.bazelrc / 变量覆盖）
def media_record_deps():
    if not native.existing_rule("graph_runtime"):
        native.local_repository(
            name = "graph_runtime",
            path = "/Users/moks/Develop/docker/ubuntu24/codes/graph_runtime/graph_runtime",
        )
    if not native.existing_rule("native_ui"):
        native.local_repository(
            name = "native_ui",
            path = "/Users/moks/Develop/docker/ubuntu24/codes/native_ui/native_ui",
        )
    if not native.existing_rule("video_codec"):
        native.local_repository(
            name = "video_codec",
            path = "/Users/moks/Develop/docker/ubuntu24/codes/video_codec/codec",
        )
```

```python
# media_record_setup.bzl（各仓库自身的 http_archive 依赖）
load("@graph_runtime//:graph_runtime_deps.bzl", "graph_runtime_setup")
load("@native_ui//:native_ui_deps.bzl", "native_ui_setup")
load("@video_codec//:video_codec_deps.bzl", "video_codec_setup")

def media_record_setup():
    graph_runtime_setup()
    native_ui_setup()
    video_codec_setup()
```

> **video_codec 的 foreign_cc 传递依赖**：video_codec 用 `rules_foreign_cc` 从源码构建
> FFmpeg。其 `video_codec_setup()` 只注册 `rules_foreign_cc` 仓库；`rules_foreign_cc_dependencies()`
> （注册 `rules_python` 等）必须在仓库已存在后再调用，故 WORKSPACE 在 `media_record_setup()`
> 之后追加一阶段（镜像 video_codec 自身 WORKSPACE）：
>
> ```python
> load("@rules_foreign_cc//foreign_cc:repositories.bzl", "rules_foreign_cc_dependencies")
>
> rules_foreign_cc_dependencies(
>     register_built_tools = False,
>     register_built_pkgconfig_toolchain = False,
> )
> ```

## 2. 契约规则

| # | 规则 |
|---|------|
| D-1 | `local_repository` 的 `path` 必须指向含 `WORKSPACE` 的内层目录（见 research.md §1），指向仓库根会构建失败 |
| D-2 | 三个仓库的消费 target 固定：`@graph_runtime//src/framework/public:runtime`、`@native_ui//:native_ui`、`@video_codec//src/framework/public:video_codec` |
| D-3 | 本工程 BUILD `deps` 使用 `@media_record//` 前缀（对齐 `@<repo>//` 约定），禁止 `//` 直接引用外部仓库内部模块 |
| D-4 | 首次构建仍需网络（各仓库 transitive http_archive）；后续由 bazel repository cache / `--repository_cache` 复用 |
| D-5 | 路径按机器可覆盖：默认常量放 `media_record_deps.bzl`，机器级覆盖走 git-ignored `.user.bazelrc` |
| D-6 | 宿主构建对 Android-only 能力提供 stub，保证 host 全量构建可用（spec FR-005 / SC-004） |

## 3. 验收

- 离线首次构建（本地已缓存依赖后）成功（spec SC-002）。
- 依赖路径配置错误时给出可读错误，能定位到具体仓库（spec FR-009）。
