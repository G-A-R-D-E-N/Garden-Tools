#include "npc_visual_fixture.hpp"

#include "fo4/c_api_env.h"
#include "fo4/c_api_gameplay.h"
#include "fo4/forms/gameplay_rules.hpp"
#include "fo4/forms/npc_gameplay.hpp"
#include "fo4/plugin/reader.hpp"

#include <gtest/gtest.h>

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <initializer_list>
#include <limits>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace {

using fo4::forms::AvEvidence;
using fo4::forms::AvSource;
using fo4::forms::CombatReaction;
using fo4::forms::CombatStyleSource;
using fo4::forms::FactionMembership;
using fo4::forms::LevelReason;
using fo4::forms::ReactionReason;
using npc_visual_fixture::Bytes;
using npc_visual_fixture::Rec;
using npc_visual_fixture::Subs;
using npc_visual_fixture::build;
using npc_visual_fixture::key;
using npc_visual_fixture::make_env;
using npc_visual_fixture::words;
using npc_visual_fixture::zstr;

constexpr std::uint32_t k_faction_a = 0x900;
constexpr std::uint32_t k_faction_b = 0x901;
constexpr std::uint32_t k_faction_c = 0x902;
constexpr std::uint32_t k_faction_d = 0x903;
constexpr std::uint32_t k_faction_e = 0x904;
constexpr std::uint32_t k_faction_unrelated = 0x905;
constexpr std::uint32_t k_faction_deleted = 0x906;
constexpr std::uint32_t k_faction_undecodable = 0x907;
constexpr std::uint32_t k_faction_self = 0x908;
constexpr std::uint32_t k_zone_plain = 0x960;
constexpr std::uint32_t k_zone_match_pc = 0x961;
constexpr std::uint32_t k_zone_capped = 0x962;
constexpr std::uint32_t k_zone_short = 0x963;
constexpr std::uint32_t k_crime_faction = 0x920;
constexpr std::uint32_t k_health = 0x930;
constexpr std::uint32_t k_strength = 0x931;
constexpr std::uint32_t k_action_points = 0x932;
constexpr std::uint32_t k_avif_deleted = 0x933;
constexpr std::uint32_t k_avif_undecodable = 0x934;
constexpr std::uint32_t k_perception = 0x935;
constexpr std::uint32_t k_endurance = 0x936;
constexpr std::uint32_t k_charisma = 0x937;
constexpr std::uint32_t k_intelligence = 0x938;
constexpr std::uint32_t k_agility = 0x939;
constexpr std::uint32_t k_luck = 0x93A;
constexpr std::uint32_t k_speed_mult = 0x93B;
constexpr std::uint32_t k_race = 0x940;
constexpr std::uint32_t k_class = 0x941;
constexpr std::uint32_t k_style = 0x942;
constexpr std::uint32_t k_spell = 0x943;
constexpr std::uint32_t k_perk = 0x944;
constexpr std::uint32_t k_template_a = 0x950;
constexpr std::uint32_t k_template_b = 0x951;
constexpr std::uint32_t k_missing_template = 0x952;
constexpr std::uint32_t k_npc = 0x910;
constexpr std::uint32_t k_npc_child = 0x911;
constexpr std::uint32_t k_npc_missing = 0x912;
constexpr std::uint32_t k_npc_scaled = 0x913;
constexpr std::uint32_t k_npc_undecodable = 0x914;
constexpr std::uint32_t k_template_undecodable = 0x953;
constexpr std::uint32_t k_deleted_flag = 0x20;
constexpr std::uint32_t k_compressed_flag = 0x00040000;

Rec undecodable_rec(const char* signature, std::uint32_t id) {
  return Rec{signature, id, {{"EDID", zstr("Corrupt")}}, k_compressed_flag};
}

Bytes acbs_with_level(std::uint32_t flags, std::uint16_t level, std::uint16_t template_flags,
                      std::uint16_t calc_min = 0, std::uint16_t calc_max = 0) {
  fo4::test::ByteWriter out;
  out.u32(flags);
  out.u16(0);
  out.u16(level);
  out.u16(calc_min);
  out.u16(calc_max);
  out.u16(0);
  out.u16(template_flags);
  out.u16(0);
  out.u16(0);
  return out.bytes;
}

Bytes faction_membership(std::uint32_t faction, std::uint8_t rank) {
  fo4::test::ByteWriter out;
  out.u32(faction);
  out.u8(rank);
  return out.bytes;
}

Bytes perk_rank(std::uint32_t perk, std::uint8_t rank) {
  fo4::test::ByteWriter out;
  out.u32(perk);
  out.u8(rank);
  return out.bytes;
}

Bytes calculated_stats(std::uint16_t health, std::uint16_t action_points) {
  fo4::test::ByteWriter out;
  out.u16(health);
  out.u16(action_points);
  out.u16(0);
  out.u16(0);
  return out.bytes;
}

Bytes properties(std::initializer_list<std::pair<std::uint32_t, float>> entries) {
  fo4::test::ByteWriter out;
  for (const auto& [avif, value] : entries) {
    out.u32(avif);
    out.f32(value);
  }
  return out.bytes;
}

Rec npc_with(std::uint32_t id, std::uint32_t flags, std::uint16_t level, std::uint16_t template_flags,
             Subs extra = {}, std::uint16_t calc_min = 0, std::uint16_t calc_max = 0);

Rec special_npc(std::uint16_t template_flags = 0) {
  return npc_with(k_npc, 0, 12, template_flags,
                  {{"DNAM", calculated_stats(150, 40)},
                   {"PRPS", properties({{k_speed_mult, 100.0f},
                                        {k_strength, 7.0f},
                                        {k_perception, 6.0f},
                                        {k_endurance, -2.0f},
                                        {k_charisma, 4.6f},
                                        {k_intelligence, 0.0f}})}});
}

Bytes aidt() {
  fo4::test::ByteWriter out;
  out.u8(1);
  out.u8(2);
  out.u8(3);
  out.u8(4);
  out.u8(5);
  out.u8(6);
  for (int index = 0; index < 18; ++index) out.u8(0);
  return out.bytes;
}

Bytes eczn_data(std::uint8_t min_level, std::uint8_t flags, std::uint8_t max_level) {
  fo4::test::ByteWriter out;
  out.u32(0);
  out.u32(0);
  out.u8(0);
  out.u8(min_level);
  out.u8(flags);
  out.u8(max_level);
  return out.bytes;
}

Bytes tpta(std::uint32_t stats) { return words({0, stats, 0, 0}); }

Bytes crime_values() {
  fo4::test::ByteWriter out;
  out.u8(1);
  out.u8(1);
  out.u16(10);
  out.u16(20);
  out.u16(30);
  out.u16(40);
  out.u16(50);
  out.f32(0.5f);
  out.u16(60);
  out.u16(70);
  return out.bytes;
}

Rec npc_with(std::uint32_t id, std::uint32_t flags, std::uint16_t level, std::uint16_t template_flags,
             Subs extra, std::uint16_t calc_min, std::uint16_t calc_max) {
  Subs subs{{"EDID", zstr("Npc" + std::to_string(id))},
            {"ACBS", acbs_with_level(flags, level, template_flags, calc_min, calc_max)}};
  subs.insert(subs.end(), extra.begin(), extra.end());
  return Rec{"NPC_", id, std::move(subs)};
}

Rec npc_record(std::uint32_t flags, std::uint16_t level, std::uint16_t template_flags) {
  return npc_with(k_npc, flags, level, template_flags,
                  {{"SNAM", faction_membership(k_faction_a, 2)}, {"DNAM", calculated_stats(150, 40)}});
}

Rec rich_npc() {
  return npc_with(k_npc, 0, 12, 0,
                  {{"SNAM", faction_membership(k_faction_a, 2)},
                   {"DNAM", calculated_stats(150, 40)},
                   {"RNAM", words({k_race})},
                   {"CNAM", words({k_class})},
                   {"ZNAM", words({k_style})},
                   {"CRIF", words({k_crime_faction})},
                   {"SPLO", words({k_spell})},
                   {"PRKZ", words({1})},
                   {"PRKR", perk_rank(k_perk, 3)},
                   {"AIDT", aidt()}});
}

Rec template_a() { return npc_with(k_template_a, 0, 5, 0); }

Rec template_b() { return npc_with(k_template_b, 0, 6, 0x2, {{"TPTA", tpta(k_template_a)}}); }

Rec child_of_b() { return npc_with(k_npc_child, 0, 9, 0x2, {{"TPTA", tpta(k_template_b)}}); }

Rec child_of_missing() { return npc_with(k_npc_missing, 0, 9, 0x2, {{"TPTA", tpta(k_missing_template)}}); }

std::vector<Rec> records_with(std::vector<Rec> npcs) {
  std::vector<Rec> records{
      Rec{"FACT", k_faction_a, {{"EDID", zstr("FactionA")}, {"XNAM", words({k_faction_b, 0, 1})}}},
      Rec{"FACT", k_faction_b, {{"EDID", zstr("FactionB")}, {"XNAM", words({k_faction_a, 0, 2})}}},
      Rec{"FACT",
          k_faction_c,
          {{"EDID", zstr("FactionC")},
           {"XNAM", words({k_faction_d, 5, 3})},
           {"XNAM", words({k_faction_e, static_cast<std::uint32_t>(-50), 1})}}},
      Rec{"FACT",
          k_faction_self,
          {{"EDID", zstr("FactionSelf")},
           {"XNAM", words({k_faction_b, static_cast<std::uint32_t>(-100), 1})},
           {"XNAM", words({k_faction_self, 10, 2})}}},
      Rec{"ECZN", k_zone_plain, {{"EDID", zstr("ZonePlain")}, {"DATA", eczn_data(10, 0, 0)}}},
      Rec{"ECZN", k_zone_match_pc, {{"EDID", zstr("ZoneMatch")}, {"DATA", eczn_data(10, 0x02, 0)}}},
      Rec{"ECZN", k_zone_capped, {{"EDID", zstr("ZoneCapped")}, {"DATA", eczn_data(10, 0, 20)}}},
      Rec{"ECZN", k_zone_short, {{"EDID", zstr("ZoneShort")}, {"DATA", words({0, 0})}}},
      Rec{"FACT", k_faction_d, {{"EDID", zstr("FactionD")}}},
      Rec{"FACT", k_faction_e, {{"EDID", zstr("FactionE")}}},
      Rec{"FACT", k_faction_unrelated, {{"EDID", zstr("FactionF")}}},
      Rec{"FACT", k_crime_faction, {{"EDID", zstr("Crime")}, {"CRVA", crime_values()}}},
      Rec{"AVIF", k_health, {{"EDID", zstr("Health")}}},
      Rec{"AVIF", k_strength, {{"EDID", zstr("Strength")}}},
      Rec{"AVIF", k_action_points, {{"EDID", zstr("ActionPoints")}}},
      Rec{"AVIF", k_perception, {{"EDID", zstr("PERCEPTION")}}},
      Rec{"AVIF", k_endurance, {{"EDID", zstr("Endurance")}}},
      Rec{"AVIF", k_charisma, {{"EDID", zstr("Charisma")}}},
      Rec{"AVIF", k_intelligence, {{"EDID", zstr("Intelligence")}}},
      Rec{"AVIF", k_agility, {{"EDID", zstr("Agility")}}},
      Rec{"AVIF", k_luck, {{"EDID", zstr("Luck")}}},
      Rec{"AVIF", k_speed_mult, {{"EDID", zstr("SpeedMult")}}},
      Rec{"RACE", k_race, {{"EDID", zstr("Race")}}},
      Rec{"CLAS", k_class, {{"EDID", zstr("Class")}}},
      Rec{"CSTY", k_style, {{"EDID", zstr("Style")}}},
      Rec{"SPEL", k_spell, {{"EDID", zstr("Spell")}}},
      Rec{"PERK", k_perk, {{"EDID", zstr("Perk")}}},
  };
  for (auto& npc : npcs) records.push_back(std::move(npc));
  return records;
}

fo4::env::Environment env_with(std::vector<Rec> npcs) {
  return make_env({{"Base.esm", build({}, 0x1, records_with(std::move(npcs)))}});
}

fo4::env::Environment env_of(Rec npc) { return env_with({std::move(npc)}); }

const fo4::env::RecordRef& ref_of(const fo4::env::Environment& env, std::uint32_t local) {
  const auto* ref = env.winner(key(local));
  if (ref == nullptr) throw std::runtime_error("fixture record missing");
  return *ref;
}

fo4::forms::NpcGameplay decode_local(const fo4::env::Environment& env, std::uint32_t local) {
  return fo4::forms::decode_npc_gameplay(env, ref_of(env, local));
}

using EnvPtr = std::unique_ptr<radicle_env, decltype(&radicle_env_destroy)>;
using GameplayPtr = std::unique_ptr<radicle_npc_gameplay, decltype(&radicle_npc_gameplay_free)>;

class NpcGameplayCApi : public ::testing::Test {
protected:
  void SetUp() override {
    root_ = std::filesystem::temp_directory_path() /
            ("radicle_npc_gameplay_" +
             std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::create_directories(root_);
  }

  void TearDown() override {
    env_.reset();
    std::error_code error;
    std::filesystem::remove_all(root_, error);
  }

  void load(std::vector<Rec> npcs) {
    const auto path = (root_ / "Base.esm").string();
    const auto bytes = build({}, 0x1, records_with(std::move(npcs)));
    std::ofstream stream(path, std::ios::binary);
    stream.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    stream.close();
    const char* paths[] = {path.c_str()};
    radicle_env* env = nullptr;
    ASSERT_EQ(radicle_env_load(paths, 1, &env), RADICLE_STATUS_OK);
    env_.reset(env);
  }

  const radicle_record* winner(std::uint32_t local) {
    const radicle_record* record = nullptr;
    EXPECT_EQ(radicle_env_winner(env_.get(), radicle_form_key{"Base.esm", local}, &record), RADICLE_STATUS_OK);
    return record;
  }

  std::filesystem::path root_;
  EnvPtr env_{nullptr, radicle_env_destroy};
};

}  // namespace

TEST(NpcGameplayDecode, ReadsStoredFactionMembershipAndRawLevel) {
  const auto env = env_of(npc_record(0, 12, 0));
  const auto gameplay = fo4::forms::decode_npc_gameplay(env, ref_of(env, k_npc));
  ASSERT_EQ(gameplay.factions.size(), 1u);
  EXPECT_EQ(gameplay.factions[0].faction, key(k_faction_a));
  EXPECT_EQ(gameplay.factions[0].rank, 2);
  EXPECT_EQ(gameplay.level_raw, 12u);
  EXPECT_FALSE(gameplay.level_is_multiplier);
}

TEST(NpcGameplayDecode, ReadsStoredCalculatedStats) {
  const auto env = env_of(npc_record(0, 12, 0));
  const auto gameplay = fo4::forms::decode_npc_gameplay(env, ref_of(env, k_npc));
  ASSERT_TRUE(gameplay.calculated.has_value());
  EXPECT_EQ(gameplay.calculated->health, 150);
  EXPECT_EQ(gameplay.calculated->action_points, 40);
}

TEST(NpcGameplayDecode, ReadsEveryDirectLinkAndSubrecord) {
  const auto env = env_of(rich_npc());
  const auto gameplay = decode_local(env, k_npc);
  ASSERT_TRUE(gameplay.race.has_value());
  EXPECT_EQ(*gameplay.race, key(k_race));
  ASSERT_TRUE(gameplay.class_form.has_value());
  EXPECT_EQ(*gameplay.class_form, key(k_class));
  ASSERT_TRUE(gameplay.combat_style.has_value());
  EXPECT_EQ(*gameplay.combat_style, key(k_style));
  ASSERT_TRUE(gameplay.crime_faction.has_value());
  EXPECT_EQ(*gameplay.crime_faction, key(k_crime_faction));
  ASSERT_EQ(gameplay.spells.size(), 1u);
  EXPECT_EQ(gameplay.spells[0], key(k_spell));
  ASSERT_EQ(gameplay.perks.size(), 1u);
  EXPECT_EQ(gameplay.perks[0].perk, key(k_perk));
  EXPECT_EQ(gameplay.perks[0].rank, 3);
  ASSERT_TRUE(gameplay.ai.has_value());
  EXPECT_EQ(gameplay.ai->aggression, 1);
  EXPECT_EQ(gameplay.ai->confidence, 2);
  EXPECT_EQ(gameplay.ai->energy_level, 3);
  EXPECT_EQ(gameplay.ai->morality, 4);
  EXPECT_EQ(gameplay.ai->mood, 5);
  EXPECT_EQ(gameplay.ai->assistance, 6);
  EXPECT_FALSE(gameplay.template_base.has_value());
}

TEST(NpcGameplayDecode, TemplateChainFollowsStatsTemplatesAndReportsMissingOnes) {
  const auto env = env_with({template_a(), template_b(), child_of_b(), child_of_missing()});

  const auto chained = decode_local(env, k_npc_child);
  ASSERT_EQ(chained.template_chain.size(), 2u);
  EXPECT_EQ(chained.template_chain[0], key(k_template_b));
  EXPECT_EQ(chained.template_chain[1], key(k_template_a));
  ASSERT_TRUE(chained.template_base.has_value());
  EXPECT_EQ(*chained.template_base, key(k_template_b));
  EXPECT_EQ(chained.template_flags, 0x2u);
  EXPECT_TRUE(chained.stubs.empty());

  const auto broken = decode_local(env, k_npc_missing);
  ASSERT_EQ(broken.template_chain.size(), 1u);
  EXPECT_EQ(broken.template_chain[0], key(k_missing_template));
  ASSERT_EQ(broken.stubs.size(), 1u);
  EXPECT_NE(broken.stubs[0].find("not found"), std::string::npos);
}

TEST(NpcGameplayDecode, UndecodableTemplateIsReportedAsUndecodableNotMissing) {
  const auto env = env_with({undecodable_rec("NPC_", k_template_undecodable),
                             npc_with(k_npc_child, 0, 9, 0x2, {{"TPTA", tpta(k_template_undecodable)}})});

  const auto decoded = decode_local(env, k_npc_child);
  ASSERT_EQ(decoded.template_chain.size(), 1u);
  EXPECT_EQ(decoded.template_chain[0], key(k_template_undecodable));
  ASSERT_EQ(decoded.stubs.size(), 1u);
  EXPECT_NE(decoded.stubs[0].find("is undecodable"), std::string::npos);
  EXPECT_EQ(decoded.stubs[0].find("not found"), std::string::npos);
}

TEST(SpawnLevel, FixedLevelReturnsTheStoredLevel) {
  const auto env = env_of(npc_record(0, 12, 0));
  const auto level = fo4::forms::actor_level_for_spawn(env, ref_of(env, k_npc), 50, 0, std::nullopt);
  EXPECT_EQ(level.level, 12u);
  EXPECT_EQ(level.reason, LevelReason::fixed);
}

TEST(SpawnLevel, MultiplierFlagScalesThePlayerLevelByPerMilleAndTruncates) {
  const auto env = env_of(npc_record(0x80, 1000, 0));
  for (const std::uint32_t player_level : {1u, 30u, 50u}) {
    const auto level = fo4::forms::actor_level_for_spawn(env, ref_of(env, k_npc), player_level, 0, std::nullopt);
    EXPECT_EQ(level.level, player_level) << "player level " << player_level;
    EXPECT_EQ(level.raw_level, 1000u);
    EXPECT_EQ(level.reason, LevelReason::fixed);
  }
  const auto half = env_of(npc_record(0x80, 500, 0));
  EXPECT_EQ(fo4::forms::actor_level_for_spawn(half, ref_of(half, k_npc), 25, 0, std::nullopt).level, 12u);
  const auto triple = env_of(npc_record(0x80, 12000, 0));
  EXPECT_EQ(fo4::forms::actor_level_for_spawn(triple, ref_of(triple, k_npc), 50, 0, std::nullopt).level, 600u);
}

TEST(SpawnLevel, CalcMinFloorsAndCalcMaxCapsOnlyUnderTheMultiplier) {
  const auto bounded = env_of(npc_with(k_npc, 0x80, 1000, 0, {}, 10, 20));
  const auto level_at = [&](std::uint32_t player_level) {
    return fo4::forms::actor_level_for_spawn(bounded, ref_of(bounded, k_npc), player_level, 0, std::nullopt).level;
  };
  EXPECT_EQ(level_at(3), 10u);
  EXPECT_EQ(level_at(15), 15u);
  EXPECT_EQ(level_at(50), 20u);

  const auto unbounded = env_of(npc_with(k_npc, 0x80, 1000, 0, {}, 0, 0));
  EXPECT_EQ(fo4::forms::actor_level_for_spawn(unbounded, ref_of(unbounded, k_npc), 50, 0, std::nullopt).level, 50u);

  const auto fixed = env_of(npc_with(k_npc, 0, 12, 0, {}, 20, 5));
  EXPECT_EQ(fo4::forms::actor_level_for_spawn(fixed, ref_of(fixed, k_npc), 50, 0, std::nullopt).level, 12u);
}

TEST(SpawnLevel, TemplateStatsAndModifierLeaveLevelUnproven) {
  const auto env = env_of(npc_record(0, 12, 0x2));
  const auto templated = fo4::forms::actor_level_for_spawn(env, ref_of(env, k_npc), 50, 0, std::nullopt);
  EXPECT_EQ(templated.level, 0u);
  EXPECT_EQ(templated.reason, LevelReason::template_stats_unproven);

  const auto plain = env_of(npc_record(0, 12, 0));
  const auto modified = fo4::forms::actor_level_for_spawn(plain, ref_of(plain, k_npc), 50, 3, std::nullopt);
  EXPECT_EQ(modified.level, 0u);
  EXPECT_EQ(modified.reason, LevelReason::xlcm_unproven);
}

TEST(SpawnLevel, EncounterZoneLevelIsFloorPlusSixTenthsOfTheExcess) {
  const auto env = env_of(npc_record(0, 12, 0));
  const auto zone_level = [&](std::uint32_t zone, std::uint32_t player_level) {
    const auto level = fo4::forms::actor_level_for_spawn(env, ref_of(env, k_npc), player_level, 0,
                                                         std::optional(key(zone)));
    EXPECT_EQ(level.level, 12u);
    EXPECT_EQ(level.reason, LevelReason::fixed);
    return level.zone_level;
  };
  EXPECT_EQ(zone_level(k_zone_plain, 30), 22u);
  EXPECT_EQ(zone_level(k_zone_plain, 15), 13u);
  EXPECT_EQ(zone_level(k_zone_plain, 10), 10u);
  EXPECT_EQ(zone_level(k_zone_plain, 5), 10u);
  EXPECT_EQ(zone_level(k_zone_match_pc, 5), 5u);
  EXPECT_EQ(zone_level(k_zone_match_pc, 30), 22u);
  EXPECT_EQ(zone_level(k_zone_capped, 30), 20u);
  EXPECT_EQ(zone_level(k_zone_capped, 12), 11u);

  const auto none = fo4::forms::actor_level_for_spawn(env, ref_of(env, k_npc), 30, 0, std::nullopt);
  EXPECT_EQ(none.zone_level, 0u);
}

TEST(SpawnLevel, EncounterZoneMustBeALiveEcznWithItsData) {
  const auto env = env_of(npc_record(0, 12, 0));
  EXPECT_THROW(fo4::forms::actor_level_for_spawn(env, ref_of(env, k_npc), 30, 0, std::optional(key(k_faction_d))),
               fo4::forms::GameplayError);
  EXPECT_THROW(fo4::forms::actor_level_for_spawn(env, ref_of(env, k_npc), 30, 0, std::optional(key(k_zone_short))),
               fo4::forms::GameplayError);
}

TEST(FactionReaction, SingleRelationReturnsItsReactionAndDecider) {
  const auto env = env_of(npc_record(0, 12, 0));
  const std::vector<fo4::FormKey> actors{key(k_faction_a)};
  const std::vector<fo4::FormKey> targets{key(k_faction_b)};
  const auto result = fo4::forms::faction_reaction(env, actors, targets);
  EXPECT_EQ(result.reaction, CombatReaction::enemy);
  EXPECT_EQ(result.reason, ReactionReason::relation);
  ASSERT_TRUE(result.decider_actor_faction.has_value());
  EXPECT_EQ(*result.decider_actor_faction, key(k_faction_a));
  EXPECT_EQ(*result.decider_target_faction, key(k_faction_b));
}

TEST(FactionReaction, NoRelationIsNeutralNotAnError) {
  const auto env = env_of(npc_record(0, 12, 0));
  const std::vector<fo4::FormKey> actors{key(k_faction_a)};
  const std::vector<fo4::FormKey> targets{key(k_faction_unrelated)};
  const auto result = fo4::forms::faction_reaction(env, actors, targets);
  EXPECT_EQ(result.reaction, CombatReaction::neutral);
  EXPECT_EQ(result.reason, ReactionReason::no_relation);
  EXPECT_FALSE(result.decider_actor_faction.has_value());
}

TEST(FactionReaction, CrossPairsKeepTheMinimumModifierRatherThanFlaggingAConflict) {
  const auto env = env_of(npc_record(0, 12, 0));
  const std::vector<fo4::FormKey> actors{key(k_faction_c)};
  const std::vector<fo4::FormKey> targets{key(k_faction_d), key(k_faction_e)};
  const auto result = fo4::forms::faction_reaction(env, actors, targets);
  EXPECT_EQ(result.reaction, CombatReaction::enemy);
  EXPECT_EQ(result.reason, ReactionReason::relation);
  ASSERT_TRUE(result.decider_target_faction.has_value());
  EXPECT_EQ(*result.decider_target_faction, key(k_faction_e));

  const std::vector<fo4::FormKey> reversed{key(k_faction_e), key(k_faction_d)};
  EXPECT_EQ(fo4::forms::faction_reaction(env, actors, reversed).reaction, CombatReaction::enemy);
}

TEST(FactionReaction, IdenticalFactionUsesItsSelfReactionOverAnyCrossPair) {
  const auto env = env_of(npc_record(0, 12, 0));
  const std::vector<fo4::FormKey> actors{key(k_faction_self)};
  const std::vector<fo4::FormKey> shared{key(k_faction_b), key(k_faction_self)};
  const auto result = fo4::forms::faction_reaction(env, actors, shared);
  EXPECT_EQ(result.reaction, CombatReaction::ally);
  EXPECT_EQ(result.reason, ReactionReason::relation);
  ASSERT_TRUE(result.decider_actor_faction.has_value());
  EXPECT_EQ(*result.decider_actor_faction, key(k_faction_self));
  EXPECT_EQ(*result.decider_target_faction, key(k_faction_self));

  const std::vector<fo4::FormKey> cross_only{key(k_faction_b)};
  EXPECT_EQ(fo4::forms::faction_reaction(env, actors, cross_only).reaction, CombatReaction::enemy);
}

TEST(FactionReaction, SharedFactionWithoutASelfEntryIsNeutralAndStillDominates) {
  const auto env = env_of(npc_record(0, 12, 0));
  const std::vector<fo4::FormKey> actors{key(k_faction_a)};
  const std::vector<fo4::FormKey> targets{key(k_faction_b), key(k_faction_a)};
  const auto result = fo4::forms::faction_reaction(env, actors, targets);
  EXPECT_EQ(result.reaction, CombatReaction::neutral);
  EXPECT_EQ(result.reason, ReactionReason::relation);
  ASSERT_TRUE(result.decider_actor_faction.has_value());
  EXPECT_EQ(*result.decider_actor_faction, key(k_faction_a));
  EXPECT_EQ(*result.decider_target_faction, key(k_faction_a));
}

TEST(FactionReaction, RelationIsReadOnlyFromTheActorSide) {
  const auto env = env_of(npc_record(0, 12, 0));
  const std::vector<fo4::FormKey> actors{key(k_faction_d)};
  const std::vector<fo4::FormKey> targets{key(k_faction_c)};
  const auto result = fo4::forms::faction_reaction(env, actors, targets);
  EXPECT_EQ(result.reaction, CombatReaction::neutral);
  EXPECT_EQ(result.reason, ReactionReason::no_relation);
}

TEST(CrimeRules, DecodesCrimeValuesAndOptionalTail) {
  const auto env = env_of(npc_record(0, 12, 0));
  const auto rules = fo4::forms::decode_crime_rules(env, ref_of(env, k_crime_faction));
  ASSERT_TRUE(rules.values.has_value());
  EXPECT_EQ(rules.values->arrest, 1);
  EXPECT_EQ(rules.values->attack_on_sight, 1);
  EXPECT_EQ(rules.values->murder, 10);
  EXPECT_EQ(rules.values->unknown, 50);
  ASSERT_TRUE(rules.steal_multiplier.has_value());
  EXPECT_FLOAT_EQ(*rules.steal_multiplier, 0.5f);
  ASSERT_TRUE(rules.escape.has_value());
  EXPECT_EQ(*rules.escape, 60);
  ASSERT_TRUE(rules.werewolf.has_value());
  EXPECT_EQ(*rules.werewolf, 70);
  EXPECT_FALSE(rules.shared_crime_list.has_value());
}

TEST(Ownership, OwnerFactionNeedsTheRequiredRankOrTheOwnerIsTheActor) {
  const auto env = env_of(npc_record(0, 12, 0));
  const std::vector<FactionMembership> member{{key(k_faction_a), 3}};
  const std::vector<FactionMembership> none{};
  EXPECT_TRUE(fo4::forms::ownership_allowed(env, key(k_faction_a), key(k_npc), member, 2));
  EXPECT_FALSE(fo4::forms::ownership_allowed(env, key(k_faction_a), key(k_npc), member, 4));
  EXPECT_FALSE(fo4::forms::ownership_allowed(env, key(k_faction_a), key(k_npc), none, 0));
  EXPECT_TRUE(fo4::forms::ownership_allowed(env, key(k_npc), key(k_npc), none, 99));
}

TEST(Ownership, TheFirstEntryForTheOwnerFactionDecidesSoAReferenceOverrideWinsOverTheBase) {
  const auto env = env_of(npc_record(0, 12, 0));
  const std::vector<FactionMembership> override_first{{key(k_faction_a), 0}, {key(k_faction_a), 3}};
  const std::vector<FactionMembership> base_first{{key(k_faction_a), 3}, {key(k_faction_a), 0}};
  EXPECT_FALSE(fo4::forms::ownership_allowed(env, key(k_faction_a), key(k_npc), override_first, 2));
  EXPECT_TRUE(fo4::forms::ownership_allowed(env, key(k_faction_a), key(k_npc), base_first, 2));
}

TEST(ActorValueBase, HealthReadsTheStoredCalculatedValueAndOtherAvsStayUnproven) {
  const auto env = env_of(npc_record(0, 12, 0));
  const auto health = fo4::forms::actor_value_base(env, ref_of(env, k_npc), key(k_health));
  EXPECT_EQ(health.base, 150);
  EXPECT_EQ(health.source, AvSource::calculated_health);
  EXPECT_EQ(health.evidence, AvEvidence::record_stored);

  const auto strength = fo4::forms::actor_value_base(env, ref_of(env, k_npc), key(k_strength));
  EXPECT_EQ(strength.base, 0);
  EXPECT_EQ(strength.source, AvSource::none);
  EXPECT_EQ(strength.evidence, AvEvidence::unproven);
}

TEST(ActorValueBase, ActionPointsReadsDnamOffsetTwo) {
  const auto env = env_of(npc_record(0, 12, 0));
  const auto points = fo4::forms::actor_value_base(env, ref_of(env, k_npc), key(k_action_points));
  EXPECT_EQ(points.base, 40);
  EXPECT_EQ(points.source, AvSource::calculated_action_points);
  EXPECT_EQ(points.evidence, AvEvidence::record_stored);
}

TEST(ActorValueBase, TemplatedOrLevelMultNpcsAreUnprovenForHealth) {
  const auto templated = env_of(npc_record(0, 12, 0x2));
  const auto stats = fo4::forms::actor_value_base(templated, ref_of(templated, k_npc), key(k_health));
  EXPECT_EQ(stats.base, 0);
  EXPECT_EQ(stats.source, AvSource::calculated_health);
  EXPECT_EQ(stats.evidence, AvEvidence::unproven);

  const auto scaled = env_of(npc_record(0x80, 12000, 0));
  const auto mult = fo4::forms::actor_value_base(scaled, ref_of(scaled, k_npc), key(k_action_points));
  EXPECT_EQ(mult.base, 0);
  EXPECT_EQ(mult.source, AvSource::calculated_action_points);
  EXPECT_EQ(mult.evidence, AvEvidence::unproven);
}

TEST(ActorValueBase, StoredSpecialReadsFromPrpsByEditorId) {
  const auto env = env_of(special_npc());
  const auto expect = [&](std::uint32_t avif, std::int32_t base) {
    const auto value = fo4::forms::actor_value_base(env, ref_of(env, k_npc), key(avif));
    EXPECT_EQ(value.base, base) << std::hex << avif;
    EXPECT_EQ(value.source, AvSource::stored_property) << std::hex << avif;
    EXPECT_EQ(value.evidence, AvEvidence::record_stored) << std::hex << avif;
  };
  expect(k_strength, 7);
  expect(k_perception, 6);
  expect(k_endurance, -2);
  expect(k_charisma, 5);
  expect(k_intelligence, 0);
}

TEST(ActorValueBase, StoredSpecialMissingEntryAndOtherEditorIdsStayNone) {
  const auto env = env_of(special_npc());
  for (const auto avif : {k_agility, k_luck, k_speed_mult}) {
    const auto value = fo4::forms::actor_value_base(env, ref_of(env, k_npc), key(avif));
    EXPECT_EQ(value.base, 0) << std::hex << avif;
    EXPECT_EQ(value.source, AvSource::none) << std::hex << avif;
    EXPECT_EQ(value.evidence, AvEvidence::unproven) << std::hex << avif;
  }
}

TEST(ActorValueBase, StoredSpecialLeavesHealthUnchangedAndTemplatedStatsUnproven) {
  const auto env = env_of(special_npc());
  const auto health = fo4::forms::actor_value_base(env, ref_of(env, k_npc), key(k_health));
  EXPECT_EQ(health.base, 150);
  EXPECT_EQ(health.source, AvSource::calculated_health);
  EXPECT_EQ(health.evidence, AvEvidence::record_stored);

  const auto templated = env_of(special_npc(0x2));
  const auto strength = fo4::forms::actor_value_base(templated, ref_of(templated, k_npc), key(k_strength));
  EXPECT_EQ(strength.base, 0);
  EXPECT_EQ(strength.source, AvSource::stored_property);
  EXPECT_EQ(strength.evidence, AvEvidence::unproven);
  const auto missing = fo4::forms::actor_value_base(templated, ref_of(templated, k_npc), key(k_luck));
  EXPECT_EQ(missing.source, AvSource::none);
}

TEST(ActorValueBase, StoredSpecialWithANonFiniteValueIsUnproven) {
  const auto infinite = env_of(npc_with(k_npc, 0, 12, 0,
                                        {{"PRPS", properties({{k_strength, std::numeric_limits<float>::infinity()}})}}));
  const auto value = fo4::forms::actor_value_base(infinite, ref_of(infinite, k_npc), key(k_strength));
  EXPECT_EQ(value.base, 0);
  EXPECT_EQ(value.source, AvSource::stored_property);
  EXPECT_EQ(value.evidence, AvEvidence::unproven);
}

TEST(CombatProfile, OwnZnamIsReportedAndAbsentIsUnprovenFallback) {
  const auto env = env_of(rich_npc());
  const auto own = fo4::forms::combat_profile(env, ref_of(env, k_npc));
  ASSERT_TRUE(own.style.has_value());
  EXPECT_EQ(*own.style, key(k_style));
  EXPECT_EQ(own.source, CombatStyleSource::own_record);

  const auto plain = env_of(npc_record(0, 12, 0));
  const auto absent = fo4::forms::combat_profile(plain, ref_of(plain, k_npc));
  EXPECT_FALSE(absent.style.has_value());
  EXPECT_EQ(absent.source, CombatStyleSource::absent_fallback_unproven);
}

TEST_F(NpcGameplayCApi, DecodeReportsStoredFieldsAndFreeReleasesThem) {
  load({rich_npc()});
  radicle_npc_gameplay* raw = nullptr;
  ASSERT_EQ(radicle_npc_gameplay_decode(env_.get(), winner(k_npc), &raw), RADICLE_STATUS_OK);
  const GameplayPtr view(raw, radicle_npc_gameplay_free);
  ASSERT_NE(raw, nullptr);
  EXPECT_STREQ(view->npc.plugin, "Base.esm");
  EXPECT_EQ(view->npc.local_id, k_npc);
  EXPECT_EQ(view->level_raw, 12u);
  EXPECT_EQ(view->has_race, 1);
  EXPECT_EQ(view->race.local_id, k_race);
  EXPECT_EQ(view->has_combat_style, 1);
  EXPECT_EQ(view->combat_style.local_id, k_style);
  EXPECT_EQ(view->has_crime_faction, 1);
  EXPECT_EQ(view->has_template, 0);
  ASSERT_EQ(view->faction_count, 1u);
  EXPECT_EQ(view->factions[0].faction.local_id, k_faction_a);
  EXPECT_EQ(view->factions[0].rank, 2);
  ASSERT_EQ(view->perk_count, 1u);
  EXPECT_EQ(view->perks[0].rank, 3u);
  EXPECT_EQ(view->spell_count, 1u);
  EXPECT_EQ(view->has_ai_data, 1);
  EXPECT_EQ(view->ai.aggression, 1);
  EXPECT_EQ(view->has_calculated_stats, 1);
  EXPECT_EQ(view->calculated_health, 150u);
  EXPECT_EQ(view->stub_count, 0u);
}

TEST_F(NpcGameplayCApi, DecodeRejectsARecordThatIsNotAnNpc) {
  load({rich_npc()});
  radicle_npc_gameplay* raw = nullptr;
  EXPECT_EQ(radicle_npc_gameplay_decode(env_.get(), winner(k_faction_a), &raw), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(raw, nullptr);
}

TEST_F(NpcGameplayCApi, SpawnLevelReportsFixedAndScaledReasons) {
  load({rich_npc(), npc_with(k_npc_scaled, 0x80, 12000, 0, {{"DNAM", calculated_stats(150, 40)}}, 0, 400)});
  radicle_actor_level level{};
  ASSERT_EQ(radicle_actor_level_for_spawn(env_.get(), winner(k_npc), 50, 0, nullptr, &level), RADICLE_STATUS_OK);
  EXPECT_EQ(level.level, 12u);
  EXPECT_EQ(level.reason, RADICLE_LEVEL_REASON_FIXED);
  EXPECT_EQ(level.zone_level, 0u);

  ASSERT_EQ(radicle_actor_level_for_spawn(env_.get(), winner(k_npc_scaled), 50, 0, nullptr, &level),
            RADICLE_STATUS_OK);
  EXPECT_EQ(level.level, 400u);
  EXPECT_EQ(level.raw_level, 12000u);
  EXPECT_EQ(level.reason, RADICLE_LEVEL_REASON_FIXED);

  const radicle_form_key zone{"Base.esm", k_zone_plain};
  ASSERT_EQ(radicle_actor_level_for_spawn(env_.get(), winner(k_npc), 50, 0, &zone, &level), RADICLE_STATUS_OK);
  EXPECT_EQ(level.level, 12u);
  EXPECT_EQ(level.zone_level, 34u);
  EXPECT_EQ(level.reason, RADICLE_LEVEL_REASON_FIXED);

  const radicle_form_key not_a_zone{"Base.esm", k_faction_d};
  EXPECT_EQ(radicle_actor_level_for_spawn(env_.get(), winner(k_npc), 50, 0, &not_a_zone, &level),
            RADICLE_STATUS_INVALID_ARGUMENT);
}

TEST_F(NpcGameplayCApi, FactionReactionAndOwnershipReturnTheCoreAnswers) {
  load({rich_npc()});
  const radicle_form_key actor_factions[] = {{"Base.esm", k_faction_a}};
  const radicle_form_key target_factions[] = {{"Base.esm", k_faction_b}};
  radicle_faction_reaction_result reaction{};
  ASSERT_EQ(radicle_faction_reaction(env_.get(), actor_factions, 1, target_factions, 1, &reaction),
            RADICLE_STATUS_OK);
  EXPECT_EQ(reaction.reaction, RADICLE_COMBAT_REACTION_ENEMY);
  EXPECT_EQ(reaction.reason, RADICLE_REACTION_REASON_RELATION);
  EXPECT_EQ(reaction.has_decider, 1);
  EXPECT_EQ(reaction.decider_actor_faction.local_id, k_faction_a);

  const radicle_form_key unrelated[] = {{"Base.esm", k_faction_unrelated}};
  ASSERT_EQ(radicle_faction_reaction(env_.get(), actor_factions, 1, unrelated, 1, &reaction), RADICLE_STATUS_OK);
  EXPECT_EQ(reaction.reaction, RADICLE_COMBAT_REACTION_NEUTRAL);
  EXPECT_EQ(reaction.reason, RADICLE_REACTION_REASON_NO_RELATION);
  EXPECT_EQ(reaction.has_decider, 0);

  const radicle_form_key npc_as_faction[] = {{"Base.esm", k_npc}};
  EXPECT_EQ(radicle_faction_reaction(env_.get(), npc_as_faction, 1, target_factions, 1, &reaction),
            RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_faction_reaction(env_.get(), nullptr, 1, target_factions, 1, &reaction),
            RADICLE_STATUS_INVALID_ARGUMENT);

  const radicle_npc_faction_membership members[] = {{{"Base.esm", k_faction_a}, 3}};
  uint8_t allowed = 0;
  ASSERT_EQ(radicle_ownership_check(env_.get(), {"Base.esm", k_faction_a}, {"Base.esm", k_npc}, members, 1, 2,
                                    &allowed),
            RADICLE_STATUS_OK);
  EXPECT_EQ(allowed, 1);
  ASSERT_EQ(radicle_ownership_check(env_.get(), {"Base.esm", k_faction_a}, {"Base.esm", k_npc}, members, 1, 4,
                                    &allowed),
            RADICLE_STATUS_OK);
  EXPECT_EQ(allowed, 0);
  ASSERT_EQ(radicle_ownership_check(env_.get(), {"Base.esm", k_npc}, {"Base.esm", k_npc}, nullptr, 0, 99, &allowed),
            RADICLE_STATUS_OK);
  EXPECT_EQ(allowed, 1);
}

TEST_F(NpcGameplayCApi, ActorValueCombatStyleAndCrimeRulesReadThroughTheAbi) {
  load({rich_npc(), npc_with(k_npc_scaled, 0x80, 12000, 0, {{"DNAM", calculated_stats(150, 40)}})});

  radicle_actor_value_base_result value{};
  ASSERT_EQ(radicle_actor_value_base(env_.get(), winner(k_npc), {"Base.esm", k_health}, &value), RADICLE_STATUS_OK);
  EXPECT_EQ(value.base, 150);
  EXPECT_EQ(value.source, RADICLE_AV_SOURCE_CALCULATED_HEALTH);
  EXPECT_EQ(value.evidence, RADICLE_AV_EVIDENCE_RECORD_STORED);
  ASSERT_EQ(radicle_actor_value_base(env_.get(), winner(k_npc_scaled), {"Base.esm", k_health}, &value),
            RADICLE_STATUS_OK);
  EXPECT_EQ(value.base, 0);
  EXPECT_EQ(value.evidence, RADICLE_AV_EVIDENCE_UNPROVEN);

  radicle_combat_profile profile{};
  ASSERT_EQ(radicle_combat_profile_decode(env_.get(), winner(k_npc), &profile), RADICLE_STATUS_OK);
  EXPECT_EQ(profile.has_style, 1);
  EXPECT_EQ(profile.style.local_id, k_style);
  EXPECT_EQ(profile.source, RADICLE_COMBAT_STYLE_SOURCE_OWN_RECORD);
  ASSERT_EQ(radicle_combat_profile_decode(env_.get(), winner(k_npc_scaled), &profile), RADICLE_STATUS_OK);
  EXPECT_EQ(profile.has_style, 0);
  EXPECT_EQ(profile.source, RADICLE_COMBAT_STYLE_SOURCE_ABSENT_FALLBACK_UNPROVEN);

  radicle_crime_rules crime{};
  ASSERT_EQ(radicle_crime_rules_decode(env_.get(), winner(k_crime_faction), &crime), RADICLE_STATUS_OK);
  EXPECT_EQ(crime.has_crime_values, 1);
  EXPECT_EQ(crime.arrest, 1);
  EXPECT_EQ(crime.murder, 10);
  EXPECT_EQ(crime.unknown, 50);
  EXPECT_EQ(crime.has_steal_multiplier, 1);
  EXPECT_FLOAT_EQ(crime.steal_multiplier, 0.5f);
  EXPECT_EQ(crime.has_escape, 1);
  EXPECT_EQ(crime.escape, 60);
  EXPECT_EQ(crime.has_werewolf, 1);
  EXPECT_EQ(crime.werewolf, 70);
  EXPECT_EQ(crime.has_shared_crime_list, 0);
}

TEST_F(NpcGameplayCApi, StoredSpecialReadsThroughTheAbi) {
  load({special_npc()});
  radicle_actor_value_base_result value{};
  ASSERT_EQ(radicle_actor_value_base(env_.get(), winner(k_npc), {"Base.esm", k_strength}, &value), RADICLE_STATUS_OK);
  EXPECT_EQ(value.base, 7);
  EXPECT_EQ(value.source, RADICLE_AV_SOURCE_STORED_PROPERTY);
  EXPECT_EQ(static_cast<int>(value.source), 3);
  EXPECT_EQ(value.evidence, RADICLE_AV_EVIDENCE_RECORD_STORED);
  ASSERT_EQ(radicle_actor_value_base(env_.get(), winner(k_npc), {"Base.esm", k_luck}, &value), RADICLE_STATUS_OK);
  EXPECT_EQ(value.source, RADICLE_AV_SOURCE_NONE);
}

TEST_F(NpcGameplayCApi, DeletedFactionAndActorValueAreNotFound) {
  load({rich_npc(), Rec{"FACT", k_faction_deleted, {{"EDID", zstr("Gone")}}, k_deleted_flag},
        Rec{"AVIF", k_avif_deleted, {{"EDID", zstr("Gone")}}, k_deleted_flag}});
  const radicle_form_key actor_factions[] = {{"Base.esm", k_faction_a}};
  const radicle_form_key deleted_target[] = {{"Base.esm", k_faction_deleted}};
  radicle_faction_reaction_result reaction{};
  EXPECT_EQ(radicle_faction_reaction(env_.get(), actor_factions, 1, deleted_target, 1, &reaction),
            RADICLE_STATUS_NOT_FOUND);

  radicle_actor_value_base_result value{};
  EXPECT_EQ(radicle_actor_value_base(env_.get(), winner(k_npc), {"Base.esm", k_avif_deleted}, &value),
            RADICLE_STATUS_NOT_FOUND);
}

TEST_F(NpcGameplayCApi, UndecodableRecordsAreParseErrors) {
  load({rich_npc(), undecodable_rec("FACT", k_faction_undecodable), undecodable_rec("AVIF", k_avif_undecodable),
        undecodable_rec("NPC_", k_npc_undecodable)});
  const radicle_form_key good[] = {{"Base.esm", k_faction_a}};
  const radicle_form_key bad[] = {{"Base.esm", k_faction_undecodable}};
  radicle_faction_reaction_result reaction{};
  EXPECT_EQ(radicle_faction_reaction(env_.get(), good, 1, bad, 1, &reaction), RADICLE_STATUS_PARSE_ERROR);
  EXPECT_EQ(radicle_faction_reaction(env_.get(), bad, 1, good, 1, &reaction), RADICLE_STATUS_PARSE_ERROR);
  EXPECT_EQ(radicle_faction_reaction(env_.get(), nullptr, 0, bad, 1, &reaction), RADICLE_STATUS_PARSE_ERROR);

  radicle_actor_value_base_result value{};
  EXPECT_EQ(radicle_actor_value_base(env_.get(), winner(k_npc), {"Base.esm", k_avif_undecodable}, &value),
            RADICLE_STATUS_PARSE_ERROR);
  EXPECT_EQ(radicle_actor_value_base(env_.get(), winner(k_npc_undecodable), {"Base.esm", k_health}, &value),
            RADICLE_STATUS_PARSE_ERROR);

  radicle_combat_profile profile{};
  EXPECT_EQ(radicle_combat_profile_decode(env_.get(), winner(k_npc_undecodable), &profile),
            RADICLE_STATUS_PARSE_ERROR);

  radicle_crime_rules crime{};
  EXPECT_EQ(radicle_crime_rules_decode(env_.get(), winner(k_faction_undecodable), &crime),
            RADICLE_STATUS_PARSE_ERROR);

  radicle_npc_gameplay* gameplay = nullptr;
  EXPECT_EQ(radicle_npc_gameplay_decode(env_.get(), winner(k_npc_undecodable), &gameplay),
            RADICLE_STATUS_PARSE_ERROR);
  EXPECT_EQ(gameplay, nullptr);
  radicle_actor_level level{};
  EXPECT_EQ(radicle_actor_level_for_spawn(env_.get(), winner(k_npc_undecodable), 10, 0, nullptr, &level),
            RADICLE_STATUS_PARSE_ERROR);
}
