// End-to-end recording test (spec 002, T021) + failure-path tests (T027).
//
// Drives the full 6-node recorder pipeline (graph_runtime nodes) with the real
// dashcam_record.json topology (graph_runtime JSON schema; node params come from
// each node's JSON "options" object, parsed into NodeDef.options) but a short
// 60-frame budget and a temp output file, then asserts a playable MP4 was
// produced (ftyp/moov/mdat present, non-trivial size, no leftover temp file).
// The failure tests verify that missing input, unwritable output, and an
// encoder failure each produce a locatable error (path / node name) and leave
// no partial artifact (FR-008/009).

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

#include "gtest/gtest.h"
#include "graph_runtime/config_validator.h"
#include "graph_runtime/json_parser.h"
#include "graph_runtime/runtime.h"

#include "src/framework/lifecycle/lifecycle_context.h"

namespace media::record {
namespace {

// Runs the graph with graph_runtime's own async runtime (the same inline
// lifecycle the dashcam_record entry uses): Initialize → (side packet +
// error callback) → Start → WaitUntilDone → Shutdown. Returns ok=false with a
// locatable message on any init/run failure.
struct RunnerError {
  bool ok = true;
  std::string message;
};

RunnerError RunRuntime(graph::runtime::GraphConfig config) {
  graph::runtime::GraphRuntime runtime;
  absl::Status status = runtime.Initialize(config);
  if (!status.ok()) return RunnerError{false, status.ToString()};

  media::record::LifecycleContext lifecycle_ctx;
  absl::Status sp_status = runtime.SetInputSidePacket(media::record::LifecycleContext::kSidePacketTag, graph::runtime::Packet::MakePacket<media::record::LifecycleContext*>(&lifecycle_ctx));
  if (!sp_status.ok()) return RunnerError{false, sp_status.ToString()};
  std::string execution_error;
  runtime.SetErrorCallback([&](const absl::Status& s) {
    lifecycle_ctx.pipeline_failed = true;
    if (execution_error.empty()) execution_error = s.ToString();
  });

  status = runtime.Start();
  if (!status.ok()) return RunnerError{false, status.ToString()};
  status = runtime.WaitUntilDone();
  if (!status.ok() || runtime.HasError()) {
    return RunnerError{false, !execution_error.empty()
                                  ? execution_error
                                  : (status.ok()
                                         ? absl::InternalError(
                                               "graph execution failed")
                                         : status)
                                        .ToString()};
  }
  runtime.Shutdown();
  return RunnerError{true, ""};
}

std::string Runfile(const char* rel) {
  if (const char* src = std::getenv("TEST_SRCDIR")) {
    std::string candidate = std::string(src) + "/media_record/" + rel;
    if (FILE* f = std::fopen(candidate.c_str(), "rb")) {
      std::fclose(f);
      return candidate;
    }
  }
  return rel;  // fall back to workspace-relative when run outside bazel
}

std::string TempPath(const std::string& name) {
  const char* tmp = std::getenv("TEST_TMPDIR");
  std::string dir = tmp != nullptr ? std::string(tmp) : "/tmp";
  return dir + "/" + name;
}

bool FileExists(const std::string& path) {
  FILE* f = std::fopen(path.c_str(), "rb");
  if (f) std::fclose(f);
  return f != nullptr;
}

bool HasBytes(const std::string& path, const char* magic, size_t len) {
  FILE* f = std::fopen(path.c_str(), "rb");
  if (!f) return false;
  std::vector<char> buf;
  fseek(f, 0, SEEK_END);
  const long size = ftell(f);
  fseek(f, 0, SEEK_SET);
  buf.resize(static_cast<size_t>(size));
  const size_t read = std::fread(buf.data(), 1, buf.size(), f);
  std::fclose(f);
  if (read != buf.size()) return false;
  for (size_t i = 0; i + len <= buf.size(); ++i) {
    if (std::memcmp(buf.data() + i, magic, len) == 0) return true;
  }
  return false;
}

// Patches a node option for every node of the given type. Params live in the
// config JSON's per-node "options" object; tests override the values that must
// differ per scenario (temp output path, short frame budget, failure inputs).
void PatchNodeOption(graph::runtime::GraphConfig* config,
                     const std::string& type, const std::string& key,
                     const std::string& value) {
  for (auto& def : config->nodes) {
    if (def.type == type) def.options.Set(key, value);
  }
}

void PatchNodeOption(graph::runtime::GraphConfig* config,
                     const std::string& type, const std::string& key, int value) {
  for (auto& def : config->nodes) {
    if (def.type == type) def.options.Set(key, value);
  }
}

graph::runtime::GraphConfig LoadConfig() {
  graph::runtime::JsonParser parser;
  auto parsed =
      parser.Parse(Runfile("src/examples/configs/dashcam_record.json"));
  EXPECT_TRUE(parsed.ok()) << parsed.status();
  return parsed.ok() ? std::move(*parsed) : graph::runtime::GraphConfig{};
}

// Applies the common test scenario overrides on top of the config JSON values:
// a temp output file and a short frame budget. Background/car assets and other
// DashcamRenderNode params come from the config JSON defaults.
void ApplyScenario(graph::runtime::GraphConfig* config,
                   const std::string& output, int frames) {
  PatchNodeOption(config, "DashcamRenderNode", "frame_count", frames);
  PatchNodeOption(config, "MuxerSinkNode", "output", output);
}

TEST(DashcamRecordTest, RecordsPlayableMp4) {
  const std::string output = TempPath("dashcam_record_test.mp4");
  std::remove(output.c_str());
  std::remove((output + ".tmp").c_str());

  graph::runtime::GraphConfig config = LoadConfig();
  ApplyScenario(&config, output, /*frames=*/60);
  RunnerError run = RunRuntime(std::move(config));
  ASSERT_TRUE(run.ok) << run.message;

  EXPECT_TRUE(HasBytes(output, "ftyp", 4));
  EXPECT_TRUE(HasBytes(output, "moov", 4));
  EXPECT_TRUE(HasBytes(output, "mdat", 4));

  FILE* f = std::fopen(output.c_str(), "rb");
  ASSERT_NE(f, nullptr);
  fseek(f, 0, SEEK_END);
  const long size = ftell(f);
  std::fclose(f);
  EXPECT_GT(size, 1024 * 5) << "output file suspiciously small";

  EXPECT_FALSE(FileExists(output + ".tmp")) << "temp file should be renamed away";
}

TEST(DashcamRecordTest, MissingBackgroundFailsWithPath) {
  const std::string output = TempPath("dashcam_missing_input.mp4");
  const std::string missing = "/nonexistent_dir_xyz/dashcam_road.png";
  std::remove(output.c_str());

  graph::runtime::GraphConfig config = LoadConfig();
  PatchNodeOption(&config, "DashcamRenderNode", "background", missing);
  ApplyScenario(&config, output, /*frames=*/60);
  RunnerError run = RunRuntime(std::move(config));
  ASSERT_FALSE(run.ok);
  EXPECT_NE(run.message.find(missing), std::string::npos)
      << "error must contain the input path: " << run.message;
  EXPECT_NE(run.message.find("dashcam_render"), std::string::npos);
  EXPECT_FALSE(FileExists(output)) << "no output on failure";
}

TEST(DashcamRecordTest, UnwritableOutputFailsWithPath) {
  const std::string output =
      "/nonexistent_dir_xyz/out/sub/dashcam.mp4";  // parent does not exist

  graph::runtime::GraphConfig config = LoadConfig();
  ApplyScenario(&config, output, /*frames=*/60);
  RunnerError run = RunRuntime(std::move(config));
  ASSERT_FALSE(run.ok);
  EXPECT_NE(run.message.find("muxer_sink"), std::string::npos);
  EXPECT_NE(run.message.find(output), std::string::npos)
      << "error must contain the output path: " << run.message;
  EXPECT_FALSE(FileExists(output));
  EXPECT_FALSE(FileExists(output + ".tmp"));
}

TEST(DashcamRecordTest, EncodeFailureLeavesNoPartialArtifact) {
  // An odd render dimension forces the H.264 encoder Init to fail (x264
  // rejects it), exercising the encode-failure path deterministically.
  const std::string output = TempPath("dashcam_encode_fail.mp4");
  std::remove(output.c_str());
  std::remove((output + ".tmp").c_str());

  graph::runtime::GraphConfig config = LoadConfig();
  ApplyScenario(&config, output, /*frames=*/60);
  // Render odd frames (e.g. 5x5) through the whole pipeline.
  PatchNodeOption(&config, "DashcamRenderNode", "width", 5);
  PatchNodeOption(&config, "DashcamRenderNode", "height", 5);
  PatchNodeOption(&config, "MuxerSinkNode", "width", 5);
  PatchNodeOption(&config, "MuxerSinkNode", "height", 5);
  RunnerError run = RunRuntime(std::move(config));
  ASSERT_FALSE(run.ok);
  EXPECT_NE(run.message.find("video_encoder"), std::string::npos)
      << run.message;
  EXPECT_FALSE(FileExists(output)) << "no partial output on encode failure";
  EXPECT_FALSE(FileExists(output + ".tmp")) << "no leftover temp file";
}

}  // namespace
}  // namespace media::record
