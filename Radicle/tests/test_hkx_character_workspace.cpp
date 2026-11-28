#include "fo4/hkx/character_workspace.hpp"
#include "fo4/hkx/packfile.hpp"
#include "fo4/hkx/skeleton.hpp"
#include "fo4/nif/nif.hpp"
#include "fo4/nif/skin_binding.hpp"
#include "hkx_mapper_fixture.hpp"
#include "hkx_physics_fixture.hpp"
#include "nif_build.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

using namespace fo4::hkx;
using namespace fo4::test::hkx;
using fo4::nif::Transform;
using nif_build::Block;
using nif_build::Writer;

constexpr std::uint32_t k_skin_flags = nif_build::k_full | nif_build::k_has_skin;
constexpr std::size_t k_skin_stride = 28;

struct SkinnedVertex {
  float position[3];
  std::uint16_t weights[4];
  std::uint8_t bones[4];
};

// Constraint: weights are half floats, 0x3C00 is 1.0, 0x3800 is 0.5, 0xBC00 is -1.0.
const std::vector<SkinnedVertex> k_good{
    {{0.0f, 0.0f, 0.0f}, {0x3C00, 0, 0, 0}, {0, 0, 0, 0}},
    {{1.0f, 0.0f, 0.0f}, {0x3800, 0x3800, 0, 0}, {0, 1, 0, 0}},
    {{0.0f, 1.0f, 0.0f}, {0x3800, 0x3800, 0, 0}, {1, 0, 0, 0}},
};

std::vector<std::byte> skinned_vertex_bytes(const std::vector<SkinnedVertex>& vertices) {
  Writer w;
  for (const auto& vertex : vertices) {
    for (const float value : vertex.position) w.f32(value);
    w.u32(0);
    for (const auto weight : vertex.weights) w.u16(weight);
    for (const auto bone : vertex.bones) w.u8(bone);
  }
  return w.bytes();
}

std::vector<std::byte> skinned_nif(const std::vector<SkinnedVertex>& vertices,
                                   const std::vector<std::string>& bone_names = {"Bone0", "Bone1"}) {
  const std::vector<Block> blocks{
      nif_build::named_node_block("NiNode", 0, Transform{}, {1, 3, 4}),
      nif_build::shape_with_refs("BSTriShape", 1, Transform{}, k_skin_flags, k_skin_stride, 2, -1, -1,
                                 skinned_vertex_bytes(vertices), {0, 1, 2}),
      nif_build::skin_instance_block(5, {3, 4}),
      nif_build::named_node_block("NiNode", 2, Transform{}, {}),
      nif_build::named_node_block("NiNode", 3, Transform{}, {}),
      nif_build::bone_data_block(2),
  };
  return nif_build::build_nif({"Root", "Body", bone_names.at(0), bone_names.at(1)}, blocks, {0});
}

std::vector<std::byte> unskinned_nif() {
  const std::vector<Block> blocks{
      nif_build::named_node_block("NiNode", 0, Transform{}, {1}),
      nif_build::shape_block("BSTriShape", 1, Transform{}, nif_build::k_full, 16, nif_build::triangle_vertices(),
                             {0, 1, 2}),
  };
  return nif_build::build_nif({"Root", "Body"}, blocks, {0});
}

HkxSkeleton skeleton_of(const std::vector<std::string>& names, const std::vector<std::int16_t>& parents) {
  HkxSkeleton skeleton;
  skeleton.name = "Rig";
  skeleton.bone_names = names;
  skeleton.parent_indices = parents;
  HkxQsTransform pose;
  pose.rotation = {0.0f, 0.0f, 0.0f, 1.0f};
  pose.scale = {1.0f, 1.0f, 1.0f};
  skeleton.reference_pose.assign(names.size(), pose);
  return skeleton;
}

HkxSkeleton clean_skeleton() { return skeleton_of({"Root", "Child", "Tip"}, {-1, 0, 1}); }

std::vector<std::byte> character_bytes(const std::vector<std::string>& animation_names = {"Bone0", "Bone1", "Tip"}) {
  PhysicsSpec spec = default_physics_spec();
  spec.animation_rig = {{animation_names.at(0), -1, {0, 0, 0}},
                        {animation_names.at(1), 0, {0, 1, 0}},
                        {animation_names.at(2), 1, {0, 1, 0}}};
  return physics_packfile(spec);
}

bool has(const std::vector<CharacterFinding>& findings, CharacterLevel level, std::string_view area,
         std::string_view text) {
  return std::any_of(findings.begin(), findings.end(), [&](const CharacterFinding& item) {
    return item.level == level && item.area == area &&
           (item.where.find(text) != std::string::npos || item.message.find(text) != std::string::npos);
  });
}

std::size_t count_area(const std::vector<CharacterFinding>& findings, std::string_view area, CharacterLevel level) {
  return static_cast<std::size_t>(std::count_if(findings.begin(), findings.end(), [&](const CharacterFinding& item) {
    return item.area == area && item.level == level;
  }));
}

CharacterSaveDocument document(CharacterDocumentKind kind, std::string source, std::string target = {}) {
  return CharacterSaveDocument{kind, std::move(source), std::move(target)};
}

}  // namespace

TEST(HkxCharacterValidate, ACleanSkeletonHasNoFindings) {
  EXPECT_TRUE(validate_skeleton(clean_skeleton()).empty());
}

TEST(HkxCharacterValidate, AnEmptySkeletonIsAnError) {
  const auto findings = validate_skeleton(HkxSkeleton{});
  EXPECT_TRUE(has(findings, CharacterLevel::error, "skeleton", "it has no bones"));
  EXPECT_FALSE(has(findings, CharacterLevel::error, "skeleton", "no bone is a root"));
}

TEST(HkxCharacterValidate, AShortParallelArrayNamesTheArrayAndBothCounts) {
  auto skeleton = clean_skeleton();
  skeleton.parent_indices.pop_back();
  const auto findings = validate_skeleton(skeleton);
  EXPECT_TRUE(has(findings, CharacterLevel::error, "skeleton", "it holds 2 entries against 3 in boneNames"));
  skeleton = clean_skeleton();
  skeleton.reference_pose.pop_back();
  EXPECT_TRUE(has(validate_skeleton(skeleton), CharacterLevel::error, "skeleton", "referencePose"));
}

TEST(HkxCharacterValidate, ADuplicateOrBlankNameIsNamedWithTheFirstHolder) {
  const auto duplicate = validate_skeleton(skeleton_of({"Root", "Child", "root"}, {-1, 0, 1}));
  EXPECT_TRUE(has(duplicate, CharacterLevel::error, "skeleton", "bone 0 is already called 'root'"));
  const auto blank = validate_skeleton(skeleton_of({"Root", "  ", "Tip"}, {-1, 0, 1}));
  EXPECT_TRUE(has(blank, CharacterLevel::error, "skeleton", "it has no name"));
}

TEST(HkxCharacterValidate, AParentStoredAfterItsChildOrItselfIsAnError) {
  const auto later = validate_skeleton(skeleton_of({"A", "B", "C"}, {-1, 2, 0}));
  EXPECT_TRUE(has(later, CharacterLevel::error, "skeleton", "stored after it"));
  const auto self = validate_skeleton(skeleton_of({"A", "B"}, {-1, 1}));
  EXPECT_TRUE(has(self, CharacterLevel::error, "skeleton", "it is its own parent"));
  const auto past = validate_skeleton(skeleton_of({"A", "B"}, {-1, 5}));
  EXPECT_TRUE(has(past, CharacterLevel::error, "skeleton", "past the last bone"));
}

TEST(HkxCharacterValidate, AParentCycleIsNamedOnEveryBoneOnIt) {
  const auto findings = validate_skeleton(skeleton_of({"A", "B"}, {1, 0}));
  EXPECT_GE(count_area(findings, "skeleton", CharacterLevel::error), 2u);
  EXPECT_TRUE(has(findings, CharacterLevel::error, "skeleton", "its parent chain comes back to it"));
  EXPECT_TRUE(has(findings, CharacterLevel::error, "skeleton", "no bone is a root"));
}

TEST(HkxCharacterValidate, TwoRootsAreAWarningNotAnError) {
  const auto findings = validate_skeleton(skeleton_of({"A", "B"}, {-1, -1}));
  EXPECT_TRUE(has(findings, CharacterLevel::warning, "skeleton", "2 bones are roots"));
  EXPECT_EQ(count_area(findings, "skeleton", CharacterLevel::error), 0u);
}

TEST(HkxCharacterValidate, PoseFaultsAreSplitIntoErrorsAndAWarning) {
  auto skeleton = clean_skeleton();
  skeleton.reference_pose[1].translation[0] = std::numeric_limits<float>::quiet_NaN();
  skeleton.reference_pose[2].scale = {1.0f, 0.0f, 1.0f};
  EXPECT_TRUE(has(validate_skeleton(skeleton), CharacterLevel::error, "skeleton", "translation is not a finite number"));
  EXPECT_TRUE(has(validate_skeleton(skeleton), CharacterLevel::error, "skeleton", "collapses an axis to zero"));

  skeleton = clean_skeleton();
  skeleton.reference_pose[1].rotation = {0.0f, 0.0f, 0.0f, 2.0f};
  EXPECT_TRUE(has(validate_skeleton(skeleton), CharacterLevel::warning, "skeleton", "long rather than 1"));
  skeleton.reference_pose[1].rotation = {0.0f, 0.0f, 0.0f, 0.0f};
  EXPECT_TRUE(has(validate_skeleton(skeleton), CharacterLevel::error, "skeleton", "has no length"));
}

TEST(HkxCharacterMirror, TheAxisIsTheLaneThatReflectsLeftOntoRight) {
  auto skeleton = skeleton_of({"Root", "LArm", "RArm"}, {-1, 0, 0});
  skeleton.reference_pose[1].translation = {1.0f, 0.0f, 0.0f};
  skeleton.reference_pose[2].translation = {-1.0f, 0.0f, 0.0f};
  const auto report = inspect_mirror_pairs(skeleton);
  EXPECT_EQ(report.pairs, 1u);
  EXPECT_EQ(report.lane, 0);
  EXPECT_NEAR(report.typical, 0.0f, 1e-4f);
  EXPECT_TRUE(report.ambiguous.empty());
}

TEST(HkxCharacterMirror, ARigWithNoPairReportsNoLane) {
  const auto report = inspect_mirror_pairs(clean_skeleton());
  EXPECT_EQ(report.pairs, 0u);
  EXPECT_EQ(report.lane, -1);
}

TEST(HkxCharacterCheck, AnUnreadableSkeletonIsARefusalAndNothingElse) {
  const std::vector<std::byte> junk(16, std::byte{0x41});
  CharacterAssets assets;
  assets.skeleton = junk;
  const auto report = check_character(assets);
  EXPECT_FALSE(report.refusal.empty());
  EXPECT_FALSE(report.ok());
  EXPECT_TRUE(report.findings.empty());
}

TEST(HkxCharacterCheck, ASkeletonRagdollAndMapperInOneFileAreReadFromTheSkeletonBytes) {
  const auto bytes = character_bytes();
  CharacterAssets assets;
  assets.skeleton = bytes;
  const auto report = check_character(assets);
  ASSERT_EQ(report.refusal, "");
  EXPECT_EQ(report.bones, 3u);
  EXPECT_TRUE(report.ragdoll.present);
  EXPECT_EQ(report.ragdoll.bodies, 2u);
  EXPECT_EQ(report.ragdoll.constraints, 1u);
  EXPECT_EQ(report.ragdoll.physics_bones, 2u);
  EXPECT_EQ(report.ragdoll.animation_bones, 3u);
  ASSERT_EQ(report.mappers.size(), 1u);
  EXPECT_EQ(report.mappers[0].rows, 2u);
  EXPECT_TRUE(report.mappers[0].a_has_character_bones);
  EXPECT_FALSE(report.mappers[0].b_has_character_bones);
  EXPECT_EQ(report.errors(), 0u);
  EXPECT_TRUE(report.ok());
}

TEST(HkxCharacterCheck, ASkeletonFileWithNoRagdollIsAnInfoWhenNoRagdollWasGiven) {
  MapperSpec spec = default_mapper_spec();
  spec.rig_a = {{"Root", -1, {0, 0, 0}}, {"Child", 0, {0, 1, 0}}, {"Tip", 1, {0, 1, 0}}, {"Extra", 2, {0, 1, 0}}};
  const auto bytes = mapper_packfile(spec);
  CharacterAssets assets;
  assets.skeleton = bytes;
  const auto report = check_character(assets);
  ASSERT_EQ(report.refusal, "");
  EXPECT_FALSE(report.ragdoll.present);
  EXPECT_EQ(count_area(report.findings, "ragdoll", CharacterLevel::info), 1u);
  EXPECT_EQ(count_area(report.findings, "ragdoll", CharacterLevel::warning), 0u);
  EXPECT_EQ(count_area(report.findings, "ragdoll", CharacterLevel::error), 0u);
  ASSERT_EQ(report.mappers.size(), 1u);
  EXPECT_EQ(report.mappers[0].chains, 1u);
}

TEST(HkxCharacterCheck, AGivenRagdollFileWithNoRagdollIsAWarning) {
  MapperSpec spec = default_mapper_spec();
  spec.rig_a = {{"Root", -1, {0, 0, 0}}, {"Child", 0, {0, 1, 0}}, {"Tip", 1, {0, 1, 0}}, {"Extra", 2, {0, 1, 0}}};
  const auto skeleton = mapper_packfile(spec);
  const auto no_ragdoll = mapper_packfile(default_mapper_spec());
  CharacterAssets assets;
  assets.skeleton = skeleton;
  assets.ragdoll = no_ragdoll;
  const auto report = check_character(assets);
  EXPECT_EQ(count_area(report.findings, "ragdoll", CharacterLevel::warning), 1u);
}

TEST(HkxCharacterCheck, AMapperRowOutsideItsSkeletonIsAnError) {
  MapperSpec spec = default_mapper_spec();
  spec.rig_a = {{"Root", -1, {0, 0, 0}}, {"Child", 0, {0, 1, 0}}, {"Tip", 1, {0, 1, 0}}, {"Extra", 2, {0, 1, 0}}};
  spec.rows = {{0, 0}, {1, 7}};
  const auto bytes = mapper_packfile(spec);
  CharacterAssets assets;
  assets.skeleton = bytes;
  const auto report = check_character(assets);
  EXPECT_TRUE(has(report.findings, CharacterLevel::error, "mapper", "row 1"));
  EXPECT_FALSE(report.ok());
}

TEST(HkxCharacterCheck, ARagdollAnimationSkeletonWithAnotherBoneNameIsAWarning) {
  const auto character = character_bytes();
  const auto other = character_bytes({"Bone0", "Elsewhere", "Tip"});
  CharacterAssets assets;
  assets.skeleton = character;
  assets.ragdoll = other;
  const auto report = check_character(assets);
  EXPECT_TRUE(has(report.findings, CharacterLevel::warning, "ragdoll", "are not in the character skeleton: Elsewhere"));
}

TEST(HkxCharacterCheck, AFullyBoundSkinHasNoSkinFindings) {
  const auto character = character_bytes();
  const auto body = skinned_nif(k_good);
  CharacterAssets assets;
  assets.skeleton = character;
  assets.body_nif = body;
  const auto report = check_character(assets);
  ASSERT_EQ(report.shapes.size(), 1u);
  EXPECT_EQ(report.shapes[0].binding.exact, 2u);
  EXPECT_EQ(report.shapes[0].unbound_vertices, 0u);
  EXPECT_TRUE(report.shapes[0].repairs.empty());
  EXPECT_EQ(report.skin_status, "");
}

TEST(HkxCharacterCheck, AVertexOnABoneTheSkeletonLacksIsCountedAsUnbound) {
  const auto character = character_bytes();
  const auto body = skinned_nif(k_good, {"Bone0", "Nope"});
  CharacterAssets assets;
  assets.skeleton = character;
  assets.body_nif = body;
  const auto report = check_character(assets);
  ASSERT_EQ(report.shapes.size(), 1u);
  EXPECT_EQ(report.shapes[0].binding.unmatched, 1u);
  EXPECT_EQ(report.shapes[0].unbound_vertices, 2u);
  EXPECT_TRUE(has(report.findings, CharacterLevel::warning, "skin", "weighted to a bone the skeleton does not have"));
}

TEST(HkxCharacterCheck, AHelperBoneBridgedBySkeletonNifIsNotUnbound) {
  const auto character = character_bytes({"Pelvis", "Bone1", "Tip"});
  const auto body = skinned_nif(k_good, {"Helper", "Bone1"});
  const std::vector<Block> blocks{
      nif_build::named_node_block("NiNode", 0, Transform{}, {1}),
      nif_build::named_node_block("NiNode", 1, Transform{}, {2}),
      nif_build::named_node_block("NiNode", 2, Transform{}, {}),
  };
  const auto tree = nif_build::build_nif({"Root", "Pelvis", "Helper"}, blocks, {0});
  CharacterAssets assets;
  assets.skeleton = character;
  assets.body_nif = body;
  assets.skeleton_nif = tree;
  const auto report = check_character(assets);
  ASSERT_EQ(report.shapes.size(), 1u);
  EXPECT_EQ(report.shapes[0].binding.node_chain, 1u);
  EXPECT_EQ(report.shapes[0].unbound_vertices, 0u);
}

TEST(HkxCharacterCheck, WeightsThatDoNotSumToOneSuggestNormalising) {
  auto vertices = k_good;
  vertices[1].weights[1] = 0;
  const auto character = character_bytes();
  const auto body = skinned_nif(vertices);
  CharacterAssets assets;
  assets.skeleton = character;
  assets.body_nif = body;
  const auto report = check_character(assets);
  ASSERT_EQ(report.shapes.size(), 1u);
  ASSERT_EQ(report.shapes[0].repairs.size(), 1u);
  EXPECT_EQ(report.shapes[0].repairs[0].kind, CharacterRepairKind::normalize_weights);
  EXPECT_EQ(report.shapes[0].repairs[0].total, 1u);
  EXPECT_EQ(report.shapes[0].repairs[0].vertices, (std::vector<std::uint32_t>{1}));
}

TEST(HkxCharacterCheck, ANegativeWeightOrARepeatedBoneSuggestsSettingInfluences) {
  auto vertices = k_good;
  vertices[2].weights[0] = 0xBC00;
  vertices[2].weights[1] = 0x3C00;
  const auto character = character_bytes();
  const auto body = skinned_nif(vertices);
  CharacterAssets assets;
  assets.skeleton = character;
  assets.body_nif = body;
  auto report = check_character(assets);
  ASSERT_EQ(report.shapes.size(), 1u);
  ASSERT_FALSE(report.shapes[0].repairs.empty());
  EXPECT_EQ(report.shapes[0].repairs[0].kind, CharacterRepairKind::set_influences);
  EXPECT_EQ(report.shapes[0].repairs[0].vertices, (std::vector<std::uint32_t>{2}));

  vertices = k_good;
  vertices[1].bones[1] = 0;
  const auto repeated = skinned_nif(vertices);
  assets.body_nif = repeated;
  report = check_character(assets);
  ASSERT_EQ(report.shapes.size(), 1u);
  ASSERT_FALSE(report.shapes[0].repairs.empty());
  EXPECT_EQ(report.shapes[0].repairs[0].kind, CharacterRepairKind::set_influences);
}

TEST(HkxCharacterCheck, ANifWithNoSkinnedShapeIsAWarningNotARefusal) {
  const auto character = character_bytes();
  const auto body = unskinned_nif();
  CharacterAssets assets;
  assets.skeleton = character;
  assets.body_nif = body;
  const auto report = check_character(assets);
  EXPECT_EQ(report.refusal, "");
  EXPECT_TRUE(report.shapes.empty());
  EXPECT_EQ(report.skin_status, "no_skinned_shape");
  EXPECT_EQ(report.unskinned_shapes, 1u);
  EXPECT_EQ(count_area(report.findings, "skin", CharacterLevel::warning), 1u);
}

TEST(HkxCharacterCheck, AnUnreadableBodyNifIsAFindingAndTheOtherSectionsStillRun) {
  const auto character = character_bytes();
  const std::vector<std::byte> junk(8, std::byte{0x7F});
  CharacterAssets assets;
  assets.skeleton = character;
  assets.body_nif = junk;
  const auto report = check_character(assets);
  EXPECT_EQ(report.skin_status, "parse_error");
  EXPECT_TRUE(has(report.findings, CharacterLevel::error, "skin", ""));
  EXPECT_TRUE(report.ragdoll.present);
}

TEST(HkxSkinShapes, ABlockThatIsNotInTheNifOrNotAShapeIsNamed) {
  const auto nif = fo4::nif::read_nif(skinned_nif(k_good));
  const std::vector<std::string> bones{"Bone0", "Bone1"};
  EXPECT_EQ(fo4::nif::bind_skin_shapes(nif, 99u, bones).status, fo4::nif::SkinShapesStatus::invalid_block);
  EXPECT_EQ(fo4::nif::bind_skin_shapes(nif, 0u, bones).status, fo4::nif::SkinShapesStatus::not_a_shape);
}

TEST(HkxSkinShapes, OneBlockBindsOnlyThatShapeAndAllShapesBindEveryOne) {
  const auto nif = fo4::nif::read_nif(skinned_nif(k_good));
  const std::vector<std::string> bones{"Bone0", "Bone1"};
  const auto one = fo4::nif::bind_skin_shapes(nif, 1u, bones);
  ASSERT_EQ(one.status, fo4::nif::SkinShapesStatus::ok);
  ASSERT_EQ(one.shapes.size(), 1u);
  EXPECT_EQ(one.shapes[0].exact, 2u);
  const auto all = fo4::nif::bind_skin_shapes(nif, std::nullopt, bones);
  EXPECT_EQ(all.status, fo4::nif::SkinShapesStatus::ok);
  EXPECT_EQ(all.shapes.size(), 1u);
  EXPECT_EQ(all.unskinned, 0u);
}

TEST(HkxSkinShapes, AnUnskinnedShapeIsCountedAndAloneIsNoSkinnedShape) {
  const auto nif = fo4::nif::read_nif(unskinned_nif());
  const std::vector<std::string> bones{"Bone0"};
  const auto all = fo4::nif::bind_skin_shapes(nif, std::nullopt, bones);
  EXPECT_EQ(all.status, fo4::nif::SkinShapesStatus::no_skinned_shape);
  EXPECT_EQ(all.unskinned, 1u);
  EXPECT_STREQ(fo4::nif::skin_shapes_status_name(all.status), "no_skinned_shape");
  const auto single = fo4::nif::bind_skin_shapes(nif, 1u, bones);
  EXPECT_EQ(single.status, fo4::nif::SkinShapesStatus::refused);
}

TEST(HkxCharacterSave, DocumentsOnOneSourceChainInSkeletonRagdollMapperOrder) {
  CharacterSaveInput input;
  input.documents = {document(CharacterDocumentKind::mapper, "a.hkx", "m.hkx"),
                     document(CharacterDocumentKind::ragdoll, "a.hkx", "r.hkx"),
                     document(CharacterDocumentKind::skeleton, "a.hkx", "s.hkx")};
  const auto plan = plan_character_save(input);
  EXPECT_TRUE(plan.ok());
  ASSERT_EQ(plan.steps.size(), 3u);
  EXPECT_EQ(plan.steps[0].kind, CharacterDocumentKind::skeleton);
  EXPECT_EQ(plan.steps[0].reads, "a.hkx");
  EXPECT_FALSE(plan.steps[0].reads_previous_output);
  EXPECT_EQ(plan.steps[1].kind, CharacterDocumentKind::ragdoll);
  EXPECT_EQ(plan.steps[1].reads, "s.hkx");
  EXPECT_TRUE(plan.steps[1].reads_previous_output);
  EXPECT_EQ(plan.steps[2].kind, CharacterDocumentKind::mapper);
  EXPECT_EQ(plan.steps[2].reads, "r.hkx");
  EXPECT_EQ(plan.steps[2].writes, "m.hkx");
  EXPECT_TRUE(plan.steps[2].reads_previous_output);
}

TEST(HkxCharacterSave, OnlyASkeletonSaveMayReplaceItsSourceInPlace) {
  CharacterSaveInput input;
  input.documents = {document(CharacterDocumentKind::skeleton, "a.hkx")};
  input.overwrite = true;
  const auto skeleton_plan = plan_character_save(input);
  EXPECT_TRUE(skeleton_plan.ok());
  EXPECT_EQ(skeleton_plan.steps.at(0).writes, "a.hkx");
  input.overwrite = false;

  input.documents = {document(CharacterDocumentKind::ragdoll, "a.hkx")};
  EXPECT_TRUE(has(plan_character_save(input).findings, CharacterLevel::error, "save", "needs a target file"));
  input.documents = {document(CharacterDocumentKind::mapper, "a.hkx")};
  EXPECT_FALSE(plan_character_save(input).ok());
}

TEST(HkxCharacterSave, AnInPlaceSkeletonSaveNeedsOverwrite) {
  CharacterSaveInput input;
  input.documents = {document(CharacterDocumentKind::skeleton, "a.hkx")};
  const auto plan = plan_character_save(input);
  EXPECT_FALSE(plan.ok());
  EXPECT_TRUE(has(plan.findings, CharacterLevel::error, "save", "replace its source file in place"));
  input.documents = {document(CharacterDocumentKind::skeleton, "a.hkx", "A.HKX")};
  EXPECT_TRUE(has(plan_character_save(input).findings, CharacterLevel::error, "save", "its own source file"));
  input.overwrite = true;
  EXPECT_TRUE(plan_character_save(input).ok());
}

TEST(HkxCharacterSave, AMapperOrSkinTargetEqualToItsSourceNeedsOverwriteWhateverTheSpelling) {
  CharacterSaveInput input;
  input.documents = {document(CharacterDocumentKind::mapper, "dir/m.hkx", "DIR\\M.hkx")};
  EXPECT_TRUE(has(plan_character_save(input).findings, CharacterLevel::error, "save", "its own source file"));
  input.documents = {document(CharacterDocumentKind::mapper, "dir/m.hkx", "dir/./sub/../m.hkx")};
  EXPECT_FALSE(plan_character_save(input).ok());
  input.documents = {document(CharacterDocumentKind::skin, "body.nif", "BODY.nif")};
  EXPECT_TRUE(has(plan_character_save(input).findings, CharacterLevel::error, "save", "its own source file"));
  input.overwrite = true;
  EXPECT_TRUE(plan_character_save(input).ok());
  input.documents = {document(CharacterDocumentKind::mapper, "dir/m.hkx", "dir/m2.hkx")};
  input.overwrite = false;
  EXPECT_TRUE(plan_character_save(input).ok());
}

TEST(HkxCharacterSave, TwoSpellingsOfOneTargetAreOneFile) {
  CharacterSaveInput input;
  input.documents = {document(CharacterDocumentKind::skeleton, "a.hkx", "out/x.hkx"),
                     document(CharacterDocumentKind::mapper, "b.hkx", "out/sub/../X.hkx")};
  EXPECT_TRUE(has(plan_character_save(input).findings, CharacterLevel::error, "save", "would replace the first"));
}

TEST(HkxCharacterSave, ARagdollTargetEqualToTheFileItReadsIsRefused) {
  CharacterSaveInput input;
  input.documents = {document(CharacterDocumentKind::ragdoll, "a.hkx", "A.HKX")};
  const auto plan = plan_character_save(input);
  EXPECT_FALSE(plan.ok());
  EXPECT_TRUE(has(plan.findings, CharacterLevel::error, "save", "refuses a target equal to the file it reads"));
}

TEST(HkxCharacterSave, TwoDocumentsWithOneTargetAreRefusedWhateverTheCase) {
  CharacterSaveInput input;
  input.documents = {document(CharacterDocumentKind::skeleton, "a.hkx", "out/X.hkx"),
                     document(CharacterDocumentKind::mapper, "b.hkx", "OUT\\x.hkx")};
  const auto plan = plan_character_save(input);
  EXPECT_FALSE(plan.ok());
  EXPECT_TRUE(has(plan.findings, CharacterLevel::error, "save", "would replace the first"));
}

TEST(HkxCharacterSave, ATargetThatIsAnotherDocumentsSourceIsRefused) {
  CharacterSaveInput input;
  input.documents = {document(CharacterDocumentKind::skeleton, "a.hkx", "b.hkx"),
                     document(CharacterDocumentKind::mapper, "b.hkx", "m.hkx")};
  const auto plan = plan_character_save(input);
  EXPECT_FALSE(plan.ok());
  EXPECT_TRUE(has(plan.findings, CharacterLevel::error, "save", "its target is the source file"));
}

TEST(HkxCharacterSave, ASkinDocumentIsIndependentOfTheHavokChain) {
  CharacterSaveInput input;
  input.documents = {document(CharacterDocumentKind::skin, "body.nif", "body2.nif"),
                     document(CharacterDocumentKind::skeleton, "a.hkx", "s.hkx")};
  const auto plan = plan_character_save(input);
  EXPECT_TRUE(plan.ok());
  ASSERT_EQ(plan.steps.size(), 2u);
  EXPECT_EQ(plan.steps[0].kind, CharacterDocumentKind::skeleton);
  EXPECT_EQ(plan.steps[1].kind, CharacterDocumentKind::skin);
  EXPECT_FALSE(plan.steps[1].reads_previous_output);
}

TEST(HkxCharacterSave, ARemovedBoneBreaksTheMapperRowsThatPointAtIt) {
  const auto bytes = character_bytes({"Root", "Child", "Tip"});
  std::vector<HkxSkeletonEdit> edits(2);
  edits[0].kind = HkxSkeletonEditKind::Reparent;
  edits[0].bone = 2;
  edits[0].parent = 0;
  edits[1].kind = HkxSkeletonEditKind::Remove;
  edits[1].bone = 1;
  CharacterSaveInput input;
  input.documents = {document(CharacterDocumentKind::skeleton, "a.hkx", "s.hkx")};
  input.skeleton = bytes;
  input.skeleton_edits = edits;
  const auto plan = plan_character_save(input);
  EXPECT_FALSE(plan.ok());
  EXPECT_EQ(plan.shifted_references, 1u);
  EXPECT_TRUE(has(plan.findings, CharacterLevel::error, "save", "at a removed bone (Child)"));
  EXPECT_TRUE(has(plan.findings, CharacterLevel::info, "save", "animation clips bind tracks"));
}

TEST(HkxCharacterSave, ARenameKeepsEveryIndexAndPlansClean) {
  const auto bytes = character_bytes({"Root", "Child", "Tip"});
  std::vector<HkxSkeletonEdit> edits(1);
  edits[0].kind = HkxSkeletonEditKind::Rename;
  edits[0].bone = 2;
  edits[0].name = "Tip2";
  CharacterSaveInput input;
  input.documents = {document(CharacterDocumentKind::skeleton, "a.hkx", "s.hkx")};
  input.skeleton = bytes;
  input.skeleton_edits = edits;
  const auto plan = plan_character_save(input);
  EXPECT_TRUE(plan.ok());
  EXPECT_EQ(plan.shifted_references, 0u);
  EXPECT_TRUE(plan.findings.empty());
}

TEST(HkxCharacterSave, AnEditListTheSkeletonRefusesIsAnErrorOfThePlan) {
  const auto bytes = character_bytes({"Root", "Child", "Tip"});
  std::vector<HkxSkeletonEdit> edits(1);
  edits[0].kind = HkxSkeletonEditKind::Remove;
  edits[0].bone = 1;
  CharacterSaveInput input;
  input.documents = {document(CharacterDocumentKind::skeleton, "a.hkx", "s.hkx")};
  input.skeleton = bytes;
  input.skeleton_edits = edits;
  const auto plan = plan_character_save(input);
  EXPECT_FALSE(plan.ok());
  EXPECT_TRUE(has(plan.findings, CharacterLevel::error, "save", "skeleton edits"));
}

TEST(HkxCharacterSave, AGivenRagdollOrMapperTheImpactCheckCannotReadIsAWarningNotSilence) {
  const auto bytes = character_bytes({"Root", "Child", "Tip"});
  std::vector<HkxSkeletonEdit> edits(1);
  edits[0].kind = HkxSkeletonEditKind::Remove;
  edits[0].bone = 2;
  const std::vector<std::byte> junk(64, std::byte{0x5a});
  CharacterSaveInput input;
  input.documents = {document(CharacterDocumentKind::skeleton, "a.hkx", "s.hkx")};
  input.skeleton = bytes;
  input.skeleton_edits = edits;
  input.ragdoll = junk;
  input.mapper = junk;
  const auto plan = plan_character_save(input);
  EXPECT_TRUE(has(plan.findings, CharacterLevel::warning, "save", "ragdoll"));
  EXPECT_TRUE(has(plan.findings, CharacterLevel::warning, "save", "mappers"));
}
