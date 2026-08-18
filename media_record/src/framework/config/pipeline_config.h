#ifndef MEDIA_RECORD_CONFIG_PIPELINE_CONFIG_H_
#define MEDIA_RECORD_CONFIG_PIPELINE_CONFIG_H_

#include <functional>
#include <string>
#include <vector>

// Pipeline template loader / validator (spec 001, US3).
//
// Templates follow the graph_runtime native JSON schema documented in
// specs/001-project-architecture/research.md §6 and
// contracts/pipeline-contract.md:
//
//   {
//     "nodes":   [ { "name", "type", "input_streams": ["tag:stream"],
//                    "output_streams": ["tag:stream"] }, ... ],
//     "streams": [ { "name", "source_node", "source_port",
//                    "dest_node", "dest_port" }, ... ]
//   }
//
// Unknown fields are tolerated (the runtime ignores them); required fields are
// enforced by the parser. Type-registration checks are decoupled from this
// module: callers supply a predicate so the validator never depends on the
// concrete registry (business nodes arrive in later features).

namespace media::record::config {

struct PipelineNodeDef {
  std::string name;
  std::string type;
  std::vector<std::string> input_streams;   // "tag:stream_name"
  std::vector<std::string> output_streams;  // "tag:stream_name"
};

struct PipelineStreamDef {
  std::string name;
  std::string source_node;
  std::string source_port;
  std::string dest_node;
  std::string dest_port;
};

struct PipelineConfig {
  std::vector<PipelineNodeDef> nodes;
  std::vector<PipelineStreamDef> streams;
};

struct ConfigStatus {
  bool ok = true;
  std::string message;

  static ConfigStatus Ok() { return ConfigStatus(); }
  static ConfigStatus Error(std::string message) {
    ConfigStatus status;
    status.ok = false;
    status.message = std::move(message);
    return status;
  }
};

// Parses pipeline JSON |json_text| into |out|. Errors carry the JSON offset.
ConfigStatus ParsePipelineConfig(const std::string& json_text,
                                 PipelineConfig* out);

// Loads and parses a pipeline template from |path|.
ConfigStatus LoadPipelineConfigFile(const std::string& path,
                                    PipelineConfig* out);

// Structural validation, independent of node implementations:
//   - unique node names (and stream names)
//   - every streams[] source/dest node references a defined node (P-2)
//   - stream source/dest port tags match the node's declared ports (P-3)
//   - every node input stream is produced by some node output stream
ConfigStatus ValidatePipelineConfig(const PipelineConfig& config);

// Registration check (P-1): every node type must be known to the registry.
// |is_registered| is called per unique type; on failure the error message
// names the offending node and its type (spec FR-009 locatable error).
ConfigStatus CheckRegisteredTypes(
    const PipelineConfig& config,
    const std::function<bool(const std::string&)>& is_registered);

// Splits "tag:stream_name" into its parts. |stream_name| is the part after the
// first ':' (or the whole string when no colon is present); |tag| is the part
// before it (empty when absent). Mirrors graph_runtime's StreamName().
void SplitPortStream(const std::string& port_stream, std::string* tag,
                     std::string* stream_name);

}  // namespace media::record::config

#endif  // MEDIA_RECORD_CONFIG_PIPELINE_CONFIG_H_
