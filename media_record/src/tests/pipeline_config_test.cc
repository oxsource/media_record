// Pipeline template config validation (spec 001, task T025 / US3).
//
// Loads the three pre-built templates (recorder / stream / preview) and checks:
//   - they parse as graph_runtime native JSON schema
//   - node/stream references are structurally valid (contracts P-2/P-3)
//   - every referenced node type is one of the 10 planned node types (they are
//     registered by business-node features; see research.md §6 mapping)
// Negative cases verify the locatable error messages required by FR-009.

#include <string>

#include "gtest/gtest.h"
#include "src/framework/config/pipeline_config.h"

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
    "src/examples/configs/recorder.json",
    "src/examples/configs/stream.json",
    "src/examples/configs/preview.json",
};

TEST(PipelineConfigTest, TemplatesParseAndValidate) {
  for (const char* path : kTemplates) {
    PipelineConfig config;
    ConfigStatus status = LoadPipelineConfigFile(path, &config);
    ASSERT_TRUE(status.ok) << path << ": " << status.message;
    ASSERT_FALSE(config.nodes.empty()) << path;

    status = ValidatePipelineConfig(config);
    EXPECT_TRUE(status.ok) << path << ": " << status.message;

    status = CheckRegisteredTypes(config, IsPlannedType);
    EXPECT_TRUE(status.ok) << path << ": " << status.message;
  }
}

TEST(PipelineConfigTest, RecorderTopologyIsComplete) {
  PipelineConfig config;
  ConfigStatus status =
      LoadPipelineConfigFile(kTemplates[0], &config);
  ASSERT_TRUE(status.ok) << status.message;

  EXPECT_EQ(config.nodes.size(), 8u);
  EXPECT_EQ(config.streams.size(), 7u);

  // Full recorder chain: input -> layout -> overlay -> encoder -> recorder
  // -> muxer. Every node input must be fed by a producer (already validated),
  // spot-check the head and tail of the chain.
  const PipelineNodeDef* muxer = nullptr;
  for (const PipelineNodeDef& node : config.nodes) {
    if (node.name == "muxer") muxer = &node;
  }
  ASSERT_NE(muxer, nullptr);
  ASSERT_EQ(muxer->input_streams.size(), 1u);
  EXPECT_EQ(muxer->input_streams[0], "input:clips");
}

TEST(PipelineConfigTest, DuplicateNodeNameIsRejected) {
  const char* kJson =
      "{\"nodes\":["
      "{\"name\":\"a\",\"type\":\"StreamInputNode\",\"output_streams\":[\"output:x\"]},"
      "{\"name\":\"a\",\"type\":\"MuxerSinkNode\",\"input_streams\":[\"input:x\"]}]}";
  PipelineConfig config;
  ASSERT_TRUE(ParsePipelineConfig(kJson, &config).ok);
  ConfigStatus status = ValidatePipelineConfig(config);
  ASSERT_FALSE(status.ok);
  EXPECT_NE(status.message.find("duplicate node name: 'a'"),
            std::string::npos);
}

TEST(PipelineConfigTest, UndefinedDestNodeIsRejected) {
  const char* kJson =
      "{\"nodes\":["
      "{\"name\":\"a\",\"type\":\"StreamInputNode\",\"output_streams\":[\"output:x\"]}],"
      "\"streams\":[{\"name\":\"x\",\"source_node\":\"a\",\"source_port\":\"output\","
      "\"dest_node\":\"ghost\",\"dest_port\":\"input\"}]}";
  PipelineConfig config;
  ASSERT_TRUE(ParsePipelineConfig(kJson, &config).ok);
  ConfigStatus status = ValidatePipelineConfig(config);
  ASSERT_FALSE(status.ok);
  EXPECT_NE(status.message.find("dest_node 'ghost'"), std::string::npos);
  EXPECT_NE(status.message.find("stream 'x'"), std::string::npos);
}

TEST(PipelineConfigTest, MismatchedPortTagIsRejected) {
  const char* kJson =
      "{\"nodes\":["
      "{\"name\":\"a\",\"type\":\"StreamInputNode\",\"output_streams\":[\"output:x\"]},"
      "{\"name\":\"b\",\"type\":\"MuxerSinkNode\",\"input_streams\":[\"input:x\"]}],"
      "\"streams\":[{\"name\":\"x\",\"source_node\":\"a\",\"source_port\":\"output\","
      "\"dest_node\":\"b\",\"dest_port\":\"wrong_tag\"}]}";
  PipelineConfig config;
  ASSERT_TRUE(ParsePipelineConfig(kJson, &config).ok);
  ConfigStatus status = ValidatePipelineConfig(config);
  ASSERT_FALSE(status.ok);
  EXPECT_NE(status.message.find("has no input port 'wrong_tag'"),
            std::string::npos);
  EXPECT_NE(status.message.find("dest node 'b'"), std::string::npos);
}

TEST(PipelineConfigTest, MissingProducerIsRejected) {
  // Node b consumes 'x' but no node produces it (mirrors graph_runtime
  // ConfigValidator::ValidateConnectivity).
  const char* kJson =
      "{\"nodes\":["
      "{\"name\":\"b\",\"type\":\"MuxerSinkNode\",\"input_streams\":[\"input:x\"]}]}";
  PipelineConfig config;
  ASSERT_TRUE(ParsePipelineConfig(kJson, &config).ok);
  ConfigStatus status = ValidatePipelineConfig(config);
  ASSERT_FALSE(status.ok);
  EXPECT_NE(status.message.find("node 'b'"), std::string::npos);
  EXPECT_NE(status.message.find("input stream 'x'"), std::string::npos);
}

TEST(PipelineConfigTest, UnregisteredTypeErrorIsLocatable) {
  const char* kJson =
      "{\"nodes\":["
      "{\"name\":\"fancy\",\"type\":\"NotARealNode\",\"output_streams\":[\"output:x\"]}]}";
  PipelineConfig config;
  ASSERT_TRUE(ParsePipelineConfig(kJson, &config).ok);
  ConfigStatus status = CheckRegisteredTypes(config, IsPlannedType);
  ASSERT_FALSE(status.ok);
  EXPECT_NE(status.message.find("node 'fancy'"), std::string::npos);
  EXPECT_NE(status.message.find("unregistered node type 'NotARealNode'"),
            std::string::npos);
}

TEST(PipelineConfigTest, MalformedJsonIsRejected) {
  PipelineConfig config;
  ConfigStatus status =
      ParsePipelineConfig("{\"nodes\": [", &config);
  ASSERT_FALSE(status.ok);
  EXPECT_NE(status.message.find("json error"), std::string::npos);
}

}  // namespace
}  // namespace media::record::config
