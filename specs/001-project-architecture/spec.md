# Feature Specification: 工程架构设计及基础框架搭建

**Feature Branch**: `001-project-architecture`

**Created**: 2026-08-17

**Status**: Draft

**Input**: User description: "工程架构设计及基础框架搭建，上述提到的三个仓库依赖先以local_repository引用"

## Clarifications

### Session 2026-08-17

- Q: 本期架构设计覆盖哪些 pipeline？ → A: 覆盖全部核心 pipeline（记录仪 / 推流 / 预览）+ 统一组合规则
- Q: 工程结构骨架铺到什么深度？ → A: 全量节点骨架（`src/nodes/` 下 9 个节点目录各带空 BUILD + 头部占位，可编译）
- Q: 工程结构与 pipeline 设计文档落在哪里？ → A: `media_record/doc/architecture/`
- Q: pipeline 如何表达/落地？ → A: 每类 pipeline 预置 JSON 配置模板 + 示例引用，配套交付

## User Scenarios & Testing *(mandatory)*

### User Story 1 - 可构建的工程骨架（Priority: P1）

开发者在新机器上拿到本仓库后，仅需执行构建命令即可得到一个**编译通过**的工程骨架。工程通过本地路径引用三个外部能力仓库（graph_runtime / native_ui / video_codec），整个过程**不依赖任何网络下载**。

**Why this priority**: 这是后续一切功能开发的前提——没有可编译的工程骨架，任何节点、示例、测试都无法落地。

**Independent Test**: 在干净检出上执行工程构建命令（如 `bazel build //...`），全部目标编译成功即通过。

**Acceptance Scenarios**:

1. **Given** 一份干净的仓库检出与正确的本地依赖路径配置，**When** 执行全量构建，**Then** 所有基础库目标编译成功，无报错
2. **Given** 网络不可用（离线环境），**When** 执行构建，**Then** 构建仍成功，三个依赖仓库从本地解析，不发起任何网络拉取
3. **Given** 工程已构建成功，**When** 查看构建产物，**Then** 存在可供其他 Bazel 项目消费的公共库目标

---

### User Story 2 - 公共 API 与基础框架模块（Priority: P1）

开发者可以通过一个**统一的公共头文件入口**使用本框架的基础能力，并且内部模块（输入 / 渲染 / 编码 / 录制等）已按既定目录结构铺好**全量骨架**，每个规划节点都有可编译的占位目录，后续功能可在此骨架内迭代。

**Why this priority**: 公共 API 入口决定了外部消费者如何接入；模块骨架决定了团队如何组织后续开发，二者构成"基础框架搭建"的主体。

**Independent Test**: 编写一个消费公共头文件的最小程序，编译链接通过；同时目录结构符合 `media_record/doc/project_bootstrap.md` 的约定，且 `src/nodes/` 下全部节点骨架目录可编译。

**Acceptance Scenarios**:

1. **Given** 一个仅包含公共头文件的消费者程序，**When** 编译并链接本工程公共库，**Then** 成功，无缺失符号
2. **Given** 基础框架已搭建，**When** 查看仓库目录，**Then** 包含 `src/framework/public`、`src/nodes`、`examples`、`tests`、`mk`、`doc` 等约定结构
3. **Given** 模块骨架存在，**When** 查看公共 API，**Then** 提供节点注册与基础类型等最小可用接口
4. **Given** 全量节点骨架已铺好，**When** 执行全量构建，**Then** `src/nodes/` 下全部节点占位目录编译通过

---

### User Story 3 - 冒烟验证与可运行示例（Priority: P2）

开发者可以运行一个**最小示例**，验证"三个仓库依赖 + 本工程骨架"组合链路已打通：示例程序加载一份预置的 pipeline JSON 模板，构建并执行由配置驱动的节点图并正常结束。

**Why this priority**: 冒烟验证是"基础框架搭建"闭环的验收点——证明依赖解析、模块组织、运行入口三者可用，而不只是编译通过。

**Independent Test**: 运行示例程序并检查其退出码与输出；执行 `make verify` 得到通过结果。

**Acceptance Scenarios**:

1. **Given** 示例程序已构建，**When** 运行它，**Then** 进程正常启动、执行、退出（退出码 0）
2. **Given** 示例程序运行，**When** 传入一份预置 pipeline JSON 模板，**Then** 图被正确构建并执行
3. **Given** 工程骨架完成，**When** 执行 `make verify`，**Then** 编译、测试、示例全部通过并输出汇总

---

### Edge Cases

- 本地依赖路径不存在或配置错误时，构建应给出**清晰可读的错误提示**，而非晦涩的解析失败。
- 平台不支持的功能（如 Android 专属能力在 host 上）应提供占位 / 降级实现，保持 host 构建可用。
- 三个依赖仓库版本与本项目要求不兼容时，构建失败信息应能定位到具体仓库。
- 多平台构建时（macOS / Linux / Android），平台选择逻辑应保证同一套代码在各平台正确分支。
- pipeline JSON 模板引用了尚未实现的节点时，示例应给出**明确的缺失节点提示**（定位到具体节点名），而非崩溃或无意义报错。

## Requirements *(mandatory)*

### Functional Requirements

- **FR-001**: 工程 MUST 通过本地路径（local repository）方式引用 graph_runtime、native_ui、video_codec 三个依赖仓库，路径 MUST 可配置且不依赖网络。
- **FR-002**: 工程 MUST 提供可被外部 Bazel 项目消费的公共库目标，并暴露统一的公共头文件入口（umbrella header）。
- **FR-003**: 工程 MUST 按 `media_record/doc/project_bootstrap.md` 中约定的目录结构搭建基础框架骨架，且 `src/nodes/` 下 MUST 预置全部规划节点的骨架目录（stream_input / signal_source / multi_view_layout / ui_overlay / video_encoder / audio_encoder / recorder / muxer_sink / stream_sink / preview），每个目录 MUST 含可编译的空 BUILD 与头部占位。
- **FR-004**: 工程 MUST 提供节点注册的基础机制，允许后续以插件化方式加入新节点，而无需修改运行时。
- **FR-005**: 工程 MUST 提供平台感知的构建选择（macOS / Linux / Android），不支持平台的功能在 host 上 MUST 有占位 / 降级。
- **FR-006**: 工程 MUST 提供配置驱动的可运行示例，示例 MUST 通过加载预置 pipeline JSON 模板装配节点图并正常结束。
- **FR-007**: 工程 MUST 提供自动化验证入口（编译 + 测试 + 示例的一键验证），供开发与 CI 使用。
- **FR-008**: 工程 MUST 在 `media_record/doc/architecture/` 提供架构文档，内容 MUST 包含工程结构设计与 pipeline 设计（记录仪 / 推流 / 预览三类 pipeline + 统一组合规则）。
- **FR-009**: 依赖路径错误、平台不支持等情况 MUST 产生清晰可读的失败提示；pipeline 模板引用未实现节点时 MUST 给出可定位到节点名的明确提示。
- **FR-010**: 架构设计 MUST 覆盖全部核心 pipeline：记录仪（多路输入→布局→OSD→编码→录像）、推流（编码→RTMP/WebRTC）、预览（复合画面→屏幕），并 MUST 归纳统一的 pipeline 组合规则（节点、流、旁路事件的连接约定）。
- **FR-011**: 工程 MUST 为每类 pipeline 提供预置 JSON 配置模板（如 recorder.json / stream.json / preview.json），示例 MUST 引用这些模板运行。

### Key Entities

- **Dependency Reference**: 三个外部仓库（graph_runtime / native_ui / video_codec）的本地路径引用，可配置、可替换。
- **Public API Surface**: 对外暴露的统一接口层，外部消费者仅通过它接入。
- **Node**: 图执行的最小单元，通过注册机制进入框架，构成后续所有功能的基础。
- **Pipeline**: 由预置 JSON 模板描述的节点组合蓝图（记录仪 / 推流 / 预览），是架构设计核心产物。
- **Build Configuration**: 平台选择、编译选项、目标别名等构建期配置。

## Success Criteria *(mandatory)*

### Measurable Outcomes

- **SC-001**: 在干净检出上，一条命令即可完成全量构建且零失败。
- **SC-002**: 离线环境下（无网络）构建仍成功，三个依赖全部从本地解析。
- **SC-003**: 公共库可被外部示例程序编译链接并成功运行（退出码 0）。
- **SC-004**: 最小示例在 macOS 与 Linux 上均可构建运行，Android 平台至少可完成交叉编译配置。
- **SC-005**: 一键验证入口（编译 + 测试 + 示例）在新增节点或模块后仍保持通过。
- **SC-006**: 基础框架目录结构与 bootstrap 文档约定一致，差异为零。
- **SC-007**: `src/nodes/` 全量节点骨架目录均可编译通过（空实现占位，无真实业务逻辑）。
- **SC-008**: 至少一条 pipeline（记录仪）模板可端到端运行并正常退出；其余 pipeline 模板通过配置校验（能正确解析、引用节点名有效）。
- **SC-009**: `media_record/doc/architecture/` 架构文档包含三类 pipeline 的节点连线与数据流描述，且与 bootstrap 的节点清单一致。

## Assumptions

- 开发环境已具备 Bazel 6.5.x（含 bazelisk）与对应平台工具链。
- 三个依赖仓库已存在于本机已知路径（路径通过配置文件可覆盖），且各自公共 API 已按 `graph_runtime/graph_runtime.h`、`native_ui/native_ui.h`（含各子模块）、`video_codec/video_codec.h` 暴露。
- 一期以 macOS 为主要开发宿主，Linux / Android 通过平台选择机制支持。
- 本 feature 只搭建"架构 + 骨架"，不实现具体业务节点逻辑；节点以可编译占位形态存在，真实节点（采集 / OSD / 编码 / 录制 / 推流 / 预览）由后续 feature 实现。
- 推流 / 预览等 pipeline 的端到端可运行依赖后续节点实现；本期交付设计文档 + JSON 模板 + 配置校验。
- 公共 API 入口与内部模块组织遵循 `media_record/doc/project_bootstrap.md` 的设计约定。
