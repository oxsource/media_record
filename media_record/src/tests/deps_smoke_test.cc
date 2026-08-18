// Cross-library dependency smoke test (spec 001, task T026 / US3).
//
// The one place that includes all three capability-repo umbrella headers at
// once and exercises a minimal set of link-reachable symbols from each. This is
// the objective check that "three repos + media_record skeleton" link cleanly
// (spec FR-006 / SC-003), per research.md §4. Only public umbrella targets are
// consumed (contracts/dependency-contract.md D-2).

#include <string>

#include "gtest/gtest.h"

#include "graph_runtime/graph_runtime.h"
#include "native_ui/core.h"
#include "video_codec/video_codec.h"

namespace {

// --- graph_runtime ---------------------------------------------------------

TEST(DepsSmokeTest, GraphRuntimePublicTypesLink) {
  graph::runtime::Timestamp ts(100);
  EXPECT_EQ(ts.Value(), 100);

  auto pkt = graph::runtime::Packet::MakePacket<int>(42);
  ASSERT_FALSE(pkt.IsEmpty());
  auto value = pkt.Get<int>();
  ASSERT_TRUE(value.ok());
  EXPECT_EQ(*value, 42);

  graph::runtime::GraphConfig config;
  config.nodes.push_back({"n1", "A", {}, {"out:x"}, {}, {}, {}, "", "", 1, 0});
  EXPECT_EQ(config.nodes.size(), 1u);
  EXPECT_EQ(config.nodes[0].name, "n1");

  graph::runtime::PacketSet set;
  set.Set("key", graph::runtime::Packet::MakePacket<int>(1));
  EXPECT_EQ(set.NumEntries(), 1);

  graph::runtime::CollectionItemId id = 7;
  EXPECT_EQ(id, 7);
  absl::Status stop = graph::runtime::StatusStop();
  EXPECT_TRUE(graph::runtime::IsStopStatus(stop));
}

// --- native_ui -------------------------------------------------------------

TEST(DepsSmokeTest, NativeUiPublicTypesLink) {
  native::ui::Point p{3.0f, 4.0f};
  native::ui::Rect r{0.0f, 0.0f, 10.0f, 10.0f};
  // Member-function calls force link resolution of the implementation.
  EXPECT_TRUE(r.Contains(p));
  native::ui::Rect small{0.0f, 0.0f, 1.0f, 1.0f};
  native::ui::Point outside{5.0f, 5.0f};
  EXPECT_FALSE(small.Contains(outside));
  native::ui::Rect inter = r.Intersect(small);
  EXPECT_EQ(inter.width, 1.0f);
  EXPECT_EQ(inter.height, 1.0f);
}

// --- video_codec -----------------------------------------------------------

TEST(DepsSmokeTest, VideoCodecPublicTypesLink) {
  EXPECT_EQ(video::codec::StatusToString(video::codec::Status::kOk),
            std::string("kOk"));
  EXPECT_EQ(video::codec::utils::MediaFormat::kMp4, ".mp4");
  EXPECT_TRUE(video::codec::utils::MediaFormat::HasExtension(
      "recording.mp4", video::codec::utils::MediaFormat::kMp4));
  EXPECT_FALSE(video::codec::utils::MediaFormat::HasExtension(
      "recording.mkv", video::codec::utils::MediaFormat::kMp4));
}

}  // namespace
