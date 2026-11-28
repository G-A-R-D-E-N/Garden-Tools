#include "fo4/environment/environment.hpp"
#include "fo4/forms/npc_visual.hpp"
#include "fo4/forms/spawn_rules.hpp"
#include "fo4/plugin/mutation.hpp"
#include "fo4/schema/scalar.hpp"
#include "real_data.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <optional>
#include <set>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace {

std::optional<fo4::env::Environment> load_fallout4() {
  const char* dir = ::fo4::test::fo4_data_dir_cstr();
  if (dir == nullptr) return std::nullopt;
  const auto master = std::filesystem::path(dir) / "Fallout4.esm";
  if (!std::filesystem::exists(master)) return std::nullopt;
  const std::vector<std::filesystem::path> paths{master};
  return fo4::env::Environment::load(paths);
}

fo4::FormKey vanilla(std::uint32_t local) {
  return fo4::FormKey{fo4::PluginKey::from_filename("Fallout4.esm"), fo4::LocalFormId{local}};
}

fo4::forms::NpcVisual decode(const fo4::env::Environment& env,
                             std::uint32_t local,
                             std::uint16_t level = 1,
                             std::uint64_t seed = 0) {
  const auto* ref = env.winner(vanilla(local));
  EXPECT_NE(ref, nullptr);
  return fo4::forms::decode_npc_visual(env, *ref, fo4::forms::NpcVisualOptions{level, seed});
}

}  // namespace

TEST(NpcVisualReal, CaitIsAFemaleHumanWithAFaceGenHeadAndTheHumanSkeleton) {
  const auto env = load_fallout4();
  if (!env) GTEST_SKIP() << "set FO4_DATA_DIR to the Fallout 4 Data folder to run this";
  const auto visual = decode(*env, 0x79249);
  EXPECT_TRUE(visual.female);
  EXPECT_EQ(visual.race_editor_id, std::optional<std::string>("HumanRace"));
  EXPECT_EQ(visual.skeleton_hkx_entry,
            std::optional<std::string>("meshes/actors/character/characterassets/skeleton.hkx"));
  EXPECT_EQ(visual.idle_clip_entry,
            std::optional<std::string>("meshes/actors/character/animations/h2h/idle.hkx"));
  ASSERT_FALSE(visual.parts.empty());
  EXPECT_EQ(visual.parts.front().source, fo4::forms::NpcVisualSource::facegen);
  EXPECT_EQ(visual.parts.front().mesh_entry,
            "meshes/actors/character/facegendata/facegeom/fallout4.esm/00079249.nif");
  EXPECT_TRUE(visual.skin_tint.has_value());
  const auto outfit = std::any_of(visual.parts.begin(), visual.parts.end(), [](const auto& part) {
    return part.source == fo4::forms::NpcVisualSource::outfit &&
           part.mesh_entry == "meshes/clothes/cait/outfitf.nif";
  });
  EXPECT_TRUE(outfit);
}

TEST(NpcVisualReal, TemplatedSuperMutantUsesTheTemplateFaceAndItsOwnSkeleton) {
  const auto env = load_fallout4();
  if (!env) GTEST_SKIP() << "set FO4_DATA_DIR to the Fallout 4 Data folder to run this";
  const auto visual = decode(*env, 0x2483E8);
  EXPECT_EQ(visual.skeleton_hkx_entry,
            std::optional<std::string>("meshes/actors/supermutant/characterassets/skeleton.hkx"));
  EXPECT_FALSE(visual.idle_clip_entry.has_value());
  ASSERT_FALSE(visual.parts.empty());
  EXPECT_EQ(visual.parts.front().mesh_entry,
            "meshes/actors/character/facegendata/facegeom/fallout4.esm/0001A00C.nif");
}

TEST(NpcVisualReal, LeveledTemplateGunnersResolveToAPlacedNpc) {
  const auto env = load_fallout4();
  if (!env) GTEST_SKIP() << "set FO4_DATA_DIR to the Fallout 4 Data folder to run this";
  const std::pair<std::uint32_t, bool> gunners[] = {{0xDE969, true}, {0xDE965, true}, {0x3467F, false}, {0x34680, false}};
  for (const auto& [local, face_from_template] : gunners) {
    SCOPED_TRACE(local);
    const auto visual = decode(*env, local, 20, 0x5EED);
    ASSERT_FALSE(visual.parts.empty());
    ASSERT_EQ(visual.parts.front().source, fo4::forms::NpcVisualSource::facegen);
    char own[16] = {};
    std::snprintf(own, sizeof(own), "%08X.nif", static_cast<unsigned>(local));
    EXPECT_EQ(visual.parts.front().mesh_entry.ends_with(own), !face_from_template)
        << "a leveled traits template gives the picked NPC's FaceGen, and a gunner without the traits flag keeps its own";
    for (const auto& stub : visual.stubs) {
      if (stub.starts_with("note:")) continue;
      EXPECT_EQ(stub.find("leveled"), std::string::npos) << stub;
      EXPECT_EQ(stub.find("not found"), std::string::npos) << stub;
    }
  }
}

TEST(NpcVisualReal, WhitechapelCharlieGetsHisObjectTemplateParts) {
  const auto env = load_fallout4();
  if (!env) GTEST_SKIP() << "set FO4_DATA_DIR to the Fallout 4 Data folder to run this";
  const auto visual = decode(*env, 0x22687);
  EXPECT_EQ(visual.race_editor_id, std::optional<std::string>("HandyRace"));
  EXPECT_EQ(visual.skeleton_hkx_entry,
            std::optional<std::string>("meshes/actors/robot/characterassets/skeleton.hkx"));
  EXPECT_EQ(visual.idle_clip_entry,
            std::optional<std::string>("meshes/actors/robot/animations/idle.hkx"));
  ASSERT_FALSE(visual.parts.empty()) << "a robot with an OBTS template must not decode to no parts";
  const auto torso = std::any_of(visual.parts.begin(), visual.parts.end(), [](const auto& part) {
    return part.source == fo4::forms::NpcVisualSource::object_template &&
           part.mesh_entry == "meshes/actors/robot/parts/torsohandy.nif";
  });
  EXPECT_TRUE(torso);
  for (const auto& part : visual.parts) {
    EXPECT_TRUE(part.mesh_entry.starts_with("meshes/")) << part.mesh_entry;
    EXPECT_EQ(part.mesh_entry.find('\\'), std::string::npos) << part.mesh_entry;
  }
}

TEST(NpcVisualReal, EveryVanillaObjectTemplateCombinationReads) {
  const auto env = load_fallout4();
  if (!env) GTEST_SKIP() << "set FO4_DATA_DIR to the Fallout 4 Data folder to run this";
  const auto obts = fo4::Signature::from_string("OBTS");
  std::size_t records = 0;
  std::size_t combinations = 0;
  std::size_t unreadable = 0;
  for (const auto* ref : env->winners_of(fo4::Signature::from_string("NPC_"))) {
    const auto carried = static_cast<std::size_t>(
        std::count_if(ref->record->subrecords.begin(), ref->record->subrecords.end(),
                      [obts](const auto& subrecord) { return subrecord.signature == obts; }));
    if (carried == 0) continue;
    ++records;
    const auto& plugin = env->plugin(ref->plugin);
    const auto word = [&](const char* leaf, std::size_t index) {
      const auto field = fo4::schema::scalar_field_descriptor(std::string("NPC_\\ObjectTemplate") + leaf);
      return field ? fo4::record_scalar_text(plugin, *ref->record, *field, index) : std::nullopt;
    };
    std::size_t include_words = 0;
    for (std::size_t index = 0; index < carried; ++index) {
      ++combinations;
      try {
        const auto includes = word("IncludeCount", index);
        if (!includes || !word("ParentCombinationIndex", index)) {
          throw std::runtime_error("object template header rows are missing");
        }
        include_words += static_cast<std::size_t>(std::stoull(*includes));
        if (index + 1 == carried &&
            fo4::record_field_count(plugin, ref->record->form_key, "NPC_\\ObjectTemplateIncludeMod") !=
                std::optional<std::size_t>{include_words}) {
          throw std::runtime_error("include rows do not add up to the stored include counts");
        }
      } catch (const std::exception& error) {
        ++unreadable;
        ADD_FAILURE() << ref->record->form_key.to_string() << " OBTS " << index << ": "
                      << error.what();
      }
    }
  }
  EXPECT_GE(records, 150u) << "only " << records << " NPC_ records carry an OBTS";
  EXPECT_GE(combinations, records);
  EXPECT_EQ(unreadable, 0u) << unreadable << " of " << combinations
                            << " object templates did not read";
}

TEST(NpcVisualReal, VanillaObjectTemplateVisualsHaveNoObjectTemplateStubs) {
  const auto env = load_fallout4();
  if (!env) GTEST_SKIP() << "set FO4_DATA_DIR to the Fallout 4 Data folder to run this";
  const auto obts = fo4::Signature::from_string("OBTS");
  std::size_t templates = 0;
  std::size_t unreadable = 0;
  for (const auto* ref : env->winners_of(fo4::Signature::from_string("NPC_"))) {
    SCOPED_TRACE(ref->record->form_key.to_string());
    const auto carried = std::any_of(ref->record->subrecords.begin(), ref->record->subrecords.end(),
                                     [obts](const auto& subrecord) {
                                       return subrecord.signature == obts;
                                     });
    if (!carried) continue;
    ++templates;
    try {
      const auto visual = fo4::forms::decode_npc_visual(*env, *ref);
      for (const auto& stub : visual.stubs) {
        const auto broken = stub.starts_with("object template ");
        if (broken) ++unreadable;
        EXPECT_FALSE(broken) << stub;
      }
      const auto robot =
          visual.skeleton_hkx_entry == "meshes/actors/robot/characterassets/skeleton.hkx";
      if (robot) {
        const auto parts = std::count_if(visual.parts.begin(), visual.parts.end(),
                                         [](const auto& item) {
                                           return item.source ==
                                                  fo4::forms::NpcVisualSource::object_template;
                                         });
        EXPECT_GT(parts, 0) << "a robot with an OBTS drew no object-template part";
      }
    } catch (const std::exception& error) {
      ADD_FAILURE() << error.what();
    }
  }
  EXPECT_GE(templates, 150u) << "only " << templates << " NPC_ records carry an OBTS";
  EXPECT_EQ(unreadable, 0u) << unreadable << " of " << templates << " templates did not decode";
}

TEST(NpcVisualReal, EveryVanillaNpcDecodesToWellFormedEntries) {
  const auto env = load_fallout4();
  if (!env) GTEST_SKIP() << "set FO4_DATA_DIR to the Fallout 4 Data folder to run this";
  const auto npcs = env->winners_of(fo4::Signature::from_string("NPC_"));
  EXPECT_GT(npcs.size(), 3000u);
  for (const auto* ref : npcs) {
    SCOPED_TRACE(ref->record->form_key.to_string());
    try {
      const auto visual = fo4::forms::decode_npc_visual(*env, *ref);
      std::set<std::string> seen;
      for (const auto& part : visual.parts) {
        EXPECT_TRUE(part.mesh_entry.starts_with("meshes/")) << part.mesh_entry;
        EXPECT_EQ(part.mesh_entry.find('\\'), std::string::npos) << part.mesh_entry;
        EXPECT_TRUE(seen.insert(part.mesh_entry).second) << part.mesh_entry;
        if (part.source == fo4::forms::NpcVisualSource::facegen) {
          EXPECT_EQ(part.biped_slots, 0u);
        } else if (!visual.parts.empty() && visual.parts.front().source == fo4::forms::NpcVisualSource::facegen) {
          EXPECT_EQ(part.biped_slots & 0x7u, 0u) << part.mesh_entry;
        }
      }
      const auto human_idle_skeleton =
          visual.skeleton_hkx_entry == "meshes/actors/character/characterassets/skeleton.hkx";
      const auto robot_idle_skeleton =
          visual.skeleton_hkx_entry == "meshes/actors/robot/characterassets/skeleton.hkx";
      EXPECT_EQ(visual.idle_clip_entry.has_value(), human_idle_skeleton || robot_idle_skeleton);
    } catch (const std::exception& error) {
      ADD_FAILURE() << ref->record->form_key.to_string() << ": " << error.what();
    }
  }
}

// Constraint: lists are found by scanning the winners, so no vanilla form ID is hard-coded.
TEST(NpcVisualReal, VanillaLeveledNpcListsResolveDeterministicallyToNpcs) {
  const auto env = load_fallout4();
  if (!env) GTEST_SKIP() << "set FO4_DATA_DIR to the Fallout 4 Data folder to run this";
  const auto lists = env->winners_of(fo4::Signature::from_string("LVLN"));
  ASSERT_FALSE(lists.empty());
  for (const auto* ref : lists) {
    SCOPED_TRACE(ref->record->form_key.to_string());
    for (const std::uint16_t level : {std::uint16_t{1}, std::uint16_t{50}}) {
      for (std::uint64_t seed = 0; seed < 8; ++seed) {
        const auto first = fo4::forms::pick_leveled(*env, ref->record->form_key, level, seed);
        const auto again = fo4::forms::pick_leveled(*env, ref->record->form_key, level, seed);
        EXPECT_EQ(first.entries, again.entries);
        EXPECT_EQ(first.reason, again.reason);
        for (const auto& entry : first.entries) {
          EXPECT_EQ(fo4::forms::spawn_base_kind_of(*env, entry), fo4::forms::SpawnBaseKind::npc)
              << entry.to_string();
        }
      }
    }
  }
}

// Constraint: vanilla places no LVLN base directly, so the placed-actor path is pinned on NPC_ bases.
TEST(NpcVisualReal, VanillaPlacedActorBaseResolvesToItself) {
  const auto env = load_fallout4();
  if (!env) GTEST_SKIP() << "set FO4_DATA_DIR to the Fallout 4 Data folder to run this";
  std::size_t resolved = 0;
  for (const auto* ref : env->winners_of(fo4::Signature::from_string("ACHR"))) {
    if (resolved >= 5) break;
    const auto spawn = fo4::forms::decode_placed_actor_spawn(*env, *ref, 20, 0);
    if (spawn.base_kind != fo4::forms::SpawnBaseKind::npc) continue;
    SCOPED_TRACE(ref->record->form_key.to_string());
    EXPECT_EQ(spawn.resolved_base, spawn.base);
    ++resolved;
  }
  EXPECT_GE(resolved, 1u);
}
