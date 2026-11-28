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

// Detects: a sub-segment with cut offsets losing its bone or its delta ID; the vanilla RArm_UpperArm entry on sub 1 of segment 2 must read 0x20100, and a sub-segment without cut offsets must stay unlisted.
TEST(NifSidecarGenerate, CutSubSegmentOfANamedBoneBecomesItsDeltaId) {
  const auto bone = fo4::nif::bone_id_hash("RArm_UpperArm");
  const auto table = layout({0, 0, 2}, {user(0, k_no_bone), user(0, k_no_bone), user(0, k_no_bone),
                                       user(1, fo4::nif::bone_id_hash("RArm_Hand")), user(2, bone, {0.5f})});
  fo4::nif::SsfDiagnostics diagnostics;
  const auto shape = fo4::nif::ssf_shape_from_segments(table, "Arm:0", std::vector<std::string>({"RArm_UpperArm", "RArm_Hand"}), &diagnostics);
  EXPECT_EQ(diagnostics.unresolved_ids, (std::vector<std::uint32_t>{fo4::nif::bone_id_hash("RArm_Hand")}));
  EXPECT_EQ(shape.name, "Arm:0");
  EXPECT_EQ(shape.base_bone, "DISABLED");
  ASSERT_EQ(shape.deltas.size(), 1U);
  EXPECT_EQ(shape.deltas[0].bone_name, "RArm_UpperArm");
  EXPECT_EQ(shape.deltas[0].segment_ids, (std::vector<std::uint32_t>{0x20100U}));
}

// Detects: the sub-segment index encoded as a bit (1 << index) instead of a number, or gated on the user index; vanilla Raider04M lists RLeg_Calf as 0x50300 for a cut at sub 3 (user index 4), UVRaiderPantsBackfacing lists LLeg_Calf as 0x60000 for a cut at sub 0 (user index 4), and 9-sub tables carry IDs up to 0x800.
TEST(NifSidecarGenerate, SubSegmentIndexIsEncodedAsANumberWhateverItsUserIndex) {
  const auto thigh = fo4::nif::bone_id_hash("RLeg_Thigh");
  const auto calf = fo4::nif::bone_id_hash("RLeg_Calf");
  const auto table = layout({5, 1, 9},
                            {user(5, k_no_bone), user(1, thigh), user(2, thigh, {1.0f}), user(3, thigh), user(4, calf, {2.0f}),
                             user(5, calf), user(6, k_no_bone), user(4, calf, {3.0f}), user(0, k_no_bone), user(1, thigh),
                             user(2, thigh), user(3, thigh), user(4, thigh), user(5, thigh), user(6, thigh), user(7, thigh),
                             user(8, thigh), user(9, calf, {4.0f})});
  fo4::nif::SsfDiagnostics diagnostics;
  const auto shape = fo4::nif::ssf_shape_from_segments(table, "Leg", std::vector<std::string>({"RLeg_Thigh", "RLeg_Calf"}), &diagnostics);
  EXPECT_EQ(diagnostics.unresolved_ids, (std::vector<std::uint32_t>{thigh, calf}));
  ASSERT_EQ(shape.deltas.size(), 2U);
  EXPECT_EQ(shape.deltas[0].bone_name, "RLeg_Thigh");
  EXPECT_EQ(shape.deltas[0].segment_ids, (std::vector<std::uint32_t>{0x00100U}));
  EXPECT_EQ(shape.deltas[1].bone_name, "RLeg_Calf");
  EXPECT_EQ(shape.deltas[1].segment_ids, (std::vector<std::uint32_t>{0x00300U, 0x10000U, 0x20800U}));
}

TEST(NifSidecarGenerate, NamedSubSegmentsWithoutCutOffsetsAreUnsupported) {
  const auto bone = fo4::nif::bone_id_hash("LLeg_Thigh");
  const auto plain = layout({2}, {user(0, k_no_bone), user(1, bone), user(3, bone)});
  EXPECT_THROW(fo4::nif::generate_ssf(plain, "Leg", std::vector<std::string>({"LLeg_Thigh"})), fo4::parse_error);
  fo4::nif::SsfDiagnostics diagnostics;
  const auto shape = fo4::nif::ssf_shape_from_segments(plain, "Leg", std::vector<std::string>({"LLeg_Thigh"}), &diagnostics);
  EXPECT_TRUE(shape.deltas.empty());
  EXPECT_EQ(diagnostics.unresolved_ids, (std::vector<std::uint32_t>{bone}));
  const auto unbound = layout({1}, {user(0, k_no_bone), user(2, k_no_bone, {0.5f})});
  EXPECT_TRUE(fo4::nif::ssf_shape_from_segments(unbound, "Leg", {}).deltas.empty());
}

// Detects: a bone ID that no string in the NIF hashes to being given an invented bone name, or silently dropped; the generation must be refused unless diagnostics are passed, and then the ID must be reported.
TEST(NifSidecarGenerate, UnresolvedBoneIdIsReportedAndRefusedByDefault) {
  const auto table = layout({1}, {user(0, k_no_bone), user(1, 0x12345678U, {0.5f})});
  const std::vector<std::string> strings{"Bip01 Spine"};
  EXPECT_THROW(fo4::nif::ssf_shape_from_segments(table, "Leg", strings), fo4::parse_error);
  fo4::nif::SsfDiagnostics diagnostics;
  const auto shape = fo4::nif::ssf_shape_from_segments(table, "Leg", strings, &diagnostics);
  EXPECT_TRUE(shape.deltas.empty());
  EXPECT_EQ(shape.base_bone, "DISABLED");
  EXPECT_EQ(diagnostics.unresolved_ids, (std::vector<std::uint32_t>{0x12345678U}));
  EXPECT_TRUE(diagnostics.ambiguous_ids.empty());
}

// Detects: a resolvable bone dropped when another bone in the same table is unresolved, or the unresolved ID reported as if it were named; the named delta must survive and only the unresolved ID is reported.
TEST(NifSidecarGenerate, MixedResolvedAndUnresolvedAssignmentsAreSplit) {
  const auto table = layout({1, 1}, {user(0, k_no_bone), user(1, fo4::nif::bone_id_hash("RArm_UpperArm"), {0.5f}),
                                     user(0, k_no_bone), user(1, 0x12345678U, {0.5f})});
  const std::vector<std::string> strings{"RArm_UpperArm"};
  EXPECT_THROW(fo4::nif::generate_ssf(table, "Arm", strings), fo4::parse_error);
  fo4::nif::SsfDiagnostics diagnostics;
  const auto shape = fo4::nif::ssf_shape_from_segments(table, "Arm", strings, &diagnostics);
  ASSERT_EQ(shape.deltas.size(), 1U);
  EXPECT_EQ(shape.deltas[0].bone_name, "RArm_UpperArm");
  EXPECT_EQ(shape.deltas[0].segment_ids, (std::vector<std::uint32_t>{0x0U}));
  EXPECT_EQ(diagnostics.unresolved_ids, (std::vector<std::uint32_t>{0x12345678U}));
  EXPECT_TRUE(diagnostics.ambiguous_ids.empty());
}

// Detects: two distinct strings sharing a bone ID hash being named by whichever string comes first; the ID must be reported as ambiguous in either order, while a unique match in the same table still names its bone.
TEST(NifSidecarGenerate, BoneIdHashCollisionIsReportedInEitherOrder) {
  const auto collide = fo4::nif::bone_id_hash("bone_44fk8rjyprxo");
  ASSERT_EQ(collide, fo4::nif::bone_id_hash("bone_xhm3evmr4bfq"));
  const auto table = layout({1, 1}, {user(0, k_no_bone), user(1, collide, {0.5f}), user(0, k_no_bone),
                                     user(1, fo4::nif::bone_id_hash("RArm_UpperArm"), {0.5f})});
  const std::vector<std::string> forward{"bone_44fk8rjyprxo", "bone_xhm3evmr4bfq", "RArm_UpperArm"};
  const std::vector<std::string> reversed{"RArm_UpperArm", "bone_xhm3evmr4bfq", "bone_44fk8rjyprxo"};
  for (const auto& strings : {forward, reversed}) {
    EXPECT_THROW(fo4::nif::generate_ssf(table, "Arm", strings), fo4::parse_error);
    fo4::nif::SsfDiagnostics diagnostics;
    const auto shape = fo4::nif::ssf_shape_from_segments(table, "Arm", strings, &diagnostics);
    ASSERT_EQ(shape.deltas.size(), 1U);
    EXPECT_EQ(shape.deltas[0].bone_name, "RArm_UpperArm");
    EXPECT_EQ(shape.deltas[0].segment_ids, (std::vector<std::uint32_t>{0x10000U}));
    EXPECT_TRUE(diagnostics.unresolved_ids.empty());
    EXPECT_EQ(diagnostics.ambiguous_ids, (std::vector<std::uint32_t>{collide}));
  }
}

// Detects: two spellings of one bone that differ only in case (the hash folds case) being refused as ambiguous; they must name the bone with the first spelling.
TEST(NifSidecarGenerate, CaseOnlySpellingsOfOneBoneAreNotAmbiguous) {
  const auto table = layout({1}, {user(0, k_no_bone), user(1, fo4::nif::bone_id_hash("RArm_UpperArm"), {0.5f})});
  const std::vector<std::string> strings{"RArm_UpperArm", "rarm_upperarm"};
  const auto shape = fo4::nif::ssf_shape_from_segments(table, "Arm", strings);
  ASSERT_EQ(shape.deltas.size(), 1U);
  EXPECT_EQ(shape.deltas[0].bone_name, "RArm_UpperArm");
}

TEST(NifSidecarGenerate, RefusesSegmentsThatShareAUserDataStart) {
  const auto bone = fo4::nif::bone_id_hash("RArm_UpperArm");
  auto table = layout({1, 1}, {user(0, k_no_bone), user(1, bone, {0.5f}), user(0, k_no_bone), user(1, bone, {0.5f})});
  table.segment_starts[1] = 0;
  EXPECT_THROW(fo4::nif::ssf_shape_from_segments(table, "Arm", std::vector<std::string>({"RArm_UpperArm"})), fo4::parse_error);
}

TEST(NifSidecarGenerate, RefusesSegmentRangesThatOverlap) {
  const auto bone = fo4::nif::bone_id_hash("RArm_UpperArm");
  auto table = layout({2, 0}, {user(0, k_no_bone), user(1, bone, {0.5f}), user(2, bone, {0.5f}), user(0, k_no_bone)});
  table.segment_starts[1] = 1;
  EXPECT_THROW(fo4::nif::ssf_shape_from_segments(table, "Arm", std::vector<std::string>({"RArm_UpperArm"})), fo4::parse_error);
}

// Detects: a 257th sub-segment index overflowing into the segment-index bits of the delta ID, or a vanilla 9-sub-segment table being refused; 256 sub-segments encode and 257 must be refused.
TEST(NifSidecarGenerate, RefusesMoreSubSegmentsThanTheIdCanEncode) {
  const auto encodable = layout({256}, std::vector<fo4::nif::SegmentUserData>(257, user(k_no_bone, k_no_bone)));
  EXPECT_NO_THROW(fo4::nif::ssf_shape_from_segments(encodable, "Leg", {}));
  const auto table = layout({257}, std::vector<fo4::nif::SegmentUserData>(258, user(k_no_bone, k_no_bone)));
  EXPECT_THROW(fo4::nif::ssf_shape_from_segments(table, "Leg", {}), fo4::parse_error);
}

// Detects: user data that does not cover every total segment being read with bones shifted onto the wrong sub-segments; the table must be refused.
TEST(NifSidecarGenerate, RefusesSegmentTableWhoseUserDataDoesNotCoverTheTotal) {
  auto table = layout({1}, {user(0, k_no_bone), user(1, fo4::nif::bone_id_hash("LLeg_Thigh"), {0.5f})});
  table.user_data.pop_back();
  EXPECT_THROW(fo4::nif::ssf_shape_from_segments(table, "Leg", std::vector<std::string>({"LLeg_Thigh"})), fo4::parse_error);
}

// Detects: a generated .ssf that does not read back to the shape it was built from, so the written sidecar would disagree with the segment table; the round trip must be exact.
TEST(NifSidecarGenerate, GeneratedSsfReadsBackToTheSameShape) {
  const auto table = layout({0, 0, 2}, {user(0, k_no_bone), user(0, k_no_bone), user(0, k_no_bone),
                                       user(1, fo4::nif::bone_id_hash("RArm_UpperArm")),
                                       user(2, fo4::nif::bone_id_hash("RArm_UpperArm"), {0.5f})});
  fo4::nif::SsfDiagnostics diagnostics;
  const auto text = fo4::nif::generate_ssf(table, "Arm:0", std::vector<std::string>({"RArm_UpperArm"}), &diagnostics);
  EXPECT_EQ(diagnostics.unresolved_ids, (std::vector<std::uint32_t>{fo4::nif::bone_id_hash("RArm_UpperArm")}));
  const auto shapes = fo4::nif::read_ssf(text);
  fo4::nif::SsfDiagnostics read_diagnostics;
  const auto expected = fo4::nif::ssf_shape_from_segments(table, "Arm:0", std::vector<std::string>({"RArm_UpperArm"}), &read_diagnostics);
  EXPECT_EQ(read_diagnostics.unresolved_ids, diagnostics.unresolved_ids);
  EXPECT_EQ(shapes, (std::vector{expected}));
}

// Detects: a sidecar whose segment list omits a segment that carries sub-segments, or lists a segment the table does not have; the check must report the difference.
TEST(NifSidecarGenerate, SidecarSegmentCheckFlagsMissingAndExtraSegments) {
  const auto table = layout({0, 0, 2}, {user(0, k_no_bone), user(0, k_no_bone), user(0, k_no_bone),
                                       user(1, fo4::nif::bone_id_hash("RArm_UpperArm")),
                                       user(2, fo4::nif::bone_id_hash("RArm_UpperArm"), {0.5f})});
  fo4::nif::SsfDiagnostics diagnostics;
  const auto listed = fo4::nif::ssf_shape_from_segments(table, "Arm:0", std::vector<std::string>({"RArm_UpperArm"}), &diagnostics);
  EXPECT_EQ(diagnostics.unresolved_ids, (std::vector<std::uint32_t>{fo4::nif::bone_id_hash("RArm_UpperArm")}));
  EXPECT_TRUE(fo4::nif::ssf_lists_segments(listed, table));
  fo4::nif::SsfShape empty;
  empty.name = "Arm:0";
  EXPECT_FALSE(fo4::nif::ssf_lists_segments(empty, table));
  auto extra = listed;
  extra.deltas[0].segment_ids.push_back(0x00010100U);
  EXPECT_FALSE(fo4::nif::ssf_lists_segments(extra, table));
}
