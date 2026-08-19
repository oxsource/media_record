// Pipeline template config validation (spec 001, task T025 / US3; updated for
// spec 002: templates are graph_runtime JSON schema, parsed into
// graph::runtime::GraphConfig by graph_runtime's own JsonParser and validated
// by graph_runtime's ConfigValidator).
//
// Loads the pre-built templates (dashcam_record / recorder / stream / preview)
// and checks:
//   - they parse as graph_runtime JSON schema (graph_runtime's JsonParser)
//   - the graph is structurally valid (unique names, connectivity, no cycles)
//   - every referenced node type is one of the 9 planned node types
// Negative cases verify the locatable error messages required by FR-009.

#include <set>
#include <string>

#include "gtest/gtest.h"
#include "graph_runtime/config_validator.h"
#include "graph_runtime/json_parser.h"

namespace media::record::config {
namespace {

// The implemented node types. Templates must reference exactly these type
// names. (Legacy StreamInput/MultiViewLayout/UiOverlay were replaced by the
// single compositing DashcamRenderNode.)
constexpr const char* kPlannedNodeTypes[] = {
    "DashcamRenderNode",
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

  // 4 nodes: DashcamRender + Encoder + Recorder + Muxer.
  ASSERT_EQ(config.nodes.size(), 4u);
  EXPECT_EQ(config.nodes[0].type, "DashcamRenderNode");
  EXPECT_EQ(config.nodes[3].type, "MuxerSinkNode");

  // Node params are carried by each node's JSON "options" object and parsed
  // into NodeDef.options by graph_runtime's JsonParser.
  const std::string* background =
      config.nodes[0].options.Get<std::string>("background");
  ASSERT_NE(background, nullptr);
  EXPECT_EQ(*background, "src/examples/assets/dashcam_road.png");
  const std::string* car = config.nodes[0].options.Get<std::string>("car");
  ASSERT_NE(car, nullptr);
  EXPECT_EQ(*car, "src/examples/assets/flydog.png");
  const int* fps = config.nodes[0].options.Get<int>("fps");
  ASSERT_NE(fps, nullptr);
  EXPECT_EQ(*fps, 30);
  const int* frame_count = config.nodes[0].options.Get<int>("frame_count");
  ASSERT_NE(frame_count, nullptr);
  EXPECT_EQ(*frame_count, 300);
  const int* bitrate = config.nodes[1].options.Get<int>("bitrate");
  ASSERT_NE(bitrate, nullptr);
  EXPECT_EQ(*bitrate, 4000000);
  const std::string* output = config.nodes[3].options.Get<std::string>("output");
  ASSERT_NE(output, nullptr);
  EXPECT_EQ(*output, "out/dashcam.mp4");
  const int* muxer_width = config.nodes[3].options.Get<int>("width");
  ASSERT_NE(muxer_width, nullptr);
  EXPECT_EQ(*muxer_width, 1280);

  // 3 implicit streams (frames / es_packets / clips) referenced via
  // "port:stream".
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
  EXPECT_EQ(streams.size(), 3u);
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
