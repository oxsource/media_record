// Pipeline template config validation (spec 001, task T025 / US3; updated for
// spec 002: templates are graph_runtime JSON schema, parsed into
// graph::runtime::GraphConfig by graph_runtime's own JsonParser and validated
// by graph_runtime's ConfigValidator).
//
// Loads the pre-built templates (dashcam_record / recorder / stream / preview)
// and checks:
//   - they parse as graph_runtime JSON schema (graph_runtime's JsonParser)
//   - the graph is structurally valid (unique names, connectivity, no cycles)
//   - every referenced node type is one of the 10 planned node types
// Negative cases verify the locatable error messages required by FR-009.

#include <set>
#include <string>

#include "gtest/gtest.h"
#include "src/framework/config/config_validator.h"
#include "src/framework/config/json/json_parser.h"

namespace media::record::config {
namespace {

// The 10 planned node types (spec 001 research.md §6 / FR-003). Business nodes
// land in later features; templates must reference exactly these type names.
constexpr const char* kPlannedNodeTypes[] = {
    "StreamInputNode",
    "SignalSourceNode",
    "MultiViewLayoutNode",
    "UiOverlayNode",
    "VideoEncoderNode",
    "AudioEncoderNode",
    "RecorderNode",
    "MuxerSinkNode",
    "StreamSinkNode",
    "PreviewNode",
};

bool IsPlannedType(const std::string& type) {
  for (const char* planned : kPlannedNodeTypes) {
    if (type == planned) return true;
  }
  return false;
}

constexpr const char* kTemplates[] = {
    "src/examples/configs/dashcam_record.json",
    "src/examples/configs/recorder.json",
    "src/examples/configs/stream.json",
    "src/examples/configs/preview.json",
};

TEST(PipelineConfigTest, TemplatesParseAndValidate) {
  for (const char* path : kTemplates) {
    graph::runtime::JsonParser parser;
    auto parsed = parser.Parse(path);
    ASSERT_TRUE(parsed.ok()) << path << ": " << parsed.status();
    ASSERT_FALSE(parsed->nodes.empty()) << path;

    const absl::Status validation =
        graph::runtime::ConfigValidator::Validate(*parsed);
    EXPECT_TRUE(validation.ok()) << path << ": " << validation.ToString();

    for (const auto& def : parsed->nodes) {
      EXPECT_TRUE(IsPlannedType(def.type))
          << path << ": node '" << def.name << "' has unexpected type '"
          << def.type << "'";
    }
  }
}

TEST(PipelineConfigTest, DashcamRecordTopology) {
  graph::runtime::JsonParser parser;
  auto parsed = parser.Parse("src/examples/configs/dashcam_record.json");
  ASSERT_TRUE(parsed.ok()) << parsed.status();
  const graph::runtime::GraphConfig& config = *parsed;

  // 7 nodes: 1×StreamInput + 1×SignalSource + Layout + Overlay + Encoder +
  // Recorder + Muxer.
  ASSERT_EQ(config.nodes.size(), 7u);
  EXPECT_EQ(config.nodes[0].type, "StreamInputNode");
  EXPECT_EQ(config.nodes[1].type, "SignalSourceNode");
  EXPECT_EQ(config.nodes[6].type, "MuxerSinkNode");

  // 6 implicit streams (frames / signals / view_frames / osd_frames /
  // es_packets / clips) referenced via "port:stream".
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
  EXPECT_EQ(streams.size(), 6u);
}

TEST(PipelineConfigTest, RecorderTemplateSevenStreams) {
  graph::runtime::JsonParser parser;
  auto parsed = parser.Parse("src/examples/configs/recorder.json");
  ASSERT_TRUE(parsed.ok()) << parsed.status();
  const graph::runtime::GraphConfig& config = *parsed;

  // Dual-cam reference template: 8 nodes / 7 streams (assertion unchanged).
  ASSERT_EQ(config.nodes.size(), 8u);
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
  EXPECT_EQ(streams.size(), 7u);
}

TEST(PipelineConfigTest, MissingFileError) {
  graph::runtime::JsonParser parser;
  auto parsed = parser.Parse("nonexistent.json");
  EXPECT_FALSE(parsed.ok());
  EXPECT_NE(parsed.status().ToString().find("nonexistent.json"),
            std::string::npos);
}

TEST(PipelineConfigTest, MalformedJsonError) {
  graph::runtime::JsonParser parser;
  auto parsed = parser.ParseFromString("{ \"nodes\": [");
  EXPECT_FALSE(parsed.ok());
  EXPECT_NE(parsed.status().ToString().find("JSON parse error"),
            std::string::npos);
}

TEST(PipelineConfigTest, MissingTypeError) {
  graph::runtime::JsonParser parser;
  auto parsed =
      parser.ParseFromString("{ \"nodes\": [ { \"name\": \"n\" } ] }");
  EXPECT_FALSE(parsed.ok());
  EXPECT_NE(parsed.status().ToString().find("type is required"),
            std::string::npos);
}

}  // namespace
}  // namespace media::record::config
