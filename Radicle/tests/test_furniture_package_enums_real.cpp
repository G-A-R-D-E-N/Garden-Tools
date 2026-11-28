#include "fo4/environment/behavior.hpp"
#include "fo4/environment/environment.hpp"
#include "fo4/environment/package_procedure.hpp"
#include "fo4/forms/ui_records.hpp"
#include "real_data_build.hpp"
#include "real_data.hpp"

#include <gtest/gtest.h>

#include <filesystem>
#include <optional>
#include <set>
#include <stdexcept>

namespace {

using fo4::env::PackageLocationType;
using fo4::env::PackageTargetType;

static_assert(static_cast<int>(PackageLocationType::near_reference) == 0);
static_assert(static_cast<int>(PackageLocationType::near_package_start) == 2);
static_assert(static_cast<int>(PackageLocationType::near_editor_location) == 3);
static_assert(static_cast<int>(PackageLocationType::near_self) == 12);
static_assert(static_cast<int>(PackageLocationType::alias_collection_item) == 14);
static_assert(static_cast<int>(PackageTargetType::specific_reference) == 0);
static_assert(static_cast<int>(PackageTargetType::alias_collection_item) == 8);

constexpr std::uint32_t k_sleep_marker_flag = 0x80000000u;
constexpr std::uint32_t k_power_armor_record_flag = 0x02000000u;

fo4::FormKey master_key(std::uint32_t id) {
  return fo4::FormKey{fo4::PluginKey::from_filename("Fallout4.esm"), fo4::LocalFormId{id}};
}

std::optional<std::filesystem::path> master_path() {
  const auto* dir = ::fo4::test::fo4_data_dir_cstr();
  if (dir == nullptr) return std::nullopt;
  record_game_build(detect_game_build(dir));
  auto path = std::filesystem::path(dir) / "Fallout4.esm";
  if (!std::filesystem::exists(path)) return std::nullopt;
  return path;
}

const fo4::env::RecordRef& ref_of(const fo4::env::Environment& env, std::uint32_t id) {
  const auto* found = env.winner(master_key(id));
  if (found == nullptr) throw std::runtime_error("anchor record missing from the load order");
  return *found;
}

}  // namespace

TEST(FurnitureEnumsReal, VanillaFurnitureCarriesTheSleepMarkerFlagAndTheAnimTypesTheEngineNames) {
  const auto path = master_path();
  if (!path) GTEST_SKIP() << "set FO4_DATA_DIR for read-only official coverage";
  const std::filesystem::path paths[] = {*path};
  const auto env = fo4::env::Environment::load(paths);
  const auto decode = [&](std::uint32_t id) {
    return fo4::forms::decode_furniture_info(env.plugin(0), *ref_of(env, id).record);
  };

  const auto sit = decode(0x4CD2);
  EXPECT_EQ(sit.marker_flags, 0x40000001u);
  for (const auto& entry : sit.entry_points) {
    EXPECT_EQ(static_cast<std::uint32_t>(entry.anim_type), 1u);
    EXPECT_EQ(static_cast<std::uint32_t>(entry.entry_points), 0x0Du);
  }

  const auto bed = decode(0x20C40);
  ASSERT_TRUE(bed.marker_flags.has_value());
  EXPECT_EQ(*bed.marker_flags, 0x88000001u);
  EXPECT_NE(*bed.marker_flags & k_sleep_marker_flag, 0u);
  EXPECT_EQ(*sit.marker_flags & k_sleep_marker_flag, 0u);

  const auto& frame = ref_of(env, 0x34309);
  EXPECT_NE(frame.record->header.flags.value() & k_power_armor_record_flag, 0u);
  EXPECT_EQ(decode(0x34309).bench_type, 8);
  EXPECT_EQ(ref_of(env, 0x4CD2).record->header.flags.value() & k_power_armor_record_flag, 0u);

  std::set<std::uint16_t> anim_types;
  int sleepers = 0, frames = 0;
  for (const auto* ref : env.winners_of(fo4::Signature::from_string("FURN"))) {
    const auto info = fo4::forms::decode_furniture_info(env.plugin(ref->plugin), *ref->record);
    for (const auto& entry : info.entry_points) {
      anim_types.insert(entry.anim_type);
      EXPECT_EQ(static_cast<std::uint32_t>(entry.entry_points) & ~0x1Fu, 0u);
    }
    if (info.marker_flags && (*info.marker_flags & k_sleep_marker_flag) != 0) ++sleepers;
    if ((ref->record->header.flags.value() & k_power_armor_record_flag) != 0) ++frames;
  }
  EXPECT_TRUE(anim_types.count(1) == 1);
  for (const auto type : anim_types) EXPECT_TRUE(type == 0 || type == 1 || type == 2 || type == 4) << type;
  EXPECT_GT(sleepers, 0);
  EXPECT_GT(frames, 0);
}

TEST(PackageEnumsReal, VanillaPackagesUseOnlyTheDefinedLocationAndTargetTypes) {
  const auto path = master_path();
  if (!path) GTEST_SKIP() << "set FO4_DATA_DIR for read-only official coverage";
  const std::filesystem::path paths[] = {*path};
  const auto env = fo4::env::Environment::load(paths);
  const auto package_of = [&](std::uint32_t id) { return fo4::env::decode_package(env, ref_of(env, id)); };
  const auto has_location = [&](std::uint32_t id, PackageLocationType type) {
    for (const auto& location : package_of(id).locations)
      if (location.type == static_cast<std::int32_t>(type)) return true;
    return false;
  };
  const auto has_target = [&](std::uint32_t id, PackageTargetType type) {
    for (const auto& target : package_of(id).targets)
      if (target.type == static_cast<std::int32_t>(type)) return true;
    return false;
  };
  EXPECT_TRUE(has_location(0x2CBC, PackageLocationType::near_reference));
  EXPECT_TRUE(has_location(0x2CC8, PackageLocationType::in_cell));
  EXPECT_TRUE(has_location(0x2A838, PackageLocationType::near_package_start));
  EXPECT_TRUE(has_location(0x2CB1, PackageLocationType::near_editor_location));
  EXPECT_TRUE(has_location(0x2CB5, PackageLocationType::editor_location_cell));
  EXPECT_TRUE(has_target(0x2CB6, PackageTargetType::specific_reference));
  EXPECT_TRUE(has_target(0x20C3F, PackageTargetType::object_id));
  EXPECT_TRUE(has_target(0x2CE0, PackageTargetType::linked_reference));

  std::set<std::int32_t> locations, targets;
  for (const auto* ref : env.winners_of(fo4::Signature::from_string("PACK"))) {
    const auto package = fo4::env::decode_package(env, *ref);
    for (const auto& location : package.locations) locations.insert(location.type);
    for (const auto& target : package.targets) targets.insert(target.type);
  }
  for (const auto type : locations) {
    EXPECT_GE(type, 0);
    EXPECT_LE(type, static_cast<std::int32_t>(PackageLocationType::alias_collection_item));
  }
  for (const auto type : targets) {
    EXPECT_GE(type, 0);
    EXPECT_LE(type, static_cast<std::int32_t>(PackageTargetType::alias_collection_item));
  }
  for (const auto expected : {PackageLocationType::near_reference, PackageLocationType::in_cell,
                              PackageLocationType::near_package_start, PackageLocationType::near_editor_location,
                              PackageLocationType::near_linked_reference, PackageLocationType::quest_alias_8,
                              PackageLocationType::location_handle_9, PackageLocationType::named_special_reference,
                              PackageLocationType::data_input_id_match, PackageLocationType::near_self,
                              PackageLocationType::editor_location_cell, PackageLocationType::alias_collection_item})
    EXPECT_EQ(locations.count(static_cast<std::int32_t>(expected)), 1u) << static_cast<int>(expected);
  EXPECT_EQ(targets.size(), 9u);
}
