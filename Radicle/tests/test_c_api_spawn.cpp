#include "fo4/c_api_env.h"
#include "fo4/c_api_gameplay.h"
#include "fo4/c_api_spawn.h"

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>

#include "npc_visual_fixture.hpp"
#include "temp_data_dir.hpp"

using namespace npc_visual_fixture;

namespace {

using EnvPtr = std::unique_ptr<radicle_env, decltype(&radicle_env_destroy)>;

Bytes spawn_xesp(std::uint32_t parent, std::uint8_t flags) {
  fo4::test::ByteWriter out;
  out.u32(parent);
  out.u8(flags);
  out.u8(0);
  out.u8(0);
  out.u8(0);
  return out.bytes;
}

Bytes zone_data() {
  fo4::test::ByteWriter out;
  out.u32(0);
  out.u32(0);
  out.u8(0);
  out.u8(7);
  out.u8(0);
  out.u8(50);
  return out.bytes;
}

Bytes cnto(std::uint32_t item, std::int32_t count) {
  fo4::test::ByteWriter out;
  out.u32(item);
  out.u32(static_cast<std::uint32_t>(count));
  return out.bytes;
}

Bytes coed(std::uint32_t owner, std::uint32_t global_or_rank, float condition) {
  fo4::test::ByteWriter out;
  out.u32(owner);
  out.u32(global_or_rank);
  out.f32(condition);
  return out.bytes;
}

Rec lvli_with_row_count(std::uint32_t id, std::uint32_t item, std::uint16_t row_count) {
  auto rec = leveled("LVLI", id, 0x01, 0, {{1, item, 0}});
  for (auto& sub : rec.subs) {
    if (sub.first == "LVLO") {
      sub.second[8] = static_cast<std::byte>(row_count & 0xFFu);
      sub.second[9] = static_cast<std::byte>(row_count >> 8);
    }
  }
  return rec;
}

Rec lvli_two_rows(std::uint32_t id) {
  auto rec = leveled("LVLI", id, 0x01, 0, {{1, 0x910, 0}, {1, 0x872, 0}, {5, 0x872, 0}});
  std::uint16_t counts[] = {2, 6, 9};
  size_t row = 0;
  for (auto& sub : rec.subs) {
    if (sub.first == "LVLO") sub.second[8] = static_cast<std::byte>(counts[row++]);
  }
  return rec;
}

EnvPtr load_spawn_env() {
  auto records = base_records();
  records.push_back(Rec{"ARMO", 0x910, {{"EDID", zstr("Armor")}, {"BOD2", words({0x8})}}});
  records.push_back(Rec{"REFR", 0x930,
                        {{"NAME", words({0x862})},
                         {"XCNT", words({3})},
                         {"XLCM", words({2})},
                         {"XEZN", words({0x940})},
                         {"XESP", spawn_xesp(0x931, 0x01)}}});
  records.push_back(Rec{"REFR", 0x931, {{"NAME", words({0x870})}}});
  records.push_back(Rec{"REFR", 0x932, {{"NAME", words({0x87F})}}});
  records.push_back(npc({.id = 0x872}));
  records.push_back(leveled("LVLN", 0x87F, 0x01, 0, {{1, 0x872, 0}}));
  records.push_back(lvli_with_row_count(0xB00, 0x910, 4));
  records.push_back(lvli_two_rows(0xB01));
  records.push_back(Rec{"NPC_", 0x873,
                        {{"EDID", zstr("Merchant")},
                         {"DOFT", words({0x910})},
                         {"COCT", words({3})},
                         {"CNTO", cnto(0x910, 2)},
                         {"COED", coed(0x872, 7, 0.5f)},
                         {"CNTO", cnto(0xB00, 1)},
                         {"CNTO", cnto(0x872, -3)}}});
  records.push_back(Rec{"ECZN", 0x940, {{"EDID", zstr("Zone")}, {"DATA", zone_data()}}});
  const auto bytes = build({}, 0x1, records);
  const auto root =
      std::filesystem::temp_directory_path() / ("radicle_c_api_spawn_" + std::to_string(fo4::test::process_id()));
  std::filesystem::create_directories(root);
  const auto path = (root / "Base.esm").string();
  std::ofstream out(path, std::ios::binary);
  out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
  out.close();
  const char* paths[] = {path.c_str()};
  radicle_env* env = nullptr;
  EXPECT_EQ(radicle_env_load(paths, 1, &env), RADICLE_STATUS_OK) << radicle_last_error();
  return EnvPtr(env, radicle_env_destroy);
}

radicle_form_key radicle_key(std::uint32_t local) { return radicle_form_key{"Base.esm", local}; }

}  // namespace

TEST(CApiSpawn, NullArgumentsAreRejected) {
  radicle_leveled_pick_result* pick = nullptr;
  EXPECT_EQ(radicle_leveled_pick(nullptr, radicle_key(0x870), 1, 0, 0, &pick), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(pick, nullptr);
  radicle_enable_verdict verdict{};
  EXPECT_EQ(radicle_ref_enable_state(nullptr, nullptr, nullptr, 0, &verdict), RADICLE_STATUS_INVALID_ARGUMENT);
  radicle_actor_spawn spawn{};
  EXPECT_EQ(radicle_placed_actor_spawn_decode(nullptr, nullptr, 1, 0, nullptr, 0, &spawn),
            RADICLE_STATUS_INVALID_ARGUMENT);
  radicle_spawn_plan* plan = nullptr;
  EXPECT_EQ(radicle_script_spawn_plan(nullptr, radicle_key(0x862), 1, 0, 0, 0, &plan),
            RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(plan, nullptr);
  radicle_leveled_pick_result_free(nullptr);
  radicle_spawn_plan_free(nullptr);
}

TEST(CApiSpawn, LevelMustBeInRange) {
  const auto env = load_spawn_env();
  ASSERT_NE(env, nullptr);
  radicle_leveled_pick_result* pick = nullptr;
  EXPECT_EQ(radicle_leveled_pick(env.get(), radicle_key(0x870), 0, 0, 0, &pick), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_leveled_pick(env.get(), radicle_key(0x870), 0x10000u, 0, 0, &pick),
            RADICLE_STATUS_INVALID_ARGUMENT);
  radicle_spawn_plan* plan = nullptr;
  EXPECT_EQ(radicle_script_spawn_plan(env.get(), radicle_key(0x862), 0, 0, 0, 0, &plan),
            RADICLE_STATUS_INVALID_ARGUMENT);
  const radicle_record* ref = nullptr;
  ASSERT_EQ(radicle_env_winner(env.get(), radicle_key(0x930), &ref), RADICLE_STATUS_OK);
  radicle_actor_spawn spawn{};
  EXPECT_EQ(radicle_placed_actor_spawn_decode(env.get(), ref, 0, 0, nullptr, 0, &spawn),
            RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_placed_actor_spawn_decode(env.get(), ref, 0x10000u, 0, nullptr, 0, &spawn),
            RADICLE_STATUS_INVALID_ARGUMENT);
}

TEST(CApiSpawn, FlagsOverrideMaskAcceptsUseAllAndCalculateForEachAndRefusesTheRest) {
  const auto env = load_spawn_env();
  ASSERT_NE(env, nullptr);
  radicle_leveled_pick_result* pick = nullptr;
  ASSERT_EQ(radicle_leveled_pick(env.get(), radicle_key(0x87F), 1, 0, 0x02, &pick), RADICLE_STATUS_OK)
      << radicle_last_error();
  ASSERT_EQ(pick->entry_count, 1u);
  radicle_leveled_pick_result_free(pick);
  pick = nullptr;
  EXPECT_EQ(radicle_leveled_pick(env.get(), radicle_key(0x87F), 1, 0, 0x01, &pick),
            RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(pick, nullptr);
  EXPECT_EQ(radicle_leveled_pick(env.get(), radicle_key(0x87F), 1, 0, 0x08, &pick),
            RADICLE_STATUS_INVALID_ARGUMENT);
  ASSERT_EQ(radicle_leveled_pick(env.get(), radicle_key(0x87F), 1, 0, 0x04, &pick), RADICLE_STATUS_OK)
      << radicle_last_error();
  ASSERT_EQ(pick->entry_count, 1u);
  EXPECT_EQ(pick->entries[0].local_id, 0x872u);
  EXPECT_EQ(pick->reason, 0u);
  radicle_leveled_pick_result_free(pick);
}

TEST(CApiSpawn, LeveledPickCountRejectsBadArguments) {
  const auto env = load_spawn_env();
  ASSERT_NE(env, nullptr);
  radicle_leveled_pick_count_result* pick = nullptr;
  EXPECT_EQ(radicle_leveled_pick_count(nullptr, radicle_key(0x87F), 1, 0, 0, 1, &pick),
            RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_leveled_pick_count(env.get(), radicle_key(0x87F), 1, 0, 0, 1, nullptr),
            RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_leveled_pick_count(env.get(), radicle_key(0x87F), 0, 0, 0, 1, &pick),
            RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_leveled_pick_count(env.get(), radicle_key(0x87F), 1, 0, 0, 0, &pick),
            RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_leveled_pick_count(env.get(), radicle_key(0x87F), 1, 0, 0, 0x8000u, &pick),
            RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_leveled_pick_count(env.get(), radicle_key(0x87F), 1, 0, 0x01, 1, &pick),
            RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(pick, nullptr);
  radicle_leveled_pick_count_result_free(nullptr);
}

TEST(CApiSpawn, LeveledPickCountMultipliesWhenTheFlagIsClearAndRollsWhenForced) {
  const auto env = load_spawn_env();
  ASSERT_NE(env, nullptr);
  radicle_leveled_pick_count_result* pick = nullptr;
  ASSERT_EQ(radicle_leveled_pick_count(env.get(), radicle_key(0x87F), 1, 0, 0, 3, &pick), RADICLE_STATUS_OK)
      << radicle_last_error();
  EXPECT_EQ(pick->entry_count, 1u);
  EXPECT_EQ(pick->rolls, 1u);
  EXPECT_EQ(pick->multiplier, 3u);
  EXPECT_EQ(pick->reason, 0u);
  radicle_leveled_pick_count_result_free(pick);

  ASSERT_EQ(radicle_leveled_pick_count(env.get(), radicle_key(0x87F), 1, 0, 0x02, 3, &pick), RADICLE_STATUS_OK)
      << radicle_last_error();
  ASSERT_EQ(pick->entry_count, 3u);
  EXPECT_EQ(pick->rolls, 3u);
  EXPECT_EQ(pick->multiplier, 1u);
  for (size_t i = 0; i < pick->entry_count; ++i) EXPECT_EQ(pick->entries[i].local_id, 0x872u);
  EXPECT_EQ(pick->reason, 0u);
  radicle_leveled_pick_count_result_free(pick);
}

TEST(CApiSpawn, LeveledPickCountKeepsRollingAndReportsTheReasonWhenEveryRollIsEmpty) {
  const auto env = load_spawn_env();
  ASSERT_NE(env, nullptr);
  radicle_leveled_pick_count_result* pick = nullptr;
  ASSERT_EQ(radicle_leveled_pick_count(env.get(), radicle_key(0x870), 1, 0, 0x02, 4, &pick), RADICLE_STATUS_OK)
      << radicle_last_error();
  EXPECT_EQ(pick->entry_count, 0u);
  EXPECT_EQ(pick->entries, nullptr);
  EXPECT_EQ(pick->reason, 2u);
  EXPECT_EQ(pick->rolls, 4u);
  radicle_leveled_pick_count_result_free(pick);
}

TEST(CApiSpawn, LeveledPickReportsItsReasonAndFreesCleanly) {
  const auto env = load_spawn_env();
  ASSERT_NE(env, nullptr);
  radicle_leveled_pick_result* pick = nullptr;
  ASSERT_EQ(radicle_leveled_pick(env.get(), radicle_key(0x870), 1, 0, 0, &pick), RADICLE_STATUS_OK)
      << radicle_last_error();
  EXPECT_EQ(pick->entry_count, 0u);
  EXPECT_EQ(pick->reason, 2u);
  radicle_leveled_pick_result_free(pick);

  ASSERT_EQ(radicle_leveled_pick(env.get(), radicle_key(0x910), 1, 0, 0, &pick), RADICLE_STATUS_OK);
  EXPECT_EQ(pick->reason, 8u);
  radicle_leveled_pick_result_free(pick);
}

TEST(CApiSpawn, EnableStateAndOverridesAreApplied) {
  const auto env = load_spawn_env();
  ASSERT_NE(env, nullptr);
  const radicle_record* ref = nullptr;
  ASSERT_EQ(radicle_env_winner(env.get(), radicle_key(0x930), &ref), RADICLE_STATUS_OK);
  radicle_enable_verdict verdict{};
  ASSERT_EQ(radicle_ref_enable_state(env.get(), ref, nullptr, 0, &verdict), RADICLE_STATUS_OK)
      << radicle_last_error();
  EXPECT_EQ(verdict.enabled, 0u);
  EXPECT_EQ(verdict.stop, 0u);
  EXPECT_EQ(verdict.chain_length, 1u);
  EXPECT_EQ(std::string(verdict.deciding.plugin), "Base.esm");
  EXPECT_EQ(verdict.deciding.local_id, 0x931u);

  const radicle_ref_state_override override_parent{radicle_key(0x931), 0};
  ASSERT_EQ(radicle_ref_enable_state(env.get(), ref, &override_parent, 1, &verdict), RADICLE_STATUS_OK);
  EXPECT_EQ(verdict.enabled, 1u);
  EXPECT_EQ(verdict.stop, 1u);

  const radicle_ref_state_override duplicate[] = {{radicle_key(0x931), 0}, {radicle_key(0x931), 1}};
  EXPECT_EQ(radicle_ref_enable_state(env.get(), ref, duplicate, 2, &verdict), RADICLE_STATUS_INVALID_ARGUMENT);
}

TEST(CApiSpawn, NonPlacedRecordIsRejectedForEnableState) {
  const auto env = load_spawn_env();
  ASSERT_NE(env, nullptr);
  const radicle_record* armor = nullptr;
  ASSERT_EQ(radicle_env_winner(env.get(), radicle_key(0x910), &armor), RADICLE_STATUS_OK);
  radicle_enable_verdict verdict{};
  EXPECT_EQ(radicle_ref_enable_state(env.get(), armor, nullptr, 0, &verdict), RADICLE_STATUS_INVALID_ARGUMENT);
}

TEST(CApiSpawn, PlacedActorSpawnDecodesWithoutAFreeStep) {
  const auto env = load_spawn_env();
  ASSERT_NE(env, nullptr);
  const radicle_record* ref = nullptr;
  ASSERT_EQ(radicle_env_winner(env.get(), radicle_key(0x930), &ref), RADICLE_STATUS_OK);
  radicle_actor_spawn spawn{};
  ASSERT_EQ(radicle_placed_actor_spawn_decode(env.get(), ref, 1, 0, nullptr, 0, &spawn), RADICLE_STATUS_OK)
      << radicle_last_error();
  EXPECT_EQ(spawn.has_base, 1u);
  EXPECT_EQ(spawn.base_kind, 1u);
  EXPECT_EQ(spawn.has_count, 1);
  EXPECT_EQ(spawn.count, 3);
  EXPECT_EQ(spawn.has_level_modifier, 1);
  EXPECT_EQ(spawn.level_modifier, 2);
  EXPECT_EQ(spawn.enable.enabled, 0u);
  EXPECT_EQ(spawn.has_encounter_zone, 1u);
  EXPECT_EQ(spawn.has_resolved_base, 1u);
  EXPECT_EQ(spawn.resolved_base.local_id, 0x862u);
  EXPECT_EQ(spawn.has_zone_min_level, 1u);
  EXPECT_EQ(spawn.zone_min_level, 7);
  EXPECT_EQ(spawn.level_rule_verified, 0u);
  EXPECT_EQ(spawn.resolved_level, 0u);
}

TEST(CApiSpawn, LeveledPlacedBaseResolvesToItsNpcAtThePlayerLevel) {
  const auto env = load_spawn_env();
  ASSERT_NE(env, nullptr);
  const radicle_record* ref = nullptr;
  ASSERT_EQ(radicle_env_winner(env.get(), radicle_key(0x932), &ref), RADICLE_STATUS_OK);
  radicle_actor_spawn spawn{};
  ASSERT_EQ(radicle_placed_actor_spawn_decode(env.get(), ref, 1, 0, nullptr, 0, &spawn), RADICLE_STATUS_OK)
      << radicle_last_error();
  EXPECT_EQ(spawn.base_kind, 2u);
  EXPECT_EQ(spawn.has_resolved_base, 1u);
  EXPECT_EQ(spawn.resolved_base.local_id, 0x872u);
  EXPECT_EQ(spawn.has_encounter_zone, 0u);
  EXPECT_EQ(spawn.has_zone_min_level, 0u);
}

TEST(CApiSpawn, ScriptSpawnPlanPicksAndEchoesTheModifier) {
  const auto env = load_spawn_env();
  ASSERT_NE(env, nullptr);
  radicle_spawn_plan* plan = nullptr;
  ASSERT_EQ(radicle_script_spawn_plan(env.get(), radicle_key(0x862), 1, 0, 1, 2, &plan), RADICLE_STATUS_OK)
      << radicle_last_error();
  EXPECT_EQ(plan->base_kind, 1u);
  EXPECT_EQ(plan->reason, 0u);
  ASSERT_EQ(plan->entry_count, 1u);
  EXPECT_EQ(plan->entries[0].local_id, 0x862u);
  EXPECT_EQ(plan->has_level_modifier, 1);
  EXPECT_EQ(plan->level_modifier, 2);
  radicle_spawn_plan_free(plan);
}

TEST(CApiSpawn, NpcInventoryReportsCntoRowsWithCoedAndLeavesTheOutfitOut) {
  const auto env = load_spawn_env();
  ASSERT_NE(env, nullptr);
  const radicle_record* npc = nullptr;
  ASSERT_EQ(radicle_env_winner(env.get(), radicle_key(0x873), &npc), RADICLE_STATUS_OK);
  radicle_npc_inventory* inventory = nullptr;
  ASSERT_EQ(radicle_npc_inventory_decode(env.get(), npc, &inventory), RADICLE_STATUS_OK) << radicle_last_error();
  ASSERT_EQ(inventory->item_count, 3u);
  EXPECT_EQ(inventory->items[0].item.local_id, 0x910u);
  EXPECT_EQ(inventory->items[0].count, 2);
  EXPECT_EQ(inventory->items[0].has_extra, 1);
  EXPECT_EQ(inventory->items[0].has_owner, 1);
  EXPECT_EQ(inventory->items[0].owner.local_id, 0x872u);
  EXPECT_EQ(inventory->items[0].global_or_rank, 7u);
  EXPECT_FLOAT_EQ(inventory->items[0].condition, 0.5f);
  EXPECT_EQ(inventory->items[1].item.local_id, 0xB00u);
  EXPECT_EQ(inventory->items[1].has_extra, 0);
  EXPECT_EQ(inventory->items[2].count, -3);
  radicle_npc_inventory_free(inventory);

  radicle_npc_inventory* none = nullptr;
  const radicle_record* not_npc = nullptr;
  ASSERT_EQ(radicle_env_winner(env.get(), radicle_key(0x87F), &not_npc), RADICLE_STATUS_OK);
  EXPECT_EQ(radicle_npc_inventory_decode(env.get(), not_npc, &none), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(none, nullptr);
  EXPECT_EQ(radicle_npc_inventory_decode(nullptr, npc, &none), RADICLE_STATUS_INVALID_ARGUMENT);
  radicle_npc_inventory_free(nullptr);
}

TEST(CApiSpawn, LeveledPickCountReportsTheRowCountOfEveryEntryAndSkipsRowsAboveTheLevel) {
  const auto env = load_spawn_env();
  ASSERT_NE(env, nullptr);
  radicle_leveled_pick_count_result* pick = nullptr;
  ASSERT_EQ(radicle_leveled_pick_count(env.get(), radicle_key(0xB00), 1, 0, 0, 1, &pick), RADICLE_STATUS_OK)
      << radicle_last_error();
  ASSERT_EQ(pick->entry_count, 1u);
  ASSERT_NE(pick->entry_counts, nullptr);
  EXPECT_EQ(pick->entries[0].local_id, 0x910u);
  EXPECT_EQ(pick->entry_counts[0], 4u);
  radicle_leveled_pick_count_result_free(pick);

  ASSERT_EQ(radicle_leveled_pick_count(env.get(), radicle_key(0xB01), 1, 0, 0x04, 1, &pick), RADICLE_STATUS_OK)
      << radicle_last_error();
  ASSERT_EQ(pick->entry_count, 2u);
  EXPECT_EQ(pick->entries[0].local_id, 0x910u);
  EXPECT_EQ(pick->entries[1].local_id, 0x872u);
  EXPECT_EQ(pick->entry_counts[0], 2u);
  EXPECT_EQ(pick->entry_counts[1], 6u);
  radicle_leveled_pick_count_result_free(pick);

  ASSERT_EQ(radicle_leveled_pick_count(env.get(), radicle_key(0xB00), 1, 0, 0x02, 3, &pick), RADICLE_STATUS_OK);
  ASSERT_EQ(pick->entry_count, 3u);
  for (size_t i = 0; i < pick->entry_count; ++i) EXPECT_EQ(pick->entry_counts[i], 4u);
  radicle_leveled_pick_count_result_free(pick);

  ASSERT_EQ(radicle_leveled_pick_count(env.get(), radicle_key(0x870), 1, 0, 0x02, 4, &pick), RADICLE_STATUS_OK);
  EXPECT_EQ(pick->entry_counts, nullptr);
  radicle_leveled_pick_count_result_free(pick);
}
