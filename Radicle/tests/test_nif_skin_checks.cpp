#include "fo4/archive/assets.hpp"
#include "fo4/error.hpp"
#include "fo4/nif/describe.hpp"
#include "fo4/nif/nif.hpp"
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
#include <map>
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
                                   std::size_t binds = 2) {
  const std::vector<Block> blocks{
      named_node_block("NiNode", 0, Transform{}, {1, 3, 4}),
      shape_with_refs("BSTriShape", 1, Transform{}, k_skin_flags, k_skin_stride, 2, -1, -1,
                      skinned_vertex_bytes(vertices), {0, 1, 2}),
      skin_instance_block(data, {3, 4}),
      named_node_block("NiNode", 2, Transform{}, {}),
      second_bone,
      bone_data_block(binds),
  };
  return build_nif({"Root", "Body", "Bone0", "Bone1"}, blocks, {0});
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
