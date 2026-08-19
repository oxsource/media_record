// Dashcam recording entry (spec 002 / contracts/public-api.md).
//
//   bazel run //src/examples:dashcam_record
//
// Loads the default single-cam config (dashcam_record.json), runs the recorder
// pipeline for the default 10s at 30fps, and writes out/dashcam.mp4. Overwrites
// an existing output with a log notice; any failure prints a locatable error to
// stderr and exits non-zero without leaving a partial artifact (FR-008/009).
//
// Exit codes: 0 success; 1 runtime failure (input/output/encode/mux);
//            2 argument error.

#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <sys/stat.h>
#include <unistd.h>

#include "media_record/node.h"
#include "native_ui/render.h"
#include "src/framework/config/pipeline_config.h"
#include "src/framework/transport/pipeline_runner.h"
#include "src/framework/transport/recording_defaults.h"

namespace {

void PrintUsage(const char* argv0) {
  std::printf(
      "usage: %s [--config=FILE] [--image=FILE] [--output=FILE] [--frames=N]\n"
      "  default run: 10s @ 30fps from src/examples/configs/dashcam_record.json\n"
      "  with src/examples/assets/dashcam_default.png -> out/dashcam.mp4\n",
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
  using media::record::NodeRegistry;
  using media::record::NodeStatus;
  using media::record::PipelineRunner;
  using media::record::RecordingDefaults;
  using media::record::Defaults;
  using media::record::config::CheckRegisteredTypes;
  using media::record::config::ConfigStatus;
  using media::record::config::LoadPipelineConfigFile;
  using media::record::config::PipelineConfig;
  using media::record::config::ValidatePipelineConfig;

  std::string config_path = "src/examples/configs/dashcam_record.json";
  std::string image_path = "src/examples/assets/dashcam_default.png";
  std::string output_path = "out/dashcam.mp4";
  int frames_override = -1;

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
      image_path = img;
    } else if (!out.empty()) {
      output_path = out;
    } else if (!fr.empty()) {
      frames_override = std::atoi(fr.c_str());
    } else {
      std::fprintf(stderr, "error: unknown argument '%s'\n", arg.c_str());
      PrintUsage(argv[0]);
      return 2;
    }
  }

  RecordingDefaults& d = Defaults();
  d.input_image = image_path;
  d.output_file = output_path;

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

  // Resolve input-image dimensions for the muxer stream metadata (FR: default
  // resolution follows the input image).
  {
    std::unique_ptr<native::ui::Image> image =
        native::ui::Image::FromFile(image_path.c_str());
    if (!image) {
      std::fprintf(stderr, "error: cannot decode default input image: '%s'\n",
                   image_path.c_str());
      return 1;
    }
    d.width = image->width();
    d.height = image->height();
  }

  if (!MkdirParents(output_path)) {
    std::fprintf(stderr, "error: cannot create output directory for '%s'\n",
                 output_path.c_str());
    return 1;
  }

  PipelineConfig config;
  ConfigStatus status = LoadPipelineConfigFile(config_path, &config);
  if (!status.ok) {
    std::fprintf(stderr, "error: %s\n", status.message.c_str());
    return 1;
  }
  status = ValidatePipelineConfig(config);
  if (!status.ok) {
    std::fprintf(stderr, "error: %s\n", status.message.c_str());
    return 1;
  }
  status = CheckRegisteredTypes(config, [](const std::string& type) {
    return NodeRegistry::Instance().Contains(type);
  });
  if (!status.ok) {
    std::fprintf(stderr, "error: %s\n", status.message.c_str());
    return 1;
  }

  const int frames =
      frames_override > 0 ? frames_override : d.duration_seconds * d.fps;
  PipelineRunner runner(config, frames);
  NodeStatus run = runner.Run();
  if (!run.ok) {
    std::fprintf(stderr, "error: %s\n", run.message.c_str());
    return 1;
  }

  std::printf("[dashcam_record] recorded %d frames -> %s\n", frames,
              output_path.c_str());
  return 0;
}
