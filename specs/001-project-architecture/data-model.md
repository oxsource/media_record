# Data Model: 工程架构设计及基础框架搭建

**Branch**: `001-project-architecture` | **Date**: 2026-08-17 | **Spec**: [spec.md](spec.md)

本 feature 是工程骨架，无业务持久化数据；"数据模型"描述**模块 / 依赖 / 构建 / 节点注册**四类模型。

## 1. Dependency Reference（依赖引用）

| 字段 | 类型 | 约束 / 规则 |
|------|------|-------------|
| external_name | string | Bazel 外部名：`graph_runtime` / `native_ui` / `video_codec` |
| workspace_dir | path | 指向含 `WORKSPACE` 的内层目录（不可指向仓库根） |
| public_target | string | 消费入口 target（见 research.md 表） |
| setup_fn | string | 各仓库依赖 bootstrap 函数：`graph_runtime_setup()` / `native_ui_setup()` / `video_codec_setup()` |

**关系**：`workspace_dir` 缺失或错误时，构建必须产生清晰错误（spec FR-009）。

## 2. Public API Surface（公共接口面）

| 字段 | 类型 | 约束 / 规则 |
|------|------|-------------|
| umbrella_header | path | 单一入口 `include/media_record/media_record.h` |
| export_macro | string | `MEDIA_RECORD_API`（控制符号可见性） |
| node_contract | header | `node.h`：`Node` 基类 + `REGISTER_NODE` 注册机制声明 |

**状态转移**：无（接口面为静态声明）。

## 3. Node（图执行单元）

| 字段 | 类型 | 约束 / 规则 |
|------|------|-------------|
| name | string | 注册键（配置中以字符串引用，对齐 `NodeFactoryRegistry`） |
| type | string | 节点类型（后续 feature 填充业务类型） |
| contract | NodeContract | Open/Process/Close 生命周期 |

**状态转移**：`unregistered → registered → instantiated → running → closed`。

## 4. Build Configuration（构建配置）

| 字段 | 类型 | 约束 / 规则 |
|------|------|-------------|
| platform | enum | `macos_arm64` / `macos_x86_64` / `linux_x86_64` / `linux_aarch64` / `android_arm64` |
| config_setting | target | `//platforms:<name>` |
| platform_target | target | `//platforms:<name>_platform` |
| ndk | optional | `android_arm64` 需 `@androidndk` + `--incompatible_enable_cc_toolchain_resolution=true` |

**规则**：默认开发平台 = `macos_arm64_platform`；host 构建对 Android-only 能力提供 stub。

## 5. 验证模型（冒烟验收）

| 项 | 输入 | 通过条件 |
|----|------|----------|
| 全量构建 | `bazel build //...` | 零失败 |
| 冒烟测试 | `bazel test //src/tests:all` | 三个依赖库 umbrella 链接成功 |
| 示例运行 | `bazel run //src/examples:hello_graph -- <pipeline.json>` | 退出码 0 |
| 一键验证 | `make verify` | 编译 + 测试 + 示例全部通过 |

## 6. Pipeline（澄清后新增）

| 字段 | 类型 | 约束 / 规则 |
|------|------|-------------|
| id | enum | `recorder` / `stream` / `preview` |
| template_path | path | `src/examples/configs/<id>.json`，遵循 graph_runtime JSON schema（见 research.md §6） |
| node_types | list[string] | 模板引用的节点 type 集合，须全部已在 `NodeFactoryRegistry` 注册 |
| runnable | bool | 端到端可运行（`recorder` 为真；`stream` / `preview` 依赖后续节点实现，本期仅配置校验） |

**关系**：每个 pipeline 模板由 `nodes[]` + `streams[]` 构成；节点 type 解析失败时示例必须给出**可定位到节点名的提示**（spec FR-009）。

**校验规则**：节点名唯一、type 已注册、stream 的 source/dest 引用已定义节点、tag 与节点输入输出 port 匹配（`pipeline_config_test` 覆盖）。

**状态转移**：`template defined → validated → (runnable) executed → closed`。
