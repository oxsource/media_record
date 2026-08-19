// End-to-end recording test (spec 002, T021) + failure-path tests (T027).
//
// Drives the full 7-node recorder pipeline with the real dashcam_record.json
// topology but a short 2s / 60-frame budget and a temp output file, then
// asserts a playable MP4 was produced (ftyp/moov/mdat present, non-trivial
// size, no leftover temp file). The failure tests verify that missing input,
// unwritable output, and an encoder failure each produce a locatable error
// (path / node name) and leave no partial artifact (FR-008/009).

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>
#include <vector>
#include <zlib.h>

#include "gtest/gtest.h"
#include "media_record/node.h"
#include "native_ui/render.h"
#include "src/framework/config/pipeline_config.h"
#include "src/framework/transport/pipeline_runner.h"
#include "src/framework/transport/recording_defaults.h"

namespace media::record {
namespace {

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

// Resets the process-wide defaults and sets the common success-path values.
void SetDefaults(const std::string& image, const std::string& output) {
  RecordingDefaults& d = Defaults();
  d = RecordingDefaults();  // reset to built-in defaults
  d.input_image = image;
  d.output_file = output;
  d.duration_seconds = 2;
  d.fps = 30;
  std::unique_ptr<native::ui::Image> img =
      native::ui::Image::FromFile(image.c_str());
  ASSERT_NE(img, nullptr) << "cannot decode input image: " << image;
  d.width = img->width();
  d.height = img->height();
}

config::PipelineConfig LoadConfig() {
  config::PipelineConfig config;
  config::ConfigStatus status = config::LoadPipelineConfigFile(
      Runfile("src/examples/configs/dashcam_record.json"), &config);
  EXPECT_TRUE(status.ok) << status.message;
  return config;
}

// Writes a tiny solid-color PNG with the given dimensions (stdlib only).
std::string WritePng(const std::string& path, int w, int h) {
  std::vector<uint8_t> raw;
  raw.reserve(static_cast<size_t>(h) * (w * 3 + 1));
  for (int y = 0; y < h; ++y) {
    raw.push_back(0);  // filter: none
    for (int x = 0; x < w; ++x) {
      raw.push_back(100);
      raw.push_back(150);
      raw.push_back(200);
    }
  }
  std::vector<uint8_t> compressed(compressBound(raw.size()));
  uLongf compressed_len = static_cast<uLongf>(compressed.size());
  compress2(compressed.data(), &compressed_len, raw.data(),
            static_cast<uLong>(raw.size()), 6);
  compressed.resize(compressed_len);

  const auto chunk = [](const char* tag, const uint8_t* data, size_t len,
                        std::vector<uint8_t>* out) {
    const uint32_t big_len = __builtin_bswap32(static_cast<uint32_t>(len));
    out->insert(out->end(), reinterpret_cast<const uint8_t*>(&big_len),
                reinterpret_cast<const uint8_t*>(&big_len) + 4);
    out->insert(out->end(), tag, tag + 4);
    const size_t start = out->size();
    out->insert(out->end(), data, data + len);
    const uint32_t crc = static_cast<uint32_t>(crc32(0, out->data() + start - 4,
                                                     len + 4));
    const uint32_t big_crc = __builtin_bswap32(crc);
    out->insert(out->end(), reinterpret_cast<const uint8_t*>(&big_crc),
                reinterpret_cast<const uint8_t*>(&big_crc) + 4);
  };

  std::vector<uint8_t> png{0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A};
  const uint8_t ihdr[13] = {0, 0, 0, static_cast<uint8_t>(w),
                            static_cast<uint8_t>(h >> 24 & 0xFF),
                            static_cast<uint8_t>(h >> 16 & 0xFF),
                            static_cast<uint8_t>(h >> 8 & 0xFF),
                            static_cast<uint8_t>(h), 8, 2, 0, 0, 0};
  chunk("IHDR", ihdr, sizeof(ihdr), &png);
  chunk("IDAT", compressed.data(), compressed.size(), &png);
  chunk("IEND", nullptr, 0, &png);

  FILE* f = std::fopen(path.c_str(), "wb");
  if (f) {
    std::fwrite(png.data(), 1, png.size(), f);
    std::fclose(f);
  }
  return path;
}

TEST(DashcamRecordTest, RecordsPlayableMp4) {
  const std::string output = TempPath("dashcam_record_test.mp4");
  std::remove(output.c_str());
  std::remove((output + ".tmp").c_str());
  SetDefaults(Runfile("src/examples/assets/dashcam_default.png"), output);

  PipelineRunner runner(LoadConfig(), 60);
  NodeStatus run = runner.Run();
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

TEST(DashcamRecordTest, MissingInputImageFailsWithPath) {
  const std::string output = TempPath("dashcam_missing_input.mp4");
  const std::string missing = "/nonexistent_dir_xyz/dashcam_default.png";
  RecordingDefaults& d = Defaults();
  d = RecordingDefaults();
  d.input_image = missing;
  d.output_file = output;
  d.duration_seconds = 2;
  d.fps = 30;
  std::remove(output.c_str());

  PipelineRunner runner(LoadConfig(), 60);
  NodeStatus run = runner.Run();
  ASSERT_FALSE(run.ok);
  EXPECT_NE(run.message.find(missing), std::string::npos)
      << "error must contain the input path: " << run.message;
  EXPECT_NE(run.message.find("stream_input"), std::string::npos);
  EXPECT_FALSE(FileExists(output)) << "no output on failure";
}

TEST(DashcamRecordTest, UnwritableOutputFailsWithPath) {
  const std::string output =
      "/nonexistent_dir_xyz/out/sub/dashcam.mp4";  // parent does not exist
  SetDefaults(Runfile("src/examples/assets/dashcam_default.png"), output);

  PipelineRunner runner(LoadConfig(), 60);
  NodeStatus run = runner.Run();
  ASSERT_FALSE(run.ok);
  EXPECT_NE(run.message.find("muxer_sink"), std::string::npos);
  EXPECT_NE(run.message.find(output), std::string::npos)
      << "error must contain the output path: " << run.message;
  EXPECT_FALSE(FileExists(output));
  EXPECT_FALSE(FileExists(output + ".tmp"));
}

TEST(DashcamRecordTest, EncodeFailureLeavesNoPartialArtifact) {
  // An odd-dimension input forces the H.264 encoder Init to fail (x264
  // rejects it), exercising the encode-failure path deterministically.
  const std::string output = TempPath("dashcam_encode_fail.mp4");
  const std::string odd_image = WritePng(TempPath("odd5.png"), 5, 5);
  SetDefaults(odd_image, output);
  std::remove(output.c_str());
  std::remove((output + ".tmp").c_str());

  PipelineRunner runner(LoadConfig(), 60);
  NodeStatus run = runner.Run();
  ASSERT_FALSE(run.ok);
  EXPECT_NE(run.message.find("video_encoder"), std::string::npos)
      << run.message;
  EXPECT_FALSE(FileExists(output)) << "no partial output on encode failure";
  EXPECT_FALSE(FileExists(output + ".tmp")) << "no leftover temp file";
}

}  // namespace
}  // namespace media::record
