#include "fo4/c_api_env.h"
#include "fo4/c_api_npc_visual.h"
#include "fo4/environment/environment.hpp"
#include "fo4/forms/npc_visual.hpp"
#include "fo4/plugin/reader.hpp"
#include "plugin_bytes.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <initializer_list>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>
#include <vector>

#include "npc_visual_fixture.hpp"

using namespace npc_visual_fixture;

TEST(NpcVisualDecode, LeveledTemplateBecomesOneStubAndOwnFieldsAreUsed) {
  const auto env = base_env();
  const auto visual = decode(env, 0x862);
  const std::vector<std::string> stubs{"leveled list 000870:Base.esm has no entry at level 1"};
  EXPECT_EQ(visual.stubs, stubs);
  EXPECT_FALSE(visual.female);
  ASSERT_TRUE(visual.race.has_value());
  EXPECT_EQ(*visual.race, key(k_human));
  ASSERT_FALSE(visual.parts.empty());
  EXPECT_EQ(visual.parts.front().mesh_entry, std::string(k_facegen_dir) + "00000862.nif");
}

TEST(NpcVisualDecode, LeveledTemplateFollowsTheEntryForTheSpawnLevel) {
  const auto env = base_env();
  const auto visual = decode(env, 0x877, 5, 0x5EED);
  EXPECT_TRUE(visual.stubs.empty());
  ASSERT_TRUE(visual.skin_tint.has_value());
  EXPECT_EQ(*visual.skin_tint, (std::array<float, 4>{0.25f, 0.0f, 0.0f, 1.0f}));
  ASSERT_FALSE(visual.parts.empty());
  EXPECT_EQ(visual.parts.front().mesh_entry, std::string(k_facegen_dir) + "00000875.nif");
  for (std::uint64_t seed = 0; seed < 64; ++seed) {
    const auto picked = fo4::forms::pick_leveled_entry(env, key(k_lvln_template), 5, seed);
    ASSERT_TRUE(picked.has_value());
    EXPECT_EQ(*picked, key(k_npc_low)) << "seed " << seed << " drew the level 20 entry at level 5";
  }
}

TEST(NpcVisualDecode, LeveledTemplateWithoutAllLevelsUsesTheHighestTier) {
  const auto env = base_env();
  const auto visual = decode(env, 0x87C, 25, 0x5EED);
  EXPECT_TRUE(visual.stubs.empty());
  ASSERT_TRUE(visual.skin_tint.has_value());
  EXPECT_EQ(*visual.skin_tint, (std::array<float, 4>{0.75f, 0.0f, 0.0f, 1.0f}));
  ASSERT_FALSE(visual.parts.empty());
  EXPECT_EQ(visual.parts.front().mesh_entry, std::string(k_facegen_dir) + "00000876.nif");
}

TEST(NpcVisualDecode, LeveledOutfitItemBecomesItsArmor) {
  const auto env = base_env();
  const auto visual = decode(env, 0x878, 5, 0x5EED);
  EXPECT_TRUE(visual.stubs.empty());
  const auto picked = std::any_of(visual.parts.begin(), visual.parts.end(), [](const auto& item) {
    return item.source == NpcVisualSource::outfit &&
           item.mesh_entry == "meshes/armor/raider/raidermale.nif";
  });
  EXPECT_TRUE(picked);
}

TEST(NpcVisualDecode, LeveledListChanceNoneYieldsNothing) {
  const auto env = base_env();
  const auto visual = decode(env, 0x879, 20, 0x5EED);
  const std::vector<std::string> stubs{"note: leveled list 000839:Base.esm rolled its chance none"};
  EXPECT_EQ(visual.stubs, stubs);
  const auto outfit = std::any_of(visual.parts.begin(), visual.parts.end(), [](const auto& item) {
    return item.source == NpcVisualSource::outfit;
  });
  EXPECT_FALSE(outfit);
}

TEST(NpcVisualDecode, NestedLeveledListStopsAtDepthEight) {
  const auto env = base_env();
  std::vector<std::string> stubs;
  const auto picked = fo4::forms::pick_leveled_entry(env, key(k_lvln_self), 20, 0x5EED, &stubs);
  EXPECT_FALSE(picked.has_value());
  ASSERT_EQ(stubs.size(), 1u);
  EXPECT_NE(stubs.front().find("nests deeper than 8"), std::string::npos);
}

TEST(NpcVisualDecode, LeveledPickIsStableForTheSameSeed) {
  const auto env = base_env();
  const auto first = fo4::forms::pick_leveled_entry(env, key(k_lvln_template), 25, 0x1234);
  const auto again = fo4::forms::pick_leveled_entry(env, key(k_lvln_template), 25, 0x1234);
  ASSERT_TRUE(first.has_value());
  EXPECT_EQ(first, again);
  EXPECT_TRUE(*first == key(k_npc_low) || *first == key(k_npc_high));
}

TEST(NpcVisualDecode, LeveledPickUsesEverySeedBit) {
  const auto env = base_env();
  for (const int shift : {8, 32}) {
    std::set<std::uint32_t> picks;
    for (std::uint64_t index = 0; index < 64; ++index) {
      const auto picked =
          fo4::forms::pick_leveled_entry(env, key(k_lvln_template), 25, index << shift);
      ASSERT_TRUE(picked.has_value()) << "shift " << shift << " index " << index;
      picks.insert(picked->local().value());
    }
    EXPECT_EQ(picks.size(), 2u) << "seeds with shift " << shift << " only reach one entry";
  }
}

TEST(NpcVisualDecode, SeedDependentIsSetOnlyWhenTheSeedCanChangeTheVisual) {
  const auto env = base_env();
  EXPECT_FALSE(decode(env, 0x877, 5, 0).seed_dependent);
  EXPECT_TRUE(decode(env, 0x877, 25, 0).seed_dependent);
  EXPECT_FALSE(decode(env, 0x87C, 25, 0).seed_dependent);
  EXPECT_FALSE(decode(env, k_npc_all, 1, 0).seed_dependent);
  EXPECT_TRUE(decode(env, k_npc_all, 5, 0).seed_dependent);
}

void expect_same_visual(const NpcVisual& actual, const NpcVisual& expected, const std::string& label) {
  EXPECT_EQ(actual.npc, expected.npc) << label;
  EXPECT_EQ(actual.race, expected.race) << label;
  EXPECT_EQ(actual.race_editor_id, expected.race_editor_id) << label;
  EXPECT_EQ(actual.female, expected.female) << label;
  EXPECT_EQ(actual.skin_tint, expected.skin_tint) << label;
  EXPECT_EQ(actual.skeleton_hkx_entry, expected.skeleton_hkx_entry) << label;
  EXPECT_EQ(actual.parts, expected.parts) << label;
  EXPECT_EQ(actual.idle_clip_entry, expected.idle_clip_entry) << label;
  EXPECT_EQ(actual.stubs, expected.stubs) << label;
  EXPECT_EQ(actual.seed_dependent, expected.seed_dependent) << label;
}

TEST(NpcVisualDecode, SeedIndependentNpcsDecodeIdenticallyAcrossSeedsAndLevels) {
  const auto env = base_env();
  const std::vector<std::uint32_t> corpus{0x850, 0x877, 0x878, 0x879, 0x87C, k_npc_all, 0x8E6,
                                          k_npc_nested_none, k_npc_lvlg, k_npc_low, k_npc_high,
                                          k_npc_wrong_outfit, k_npc_robot, k_npc_robot_combos,
                                          k_npc_robot_template, k_npc_robot_own, k_npc_robot_inherit,
                                          k_npc_chain_a, k_npc_inventory_obts};
  std::size_t checked = 0;
  for (const std::uint16_t level : {std::uint16_t{1}, std::uint16_t{5}, std::uint16_t{25}}) {
    for (const std::uint32_t local : corpus) {
      const auto baseline = decode(env, local, level, 0);
      if (baseline.seed_dependent) continue;
      ++checked;
      for (std::uint64_t seed = 1; seed < 64; ++seed) {
        expect_same_visual(decode(env, local, level, seed), baseline,
                           "npc " + std::to_string(local) + " level " + std::to_string(level) +
                               " seed " + std::to_string(seed));
      }
    }
  }
  EXPECT_GE(checked, 10u);
}

TEST(NpcVisualDecode, SeedDependentIsSetByAChanceNoneOnTheListEvenWithOneEntry) {
  const auto env = base_env();
  EXPECT_TRUE(decode(env, 0x8E6, 1, 0).seed_dependent);
}

TEST(NpcVisualDecode, SeedDependentIsSetByAGlobalChanceNoneEvenWhenTheStoredValueIsZero) {
  const auto env = base_env();
  EXPECT_TRUE(decode(env, k_npc_lvlg, 1, 0).seed_dependent);
}

TEST(NpcVisualDecode, SeedDependentIsSetByAChanceNoneOneListDownTheChain) {
  const auto env = base_env();
  EXPECT_TRUE(decode(env, k_npc_nested_none, 1, 0).seed_dependent);
}

TEST(NpcVisualDecode, LeveledTierFilterRunsBeforeEachEntryChanceNone) {
  const auto env = base_env();
  std::vector<std::string> stubs;
  const auto picked =
      fo4::forms::pick_leveled_entry(env, key(k_lvln_top_rolled), 25, 0x5EED, &stubs);
  EXPECT_FALSE(picked.has_value());
  ASSERT_FALSE(stubs.empty());
  EXPECT_EQ(stubs.front(),
            "note: leveled list 0008C2:Base.esm rolled out every entry at level 25");
}

TEST(NpcVisualDecode, LeveledRowsNamingOneItemRollTheirChanceNoneApart) {
  const auto env = base_env();
  std::size_t drawn = 0;
  for (std::uint64_t seed = 0; seed < 256; ++seed) {
    if (fo4::forms::pick_leveled_entry(env, key(k_lvln_twin_rows), 5, seed)) ++drawn;
  }
  EXPECT_GT(drawn, 160u) << "two 50 percent rows that roll together draw about half the time";
  EXPECT_LT(drawn, 224u);
}

TEST(NpcVisualDecode, LeveledUseAllStillPicksOneActorTemplate) {
  const auto env = base_env();
  const auto picked = fo4::forms::pick_leveled_entries(env, key(k_lvln_use_all), 5, 0x5EED);
  ASSERT_EQ(picked.size(), 1u);
  EXPECT_TRUE(picked.front() == key(k_npc_low) || picked.front() == key(k_npc_high));
}

TEST(NpcVisualDecode, LeveledNpcListWithUseAllIsStillNarrowedToItsTopTier) {
  const auto env = base_env();
  for (std::uint64_t seed = 0; seed < 64; ++seed) {
    const auto picked = fo4::forms::pick_leveled_entries(env, key(k_lvln_use_all_tiers), 25, seed);
    ASSERT_EQ(picked.size(), 1u);
    EXPECT_EQ(picked.front(), key(k_npc_high)) << "seed " << seed;
  }
}

TEST(NpcVisualDecode, LeveledEntryThatIsNotALiveRecordBecomesAStub) {
  const auto env = base_env();
  std::vector<std::string> stubs;
  const auto picked = fo4::forms::pick_leveled_entry(env, key(k_lvli_missing), 5, 0x5EED, &stubs);
  EXPECT_FALSE(picked.has_value());
  ASSERT_FALSE(stubs.empty());
  EXPECT_EQ(stubs.front(),
            "leveled list 0008C4:Base.esm picks 0008CF:Base.esm which is not a live record");
}

TEST(NpcVisualDecode, LeveledItemThatPicksAWrongTypeIsReportedByTheOutfit) {
  const auto env = base_env();
  const auto visual = decode(env, k_npc_wrong_outfit, 5, 0x5EED);
  const auto wrong = std::find_if(visual.stubs.begin(), visual.stubs.end(), [](const auto& stub) {
    return stub.find("which is a RACE record; not resolved") != std::string::npos;
  });
  EXPECT_NE(wrong, visual.stubs.end());
  const auto outfit = std::any_of(visual.parts.begin(), visual.parts.end(), [](const auto& item) {
    return item.source == NpcVisualSource::outfit;
  });
  EXPECT_FALSE(outfit) << "a wrong-type pick must not become an outfit part";
}

TEST(NpcVisualDecode, LeveledUseGlobalReportsAnApproximation) {
  const auto env = base_env();
  std::vector<std::string> stubs;
  const auto picked = fo4::forms::pick_leveled_entry(env, key(k_lvlg), 5, 0x5EED, &stubs);
  ASSERT_TRUE(picked.has_value());
  const auto unresolved = std::find_if(stubs.begin(), stubs.end(), [](const auto& stub) {
    return stub.rfind("leveled list 0008CA:Base.esm reads its chance none from global", 0) == 0;
  });
  EXPECT_NE(unresolved, stubs.end()) << "the stored chance can differ from the runtime global";
}

TEST(NpcVisualDecode, LeveledListCycleStopsAtDepthEight) {
  const auto env = base_env();
  std::vector<std::string> stubs;
  const auto picked = fo4::forms::pick_leveled_entry(env, key(k_lvln_cycle_a), 5, 0x5EED, &stubs);
  EXPECT_FALSE(picked.has_value());
  const auto depth = std::find_if(stubs.begin(), stubs.end(), [](const auto& stub) {
    return stub.find("nests deeper than 8") != std::string::npos;
  });
  EXPECT_NE(depth, stubs.end());
}

TEST(NpcVisualDecode, LeveledNestingAllowsEightListsAndStopsAtNine) {
  const auto eight = chain_env(8);
  std::vector<std::string> stubs8;
  const auto ok = fo4::forms::pick_leveled_entry(eight, chain_key(0x900), 5, 0x5EED, &stubs8);
  ASSERT_TRUE(ok.has_value()) << "eight nested lists must still reach their item";
  EXPECT_EQ(ok->local().value(), 0x910u);
  EXPECT_TRUE(stubs8.empty());

  const auto nine = chain_env(9);
  std::vector<std::string> stubs9;
  const auto stopped = fo4::forms::pick_leveled_entry(nine, chain_key(0x900), 5, 0x5EED, &stubs9);
  EXPECT_FALSE(stopped.has_value());
  const auto depth = std::find_if(stubs9.begin(), stubs9.end(), [](const auto& stub) {
    return stub.find("nests deeper than 8") != std::string::npos;
  });
  EXPECT_NE(depth, stubs9.end());
}

TEST(NpcVisualDecode, LevelZeroFindsNoEntryAndLevel65535TakesTheTopTier) {
  const auto env = base_env();
  std::vector<std::string> zero_stubs;
  EXPECT_FALSE(
      fo4::forms::pick_leveled_entry(env, key(k_lvln_template), 0, 0x5EED, &zero_stubs));
  ASSERT_FALSE(zero_stubs.empty());
  EXPECT_EQ(zero_stubs.front(),
            "note: leveled list 000871:Base.esm has no entry at level 0; the lowest entry is at level 1");

  const auto picked = fo4::forms::pick_leveled_entry(env, key(k_lvln_tier), 65535, 0x5EED);
  ASSERT_TRUE(picked.has_value());
  EXPECT_EQ(*picked, key(k_npc_high));
}

TEST(NpcVisualDecode, LeveledUseAllNodeBudgetStopsARunawayList) {
  const auto env = base_env();
  std::vector<std::string> stubs;
  const auto picked = fo4::forms::pick_leveled_entries(env, key(k_lvli_self_all), 5, 0x5EED, &stubs);
  EXPECT_TRUE(picked.empty());
  const auto budget = std::find_if(stubs.begin(), stubs.end(), [](const auto& stub) {
    return stub.find("1024-node budget") != std::string::npos;
  });
  EXPECT_NE(budget, stubs.end()) << "the node budget must stop the runaway list";
}

TEST(NpcVisualDecode, LeveledOutfitListWithUseAllContributesEveryEntry) {
  const auto env = base_env();
  const auto picked = fo4::forms::pick_leveled_entries(env, key(k_lvli_all), 10, 0x5EED);
  ASSERT_EQ(picked.size(), 2u);
  EXPECT_EQ(picked[0], key(k_raider_armor));
  EXPECT_EQ(picked[1], key(k_gloves_armor));
  const auto below = fo4::forms::pick_leveled_entries(env, key(k_lvli_all), 3, 0x5EED);
  ASSERT_EQ(below.size(), 1u) << "Use all must not hand over an entry above the spawn level";
  EXPECT_EQ(below.front(), key(k_raider_armor));
  const auto visual = decode(env, k_npc_all, 10, 0x5EED);
  EXPECT_TRUE(visual.stubs.empty());
  const auto outfit_parts = std::count_if(visual.parts.begin(), visual.parts.end(),
                                          [](const auto& item) {
                                            return item.source == NpcVisualSource::outfit;
                                          });
  EXPECT_EQ(outfit_parts, 2);
}
