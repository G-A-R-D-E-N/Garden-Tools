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

TEST(NpcVisualDecode, HumanFemaleGetsFaceGenUncoveredSkinAndOutfit) {
  const auto env = base_env();
  const auto visual = decode(env, 0x850);
  EXPECT_EQ(visual.npc, key(0x850));
  ASSERT_TRUE(visual.race.has_value());
  EXPECT_EQ(*visual.race, key(k_human));
  EXPECT_EQ(visual.race_editor_id, std::optional<std::string>("HumanRaceT"));
  EXPECT_TRUE(visual.female);
  ASSERT_TRUE(visual.skin_tint.has_value());
  EXPECT_EQ(*visual.skin_tint, (std::array<float, 4>{0.1f, 0.2f, 0.3f, 1.0f}));
  EXPECT_EQ(visual.skeleton_hkx_entry, std::optional<std::string>(std::string(k_human_skeleton)));
  EXPECT_EQ(visual.idle_clip_entry, std::optional<std::string>(std::string(k_human_idle)));
  const std::vector<NpcVisualPart> expected{
      part(std::string(k_facegen_dir) + "00000850.nif", 0, NpcVisualSource::facegen),
      part("meshes/actors/character/characterassets/femalehands.nif", 0x30, NpcVisualSource::skin),
      part("meshes/armor/raider/raiderfemale.nif", 0x8, NpcVisualSource::outfit),
  };
  EXPECT_EQ(visual.parts, expected);
  EXPECT_TRUE(visual.stubs.empty());
}

TEST(NpcVisualDecode, AddonForAnotherRaceIsDroppedAndAdditionalRacesMatch) {
  const auto env = base_env();
  const auto male = decode(env, 0x851);
  const std::vector<NpcVisualPart> expected_male{
      part(std::string(k_facegen_dir) + "00000851.nif", 0, NpcVisualSource::facegen),
      part("meshes/actors/character/characterassets/malebody.nif", 0x8, NpcVisualSource::skin),
      part("meshes/actors/character/characterassets/malehands.nif", 0x30, NpcVisualSource::skin),
  };
  EXPECT_EQ(male.parts, expected_male);
  EXPECT_FALSE(male.female);
  EXPECT_FALSE(male.skin_tint.has_value());
  EXPECT_TRUE(male.stubs.empty());

  const auto extra = decode(env, 0x893);
  ASSERT_FALSE(extra.parts.empty());
  EXPECT_EQ(extra.parts.back(), part("meshes/armor/extra.nif", 0x40, NpcVisualSource::outfit));
}

TEST(NpcVisualDecode, GhoulWearsAnAddonForItsRacesArmorRace) {
  const auto env = armor_race_env();
  const auto ghoul = decode(env, k_npc_ghoul_suit);
  const std::vector<NpcVisualPart> expected{
      part(std::string(k_facegen_dir) + "000008F5.nif", 0, NpcVisualSource::facegen),
      part("meshes/armor/suit/suitm.nif", 0x8, NpcVisualSource::outfit),
      part("meshes/armor/suit/glovesm.nif", 0x30, NpcVisualSource::outfit),
  };
  EXPECT_EQ(ghoul.parts, expected);
  EXPECT_TRUE(ghoul.stubs.empty());
}

TEST(NpcVisualDecode, HumanKeepsTheSameSuit) {
  const auto env = armor_race_env();
  const auto human = decode(env, k_npc_human_suit);
  const std::vector<NpcVisualPart> expected{
      part(std::string(k_facegen_dir) + "000008F6.nif", 0, NpcVisualSource::facegen),
      part("meshes/armor/suit/suitm.nif", 0x8, NpcVisualSource::outfit),
      part("meshes/armor/suit/glovesm.nif", 0x30, NpcVisualSource::outfit),
  };
  EXPECT_EQ(human.parts, expected);
}

TEST(NpcVisualDecode, GhoulOnlyAddonStillWorksForAGhoulAndIsNotWornByAHuman) {
  const auto env = armor_race_env();
  const auto ghoul = decode(env, k_npc_ghoul_own);
  const std::vector<NpcVisualPart> expected_ghoul{
      part(std::string(k_facegen_dir) + "000008FA.nif", 0, NpcVisualSource::facegen),
      part("meshes/armor/ghoul/body.nif", 0x8, NpcVisualSource::outfit),
  };
  EXPECT_EQ(ghoul.parts, expected_ghoul);

  const auto human = decode(env, k_npc_human_ghoul_own);
  const std::vector<NpcVisualPart> expected_human{
      part(std::string(k_facegen_dir) + "000008FB.nif", 0, NpcVisualSource::facegen),
      part("meshes/actors/character/characterassets/malebody.nif", 0x8, NpcVisualSource::skin),
      part("meshes/actors/character/characterassets/malehands.nif", 0x30, NpcVisualSource::skin),
  };
  EXPECT_EQ(human.parts, expected_human);
}

TEST(NpcVisualDecode, OutfitThatSuppliesNoAddonDoesNotRemoveTheSkinBody) {
  const auto env = armor_race_env();
  const auto human = decode(env, k_npc_human_unmatched);
  const std::vector<NpcVisualPart> expected{
      part(std::string(k_facegen_dir) + "000008FE.nif", 0, NpcVisualSource::facegen),
      part("meshes/actors/character/characterassets/malebody.nif", 0x8, NpcVisualSource::skin),
      part("meshes/actors/character/characterassets/malehands.nif", 0x30, NpcVisualSource::skin),
  };
  EXPECT_EQ(human.parts, expected);
}

TEST(NpcVisualDecode, ArmorRaceCycleStopsAndStillMatches) {
  const auto env = armor_race_env();
  const auto cycle = decode(env, k_npc_race_cycle);
  const std::vector<NpcVisualPart> expected{
      part("meshes/armor/cycle/body.nif", 0x8, NpcVisualSource::outfit),
  };
  EXPECT_EQ(cycle.parts, expected);
  EXPECT_TRUE(cycle.stubs.empty());
}

TEST(NpcVisualDecode, MatchedAddonWithoutAModelStillBlocksTheSkinSlot) {
  const auto env = armor_race_env();
  const auto human = decode(env, k_npc_blocker);
  const std::vector<NpcVisualPart> expected{
      part(std::string(k_facegen_dir) + "000009B3.nif", 0, NpcVisualSource::facegen),
      part("meshes/actors/character/characterassets/malehands.nif", 0x30, NpcVisualSource::skin),
  };
  EXPECT_EQ(human.parts, expected);
  const std::vector<std::string> stubs{"armor addon 0009B1:Base.esm has no male model"};
  EXPECT_EQ(human.stubs, stubs);
}

TEST(NpcVisualDecode, SharedSlotKeepsTheAddonMatchingEarliestInTheChain) {
  const auto env = armor_race_env();
  const auto ghoul = decode(env, k_npc_mixed_ghoul);
  const std::vector<NpcVisualPart> expected_ghoul{
      part(std::string(k_facegen_dir) + "000009B8.nif", 0, NpcVisualSource::facegen),
      part("meshes/armor/mixed/ghoul.nif", 0x8, NpcVisualSource::outfit),
  };
  EXPECT_EQ(ghoul.parts, expected_ghoul);

  const auto human = decode(env, k_npc_mixed_human);
  const std::vector<NpcVisualPart> expected_human{
      part(std::string(k_facegen_dir) + "000009B9.nif", 0, NpcVisualSource::facegen),
      part("meshes/actors/character/characterassets/malehands.nif", 0x30, NpcVisualSource::skin),
      part("meshes/armor/mixed/human.nif", 0x8, NpcVisualSource::outfit),
  };
  EXPECT_EQ(human.parts, expected_human);
}

TEST(NpcVisualDecode, ArmorRaceChainStopsAfterEightHops) {
  const auto env = armor_race_env();
  const auto visual = decode(env, k_npc_hops);
  const std::vector<NpcVisualPart> expected{
      part("meshes/armor/hop/eight.nif", 0x8, NpcVisualSource::outfit),
  };
  EXPECT_EQ(visual.parts, expected);
  EXPECT_TRUE(visual.stubs.empty());
}

TEST(NpcVisualDecode, ArmorRaceWithTheWrongSizeIsOneStubAndTheOwnRaceStillMatches) {
  const auto env = armor_race_env();
  const auto visual = decode(env, k_npc_bad_rnam);
  EXPECT_TRUE(visual.parts.empty());
  const std::vector<std::string> stubs{"RACE 0009E0:Base.esm RNAM must be 4 bytes"};
  EXPECT_EQ(visual.stubs, stubs);
}

TEST(NpcVisualDecode, SelfReferencingArmorRaceMatchesOnceWithoutStubs) {
  const auto env = armor_race_env();
  const auto visual = decode(env, k_npc_self_rnam);
  const std::vector<NpcVisualPart> expected{
      part("meshes/armor/self/body.nif", 0x8, NpcVisualSource::outfit),
  };
  EXPECT_EQ(visual.parts, expected);
  EXPECT_TRUE(visual.stubs.empty());
}

TEST(NpcVisualDecode, ArmorRaceThatIsNotALiveRaceIsNotFollowed) {
  const auto env = armor_race_env();
  const auto visual = decode(env, k_npc_dead_rnam);
  EXPECT_TRUE(visual.parts.empty());
  EXPECT_TRUE(visual.stubs.empty());
}

TEST(NpcVisualDecode, FemaleFallsBackToTheMaleModelAndUnusableItemsBecomeStubs) {
  const auto env = base_env();
  const auto female = decode(env, 0x852);
  const std::vector<NpcVisualPart> expected{
      part(std::string(k_facegen_dir) + "00000852.nif", 0, NpcVisualSource::facegen),
      part("meshes/actors/character/characterassets/femalebody.nif", 0x8, NpcVisualSource::skin),
      part("meshes/armor/raider/glovesmale.nif", 0x30, NpcVisualSource::outfit),
  };
  EXPECT_EQ(female.parts, expected);
  const std::vector<std::string> stubs{
      "armor addon 000835:Base.esm has no female model",
      "leveled list 000837:Base.esm has no entry at level 1",
  };
  EXPECT_EQ(female.stubs, stubs);
}

TEST(NpcVisualDecode, TemplateTraitsAndInventoryAreFollowedIndependently) {
  const auto env = base_env();
  const auto child = decode(env, 0x861);
  ASSERT_TRUE(child.race.has_value());
  EXPECT_EQ(*child.race, key(k_human));
  EXPECT_FALSE(child.female);
  ASSERT_TRUE(child.skin_tint.has_value());
  EXPECT_EQ(*child.skin_tint, (std::array<float, 4>{0.5f, 0.25f, 0.125f, 1.0f}));
  const std::vector<NpcVisualPart> expected{
      part(std::string(k_facegen_dir) + "00000860.nif", 0, NpcVisualSource::facegen),
      part("meshes/actors/character/characterassets/malehands.nif", 0x30, NpcVisualSource::skin),
      part("meshes/armor/raider/raidermale.nif", 0x8, NpcVisualSource::outfit),
  };
  EXPECT_EQ(child.parts, expected);
  EXPECT_TRUE(child.stubs.empty());

  const auto traits_only = decode(env, 0x864);
  EXPECT_FALSE(traits_only.female);
  const std::vector<NpcVisualPart> expected_traits_only{
      part(std::string(k_facegen_dir) + "00000860.nif", 0, NpcVisualSource::facegen),
      part("meshes/actors/character/characterassets/malebody.nif", 0x8, NpcVisualSource::skin),
      part("meshes/armor/raider/glovesmale.nif", 0x30, NpcVisualSource::outfit),
  };
  EXPECT_EQ(traits_only.parts, expected_traits_only);
}

TEST(NpcVisualDecode, TptaTraitsEntryOverridesTheDefaultTemplate) {
  const auto env = base_env();
  const auto visual = decode(env, 0x863);
  EXPECT_TRUE(visual.female);
  ASSERT_TRUE(visual.skin_tint.has_value());
  EXPECT_EQ(*visual.skin_tint, (std::array<float, 4>{1.0f, 0.0f, 0.0f, 1.0f}));
  ASSERT_FALSE(visual.parts.empty());
  EXPECT_EQ(visual.parts.front().mesh_entry, std::string(k_facegen_dir) + "00000865.nif");
}

TEST(NpcVisualDecode, ObjectTemplateGivesRobotPartsAndNestedModsAreFollowed) {
  const auto env = base_env();
  const auto visual = decode(env, k_npc_robot);
  ASSERT_TRUE(visual.skeleton_hkx_entry.has_value());
  EXPECT_EQ(*visual.skeleton_hkx_entry, std::string(k_robot_skeleton));
  EXPECT_EQ(visual.idle_clip_entry,
            std::optional<std::string>("meshes/actors/robot/animations/idle.hkx"));
  ASSERT_EQ(visual.parts.size(), 2u);
  EXPECT_EQ(visual.parts[0],
            part("meshes/actors/robot/parts/torsohandy.nif", 0, NpcVisualSource::object_template));
  EXPECT_EQ(visual.parts[1],
            part("meshes/actors/robot/parts/armhandy.nif", 0, NpcVisualSource::object_template));
  for (const auto& stub : visual.stubs) {
    EXPECT_EQ(stub.find("object template"), std::string::npos) << stub;
  }
}

TEST(NpcVisualDecode, MissingObjectTemplateItemBecomesAStub) {
  const auto env = base_env();
  const auto visual = decode(env, k_npc_robot_bad);
  EXPECT_TRUE(visual.parts.empty());
  EXPECT_NE(std::find(visual.stubs.begin(), visual.stubs.end(),
                      "object template item 0008BF:Base.esm not found"),
            visual.stubs.end());
}

TEST(NpcVisualDecode, ObjectTemplatePrefersTheNpcsOwnBeforeItsTemplates) {
  const auto env = base_env();
  const auto own = decode(env, k_npc_robot_own);
  const std::vector<NpcVisualPart> own_expected{
      part("meshes/actors/robot/parts/headhandy.nif", 0, NpcVisualSource::object_template),
  };
  EXPECT_EQ(own.parts, own_expected);

  const auto inherited = decode(env, k_npc_robot_inherit);
  const std::vector<NpcVisualPart> inherited_expected{
      part("meshes/actors/robot/parts/torsohandy.nif", 0, NpcVisualSource::object_template),
  };
  EXPECT_EQ(inherited.parts, inherited_expected);
}

TEST(NpcVisualDecode, ObjectTemplateOnAMidChainTemplateIsUsed) {
  const auto env = base_env();
  const auto visual = decode(env, k_npc_chain_a);
  const std::vector<NpcVisualPart> expected{
      part("meshes/actors/robot/parts/headhandy.nif", 0, NpcVisualSource::object_template),
  };
  EXPECT_EQ(visual.parts, expected)
      << "stopping at the terminal template loses the mid-chain OBTS";
}

TEST(NpcVisualDecode, ObjectTemplateOnTheInventoryTemplateIsUsed) {
  const auto env = base_env();
  const auto visual = decode(env, k_npc_inventory_obts);
  const std::vector<NpcVisualPart> expected{
      part("meshes/actors/robot/parts/torsohandy.nif", 0, NpcVisualSource::object_template),
  };
  EXPECT_EQ(visual.parts, expected);
}

TEST(NpcVisualDecode, TruncatedObjectTemplateBecomesAStub) {
  const auto env = base_env();
  const auto visual = decode(env, k_npc_truncated);
  EXPECT_TRUE(visual.parts.empty());
  const auto bad = std::find_if(visual.stubs.begin(), visual.stubs.end(), [](const auto& stub) {
    return stub.find("did not decode") != std::string::npos;
  });
  EXPECT_NE(bad, visual.stubs.end()) << "a miscounted OBTS must not throw or decode silently";
}

TEST(NpcVisualDecode, OmodCycleKeepsBothMeshesOnce) {
  const auto env = base_env();
  const auto visual = decode(env, k_npc_omod_cycle);
  ASSERT_EQ(visual.parts.size(), 2u);
  EXPECT_EQ(visual.parts[0].mesh_entry, "meshes/actors/robot/parts/cyclea.nif");
  EXPECT_EQ(visual.parts[1].mesh_entry, "meshes/actors/robot/parts/cycleb.nif");
  const auto deep = std::find_if(visual.stubs.begin(), visual.stubs.end(), [](const auto& stub) {
    return stub.find("nests deeper than 8") != std::string::npos;
  });
  EXPECT_EQ(deep, visual.stubs.end()) << "the seen guard must stop the cycle before the depth cap";
}

TEST(NpcVisualDecode, OmodNestingStopsAfterEightLevels) {
  const auto eight = omod_chain_env(8);
  const auto eight_visual = decode_in(eight, "Chain.esp", 0x95F);
  EXPECT_EQ(eight_visual.parts.size(), 8u) << "eight nested OMODs must all contribute";
  const auto eight_deep = std::find_if(eight_visual.stubs.begin(), eight_visual.stubs.end(),
                                       [](const auto& stub) {
                                         return stub.find("nests deeper than 8") != std::string::npos;
                                       });
  EXPECT_EQ(eight_deep, eight_visual.stubs.end());

  const auto nine = omod_chain_env(9);
  const auto nine_visual = decode_in(nine, "Chain.esp", 0x95F);
  EXPECT_EQ(nine_visual.parts.size(), 8u);
  const auto nine_deep = std::find_if(nine_visual.stubs.begin(), nine_visual.stubs.end(),
                                      [](const auto& stub) {
                                        return stub.find("nests deeper than 8") != std::string::npos;
                                      });
  EXPECT_NE(nine_deep, nine_visual.stubs.end());
}

TEST(NpcVisualDecode, NonOmodIncludeIsReported) {
  const auto env = base_env();
  const auto visual = decode(env, k_npc_non_omod);
  EXPECT_TRUE(visual.parts.empty());
  const auto wrong = std::find_if(visual.stubs.begin(), visual.stubs.end(), [](const auto& stub) {
    return stub.find("is a ARMO record; not resolved") != std::string::npos;
  });
  EXPECT_NE(wrong, visual.stubs.end());
}

TEST(NpcVisualDecode, ObjectTemplateIsNotUsedWhenTheNpcAlreadyHasASkin) {
  const auto env = base_env();
  const auto visual = decode(env, k_npc_robot_dressed);
  const auto template_parts = std::count_if(visual.parts.begin(), visual.parts.end(),
                                           [](const auto& item) {
                                             return item.source == NpcVisualSource::object_template;
                                           });
  EXPECT_EQ(template_parts, 0) << "a skinned robot would draw the template mesh twice";
  const auto note = std::find_if(visual.stubs.begin(), visual.stubs.end(), [](const auto& stub) {
    return stub.find("not used: the NPC already has") != std::string::npos;
  });
  EXPECT_NE(note, visual.stubs.end());
}

TEST(NpcVisualDecode, ObjectTemplateIsNotUsedWhenTheNpcHasOnlyASkinOrAnOutfit) {
  const auto env = base_env();
  for (const auto id : {k_npc_human_obts, k_npc_robot_skin, k_npc_robot_outfit, k_npc_face_only}) {
    const auto visual = decode(env, id);
    const auto template_parts = std::count_if(visual.parts.begin(), visual.parts.end(),
                                             [](const auto& item) {
                                               return item.source ==
                                                      NpcVisualSource::object_template;
                                             });
    EXPECT_EQ(template_parts, 0) << "NPC " << id << " drew a template part";
    const auto note = std::find_if(visual.stubs.begin(), visual.stubs.end(), [](const auto& stub) {
      return stub.starts_with("note: object template on ");
    });
    EXPECT_NE(note, visual.stubs.end()) << "NPC " << id << " lost its skip note";
  }
}

TEST(NpcVisualDecode, ObjectTemplateBadMasterIndexInAnOmodIsReported) {
  const auto env = base_env();
  const auto visual = decode(env, k_npc_omod_bad_include);
  ASSERT_EQ(visual.parts.size(), 1u);
  EXPECT_EQ(visual.parts.front().mesh_entry, "meshes/actors/robot/parts/badinclude.nif");
  const auto bad = std::find_if(visual.stubs.begin(), visual.stubs.end(), [](const auto& stub) {
    return stub.find("has a bad form ID") != std::string::npos;
  });
  EXPECT_NE(bad, visual.stubs.end()) << "a bad master index must not fail the whole decode";
}

TEST(NpcVisualDecode, BadFormIdInAnObjectTemplateBecomesAStub) {
  const auto env = base_env();
  const auto visual = decode(env, k_npc_robot_badform);
  EXPECT_TRUE(visual.parts.empty());
  const auto bad = std::find_if(visual.stubs.begin(), visual.stubs.end(), [](const auto& stub) {
    return stub.find("has a bad form ID") != std::string::npos;
  });
  EXPECT_NE(bad, visual.stubs.end());
}

TEST(NpcVisualDecode, ObjectTemplatePrefersTheDefaultParentlessCombination) {
  const auto env = base_env();
  const auto visual = decode(env, k_npc_robot_combos);
  const std::vector<NpcVisualPart> expected{
      part("meshes/actors/robot/parts/headhandy.nif", 0, NpcVisualSource::object_template),
  };
  EXPECT_EQ(visual.parts, expected);
}

TEST(NpcVisualDecode, ObjectTemplateWithKeywordsAndPropertiesStillDecodes) {
  const auto env = base_env();
  const auto visual = decode(env, k_npc_robot_rich);
  const std::vector<NpcVisualPart> expected{
      part("meshes/actors/robot/parts/headhandy.nif", 0, NpcVisualSource::object_template),
  };
  EXPECT_EQ(visual.parts, expected);
  EXPECT_TRUE(visual.stubs.empty());
}

TEST(NpcVisualDecode, TemplateCycleStopsAtDepthEight) {
  const auto env = base_env();
  const auto visual = decode(env, 0x866);
  ASSERT_FALSE(visual.stubs.empty());
  EXPECT_NE(visual.stubs.front().find("deeper than 8"), std::string::npos);
  EXPECT_EQ(visual.stubs.size(), 1u);
}

TEST(NpcVisualDecode, NonFaceGenRaceUsesItsOwnSkeletonSkinAndDeduplicates) {
  const auto env = base_env();
  const auto male = decode(env, 0x886);
  EXPECT_EQ(male.skeleton_hkx_entry, std::optional<std::string>("meshes/actors/robot/characterassets/skeleton.hkx"));
  EXPECT_EQ(male.idle_clip_entry,
            std::optional<std::string>("meshes/actors/robot/animations/idle.hkx"));
  EXPECT_TRUE(male.stubs.empty());
  const std::vector<NpcVisualPart> expected{
      part("meshes/actors/robot/body.nif", 0x8, NpcVisualSource::skin),
      part("meshes/actors/robot/head.nif", 0x4, NpcVisualSource::skin),
  };
  EXPECT_EQ(male.parts, expected);

  const auto female = decode(env, 0x887);
  EXPECT_EQ(female.skeleton_hkx_entry,
            std::optional<std::string>("meshes/actors/createabot/characterassets/skeleton.hkx"));
  EXPECT_EQ(female.parts, expected);
  EXPECT_FALSE(female.idle_clip_entry) << "only the shared robot skeleton has a mapped idle";
  const auto no_idle = std::find_if(female.stubs.begin(), female.stubs.end(),
                                    [](const auto& stub) {
                                      return stub.find("no idle mapped") != std::string::npos;
                                    });
  EXPECT_NE(no_idle, female.stubs.end());
}

TEST(NpcVisualDecode, FaceGenUsesTheOriginalPluginAndTheWinningRecordSuppliesTheFields) {
  const auto patch = build({"Base.esm"}, 0x0,
                           {npc({.id = 0x850, .flags = 0, .outfit = k_outfit_raider})});
  const auto env = make_env({{"Base.esm", build({}, 0x1, base_records())}, {"Patch.esp", patch}});
  const auto visual = decode(env, 0x850);
  EXPECT_EQ(visual.npc, key(0x850));
  EXPECT_FALSE(visual.female);
  ASSERT_FALSE(visual.parts.empty());
  EXPECT_EQ(visual.parts.front().mesh_entry, std::string(k_facegen_dir) + "00000850.nif");
  EXPECT_FALSE(visual.skin_tint.has_value());
}

TEST(NpcVisualDecode, NonNpcRecordIsRejected) {
  const auto env = base_env();
  const auto* race_ref = env.winner(key(k_human));
  ASSERT_NE(race_ref, nullptr);
  EXPECT_THROW(fo4::forms::decode_npc_visual(env, *race_ref), NpcVisualError);
}

TEST(NpcVisualDecode, EntriesAreLowercaseSlashedAndPrefixedOnce) {
  EXPECT_EQ(fo4::forms::npc_visual_mesh_entry("Actors\\Character\\CharacterAssets\\MaleBody.nif"),
            "meshes/actors/character/characterassets/malebody.nif");
  EXPECT_EQ(fo4::forms::npc_visual_mesh_entry("Meshes\\Armor\\Extra.NIF"), "meshes/armor/extra.nif");
  EXPECT_EQ(fo4::forms::npc_visual_mesh_entry("/meshes/Armor/Extra.nif"), "meshes/armor/extra.nif");
  EXPECT_EQ(fo4::forms::npc_visual_hkx_entry("Actors\\Character\\CharacterAssets\\skeleton.nif"),
            "meshes/actors/character/characterassets/skeleton.hkx");
  EXPECT_EQ(fo4::forms::npc_visual_hkx_entry("Actors\\Supermutant\\CharacterAssets\\skeleton.nif"),
            "meshes/actors/supermutant/characterassets/skeleton.hkx");
  EXPECT_EQ(fo4::forms::npc_visual_hkx_entry("Actors\\Odd.Dir\\skeleton"), "meshes/actors/odd.dir/skeleton.hkx");
}

using EnvPtr = std::unique_ptr<radicle_env, decltype(&radicle_env_destroy)>;
using VisualPtr = std::unique_ptr<radicle_npc_visual, decltype(&radicle_npc_visual_free)>;

class NpcVisualCApi : public ::testing::Test {
protected:
  void SetUp() override {
    root_ = std::filesystem::temp_directory_path() /
            ("radicle_npc_visual_" +
             std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::create_directories(root_);
    path_ = (root_ / "Base.esm").string();
    const auto bytes = build({}, 0x1, base_records());
    std::ofstream stream(path_, std::ios::binary);
    stream.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    stream.close();
    const char* paths[] = {path_.c_str()};
    radicle_env* env = nullptr;
    ASSERT_EQ(radicle_env_load(paths, 1, &env), RADICLE_STATUS_OK);
    env_.reset(env);
  }

  void TearDown() override {
    env_.reset();
    std::error_code error;
    std::filesystem::remove_all(root_, error);
  }

  const radicle_record* winner(std::uint32_t local) {
    const radicle_record* record = nullptr;
    EXPECT_EQ(radicle_env_winner(env_.get(), radicle_form_key{"Base.esm", local}, &record), RADICLE_STATUS_OK);
    return record;
  }

  std::filesystem::path root_;
  std::string path_;
  EnvPtr env_{nullptr, radicle_env_destroy};
};

TEST_F(NpcVisualCApi, DecodeReportsThePlanAndFreeReleasesIt) {
  radicle_npc_visual* raw = nullptr;
  ASSERT_EQ(radicle_npc_visual_decode_ex(env_.get(), winner(0x850), nullptr, &raw), RADICLE_STATUS_OK);
  const VisualPtr view(raw, radicle_npc_visual_free);
  EXPECT_STREQ(view->npc.plugin, "Base.esm");
  EXPECT_EQ(view->npc.local_id, 0x850u);
  EXPECT_EQ(view->has_race, 1);
  EXPECT_STREQ(view->race.plugin, "Base.esm");
  EXPECT_EQ(view->race.local_id, k_human);
  EXPECT_STREQ(view->race_editor_id, "HumanRaceT");
  EXPECT_EQ(view->female, 1);
  EXPECT_EQ(view->has_skin_tint, 1);
  EXPECT_FLOAT_EQ(view->skin_tint[0], 0.1f);
  EXPECT_FLOAT_EQ(view->skin_tint[3], 1.0f);
  EXPECT_STREQ(view->skeleton_hkx_entry, k_human_skeleton.data());
  EXPECT_STREQ(view->idle_clip_entry, k_human_idle.data());
  ASSERT_EQ(view->part_count, 3u);
  ASSERT_NE(view->parts, nullptr);
  EXPECT_EQ(view->parts[0].source, 0);
  EXPECT_EQ(view->parts[0].biped_slots, 0u);
  EXPECT_STREQ(view->parts[0].mesh_entry,
               (std::string(k_facegen_dir) + "00000850.nif").c_str());
  EXPECT_EQ(view->parts[1].source, 1);
  EXPECT_EQ(view->parts[1].biped_slots, 0x30u);
  EXPECT_STREQ(view->parts[1].mesh_entry, "meshes/actors/character/characterassets/femalehands.nif");
  EXPECT_EQ(view->parts[2].source, 2);
  EXPECT_EQ(view->parts[2].biped_slots, 0x8u);
  EXPECT_EQ(view->stub_count, 0u);
  EXPECT_EQ(view->stubs, nullptr);
}

TEST_F(NpcVisualCApi, StubsAndAbsentFieldsAreReported) {
  radicle_npc_visual* raw = nullptr;
  ASSERT_EQ(radicle_npc_visual_decode_ex(env_.get(), winner(0x887), nullptr, &raw), RADICLE_STATUS_OK);
  const VisualPtr view(raw, radicle_npc_visual_free);
  EXPECT_EQ(view->has_skin_tint, 0);
  EXPECT_EQ(view->idle_clip_entry, nullptr);
  ASSERT_EQ(view->stub_count, 1u);
  ASSERT_NE(view->stubs, nullptr);
  EXPECT_EQ(std::string(view->stubs[0]).rfind("no idle mapped for race ", 0), 0u);
  ASSERT_EQ(view->part_count, 2u);
}

TEST_F(NpcVisualCApi, OptionsCarryTheLevelAndSeed) {
  radicle_npc_visual_options options{};
  options.has_level = 1;
  options.level = 25;
  options.seed = 0x5EED;
  radicle_npc_visual* raw = nullptr;
  ASSERT_EQ(radicle_npc_visual_decode_ex(env_.get(), winner(0x87C), &options, &raw),
            RADICLE_STATUS_OK);
  const VisualPtr view(raw, radicle_npc_visual_free);
  EXPECT_EQ(view->has_skin_tint, 1);
  EXPECT_FLOAT_EQ(view->skin_tint[0], 0.75f);

  radicle_npc_visual* defaults = nullptr;
  ASSERT_EQ(radicle_npc_visual_decode_ex(env_.get(), winner(0x87C), nullptr, &defaults),
            RADICLE_STATUS_OK);
  const VisualPtr default_view(defaults, radicle_npc_visual_free);
  ASSERT_EQ(default_view->has_skin_tint, 1);
  EXPECT_FLOAT_EQ(default_view->skin_tint[0], 0.25f) << "NULL options must decode at level 1";

  std::set<float> tints;
  for (std::uint64_t seed = 0; seed < 64; ++seed) {
    radicle_npc_visual_options varied{};
    varied.has_level = 1;
    varied.level = 25;
    varied.seed = seed;
    radicle_npc_visual* picked = nullptr;
    ASSERT_EQ(radicle_npc_visual_decode_ex(env_.get(), winner(0x877), &varied, &picked),
              RADICLE_STATUS_OK);
    const VisualPtr picked_view(picked, radicle_npc_visual_free);
    ASSERT_EQ(picked_view->has_skin_tint, 1);
    tints.insert(picked_view->skin_tint[0]);
  }
  EXPECT_EQ(tints.size(), 2u) << "the seed does not reach the leveled pick";
}

TEST_F(NpcVisualCApi, ObjectTemplatePartsReachTheCView) {
  radicle_npc_visual* raw = nullptr;
  ASSERT_EQ(radicle_npc_visual_decode_ex(env_.get(), winner(k_npc_robot), nullptr, &raw), RADICLE_STATUS_OK);
  const VisualPtr view(raw, radicle_npc_visual_free);
  ASSERT_EQ(view->part_count, 2u);
  EXPECT_EQ(view->parts[0].source, 3);
  EXPECT_STREQ(view->parts[0].mesh_entry, "meshes/actors/robot/parts/torsohandy.nif");
  EXPECT_EQ(view->parts[1].source, 3);
}

TEST_F(NpcVisualCApi, RejectsOutOfRangeOptions) {
  radicle_npc_visual_options options{};
  options.has_level = 1;
  options.level = 0;
  radicle_npc_visual* raw = nullptr;
  EXPECT_EQ(radicle_npc_visual_decode_ex(env_.get(), winner(0x850), &options, &raw),
            RADICLE_STATUS_INVALID_ARGUMENT);
  options.level = 70000;
  EXPECT_EQ(radicle_npc_visual_decode_ex(env_.get(), winner(0x850), &options, &raw),
            RADICLE_STATUS_INVALID_ARGUMENT);
  options.level = 5;
  options.has_level = 2;
  EXPECT_EQ(radicle_npc_visual_decode_ex(env_.get(), winner(0x850), &options, &raw),
            RADICLE_STATUS_INVALID_ARGUMENT);
  options.has_level = 1;
  options.reserved[1] = 1;
  EXPECT_EQ(radicle_npc_visual_decode_ex(env_.get(), winner(0x850), &options, &raw),
            RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(raw, nullptr);
}

TEST_F(NpcVisualCApi, RejectsNonNpcRecordsAndMissingArguments) {
  radicle_npc_visual* raw = reinterpret_cast<radicle_npc_visual*>(0x1);
  EXPECT_EQ(radicle_npc_visual_decode_ex(env_.get(), winner(k_human), nullptr, &raw), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(raw, nullptr);
  EXPECT_EQ(radicle_npc_visual_decode_ex(env_.get(), winner(0x850), nullptr, nullptr), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_npc_visual_decode_ex(env_.get(), nullptr, nullptr, &raw), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_npc_visual_decode_ex(nullptr, winner(0x850), nullptr, &raw), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(raw, nullptr);
  radicle_npc_visual_free(nullptr);
}
