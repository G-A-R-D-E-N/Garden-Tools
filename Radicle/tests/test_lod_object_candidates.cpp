#include "fo4/lod/object_candidates.hpp"
#include "fo4/lod/object_merge.hpp"

#include "env_node_fixture.hpp"
#include "fo4/plugin/reader.hpp"

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

using namespace fo4::test::envnode;
using fo4::lod::collect_object_lod_candidates;
using fo4::lod::LodCandidateCollection;
using fo4::lod::LodCandidateSkip;
using fo4::lod::LodError;
using fo4::lod::LodSettings;
using fo4::lod::lod_candidate_skip_name;
using fo4::lod::plan_object_lod;
using fo4::lod::to_lod_objects;

const LodSettings k_settings{-96, -96, 256, 4, 32};

std::vector<std::byte> lod_slots(const std::vector<std::string>& meshes) {
  std::vector<std::byte> out;
  for (const auto& mesh : meshes) {
    auto slot = zstring(mesh);
    slot.resize(260, std::byte{0});
    out.insert(out.end(), slot.begin(), slot.end());
  }
  return out;
}

Node stat_record(std::uint32_t id, const std::vector<std::string>& meshes) {
  return rec("STAT", id, {{"EDID", zstring("Rock")}, {"MNAM", lod_slots(meshes)}});
}

Node placed(std::uint32_t id, std::uint32_t base, float x, float y, float z,
            std::vector<Sub> extra = {}, std::uint32_t flags = 0) {
  std::vector<Sub> subrecords{{"NAME", bytes_of({base})},
                              {"DATA", floats({x, y, z, 0.0f, 0.0f, 90.0f})}};
  subrecords.insert(subrecords.end(), extra.begin(), extra.end());
  return rec("REFR", id, std::move(subrecords), flags);
}

std::vector<Node> cell_with_refs(std::uint32_t id, std::vector<Node> refs,
                                 std::uint32_t flags = 0) {
  return {rec("CELL", id, {{"DATA", {std::byte{0}}}}, flags),
          grp(id, 6, {grp(id, 8, std::move(refs))})};
}

std::vector<std::byte> world_plugin(std::vector<Node> base_groups, std::vector<Node> top_refs,
                                    std::vector<Node> exterior_cells = {}) {
  auto world_children = cell_with_refs(0x801, std::move(top_refs), 0x400);
  if (!exterior_cells.empty()) {
    world_children.push_back(grp(0, 4, {grp(0, 5, std::move(exterior_cells))}));
  }
  base_groups.push_back(top("WRLD", {rec("WRLD", 0x800, {{"EDID", zstring("TestWorld")}}),
                                     grp(0x800, 1, std::move(world_children))}));
  return make_plugin({}, base_groups);
}

std::vector<std::byte> floats_of(const std::vector<float>& values) {
  fo4::test::ByteWriter out;
  for (const auto value : values) out.f32(value);
  return out.bytes;
}

Node scol_record(std::uint32_t id, const std::vector<std::pair<std::uint32_t, std::vector<float>>>& parts) {
  std::vector<Sub> subrecords;
  for (const auto& part : parts) {
    subrecords.push_back({"ONAM", bytes_of({part.first})});
    subrecords.push_back({"DATA", floats_of(part.second)});
  }
  return rec("SCOL", id, std::move(subrecords));
}

Node tree_record(std::uint32_t id, std::string_view model) {
  return rec("TREE", id, {{"EDID", zstring("Tree")}, {"MODL", zstring(model)}});
}

Node refr_transform(std::uint32_t id, std::uint32_t base, float x, float y, float z, float rx, float ry,
                    float rz, float scale) {
  return rec("REFR", id, {{"NAME", bytes_of({base})},
                          {"DATA", floats({x, y, z, rx, ry, rz})},
                          {"XSCL", floats({scale})}});
}

std::vector<std::byte> patch_plugin(std::vector<Node> world_children) {
  return make_plugin({"Test.esp"}, {top("WRLD", {grp(0x800, 1, std::move(world_children))})});
}

fo4::env::Environment load_env(const std::vector<std::byte>& bytes,
                               const std::vector<std::byte>& patch = {}) {
  std::vector<fo4::Plugin> plugins;
  plugins.push_back(fo4::read_plugin(bytes, "Test.esp"));
  if (!patch.empty()) plugins.push_back(fo4::read_plugin(patch, "Patch.esp"));
  return fo4::env::Environment::from_plugins(std::move(plugins));
}

fo4::FormKey worldspace() {
  return fo4::FormKey::parse("000800:Test.esp");
}

std::size_t skipped_count(const LodCandidateCollection& collection, LodCandidateSkip reason) {
  return collection.counts.skipped[static_cast<std::size_t>(reason)];
}

std::vector<std::string> reference_keys(const LodCandidateCollection& collection) {
  std::vector<std::string> keys;
  for (const auto& candidate : collection.candidates) {
    keys.push_back(candidate.reference.to_string());
  }
  return keys;
}

}  // namespace

TEST(LodObjectCandidates, IncludesAPersistentReferenceWithItsFields) {
  const auto env = load_env(world_plugin(
      {top("STAT", {stat_record(0x840, {"lod4.nif", "lod8.nif", "lod16.nif", "lod32.nif"})})},
      {placed(0x820, 0x840, 100.0f, 200.0f, 300.0f, {{"XSCL", floats({2.5f})}})}));
  const auto collection = collect_object_lod_candidates(env, worldspace(), 8);
  ASSERT_EQ(collection.candidates.size(), 1u);
  const auto& candidate = collection.candidates[0];
  EXPECT_EQ(candidate.reference.to_string(), "000820:Test.esp");
  EXPECT_EQ(candidate.base.to_string(), "000840:Test.esp");
  EXPECT_EQ(candidate.position, (std::array<float, 3>{100.0f, 200.0f, 300.0f}));
  EXPECT_EQ(candidate.rotation, (std::array<float, 3>{0.0f, 0.0f, 90.0f}));
  EXPECT_EQ(candidate.scale, 2.5f);
  EXPECT_EQ(candidate.mesh, "lod8.nif");
  EXPECT_EQ(candidate.cell_x, 0);
  EXPECT_EQ(candidate.cell_y, 0);
  EXPECT_EQ(collection.counts.references_seen, 1u);
  EXPECT_EQ(collection.counts.included, 1u);
  for (const auto count : collection.counts.skipped) EXPECT_EQ(count, 0u);
}

TEST(LodObjectCandidates, EachLevelReadsItsOwnSlot) {
  const auto env = load_env(world_plugin(
      {top("STAT", {stat_record(0x840, {"lod4.nif", "lod8.nif", "lod16.nif", "lod32.nif"})})},
      {placed(0x820, 0x840, 0.0f, 0.0f, 0.0f)}));
  for (const std::int32_t level : {4, 8, 16, 32}) {
    const auto collection = collect_object_lod_candidates(env, worldspace(), level);
    ASSERT_EQ(collection.candidates.size(), 1u);
    EXPECT_EQ(collection.candidates[0].mesh, "lod" + std::to_string(level) + ".nif");
  }
}

TEST(LodObjectCandidates, EmptySlotDropsTheObjectWithoutFallback) {
  const auto env = load_env(world_plugin(
      {top("STAT", {stat_record(0x841, {"lod4.nif", "", "lod16.nif", "lod32.nif"})})},
      {placed(0x820, 0x841, 0.0f, 0.0f, 0.0f)}));
  const auto eight = collect_object_lod_candidates(env, worldspace(), 8);
  EXPECT_TRUE(eight.candidates.empty());
  EXPECT_EQ(eight.counts.included, 0u);
  EXPECT_EQ(skipped_count(eight, LodCandidateSkip::no_lod_model), 1u);
  EXPECT_EQ(collect_object_lod_candidates(env, worldspace(), 4).candidates.size(), 1u);
  EXPECT_EQ(collect_object_lod_candidates(env, worldspace(), 16).candidates.size(), 1u);
}

TEST(LodObjectCandidates, RejectsLevelsOutsideTheSlotMap) {
  const auto env = load_env(world_plugin({top("STAT", {stat_record(0x840, {"lod4.nif"})})},
                                         {placed(0x820, 0x840, 0.0f, 0.0f, 0.0f)}));
  for (const std::int32_t level : {0, 2, 12, 64}) {
    EXPECT_THROW(collect_object_lod_candidates(env, worldspace(), level), LodError);
  }
}

// Detects (invariant): a skip reason that is counted twice or lost; initially disabled and enable-parent children are no skips.
TEST(LodObjectCandidates, CountsEachSkipReasonOnce) {
  const auto env = load_env(world_plugin(
      {top("STAT", {stat_record(0x840, {"lod4.nif", "lod8.nif", "lod16.nif", "lod32.nif"}),
                    stat_record(0x841, {"lod4.nif", "", "lod16.nif", "lod32.nif"})}),
       top("MISC", {rec("MISC", 0x851, {{"EDID", zstring("Misc")}, {"MODL", zstring("Meshes\\Misc.nif")}})})},
      {placed(0x820, 0x840, 100.0f, 100.0f, 0.0f),
       placed(0x821, 0x840, 100.0f, 100.0f, 0.0f, {}, 0x800),
       rec("REFR", 0x822, {{"NAME", bytes_of({0x840})}}),
       placed(0x823, 0x851, 100.0f, 100.0f, 0.0f),
       placed(0x824, 0x841, 100.0f, 100.0f, 0.0f),
       placed(0x825, 0x840, 100.0f, 100.0f, 0.0f, {{"XESP", bytes_of({0x8FF})}}),
       placed(0x826, 0x840, 100.0f, 100.0f, 0.0f, {}, 0x20),
       placed(0x827, 0x841, 100.0f, 100.0f, 0.0f, {{"XESP", bytes_of({0x820})}}),
       placed(0x828, 0x860, 100.0f, 100.0f, 0.0f)}));
  const auto collection = collect_object_lod_candidates(env, worldspace(), 8);
  EXPECT_EQ(collection.counts.references_seen, 8u);
  EXPECT_EQ(collection.counts.included, 3u);
  EXPECT_EQ(skipped_count(collection, LodCandidateSkip::missing_position), 1u);
  // Invariant: a base that resolves to nothing is unsupported, while a MISC base is simply not object LOD.
  EXPECT_EQ(skipped_count(collection, LodCandidateSkip::unsupported_base), 1u);
  EXPECT_EQ(skipped_count(collection, LodCandidateSkip::not_lod_base), 1u);
  EXPECT_EQ(skipped_count(collection, LodCandidateSkip::no_lod_model), 2u);
  EXPECT_EQ(reference_keys(collection),
            (std::vector<std::string>{"000820:Test.esp", "000821:Test.esp", "000825:Test.esp"}));
}

TEST(LodObjectCandidates, NamesEachSkipReason) {
  EXPECT_EQ(lod_candidate_skip_name(LodCandidateSkip::missing_position), "missing_position");
  EXPECT_EQ(lod_candidate_skip_name(LodCandidateSkip::unsupported_base), "unsupported_base");
  EXPECT_EQ(lod_candidate_skip_name(LodCandidateSkip::no_lod_model), "no_lod_model");
  EXPECT_EQ(lod_candidate_skip_name(LodCandidateSkip::not_lod_base), "not_lod_base");
  EXPECT_EQ(lod_candidate_skip_name(LodCandidateSkip::multiref_child), "multiref_child");
  EXPECT_EQ(lod_candidate_skip_name(LodCandidateSkip::malformed), "malformed");
}

TEST(LodObjectCandidates, OrdersTopCellThenExteriorCellsInTheirOwnOrder) {
  auto exterior = cell_with_refs(0x802, {placed(0x830, 0x840, 4096.0f + 10.0f, 10.0f, 0.0f),
                                         placed(0x831, 0x840, 10.0f, -4096.0f + 10.0f, 0.0f)});
  const auto second = cell_with_refs(0x803, {placed(0x832, 0x840, 5.0f, 5.0f, 0.0f)});
  exterior.insert(exterior.end(), second.begin(), second.end());
  const auto env = load_env(world_plugin(
      {top("STAT", {stat_record(0x840, {"lod4.nif", "lod8.nif", "lod16.nif", "lod32.nif"})})},
      {placed(0x820, 0x840, 100.0f, 100.0f, 0.0f)}, std::move(exterior)));
  const auto first = collect_object_lod_candidates(env, worldspace(), 8);
  const auto again = collect_object_lod_candidates(env, worldspace(), 8);
  const std::vector<std::string> expected{"000820:Test.esp", "000830:Test.esp",
                                          "000831:Test.esp", "000832:Test.esp"};
  EXPECT_EQ(reference_keys(first), expected);
  EXPECT_EQ(reference_keys(again), expected);
  ASSERT_EQ(first.candidates.size(), 4u);
  EXPECT_TRUE(first.candidates[0].persistent);
  for (std::size_t index = 1; index < 4; ++index) EXPECT_FALSE(first.candidates[index].persistent) << index;
  EXPECT_EQ(first.candidates[1].cell_x, 1);
  EXPECT_EQ(first.candidates[1].cell_y, 0);
  EXPECT_EQ(first.candidates[2].cell_x, 0);
  EXPECT_EQ(first.candidates[2].cell_y, -1);
  EXPECT_EQ(first.candidates[3].cell_x, 0);
  EXPECT_EQ(first.candidates[3].cell_y, 0);
}

TEST(LodObjectCandidates, ObjectsLandInTheExpectedTileOfThePlanner) {
  const auto env = load_env(world_plugin(
      {top("STAT", {stat_record(0x840, {"lod4.nif", "lod8.nif", "lod16.nif", "lod32.nif"})})},
      {placed(0x820, 0x840, 100.0f, 100.0f, 0.0f),
       placed(0x821, 0x840, -72.0f * 4096.0f + 10.0f, -72.0f * 4096.0f + 10.0f, 0.0f)}));
  const auto objects = to_lod_objects(collect_object_lod_candidates(env, worldspace(), 8));
  ASSERT_EQ(objects.size(), 2u);
  EXPECT_EQ(objects[0].x, 100.0);
  EXPECT_EQ(objects[0].y, 100.0);
  EXPECT_EQ(objects[0].mesh, "lod8.nif");
  const auto plan = plan_object_lod(objects, k_settings, 8);
  ASSERT_EQ(plan.tiles.size(), 2u);
  EXPECT_EQ(plan.tiles[0].x, -72);
  EXPECT_EQ(plan.tiles[0].y, -72);
  EXPECT_EQ(plan.tiles[0].objects, (std::vector<std::size_t>{1}));
  EXPECT_EQ(plan.tiles[1].x, 0);
  EXPECT_EQ(plan.tiles[1].y, 0);
  EXPECT_EQ(plan.tiles[1].objects, (std::vector<std::size_t>{0}));
}

TEST(LodObjectCandidates, IgnoresRecordsThatAreNotReferences) {
  auto npc = placed(0x828, 0x840, 100.0f, 100.0f, 0.0f);
  npc.signature = "ACHR";
  const auto env = load_env(world_plugin(
      {top("STAT", {stat_record(0x840, {"lod4.nif", "lod8.nif"})})}, {npc}));
  const auto collection = collect_object_lod_candidates(env, worldspace(), 8);
  EXPECT_TRUE(collection.candidates.empty());
  EXPECT_EQ(collection.counts.references_seen, 0u);
  EXPECT_EQ(collection.counts.included, 0u);
}

TEST(LodObjectCandidates, IncludesTemporaryChildrenBesidePersistentOnes) {
  auto exterior = cell_with_refs(0x802, {placed(0x830, 0x840, 100.0f, 100.0f, 0.0f)});
  exterior[1].children.push_back(grp(0x802, 9, {placed(0x831, 0x840, 200.0f, 200.0f, 0.0f)}));
  const auto env = load_env(world_plugin({top("STAT", {stat_record(0x840, {"lod4.nif", "lod8.nif"})})}, {},
                                         std::move(exterior)));
  const auto collection = collect_object_lod_candidates(env, worldspace(), 8);
  EXPECT_EQ(collection.counts.references_seen, 2u);
  EXPECT_EQ(collection.counts.included, 2u);
  EXPECT_EQ(reference_keys(collection),
            (std::vector<std::string>{"000830:Test.esp", "000831:Test.esp"}));
}

TEST(LodObjectCandidates, PatchedStatMeshWinsOverTheBaseMeshAtItsLevel) {
  const auto env = load_env(
      world_plugin({top("STAT", {stat_record(0x840, {"lod4.nif", "lod8.nif", "lod16.nif",
                                                      "lod32.nif"})})},
                   {placed(0x820, 0x840, 100.0f, 100.0f, 0.0f)}),
      make_plugin({"Test.esp"}, {top("STAT", {stat_record(0x840, {"lod4.nif", "lod8_patch.nif",
                                                                  "lod16.nif", "lod32.nif"})})}));
  const auto collection = collect_object_lod_candidates(env, worldspace(), 8);
  ASSERT_EQ(collection.candidates.size(), 1u);
  EXPECT_EQ(collection.candidates[0].mesh, "lod8_patch.nif");
}

TEST(LodObjectCandidates, PatchedStatEmptySlotDropsTheObjectOnlyAtThatLevel) {
  const auto env = load_env(
      world_plugin({top("STAT", {stat_record(0x840, {"lod4.nif", "lod8.nif", "lod16.nif",
                                                      "lod32.nif"})})},
                   {placed(0x820, 0x840, 100.0f, 100.0f, 0.0f)}),
      make_plugin({"Test.esp"}, {top("STAT", {stat_record(0x840, {"lod4.nif", "", "lod16.nif",
                                                                  "lod32.nif"})})}));
  const auto eight = collect_object_lod_candidates(env, worldspace(), 8);
  EXPECT_TRUE(eight.candidates.empty());
  EXPECT_EQ(skipped_count(eight, LodCandidateSkip::no_lod_model), 1u);
  EXPECT_EQ(collect_object_lod_candidates(env, worldspace(), 4).candidates.size(), 1u);
}

// Detects (invariant): the initially-disabled skip returning, which drops vanilla's disabled references (2 street lamps in
// the Sanctuary level-32 tile) from the tile.
TEST(LodObjectCandidates, PatchedReferenceInitiallyDisabledFlagFromTheWinnerIsStillIncluded) {
  const auto env = load_env(
      world_plugin({top("STAT", {stat_record(0x840, {"lod4.nif", "lod8.nif"})})},
                   {placed(0x820, 0x840, 100.0f, 100.0f, 0.0f)}),
      patch_plugin({grp(0x801, 6, {grp(0x801, 8, {placed(0x820, 0x840, 100.0f, 100.0f, 0.0f,
                                                          {}, 0x800)})})}));
  const auto collection = collect_object_lod_candidates(env, worldspace(), 8);
  EXPECT_EQ(reference_keys(collection), (std::vector<std::string>{"000820:Test.esp"}));
  EXPECT_EQ(collection.counts.references_seen, 1u);
  for (const auto count : collection.counts.skipped) EXPECT_EQ(count, 0u);
}

TEST(LodObjectCandidates, PatchedReferenceMovedToAnotherCellIsCountedOnceAtItsNewPosition) {
  const auto env = load_env(
      world_plugin({top("STAT", {stat_record(0x840, {"lod4.nif", "lod8.nif"})})},
                   {placed(0x820, 0x840, 100.0f, 100.0f, 0.0f)}, cell_with_refs(0x802, {})),
      patch_plugin({grp(0x802, 6, {grp(0x802, 8, {placed(0x820, 0x840, 4106.0f, 10.0f, 0.0f)})})}));
  const auto collection = collect_object_lod_candidates(env, worldspace(), 8);
  EXPECT_EQ(collection.counts.references_seen, 1u);
  ASSERT_EQ(collection.candidates.size(), 1u);
  EXPECT_EQ(collection.candidates[0].position, (std::array<float, 3>{4106.0f, 10.0f, 0.0f}));
  EXPECT_EQ(collection.candidates[0].cell_x, 1);
  EXPECT_EQ(collection.candidates[0].cell_y, 0);
}

TEST(LodObjectCandidates, CountsAnUnresolvedBaseAsUnsupportedBase) {
  const auto env = load_env(world_plugin({top("STAT", {stat_record(0x840, {"lod4.nif", "lod8.nif"})})},
                                         {placed(0x820, 0x8FF, 100.0f, 100.0f, 0.0f)}));
  const auto collection = collect_object_lod_candidates(env, worldspace(), 8);
  EXPECT_TRUE(collection.candidates.empty());
  EXPECT_EQ(collection.counts.references_seen, 1u);
  EXPECT_EQ(skipped_count(collection, LodCandidateSkip::unsupported_base), 1u);
}

TEST(LodObjectCandidates, CountsNonFiniteOrOutOfRangePositionsAsMissingPosition) {
  constexpr float quiet_nan = std::numeric_limits<float>::quiet_NaN();
  const auto env = load_env(world_plugin({top("STAT", {stat_record(0x840, {"lod4.nif", "lod8.nif"})})},
                                         {placed(0x820, 0x840, quiet_nan, 100.0f, 0.0f),
                                          placed(0x821, 0x840, 100.0f, 1e30f, 0.0f),
                                          placed(0x822, 0x840, 100.0f, 100.0f, quiet_nan)}));
  const auto collection = collect_object_lod_candidates(env, worldspace(), 8);
  EXPECT_TRUE(collection.candidates.empty());
  EXPECT_EQ(collection.counts.references_seen, 3u);
  EXPECT_EQ(skipped_count(collection, LodCandidateSkip::missing_position), 3u);
}

TEST(LodObjectCandidates, CountsAMalformedBaseMnamAndKeepsTheOtherReferences) {
  const auto env = load_env(world_plugin(
      {top("STAT", {stat_record(0x840, {"lod4.nif", "lod8.nif"}),
                    rec("STAT", 0x842, {{"MNAM", std::vector<std::byte>(300, std::byte{0})}})})},
      {placed(0x820, 0x842, 100.0f, 100.0f, 0.0f), placed(0x821, 0x840, 100.0f, 100.0f, 0.0f)}));
  const auto collection = collect_object_lod_candidates(env, worldspace(), 8);
  EXPECT_EQ(collection.counts.references_seen, 2u);
  EXPECT_EQ(collection.counts.included, 1u);
  EXPECT_EQ(skipped_count(collection, LodCandidateSkip::malformed), 1u);
  EXPECT_EQ(reference_keys(collection), (std::vector<std::string>{"000821:Test.esp"}));
}

TEST(LodObjectCandidates, CountsMalformedReferencesAndKeepsTheOtherReferences) {
  const auto env = load_env(world_plugin(
      {top("STAT", {stat_record(0x840, {"lod4.nif", "lod8.nif"})})},
      {placed(0x820, 0x840, 100.0f, 100.0f, 0.0f),
       placed(0x823, 0x840, 100.0f, 100.0f, 0.0f,
              {{"DATA", floats({100.0f, 100.0f, 0.0f, 0.0f, 0.0f, 90.0f})}}),
       rec("REFR", 0x824,
           {{"NAME", bytes_of({0x840})}, {"DATA", floats({100.0f, 100.0f, 0.0f, 0.0f, 0.0f})}}),
       placed(0x821, 0x840, 100.0f, 100.0f, 0.0f)}));
  const auto collection = collect_object_lod_candidates(env, worldspace(), 8);
  EXPECT_EQ(collection.counts.references_seen, 4u);
  EXPECT_EQ(collection.counts.included, 2u);
  EXPECT_EQ(skipped_count(collection, LodCandidateSkip::malformed), 2u);
  EXPECT_EQ(reference_keys(collection),
            (std::vector<std::string>{"000820:Test.esp", "000821:Test.esp"}));
}

TEST(LodObjectCandidates, DefaultsScaleToOneWhenXSCLIsAbsent) {
  const auto env = load_env(world_plugin({top("STAT", {stat_record(0x840, {"lod4.nif", "lod8.nif"})})},
                                         {placed(0x820, 0x840, 100.0f, 100.0f, 0.0f)}));
  const auto collection = collect_object_lod_candidates(env, worldspace(), 8);
  ASSERT_EQ(collection.candidates.size(), 1u);
  EXPECT_EQ(collection.candidates[0].scale, 1.0f);
}

TEST(LodObjectCandidates, DeletedReferenceIsDroppedByTheCellWalkNotCountedAsASkip) {
  const auto env = load_env(world_plugin({top("STAT", {stat_record(0x840, {"lod4.nif", "lod8.nif"})})},
                                         {placed(0x820, 0x840, 100.0f, 100.0f, 0.0f, {}, 0x20)}));
  const auto collection = collect_object_lod_candidates(env, worldspace(), 8);
  EXPECT_TRUE(collection.candidates.empty());
  EXPECT_EQ(collection.counts.references_seen, 0u);
  for (const auto count : collection.counts.skipped) EXPECT_EQ(count, 0u);
}

// Detects (invariant): an enable-parent skip returning; honouring XESP loses 19 of the 595 vanilla tiles that match exactly.
TEST(LodObjectCandidates, IncludesAChildWhateverItsEnableParentStateOrFlag) {
  const auto env = load_env(world_plugin(
      {top("STAT", {stat_record(0x840, {"lod4.nif", "lod8.nif"})})},
      {placed(0x820, 0x840, 100.0f, 100.0f, 0.0f),
       placed(0x821, 0x840, 200.0f, 200.0f, 0.0f, {{"XESP", bytes_of({0x820, 0x00})}}),
       placed(0x822, 0x840, 100.0f, 100.0f, 0.0f, {}, 0x800),
       placed(0x823, 0x840, 200.0f, 200.0f, 0.0f, {{"XESP", bytes_of({0x822, 0x00})}}),
       placed(0x824, 0x840, 200.0f, 200.0f, 0.0f, {{"XESP", bytes_of({0x820, 0x01})}}),
       placed(0x825, 0x840, 200.0f, 200.0f, 0.0f, {{"XESP", bytes_of({0x8FF, 0x00})}})}));
  const auto collection = collect_object_lod_candidates(env, worldspace(), 8);
  EXPECT_EQ(reference_keys(collection),
            (std::vector<std::string>{"000820:Test.esp", "000821:Test.esp", "000822:Test.esp", "000823:Test.esp",
                                      "000824:Test.esp", "000825:Test.esp"}));
  for (const auto count : collection.counts.skipped) EXPECT_EQ(count, 0u);
}

// Detects (invariant): children of a flagged enable parent merging into the always-shown shapes; vanilla keeps them in a
// shape of their own that the engine toggles with the parent.
TEST(LodObjectCandidates, TagsAChildOfAFlaggedEnableParentWithTheParentsRuntimeFormId) {
  const auto env = load_env(world_plugin(
      {top("STAT", {stat_record(0x840, {"lod4.nif", "lod8.nif"})})},
      {placed(0x820, 0x840, 100.0f, 100.0f, 0.0f, {}, 0x500),
       placed(0x821, 0x840, 200.0f, 200.0f, 0.0f, {{"XESP", bytes_of({0x820, 0x00})}}),
       placed(0x822, 0x840, 100.0f, 100.0f, 0.0f, {}, 0x400),
       placed(0x823, 0x840, 200.0f, 200.0f, 0.0f, {{"XESP", bytes_of({0x822, 0x00})}})}));
  const auto collection = collect_object_lod_candidates(env, worldspace(), 8);
  ASSERT_EQ(collection.candidates.size(), 4u);
  EXPECT_EQ(collection.candidates[0].toggle_ref, 0u);
  EXPECT_EQ(collection.candidates[1].toggle_ref, 0x820u);
  EXPECT_EQ(collection.candidates[2].toggle_ref, 0u);
  EXPECT_EQ(collection.candidates[3].toggle_ref, 0u);
}

namespace {

std::vector<std::byte> toggle_patch(std::uint32_t parent, std::uint32_t child) {
  return patch_plugin({grp(0x801, 6, {grp(0x801, 8, {placed(parent, 0x840, 100.0f, 100.0f, 0.0f, {}, 0x500),
                                                      placed(child, 0x840, 200.0f, 200.0f, 0.0f,
                                                             {{"XESP", bytes_of({parent, 0x00})}})})})});
}

std::uint32_t toggle_of(const LodCandidateCollection& collection, const std::string& reference) {
  for (const auto& candidate : collection.candidates) {
    if (candidate.reference.to_string() == reference) return candidate.toggle_ref;
  }
  return 0xFFFFFFFFu;
}

}  // namespace

// Detects (UNVERIFIED id rule, vanilla has no toggled shape under a non-base-game parent): a parent in a later full or
// light plugin tagged with a wrong runtime id, a light plugin consuming a full load-order index, or a second light
// plugin sharing the first one's slot.
TEST(LodObjectCandidates, TagsAChildWithTheRuntimeFormIdOfAParentInALaterFullOrLightPlugin) {
  std::vector<fo4::Plugin> plugins;
  plugins.push_back(fo4::read_plugin(world_plugin({top("STAT", {stat_record(0x840, {"lod4.nif", "lod8.nif"})})}, {}),
                                     "Test.esp"));
  plugins.push_back(fo4::read_plugin(toggle_patch(0x01000A20, 0x01000A21), "Light.esl"));
  plugins.push_back(fo4::read_plugin(toggle_patch(0x01000B30, 0x01000B31), "Light2.esl"));
  plugins.push_back(fo4::read_plugin(toggle_patch(0x01000820, 0x01000821), "Patch.esp"));
  const auto env = fo4::env::Environment::from_plugins(std::move(plugins));
  const auto collection = collect_object_lod_candidates(env, worldspace(), 8);
  ASSERT_EQ(collection.candidates.size(), 6u);
  EXPECT_EQ(toggle_of(collection, "000A21:Light.esl"), 0xFE000A20u);
  EXPECT_EQ(toggle_of(collection, "000B31:Light2.esl"), 0xFE001B30u);
  EXPECT_EQ(toggle_of(collection, "000821:Patch.esp"), 0x01000820u);
  EXPECT_EQ(toggle_of(collection, "000A20:Light.esl"), 0u);
}

namespace {

Node keyword_record(std::uint32_t id, std::string_view editor_id) {
  return rec("KYWD", id, {{"EDID", zstring(editor_id)}});
}

}  // namespace

// Detects (invariant): a MultirefLOD child drawn twice (its own mesh and its target's), or a child dropped although the
// target has no mesh at this level, or any other link keyword being treated as MultirefLOD.
TEST(LodObjectCandidates, SkipsAMultirefChildOnlyWhenItsTargetHasAMeshAtTheLevel) {
  const auto env = load_env(world_plugin(
      {top("KYWD", {keyword_record(0x870, "MultirefLOD"), keyword_record(0x871, "WorkshopStackedItemParentKEY")}),
       top("STAT", {stat_record(0x840, {"lod4.nif", "lod8.nif", "lod16.nif", ""}),
                    stat_record(0x841, {"lod4.nif", "", "lod16.nif", ""})})},
      {placed(0x820, 0x840, 100.0f, 100.0f, 0.0f),
       placed(0x821, 0x840, 110.0f, 100.0f, 0.0f, {{"XLKR", bytes_of({0x870, 0x820})}}),
       placed(0x822, 0x841, 120.0f, 100.0f, 0.0f),
       placed(0x823, 0x840, 130.0f, 100.0f, 0.0f, {{"XLKR", bytes_of({0x870, 0x822})}}),
       placed(0x824, 0x840, 140.0f, 100.0f, 0.0f, {{"XLKR", bytes_of({0x871, 0x820})}}),
       placed(0x825, 0x840, 150.0f, 100.0f, 0.0f, {{"XLKR", bytes_of({0x870, 0x8FF})}})}));
  const auto eight = collect_object_lod_candidates(env, worldspace(), 8);
  EXPECT_EQ(reference_keys(eight), (std::vector<std::string>{"000820:Test.esp", "000823:Test.esp", "000824:Test.esp",
                                                             "000825:Test.esp"}));
  EXPECT_EQ(skipped_count(eight, LodCandidateSkip::multiref_child), 1u);
  EXPECT_EQ(skipped_count(eight, LodCandidateSkip::no_lod_model), 1u);
  const auto sixteen = collect_object_lod_candidates(env, worldspace(), 16);
  EXPECT_EQ(reference_keys(sixteen),
            (std::vector<std::string>{"000820:Test.esp", "000822:Test.esp", "000824:Test.esp", "000825:Test.esp"}));
  EXPECT_EQ(skipped_count(sixteen, LodCandidateSkip::multiref_child), 2u);
}

// Detects (invariant): a collection linked by MultirefLOD to a target with a mesh still adding its parts.
TEST(LodObjectCandidates, SkipsAMultirefLinkedStaticCollection) {
  const auto env = load_env(world_plugin(
      {top("KYWD", {keyword_record(0x870, "MultirefLOD")}),
       top("STAT", {stat_record(0x840, {"lod4.nif", "lod8.nif"})}),
       top("SCOL", {scol_record(0x850, {{0x840, {0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f}}})})},
      {placed(0x820, 0x840, 100.0f, 100.0f, 0.0f),
       rec("REFR", 0x821, {{"NAME", bytes_of({0x850})},
                           {"DATA", floats({10.0f, 20.0f, 0.0f, 0.0f, 0.0f, 0.0f})},
                           {"XLKR", bytes_of({0x870, 0x820})}})}));
  const auto collection = collect_object_lod_candidates(env, worldspace(), 8);
  EXPECT_EQ(reference_keys(collection), (std::vector<std::string>{"000820:Test.esp"}));
  EXPECT_EQ(skipped_count(collection, LodCandidateSkip::multiref_child), 1u);
}

// Detects (invariant): a collection part filed under its own cell instead of the collection reference's cell, which moves
// vanilla's level-4 segment and the level-8 tile away from the tile that holds the collection.
TEST(LodObjectCandidates, FilesACollectionPartUnderTheCellOfItsParentReference) {
  const auto env = load_env(world_plugin(
      {top("STAT", {stat_record(0x840, {"lod4.nif", "lod8.nif"})}),
       top("SCOL", {scol_record(0x850, {{0x840, {5000.0f, -5000.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f}}})})},
      {refr_transform(0x820, 0x850, 1000.0f, 2000.0f, 50.0f, 0.0f, 0.0f, 0.0f, 1.0f)}));
  const auto collection = collect_object_lod_candidates(env, worldspace(), 8);
  ASSERT_EQ(collection.candidates.size(), 1u);
  EXPECT_NEAR(collection.candidates[0].position[0], 6000.0f, 1e-3f);
  EXPECT_NEAR(collection.candidates[0].position[1], -3000.0f, 1e-3f);
  EXPECT_EQ(collection.candidates[0].cell_x, 0);
  EXPECT_EQ(collection.candidates[0].cell_y, 0);
}

// Detects (invariant): a static collection collapsing to one candidate, or losing the placement transform.
TEST(LodObjectCandidates, ExpandsAStaticCollectionIntoOneCandidatePerPlacement) {
  constexpr float k_quarter_turn = 1.5707963267948966f;
  const auto env = load_env(world_plugin(
      {top("STAT", {stat_record(0x840, {"lod4.nif", "lod8.nif", "lod16.nif", "lod32.nif"})}),
       top("SCOL", {scol_record(0x850,
                               {{0x840, {0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f,
                                         100.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f,
                                         0.0f, 0.0f, 0.0f, 0.0f, 0.0f, k_quarter_turn, 0.5f}}})})},
      {refr_transform(0x820, 0x850, 1000.0f, 2000.0f, 50.0f, 0.0f, 0.0f, k_quarter_turn, 2.0f)}));
  const auto collection = collect_object_lod_candidates(env, worldspace(), 8);
  ASSERT_EQ(collection.candidates.size(), 3u);
  EXPECT_EQ(collection.counts.references_seen, 1u);
  EXPECT_EQ(collection.counts.included, 3u);
  for (const auto& candidate : collection.candidates) {
    EXPECT_EQ(candidate.reference.to_string(), "000820:Test.esp");
    EXPECT_EQ(candidate.base.to_string(), "000840:Test.esp");
    EXPECT_EQ(candidate.mesh, "lod8.nif");
    EXPECT_EQ(candidate.class_tag, fo4::lod::LodCandidateClass::scol_part);
  }
  const auto& first = collection.candidates[0];
  EXPECT_NEAR(first.position[0], 1000.0f, 1e-3f);
  EXPECT_NEAR(first.position[1], 2000.0f, 1e-3f);
  EXPECT_NEAR(first.position[2], 50.0f, 1e-3f);
  EXPECT_FLOAT_EQ(first.scale, 2.0f);
  // Invariant: a part offset turns and scales with its collection, clockwise for a positive heading: the part
  // 100 units east of a collection facing east ends up 200 units south of it.
  const auto& offset = collection.candidates[1];
  EXPECT_NEAR(offset.position[0], 1000.0f, 1e-3f);
  EXPECT_NEAR(offset.position[1], 1800.0f, 1e-3f);
  EXPECT_FLOAT_EQ(offset.scale, 2.0f);
  // Invariant: a part rotation composes with the collection rotation.
  const auto& turned = collection.candidates[2];
  EXPECT_FLOAT_EQ(turned.scale, 1.0f);
  ASSERT_TRUE(turned.rotation_basis.has_value());
  const auto& basis = *turned.rotation_basis;
  EXPECT_NEAR(basis[0], -1.0f, 1e-3f);
  EXPECT_NEAR(basis[1], 0.0f, 1e-3f);
  EXPECT_NEAR(basis[3], 0.0f, 1e-3f);
  EXPECT_NEAR(basis[4], -1.0f, 1e-3f);
  EXPECT_NEAR(basis[6], 0.0f, 1e-3f);
  EXPECT_NEAR(basis[8], 1.0f, 1e-3f);
}

// Detects (invariant): SCOL part tables that stop reading the level slot or that stop counting a missing part.
TEST(LodObjectCandidates, StaticCollectionPartsReadTheirOwnLevelSlotAndCountMissingModels) {
  const auto env = load_env(world_plugin(
      {top("STAT", {stat_record(0x840, {"lod4.nif", "lod8.nif", "", "lod32.nif"})}),
       top("SCOL", {scol_record(0x850, {{0x840, {0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f}}})})},
      {refr_transform(0x820, 0x850, 10.0f, 20.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f)}));
  const auto sixteen = collect_object_lod_candidates(env, worldspace(), 16);
  EXPECT_TRUE(sixteen.candidates.empty());
  EXPECT_EQ(skipped_count(sixteen, LodCandidateSkip::no_lod_model), 1u);
  const auto eight = collect_object_lod_candidates(env, worldspace(), 8);
  ASSERT_EQ(eight.candidates.size(), 1u);
  EXPECT_EQ(eight.candidates[0].mesh, "lod8.nif");
}

// Detects (invariant): a placed TREE reaching object LOD; xEdit restricts it to STAT and SCOL and the CK skips form type TREE (Radicle.md, box G19 resolved).
TEST(LodObjectCandidates, AReferencedTreeIsNotAnObjectLodCandidateAtAnyLevel) {
  const auto env = load_env(world_plugin(
      {top("TREE", {tree_record(0x860, "Meshes\\Landscape\\Trees\\Pine.nif")})},
      {refr_transform(0x820, 0x860, 48.0f, 52.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f)}));
  for (const std::int32_t level : {4, 8, 16, 32}) {
    const auto collection = collect_object_lod_candidates(env, worldspace(), level);
    EXPECT_TRUE(collection.candidates.empty()) << level;
    EXPECT_EQ(collection.counts.references_seen, 1u) << level;
    EXPECT_EQ(skipped_count(collection, LodCandidateSkip::not_lod_base), 1u) << level;
  }
}

// Detects (invariant): a partless collection silently vanishing from the counts, or a TREE counted as a missing LOD model.
TEST(LodObjectCandidates, ATreeIsNotALodBaseAndAPartlessCollectionCountsAsNoLodModel) {
  const auto env = load_env(world_plugin(
      {top("TREE", {tree_record(0x860, "")}), top("SCOL", {scol_record(0x850, {})})},
      {refr_transform(0x820, 0x860, 48.0f, 52.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f),
       refr_transform(0x821, 0x850, 48.0f, 52.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f)}));
  const auto collection = collect_object_lod_candidates(env, worldspace(), 8);
  EXPECT_TRUE(collection.candidates.empty());
  EXPECT_EQ(collection.counts.references_seen, 2u);
  EXPECT_EQ(skipped_count(collection, LodCandidateSkip::not_lod_base), 1u);
  EXPECT_EQ(skipped_count(collection, LodCandidateSkip::no_lod_model), 1u);
}
