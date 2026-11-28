#include "fo4/error.hpp"
#include "fo4/nif/anim.hpp"
#include "fo4/nif/controller_keys.hpp"
#include "fo4/nif/nif.hpp"
#include "fo4/nif/nif_write.hpp"
#include "fo4/nif/sclp_fit.hpp"
#include "fo4/nif/sidecar_generate.hpp"
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
  const auto shared = shared_segment_bytes({0, 5}, {{7, 9, {0.5f, 1.5f}}, {8, 10, {}}, {9, 11, {}}}, "Data\\Fx.ssf");
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
  table.segment_starts = {0};
  table.user_data = std::vector<fo4::nif::SegmentUserData>(5);
  table.user_data[0] = {5, 11, {2.0f}};
  table.ssf_file = "Data\\New.ssf";
  const std::vector<fo4::nif::Edit> edits{fo4::nif::Edit{0, "segments", table}};
  const auto reread = fo4::nif::read_nif(fo4::nif::apply_edits(nif, edits));
  const auto back = fo4::nif::read_segments(reread, 0);
  EXPECT_EQ(back.primitives, 9U);
  EXPECT_EQ(back.total_segments, 5U);
  ASSERT_EQ(back.segments.size(), 1U);
  ASSERT_EQ(back.segments[0].sub_segments.size(), 1U);
  EXPECT_EQ(back.segments[0].sub_segments[0].unused, 0x1234U);
  EXPECT_EQ(back.segment_starts, (std::vector<std::uint32_t>{0}));
  ASSERT_EQ(back.user_data.size(), 5U);
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

TEST(NifSegments, RefusesSharedTableTheSsfGeneratorCannotInterpret) {
  const fo4::nif::Segment first{0, 1, 0, {{0, 1, 0, 0}}};
  const fo4::nif::Segment second{1, 2, 1, {}};
  const auto shared = shared_segment_bytes({0, 2}, {{0, 0, {}}, {1, 0, {}}, {0, 0, {}}}, "");
  const auto nif = fo4::nif::read_nif(
      build_nif({"shape"}, {sub_index_shape_block(-1, -1, segment_table_bytes(3, 3, {first, second}, shared))}, {}));
  const auto apply = [&nif](const fo4::nif::SegmentTable& table) {
    const std::vector<fo4::nif::Edit> edits{fo4::nif::Edit{0, "segments", table}};
    return fo4::nif::apply_edits(nif, edits);
  };
  const auto valid = fo4::nif::read_segments(nif, 0);
  auto cleared = valid;
  cleared.segment_starts.clear();
  expect_parse_error([&] { apply(cleared); }, "segment starts are not one per segment");
  auto short_user_data = valid;
  short_user_data.user_data.pop_back();
  expect_parse_error([&] { apply(short_user_data); }, "user data is not one entry per total segment");
  auto past_end = valid;
  past_end.segment_starts[0] = 2;
  expect_parse_error([&] { apply(past_end); }, "names user data past its last entry");
  EXPECT_NO_THROW(apply(valid));
}

TEST(NifSegments, ValidSharedTableSavesAndGeneratesItsSsf) {
  const fo4::nif::Segment first{0, 1, 0, {{0, 1, 0, 0}}};
  const fo4::nif::Segment second{1, 2, 1, {}};
  const auto bone = fo4::nif::bone_id_hash("RArm_UpperArm");
  const auto shared = shared_segment_bytes({0, 2}, {{0, 0, {}}, {1, bone, {0.5f}}, {0, 0, {}}}, "");
  const auto nif = fo4::nif::read_nif(
      build_nif({"shape"}, {sub_index_shape_block(-1, -1, segment_table_bytes(3, 3, {first, second}, shared))}, {}));
  const std::vector<fo4::nif::Edit> edits{fo4::nif::Edit{0, "segments", fo4::nif::read_segments(nif, 0)}};
  const auto saved = fo4::nif::read_nif(fo4::nif::apply_edits(nif, edits));
  const auto table = fo4::nif::read_segments(saved, 0);
  const std::vector<std::string> strings{"RArm_UpperArm"};
  const auto shape = fo4::nif::ssf_shape_from_segments(table, "Arm", strings);
  ASSERT_EQ(shape.deltas.size(), 1U);
  EXPECT_EQ(shape.deltas[0].bone_name, "RArm_UpperArm");
  EXPECT_EQ(shape.deltas[0].segment_ids, (std::vector<std::uint32_t>{0x0U}));
  EXPECT_NO_THROW(fo4::nif::generate_ssf(table, "Arm", strings));
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

TEST(NifSegments, RefusesMoreSegmentsThanTheSsfIdCanEncode) {
  const fo4::nif::Segment segment{0, 0, 0, {}};
  const auto nif = fo4::nif::read_nif(
      build_nif({"shape"}, {sub_index_shape_block(-1, -1, segment_table_bytes(1, 1, {segment}))}, {}));
  const auto apply = [&](std::size_t count) {
    fo4::nif::SegmentTable table;
    table.primitives = 1;
    table.total_segments = static_cast<std::uint32_t>(count);
    table.segments.assign(count, segment);
    const std::vector<fo4::nif::Edit> edits{fo4::nif::Edit{0, "segments", table}};
    return fo4::nif::apply_edits(nif, edits);
  };
  EXPECT_NO_THROW(apply(65535));
  expect_parse_error([&] { apply(65536); }, "too many segments to encode in an .ssf id");
}

TEST(NifSegments, RefusesMoreSubSegmentsThanTheSsfIdCanEncode) {
  const fo4::nif::Segment segment{0, 1, 0, {}};
  const auto nif = fo4::nif::read_nif(
      build_nif({"shape"}, {sub_index_shape_block(-1, -1, segment_table_bytes(1, 1, {segment}))}, {}));
  const auto apply = [&](std::size_t subs) {
    fo4::nif::SegmentTable table;
    table.primitives = 12;
    table.total_segments = 1;
    table.segments = {{0, 0, 0, std::vector<fo4::nif::SubSegment>(subs)}};
    const std::vector<fo4::nif::Edit> edits{fo4::nif::Edit{0, "segments", table}};
    return fo4::nif::apply_edits(nif, edits);
  };
  EXPECT_NO_THROW(apply(9));
  EXPECT_NO_THROW(apply(256));
  expect_parse_error([&] { apply(257); }, "too many sub-segments to encode in an .ssf id");
}

TEST(NifSegments, SharedTableWithoutSubSegmentsSavesWithEmptyStartsAndUserData) {
  const fo4::nif::Segment segment{0, 1, 0, {}};
  const auto nif = fo4::nif::read_nif(
      build_nif({"shape"}, {sub_index_shape_block(-1, -1, segment_table_bytes(1, 1, {segment}))}, {}));
  auto table = fo4::nif::read_segments(nif, 0);
  table.total_segments = 3;
  const std::vector<fo4::nif::Edit> edits{fo4::nif::Edit{0, "segments", table}};
  const auto saved = fo4::nif::read_nif(fo4::nif::apply_edits(nif, edits));
  const auto back = fo4::nif::read_segments(saved, 0);
  EXPECT_EQ(back.total_segments, 3U);
  ASSERT_EQ(back.segments.size(), 1U);
  EXPECT_TRUE(back.segment_starts.empty());
  EXPECT_TRUE(back.user_data.empty());
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

TEST(NifSidecars, RefusesDuplicateShapeNamesInTheWriter) {
  const std::vector<fo4::nif::SsfShape> shapes{
      {"Head", "Generic", {{"Bip01 Head", {1}, {}}}, {}},
      {"Head", "Generic", {{"Neck", {2, 3}, {}}}, {}}};
  expect_parse_error([&] { fo4::nif::write_ssf(shapes); }, "duplicate SSF shape name: Head");
}

// Detects: a .ssf write and read cycle losing delta order, segment IDs or base bones; the round trip must return the same shapes.
TEST(NifSidecars, SsfRoundTripsShapesWithDeltas) {
  const std::vector<fo4::nif::SsfShape> shapes{
      {"Arm", "DISABLED", {}, {}},
      {"Head", "Generic", {{"Bip01 Head", {0, 4294967295U}, {}}, {"Neck", {}, {}}}, {}}};
  EXPECT_EQ(fo4::nif::read_ssf(fo4::nif::write_ssf(shapes)), shapes);
}

// Detects: a shape without BaseBoneName, or a uiNumDeltas that is the total BoneDeltaList length rather than the DeltaBones count, being dropped or misread; the whole document must come back unchanged.
TEST(NifSidecars, KeepsUnmodelledShapeFieldsAndRecountsUiNumDeltas) {
  const std::string text = R"({"headgear3":{"Deltas":[1,2,3],"bAllSegmentsAreEnabled":true,"uiNumDeltas":3},"HeavyLegsR_0":{"CollisionFilterDeltas":[4],"uiBaseCollisionFilter":15,"uiNumDeltas":1},"Arm":{"BaseBoneName":"Generic","DeltaBones":[{"BoneDeltaList":[1,2],"BoneName":"Bip01","UnmodelledBoneField":7},{"BoneDeltaList":[3],"BoneName":"Bip02"}],"uiNumDeltas":3},"Pad":{"BaseBoneName":"Generic","DeltaBones":[],"uiNumDeltas":0}})";
  const auto shapes = fo4::nif::read_ssf(text);
  ASSERT_EQ(shapes.size(), 4U);
  const auto& arm = shapes[0];
  EXPECT_EQ(arm.name, "Arm");
  ASSERT_TRUE(arm.base_bone.has_value());
  EXPECT_EQ(*arm.base_bone, "Generic");
  ASSERT_EQ(arm.deltas.size(), 2U);
  EXPECT_EQ(arm.deltas[0].segment_ids, (std::vector<std::uint32_t>{1, 2}));
  EXPECT_EQ(nlohmann::json::parse(arm.deltas[0].other_fields), nlohmann::json::parse(R"({"UnmodelledBoneField":7})"));
  EXPECT_TRUE(arm.deltas[1].other_fields.empty());
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
  const std::vector<fo4::nif::SclpBone> bones{{"Bip01 Spine", {0.1f, 1.0f / 3.0f, 1e-7f}}, {"Root", {2.5f, 0.5f, 3.25f}}};
  EXPECT_EQ(fo4::nif::read_sclp(fo4::nif::write_sclp(bones)), bones);
}

// Detects: an .sclp entry missing a scale axis being read as zero, which would collapse the bone; the read must fail on the missing axis.
TEST(NifSidecars, RejectsSclpEntryMissingAScaleAxis) {
  expect_parse_error([] { fo4::nif::read_sclp(R"([{"Name":"Bip","Scale":{"x":1,"y":2}}])"); }, "must hold x, y and z only");
}

TEST(NifSidecars, RefusesSclpScalesOutsideTheFloatRange) {
  expect_parse_error([] { fo4::nif::read_sclp(R"([{"Name":"Bip","Scale":{"x":1e300,"y":1,"z":1}}])"); },
                     "exceeds the float range");
  expect_parse_error([] { fo4::nif::read_sclp(R"([{"Name":"Bip","Scale":{"x":1e-50,"y":1,"z":1}}])"); },
                     "must be a positive finite float");
}

TEST(NifSidecars, RefusesNonFiniteDirectScalesOnWrite) {
  const std::vector<fo4::nif::SclpBone> nan_scale{{"Bip", {std::numeric_limits<float>::quiet_NaN(), 1.0f, 1.0f}}};
  const std::vector<fo4::nif::SclpBone> inf_scale{{"Bip", {1.0f, std::numeric_limits<float>::infinity(), 1.0f}}};
  expect_parse_error([&] { fo4::nif::write_sclp(nan_scale); }, "must be a positive finite float");
  expect_parse_error([&] { fo4::nif::write_sclp(inf_scale); }, "must be a positive finite float");
}

TEST(NifSidecars, SclpWriterRefusesWhatTheClothReaderRefuses) {
  const std::vector<fo4::nif::SclpBone> empty_name{{"", {1.0f, 1.0f, 1.0f}}};
  const std::vector<fo4::nif::SclpBone> zero_scale{{"Bip", {0.0f, 1.0f, 1.0f}}};
  const std::vector<fo4::nif::SclpBone> repeated{{"Bip", {1.0f, 1.0f, 1.0f}}, {"Bip", {2.0f, 1.0f, 1.0f}}};
  expect_parse_error([&] { fo4::nif::write_sclp(empty_name); }, "cloth profile bone name is empty");
  expect_parse_error([&] { fo4::nif::write_sclp(zero_scale); }, "must be a positive finite float");
  expect_parse_error([&] { fo4::nif::write_sclp(repeated); }, "cloth profile bone name repeats");
}

TEST(NifSidecars, RefusesSclpEntryWithAnExtraKey) {
  expect_parse_error(
      [] { fo4::nif::read_sclp(R"([{"Name":"Bip","Scale":{"x":1,"y":1,"z":1},"Extra":1}])"); },
      "must hold Name and Scale only");
}

// Invariant: fitted scales equal the fitted-over-reference ratio with unscaled bones skipped.
TEST(SclpFit, ComputesPerAxisRatiosAndSkipsTheUnscaled) {
  const auto fit = fo4::nif::fit_sclp_scales({{"Calf", {1.0f, 2.0f, 0.0f}, {2.0f, 2.0f, 5.0f}},
                                              {"Toe", {0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 1.0f}}});
  ASSERT_EQ(fit.bones.size(), 1U);
  EXPECT_EQ(fit.bones[0].name, "Calf");
  EXPECT_FLOAT_EQ(fit.bones[0].scale[0], 2.0f);
  EXPECT_FLOAT_EQ(fit.bones[0].scale[1], 1.0f);
  EXPECT_FLOAT_EQ(fit.bones[0].scale[2], 1.0f);
  ASSERT_EQ(fit.skipped.size(), 1U);
  EXPECT_NE(fit.skipped[0].find("Toe"), std::string::npos);
  EXPECT_EQ(fo4::nif::read_sclp(fo4::nif::write_sclp(fit.bones)), fit.bones);
}

// Constraint: a direction flip or a duplicated bone is a refusal.
TEST(SclpFit, RefusesANonPositiveRatio) {
  EXPECT_THROW(fo4::nif::fit_sclp_scales({{"X", {1.0f, 1.0f, 1.0f}, {-1.0f, 1.0f, 1.0f}}}),
               std::invalid_argument);
  EXPECT_THROW(fo4::nif::fit_sclp_scales({{"X", {1.0f, 1.0f, 1.0f}, {1.0f, 1.0f, 1.0f}},
                                          {"X", {1.0f, 1.0f, 1.0f}, {1.0f, 1.0f, 1.0f}}}),
               std::invalid_argument);
}

Block float_data_block() {
  Writer w;
  w.u32(2);
  w.u32(1);
  w.f32(0.0f);
  w.f32(1.5f);
  w.f32(1.0f);
  w.f32(2.5f);
  return Block{"NiFloatData", w.bytes()};
}

// Invariant: key times and values round-trip through write and re-read.
TEST(ControllerKeys, FloatDataRoundTripsThroughWrite) {
  const auto nif = fo4::nif::read_nif(build_nif({}, {float_data_block()}, {}));
  auto keys = fo4::nif::read_controller_keys(nif, 0);
  EXPECT_EQ(keys.block_type, "NiFloatData");
  ASSERT_EQ(keys.data.keys.size(), 2U);
  EXPECT_EQ(keys.data.interpolation, fo4::nif::k_key_linear);
  EXPECT_FLOAT_EQ(keys.data.keys[0].time, 0.0f);
  EXPECT_FLOAT_EQ(keys.data.keys[1].time, 1.0f);
  keys.data.keys[1].time = 2.0f;
  const auto rewritten = fo4::nif::write_controller_keys(nif, 0, keys);
  const auto reread = fo4::nif::read_controller_keys(fo4::nif::read_nif(rewritten), 0);
  ASSERT_EQ(reread.data.keys.size(), 2U);
  EXPECT_FLOAT_EQ(reread.data.keys[1].time, 2.0f);
  keys.data.keys[0].value.resize(2);
  EXPECT_THROW(fo4::nif::write_controller_keys(nif, 0, keys), std::invalid_argument);
}

Block transform_data_block() {
  Writer w;
  w.u32(1);
  w.u32(1);
  w.f32(0.0f);
  w.f32(0.0f);
  w.f32(0.0f);
  w.f32(0.0f);
  w.f32(1.0f);
  w.u32(1);
  w.u32(1);
  w.f32(0.0f);
  w.f32(1.0f);
  w.f32(2.0f);
  w.f32(3.0f);
  w.u32(0);
  return Block{"NiTransformData", w.bytes()};
}

// Invariant: the rotation group and translation values survive the write.
TEST(ControllerKeys, TransformDataRoundTripsRotationsAndTranslations) {
  const auto nif = fo4::nif::read_nif(build_nif({}, {transform_data_block()}, {}));
  auto keys = fo4::nif::read_controller_keys(nif, 0);
  EXPECT_EQ(keys.rotation_type, fo4::nif::k_key_linear);
  ASSERT_EQ(keys.rotation_quat.keys.size(), 1U);
  ASSERT_EQ(keys.translations.keys.size(), 1U);
  EXPECT_TRUE(keys.scales.keys.empty());
  keys.translations.keys[0].time = 4.0f;
  const auto rewritten = fo4::nif::write_controller_keys(nif, 0, keys);
  const auto reread = fo4::nif::read_controller_keys(fo4::nif::read_nif(rewritten), 0);
  ASSERT_EQ(reread.translations.keys.size(), 1U);
  EXPECT_FLOAT_EQ(reread.translations.keys[0].time, 4.0f);
  EXPECT_EQ(reread.translations.keys[0].value, keys.translations.keys[0].value);
  keys.translations.keys.push_back({-1.0f, keys.translations.keys[0].value});
  EXPECT_THROW(fo4::nif::write_controller_keys(nif, 0, keys), std::invalid_argument);
}

// Layout source: nif.xml QuatKey ("Never has tangents") and TBC (three floats).
Block quat_block(std::uint32_t interpolation, std::size_t tail_floats) {
  Writer w;
  w.u32(1);
  w.u32(interpolation);
  w.f32(0.0f);
  w.f32(0.0f);
  w.f32(0.0f);
  w.f32(0.0f);
  w.f32(1.0f);
  for (std::size_t i = 0; i < tail_floats; ++i) w.f32(0.25f);
  w.u32(0);
  w.u32(0);
  return Block{"NiTransformData", w.bytes()};
}

// Regression: quadratic rotation keys were read with two quaternion tangents (52 bytes a key). nif.xml QuatKey has
// no tangents, so a real quadratic NiTransformData was refused or decoded from the wrong offsets.
TEST(ControllerKeys, QuadraticRotationKeysAreBareQuaternions) {
  const auto nif = fo4::nif::read_nif(build_nif({}, {quat_block(fo4::nif::k_key_quadratic, 0)}, {}));
  auto keys = fo4::nif::read_controller_keys(nif, 0);
  EXPECT_EQ(keys.rotation_type, fo4::nif::k_key_quadratic);
  ASSERT_EQ(keys.rotation_quat.keys.size(), 1U);
  EXPECT_EQ(keys.rotation_quat.keys[0].value.size(), 16U);
  keys.rotation_quat.keys[0].time = 1.5f;
  const auto rewritten = fo4::nif::write_controller_keys(nif, 0, keys);
  const auto reread = fo4::nif::read_controller_keys(fo4::nif::read_nif(rewritten), 0);
  ASSERT_EQ(reread.rotation_quat.keys.size(), 1U);
  EXPECT_FLOAT_EQ(reread.rotation_quat.keys[0].time, 1.5f);
  EXPECT_EQ(reread.rotation_quat.keys[0].value, keys.rotation_quat.keys[0].value);
  // Constraint: the old 52-byte key no longer parses as one key.
  EXPECT_THROW(fo4::nif::read_controller_keys(
                   fo4::nif::read_nif(build_nif({}, {quat_block(fo4::nif::k_key_quadratic, 8)}, {})), 0),
               fo4::parse_error);
}

// Regression: the TBC tail was read as 16 bytes. nif.xml TBC is three floats, so a TBC rotation key is the
// quaternion plus 12 bytes and the groups after it start where the file puts them.
TEST(ControllerKeys, TbcRotationKeysCarryTwelveTbcBytes) {
  const auto nif = fo4::nif::read_nif(build_nif({}, {quat_block(fo4::nif::k_key_tbc, 3)}, {}));
  const auto keys = fo4::nif::read_controller_keys(nif, 0);
  ASSERT_EQ(keys.rotation_quat.keys.size(), 1U);
  EXPECT_EQ(keys.rotation_quat.keys[0].value.size(), 28U);
  EXPECT_TRUE(keys.translations.keys.empty());
  EXPECT_EQ(fo4::nif::write_controller_keys(nif, 0, keys), nif.bytes());
}

// Regression: same 16-byte TBC misread on plain key groups; one TBC float key is value plus three floats.
TEST(ControllerKeys, TbcFloatKeysRoundTripWithTheirTbcWords) {
  Writer w;
  w.u32(2);
  w.u32(3);
  w.f32(0.5f);
  w.f32(2.0f);
  for (int i = 0; i < 3; ++i) w.f32(0.0f);
  w.f32(1.0f);
  w.f32(4.0f);
  for (int i = 0; i < 3; ++i) w.f32(0.0f);
  const auto nif = fo4::nif::read_nif(build_nif({}, {Block{"NiFloatData", w.bytes()}}, {}));
  auto keys = fo4::nif::read_controller_keys(nif, 0);
  ASSERT_EQ(keys.data.keys.size(), 2U);
  EXPECT_EQ(keys.data.keys[0].value.size(), 16U);
  EXPECT_FLOAT_EQ(keys.data.keys[1].time, 1.0f);
  keys.data.keys[0].time = 0.75f;
  const auto rewritten = fo4::nif::write_controller_keys(nif, 0, keys);
  const auto reread = fo4::nif::read_controller_keys(fo4::nif::read_nif(rewritten), 0);
  ASSERT_EQ(reread.data.keys.size(), 2U);
  EXPECT_FLOAT_EQ(reread.data.keys[0].time, 0.75f);
}

// Regression: text keys were read as a count followed by inline sized strings. FO4 stores the NiExtraData name
// first and every text as a header string-table index (the layout read_text_keys already decodes), so a real
// NiTextKeyExtraData took its name index for the key count.
TEST(ControllerKeys, TextKeysAreStringTableIndicesAfterTheBlockName) {
  Writer w;
  w.i32(0);
  w.u32(2);
  w.f32(0.5f);
  w.i32(1);
  w.f32(1.5f);
  w.i32(2);
  const auto nif = fo4::nif::read_nif(build_nif({"keys", "start", "end"}, {Block{"NiTextKeyExtraData", w.bytes()}}, {}));
  auto keys = fo4::nif::read_controller_keys(nif, 0);
  ASSERT_EQ(keys.data.keys.size(), 2U);
  EXPECT_FLOAT_EQ(keys.data.keys[0].time, 0.5f);
  Writer start;
  start.i32(1);
  EXPECT_EQ(keys.data.keys[0].value, start.bytes());
  const auto texts = fo4::nif::read_text_keys(nif, 0);
  ASSERT_EQ(texts.size(), 2U);
  EXPECT_EQ(texts[1].text, "end");

  // Invariant: retiming and retargeting a key keeps the block name and reads back through the anim reader.
  keys.data.keys[1].time = 2.0f;
  keys.data.keys[1].value = start.bytes();
  const auto rewritten = fo4::nif::read_nif(fo4::nif::write_controller_keys(nif, 0, keys));
  const auto reread = fo4::nif::read_text_keys(rewritten, 0);
  ASSERT_EQ(reread.size(), 2U);
  EXPECT_FLOAT_EQ(reread[1].time, 2.0f);
  EXPECT_EQ(reread[1].text, "start");

  // Constraint: an index past the string table is a refusal.
  Writer past;
  past.i32(9);
  keys.data.keys[1].value = past.bytes();
  EXPECT_THROW(fo4::nif::write_controller_keys(nif, 0, keys), std::invalid_argument);
}
