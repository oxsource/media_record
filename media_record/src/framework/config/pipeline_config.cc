#include "src/framework/config/pipeline_config.h"

#include <fstream>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#include "src/framework/config/json.h"

namespace media::record::config {

void SplitPortStream(const std::string& port_stream, std::string* tag,
                     std::string* stream_name) {
  const size_t pos = port_stream.find(':');
  if (pos == std::string::npos) {
    if (tag != nullptr) tag->clear();
    if (stream_name != nullptr) *stream_name = port_stream;
    return;
  }
  if (tag != nullptr) *tag = port_stream.substr(0, pos);
  if (stream_name != nullptr) *stream_name = port_stream.substr(pos + 1);
}

namespace {

constexpr char kNodes[] = "nodes";
constexpr char kStreams[] = "streams";
constexpr char kName[] = "name";
constexpr char kType[] = "type";
constexpr char kInputStreams[] = "input_streams";
constexpr char kOutputStreams[] = "output_streams";
constexpr char kSourceNode[] = "source_node";
constexpr char kSourcePort[] = "source_port";
constexpr char kDestNode[] = "dest_node";
constexpr char kDestPort[] = "dest_port";

bool ReadStringArray(const JsonValue& value, const char* key,
                     std::vector<std::string>* out) {
  const JsonValue* array = value.Find(key);
  if (array == nullptr || !array->IsArray()) return true;
  out->clear();
  for (const JsonValue& element : array->AsArray()) {
    std::string s;
    if (!element.GetString(&s)) return false;
    out->push_back(s);
  }
  return true;
}

ConfigStatus ParseNodes(const JsonValue& root, PipelineConfig* out) {
  const JsonValue* nodes = root.Find(kNodes);
  if (nodes == nullptr || !nodes->IsArray()) {
    return ConfigStatus::Error("pipeline config: missing 'nodes' array");
  }
  for (const JsonValue& node_json : nodes->AsArray()) {
    if (!node_json.IsObject()) {
      return ConfigStatus::Error("pipeline config: node entry is not an object");
    }
    const std::string name = node_json.GetStringOr(kName, "");
    if (name.empty()) {
      return ConfigStatus::Error("pipeline config: node 'name' is required");
    }
    const std::string type = node_json.GetStringOr(kType, "");
    if (type.empty()) {
      return ConfigStatus::Error(
          "pipeline config: node '" + name + "': 'type' is required");
    }
    PipelineNodeDef def;
    def.name = name;
    def.type = type;
    if (!ReadStringArray(node_json, kInputStreams, &def.input_streams) ||
        !ReadStringArray(node_json, kOutputStreams, &def.output_streams)) {
      return ConfigStatus::Error(
          "pipeline config: node '" + name + "': stream lists must be string arrays");
    }
    out->nodes.push_back(std::move(def));
  }
  return ConfigStatus::Ok();
}

ConfigStatus ParseStreams(const JsonValue& root, PipelineConfig* out) {
  const JsonValue* streams = root.Find(kStreams);
  if (streams == nullptr || !streams->IsArray()) return ConfigStatus::Ok();
  for (const JsonValue& stream_json : streams->AsArray()) {
    if (!stream_json.IsObject()) {
      return ConfigStatus::Error("pipeline config: stream entry is not an object");
    }
    PipelineStreamDef def;
    def.name = stream_json.GetStringOr(kName, "");
    def.source_node = stream_json.GetStringOr(kSourceNode, "");
    def.source_port = stream_json.GetStringOr(kSourcePort, "");
    def.dest_node = stream_json.GetStringOr(kDestNode, "");
    def.dest_port = stream_json.GetStringOr(kDestPort, "");
    out->streams.push_back(std::move(def));
  }
  return ConfigStatus::Ok();
}

const PipelineNodeDef* FindNode(const PipelineConfig& config,
                                const std::string& name) {
  for (const PipelineNodeDef& node : config.nodes) {
    if (node.name == name) return &node;
  }
  return nullptr;
}

// Port (tag) declared on a node for the given direction. Returns empty when the
// tag is not present. Mirrors graph_runtime connectivity semantics: tag matches
// are resolved by stream_name, and the tag is the port on the node contract.
bool NodeHasPort(const PipelineNodeDef& node, const std::string& port,
                 const std::string& stream_name, bool input) {
  const std::vector<std::string>& streams =
      input ? node.input_streams : node.output_streams;
  for (const std::string& port_stream : streams) {
    std::string tag;
    std::string name;
    SplitPortStream(port_stream, &tag, &name);
    if (name == stream_name && tag == port) return true;
  }
  return false;
}

}  // namespace

ConfigStatus ParsePipelineConfig(const std::string& json_text,
                                 PipelineConfig* out) {
  JsonValue root;
  std::string error;
  if (!ParseJson(json_text, &root, &error)) {
    return ConfigStatus::Error("pipeline config: " + error);
  }
  if (!root.IsObject()) {
    return ConfigStatus::Error("pipeline config: root must be a JSON object");
  }

  PipelineConfig config;
  ConfigStatus status = ParseNodes(root, &config);
  if (!status.ok) return status;
  status = ParseStreams(root, &config);
  if (!status.ok) return status;

  *out = std::move(config);
  return ConfigStatus::Ok();
}

ConfigStatus LoadPipelineConfigFile(const std::string& path,
                                    PipelineConfig* out) {
  std::ifstream file(path);
  if (!file.is_open()) {
    return ConfigStatus::Error("pipeline config: cannot open file: " + path);
  }
  std::stringstream buffer;
  buffer << file.rdbuf();
  return ParsePipelineConfig(buffer.str(), out);
}

ConfigStatus ValidatePipelineConfig(const PipelineConfig& config) {
  // Unique node names (P-2 / graph_runtime ConfigValidator parity).
  std::set<std::string> node_names;
  for (const PipelineNodeDef& node : config.nodes) {
    if (!node_names.insert(node.name).second) {
      return ConfigStatus::Error(
          "pipeline config: duplicate node name: '" + node.name + "'");
    }
  }

  // Stream names within the explicit streams[] array must be unique.
  std::set<std::string> stream_names;
  for (const PipelineStreamDef& stream : config.streams) {
    if (!stream_names.insert(stream.name).second) {
      return ConfigStatus::Error(
          "pipeline config: duplicate stream name: '" + stream.name + "'");
    }
  }

  // Every streams[] source/dest node must exist (P-2) and the port tags must
  // match the node's declared input/output ports (P-3).
  for (const PipelineStreamDef& stream : config.streams) {
    const PipelineNodeDef* source = FindNode(config, stream.source_node);
    if (source == nullptr) {
      return ConfigStatus::Error("pipeline config: stream '" + stream.name +
                                 "': source_node '" + stream.source_node +
                                 "' is not a defined node");
    }
    const PipelineNodeDef* dest = FindNode(config, stream.dest_node);
    if (dest == nullptr) {
      return ConfigStatus::Error("pipeline config: stream '" + stream.name +
                                 "': dest_node '" + stream.dest_node +
                                 "' is not a defined node");
    }
    if (!NodeHasPort(*source, stream.source_port, stream.name, /*input=*/false)) {
      return ConfigStatus::Error(
          "pipeline config: stream '" + stream.name + "': source node '" +
          source->name + "' has no output port '" + stream.source_port +
          "' producing stream '" + stream.name + "'");
    }
    if (!NodeHasPort(*dest, stream.dest_port, stream.name, /*input=*/true)) {
      return ConfigStatus::Error(
          "pipeline config: stream '" + stream.name + "': dest node '" +
          dest->name + "' has no input port '" + stream.dest_port +
          "' consuming stream '" + stream.name + "'");
    }
  }

  // Connectivity (graph_runtime ConfigValidator parity): every node input
  // stream must be produced by some node output stream.
  std::set<std::string> produced;
  for (const PipelineNodeDef& node : config.nodes) {
    for (const std::string& port_stream : node.output_streams) {
      std::string stream_name;
      SplitPortStream(port_stream, nullptr, &stream_name);
      produced.insert(stream_name);
    }
  }
  for (const PipelineNodeDef& node : config.nodes) {
    for (const std::string& port_stream : node.input_streams) {
      std::string stream_name;
      SplitPortStream(port_stream, nullptr, &stream_name);
      if (produced.count(stream_name) == 0) {
        return ConfigStatus::Error(
            "pipeline config: node '" + node.name + "': input stream '" +
            stream_name + "' has no matching output stream in any node");
      }
    }
  }

  return ConfigStatus::Ok();
}

ConfigStatus CheckRegisteredTypes(
    const PipelineConfig& config,
    const std::function<bool(const std::string&)>& is_registered) {
  for (const PipelineNodeDef& node : config.nodes) {
    if (!is_registered(node.type)) {
      return ConfigStatus::Error(
          "pipeline config: node '" + node.name +
          "' references unregistered node type '" + node.type + "'");
    }
  }
  return ConfigStatus::Ok();
}

}  // namespace media::record::config
