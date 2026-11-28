#include "fo4/hkx/layout.hpp"
#include "fo4/hkx/packfile.hpp"
#include "fo4/hkx/skeleton.hpp"
#include "fo4/hkx/skeleton_mapper.hpp"
#include "fo4/hkx/write.hpp"
#include "hkx_mapper_fixture.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <optional>
#include <string>
#include <vector>

namespace {

using fo4::hkx::apply_mapper_edit;
using fo4::hkx::build_skeleton_mapping;
using fo4::hkx::check_mapping_pairs;
using fo4::hkx::find_layout;
using fo4::hkx::HkxLocalFixup;
using fo4::hkx::HkxMapperChainRow;
using fo4::hkx::HkxMapperEdit;
using fo4::hkx::HkxMapperEditKind;
using fo4::hkx::HkxMapperSimpleRow;
using fo4::hkx::HkxMapperTransform;
using fo4::hkx::HkxMappingPair;
using fo4::hkx::HkxPackfile;
using fo4::hkx::HkxQsTransform;
using fo4::hkx::HkxSkeleton;
using fo4::hkx::HkxSkeletonMapper;
using fo4::hkx::HkxStatus;
using fo4::hkx::read_packfile;
using fo4::hkx::read_skeleton_mappers;
using fo4::hkx::recompute_mapping_row;
using fo4::hkx::reverse_mapping;
using fo4::hkx::write_skeleton_mappers;
using fo4::test::hkx::default_mapper_spec;
using fo4::test::hkx::mapper_lanes;
using fo4::test::hkx::mapper_packfile;
using fo4::test::hkx::MapperBoneSpec;
using fo4::test::hkx::MapperSpec;
using fo4::test::hkx::member_at;

constexpr float kS = 0.70710678f;
constexpr float kNear = 1e-6f;
constexpr float kNan = std::numeric_limits<float>::quiet_NaN();
const std::array<float, 4> kIdentity{0.0f, 0.0f, 0.0f, 1.0f};

std::vector<HkxSkeletonMapper> read_mappers(const std::vector<std::byte>& bytes) {
  HkxPackfile packfile;
  EXPECT_EQ(read_packfile(bytes, packfile), HkxStatus::Ok);
  std::vector<HkxSkeletonMapper> mappers;
  EXPECT_EQ(read_skeleton_mappers(packfile, mappers), HkxStatus::Ok);
  return mappers;
}

HkxSkeleton make_skeleton(const std::string& name, const std::vector<MapperBoneSpec>& bones) {
  HkxSkeleton skeleton;
  skeleton.name = name;
  for (const MapperBoneSpec& bone : bones) {
    skeleton.bone_names.push_back(bone.name);
    skeleton.parent_indices.push_back(bone.parent);
    skeleton.reference_pose.push_back(HkxQsTransform{bone.position, bone.rotation, {1.0f, 1.0f, 1.0f}});
    skeleton.lock_translation.push_back(false);
  }
  return skeleton;
}

std::size_t rows_header(const HkxPackfile& packfile, const HkxSkeletonMapper& mapper) {
  return packfile.objects[mapper.object_index].offset + member_at("hkaSkeletonMapper", "mapping") +
         member_at("hkaSkeletonMapperData", "simpleMappings");
}

std::optional<HkxLocalFixup> local_at(const HkxPackfile& packfile, std::size_t source) {
  for (const HkxLocalFixup& fixup : packfile.sections[packfile.data_section].local_fixups) {
    if (fixup.source == source) return fixup;
  }
  return std::nullopt;
}

std::uint32_t u32_at(const HkxPackfile& packfile, std::size_t at) {
  std::uint32_t value = 0;
  std::memcpy(&value, packfile.sections[packfile.data_section].data.data() + at, sizeof value);
  return value;
}

std::size_t differing_bytes(const std::vector<std::byte>& a, const std::vector<std::byte>& b) {
  std::size_t count = 0;
  for (std::size_t i = 0; i < a.size() && i < b.size(); ++i) count += a[i] != b[i] ? 1 : 0;
  return count;
}

bool same_values(const HkxSkeletonMapper& a, const HkxSkeletonMapper& b) {
  return a.mapping_type == b.mapping_type && a.keep_unmapped_local == b.keep_unmapped_local && a.rows == b.rows &&
         a.chains == b.chains && a.unmapped_bones == b.unmapped_bones && a.extracted_motion == b.extracted_motion;
}

void expect_pose(const HkxMapperTransform& transform, const std::array<float, 3>& translation,
                 const std::array<float, 4>& rotation) {
  for (std::size_t axis = 0; axis < 3; ++axis) EXPECT_NEAR(transform.lanes[axis], translation[axis], kNear) << axis;
  for (std::size_t axis = 0; axis < 4; ++axis) EXPECT_NEAR(transform.lanes[4 + axis], rotation[axis], kNear) << axis;
}

bool mentions(const std::string& text, const char* needle) { return text.find(needle) != std::string::npos; }

struct Written {
  HkxStatus status{HkxStatus::Ok};
  std::string refusal;
  std::vector<std::byte> bytes;
};

Written write_edited(const std::vector<std::byte>& source, const std::vector<HkxSkeletonMapper>& edited) {
  Written written;
  written.bytes = {std::byte{1}};
  written.status = write_skeleton_mappers(source, edited, written.bytes, &written.refusal);
  return written;
}

void expect_refused(const std::vector<std::byte>& source, const std::vector<HkxSkeletonMapper>& edited,
                    const char* reason) {
  const Written written = write_edited(source, edited);
  EXPECT_NE(written.status, HkxStatus::Ok);
  EXPECT_TRUE(mentions(written.refusal, reason)) << written.refusal;
  EXPECT_EQ(written.bytes.size(), 1U);
}

HkxMapperSimpleRow row_of(std::int16_t bone_a, std::int16_t bone_b, const std::array<float, 12>& lanes) {
  HkxMapperSimpleRow row;
  row.bone_a = bone_a;
  row.bone_b = bone_b;
  row.a_from_b.lanes = lanes;
  return row;
}

TEST(SkeletonMapperHkx, TransformEqualityIsBitwise) {
  HkxMapperTransform a = HkxMapperTransform::identity();
  HkxMapperTransform b = a;
  EXPECT_TRUE(a == b);
  b.lanes[3] = 0.5f;
  EXPECT_FALSE(a == b);
  a.lanes[11] = kNan;
  b = a;
  EXPECT_TRUE(a == b);
  EXPECT_EQ(HkxMapperTransform::identity().lanes[7], 1.0f);
  EXPECT_EQ(HkxMapperTransform::identity().lanes[9], 1.0f);
}

TEST(SkeletonMapperHkx, ReadsEveryMember) {
  const MapperSpec spec = default_mapper_spec();
  const auto mappers = read_mappers(mapper_packfile(spec));
  ASSERT_EQ(mappers.size(), 1U);
  const HkxSkeletonMapper& mapper = mappers[0];
  EXPECT_EQ(mapper.skeleton_a.name, "RigA");
  EXPECT_EQ(mapper.skeleton_b.name, "RigB");
  EXPECT_EQ(mapper.skeleton_a.bone_names.size(), 3U);
  EXPECT_EQ(mapper.skeleton_b.bone_names[2], "Extra");
  EXPECT_NE(mapper.skeleton_a_object, mapper.skeleton_b_object);
  EXPECT_EQ(mapper.mapping_type, 1);
  EXPECT_TRUE(mapper.keep_unmapped_local);
  ASSERT_EQ(mapper.unmapped_bones.size(), 1U);
  EXPECT_EQ(mapper.unmapped_bones[0], 2);
  EXPECT_EQ(mapper.extracted_motion.lanes, spec.extracted);
  ASSERT_EQ(mapper.rows.size(), 2U);
  EXPECT_EQ(mapper.rows[1].bone_a, 1);
  EXPECT_EQ(mapper.rows[1].bone_b, 1);
  EXPECT_EQ(mapper.rows[0].a_from_b.lanes, spec.rows[0].lanes);
  EXPECT_EQ(mapper.rows[0].a_from_b.lanes[3], 0.125f);
  ASSERT_EQ(mapper.chains.size(), 1U);
  EXPECT_EQ(mapper.chains[0].end_bone_a, 2);
  EXPECT_EQ(mapper.chains[0].end_a_from_b.lanes[0], 0.5f);
}

TEST(SkeletonMapperHkx, ReadIsStrict) {
  HkxPackfile packfile;
  std::vector<HkxSkeletonMapper> mappers;

  MapperSpec bad_translation = default_mapper_spec();
  bad_translation.rows[0].lanes[1] = kNan;
  auto bytes = mapper_packfile(bad_translation);
  ASSERT_EQ(read_packfile(bytes, packfile), HkxStatus::Ok);
  EXPECT_NE(read_skeleton_mappers(packfile, mappers), HkxStatus::Ok);

  MapperSpec zero_rotation = default_mapper_spec();
  zero_rotation.rows[1].lanes = mapper_lanes(0, 0, 0, 0, 0, 0, 0);
  bytes = mapper_packfile(zero_rotation);
  ASSERT_EQ(read_packfile(bytes, packfile), HkxStatus::Ok);
  EXPECT_NE(read_skeleton_mappers(packfile, mappers), HkxStatus::Ok);

  MapperSpec not_a_skeleton = default_mapper_spec();
  not_a_skeleton.skeleton_a_is_not_a_skeleton = true;
  bytes = mapper_packfile(not_a_skeleton);
  ASSERT_EQ(read_packfile(bytes, packfile), HkxStatus::Ok);
  EXPECT_NE(read_skeleton_mappers(packfile, mappers), HkxStatus::Ok);

  bytes = fo4::test::hkx::clip_packfile(true);
  ASSERT_EQ(read_packfile(bytes, packfile), HkxStatus::Ok);
  EXPECT_EQ(read_skeleton_mappers(packfile, mappers), HkxStatus::MissingObject);
}

TEST(SkeletonMapperHkx, PaddingLanesAreNotChecked) {
  MapperSpec spec = default_mapper_spec();
  spec.rows[0].lanes[3] = kNan;
  spec.rows[0].lanes[11] = kNan;
  const auto mappers = read_mappers(mapper_packfile(spec));
  ASSERT_EQ(mappers.size(), 1U);
  ASSERT_EQ(mappers[0].rows.size(), 2U);
  EXPECT_EQ(std::memcmp(mappers[0].rows[0].a_from_b.lanes.data(), spec.rows[0].lanes.data(), 48), 0);
}

TEST(SkeletonMapperHkx, UnchangedModelWritesACopy) {
  const auto source = mapper_packfile(default_mapper_spec());
  const Written written = write_edited(source, read_mappers(source));
  ASSERT_EQ(written.status, HkxStatus::Ok) << written.refusal;
  EXPECT_EQ(written.bytes, source);
}

TEST(SkeletonMapperHkx, ValueEditPatchesInPlace) {
  const auto source = mapper_packfile(default_mapper_spec());
  auto edited = read_mappers(source);
  edited[0].rows[1].a_from_b.lanes[0] = 0.5f;
  edited[0].mapping_type = 0;
  edited[0].keep_unmapped_local = false;
  edited[0].chains[0].end_a_from_b.lanes[0] = 0.75f;
  const Written written = write_edited(source, edited);
  ASSERT_EQ(written.status, HkxStatus::Ok) << written.refusal;
  EXPECT_EQ(written.bytes.size(), source.size());
  EXPECT_GT(differing_bytes(source, written.bytes), 0U);
  EXPECT_LE(differing_bytes(source, written.bytes), 4U + 1U + 4U + 4U);
  const auto back = read_mappers(written.bytes);
  ASSERT_EQ(back.size(), 1U);
  EXPECT_TRUE(same_values(back[0], edited[0]));
}

TEST(SkeletonMapperHkx, RowBonesAndPaddingSurviveAValueEdit) {
  const auto source = mapper_packfile(default_mapper_spec());
  auto edited = read_mappers(source);
  edited[0].rows[0].bone_a = 2;
  edited[0].rows[0].bone_b = 2;
  edited[0].rows[0].a_from_b.lanes[3] = 0.75f;
  const Written written = write_edited(source, edited);
  ASSERT_EQ(written.status, HkxStatus::Ok) << written.refusal;
  const auto back = read_mappers(written.bytes);
  ASSERT_EQ(back.size(), 1U);
  EXPECT_EQ(back[0].rows[0].bone_a, 2);
  EXPECT_EQ(back[0].rows[0].a_from_b.lanes[3], 0.75f);
  EXPECT_TRUE(back[0].rows[1] == edited[0].rows[1]);
}

TEST(SkeletonMapperHkx, AppendingARowRelocatesTheRun) {
  const auto source = mapper_packfile(default_mapper_spec());
  HkxPackfile before;
  ASSERT_EQ(read_packfile(source, before), HkxStatus::Ok);
  auto edited = read_mappers(source);
  const std::size_t header = rows_header(before, edited[0]);
  const auto old_fixup = local_at(before, header);
  ASSERT_TRUE(old_fixup.has_value());
  edited[0].rows.push_back(row_of(2, 2, mapper_lanes(0, 0, 3, 0, 0, 0, 1, 0.5f)));

  const Written written = write_edited(source, edited);
  ASSERT_EQ(written.status, HkxStatus::Ok) << written.refusal;
  HkxPackfile after;
  ASSERT_EQ(read_packfile(written.bytes, after), HkxStatus::Ok);
  const auto back = read_mappers(written.bytes);
  ASSERT_EQ(back.size(), 1U);
  EXPECT_TRUE(same_values(back[0], edited[0]));
  EXPECT_EQ(back[0].rows.size(), 3U);
  EXPECT_EQ(after.objects.size(), before.objects.size());

  const auto fixup = local_at(after, header);
  ASSERT_TRUE(fixup.has_value());
  EXPECT_EQ(fixup->destination % 16U, 0U);
  EXPECT_GE(fixup->destination, before.sections[before.data_section].data.size());
  EXPECT_NE(fixup->destination, old_fixup->destination);
  EXPECT_EQ(u32_at(after, header + 8), 3U);
  EXPECT_EQ(u32_at(after, header + 12), 0x80000000u | 3U);
  const auto& data = after.sections[after.data_section];
  const std::size_t stride = find_layout("hkaSkeletonMapperDataSimpleMapping")->size;
  for (std::size_t i = 0; i < 2 * stride; ++i) EXPECT_EQ(data.data[old_fixup->destination + i], std::byte{0}) << i;
  EXPECT_EQ(data.data[fixup->destination + 4], std::byte{0xA0});
  EXPECT_EQ(data.data[fixup->destination + stride + 4], std::byte{0xA1});
  const auto& locals = data.local_fixups;
  EXPECT_TRUE(std::is_sorted(locals.begin(), locals.end(), [](const HkxLocalFixup& a, const HkxLocalFixup& b) {
    return a.source < b.source;
  }));
  EXPECT_EQ(locals.size(), before.sections[before.data_section].local_fixups.size());
}

TEST(SkeletonMapperHkx, ShrinkingKeepsTheSurvivingRows) {
  const auto source = mapper_packfile(default_mapper_spec());
  auto edited = read_mappers(source);
  edited[0].rows.erase(edited[0].rows.begin());
  const Written written = write_edited(source, edited);
  ASSERT_EQ(written.status, HkxStatus::Ok) << written.refusal;
  const auto back = read_mappers(written.bytes);
  ASSERT_EQ(back.size(), 1U);
  ASSERT_EQ(back[0].rows.size(), 1U);
  EXPECT_EQ(back[0].rows[0].bone_a, 1);
  EXPECT_TRUE(same_values(back[0], edited[0]));
}

TEST(SkeletonMapperHkx, RemovingEveryRowDropsTheFixup) {
  const auto source = mapper_packfile(default_mapper_spec());
  auto edited = read_mappers(source);
  edited[0].rows.clear();
  const Written written = write_edited(source, edited);
  ASSERT_EQ(written.status, HkxStatus::Ok) << written.refusal;
  HkxPackfile after;
  ASSERT_EQ(read_packfile(written.bytes, after), HkxStatus::Ok);
  const std::size_t header = rows_header(after, edited[0]);
  EXPECT_FALSE(local_at(after, header).has_value());
  EXPECT_EQ(u32_at(after, header + 8), 0U);
  EXPECT_EQ(u32_at(after, header + 12) & 0x3FFFFFFFu, 0U);
  EXPECT_EQ(u32_at(after, header), 0U);
  EXPECT_EQ(u32_at(after, header + 4), 0U);
  const auto back = read_mappers(written.bytes);
  ASSERT_EQ(back.size(), 1U);
  EXPECT_TRUE(back[0].rows.empty());
  EXPECT_EQ(back[0].chains.size(), 1U);
}

TEST(SkeletonMapperHkx, GrowingFromNoRowsAddsTheFixup) {
  MapperSpec spec = default_mapper_spec();
  spec.rows.clear();
  const auto source = mapper_packfile(spec);
  auto edited = read_mappers(source);
  ASSERT_TRUE(edited[0].rows.empty());
  edited[0].rows.push_back(row_of(0, 0, mapper_lanes(0, 0, 0, 0, 0, 0, 1)));
  edited[0].rows.push_back(row_of(1, 1, mapper_lanes(0, 1, 0, 0, 0, 0, 1)));
  const Written written = write_edited(source, edited);
  ASSERT_EQ(written.status, HkxStatus::Ok) << written.refusal;
  HkxPackfile after;
  ASSERT_EQ(read_packfile(written.bytes, after), HkxStatus::Ok);
  const std::size_t header = rows_header(after, edited[0]);
  const auto fixup = local_at(after, header);
  ASSERT_TRUE(fixup.has_value());
  EXPECT_EQ(fixup->destination % 16U, 0U);
  EXPECT_EQ(u32_at(after, header + 8), 2U);
  const auto back = read_mappers(written.bytes);
  ASSERT_EQ(back.size(), 1U);
  EXPECT_TRUE(same_values(back[0], edited[0]));
}

TEST(SkeletonMapperHkx, ResizeCarriesAValueEditOnTheSameMapper) {
  const auto source = mapper_packfile(default_mapper_spec());
  auto edited = read_mappers(source);
  edited[0].rows.push_back(row_of(2, 2, mapper_lanes(0, 0, 3, 0, 0, 0, 1)));
  edited[0].rows[0].a_from_b.lanes[0] = 4.0f;
  edited[0].mapping_type = 0;
  const Written written = write_edited(source, edited);
  ASSERT_EQ(written.status, HkxStatus::Ok) << written.refusal;
  const auto back = read_mappers(written.bytes);
  ASSERT_EQ(back.size(), 1U);
  EXPECT_TRUE(same_values(back[0], edited[0]));
}

TEST(SkeletonMapperHkx, WriterRefusals) {
  const auto source = mapper_packfile(default_mapper_spec());
  const auto original = read_mappers(source);

  auto chain_added = original;
  chain_added[0].chains.push_back(chain_added[0].chains[0]);
  expect_refused(source, chain_added, "chain");

  auto bad_bone = original;
  bad_bone[0].rows[0].bone_a = 9;
  expect_refused(source, bad_bone, "is not a bone");

  auto not_unit = original;
  not_unit[0].rows[0].a_from_b.lanes[4] = 2.0f;
  expect_refused(source, not_unit, "unit");

  auto not_finite = original;
  not_finite[0].rows[1].a_from_b.lanes[2] = kNan;
  expect_refused(source, not_finite, "non-finite");

  auto zero_length = original;
  for (std::size_t lane = 4; lane < 8; ++lane) zero_length[0].rows[0].a_from_b.lanes[lane] = 0.0f;
  expect_refused(source, zero_length, "zero-length");

  auto bad_type = original;
  bad_type[0].mapping_type = 2;
  expect_refused(source, bad_type, "mapping type");

  auto moved_skeleton = original;
  moved_skeleton[0].skeleton_a_object = moved_skeleton[0].skeleton_b_object;
  expect_refused(source, moved_skeleton, "skeletons");

  auto unmapped_changed = original;
  unmapped_changed[0].unmapped_bones.push_back(1);
  expect_refused(source, unmapped_changed, "unmapped");

  expect_refused(source, {}, "mapper(s)");
}

TEST(SkeletonMapperHkx, SharedRowStorageIsRefused) {
  MapperSpec spec = default_mapper_spec();
  spec.second_mapper_shares_rows = true;
  const auto source = mapper_packfile(spec);
  const auto original = read_mappers(source);
  ASSERT_EQ(original.size(), 2U);

  auto value_edit = original;
  value_edit[0].rows[0].a_from_b.lanes[0] = 1.0f;
  expect_refused(source, value_edit, "shares");

  auto resize = original;
  resize[0].rows.pop_back();
  expect_refused(source, resize, "shares");

  auto type_only = original;
  type_only[0].mapping_type = 0;
  const Written written = write_edited(source, type_only);
  EXPECT_EQ(written.status, HkxStatus::Ok) << written.refusal;
}

TEST(SkeletonMapperHkx, PartitionRangesBlockOnlyAResize) {
  MapperSpec spec = default_mapper_spec();
  spec.partition_ranges = true;
  const auto source = mapper_packfile(spec);
  const auto original = read_mappers(source);

  auto resize = original;
  resize[0].rows.pop_back();
  expect_refused(source, resize, "simpleMappingPartitionRanges");

  auto value_edit = original;
  value_edit[0].rows[1].a_from_b.lanes[0] = 0.5f;
  const Written written = write_edited(source, value_edit);
  ASSERT_EQ(written.status, HkxStatus::Ok) << written.refusal;
  const auto back = read_mappers(written.bytes);
  ASSERT_EQ(back.size(), 1U);
  EXPECT_TRUE(same_values(back[0], value_edit[0]));
}

TEST(SkeletonMapperHkx, ABuiltMappingRoundTripsThroughTheWriter) {
  MapperSpec spec = default_mapper_spec();
  spec.rows.clear();
  const auto source = mapper_packfile(spec);
  auto edited = read_mappers(source);
  const std::vector<HkxMappingPair> pairs = {{0, 0}, {1, 1}, {2, 2}};
  ASSERT_EQ(build_skeleton_mapping(edited[0].skeleton_a, edited[0].skeleton_b, pairs, edited[0].rows), "");
  ASSERT_EQ(edited[0].rows.size(), 3U);
  expect_pose(edited[0].rows[2].a_from_b, {0, 0, 3}, kIdentity);
  const Written written = write_edited(source, edited);
  ASSERT_EQ(written.status, HkxStatus::Ok) << written.refusal;
  const auto back = read_mappers(written.bytes);
  ASSERT_EQ(back.size(), 1U);
  EXPECT_TRUE(back[0].rows == edited[0].rows);
}

HkxSkeleton one_bone(const std::array<float, 3>& position, const std::array<float, 4>& rotation) {
  return make_skeleton("One", {{"Bone", -1, position, rotation}});
}

TEST(SkeletonMapperHkx, RotationRowFollowsTheHamiltonOrder) {
  const HkxSkeleton a = one_bone({0, 0, 0}, {kS, 0, 0, kS});
  const HkxSkeleton b = one_bone({0, 0, 0}, {0, kS, 0, kS});
  const std::vector<HkxMappingPair> pairs = {{0, 0}};
  std::vector<HkxMapperSimpleRow> rows;
  ASSERT_EQ(build_skeleton_mapping(a, b, pairs, rows), "");
  ASSERT_EQ(rows.size(), 1U);
  expect_pose(rows[0].a_from_b, {0, 0, 0}, {-0.5f, 0.5f, -0.5f, 0.5f});
  EXPECT_EQ(rows[0].a_from_b.lanes[3], 0.0f);
  EXPECT_EQ(rows[0].a_from_b.lanes[8], 1.0f);
  EXPECT_EQ(rows[0].a_from_b.lanes[11], 0.0f);

  std::vector<HkxMapperSimpleRow> swapped;
  ASSERT_EQ(build_skeleton_mapping(b, a, pairs, swapped), "");
  expect_pose(swapped[0].a_from_b, {0, 0, 0}, {0.5f, -0.5f, 0.5f, 0.5f});
  EXPECT_GT(swapped[0].a_from_b.lanes[6], 0.0f);
}

TEST(SkeletonMapperHkx, TranslationRowIsInverseOfAThenB) {
  const HkxSkeleton a = one_bone({1, 0, 0}, kIdentity);
  const HkxSkeleton b = one_bone({0, 2, 0}, kIdentity);
  std::vector<HkxMapperSimpleRow> rows;
  ASSERT_EQ(build_skeleton_mapping(a, b, std::vector<HkxMappingPair>{{0, 0}}, rows), "");
  expect_pose(rows[0].a_from_b, {-1, 2, 0}, kIdentity);
}

TEST(SkeletonMapperHkx, ChildOfATurnedRootMapsToAnIdentityRoot) {
  const HkxSkeleton a = make_skeleton("Turned", {{"Root", -1, {0, 0, 0}, {kS, 0, 0, kS}}, {"Child", 0, {0, 1, 0}, kIdentity}});
  const HkxSkeleton b = one_bone({0, 0, 0}, kIdentity);
  std::vector<HkxMapperSimpleRow> rows;
  ASSERT_EQ(build_skeleton_mapping(a, b, std::vector<HkxMappingPair>{{1, 0}}, rows), "");
  expect_pose(rows[0].a_from_b, {0, -1, 0}, {-kS, 0, 0, kS});

  std::vector<HkxMapperSimpleRow> reversed;
  ASSERT_EQ(reverse_mapping(rows, reversed), "");
  std::vector<HkxMapperSimpleRow> direct;
  ASSERT_EQ(build_skeleton_mapping(b, a, std::vector<HkxMappingPair>{{0, 1}}, direct), "");
  ASSERT_EQ(reversed.size(), 1U);
  EXPECT_EQ(reversed[0].bone_a, 0);
  EXPECT_EQ(reversed[0].bone_b, 1);
  for (std::size_t lane = 0; lane < 12; ++lane) {
    EXPECT_NEAR(reversed[0].a_from_b.lanes[lane], direct[0].a_from_b.lanes[lane], 1e-5f) << lane;
  }
  EXPECT_EQ(reversed[0].a_from_b.lanes[3], 0.0f);
  EXPECT_EQ(reversed[0].a_from_b.lanes[8], 1.0f);
}

TEST(SkeletonMapperHkx, RecomputeKeepsScaleAndPadding) {
  const HkxSkeleton a = one_bone({1, 0, 0}, kIdentity);
  const HkxSkeleton b = one_bone({0, 2, 0}, kIdentity);
  HkxMapperTransform transform = HkxMapperTransform::identity();
  transform.lanes = {9.0f, 9.0f, 9.0f, 0.5f, 0.0f, 0.0f, 0.0f, 1.0f, 2.0f, 3.0f, 4.0f, 0.25f};
  ASSERT_EQ(recompute_mapping_row(a, 0, b, 0, transform), "");
  expect_pose(transform, {-1, 2, 0}, kIdentity);
  EXPECT_EQ(transform.lanes[3], 0.5f);
  EXPECT_EQ(transform.lanes[8], 2.0f);
  EXPECT_EQ(transform.lanes[9], 3.0f);
  EXPECT_EQ(transform.lanes[10], 4.0f);
  EXPECT_EQ(transform.lanes[11], 0.25f);

  const HkxMapperTransform before = transform;
  EXPECT_TRUE(mentions(recompute_mapping_row(a, 5, b, 0, transform), "is not a bone"));
  EXPECT_TRUE(transform == before);
}

TEST(SkeletonMapperHkx, PairChecksRefuseBadInput) {
  const HkxSkeleton a = make_skeleton("A", {{"Root", -1, {0, 0, 0}, kIdentity}, {"Kid", 0, {0, 1, 0}, kIdentity}});
  const HkxSkeleton b = one_bone({0, 0, 0}, kIdentity);
  EXPECT_EQ(check_mapping_pairs(a, b, std::vector<HkxMappingPair>{{1, 0}}), "");
  EXPECT_TRUE(mentions(check_mapping_pairs(a, b, std::vector<HkxMappingPair>{}), "at least one"));
  EXPECT_TRUE(mentions(check_mapping_pairs(a, b, std::vector<HkxMappingPair>{{0, 0}, {0, 0}}), "already mapped"));
  EXPECT_TRUE(mentions(check_mapping_pairs(a, b, std::vector<HkxMappingPair>{{2, 0}}), "is not a bone"));
  EXPECT_TRUE(mentions(check_mapping_pairs(a, b, std::vector<HkxMappingPair>{{-1, 0}}), "is not a bone"));

  HkxSkeleton forward = a;
  forward.parent_indices[0] = 1;
  EXPECT_TRUE(mentions(check_mapping_pairs(forward, b, std::vector<HkxMappingPair>{{1, 0}}), "before children"));

  HkxSkeleton long_rotation = a;
  long_rotation.reference_pose[1].rotation = {0, 0, 0, 2};
  EXPECT_TRUE(mentions(check_mapping_pairs(long_rotation, b, std::vector<HkxMappingPair>{{1, 0}}), "unit quaternion"));

  HkxSkeleton scaled = a;
  scaled.reference_pose[1].scale = {2, 2, 2};
  EXPECT_TRUE(mentions(check_mapping_pairs(scaled, b, std::vector<HkxMappingPair>{{1, 0}}), "scale"));

  std::vector<HkxMapperSimpleRow> rows = {row_of(7, 7, mapper_lanes(0, 0, 0, 0, 0, 0, 1))};
  std::vector<HkxMapperSimpleRow> built;
  EXPECT_FALSE(build_skeleton_mapping(forward, b, std::vector<HkxMappingPair>{{1, 0}}, built).empty());
  EXPECT_TRUE(built.empty());
  rows[0].a_from_b.lanes[4] = 3.0f;
  std::vector<HkxMapperSimpleRow> reversed;
  EXPECT_TRUE(mentions(reverse_mapping(rows, reversed), "cannot be inverted"));
  EXPECT_TRUE(reversed.empty());
}

HkxMapperEdit edit_of(HkxMapperEditKind kind, std::size_t row = 0) {
  HkxMapperEdit edit;
  edit.kind = kind;
  edit.row = row;
  return edit;
}

TEST(SkeletonMapperHkx, EditsApplyToTheModel) {
  const auto mappers = read_mappers(mapper_packfile(default_mapper_spec()));
  const HkxSkeletonMapper& mapper = mappers[0];
  HkxSkeletonMapper out;

  HkxMapperEdit bones = edit_of(HkxMapperEditKind::SetRowBones, 1);
  bones.bone_a = 2;
  bones.bone_b = 2;
  auto report = apply_mapper_edit(mapper, bones, out);
  ASSERT_TRUE(report.changed) << report.refusal;
  EXPECT_EQ(out.rows[1].bone_a, 2);
  EXPECT_EQ(out.rows[1].bone_b, 2);
  EXPECT_TRUE(out.rows[1].a_from_b == mapper.rows[1].a_from_b);

  HkxMapperEdit transform = edit_of(HkxMapperEditKind::SetRowTransform, 0);
  transform.transform.lanes = {1, 2, 3, 0.5f, 0, 0, 0, 1, 1, 1, 1, 0};
  report = apply_mapper_edit(mapper, transform, out);
  ASSERT_TRUE(report.changed) << report.refusal;
  EXPECT_TRUE(out.rows[0].a_from_b == transform.transform);

  HkxMapperEdit pose = edit_of(HkxMapperEditKind::SetRowPose, 0);
  pose.transform.lanes = {7, 8, 9, 0.9f, 0, 0, 0, 1, 5, 5, 5, 0.9f};
  report = apply_mapper_edit(mapper, pose, out);
  ASSERT_TRUE(report.changed) << report.refusal;
  EXPECT_EQ(out.rows[0].a_from_b.lanes[0], 7.0f);
  EXPECT_EQ(out.rows[0].a_from_b.lanes[3], 0.125f);
  EXPECT_EQ(out.rows[0].a_from_b.lanes[8], 1.0f);
  EXPECT_EQ(out.rows[0].a_from_b.lanes[11], 0.0f);

  HkxMapperEdit insert = edit_of(HkxMapperEditKind::InsertRow, 1);
  insert.bone_a = 2;
  insert.bone_b = 2;
  report = apply_mapper_edit(mapper, insert, out);
  ASSERT_TRUE(report.changed) << report.refusal;
  ASSERT_EQ(out.rows.size(), 3U);
  EXPECT_EQ(out.rows[1].bone_a, 2);
  expect_pose(out.rows[1].a_from_b, {0, 0, 3}, kIdentity);
  EXPECT_EQ(out.rows[1].a_from_b.lanes[8], 1.0f);
  EXPECT_EQ(out.rows[2].bone_a, 1);

  HkxMapperEdit remove = edit_of(HkxMapperEditKind::RemoveRow, 0);
  report = apply_mapper_edit(mapper, remove, out);
  ASSERT_TRUE(report.changed) << report.refusal;
  ASSERT_EQ(out.rows.size(), 1U);
  EXPECT_EQ(out.rows[0].bone_a, 1);

  HkxMapperEdit chain_bones = edit_of(HkxMapperEditKind::SetChainBones, 0);
  chain_bones.bone_a = 0;
  chain_bones.end_bone_a = 1;
  chain_bones.bone_b = 0;
  chain_bones.end_bone_b = 2;
  report = apply_mapper_edit(mapper, chain_bones, out);
  ASSERT_TRUE(report.changed) << report.refusal;
  EXPECT_EQ(out.chains[0].end_bone_b, 2);
  EXPECT_TRUE(out.chains[0].start_a_from_b == mapper.chains[0].start_a_from_b);

  HkxMapperEdit chain_transforms = edit_of(HkxMapperEditKind::SetChainTransforms, 0);
  chain_transforms.transform.lanes[0] = 3.0f;
  chain_transforms.end_transform.lanes[0] = 4.0f;
  report = apply_mapper_edit(mapper, chain_transforms, out);
  ASSERT_TRUE(report.changed) << report.refusal;
  EXPECT_EQ(out.chains[0].start_a_from_b.lanes[0], 3.0f);
  EXPECT_EQ(out.chains[0].end_a_from_b.lanes[0], 4.0f);

  HkxMapperEdit type = edit_of(HkxMapperEditKind::SetMappingType);
  type.mapping_type = 0;
  report = apply_mapper_edit(mapper, type, out);
  ASSERT_TRUE(report.changed) << report.refusal;
  EXPECT_EQ(out.mapping_type, 0);

  HkxMapperEdit keep = edit_of(HkxMapperEditKind::SetKeepUnmappedLocal);
  keep.flag = false;
  report = apply_mapper_edit(mapper, keep, out);
  ASSERT_TRUE(report.changed) << report.refusal;
  EXPECT_FALSE(out.keep_unmapped_local);

  HkxSkeletonMapper stale = mapper;
  stale.rows[1].a_from_b.lanes = {9, 9, 9, 0.5f, 0, 0, 0, 1, 2, 2, 2, 0.25f};
  HkxMapperEdit recompute = edit_of(HkxMapperEditKind::RecomputeRow, 1);
  report = apply_mapper_edit(stale, recompute, out);
  ASSERT_TRUE(report.changed) << report.refusal;
  expect_pose(out.rows[1].a_from_b, {0, 1, 0}, kIdentity);
  EXPECT_EQ(out.rows[1].a_from_b.lanes[3], 0.5f);
  EXPECT_EQ(out.rows[1].a_from_b.lanes[8], 2.0f);
  EXPECT_EQ(out.rows[1].a_from_b.lanes[11], 0.25f);
}

TEST(SkeletonMapperHkx, RefusedEditsLeaveTheOutputAlone) {
  const auto mappers = read_mappers(mapper_packfile(default_mapper_spec()));
  const HkxSkeletonMapper& mapper = mappers[0];
  HkxSkeletonMapper out;
  out.mapping_type = 77;

  const auto refused = [&](const HkxMapperEdit& edit, const char* reason) {
    const auto report = apply_mapper_edit(mapper, edit, out);
    EXPECT_FALSE(report.changed);
    EXPECT_TRUE(mentions(report.refusal, reason)) << report.refusal;
    EXPECT_EQ(out.mapping_type, 77);
    EXPECT_TRUE(out.rows.empty());
  };

  HkxMapperEdit duplicate = edit_of(HkxMapperEditKind::SetRowBones, 1);
  duplicate.bone_a = 0;
  duplicate.bone_b = 1;
  refused(duplicate, "already mapped");

  HkxMapperEdit insert_duplicate = edit_of(HkxMapperEditKind::InsertRow, 0);
  insert_duplicate.bone_a = 1;
  insert_duplicate.bone_b = 2;
  refused(insert_duplicate, "already mapped");

  HkxMapperEdit far_row = edit_of(HkxMapperEditKind::RemoveRow, 5);
  refused(far_row, "no row");

  HkxMapperEdit far_insert = edit_of(HkxMapperEditKind::InsertRow, 5);
  far_insert.bone_a = 2;
  far_insert.bone_b = 2;
  refused(far_insert, "no position");

  HkxMapperEdit bad_bone = edit_of(HkxMapperEditKind::SetRowBones, 0);
  bad_bone.bone_a = 9;
  refused(bad_bone, "is not a bone");

  HkxMapperEdit bad_chain = edit_of(HkxMapperEditKind::SetChainBones, 0);
  bad_chain.end_bone_b = 9;
  refused(bad_chain, "is not a bone");

  HkxMapperEdit long_rotation = edit_of(HkxMapperEditKind::SetRowTransform, 0);
  long_rotation.transform.lanes[7] = 2.0f;
  refused(long_rotation, "unit");

  HkxMapperEdit not_finite = edit_of(HkxMapperEditKind::SetRowTransform, 0);
  not_finite.transform.lanes[1] = kNan;
  refused(not_finite, "non-finite");

  HkxMapperEdit zero_length = edit_of(HkxMapperEditKind::SetRowTransform, 0);
  for (std::size_t lane = 4; lane < 8; ++lane) zero_length.transform.lanes[lane] = 0.0f;
  refused(zero_length, "zero-length");

  HkxMapperEdit bad_type = edit_of(HkxMapperEditKind::SetMappingType);
  bad_type.mapping_type = 2;
  refused(bad_type, "mapping type");

  HkxMapperEdit far_chain = edit_of(HkxMapperEditKind::SetChainTransforms, 3);
  refused(far_chain, "no row");
}

TEST(SkeletonMapperHkx, ADuplicatePairNeedsTheExplicitFlag) {
  const auto mappers = read_mappers(mapper_packfile(default_mapper_spec()));
  HkxSkeletonMapper out;
  HkxMapperEdit insert = edit_of(HkxMapperEditKind::InsertRowWithTransform, 2);
  insert.bone_a = 1;
  insert.bone_b = 2;
  EXPECT_FALSE(apply_mapper_edit(mappers[0], insert, out).changed);
  insert.allow_duplicate_pair = true;
  const auto report = apply_mapper_edit(mappers[0], insert, out);
  ASSERT_TRUE(report.changed) << report.refusal;
  EXPECT_EQ(out.rows.size(), 3U);
  EXPECT_TRUE(out.rows[2].a_from_b == insert.transform);
}

}  // namespace
