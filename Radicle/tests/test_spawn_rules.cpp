#include "fo4/forms/npc_visual.hpp"
#include "fo4/forms/spawn_rules.hpp"
#include "../src/forms/npc_visual_detail.hpp"

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <set>
#include <utility>
#include <vector>

#include "npc_visual_fixture.hpp"

using namespace npc_visual_fixture;
using fo4::forms::EnableLink;
using fo4::forms::EnableStop;
using fo4::forms::EnableVerdict;
using fo4::forms::LeveledPickReason;

namespace {

using Links = std::map<fo4::FormKey, EnableLink>;

fo4::forms::EnableLinkLookup lookup_in(const Links& links) {
  return [&links](const fo4::FormKey& key) -> std::optional<EnableLink> {
    const auto found = links.find(key);
    if (found == links.end()) return std::nullopt;
    return found->second;
  };
}

fo4::forms::EnableOverrideLookup overrides_of(std::map<fo4::FormKey, bool> values) {
  return [values = std::move(values)](const fo4::FormKey& key) -> std::optional<bool> {
    const auto found = values.find(key);
    if (found == values.end()) return std::nullopt;
    return found->second;
  };
}

EnableLink root(bool initially_disabled) {
  EnableLink link;
  link.initially_disabled = initially_disabled;
  return link;
}

EnableLink child_of(std::uint32_t parent, bool opposite, bool initially_disabled = false) {
  EnableLink link;
  link.initially_disabled = initially_disabled;
  link.parent = key(parent);
  link.opposite = opposite;
  return link;
}

EnableVerdict evaluate(std::uint32_t ref, const Links& links,
                       const fo4::forms::EnableOverrideLookup& overrides = {}) {
  return fo4::forms::evaluate_enable_state(key(ref), links.at(key(ref)), lookup_in(links), overrides);
}

Bytes xesp(std::uint32_t parent, std::uint8_t flags) {
  fo4::test::ByteWriter out;
  out.u32(parent);
  out.u8(flags);
  out.u8(0);
  out.u8(0);
  out.u8(0);
  return out.bytes;
}

fo4::env::Environment spawn_env() {
  auto records = base_records();
  records.push_back(Rec{"ARMO", 0x910, {{"EDID", zstr("Armor")}, {"BOD2", words({0x8})}}});
  records.push_back(Rec{"REFR", 0x930,
                        {{"NAME", words({0x862})},
                         {"XCNT", words({3})},
                         {"XLCM", words({2})},
                         {"XEZN", words({0x940})},
                         {"XESP", xesp(0x931, 0x01)}}});
  records.push_back(Rec{"REFR", 0x931, {{"NAME", words({0x870})}}});
  records.push_back(Rec{"REFR", 0x932, {{"NAME", words({0x87F})}}});
  records.push_back(npc({.id = 0x872}));
  records.push_back(leveled("LVLN", 0x87F, 0x01, 0, {{1, 0x872, 0}}));
  fo4::test::ByteWriter zone;
  zone.u32(0);
  zone.u32(0);
  zone.u8(0);
  zone.u8(7);
  zone.u8(0);
  zone.u8(50);
  records.push_back(Rec{"ECZN", 0x940, {{"EDID", zstr("Zone")}, {"DATA", zone.bytes}}});
  return make_env({{"Base.esm", build({}, 0x1, records)}});
}

}  // namespace

TEST(EnableState, RootUsesItsOwnInitialFlag) {
  const Links links{{key(1), root(false)}, {key(2), root(true)}};
  const auto on = evaluate(1, links);
  EXPECT_TRUE(on.enabled);
  EXPECT_EQ(on.stop, EnableStop::root);
  EXPECT_EQ(on.chain_length, 0u);
  EXPECT_FALSE(evaluate(2, links).enabled);
}

TEST(EnableState, ChildFollowsItsParent) {
  const Links links{{key(1), root(true)}, {key(2), child_of(1, false)}};
  const auto verdict = evaluate(2, links);
  EXPECT_FALSE(verdict.enabled);
  EXPECT_EQ(verdict.deciding, key(1));
  EXPECT_EQ(verdict.chain_length, 1u);
  EXPECT_EQ(verdict.stop, EnableStop::root);
}

TEST(EnableState, OppositeFlagInvertsTheParentState) {
  const Links links{{key(1), root(true)}, {key(2), child_of(1, true)}};
  EXPECT_TRUE(evaluate(2, links).enabled);
}

TEST(EnableState, ChildIgnoresItsOwnInitialFlagWhenTheParentResolves) {
  const Links links{{key(1), root(false)}, {key(2), child_of(1, false, true)}};
  EXPECT_TRUE(evaluate(2, links).enabled);
}

TEST(EnableState, CycleStopsAndUsesTheCurrentLinkFlag) {
  const Links links{{key(1), child_of(2, false, false)}, {key(2), child_of(1, true, true)}};
  const auto verdict = evaluate(1, links);
  EXPECT_EQ(verdict.stop, EnableStop::cycle);
  EXPECT_FALSE(verdict.enabled);
  EXPECT_EQ(verdict.deciding, key(2));
}

TEST(EnableState, MissingParentUsesTheCurrentLinkFlag) {
  const Links links{{key(2), child_of(9, true, true)}};
  const auto verdict = evaluate(2, links);
  EXPECT_EQ(verdict.stop, EnableStop::missing_parent);
  EXPECT_FALSE(verdict.enabled);
  EXPECT_EQ(verdict.deciding, key(2));
  EXPECT_EQ(verdict.chain_length, 0u);
}

TEST(EnableState, OverrideOnTheRefWins) {
  const Links links{{key(1), root(false)}, {key(2), child_of(1, false)}};
  const auto verdict = evaluate(2, links, overrides_of({{key(2), false}}));
  EXPECT_FALSE(verdict.enabled);
  EXPECT_EQ(verdict.stop, EnableStop::overridden);
  EXPECT_EQ(verdict.chain_length, 0u);
}

TEST(EnableState, OverrideOnTheParentFlipsTheChild) {
  const Links links{{key(1), root(true)}, {key(2), child_of(1, false)}};
  EXPECT_FALSE(evaluate(2, links).enabled);
  const auto enabled_parent = evaluate(2, links, overrides_of({{key(1), true}}));
  EXPECT_TRUE(enabled_parent.enabled);
  EXPECT_EQ(enabled_parent.stop, EnableStop::overridden);
  EXPECT_EQ(enabled_parent.deciding, key(1));
  const Links inverted{{key(1), root(true)}, {key(2), child_of(1, true)}};
  EXPECT_FALSE(evaluate(2, inverted, overrides_of({{key(1), true}})).enabled);
}

TEST(EnableState, DepthLimitStopsAtSixteenLinks) {
  Links links;
  for (std::uint32_t index = 0; index < 20; ++index) {
    links.emplace(key(100 + index), child_of(101 + index, false));
  }
  links.emplace(key(120), root(false));
  const auto verdict = evaluate(100, links);
  EXPECT_EQ(verdict.stop, EnableStop::depth_limit);
  EXPECT_EQ(verdict.chain_length, fo4::forms::k_enable_max_depth);
  EXPECT_EQ(verdict.deciding, key(static_cast<std::uint32_t>(100 + fo4::forms::k_enable_max_depth)));
}

TEST(LeveledPickReasons, PickedReportsTheEntryAndDepth) {
  const auto env = chain_env(1);
  const auto pick = fo4::forms::pick_leveled(env, chain_key(0x900), 1, 7);
  ASSERT_EQ(pick.entries.size(), 1u);
  EXPECT_EQ(pick.entries.front(), chain_key(0x910));
  EXPECT_EQ(pick.reason, LeveledPickReason::picked);
  EXPECT_EQ(pick.depth_used, 0u);
}

TEST(LeveledPickReasons, DeepestNestIsReported) {
  const auto env = chain_env(8);
  const auto pick = fo4::forms::pick_leveled(env, chain_key(0x900), 1, 7);
  EXPECT_EQ(pick.reason, LeveledPickReason::picked);
  EXPECT_EQ(pick.depth_used, 7u);
}

TEST(LeveledPickReasons, DepthLimitOutranksTheRest) {
  const auto env = chain_env(9);
  const auto pick = fo4::forms::pick_leveled(env, chain_key(0x900), 1, 7);
  EXPECT_TRUE(pick.entries.empty());
  EXPECT_EQ(pick.reason, LeveledPickReason::depth_exceeded);
  EXPECT_EQ(pick.depth_used, 8u);
}

TEST(LeveledPickReasons, MissingAndNonLeveledListsAreNamed) {
  const auto env = chain_env(1);
  EXPECT_EQ(fo4::forms::pick_leveled(env, chain_key(0x999), 1, 7).reason, LeveledPickReason::list_missing);
  EXPECT_EQ(fo4::forms::pick_leveled(env, chain_key(0x910), 1, 7).reason,
            LeveledPickReason::not_leveled_list);
}

TEST(LeveledPickReasons, ChanceNoneAndTierMissesAreNamed) {
  const auto env = make_env({{"Chain.esm",
                              build({}, 0x1,
                                    {leveled("LVLI", 0x920, 0x01, 100, {{1, 0x910, 0}}),
                                     leveled("LVLI", 0x921, 0x01, 0, {{5, 0x910, 0}}),
                                     leveled("LVLI", 0x922, 0x01, 0, {{1, 0x910, 100}}),
                                     Rec{"ARMO", 0x910, {{"EDID", zstr("Armor")}, {"BOD2", words({0x8})}}}})}});
  const auto chance = fo4::forms::pick_leveled(env, chain_key(0x920), 1, 7);
  EXPECT_TRUE(chance.entries.empty());
  EXPECT_EQ(chance.reason, LeveledPickReason::chance_none);
  EXPECT_EQ(fo4::forms::pick_leveled(env, chain_key(0x921), 1, 7).reason,
            LeveledPickReason::no_entry_at_level);
  EXPECT_EQ(fo4::forms::pick_leveled(env, chain_key(0x922), 1, 7).reason,
            LeveledPickReason::entries_rolled_out);
}

TEST(PlacedActorSpawn, DecodesCountLevelModifierAndAChainVerdict) {
  const auto env = spawn_env();
  const auto spawn = fo4::forms::decode_placed_actor_spawn(env, *env.winner(key(0x930)), 1, 7);
  EXPECT_EQ(spawn.base, key(0x862));
  EXPECT_EQ(spawn.base_kind, fo4::forms::SpawnBaseKind::npc);
  EXPECT_EQ(spawn.resolved_base, key(0x862));
  EXPECT_EQ(spawn.count, std::optional<std::int32_t>(3));
  EXPECT_EQ(spawn.level_modifier, std::optional<std::int32_t>(2));
  EXPECT_FALSE(spawn.enable.enabled);
  EXPECT_EQ(spawn.enable.deciding, key(0x931));
  EXPECT_EQ(spawn.enable.chain_length, 1u);
}

TEST(PlacedActorSpawn, OverrideOnTheParentIsAppliedBeforeTheOpposite) {
  const auto env = spawn_env();
  const auto spawn = fo4::forms::decode_placed_actor_spawn(env, *env.winner(key(0x930)), 1, 7,
                                                           overrides_of({{key(0x931), false}}));
  EXPECT_TRUE(spawn.enable.enabled);
  EXPECT_EQ(spawn.enable.stop, EnableStop::overridden);
}

TEST(PlacedActorSpawn, LeveledBaseIsNamedAndNotResolved) {
  const auto env = spawn_env();
  const auto spawn = fo4::forms::decode_placed_actor_spawn(env, *env.winner(key(0x931)), 1, 7);
  EXPECT_EQ(spawn.base_kind, fo4::forms::SpawnBaseKind::leveled_npc);
  EXPECT_FALSE(spawn.count.has_value());
  EXPECT_FALSE(spawn.level_modifier.has_value());
  EXPECT_TRUE(spawn.enable.enabled);
}

TEST(PlacedActorSpawn, ZoneMinLevelAndLeveledBaseResolveAtThePlayerLevel) {
  const auto env = spawn_env();
  const auto spawn = fo4::forms::decode_placed_actor_spawn(env, *env.winner(key(0x930)), 1, 7);
  EXPECT_EQ(spawn.encounter_zone, std::optional<fo4::FormKey>(key(0x940)));
  EXPECT_EQ(spawn.encounter_zone_min_level, std::optional<std::int32_t>(7));

  const auto leveled_ref = fo4::forms::decode_placed_actor_spawn(env, *env.winner(key(0x932)), 1, 7);
  EXPECT_EQ(leveled_ref.base_kind, fo4::forms::SpawnBaseKind::leveled_npc);
  EXPECT_EQ(leveled_ref.resolved_base, key(0x872));
  EXPECT_FALSE(leveled_ref.encounter_zone_min_level.has_value());
}

TEST(LeveledPickReasons, BudgetExceededOutranksTheRest) {
  // Constraint: 1093 use-all LVLI nodes in a 3-ary tree exceed the 1024-node budget.
  std::vector<Rec> records;
  for (std::uint32_t index = 0; index < 1093; ++index) {
    const std::uint32_t id = 0xA00 + index;
    if (index < 364) {
      const std::uint32_t first = 0xA00 + 3 * index + 1;
      records.push_back(leveled("LVLI", id, 0x04, 0, {{1, first, 0}, {1, first + 1, 0}, {1, first + 2, 0}}));
    } else {
      records.push_back(leveled("LVLI", id, 0x04, 0, {{1, 0x910, 0}}));
    }
  }
  records.push_back(Rec{"ARMO", 0x910, {{"EDID", zstr("Armor")}, {"BOD2", words({0x8})}}});
  const auto env = make_env({{"Tree.esm", build({}, 0x1, records)}});
  const auto tree_root = fo4::FormKey{fo4::PluginKey::from_filename("Tree.esm"), fo4::LocalFormId{0xA00}};
  EXPECT_EQ(fo4::forms::pick_leveled(env, tree_root, 1, 7).reason, LeveledPickReason::budget_exceeded);
}

TEST(LeveledPickReasons, ForcedUseAllKeepsEveryTier) {
  const auto force_key = [](std::uint32_t local) {
    return fo4::FormKey{fo4::PluginKey::from_filename("Force.esm"), fo4::LocalFormId{local}};
  };
  const auto env = make_env({{"Force.esm",
                              build({}, 0x1,
                                    {leveled("LVLI", 0xA80, 0x00, 0, {{1, 0x910, 0}, {5, 0x911, 0}}),
                                     Rec{"ARMO", 0x910, {{"EDID", zstr("Armor")}, {"BOD2", words({0x8})}}},
                                     Rec{"ARMO", 0x911, {{"EDID", zstr("Armor")}, {"BOD2", words({0x8})}}}})}});
  const auto tiered = fo4::forms::pick_leveled(env, force_key(0xA80), 5, 7);
  ASSERT_EQ(tiered.entries.size(), 1u);
  EXPECT_EQ(tiered.entries.front(), force_key(0x911));
  const auto forced = fo4::forms::pick_leveled(env, force_key(0xA80), 5, 7, nullptr, 0x04);
  EXPECT_EQ(forced.entries.size(), 2u);
  EXPECT_EQ(forced.reason, LeveledPickReason::picked);
}

namespace {

constexpr std::uint32_t k_each = 0xB00;
constexpr std::uint32_t k_once = 0xB01;
constexpr std::uint32_t k_each_chance = 0xB02;

fo4::FormKey count_key(std::uint32_t local) {
  return fo4::FormKey{fo4::PluginKey::from_filename("Count.esm"), fo4::LocalFormId{local}};
}

fo4::env::Environment count_env() {
  return make_env({{"Count.esm",
                    build({}, 0x1,
                          {leveled("LVLI", k_each, 0x03, 0, {{1, 0x910, 0}, {1, 0x911, 0}}),
                           leveled("LVLI", k_once, 0x01, 0, {{1, 0x910, 0}, {1, 0x911, 0}}),
                           leveled("LVLI", k_each_chance, 0x03, 50, {{1, 0x910, 0}}),
                           Rec{"ARMO", 0x910, {{"EDID", zstr("Armor")}, {"BOD2", words({0x8})}}},
                           Rec{"ARMO", 0x911, {{"EDID", zstr("Armor")}, {"BOD2", words({0x8})}}}})}});
}

}  // namespace

TEST(LeveledPickCount, CalculateForEachDrawsCountIndependentRolls) {
  const auto env = count_env();
  const auto pick = fo4::forms::pick_leveled_counted(env, count_key(k_each), 1, 7, 64);
  EXPECT_EQ(pick.entries.size(), 64u);
  EXPECT_EQ(pick.rolls, 64u);
  EXPECT_EQ(pick.multiplier, 1u);
  EXPECT_EQ(pick.reason, LeveledPickReason::picked);
  const std::set<fo4::FormKey> distinct(pick.entries.begin(), pick.entries.end());
  EXPECT_EQ(distinct.size(), 2u) << "64 rolls repeated one draw";
}

TEST(LeveledPickCount, ClearFlagDrawsOnceAndCarriesTheCountAsMultiplier) {
  const auto env = count_env();
  const auto pick = fo4::forms::pick_leveled_counted(env, count_key(k_once), 1, 7, 64);
  EXPECT_EQ(pick.entries.size(), 1u);
  EXPECT_EQ(pick.rolls, 1u);
  EXPECT_EQ(pick.multiplier, 64u);
}

TEST(LeveledPickCount, ForcedCalculateForEachAppliesToAClearList) {
  const auto env = count_env();
  const auto pick = fo4::forms::pick_leveled_counted(env, count_key(k_once), 1, 7, 64, nullptr, 0x02);
  EXPECT_EQ(pick.entries.size(), 64u);
  EXPECT_EQ(pick.rolls, 64u);
  EXPECT_EQ(pick.multiplier, 1u);
}

TEST(LeveledPickCount, CountOneEqualsThePlainPickForEverySeed) {
  const auto env = count_env();
  for (const auto list : {k_each, k_once}) {
    for (std::uint64_t seed = 0; seed < 16; ++seed) {
      const auto plain = fo4::forms::pick_leveled(env, count_key(list), 1, seed);
      const auto counted = fo4::forms::pick_leveled_counted(env, count_key(list), 1, seed, 1);
      EXPECT_EQ(counted.entries, plain.entries) << "list " << list << " seed " << seed;
      EXPECT_EQ(counted.reason, plain.reason);
      EXPECT_EQ(counted.rolls, 1u);
      EXPECT_EQ(counted.multiplier, 1u);
    }
  }
}

TEST(LeveledPickCount, SameSeedRepeatsAndAnotherSeedChangesTheSequence) {
  const auto env = count_env();
  const auto first = fo4::forms::pick_leveled_counted(env, count_key(k_each), 1, 7, 64);
  const auto again = fo4::forms::pick_leveled_counted(env, count_key(k_each), 1, 7, 64);
  const auto other = fo4::forms::pick_leveled_counted(env, count_key(k_each), 1, 8, 64);
  EXPECT_EQ(first.entries, again.entries);
  EXPECT_NE(first.entries, other.entries);
}

TEST(LeveledPickCount, EveryRollRollsTheListChanceNoneApart) {
  const auto env = count_env();
  const auto pick = fo4::forms::pick_leveled_counted(env, count_key(k_each_chance), 1, 7, 64);
  EXPECT_EQ(pick.rolls, 64u);
  EXPECT_GT(pick.entries.size(), 0u);
  EXPECT_LT(pick.entries.size(), 64u);
  EXPECT_EQ(pick.reason, LeveledPickReason::picked);
}

TEST(LeveledPickCount, MissingListReportsItsReasonWithOneRoll) {
  const auto env = count_env();
  const auto pick = fo4::forms::pick_leveled_counted(env, count_key(0xB99), 1, 7, 5);
  EXPECT_TRUE(pick.entries.empty());
  EXPECT_EQ(pick.reason, LeveledPickReason::list_missing);
  EXPECT_EQ(pick.rolls, 1u);
  EXPECT_EQ(pick.multiplier, 5u);
}

TEST(LeveledPickCount, ForcedCalculateForEachOnAnUnresolvedListStillReportsMultiplierOne) {
  const auto env = count_env();
  const auto pick = fo4::forms::pick_leveled_counted(env, count_key(0xB99), 1, 7, 5, nullptr, 0x02);
  EXPECT_TRUE(pick.entries.empty());
  EXPECT_EQ(pick.reason, LeveledPickReason::list_missing);
  EXPECT_EQ(pick.rolls, 5u);
  EXPECT_EQ(pick.multiplier, 1u);
}

TEST(LeveledPickCount, RollSeedsAreDistinctAcrossSeedsAndRollsAndRollZeroIsTheSeed) {
  std::set<std::uint64_t> seen;
  for (std::uint64_t seed = 0; seed < 64; ++seed) {
    EXPECT_EQ(fo4::forms::detail::roll_seed(seed, 0), seed);
    for (std::uint64_t roll = 1; roll < 64; ++roll) {
      EXPECT_TRUE(seen.insert(fo4::forms::detail::roll_seed(seed, roll)).second)
          << "seed " << seed << " roll " << roll << " repeats an earlier stream";
    }
  }
  EXPECT_EQ(seen.size(), 64u * 63u);
}

TEST(LeveledPickCount, TotalNodeBudgetStopsAnOversizedCountWithBudgetExceeded) {
  std::vector<Rec> records;
  for (std::uint32_t index = 0; index < 121; ++index) {
    const std::uint32_t id = 0xC00 + index;
    const std::uint8_t flags = index == 0 ? 0x06 : 0x04;
    if (index < 40) {
      const std::uint32_t first = 0xC00 + 3 * index + 1;
      records.push_back(leveled("LVLI", id, flags, 0, {{1, first, 0}, {1, first + 1, 0}, {1, first + 2, 0}}));
    } else {
      records.push_back(leveled("LVLI", id, flags, 0, {{1, 0x910, 0}}));
    }
  }
  records.push_back(Rec{"ARMO", 0x910, {{"EDID", zstr("Armor")}, {"BOD2", words({0x8})}}});
  const auto env = make_env({{"Tree.esm", build({}, 0x1, records)}});
  const auto root_key = fo4::FormKey{fo4::PluginKey::from_filename("Tree.esm"), fo4::LocalFormId{0xC00}};
  const auto pick = fo4::forms::pick_leveled_counted(env, root_key, 1, 7, 1000);
  EXPECT_EQ(pick.reason, LeveledPickReason::budget_exceeded);
  EXPECT_EQ(pick.multiplier, 1u);
  EXPECT_FALSE(pick.entries.empty());
  EXPECT_GE(pick.rolls, 500u);
  EXPECT_LE(pick.rolls, 600u);
  EXPECT_EQ(pick.entries.size(), static_cast<std::size_t>(pick.rolls) * 81u);
}

TEST(LeveledPickCount, TotalEntryCapStopsAnOversizedCountWithBudgetExceeded) {
  const auto env = make_env(
      {{"Wide.esm",
        build({}, 0x1,
              {leveled("LVLI", 0xD00, 0x06, 0,
                       {{1, 0x910, 0}, {1, 0x910, 0}, {1, 0x910, 0}, {1, 0x910, 0},
                        {1, 0x910, 0}, {1, 0x910, 0}, {1, 0x910, 0}, {1, 0x910, 0}}),
               Rec{"ARMO", 0x910, {{"EDID", zstr("Armor")}, {"BOD2", words({0x8})}}}})}});
  const auto root_key = fo4::FormKey{fo4::PluginKey::from_filename("Wide.esm"), fo4::LocalFormId{0xD00}};
  const auto pick = fo4::forms::pick_leveled_counted(env, root_key, 1, 7, 20000);
  EXPECT_EQ(pick.reason, LeveledPickReason::budget_exceeded);
  EXPECT_EQ(pick.multiplier, 1u);
  EXPECT_EQ(pick.entries.size(), 65536u);
  EXPECT_EQ(pick.rolls, 8192u);
}

TEST(ScriptSpawnPlan, NpcBaseGivesItselfAndLeveledBaseIsPicked) {
  const auto env = spawn_env();
  const auto npc = fo4::forms::plan_script_spawn(env, key(0x862), 1, 7, 2);
  ASSERT_EQ(npc.entries.size(), 1u);
  EXPECT_EQ(npc.entries.front(), key(0x862));
  EXPECT_EQ(npc.reason, LeveledPickReason::picked);
  EXPECT_EQ(npc.level_modifier, std::optional<std::int32_t>(2));

  const auto leveled_plan = fo4::forms::plan_script_spawn(env, key(0x870), 1, 7);
  EXPECT_EQ(leveled_plan.base_kind, fo4::forms::SpawnBaseKind::leveled_npc);
  EXPECT_TRUE(leveled_plan.entries.empty());
  EXPECT_EQ(leveled_plan.reason, LeveledPickReason::no_entry_at_level);
  EXPECT_FALSE(leveled_plan.level_modifier.has_value());
}

TEST(ScriptSpawnPlan, OtherBaseIsNotSpawnable) {
  const auto env = spawn_env();
  const auto plan = fo4::forms::plan_script_spawn(env, key(0x910), 1, 7);
  EXPECT_TRUE(plan.entries.empty());
  EXPECT_EQ(plan.reason, LeveledPickReason::not_spawnable);
}
