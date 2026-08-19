// Config-driven pipeline example (spec 001, task T030 / US3).
//
// Loads a pre-built pipeline JSON template (graph_runtime native schema),
// validates it, and assembles + executes the described node graph on
// media_record's own Node/NodeRegistry skeleton:
//
//   bazel run //src/examples:hello_graph -- --config=src/examples/configs/recorder.json
//
// Behavior:
//   - parse / structural validation errors -> readable message on stderr, exit 1
//   - a node referencing an unregistered type -> error naming node + type (FR-009)
//   - recorder.json (types registered by the placeholders below) -> graph runs,
//     exit 0
//   - stream.json / preview.json reference not-yet-implemented node types ->
//     locatable missing-node error (spec SC-008: config validation only)

#include <cstdio>
#include <cstdlib>
#include <memory>
#include <set>
#include <string>
#include <vector>

#include "media_record/media_record.h"
#include "src/framework/config/config_validator.h"
#include "src/framework/config/json/json_parser.h"

namespace media::record::examples {

// Minimal runnable placeholder nodes (spec 001 US3). Real business nodes land
// in later features; these stubs let the recorder topology assemble and execute
// end-to-end (spec SC-008) without any stream/packet plumbing.
class PlaceholderNode : public Node {
 public:
  explicit PlaceholderNode(std::string tag) : tag_(std::move(tag)) {}

  NodeStatus Open() override {
    std::printf("[hello_graph] Open    %s\n", tag_.c_str());
    return {};
  }
  NodeStatus Process() override {
    std::printf("[hello_graph] Process %s\n", tag_.c_str());
    return {};
  }
  NodeStatus Close() override {
    std::printf("[hello_graph] Close   %s\n", tag_.c_str());
    return {};
  }

 private:
  std::string tag_;
};

#define DEFINE_RECORDER_PLACEHOLDER(type_name, tag)                     \
  class type_name##Placeholder : public PlaceholderNode {               \
   public:                                                              \
    type_name##Placeholder() : PlaceholderNode(tag) {}                  \
  };                                                                    \
  REGISTER_NODE(#type_name, type_name##Placeholder)

// The recorder.json template's node types (research.md §6 / FR-003). Not
// registered here: stream_sink (StreamSinkNode), preview (PreviewNode),
// audio_encoder (AudioEncoderNode) — those pipelines are config-validation-only
// this feature (spec SC-008).
DEFINE_RECORDER_PLACEHOLDER(StreamInputNode, "stream_input");
DEFINE_RECORDER_PLACEHOLDER(MultiViewLayoutNode, "multi_view_layout");
DEFINE_RECORDER_PLACEHOLDER(UiOverlayNode, "ui_overlay");
DEFINE_RECORDER_PLACEHOLDER(VideoEncoderNode, "video_encoder");
DEFINE_RECORDER_PLACEHOLDER(RecorderNode, "recorder");
DEFINE_RECORDER_PLACEHOLDER(MuxerSinkNode, "muxer_sink");

#undef DEFINE_RECORDER_PLACEHOLDER

void PrintUsage(const char* argv0) {
  std::printf("usage: %s --config=<pipeline.json>\n", argv0);
  std::printf(
      "  loads a graph_runtime-native pipeline template, assembles and runs\n"
      "  the node graph on the media_record skeleton, then exits 0.\n");
}

}  // namespace media::record::examples

int main(int argc, char** argv) {
  using media::record::Node;
  using media::record::NodeRegistry;
  using media::record::examples::PrintUsage;

  std::string config_path = "src/examples/configs/recorder.json";
  for (int i = 1; i < argc; ++i) {
    const std::string arg = argv[i];
    if (arg == "--config" && i + 1 < argc) {
      config_path = argv[++i];
    } else if (arg.rfind("--config=", 0) == 0) {
      config_path = arg.substr(std::string("--config=").size());
    } else if (arg == "--help" || arg == "-h") {
      PrintUsage(argv[0]);
      return 0;
    } else {
      std::fprintf(stderr, "error: unknown argument '%s'\n", arg.c_str());
      PrintUsage(argv[0]);
      return 2;
    }
  }

  // Resolve the template. `bazel run` executes from the workspace root, so the
  // source-tree-relative path (src/examples/configs/...) is the canonical form.
  // Parse the template with graph_runtime's own JSON parser.
  graph::runtime::JsonParser parser;
  auto parsed = parser.Parse(config_path);
  if (!parsed.ok()) {
    std::fprintf(stderr, "error: cannot parse '%s': %s\n", config_path.c_str(),
                 parsed.status().ToString().c_str());
    return 1;
  }
  graph::runtime::GraphConfig config = std::move(*parsed);

  const absl::Status validation = graph::runtime::ConfigValidator::Validate(config);
  if (!validation.ok()) {
    std::fprintf(stderr, "error: %s\n", validation.ToString().c_str());
    return 1;
  }

  for (const auto& def : config.nodes) {
    if (!NodeRegistry::Instance().Contains(def.type)) {
      std::fprintf(stderr, "error: node '%s': type '%s' is not registered\n",
                   def.name.c_str(), def.type.c_str());
      return 1;
    }
  }

  // Count the implicit streams ("port:stream" names) for the log line.
  std::set<std::string> streams;
  for (const auto& def : config.nodes) {
    for (const std::string& is : def.input_streams) {
      const size_t colon = is.find(':');
      streams.insert(colon == std::string::npos ? is : is.substr(colon + 1));
    }
    for (const std::string& os : def.output_streams) {
      const size_t colon = os.find(':');
      streams.insert(colon == std::string::npos ? os : os.substr(colon + 1));
    }
  }

  // Assemble the graph from the config, then run the skeleton lifecycle.
  std::printf("[hello_graph] loading %s: %zu node(s), %zu stream(s)\n",
              config_path.c_str(), config.nodes.size(), streams.size());

  std::vector<std::unique_ptr<Node>> graph;
  for (const graph::runtime::GraphConfig::NodeDef& def : config.nodes) {
    std::unique_ptr<Node> node = NodeRegistry::Instance().Create(def.type);
    if (node == nullptr) {
      std::fprintf(stderr, "error: node '%s': factory returned null\n",
                   def.name.c_str());
      return 1;
    }
    graph.push_back(std::move(node));
  }

  bool ok = true;
  for (size_t i = 0; i < graph.size(); ++i) {
    media::record::NodeStatus s = graph[i]->Open();
    if (!s.ok) {
      std::fprintf(stderr, "error: node '%s' Open: %s\n",
                   config.nodes[i].name.c_str(), s.message.c_str());
      ok = false;
      break;
    }
  }
  for (size_t i = 0; ok && i < graph.size(); ++i) {
    media::record::NodeStatus s = graph[i]->Process();
    if (!s.ok) {
      std::fprintf(stderr, "error: node '%s' Process: %s\n",
                   config.nodes[i].name.c_str(), s.message.c_str());
      ok = false;
      break;
    }
  }
  for (size_t i = 0; i < graph.size(); ++i) {
    (void)graph[i]->Close();
  }

  if (!ok) return 1;
  std::printf("[hello_graph] graph executed (%zu node(s)), exiting 0\n",
              graph.size());
  return 0;
}
