// Dashcam recording entry (spec 002 / contracts/public-api.md).
//
//   bazel run //src/examples:dashcam_record
//
// Loads the default single-cam config (dashcam_record.json, graph_runtime JSON
// schema). Node params (image/output/fps/frame_count/bitrate/format) live in
// each node's "options" object of the config file — graph_runtime's own
// JsonParser turns them into GraphConfig::NodeDef::options, and each node
// stores them in its own config data structure (NodeOptions) at construction.
// Optional CLI flags (--image/--output/--frames) are applied via graph_runtime's
// node-option injection (GraphRuntime::Options, a per-node-type overrides map
// merged by Initialize(config, options)) on top of the file-based config.
//
// This entry drives graph_runtime's own async runtime inline (no dedicated
// runner module): Initialize (wires the node-to-node streams) → Start →
// WaitUntilDone (blocks until the source node exhausts its frame budget) →
// Shutdown. It writes out/dashcam.mp4, overwriting an existing output with a
// log notice; any failure prints a locatable error to stderr and exits non-zero
// without leaving a partial artifact (FR-008/009). A local pipeline_failed flag
// is handed to sink nodes as a side packet so MuxerSinkNode::Close discards
// partial output on failure (FR-009).
//
// Exit codes: 0 success; 1 runtime failure (input/output/encode/mux);
//            2 argument error.

#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <string>
#include <sys/stat.h>
#include <unistd.h>

#include "graph_runtime/config_validator.h"
#include "graph_runtime/json_parser.h"
#include "graph_runtime/node_registry.h"
#include "graph_runtime/runtime.h"

namespace {

void PrintUsage(const char* argv0) {
  std::printf(
      "usage: %s [--config=FILE] [--image=FILE] [--output=FILE] [--frames=N]\n"
      "  default run: 10s @ 30fps from src/examples/configs/dashcam_record.json\n"
      "  (node params come from each node's JSON 'options' object)\n"
      "  -> out/dashcam.mp4\n",
      argv0);
}

std::string TakeFlag(const std::string& arg, const char* prefix) {
  const std::string p = std::string("--") + prefix + "=";
  return arg.rfind(p, 0) == 0 ? arg.substr(p.size()) : std::string();
}

// Best-effort mkdir for the output parent directory ("out" for the default).
bool MkdirParents(const std::string& path) {
  std::string cur;
  for (char c : path) {
    if (c == '/') {
      if (!cur.empty() && ::mkdir(cur.c_str(), 0755) != 0 && errno != EEXIST) {
        return false;
      }
    }
    cur += c;
  }
  return true;
}

}  // namespace

int main(int argc, char** argv) {
  std::string config_path = "src/examples/configs/dashcam_record.json";
  std::string image_override;   // empty = keep the config value
  std::string output_override;  // empty = keep the config value
  int frames_override = -1;     // < 0 = keep the config frame budget

  for (int i = 1; i < argc; ++i) {
    const std::string arg = argv[i];
    if (arg == "--help" || arg == "-h") {
      PrintUsage(argv[0]);
      return 0;
    }
    const std::string cfg = TakeFlag(arg, "config");
    const std::string img = TakeFlag(arg, "image");
    const std::string out = TakeFlag(arg, "output");
    const std::string fr = TakeFlag(arg, "frames");
    if (!cfg.empty()) {
      config_path = cfg;
    } else if (!img.empty()) {
      image_override = img;
    } else if (!out.empty()) {
      output_override = out;
    } else if (!fr.empty()) {
      frames_override = std::atoi(fr.c_str());
    } else {
      std::fprintf(stderr, "error: unknown argument '%s'\n", arg.c_str());
      PrintUsage(argv[0]);
      return 2;
    }
  }

  // `bazel run` executes with cwd = the target's runfiles copy of the workspace
  // root, so relative paths (config/image/out/) would land in the build tree.
  // BUILD_WORKSPACE_DIRECTORY names the real workspace; chdir there so the
  // default output `out/dashcam.mp4` appears in the project root.
  if (const char* ws = std::getenv("BUILD_WORKSPACE_DIRECTORY")) {
    if (::chdir(ws) != 0) {
      std::fprintf(stderr, "error: cannot chdir to workspace '%s'\n", ws);
      return 1;
    }
  }

  // Parse the pipeline config with graph_runtime's own JSON parser. Node params
  // are carried by each node's "options" object and land in NodeDef.options.
  graph::runtime::JsonParser parser;
  auto parsed = parser.Parse(config_path);
  if (!parsed.ok()) {
    std::fprintf(stderr, "error: cannot parse '%s': %s\n", config_path.c_str(),
                 parsed.status().ToString().c_str());
    return 2;
  }
  graph::runtime::GraphConfig config = std::move(*parsed);
  const absl::Status validation = graph::runtime::ConfigValidator::Validate(config);
  if (!validation.ok()) {
    std::fprintf(stderr, "error: %s\n", validation.ToString().c_str());
    return 2;
  }
  for (const auto& def : config.nodes) {
    if (!graph::runtime::NodeFactoryRegistry::IsRegistered(def.type)) {
      std::fprintf(stderr, "error: node '%s': type '%s' is not registered\n", def.name.c_str(), def.type.c_str());
      return 2;
    }
  }

  // Resolve the recording budget and output path from the config node options
  // (graph_runtime's GraphConfig::GetNodeOption<T> reads the first node of the
  // given type with a default fallback) before constructing the runtime.
  int frame_count = config.GetNodeOption<int>("StreamInputNode", "frame_count", 300);
  if (frames_override > 0) {
    frame_count = frames_override;  // CLI --frames wins over the config.
  }
  const std::string output_path = config.GetNodeOption<std::string>("MuxerSinkNode", "output", "out/dashcam.mp4");
  if (!MkdirParents(output_path)) {
    std::fprintf(stderr, "error: cannot create output directory for '%s'\n", output_path.c_str());
    return 1;
  }

  // Build and run the graph with graph_runtime's own async runtime.
  graph::runtime::GraphRuntime runtime;

  // Optional CLI overrides patch the matching node options via graph_runtime's
  // node-option injection (GraphRuntime::Options, applied at Initialize()).
  graph::runtime::GraphRuntime::Options options;
  if (!image_override.empty()) {
    options.nodes["StreamInputNode"].Set("image", image_override);
  }
  if (!output_override.empty()) {
    options.nodes["MuxerSinkNode"].Set("output", output_override);
  }
  if (frames_override > 0) {
    options.nodes["StreamInputNode"].Set("frame_count", frames_override);
  }

  absl::Status status = runtime.Initialize(config, options);
  if (!status.ok()) {
    std::fprintf(stderr, "error: %s\n", status.ToString().c_str());
    return 1;
  }

  // A local failure flag, handed to sink nodes as a side packet so
  // MuxerSinkNode::Close discards partial output on a failed run (FR-009).
  bool pipeline_failed = false;
  absl::Status sp_status = runtime.SetInputSidePacket("pipeline_failed", graph::runtime::Packet::MakePacket<bool*>(&pipeline_failed));
  if (!sp_status.ok()) {
    std::fprintf(stderr, "error: cannot set pipeline_failed side packet: %s\n", sp_status.ToString().c_str());
    return 1;
  }

  // Any node error during execution must reach MuxerSinkNode::Close before
  // nodes are closed, so the partial artifact is dropped.
  std::string execution_error;
  runtime.SetErrorCallback([&](const absl::Status& s) {
    pipeline_failed = true;
    if (execution_error.empty()) execution_error = s.ToString();
  });

  status = runtime.Start();
  if (!status.ok()) {
    pipeline_failed = true;
    std::fprintf(stderr, "error: %s\n", status.ToString().c_str());
    return 1;
  }
  status = runtime.WaitUntilDone();
  if (!status.ok() || runtime.HasError()) {
    pipeline_failed = true;
    std::string msg;
    if (!execution_error.empty()) {
      // A node failed during execution: surface the locatable error.
      msg = execution_error;
    } else if (status.ok()) {
      msg = "graph execution failed";
    } else {
      msg = status.ToString();
    }
    std::fprintf(stderr, "error: %s\n", msg.c_str());
    return 1;
  }
  runtime.Shutdown();

  std::printf("[dashcam_record] recorded %d frames -> %s\n", frame_count, output_path.c_str());
  return 0;
}
