#include "fo4/error.hpp"
#include "fo4/nif/nif.hpp"
#include "fo4/nif/sidecar_generate.hpp"
#include "fo4/nif/sidecars.hpp"

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace {

constexpr std::uint32_t k_no_bone = 0xFFFFFFFFU;

fo4::nif::SegmentUserData user(std::uint32_t index, std::uint32_t bone, std::vector<float> cuts = {}) {
  return fo4::nif::SegmentUserData{index, bone, std::move(cuts)};
}

fo4::nif::SegmentTable layout(const std::vector<std::size_t>& subs_per_segment,
                              std::vector<fo4::nif::SegmentUserData> users) {
  fo4::nif::SegmentTable table;
  std::uint32_t next = 0;
  for (const auto subs : subs_per_segment) {
    fo4::nif::Segment segment;
    segment.sub_segments.resize(subs);
    table.segment_starts.push_back(next);
    table.segments.push_back(std::move(segment));
    next += static_cast<std::uint32_t>(1 + subs);
  }
  table.total_segments = next;
  table.user_data = std::move(users);
  return table;
}

}  // namespace

// Detects: a plain first sub-segment (user index 1, no cut offsets) losing its bone or its delta ID; the vanilla RArm_UpperArm entry on sub 0 of segment 2 must read 0x20100.
TEST(NifSidecarGenerate, FirstSubSegmentOfANamedBoneBecomesItsDeltaId) {
  const auto table = layout({0, 0, 2}, {user(0, k_no_bone), user(0, k_no_bone), user(0, k_no_bone),
                                       user(1, fo4::nif::bone_id_hash("RArm_UpperArm")),
                                       user(2, fo4::nif::bone_id_hash("RArm_UpperArm"))});
  const auto shape = fo4::nif::ssf_shape_from_segments(table, "Arm:0", std::vector<std::string>({"RArm_UpperArm"}));
  EXPECT_EQ(shape.name, "Arm:0");
  EXPECT_EQ(shape.base_bone, "DISABLED");
  ASSERT_EQ(shape.deltas.size(), 1U);
  EXPECT_EQ(shape.deltas[0].bone_name, "RArm_UpperArm");
  EXPECT_EQ(shape.deltas[0].segment_ids, (std::vector<std::uint32_t>{0x20100U}));
}

// Detects: a sub-segment with cut offsets, or with a user index other than 1, producing a delta; only the plain first sub-segment of a bone is listed.
TEST(NifSidecarGenerate, CutOffsetsAndOtherUserIndicesProduceNoDelta) {
  const auto bone = fo4::nif::bone_id_hash("LLeg_Thigh");
  const auto cut = layout({1}, {user(0, k_no_bone), user(1, bone, {0.5f})});
  EXPECT_TRUE(fo4::nif::ssf_shape_from_segments(cut, "Leg", std::vector<std::string>({"LLeg_Thigh"})).deltas.empty());
  const auto other = layout({1}, {user(0, k_no_bone), user(3, bone)});
  EXPECT_TRUE(fo4::nif::ssf_shape_from_segments(other, "Leg", std::vector<std::string>({"LLeg_Thigh"})).deltas.empty());
}

// Detects: a bone ID that no string in the NIF hashes to being given an invented bone name; an unresolved entry must be left out.
TEST(NifSidecarGenerate, UnresolvedBoneIdIsLeftOutInsteadOfNamed) {
  const auto table = layout({1}, {user(0, k_no_bone), user(1, 0x12345678U)});
  const auto shape = fo4::nif::ssf_shape_from_segments(table, "Leg", std::vector<std::string>({"Bip01 Spine"}));
  EXPECT_TRUE(shape.deltas.empty());
  EXPECT_EQ(shape.base_bone, "DISABLED");
}

// Detects: a ninth sub-segment wrapping its bit into the segment-index bits of the delta ID; more than eight sub-segments must be refused.
TEST(NifSidecarGenerate, RefusesMoreSubSegmentsThanTheIdCanEncode) {
  const auto table = layout({9}, std::vector<fo4::nif::SegmentUserData>(10, user(k_no_bone, k_no_bone)));
  EXPECT_THROW(fo4::nif::ssf_shape_from_segments(table, "Leg", {}), fo4::parse_error);
}

// Detects: user data that does not cover every total segment being read with bones shifted onto the wrong sub-segments; the table must be refused.
TEST(NifSidecarGenerate, RefusesSegmentTableWhoseUserDataDoesNotCoverTheTotal) {
  auto table = layout({1}, {user(0, k_no_bone), user(1, fo4::nif::bone_id_hash("LLeg_Thigh"))});
  table.user_data.pop_back();
  EXPECT_THROW(fo4::nif::ssf_shape_from_segments(table, "Leg", std::vector<std::string>({"LLeg_Thigh"})), fo4::parse_error);
}

// Detects: a generated .ssf that does not read back to the shape it was built from, so the written sidecar would disagree with the segment table; the round trip must be exact.
TEST(NifSidecarGenerate, GeneratedSsfReadsBackToTheSameShape) {
  const auto table = layout({0, 0, 2}, {user(0, k_no_bone), user(0, k_no_bone), user(0, k_no_bone),
                                       user(1, fo4::nif::bone_id_hash("RArm_UpperArm")),
                                       user(2, fo4::nif::bone_id_hash("RArm_UpperArm"))});
  const auto text = fo4::nif::generate_ssf(table, "Arm:0", std::vector<std::string>({"RArm_UpperArm"}));
  const auto shapes = fo4::nif::read_ssf(text);
  EXPECT_EQ(shapes, (std::vector{fo4::nif::ssf_shape_from_segments(table, "Arm:0", std::vector<std::string>({"RArm_UpperArm"}))}));
}

// Detects: a sidecar whose segment list omits a segment that carries sub-segments, or lists a segment the table does not have; the check must report the difference.
TEST(NifSidecarGenerate, SidecarSegmentCheckFlagsMissingAndExtraSegments) {
  const auto table = layout({0, 0, 2}, {user(0, k_no_bone), user(0, k_no_bone), user(0, k_no_bone),
                                       user(1, fo4::nif::bone_id_hash("RArm_UpperArm")),
                                       user(2, fo4::nif::bone_id_hash("RArm_UpperArm"))});
  const auto listed = fo4::nif::ssf_shape_from_segments(table, "Arm:0", std::vector<std::string>({"RArm_UpperArm"}));
  EXPECT_TRUE(fo4::nif::ssf_lists_segments(listed, table));
  fo4::nif::SsfShape empty;
  empty.name = "Arm:0";
  EXPECT_FALSE(fo4::nif::ssf_lists_segments(empty, table));
  auto extra = listed;
  extra.deltas[0].segment_ids.push_back(0x00010100U);
  EXPECT_FALSE(fo4::nif::ssf_lists_segments(extra, table));
}
