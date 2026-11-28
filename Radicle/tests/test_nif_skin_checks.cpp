#include "fo4/archive/assets.hpp"
#include "fo4/error.hpp"
#include "fo4/nif/describe.hpp"
#include "fo4/nif/nif.hpp"
#include "fo4/nif/skin_binding.hpp"
#include "fo4/nif/skin_checks.hpp"
#include "fo4/nif/skin_validate.hpp"
#include "nif_build.hpp"
#include "real_data_build.hpp"
#include "real_data_plugins.hpp"
#include "real_data.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <limits>
#include <map>
#include <stdexcept>
#include <string>
#include <vector>

using fo4::nif::Transform;
using namespace nif_build;

namespace {

constexpr std::uint32_t k_skin_flags = k_full | k_has_skin;
constexpr std::size_t k_skin_stride = 28;

// Weights are half floats: 0x3C00 is 1.0, 0x3800 is 0.5, 0x3400 is 0.25, 0xBC00 is -1.0.
struct SkinnedVertex {
  float position[3];
  std::uint16_t weights[4];
  std::uint8_t bones[4];
};

const std::vector<SkinnedVertex> k_clean{
    {{0.0f, 0.0f, 0.0f}, {0x3C00, 0, 0, 0}, {0, 0, 0, 0}},
    {{1.0f, 0.0f, 0.0f}, {0x3800, 0x3800, 0, 0}, {0, 1, 0, 0}},
    {{0.0f, 1.0f, 0.0f}, {0x3400, 0x3400, 0x3400, 0x3400}, {0, 1, 0, 1}},
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

Block name_only_block(std::int32_t name) {
  Writer w;
  w.i32(name);
  return Block{"NiStringExtraData", w.bytes()};
}

// Bones are blocks 3 and 4, named Bone0 and Bone1; the bind transforms are block 5 unless data says otherwise.
std::vector<std::byte> skinned_nif(const std::vector<SkinnedVertex>& vertices, std::int32_t data = 5,
                                   const Block& second_bone = named_node_block("NiNode", 3, Transform{}, {}),
                                   std::size_t binds = 2,
                                   const std::vector<std::string>& bone_names = {"Bone0", "Bone1"}) {
  const std::vector<Block> blocks{
      named_node_block("NiNode", 0, Transform{}, {1, 3, 4}),
      shape_with_refs("BSTriShape", 1, Transform{}, k_skin_flags, k_skin_stride, 2, -1, -1,
                      skinned_vertex_bytes(vertices), {0, 1, 2}),
      skin_instance_block(data, {3, 4}),
      named_node_block("NiNode", 2, Transform{}, {}),
      second_bone,
      bone_data_block(binds),
  };
  return build_nif({"Root", "Body", bone_names.at(0), bone_names.at(1)}, blocks, {0});
}

// Invariant: the nodes are Root, Pelvis and Helper, with Helper under Pelvis, so a helper bone reaches a skeleton bone by its parent.
std::vector<std::byte> helper_tree_nif(const std::string& parent = "Pelvis") {
  const std::vector<Block> blocks{
      named_node_block("NiNode", 0, Transform{}, {1}),
      named_node_block("NiNode", 1, Transform{}, {2}),
      named_node_block("NiNode", 2, Transform{}, {}),
  };
  return build_nif({"Root", parent, "Helper"}, blocks, {0});
}

std::string lower(std::string text) {
  std::transform(text.begin(), text.end(), text.begin(),
                 [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
  return text;
}

}  // namespace

TEST(NifSkinChecks, WellWeightedSkinPassesEveryCheck) {
  // regression: a correctly weighted, fully bound skin must not be reported (false positive on a clean rig)
  const auto nif = fo4::nif::read_nif(skinned_nif(k_clean));
  const auto checks = fo4::nif::skin_checks(nif, fo4::nif::read_shapes(nif));
  ASSERT_EQ(checks.size(), 3U);
  for (const auto& check : checks) EXPECT_EQ(check.failure, "") << check.name;
}

TEST(NifSkinChecks, WeightsThatDoNotSumToOneNameTheVertex) {
  // regression: unnormalised weights shrink or stretch a vertex as the rig moves, so the sum must be named
  auto vertices = k_clean;
  vertices[1].weights[0] = 0x3800;
  vertices[1].weights[1] = 0;
  const auto nif = fo4::nif::read_nif(skinned_nif(vertices));
  const auto checks = fo4::nif::skin_checks(nif, fo4::nif::read_shapes(nif));
  EXPECT_EQ(checks.at(2).failure, "shape 'Body' (block 1) vertex 1 weights sum to 0.500000, not 1");
}

TEST(NifSkinChecks, UnweightedVertexIsNotASumFault) {
  // regression: vanilla skins leave a few vertices with no weight at all, and those must not fail the sum check
  auto vertices = k_clean;
  vertices[1].weights[0] = 0;
  vertices[1].weights[1] = 0;
  const auto nif = fo4::nif::read_nif(skinned_nif(vertices));
  EXPECT_EQ(fo4::nif::skin_checks(nif, fo4::nif::read_shapes(nif)).at(2).failure, "");
}

TEST(NifSkinChecks, NegativeWeightIsReportedBeforeTheSumCancelsIt) {
  // regression: a -1 weight paired with a +1 sums to zero and must not pass as a weighted vertex
  auto vertices = k_clean;
  vertices[2].weights[0] = 0xBC00;
  vertices[2].weights[1] = 0x3C00;
  vertices[2].weights[2] = 0;
  vertices[2].weights[3] = 0;
  const auto nif = fo4::nif::read_nif(skinned_nif(vertices));
  const auto checks = fo4::nif::skin_checks(nif, fo4::nif::read_shapes(nif));
  EXPECT_EQ(checks.at(2).failure, "shape 'Body' (block 1) vertex 2 has a negative weight");
}

// Detects: a NaN weight passing the sum check, since NaN fails every comparison and only the finite test names it
TEST(NifSkinChecks, NonFiniteWeightIsNamedInsteadOfPassingTheSum) {
  auto vertices = k_clean;
  vertices[1].weights[0] = 0x7E00;
  const auto nif = fo4::nif::read_nif(skinned_nif(vertices));
  EXPECT_EQ(fo4::nif::skin_checks(nif, fo4::nif::read_shapes(nif)).at(2).failure,
            "shape 'Body' (block 1) vertex 1 has a weight that is not a finite number");
}

TEST(NifSkinChecks, SkinWithoutBoneDataFailsTheBindCheck) {
  // regression: a skin whose bind transforms are missing loads with identity bones and must be named
  const auto nif = fo4::nif::read_nif(skinned_nif(k_clean, -1));
  const auto checks = fo4::nif::skin_checks(nif, fo4::nif::read_shapes(nif));
  EXPECT_EQ(checks.at(0).failure,
            "shape 'Body' (block 1) has no bind transforms: its skin holds no BSSkin::BoneData block");
}

TEST(NifSkinChecks, SkinWithAWrongTypeBoneDataFailsTheBindCheck) {
  // regression: a bone data reference to a non-BoneData block must fail the bind check, since the lost skin has no bones to name it
  const auto nif = fo4::nif::read_nif(skinned_nif(k_clean, 3));
  const auto checks = fo4::nif::skin_checks(nif, fo4::nif::read_shapes(nif));
  EXPECT_EQ(checks.at(0).failure,
            "shape 'Body' (block 1) has no bind transforms: its skin holds no BSSkin::BoneData block");
}

TEST(NifSkinChecks, SkinReferenceThatIsNotASkinInstanceFailsTheBindCheck) {
  // regression: a skin reference to a non-instance block must not be read as a skin instance, so the bind check must name it
  const std::vector<Block> blocks{
      named_node_block("NiNode", 0, Transform{}, {1}),
      shape_with_refs("BSTriShape", 1, Transform{}, k_skin_flags, k_skin_stride, 0, -1, -1,
                      skinned_vertex_bytes(k_clean), {0, 1, 2}),
  };
  const auto nif = fo4::nif::read_nif(build_nif({"Root", "Body"}, blocks, {0}));
  const auto checks = fo4::nif::skin_checks(nif, fo4::nif::read_shapes(nif));
  EXPECT_EQ(checks.at(0).failure,
            "shape 'Body' (block 1) has no bind transforms: its skin reference is not a BSSkin::Instance block");
}

TEST(NifSkinChecks, BoneNameThatNamesNoNodeFailsTheResolveCheck) {
  // regression: a skin bound to a bone the file does not hold must not pass as resolved
  const auto nif = fo4::nif::read_nif(skinned_nif(k_clean, 5, name_only_block(3)));
  const auto checks = fo4::nif::skin_checks(nif, fo4::nif::read_shapes(nif));
  EXPECT_EQ(checks.at(1).failure, "shape 'Body' (block 1) bone 'Bone1' does not name a node in this file");
}

TEST(NifSkinChecks, WeightsOnAShapeWithoutASkinInstanceAreNotChecked) {
  // regression: a shape that sets the skinned flag but has no skin instance is not a skin, so its weights are not judged
  auto vertices = k_clean;
  vertices[1].weights[0] = 0x3800;
  vertices[1].weights[1] = 0;
  const std::vector<Block> blocks{
      named_node_block("NiNode", 0, Transform{}, {1}),
      shape_with_refs("BSTriShape", 1, Transform{}, k_skin_flags, k_skin_stride, -1, -1, -1,
                      skinned_vertex_bytes(vertices), {0, 1, 2}),
  };
  const auto nif = fo4::nif::read_nif(build_nif({"Root", "Body"}, blocks, {0}));
  for (const auto& check : fo4::nif::skin_checks(nif, fo4::nif::read_shapes(nif))) {
    EXPECT_EQ(check.failure, "") << check.name;
  }
}

TEST(NifSkinChecks, BoneIndexPastTheListFailsTheShapeParseCheck) {
  // regression: a bone index past the skin's bone list must fail verify rather than pass with a zero weight
  auto vertices = k_clean;
  vertices[0].bones[0] = 5;
  const auto report = fo4::nif::verify_nif(fo4::nif::read_nif(skinned_nif(vertices)));
  EXPECT_EQ(report.checks.at(3).name, "shapes parse");
  EXPECT_NE(report.checks.at(3).failure.find("bone index is past the end of the skin's bone list"), std::string::npos);
}

// regression: a skin check that fires on a shipped skinned mesh would flag files the game loads fine
TEST(NifSkinChecksReal, VanillaSkinnedShapesGiveNoFindings) {
  FO4_REQUIRE_DATA_DIR(dir, "set FO4_DATA_DIR to a Fallout 4 Data folder with its Fallout4 - *.ba2 archives");
  record_game_build(detect_game_build(dir));
  const auto archives = fo4::archive::archive_load_order(dir, {}, installed_master_and_cc_plugins(dir));
  if (archives.empty()) GTEST_SKIP() << "set FO4_DATA_DIR to a Fallout 4 Data folder with its Fallout4 - *.ba2 archives";

  const auto assets = fo4::archive::AssetResolver::open({}, archives);
  std::map<std::string, std::size_t> failures;
  std::size_t nifs = 0;
  std::size_t skinned_shapes = 0;
  std::size_t parse_failures = 0;
  std::string first_failure;
  for (const auto& reader : assets.archives()) {
    if (reader.kind() != fo4::archive::Ba2Kind::general) continue;
    // Constraint: one in 37 keeps this test inside the shared real-data job.
    for (std::size_t i = 0; i < reader.entries().size(); i += 37) {
      const auto& path = reader.entries()[i].path;
      if (lower(std::filesystem::path(path).extension().string()) != ".nif") continue;
      ++nifs;
      try {
        const auto bytes = reader.extract(i);
        const auto nif = fo4::nif::read_nif(bytes);
        const auto shapes = fo4::nif::read_shapes(nif);
        for (const auto& shape : shapes) {
          if (!shape.bones.empty()) ++skinned_shapes;
        }
        for (const auto& check : fo4::nif::skin_checks(nif, shapes)) {
          if (check.failure.empty()) continue;
          ++failures[check.name];
          if (first_failure.empty()) first_failure = path + ": " + check.failure;
        }
      } catch (const fo4::parse_error& error) {
        ++parse_failures;
        ADD_FAILURE() << path << ": " << error.what();
      }
    }
  }

  for (const auto& [name, count] : failures) std::cout << "skin check fires on vanilla: " << name << ": " << count << '\n';
  std::cout << "nifs " << nifs << ", skinned shapes " << skinned_shapes << ", parse failures " << parse_failures << '\n';
  RecordProperty("skinned_shape_count", std::to_string(skinned_shapes));
  RecordProperty("skin_check_nif_count", std::to_string(nifs));
  EXPECT_GT(skinned_shapes, 0U);
  EXPECT_TRUE(failures.empty()) << first_failure;
}

// Constraint: these vertices each name a bone once and sum to one, so only a real fault can fail the validator.
const std::vector<SkinnedVertex> k_clean_two_bone{
    {{0.0f, 0.0f, 0.0f}, {0x3C00, 0, 0, 0}, {0, 0, 0, 0}},
    {{1.0f, 0.0f, 0.0f}, {0x3800, 0x3800, 0, 0}, {0, 1, 0, 0}},
    {{0.0f, 1.0f, 0.0f}, {0x3800, 0x3800, 0, 0}, {1, 0, 0, 0}},
};

// Requires the report only to be readable, so a failing expectation can say what fired.
std::string describe(const fo4::nif::SkinReport& report) {
  std::string text;
  for (const auto& finding : report.findings) {
    if (!text.empty()) text += "; ";
    text += finding.where + ": " + finding.what;
  }
  return text.empty() ? "no findings" : text;
}

TEST(SkinValidate, WarnsAboutABoneThatMovesNoVertex) {
  // Detects a regression: a bone no vertex is weighted to must be a warning, so a rig with an unused bone still saves.
  const std::vector<SkinnedVertex> one_bone{
      {{0.0f, 0.0f, 0.0f}, {0x3C00, 0, 0, 0}, {0, 0, 0, 0}},
      {{1.0f, 0.0f, 0.0f}, {0x3C00, 0, 0, 0}, {0, 0, 0, 0}},
      {{0.0f, 1.0f, 0.0f}, {0x3C00, 0, 0, 0}, {0, 0, 0, 0}},
  };
  const auto nif = fo4::nif::read_nif(skinned_nif(one_bone));
  const auto report = fo4::nif::validate_skin(nif, fo4::nif::read_shapes(nif).at(0));
  EXPECT_TRUE(report.ok()) << describe(report);
  EXPECT_EQ(report.used_bones, 1U);
  ASSERT_EQ(report.warnings(), 1U);
  EXPECT_EQ(report.findings.at(0).level, fo4::nif::SkinFindingLevel::warning);
  EXPECT_EQ(report.findings.at(0).where, "bones");
  EXPECT_EQ(report.findings.at(0).what, "1 move no vertex of this shape: Bone1");
}

TEST(SkinValidate, ErrorsARepeatedBoneAndAVertexThatMovesNothing) {
  // Detects a regression: the same bone twice on one vertex must drop the lighter share, and a vertex with no weight
  // must not move at all, so both are errors instead of a passing sum.
  auto vertices = k_clean_two_bone;
  vertices[1].bones[0] = 0;
  vertices[1].bones[1] = 0;
  vertices[2].weights[0] = 0;
  vertices[2].weights[1] = 0;
  const auto nif = fo4::nif::read_nif(skinned_nif(vertices));
  const auto report = fo4::nif::validate_skin(nif, fo4::nif::read_shapes(nif).at(0));
  EXPECT_FALSE(report.ok()) << describe(report);
  ASSERT_EQ(report.errors(), 2U);
  EXPECT_EQ(report.findings.at(0).what, "1 put the same bone in two slots of one vertex");
  EXPECT_EQ(report.findings.at(1).what, "1 have no weight at all, so nothing moves them");
}

TEST(SkinValidate, AcceptsACleanTwoBoneSkin) {
  // Detects a regression: a clean rig must report nothing, so the validator does not cry wolf before a save.
  const auto nif = fo4::nif::read_nif(skinned_nif(k_clean_two_bone));
  const auto report = fo4::nif::validate_skin(nif, fo4::nif::read_shapes(nif).at(0));
  EXPECT_TRUE(report.ok()) << describe(report);
  EXPECT_EQ(report.findings.size(), 0U);
  EXPECT_EQ(report.used_bones, 2U);
  EXPECT_EQ(report.bind_transforms, 2U);
}

TEST(SkinBinding, NamesMatchTheSkeletonIgnoringCaseAndCountBoundVertices) {
  // regression: a case-sensitive name compare must not read a rig whose bones differ only in case as fully unbound
  const auto nif = fo4::nif::read_nif(skinned_nif(k_clean_two_bone));
  const std::vector<std::string> skeleton{"BONE1", "bone0", "Spine"};
  const auto report = fo4::nif::bind_skin_to_skeleton(fo4::nif::read_shapes(nif).at(0), skeleton);
  ASSERT_EQ(report.bones.size(), 2U);
  EXPECT_EQ(report.bones[0].match, fo4::nif::SkinBoneMatch::exact);
  EXPECT_EQ(report.bones[0].skeleton_bone, 1);
  EXPECT_EQ(report.bones[1].skeleton_bone, 0);
  EXPECT_EQ(report.exact, 2U);
  EXPECT_TRUE(report.complete());
  EXPECT_EQ(report.fully_bound_vertices, 3U);
  EXPECT_EQ(report.warning, "");
}

TEST(SkinBinding, UnmatchedBoneIsNamedWithTheVerticesItMoves) {
  // regression: a skin bone the skeleton lacks must be reported with its vertices and must not count as bound
  const auto nif = fo4::nif::read_nif(skinned_nif(k_clean_two_bone));
  const std::vector<std::string> skeleton{"Bone0", "Spine"};
  const auto report = fo4::nif::bind_skin_to_skeleton(fo4::nif::read_shapes(nif).at(0), skeleton);
  EXPECT_EQ(report.exact, 1U);
  EXPECT_EQ(report.unmatched, 1U);
  EXPECT_FALSE(report.complete());
  EXPECT_EQ(report.bones[1].match, fo4::nif::SkinBoneMatch::unmatched);
  EXPECT_EQ(report.bones[1].skeleton_bone, -1);
  EXPECT_EQ(report.bones[0].weighted_vertices, 3U);
  EXPECT_EQ(report.bones[1].weighted_vertices, 2U);
  EXPECT_EQ(report.fully_bound_vertices, 1U);
  EXPECT_NE(report.warning.find("1 bone matches no skeleton bone and no helper node: Bone1"), std::string::npos)
      << report.warning;
}

TEST(SkinBinding, SkinSuffixOnlyApproximatesAndNeverBeatsAnExactName) {
  // regression: Thigh_skin resolving to Thigh is a placement guess and must stay apart from an exact match, which must win
  const auto nif = fo4::nif::read_nif(
      skinned_nif(k_clean_two_bone, 5, named_node_block("NiNode", 3, Transform{}, {}), 2, {"Thigh_skin", "Calf"}));
  const auto shape = fo4::nif::read_shapes(nif).at(0);

  const std::vector<std::string> plain{"Thigh", "Calf"};
  const auto guess = fo4::nif::bind_skin_to_skeleton(shape, plain);
  EXPECT_EQ(guess.bones[0].match, fo4::nif::SkinBoneMatch::skin_parent);
  EXPECT_EQ(guess.bones[0].skeleton_bone, 0);
  EXPECT_EQ(guess.skin_parent, 1U);
  EXPECT_EQ(guess.exact, 1U);
  EXPECT_FALSE(guess.complete());
  EXPECT_EQ(guess.fully_bound_vertices, 0U);
  EXPECT_NE(guess.warning.find("Thigh_skin"), std::string::npos);

  const std::vector<std::string> skinned{"Thigh", "Thigh_skin", "Calf"};
  const auto exact = fo4::nif::bind_skin_to_skeleton(shape, skinned);
  EXPECT_EQ(exact.bones[0].match, fo4::nif::SkinBoneMatch::exact);
  EXPECT_EQ(exact.bones[0].skeleton_bone, 1);
  EXPECT_EQ(exact.skin_parent, 0U);
}

TEST(SkinBinding, HelperNodeResolvesThroughItsParentChainInASkeletonNif) {
  // regression: a skin bone that exists only as a helper node under a skeleton bone must bind through its node chain
  const auto nif = fo4::nif::read_nif(
      skinned_nif(k_clean_two_bone, 5, named_node_block("NiNode", 3, Transform{}, {}), 2, {"Helper", "Bone1"}));
  const auto tree = fo4::nif::read_nif(helper_tree_nif());
  const std::vector<std::string> skeleton{"Pelvis", "Bone1"};
  const std::vector<fo4::nif::SkinNodeSource> sources{{&tree, "skeleton.nif"}};

  const auto without = fo4::nif::bind_skin_to_skeleton(fo4::nif::read_shapes(nif).at(0), skeleton);
  EXPECT_EQ(without.bones[0].match, fo4::nif::SkinBoneMatch::unmatched);

  const auto report = fo4::nif::bind_skin_to_skeleton(fo4::nif::read_shapes(nif).at(0), skeleton, sources);
  EXPECT_EQ(report.bones[0].match, fo4::nif::SkinBoneMatch::node_chain);
  EXPECT_EQ(report.bones[0].skeleton_bone, 0);
  EXPECT_EQ(report.bones[0].via, "Pelvis");
  EXPECT_EQ(report.bones[0].source, "skeleton.nif");
  EXPECT_EQ(report.node_chain, 1U);
  EXPECT_EQ(report.unmatched, 0U);
  EXPECT_EQ(report.fully_bound_vertices, 0U);
}

TEST(SkinBinding, RepeatedSkeletonNameBindsTheFirstBoneAndIsFlagged) {
  // regression: a skeleton that repeats a bone name must keep the first bone and flag the match
  const auto nif = fo4::nif::read_nif(skinned_nif(k_clean_two_bone));
  const std::vector<std::string> skeleton{"Bone0", "Bone0", "Bone1"};
  const auto report = fo4::nif::bind_skin_to_skeleton(fo4::nif::read_shapes(nif).at(0), skeleton);
  EXPECT_EQ(report.bones[0].skeleton_bone, 0);
  EXPECT_TRUE(report.bones[0].ambiguous);
  EXPECT_FALSE(report.bones[1].ambiguous);
  EXPECT_EQ(report.ambiguous, 1U);
  EXPECT_NE(report.warning.find("Bone0 matches more than one skeleton bone or helper node"), std::string::npos)
      << report.warning;
}

TEST(SkinBinding, NodeChainsToDifferentBonesAreFlaggedAndTheFirstSourceWins) {
  // regression: a helper node that reaches two skeleton bones must be flagged and follow the source order, not the last source
  const auto nif = fo4::nif::read_nif(
      skinned_nif(k_clean_two_bone, 5, named_node_block("NiNode", 3, Transform{}, {}), 2, {"Helper", "Bone1"}));
  const auto shape = fo4::nif::read_shapes(nif).at(0);
  const auto under_pelvis = fo4::nif::read_nif(helper_tree_nif("Pelvis"));
  const auto under_spine = fo4::nif::read_nif(helper_tree_nif("Spine"));
  const std::vector<std::string> skeleton{"Pelvis", "Spine", "Bone1"};

  const std::vector<fo4::nif::SkinNodeSource> pelvis_first{{&under_pelvis, "a.nif"}, {&under_spine, "b.nif"}};
  const auto first = fo4::nif::bind_skin_to_skeleton(shape, skeleton, pelvis_first);
  EXPECT_EQ(first.bones[0].match, fo4::nif::SkinBoneMatch::node_chain);
  EXPECT_EQ(first.bones[0].skeleton_bone, 0);
  EXPECT_EQ(first.bones[0].via, "Pelvis");
  EXPECT_EQ(first.bones[0].source, "a.nif");
  EXPECT_TRUE(first.bones[0].ambiguous);
  EXPECT_EQ(first.ambiguous, 1U);
  EXPECT_NE(first.warning.find("Helper matches more than one skeleton bone or helper node"), std::string::npos)
      << first.warning;

  const std::vector<fo4::nif::SkinNodeSource> spine_first{{&under_spine, "b.nif"}, {&under_pelvis, "a.nif"}};
  const auto second = fo4::nif::bind_skin_to_skeleton(shape, skeleton, spine_first);
  EXPECT_EQ(second.bones[0].skeleton_bone, 1);
  EXPECT_EQ(second.bones[0].via, "Spine");
  EXPECT_EQ(second.bones[0].source, "b.nif");
  EXPECT_TRUE(second.bones[0].ambiguous);

  const std::vector<fo4::nif::SkinNodeSource> agree{{&under_pelvis, "a.nif"}, {&under_pelvis, "c.nif"}};
  EXPECT_FALSE(fo4::nif::bind_skin_to_skeleton(shape, skeleton, agree).bones[0].ambiguous);
}

TEST(SkinBinding, NodeChainOntoARepeatedSkeletonNameKeepsTheFirstBoneIndexAndIsFlagged) {
  // regression: a node chain onto a repeated skeleton name must report the first bone's index and flag the match
  const auto nif = fo4::nif::read_nif(
      skinned_nif(k_clean_two_bone, 5, named_node_block("NiNode", 3, Transform{}, {}), 2, {"Helper", "Bone1"}));
  const auto tree = fo4::nif::read_nif(helper_tree_nif("Pelvis"));
  const std::vector<std::string> skeleton{"Spine", "Pelvis", "Pelvis", "Bone1"};
  const std::vector<fo4::nif::SkinNodeSource> sources{{&tree, "skeleton.nif"}};
  const auto report = fo4::nif::bind_skin_to_skeleton(fo4::nif::read_shapes(nif).at(0), skeleton, sources);
  EXPECT_EQ(report.bones[0].match, fo4::nif::SkinBoneMatch::node_chain);
  EXPECT_EQ(report.bones[0].skeleton_bone, 1);
  EXPECT_TRUE(report.bones[0].ambiguous);
}

TEST(SkinBinding, ANonFiniteWeightOnAnUnmatchedBoneStillCountsAsWeighted) {
  // regression: a NaN weight must not be skipped as no weight, or a vertex on an unmatched bone reads as fully bound
  const auto nif = fo4::nif::read_nif(skinned_nif(k_clean_two_bone));
  auto shape = fo4::nif::read_shapes(nif).at(0);
  for (std::size_t slot = 0; slot < 4; ++slot) {
    shape.weights[slot] = slot == 0 ? 1.0f : 0.0f;
    shape.bone_indices[slot] = 0;
  }
  const std::vector<std::string> skeleton{"Bone0"};
  const auto before = fo4::nif::bind_skin_to_skeleton(shape, skeleton);
  EXPECT_EQ(before.bones[1].weighted_vertices, 2U);
  EXPECT_EQ(before.fully_bound_vertices, 1U);

  shape.weights[1] = std::numeric_limits<float>::quiet_NaN();
  shape.bone_indices[1] = 1;
  const auto report = fo4::nif::bind_skin_to_skeleton(shape, skeleton);
  EXPECT_EQ(report.bones[1].match, fo4::nif::SkinBoneMatch::unmatched);
  EXPECT_EQ(report.bones[1].weighted_vertices, 3U);
  EXPECT_EQ(report.fully_bound_vertices, 0U);
}

TEST(SkinBinding, RefusesAShapeThatIsNotReadablySkinned) {
  // regression: an unskinned shape, a dropped skin or short weight arrays must be refused, not reported as a clean rig
  const std::vector<std::string> skeleton{"Bone0"};

  const fo4::nif::Shape plain;
  EXPECT_THROW(fo4::nif::bind_skin_to_skeleton(plain, skeleton), std::invalid_argument);

  const auto nif = fo4::nif::read_nif(skinned_nif(k_clean_two_bone));
  auto lost = fo4::nif::read_shapes(nif).at(0);
  lost.skin_lost = true;
  EXPECT_THROW(fo4::nif::bind_skin_to_skeleton(lost, skeleton), std::invalid_argument);
  lost.warning = "skin instance type is not supported";
  try {
    fo4::nif::bind_skin_to_skeleton(lost, skeleton);
    ADD_FAILURE() << "a lost skin must be refused";
  } catch (const std::invalid_argument& error) {
    EXPECT_NE(std::string(error.what()).find("skin instance type is not supported"), std::string::npos)
        << error.what();
  }

  auto short_weights = fo4::nif::read_shapes(nif).at(0);
  short_weights.weights.pop_back();
  EXPECT_THROW(fo4::nif::bind_skin_to_skeleton(short_weights, skeleton), std::invalid_argument);

  auto stray_index = fo4::nif::read_shapes(nif).at(0);
  stray_index.bone_indices[0] = 9;
  EXPECT_THROW(fo4::nif::bind_skin_to_skeleton(stray_index, skeleton), std::invalid_argument);
}
