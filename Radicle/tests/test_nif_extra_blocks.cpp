#include "fo4/error.hpp"
#include "fo4/nif/nif.hpp"
#include "fo4/nif/nif_write.hpp"
#include "fo4/nif/sidecars.hpp"
#include "nif_build.hpp"

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

using fo4::nif::Shape;
using fo4::nif::Transform;
using namespace nif_build;

// Detects: swapped parent and name or quaternion order; each connect point field must match the written block.
TEST(NifExtraData, ConnectParentsKeepParentNameAndWFirstRotationOrder) {
  const fo4::nif::ScannedConnectPoint first{"Bip01 Spine", "Weapon", {0.9f, 0.1f, 0.2f, 0.3f}, {1.0f, 2.0f, 3.0f}, 1.5f};
  const fo4::nif::ScannedConnectPoint second{"Root", "Socket", {1.0f, 0.0f, 0.0f, 0.0f}, {4.0f, 5.0f, 6.0f}, 2.0f};
  const auto nif = fo4::nif::read_nif(build_nif({"s"}, {connect_parents_block({first, second})}, {}));
  const auto points = fo4::nif::read_connect_parents(nif);
  ASSERT_EQ(points.size(), 2U);
  EXPECT_EQ(points[0].parent, "Bip01 Spine");
  EXPECT_EQ(points[0].name, "Weapon");
  EXPECT_FLOAT_EQ(points[0].rotation_wxyz[0], 0.9f);
  EXPECT_FLOAT_EQ(points[0].rotation_wxyz[1], 0.1f);
  EXPECT_FLOAT_EQ(points[0].rotation_wxyz[3], 0.3f);
  expect_floats({points[0].translation[0], points[0].translation[1], points[0].translation[2]}, {1.0f, 2.0f, 3.0f});
  EXPECT_FLOAT_EQ(points[0].scale, 1.5f);
  EXPECT_EQ(points[1].parent, "Root");
  EXPECT_EQ(points[1].name, "Socket");
  EXPECT_FLOAT_EQ(points[1].rotation_wxyz[0], 1.0f);
  expect_floats({points[1].translation[0], points[1].translation[1], points[1].translation[2]}, {4.0f, 5.0f, 6.0f});
  EXPECT_FLOAT_EQ(points[1].scale, 2.0f);
}

// Detects: a misread skinned byte; every child name after it must still decode.
TEST(NifExtraData, ConnectChildrenReadSkinnedFlagAndNames) {
  const auto nif = fo4::nif::read_nif(
      build_nif({"s"}, {connect_children_block(true, {"Hand", "Head"}), connect_children_block(false, {})}, {}));
  const auto children = fo4::nif::read_connect_children(nif);
  ASSERT_EQ(children.size(), 2U);
  EXPECT_TRUE(children[0].skinned);
  EXPECT_EQ(children[0].names, (std::vector<std::string>{"Hand", "Head"}));
  EXPECT_FALSE(children[1].skinned);
  EXPECT_TRUE(children[1].names.empty());
}

// Detects: reading shared data unconditionally; shapes whose segment count equals the total must parse.
TEST(NifSegments, TableWithoutSharedDataLeavesSsfAndUserDataEmpty) {
  const fo4::nif::Segment segment{0, 1, 0, {}};
  const auto nif = fo4::nif::read_nif(
      build_nif({"shape"}, {sub_index_shape_block(-1, -1, segment_table_bytes(2, 1, {segment}))}, {}));
  const auto table = fo4::nif::read_segments(nif, 0);
  EXPECT_EQ(table.primitives, 2U);
  EXPECT_EQ(table.total_segments, 1U);
  ASSERT_EQ(table.segments.size(), 1U);
  EXPECT_EQ(table.segments[0].primitives, 1U);
  EXPECT_TRUE(table.segment_starts.empty());
  EXPECT_TRUE(table.user_data.empty());
  EXPECT_TRUE(table.ssf_file.empty());
}

// Detects: a wrong shared data layout; the SSF path and per-segment bone IDs must survive.
TEST(NifSegments, ReadsSharedStartsUserDataCutOffsetsAndSsfWhenSegmentsFallShortOfTotal) {
  const fo4::nif::Segment first{0, 1, 0, {}};
  const fo4::nif::Segment second{1, 2, 1, {}};
  const std::vector<std::uint32_t> starts{0, 5};
  const fo4::nif::SegmentUserData first_user{7, 9, {0.5f, 1.5f}};
  const fo4::nif::SegmentUserData second_user{8, 10, {}};
  const auto shared = shared_segment_bytes(starts, {first_user, second_user}, "Data\\Fx.ssf");
  const auto nif = fo4::nif::read_nif(
      build_nif({"shape"}, {sub_index_shape_block(-1, -1, segment_table_bytes(3, 3, {first, second}, shared))}, {}));
  const auto table = fo4::nif::read_segments(nif, 0);
  EXPECT_EQ(table.total_segments, 3U);
  ASSERT_EQ(table.segments.size(), 2U);
  EXPECT_EQ(table.segments[1].parent_array_index, 1U);
  EXPECT_EQ(table.segment_starts, starts);
  ASSERT_EQ(table.user_data.size(), 2U);
  EXPECT_EQ(table.user_data[0].user_index, 7U);
  EXPECT_EQ(table.user_data[0].bone_id, 9U);
  expect_floats(table.user_data[0].cut_offsets, {0.5f, 1.5f});
  EXPECT_TRUE(table.user_data[1].cut_offsets.empty());
  EXPECT_EQ(table.ssf_file, "Data\\Fx.ssf");
}

// Detects: a sub-segment unused field dropped on read; the value must read back and fields after the first sub-segment must stay aligned.
TEST(NifSegments, CountsEverySubSegmentKeepsItsUnusedFieldAndStaysAligned) {
  const fo4::nif::Segment first{0, 4, 0, {{0, 1, 0, 0xDEADBEEFU}, {1, 3, 2}}};
  const fo4::nif::Segment second{4, 2, 1, {{4, 2, 1}}};
  const std::vector<fo4::nif::SegmentUserData> users(5);
  const auto shared = shared_segment_bytes({0, 4}, users, "");
  const auto nif = fo4::nif::read_nif(
      build_nif({"shape"}, {sub_index_shape_block(-1, -1, segment_table_bytes(6, 5, {first, second}, shared))}, {}));
  const auto table = fo4::nif::read_segments(nif, 0);
  EXPECT_EQ(table.total_segments, 5U);
  ASSERT_EQ(table.user_data.size(), 5U);
  ASSERT_EQ(table.segments.size(), 2U);
  ASSERT_EQ(table.segments[0].sub_segments.size(), 2U);
  EXPECT_EQ(table.segments[0].sub_segments[0].unused, 0xDEADBEEFU);
  EXPECT_EQ(table.segments[0].sub_segments[1].start_index, 1U);
  EXPECT_EQ(table.segments[0].sub_segments[1].primitives, 3U);
  EXPECT_EQ(table.segments[0].sub_segments[1].parent_array_index, 2U);
  EXPECT_EQ(table.segments[1].start_index, 4U);
  ASSERT_EQ(table.segments[1].sub_segments.size(), 1U);
  EXPECT_EQ(table.segments[1].sub_segments[0].parent_array_index, 1U);
}

// Detects: reading a table from a plain shape; a BSTriShape must not parse past its mesh data.
TEST(NifSegments, PlainBsTriShapeYieldsDefaultTable) {
  const auto nif = fo4::nif::read_nif(build_nif({"s"}, {triangle_block("BSTriShape", Transform{})}, {}));
  const auto table = fo4::nif::read_segments(nif, 0);
  EXPECT_EQ(table.primitives, 0U);
  EXPECT_EQ(table.total_segments, 0U);
  EXPECT_TRUE(table.segments.empty());
}

// Detects: a corrupt count; it must not reserve gigabytes before the bounds check fails.
TEST(NifSegments, RejectsSegmentCountPastBlockBeforeAllocating) {
  Writer w;
  w.u32(0);
  w.u32(0xFFFFFFFFU);
  w.u32(1);
  const auto nif = fo4::nif::read_nif(build_nif({"shape"}, {sub_index_shape_block(-1, -1, w.bytes())}, {}));
  expect_parse_error([&] { fo4::nif::read_segments(nif, 0); }, "segment list runs past the end of its block");
}

// Detects: skipping mesh bytes when data size is 0; an empty sub-index shape must parse.
TEST(NifSegments, ZeroDataSizeSkipsMeshBytesAndSegmentTable) {
  auto block = shape_with_refs("BSSubIndexTriShape", 0, Transform{}, k_full, 16, -1, -1, -1, triangle_vertices(), {0, 1, 2});
  block.data.resize(block.data.size() - triangle_vertices().size() - 6);
  std::fill_n(block.data.end() - 4, 4, std::byte{0});
  const auto nif = fo4::nif::read_nif(build_nif({"shape"}, {block}, {}));
  const auto table = fo4::nif::read_segments(nif, 0);
  EXPECT_EQ(table.total_segments, 0U);
  EXPECT_TRUE(table.segments.empty());
}

// Detects: a table layout that falls short of the block; leftover bytes must be rejected.
TEST(NifSegments, RejectsSegmentTableWithTrailingBytes) {
  const fo4::nif::Segment segment{0, 1, 0, {}};
  auto data = segment_table_bytes(1, 1, {segment});
  data.push_back(std::byte{0});
  const auto nif = fo4::nif::read_nif(build_nif({"shape"}, {sub_index_shape_block(-1, -1, data)}, {}));
  expect_parse_error([&] { fo4::nif::read_segments(nif, 0); }, "BSSubIndexTriShape has bytes past its segment table");
}

// Detects: a segment write that drops the unused sub-segment word or the shared data; an unchanged table must rewrite the block to the plain writer's bytes.
TEST(NifSegments, UnchangedTableRewritesToPlainWriterBytes) {
  const fo4::nif::Segment first{0, 1, 0, {{0, 1, 0, 7}}};
  const fo4::nif::Segment second{1, 2, 1, {}};
  const auto shared = shared_segment_bytes({0, 5}, {{7, 9, {0.5f, 1.5f}}, {8, 10, {}}}, "Data\\Fx.ssf");
  const auto nif = fo4::nif::read_nif(
      build_nif({"shape"}, {sub_index_shape_block(-1, -1, segment_table_bytes(3, 3, {first, second}, shared))}, {}));
  const std::vector<fo4::nif::Edit> edits{fo4::nif::Edit{0, "segments", fo4::nif::read_segments(nif, 0)}};
  EXPECT_EQ(fo4::nif::apply_edits(nif, edits), fo4::nif::write_nif(nif));
}

// Detects: a replaced table landing at the wrong offset or keeping stale shared data; the new values must read back exactly.
TEST(NifSegments, ReplacedTableReadsBackWithItsNewValues) {
  const fo4::nif::Segment first{0, 4, 0, {{0, 1, 0, 0}, {1, 3, 2, 0}}};
  const auto shared = shared_segment_bytes({0, 4}, std::vector<fo4::nif::SegmentUserData>(5), "");
  const auto nif = fo4::nif::read_nif(
      build_nif({"shape"}, {sub_index_shape_block(-1, -1, segment_table_bytes(4, 5, {first}, shared))}, {}));
  fo4::nif::SegmentTable table;
  table.primitives = 9;
  table.total_segments = 5;
  table.segments = {{0, 9, 0, {{0, 9, 0, 0x1234}}}};
  table.segment_starts = {0, 9};
  table.user_data = {{5, 11, {2.0f}}};
  table.ssf_file = "Data\\New.ssf";
  const std::vector<fo4::nif::Edit> edits{fo4::nif::Edit{0, "segments", table}};
  const auto reread = fo4::nif::read_nif(fo4::nif::apply_edits(nif, edits));
  const auto back = fo4::nif::read_segments(reread, 0);
  EXPECT_EQ(back.primitives, 9U);
  EXPECT_EQ(back.total_segments, 5U);
  ASSERT_EQ(back.segments.size(), 1U);
  ASSERT_EQ(back.segments[0].sub_segments.size(), 1U);
  EXPECT_EQ(back.segments[0].sub_segments[0].unused, 0x1234U);
  EXPECT_EQ(back.segment_starts, (std::vector<std::uint32_t>{0, 9}));
  ASSERT_EQ(back.user_data.size(), 1U);
  EXPECT_EQ(back.user_data[0].bone_id, 11U);
  EXPECT_EQ(back.ssf_file, "Data\\New.ssf");
}

// Detects: shared data written into a table whose Total Segments equals Num Segments; the reader never consumes it, so the write must be refused.
TEST(NifSegments, RefusesSharedDataWhenTotalLeavesNoRoom) {
  const fo4::nif::Segment segment{0, 1, 0, {}};
  const auto nif = fo4::nif::read_nif(
      build_nif({"shape"}, {sub_index_shape_block(-1, -1, segment_table_bytes(1, 1, {segment}))}, {}));
  auto table = fo4::nif::read_segments(nif, 0);
  table.segment_starts = {0};
  const std::vector<fo4::nif::Edit> edits{fo4::nif::Edit{0, "segments", table}};
  expect_parse_error([&] { fo4::nif::apply_edits(nif, edits); }, "Total Segments equals Num Segments");
}

// Detects: an SSF path whose length wraps the 16-bit field and corrupts the table; the write must be refused.
TEST(NifSegments, RefusesSsfPathLongerThanSixteenBitLength) {
  const fo4::nif::Segment segment{0, 1, 0, {}};
  const auto nif = fo4::nif::read_nif(
      build_nif({"shape"}, {sub_index_shape_block(-1, -1, segment_table_bytes(1, 1, {segment}))}, {}));
  fo4::nif::SegmentTable table = fo4::nif::read_segments(nif, 0);
  table.total_segments = 2;
  table.segment_starts = {0};
  table.ssf_file = std::string(65536, 'a');
  const std::vector<fo4::nif::Edit> edits{fo4::nif::Edit{0, "segments", table}};
  expect_parse_error([&] { fo4::nif::apply_edits(nif, edits); }, "SSF File is longer than its 16-bit length");
}

// Detects: a segment edit appended to a shape with no segment table; the reader would reject those bytes, so the write must be refused.
TEST(NifSegments, RefusesSegmentEditOnPlainTriShape) {
  const auto nif = fo4::nif::read_nif(build_nif({"s"}, {triangle_block("BSTriShape", Transform{})}, {}));
  const std::vector<fo4::nif::Edit> edits{fo4::nif::Edit{0, "segments", fo4::nif::SegmentTable{}}};
  expect_parse_error([&] { fo4::nif::apply_edits(nif, edits); }, "key 'segments' needs a BSSubIndexTriShape");
}

// Detects: a segment or sub-segment running past the shape's 12 triangles, or start indices counted as triangles, or a 32-bit sum wrapping; the write must be refused past the last index and accept a segment ending on it.
TEST(NifSegments, RefusesSegmentRunningPastTheShapesTriangles) {
  const fo4::nif::Segment segment{0, 1, 0, {}};
  const auto nif = fo4::nif::read_nif(
      build_nif({"shape"}, {sub_index_shape_block(-1, -1, segment_table_bytes(1, 1, {segment}))}, {}));
  const auto apply = [&](const fo4::nif::Segment& next) {
    fo4::nif::SegmentTable table;
    table.primitives = 12;
    table.total_segments = 1;
    table.segments = {next};
    const std::vector<fo4::nif::Edit> edits{fo4::nif::Edit{0, "segments", table}};
    return fo4::nif::apply_edits(nif, edits);
  };
  expect_parse_error([&] { apply({0, 1000000, 0, {}}); }, "runs past the shape's 12 triangles");
  expect_parse_error([&] { apply({0, 1, 0, {{28, 3, 0, 0}}}); }, "runs past the shape's 12 triangles");
  expect_parse_error([&] { apply({0xFFFFFFFEU, 2, 0, {}}); }, "runs past the shape's 12 triangles");
  EXPECT_NO_THROW(apply({27, 3, 0, {}}));
}

// Detects: a bone ID hash with the wrong seed, final XOR or case folding; these CRC-32 vectors must hold for the stored IDs.
TEST(NifSidecars, BoneIdHashIsReflectedCrc32OfLowercaseName) {
  EXPECT_EQ(fo4::nif::bone_id_hash(""), 0U);
  EXPECT_EQ(fo4::nif::bone_id_hash("123456789"), 0x2DFD2D88U);
  EXPECT_EQ(fo4::nif::bone_id_hash("RArm_UpperArm"), 0xB2E2764FU);
  EXPECT_EQ(fo4::nif::bone_id_hash("rarm_upperarm"), 0xB2E2764FU);
}

// Detects: a reader rejecting vanilla .ssf layouts (Generic base, delta bones) or null documents; those files must parse with sorted shape keys.
TEST(NifSidecars, ReadsVanillaSsfLayoutAndNullDocument) {
  const auto shapes = fo4::nif::read_ssf(
      R"({"Head":{"BaseBoneName":"Generic","DeltaBones":[{"BoneDeltaList":[1,2],"BoneName":"Bip01 Spine"}],"uiNumDeltas":1},"Arm":{"BaseBoneName":"DISABLED"}})");
  ASSERT_EQ(shapes.size(), 2U);
  EXPECT_EQ(shapes[0].name, "Arm");
  ASSERT_TRUE(shapes[0].base_bone.has_value());
  EXPECT_EQ(*shapes[0].base_bone, "DISABLED");
  EXPECT_TRUE(shapes[0].deltas.empty());
  EXPECT_EQ(shapes[1].name, "Head");
  ASSERT_TRUE(shapes[1].base_bone.has_value());
  EXPECT_EQ(*shapes[1].base_bone, "Generic");
  ASSERT_EQ(shapes[1].deltas.size(), 1U);
  EXPECT_EQ(shapes[1].deltas[0].bone_name, "Bip01 Spine");
  EXPECT_EQ(shapes[1].deltas[0].segment_ids, (std::vector<std::uint32_t>{1, 2}));
  EXPECT_TRUE(fo4::nif::read_ssf("null").empty());
}

// Detects: a segment ID that goes negative or wraps when narrowed to 32 bits; malformed sidecars must fail with a parse error that names the field.
TEST(NifSidecars, RejectsMalformedSsfDocuments) {
  expect_parse_error(
      [] { fo4::nif::read_ssf(R"({"A":{"BaseBoneName":"x","DeltaBones":[{"BoneDeltaList":[-1],"BoneName":"b"}]}})"); },
      "segment id must be an unsigned integer");
  expect_parse_error(
      [] { fo4::nif::read_ssf(R"({"A":{"BaseBoneName":"x","DeltaBones":[{"BoneDeltaList":[4294967296],"BoneName":"b"}]}})"); },
      "segment id exceeds 32 bits");
  expect_parse_error([] { fo4::nif::read_ssf("{"); }, "parse error");
}

// Detects: the writer emitting DeltaBones or uiNumDeltas for a shape with no deltas; vanilla files omit both, so the writer must omit them too.
TEST(NifSidecars, WriterOmitsDeltaFieldsForShapeWithoutDeltas) {
  const auto text = fo4::nif::write_ssf({fo4::nif::SsfShape{"Head", "Generic", {}, {}}});
  EXPECT_EQ(text.find("DeltaBones"), std::string::npos);
  EXPECT_EQ(text.find("uiNumDeltas"), std::string::npos);
}

// Detects: a .ssf write and read cycle losing delta order, segment IDs or base bones; the round trip must return the same shapes.
TEST(NifSidecars, SsfRoundTripsShapesWithDeltas) {
  const std::vector<fo4::nif::SsfShape> shapes{
      {"Arm", "DISABLED", {}, {}},
      {"Head", "Generic", {{"Bip01 Head", {0, 4294967295U}}, {"Neck", {}}}, {}}};
  EXPECT_EQ(fo4::nif::read_ssf(fo4::nif::write_ssf(shapes)), shapes);
}

// Detects: a shape without BaseBoneName, or a uiNumDeltas that is the total BoneDeltaList length rather than the DeltaBones count, being dropped or misread; the whole document must come back unchanged.
TEST(NifSidecars, KeepsUnmodelledShapeFieldsAndRecountsUiNumDeltas) {
  const std::string text = R"({"headgear3":{"Deltas":[1,2,3],"bAllSegmentsAreEnabled":true,"uiNumDeltas":3},"HeavyLegsR_0":{"CollisionFilterDeltas":[4],"uiBaseCollisionFilter":15,"uiNumDeltas":1},"Arm":{"BaseBoneName":"Generic","DeltaBones":[{"BoneDeltaList":[1,2],"BoneName":"Bip01"},{"BoneDeltaList":[3],"BoneName":"Bip02"}],"uiNumDeltas":3},"Pad":{"BaseBoneName":"Generic","DeltaBones":[],"uiNumDeltas":0}})";
  const auto shapes = fo4::nif::read_ssf(text);
  ASSERT_EQ(shapes.size(), 4U);
  const auto& arm = shapes[0];
  EXPECT_EQ(arm.name, "Arm");
  ASSERT_TRUE(arm.base_bone.has_value());
  EXPECT_EQ(*arm.base_bone, "Generic");
  ASSERT_EQ(arm.deltas.size(), 2U);
  EXPECT_EQ(arm.deltas[0].segment_ids, (std::vector<std::uint32_t>{1, 2}));
  EXPECT_TRUE(arm.other_fields.empty());
  EXPECT_EQ(shapes[1].name, "HeavyLegsR_0");
  EXPECT_FALSE(shapes[1].base_bone.has_value());
  EXPECT_EQ(nlohmann::json::parse(shapes[1].other_fields),
            nlohmann::json::parse(R"({"CollisionFilterDeltas":[4],"uiBaseCollisionFilter":15,"uiNumDeltas":1})"));
  EXPECT_EQ(shapes[2].name, "Pad");
  EXPECT_TRUE(shapes[2].deltas.empty());
  EXPECT_EQ(nlohmann::json::parse(shapes[2].other_fields), nlohmann::json::parse(R"({"DeltaBones":[],"uiNumDeltas":0})"));
  EXPECT_EQ(shapes[3].name, "headgear3");
  EXPECT_FALSE(shapes[3].base_bone.has_value());
  EXPECT_EQ(nlohmann::json::parse(fo4::nif::write_ssf(shapes)), nlohmann::json::parse(text));
  EXPECT_EQ(fo4::nif::write_ssf({}), "null");
}

// Detects: float precision loss in .sclp scale values across a write and read cycle; the scales must come back bit for bit.
TEST(NifSidecars, SclpRoundTripsScalesExactly) {
  const std::vector<fo4::nif::SclpBone> bones{{"Bip01 Spine", {0.1f, 1.0f / 3.0f, 1e-7f}}, {"Root", {2.5f, 0.0f, 3.25f}}};
  EXPECT_EQ(fo4::nif::read_sclp(fo4::nif::write_sclp(bones)), bones);
}

// Detects: an .sclp entry missing a scale axis being read as zero, which would collapse the bone; the read must fail on the missing axis.
TEST(NifSidecars, RejectsSclpEntryMissingAScaleAxis) {
  expect_parse_error([] { fo4::nif::read_sclp(R"([{"Name":"Bip","Scale":{"x":1,"y":2}}])"); }, "missing z");
}
