# Contract: Pipeline 模板

**Branch**: `001-project-architecture` | **Date**: 2026-08-17 | **Spec**: [spec.md](../spec.md)

## 1. Schema（graph_runtime 原生 JSON schema）

pipeline 模板必须遵循 graph_runtime `json_parser.cc` 的实际字段（**不使用** `calculator` / `from` / `to`）：

```json
{
  "nodes": [
    {
      "name": "node_a",
      "type": "NodeType",
      "input_streams": ["tag:stream_name"],
      "output_streams": ["tag:stream_name"],
      "input_side_packets": [],
      "output_side_packets": [],
      "options": { }
    }
  ],
  "streams": [
    {
      "name": "stream_name",
      "source_node": "node_a",
      "source_port": "output",
      "dest_node": "node_b",
      "dest_port": "tag"
    }
  ]
}
```

## 2. 模板清单

| id | 文件 | runnable（本期） | 用途 |
|----|------|------------------|------|
| recorder | `src/examples/configs/recorder.json` | 是（占位节点实现后续 feature，本期示例演示可解析运行的最小图） | 多路→布局→OSD→编码→录像 |
| stream | `src/examples/configs/stream.json` | 配置校验通过 | 编码→推流 |
| preview | `src/examples/configs/preview.json` | 配置校验通过 | 复合画面→屏幕 |

## 3. 规则

| # | 规则 |
|---|------|
| P-1 | `nodes[].type` 即 `NodeFactoryRegistry` 注册名；未注册 type 必须产生含**节点名**的可读错误 |
| P-2 | 节点 `name` 全局唯一；`streams[].source_node` / `dest_node` 必须引用已定义节点 |
| P-3 | `input_streams` / `output_streams` 用 `tag:stream_name`；tag 与节点契约端口匹配 |
| P-4 | 模板由 `pipeline_config_test` 校验（解析 + 引用合法性），不依赖节点真实实现 |
| P-5 | 三类模板命名约定：节点名小写驼峰（`cam_front`），流名小写蛇形（`front_frames`），port tag 语义化（`video` / `signal` / `input` / `output`） |

## 4. 验收

- 三个模板均可被示例 / 测试加载并解析成功（spec SC-008）。
- 含未注册 type 的模板给出可定位错误（spec FR-009）。
