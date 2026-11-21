#include "dialogue_writer_fixture.hpp"
#include "fo4/forms/scene.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <cstddef>
#include <string_view>
#include <utility>
#include <tuple>
#include <vector>

namespace {

using namespace fo4::test::dialogue;
using Scalar = fo4::ScalarMutationStatus;
using Link = fo4::FormLinkFieldMutationStatus;

Scalar set(fo4::Plugin& plugin, const char* path, std::size_t group, const char* value, std::uint32_t id = k_scene) {
  return fo4::set_scene_scalar(plugin, key(id), path, group, value);
}

inline constexpr std::uint32_t k_subtype = 0x830;

Bytes payload_scene(bool malformed_package = false) {
  fo4::test::ByteWriter out;
  const auto tes4 = fo4::test::begin_record(out, "TES4", 0, 0);
  fo4::test::ByteWriter hedr;
  hedr.f32(1.0f);
  hedr.u32(0);
  hedr.u32(0x900);
  fo4::test::write_subrecord(out, "HEDR", hedr.bytes);
  fo4::test::end_record(out, tes4);
  const auto quests = fo4::test::begin_group(out, "QUST", 0);
  record(out, "QUST", k_quest, {{"EDID", zstr("Quest")}});
  record(out, "DIAL", k_topic, {{"EDID", zstr("Topic")}});
  record(out, "DIAL", k_bare, {{"EDID", zstr("Bare")}});
  record(out, "SCEN", k_scene,
         {
             {"EDID", zstr("Payload scene")},
             {"ANAM", halves({0})}, {"NAM0", zstr("")}, {"ALID", words({0})},
             {"DATA", words({k_topic})}, {"DMIN", f32s({1.0f})}, {"DMAX", f32s({3.5f})},
             {"CRIS", f32s({40.0f, 0.5f})}, {"DEMO", words({2})}, {"DEVA", words({60})},
             {"VENC", words({k_kywd})}, {"ONAM", words({k_sopm})}, {"PNAM", words({k_kywd})},
             {"ANAM", {}},
             {"ANAM", halves({2})}, {"NAM0", zstr("")}, {"ALID", words({0xFFFFFFFF})},
             {"SNAM", words({0})}, {"SNAM", f32s({5.0f})}, {"TNAM", f32s({1.0f})},
             {"ANAM", {}},
             {"ANAM", halves({3})}, {"NAM0", zstr("")}, {"ALID", words({0})},
             {"PTOP", words({k_topic})}, {"PLVD", words({k_kywd})}, {"DTGT", words({0xFFFFFFFF})},
             {"NPOS", words({k_kywd})},
             {"ANAM", {}},
             {"ANAM", halves({6})}, {"NAM0", zstr("")}, {"ALID", words({0})},
             {"DATA", words({k_topic})}, {"HTID", words({k_sndr})}, {"DMAX", f32s({2.0f})},
             {"VENC", words({k_kywd})},
             {"ANAM", {}},
             {"ANAM", halves({4})}, {"NAM0", zstr("")}, {"ALID", words({0})},
             {"STSC", words({k_scene})},
             {"ANAM", {}},
             {"ANAM", halves({1})}, {"NAM0", zstr("")}, {"ALID", words({0})},
             {"PNAM", malformed_package ? halves({1}) : words({k_pack})},
             {"ANAM", {}},
             {"PNAM", words({k_quest})}, {"INAM", words({5})}, {"VNAM", words({0, 0, 0, 0})},
         });
  record(out, "SCEN", k_template, {{"EDID", zstr("Other scene")}});
  fo4::test::end_group(out, quests);
  struct Target {
    const char* signature;
    std::uint32_t id;
    const char* editor_id;
  };
  const Target targets[] = {{"KYWD", k_kywd, "Keyword"}, {"KYWD", k_subtype, "Subtype"}, {"SOPM", k_sopm, "Output"},
                            {"SNDR", k_sndr, "Sound"},   {"PACK", k_pack, "Package"}};
  for (const auto& target : targets) {
    const auto group = fo4::test::begin_group(out, target.signature, 0);
    record(out, target.signature, target.id, {{"EDID", zstr(target.editor_id)}});
    fo4::test::end_group(out, group);
  }
  return out.bytes;
}

}

// Detects: a payload row whose exposed signature must be the subrecord the writer stores, so a reader keyed on it cannot miss the member.
TEST(SceneWriter, PayloadRowsExposeTheSubrecordTheWriterStores) {
  const std::pair<std::string_view, std::string_view> expected[] = {
      {"SCEN\\Action\\Topic", "DATA"},
      {"SCEN\\Action\\DialogueSubtype", "VENC"},
      {"SCEN\\Action\\AnimArchType", "PNAM"},
      {"SCEN\\Action\\AudioOutputOverride", "ONAM"},
      {"SCEN\\Action\\PlayerPositiveDialogueSubtype", "VENC"},
      {"SCEN\\Action\\PlayerNegativeDialogueSubtype", "PLVD"},
      {"SCEN\\Action\\PlayerQuestionDialogueSubtype", "DALC"},
      {"SCEN\\Action\\NpcQuestionDialogueSubtype", "NQUS"},
      {"SCEN\\Action\\StartSceneTopic", "STSC"},
      {"SCEN\\Action\\PlaySound", "HTID"},
      {"SCEN\\Action\\LoopingMax", "DMAX"},
      {"SCEN\\Action\\TimerMax", "SNAM"},
      {"SCEN\\Action\\CameraRateOfChange", "CRIS"},
      {"SCEN\\Action\\DialogueTargetActor", "DTGT"},
  };
  const auto rows = fo4::scene_action_payload_paths();
  for (const auto& row : rows) EXPECT_FALSE(row.signature.to_string().empty()) << row.path;
  for (const auto& [path, tag] : expected) {
    const fo4::ScenePayloadPath* found = nullptr;
    for (const auto& row : rows) {
      if (row.path == path) found = &row;
    }
    ASSERT_NE(found, nullptr) << path;
    EXPECT_EQ(found->signature.to_string(), tag) << path;
  }
}

// Regression guard: create_scene must append the SCEN inside the quest children group with its flags, parent quest, index and tail members.
TEST(SceneWriter, CreateSceneAppendsASceneWithItsFlagsParentQuestAndIndexAndSurvivesReload) {
  auto plugin = fo4::test::dialogue::read(build());
  const auto created = fo4::forms::create_scene(plugin, key(k_quest), fo4::forms::SceneSpec{"Made scene", 0x24, 7});
  ASSERT_EQ(created.status, fo4::forms::SceneAuthorStatus::ok);
  const auto id = created.key.local().value();
  const auto saved = reload(plugin);
  EXPECT_EQ(order(saved, id), (Order{"EDID", "FNAM", "PNAM", "INAM", "VNAM", "XNAM"}));
  EXPECT_EQ(payloads(saved, id, "EDID"), std::vector<Bytes>{zstr("Made scene")});
  EXPECT_EQ(payloads(saved, id, "FNAM"), std::vector<Bytes>{words({0x24})});
  EXPECT_EQ(payloads(saved, id, "PNAM"), std::vector<Bytes>{words({k_quest})});
  EXPECT_EQ(payloads(saved, id, "XNAM"), std::vector<Bytes>{words({7})});
}

// Regression guard: create_scene must refuse a non-QUST, master-owned or embedded-NUL request without changing any byte.
TEST(SceneWriter, CreateSceneRefusesUnknownAndMasterOwnedQuestsAndLeavesThePluginAlone) {
  auto plugin = fo4::test::dialogue::read(build());
  const auto before = fo4::write_plugin(plugin);
  const fo4::FormKey master_quest{fo4::PluginKey::from_filename("Other.esm"), fo4::LocalFormId{k_quest}};
  EXPECT_EQ(fo4::forms::create_scene(plugin, key(k_topic), fo4::forms::SceneSpec{"Made scene"}).status,
            fo4::forms::SceneAuthorStatus::quest_missing);
  EXPECT_EQ(fo4::forms::create_scene(plugin, master_quest, fo4::forms::SceneSpec{"Made scene"}).status,
            fo4::forms::SceneAuthorStatus::not_local);
  EXPECT_EQ(fo4::forms::create_scene(plugin, key(k_quest), fo4::forms::SceneSpec{std::string("a\0b", 3)}).status,
            fo4::forms::SceneAuthorStatus::invalid_argument);
  EXPECT_EQ(fo4::write_plugin(plugin), before);
}

TEST(SceneWriter, GroupFieldsAreAddressedByExplicitGroupIndex) {
  auto plugin = read(build());
  EXPECT_EQ(fo4::scene_group_count(plugin, key(k_scene), fo4::ScenePart::phase), 2u);
  EXPECT_EQ(fo4::scene_group_count(plugin, key(k_scene), fo4::ScenePart::actor), 2u);
  EXPECT_EQ(fo4::scene_group_count(plugin, key(k_scene), fo4::ScenePart::action), 3u);
  EXPECT_EQ(fo4::scene_group_count(plugin, key(k_template), fo4::ScenePart::action), 0u);
  EXPECT_FALSE(fo4::scene_group_count(plugin, key(0x8FF), fo4::ScenePart::phase).has_value());

  EXPECT_EQ(set(plugin, "SCEN\\Flags", 0, "37"), Scalar::updated);
  EXPECT_EQ(set(plugin, "SCEN\\LastActionIndex", 0, "4"), Scalar::updated);
  EXPECT_EQ(set(plugin, "SCEN\\FovOverride", 0, "70"), Scalar::updated);
  EXPECT_EQ(set(plugin, "SCEN\\ParentQuestStageOnBegin", 0, "5"), Scalar::updated);
  EXPECT_EQ(set(plugin, "SCEN\\Phase\\EditorWidth", 1, "400"), Scalar::updated);
  EXPECT_EQ(set(plugin, "SCEN\\Phase\\Flags", 0, "4"), Scalar::updated);
  EXPECT_EQ(set(plugin, "SCEN\\Phase\\Flags", 1, "4"), Scalar::subrecord_missing);
  EXPECT_EQ(set(plugin, "SCEN\\Phase\\QuestStageOnCompletion", 0, "-1"), Scalar::updated);
  EXPECT_EQ(set(plugin, "SCEN\\Phase\\EditorWidth", 2, "1"), Scalar::subrecord_missing);
  EXPECT_EQ(set(plugin, "SCEN\\Actor\\Alias", 1, "6"), Scalar::updated);
  EXPECT_EQ(set(plugin, "SCEN\\Actor\\Flags", 0, "3"), Scalar::updated);
  EXPECT_EQ(set(plugin, "SCEN\\Actor\\BehaviorFlags", 1, "10"), Scalar::updated);
  EXPECT_EQ(set(plugin, "SCEN\\Action\\ActorAlias", 2, "0"), Scalar::updated);
  EXPECT_EQ(set(plugin, "SCEN\\Action\\Index", 0, "9"), Scalar::updated);
  EXPECT_EQ(set(plugin, "SCEN\\Action\\Flags", 0, "0"), Scalar::updated);
  EXPECT_EQ(set(plugin, "SCEN\\Action\\Flags", 1, "0"), Scalar::subrecord_missing);
  EXPECT_EQ(set(plugin, "SCEN\\Action\\StartPhase", 1, "0"), Scalar::updated);
  EXPECT_EQ(set(plugin, "SCEN\\Action\\EndPhase", 1, "1"), Scalar::subrecord_missing);
  EXPECT_EQ(set(plugin, "SCEN\\Action\\EndPhase", 2, "4294967295"), Scalar::updated);
  EXPECT_EQ(set(plugin, "SCEN\\Action\\EndPhase", 3, "0"), Scalar::subrecord_missing);
  EXPECT_EQ(set(plugin, "SCEN\\Flags", 1, "0"), Scalar::subrecord_missing);
  EXPECT_EQ(set(plugin, "SCEN\\Flags", 0, "37"), Scalar::unchanged);

  const auto reloaded = reload(plugin);
  EXPECT_EQ(payloads(reloaded, k_scene, "CRIS").at(0), f32s({1.0f, 2.0f}));
  EXPECT_EQ(payloads(reloaded, k_scene, "CRIS").at(1), f32s({70.0f}));
  EXPECT_EQ(payloads(reloaded, k_scene, "SNAM").at(2), f32s({5.0f}));
  EXPECT_EQ(payloads(reloaded, k_scene, "SCQS").at(1), halves({5, 0xFFFF}));
  EXPECT_EQ(payloads(reloaded, k_scene, "INAM").back(), words({4}));
  const SavedEnvironment saved(reloaded);
  const auto scene = fo4::env::decode_scene(saved.env(), saved.record(k_scene));
  EXPECT_EQ(scene.flags, 37u);
  ASSERT_EQ(scene.phases.size(), 2u);
  EXPECT_EQ(scene.phases[0].flags, 4);
  EXPECT_EQ((*scene.phases[0].quest_stages)[1], -1);
  EXPECT_EQ(scene.phases[1].editor_width, 400u);
  ASSERT_EQ(scene.actors.size(), 2u);
  EXPECT_EQ(scene.actors[0].flags, 3u);
  EXPECT_EQ(scene.actors[1].alias, 6);
  EXPECT_EQ(scene.actors[1].behavior_flags, 10u);
  ASSERT_EQ(scene.actions.size(), 3u);
  EXPECT_EQ(scene.actions[0].index, 9u);
  EXPECT_EQ(scene.actions[0].flags, 0u);
  EXPECT_EQ(scene.actions[1].start_phase, 0u);
  EXPECT_FALSE(scene.actions[1].end_phase.has_value());
  EXPECT_EQ(scene.actions[2].end_phase, 0xFFFFFFFFu);
  EXPECT_EQ(scene.actions[2].actor_alias, 0);
}

TEST(SceneWriter, LinksAndRefusalsChangeNoByte) {
  auto plugin = read(build());
  EXPECT_EQ(fo4::set_scene_formlink(plugin, key(k_scene), "SCEN\\TemplateScene", key(k_scene)), Link::updated);
  EXPECT_EQ(fo4::set_scene_formlink(plugin, key(k_scene), "SCEN\\ParentQuest", key(k_quest)), Link::unchanged);
  EXPECT_EQ(fo4::set_scene_formlink(plugin, key(k_template), "SCEN\\TemplateScene", key(k_scene)),
            Link::subrecord_missing);
  EXPECT_EQ(fo4::set_scene_formlink(plugin, key(k_topic), "SCEN\\ParentQuest", key(k_quest)), Link::record_mismatch);
  EXPECT_EQ(fo4::set_scene_formlink(plugin, key(0x8FF), "SCEN\\ParentQuest", key(k_quest)), Link::record_missing);
  EXPECT_EQ(payloads(plugin, k_scene, "TNAM").at(1), words({k_scene}));
  EXPECT_EQ(payloads(plugin, k_scene, "PNAM").at(0), words({k_kywd}));

  EXPECT_TRUE(refused(plugin, [&] { fo4::set_scene_formlink(plugin, key(k_scene), "SCEN\\ParentQuest", key(k_kywd)); }));
  EXPECT_TRUE(refused(plugin, [&] { fo4::set_scene_formlink(plugin, key(k_scene), "SCEN\\ParentQuest", fo4::FormKey{}); }));
  EXPECT_TRUE(refused(plugin, [&] { fo4::set_scene_formlink(plugin, key(k_scene), "SCEN\\Keyword", key(k_kywd)); }));
  EXPECT_TRUE(refused(plugin, [&] { set(plugin, "SCEN\\Phase\\Flags", 0, "65536"); }));
  EXPECT_TRUE(refused(plugin, [&] { set(plugin, "SCEN\\Phase\\QuestStageOnStart", 0, "32768"); }));
  EXPECT_TRUE(refused(plugin, [&] { set(plugin, "SCEN\\Action\\StartPhase", 0, "-1"); }));
  EXPECT_TRUE(refused(plugin, [&] { set(plugin, "SCEN\\Action\\Type", 0, "1"); }));
  EXPECT_TRUE(refused(plugin, [&] { set(plugin, "SCEN\\Actor\\Alias", 0, "1", k_topic); }));
  EXPECT_EQ(set(plugin, "SCEN\\Flags", 0, "1", 0x8FF), Scalar::record_missing);
}

TEST(SceneWriter, ActionPayloadRowsFollowTheStoredActionType) {
  auto plugin = read(payload_scene());
  EXPECT_EQ(set(plugin, "SCEN\\Action\\LoopingMax", 0, "7.5"), Scalar::updated);
  EXPECT_EQ(set(plugin, "SCEN\\Action\\LoopingMin", 0, "-2.5"), Scalar::updated);
  EXPECT_EQ(set(plugin, "SCEN\\Action\\CameraFovOnPlayerCamera", 0, "55"), Scalar::updated);
  EXPECT_EQ(set(plugin, "SCEN\\Action\\CameraRateOfChange", 0, "0.25"), Scalar::updated);
  EXPECT_EQ(set(plugin, "SCEN\\Action\\EmotionType", 0, "3"), Scalar::updated);
  EXPECT_EQ(set(plugin, "SCEN\\Action\\EmotionValue", 0, "80"), Scalar::updated);
  EXPECT_EQ(set(plugin, "SCEN\\Action\\TimerMax", 1, "9"), Scalar::updated);
  EXPECT_EQ(set(plugin, "SCEN\\Action\\TimerMin", 1, "0.5"), Scalar::updated);
  EXPECT_EQ(set(plugin, "SCEN\\Action\\DialogueTargetActor", 2, "4"), Scalar::updated);
  EXPECT_EQ(set(plugin, "SCEN\\Action\\LoopingMax", 3, "2.5"), Scalar::updated);
  EXPECT_EQ(set(plugin, "SCEN\\Action\\CameraRateOfChange", 0, "0.25"), Scalar::unchanged);
  EXPECT_EQ(set(plugin, "SCEN\\Action\\LoopingMax", 1, "2.5"), Scalar::subrecord_missing);
  EXPECT_EQ(set(plugin, "SCEN\\Action\\EmotionType", 3, "2"), Scalar::subrecord_missing);
  EXPECT_EQ(set(plugin, "SCEN\\Action\\TimerMax", 0, "3"), Scalar::subrecord_missing);
  EXPECT_EQ(set(plugin, "SCEN\\Action\\DialogueTargetActor", 0, "1"), Scalar::subrecord_missing);
  EXPECT_EQ(set(plugin, "SCEN\\Action\\LoopingMax", 9, "1"), Scalar::subrecord_missing);
  EXPECT_TRUE(refused(plugin, [&] { set(plugin, "SCEN\\Action\\LoopingMax", 0, "x"); }));

  EXPECT_EQ(fo4::set_scene_action_formlink(plugin, key(k_scene), 0, "SCEN\\Action\\Topic", key(k_bare)),
            Link::updated);
  EXPECT_EQ(fo4::set_scene_action_formlink(plugin, key(k_scene), 0, "SCEN\\Action\\DialogueSubtype", key(k_subtype)),
            Link::updated);
  EXPECT_EQ(fo4::set_scene_action_formlink(plugin, key(k_scene), 0, "SCEN\\Action\\AnimArchType", key(k_kywd)),
            Link::unchanged);
  EXPECT_EQ(
      fo4::set_scene_action_formlink(plugin, key(k_scene), 2, "SCEN\\Action\\PlayerNegativeDialogueSubtype", key(k_subtype)),
      Link::updated);
  EXPECT_EQ(fo4::set_scene_action_formlink(plugin, key(k_scene), 4, "SCEN\\Action\\StartSceneTopic", key(k_bare)),
            Link::updated);
  EXPECT_EQ(fo4::set_scene_action_formlink(plugin, key(k_scene), 3, "SCEN\\Action\\PlaySound", key(k_sndr)),
            Link::unchanged);
  EXPECT_EQ(fo4::set_scene_action_formlink(plugin, key(k_scene), 3, "SCEN\\Action\\Topic", key(k_bare)),
            Link::updated);
  EXPECT_EQ(fo4::set_scene_action_formlink(plugin, key(k_scene), 5, "SCEN\\Action\\AnimArchType", key(k_kywd)),
            Link::subrecord_missing);
  EXPECT_EQ(fo4::set_scene_action_formlink(plugin, key(k_scene), 2, "SCEN\\Action\\Topic", key(k_topic)),
            Link::subrecord_missing);
  EXPECT_EQ(fo4::set_scene_action_formlink(plugin, key(k_scene), 9, "SCEN\\Action\\Topic", key(k_topic)),
            Link::subrecord_missing);
  EXPECT_EQ(fo4::set_scene_action_formlink(plugin, key(0x8FF), 0, "SCEN\\Action\\Topic", key(k_topic)),
            Link::record_missing);
  EXPECT_EQ(fo4::set_scene_action_formlink(plugin, key(k_topic), 0, "SCEN\\Action\\Topic", key(k_topic)),
            Link::record_mismatch);
  EXPECT_TRUE(refused(plugin, [&] {
    fo4::set_scene_action_formlink(plugin, key(k_scene), 0, "SCEN\\Action\\Topic", key(k_quest));
  }));
  // Detects: a dialogue topic must allow NULL without weakening other link target checks.
  EXPECT_EQ(fo4::set_scene_action_formlink(plugin, key(k_scene), 0, "SCEN\\Action\\Topic", fo4::FormKey{}), Link::updated);
  EXPECT_EQ(payloads(reload(plugin), k_scene, "DATA").at(0), words({0}));
  EXPECT_EQ(fo4::set_scene_action_formlink(plugin, key(k_scene), 0, "SCEN\\Action\\Topic", key(k_bare)), Link::updated);
  EXPECT_TRUE(refused(plugin, [&] {
    fo4::set_scene_action_formlink(plugin, key(k_scene), 3, "SCEN\\Action\\Topic", fo4::FormKey{});
  }));
  EXPECT_TRUE(refused(plugin, [&] {
    fo4::set_scene_action_formlink(plugin, key(k_scene), 0, "SCEN\\Action\\DialogueSubtype", fo4::FormKey{});
  }));
  EXPECT_TRUE(refused(plugin, [&] {
    fo4::set_scene_action_formlink(plugin, key(k_scene), 0, "SCEN\\ParentQuest", key(k_quest));
  }));
  EXPECT_TRUE(refused(plugin, [&] { fo4::set_scene_formlink(plugin, key(k_scene), "SCEN\\Action\\Topic", key(k_topic)); }));
  EXPECT_TRUE(refused(plugin, [&] {
    fo4::set_scene_action_formlink(plugin, key(k_scene), 0, "SCEN\\Nope", key(k_topic));
  }));
  EXPECT_TRUE(refused(plugin, [&] { fo4::set_scene_action_formlink(plugin, key(k_scene), 0, "SCEN\\Action\\DTGT", key(k_topic)); }));

  const auto reloaded = reload(plugin);
  EXPECT_EQ(payloads(reloaded, k_scene, "DMAX").at(0), f32s({7.5f}));
  EXPECT_EQ(payloads(reloaded, k_scene, "DMAX").at(1), f32s({2.5f}));
  EXPECT_EQ(payloads(reloaded, k_scene, "DMIN").at(0), f32s({-2.5f}));
  EXPECT_EQ(payloads(reloaded, k_scene, "CRIS").at(0), f32s({55.0f, 0.25f}));
  EXPECT_EQ(payloads(reloaded, k_scene, "DEMO").at(0), words({3}));
  EXPECT_EQ(payloads(reloaded, k_scene, "DEVA").at(0), words({80}));
  EXPECT_EQ(payloads(reloaded, k_scene, "SNAM").at(0), words({0}));
  EXPECT_EQ(payloads(reloaded, k_scene, "SNAM").at(1), f32s({9.0f}));
  EXPECT_EQ(payloads(reloaded, k_scene, "TNAM").at(0), f32s({0.5f}));
  EXPECT_EQ(payloads(reloaded, k_scene, "DTGT").at(0), words({4}));
  EXPECT_EQ(payloads(reloaded, k_scene, "DATA").at(0), words({k_bare}));
  EXPECT_EQ(payloads(reloaded, k_scene, "DATA").at(1), words({k_bare}));
  EXPECT_EQ(payloads(reloaded, k_scene, "VENC").at(0), words({k_subtype}));
  EXPECT_EQ(payloads(reloaded, k_scene, "VENC").at(1), words({k_kywd}));
  EXPECT_EQ(payloads(reloaded, k_scene, "STSC").at(0), words({k_bare}));
  EXPECT_EQ(payloads(reloaded, k_scene, "HTID").at(0), words({k_sndr}));
  EXPECT_EQ(payloads(reloaded, k_scene, "PNAM").at(0), words({k_kywd}));
  EXPECT_EQ(payloads(reloaded, k_scene, "PNAM").at(1), words({k_pack}));
  ASSERT_EQ(fo4::scene_group_count(reloaded, key(k_scene), fo4::ScenePart::action), 6u);

  const SavedEnvironment saved(reloaded);
  const auto scene = fo4::env::decode_scene(saved.env(), saved.record(k_scene));
  ASSERT_EQ(scene.actions.size(), 6u);
  EXPECT_FALSE(scene.actions[0].index.has_value());
  EXPECT_EQ(scene.actions[1].payload.size(), 2u);
  EXPECT_EQ(scene.actions[2].payload.size(), 4u);
  EXPECT_EQ(scene.actions[2].payload.front().signature.to_string(), "PTOP");
  EXPECT_EQ(scene.actions[5].actor_alias, 0);
}

// Detects: package-list edits must stay within the selected action and preserve unrelated PNAM fields.
TEST(SceneWriter, PackageActionListsRoundTripWithoutChangingOtherScopes) {
  auto plugin = read(payload_scene());
  const auto replace = [&](std::size_t action, std::vector<fo4::FormKey> packages) {
    return fo4::set_scene_action_packages(plugin, key(k_scene), action, packages);
  };
  EXPECT_EQ(replace(5, {key(k_pack), key(k_pack)}), Link::updated);
  EXPECT_EQ(replace(5, {key(k_pack), key(k_pack)}), Link::unchanged);
  EXPECT_EQ(replace(0, {key(k_pack)}), Link::subrecord_missing);
  EXPECT_EQ(replace(9, {key(k_pack)}), Link::subrecord_missing);
  EXPECT_TRUE(refused(plugin, [&] { replace(5, {key(k_quest)}); }));
  EXPECT_TRUE(refused(plugin, [&] { replace(5, {key(k_pack), key(k_quest)}); }));
  EXPECT_TRUE(refused(plugin, [&] { replace(5, {fo4::FormKey{}}); }));
  auto saved = reload(plugin);
  EXPECT_EQ(payloads(saved, k_scene, "PNAM"),
            (std::vector<Bytes>{words({k_kywd}), words({k_pack}), words({k_pack}), words({k_quest})}));
  EXPECT_EQ(replace(5, {}), Link::updated);
  saved = reload(plugin);
  EXPECT_EQ(payloads(saved, k_scene, "PNAM"), (std::vector<Bytes>{words({k_kywd}), words({k_quest})}));
  EXPECT_EQ(replace(5, {key(k_pack)}), Link::updated);
  saved = reload(plugin);
  EXPECT_EQ(payloads(saved, k_scene, "PNAM"),
            (std::vector<Bytes>{words({k_kywd}), words({k_pack}), words({k_quest})}));
}

// Detects: malformed package references must not be replaced or partially written.
TEST(SceneWriter, PackageListRefusesMalformedStoredEntries) {
  auto plugin = read(payload_scene(true));
  EXPECT_TRUE(refused(plugin, [&] {
    fo4::set_scene_action_packages(plugin, key(k_scene), 5, std::vector<fo4::FormKey>{key(k_pack)});
  }));
}

// Detects: separated package entries must not be silently merged across unknown payloads.
TEST(SceneWriter, PackageListRefusesSeparatedEntries) {
  auto plugin = read(payload_scene());
  fo4::visit_records(plugin, [&](fo4::Record& record) {
    if (record.form_key != key(k_scene)) return;
    for (auto it = record.subrecords.begin(); it != record.subrecords.end(); ++it) {
      if (it->signature != sig("PNAM") || it->data != words({k_pack})) continue;
      it = record.subrecords.insert(it + 1, fo4::Subrecord{sig("ZZZZ"), words({42}), false});
      record.subrecords.insert(it + 1, fo4::Subrecord{sig("PNAM"), words({k_pack}), false});
      record.original_range.clear();
      record.decompressed_payload.clear();
      plugin.original_range.clear();
      break;
    }
  });
  EXPECT_TRUE(refused(plugin, [&] {
    fo4::set_scene_action_packages(plugin, key(k_scene), 5, std::vector<fo4::FormKey>{key(k_pack)});
  }));
  EXPECT_TRUE(refused(plugin, [&] { fo4::read_scene_action_packages(plugin, key(k_scene), 5); }));
}

TEST(SceneWriter, MalformedOrDuplicatedScopesAreRefused) {
  fo4::test::ByteWriter out;
  const auto tes4 = fo4::test::begin_record(out, "TES4", 0, 0);
  fo4::test::ByteWriter hedr;
  hedr.f32(1.0f);
  hedr.u32(0);
  hedr.u32(0x900);
  fo4::test::write_subrecord(out, "HEDR", hedr.bytes);
  fo4::test::end_record(out, tes4);
  const auto quests = fo4::test::begin_group(out, "QUST", 0);
  record(out, "QUST", 0x802, {{"EDID", zstr("Q")}});
  fo4::test::end_group(out, quests);
  const auto group = fo4::test::begin_group(out, "SCEN", 0);
  record(out, "SCEN", 0x800, {{"ANAM", halves({0})}, {"SNAM", words({0})}});
  record(out, "SCEN", 0x801, {{"ALID", words({0})}, {"LNAM", words({0})}, {"DNAM", words({0})}, {"PNAM", words({0x802})},
                              {"PNAM", words({0x802})}, {"INAM", halves({1})}});
  fo4::test::end_group(out, group);
  auto plugin = read(out.bytes);
  EXPECT_TRUE(refused(plugin, [&] { set(plugin, "SCEN\\Action\\StartPhase", 0, "1", 0x800); }));
  EXPECT_THROW(static_cast<void>(fo4::scene_group_count(plugin, key(0x800), fo4::ScenePart::action)), fo4::schema::ScalarError);
  EXPECT_TRUE(refused(plugin, [&] { fo4::set_scene_formlink(plugin, key(0x801), "SCEN\\ParentQuest", key(0x802)); }));
  EXPECT_TRUE(refused(plugin, [&] { set(plugin, "SCEN\\LastActionIndex", 0, "1", 0x801); }));
  EXPECT_EQ(set(plugin, "SCEN\\Actor\\Alias", 0, "2", 0x801), Scalar::updated);
}

// Regression guard: ensure_scene_member must place an absent action end phase after the start phase and before the timer payload, so both stay readable in their scopes.
TEST(SceneWriter, EnsureSceneMemberPlacesAnAbsentActionEndPhaseInItsHeader) {
  auto plugin = read(build());
  EXPECT_EQ(fo4::ensure_scene_member(plugin, key(k_scene), "SCEN\\Action\\EndPhase", 1), Scalar::updated);
  EXPECT_EQ(fo4::ensure_scene_member(plugin, key(k_scene), "SCEN\\Action\\EndPhase", 1), Scalar::unchanged);
  EXPECT_EQ(set(plugin, "SCEN\\Action\\StartPhase", 1, "1"), Scalar::unchanged);
  EXPECT_EQ(set(plugin, "SCEN\\Action\\EndPhase", 1, "1"), Scalar::updated);
  EXPECT_EQ(set(plugin, "SCEN\\Action\\TimerMax", 1, "2.5"), Scalar::updated);
  const auto saved = reload(plugin);
  EXPECT_EQ(payloads(saved, k_scene, "ENAM"), (std::vector<Bytes>{words({1}), words({1}), words({1})}));
  EXPECT_EQ(payloads(saved, k_scene, "SNAM"),
            (std::vector<Bytes>{words({0}), words({1}), f32s({2.5f}), words({0})}));
}

// Regression guard: a timer max written before its start phase is read as the start phase, so ensure_scene_member must write the header start phase first.
TEST(SceneWriter, EnsureSceneMemberWritesATimerStartPhaseBeforeANewTimerMax) {
  auto plugin = read(build());
  EXPECT_EQ(fo4::insert_scene_action(plugin, key(k_scene), 3, 2), fo4::SubrecordGroupMutationStatus::updated);
  EXPECT_EQ(fo4::ensure_scene_member(plugin, key(k_scene), "SCEN\\Action\\TimerMax", 3), Scalar::updated);
  EXPECT_EQ(set(plugin, "SCEN\\Action\\TimerMax", 3, "2.5"), Scalar::updated);
  const auto saved = reload(plugin);
  EXPECT_EQ(payloads(saved, k_scene, "SNAM"),
            (std::vector<Bytes>{words({0}), words({1}), f32s({5.0f}), words({0}), words({0}), f32s({2.5f})}));
}

// Regression guard: ensure_scene_member must refuse a member outside its action type, an out-of-range group, a phase reference with no scene phase, or an unknown path and change no byte.
TEST(SceneWriter, EnsureSceneMemberRefusesMismatchedAndPhaselessMembersWithoutChangingBytes) {
  auto plugin = read(build());
  EXPECT_EQ(fo4::ensure_scene_member(plugin, key(k_scene), "SCEN\\Action\\LoopingMax", 1), Scalar::subrecord_missing);
  EXPECT_EQ(fo4::ensure_scene_member(plugin, key(k_scene), "SCEN\\Action\\EndPhase", 9), Scalar::subrecord_missing);
  EXPECT_EQ(fo4::ensure_scene_member(plugin, key(k_scene), "SCEN\\Phase\\Flags", 0), Scalar::unchanged);
  EXPECT_TRUE(refused(plugin, [&] { fo4::ensure_scene_member(plugin, key(k_scene), "SCEN\\Nope", 0); }));
  auto phaseless = read(payload_scene());
  EXPECT_TRUE(refused(phaseless, [&] { fo4::ensure_scene_member(phaseless, key(k_scene), "SCEN\\Action\\EndPhase", 0); }));
}

namespace {

std::size_t index_of(const Order& names, const char* signature) {
  const auto found = std::find(names.begin(), names.end(), signature);
  return found == names.end() ? names.size() : static_cast<std::size_t>(std::distance(names.begin(), found));
}

std::size_t count_of(const Order& names, const char* signature) {
  return static_cast<std::size_t>(std::count(names.begin(), names.end(), signature));
}

fo4::forms::SceneStartSceneEntry start_scene(std::uint32_t scene, std::uint16_t phase, const char* name) {
  fo4::forms::SceneStartSceneEntry entry;
  entry.scene = key(scene);
  entry.phase_index = phase;
  if (name != nullptr) entry.phase_name = std::string(name);
  return entry;
}

fo4::CtdaCondition start_scene_condition(std::uint16_t function) {
  fo4::CtdaCondition condition;
  condition.function = function;
  return condition;
}

}  // namespace

// Regression guard: SceneStartSceneEntry was declared with no reader or writer, so a type-4 Start Scene payload must be authorable through it, not only as raw bytes with every LCEP/INTT/SSPN/CITC/CTDA run invisible.
TEST(SceneWriter, StartSceneEntryAuthoringRoundTripsThroughATypeFourAction) {
  auto plugin = read(build());
  ASSERT_EQ(fo4::insert_scene_action(plugin, key(k_scene), 3, 4), fo4::SubrecordGroupMutationStatus::updated);
  const auto empty = fo4::forms::read_scene_start_scenes(plugin, key(k_scene), 3);
  ASSERT_TRUE(empty.has_value());
  EXPECT_TRUE(empty->empty());

  auto entry = start_scene(k_template, 2, "Start");
  entry.conditions.push_back(start_scene_condition(14));
  EXPECT_EQ(fo4::forms::insert_scene_start_scene(plugin, key(k_scene), 3, 0, entry),
            fo4::forms::SceneStartSceneStatus::ok);

  const auto reloaded = reload(plugin);
  const auto saved = fo4::forms::read_scene_start_scenes(reloaded, key(k_scene), 3);
  ASSERT_TRUE(saved.has_value());
  ASSERT_EQ(saved->size(), 1u);
  EXPECT_EQ(saved->at(0).scene, key(k_template));
  EXPECT_EQ(saved->at(0).phase_index, 2u);
  ASSERT_TRUE(saved->at(0).phase_name.has_value());
  EXPECT_EQ(*saved->at(0).phase_name, "Start");
  ASSERT_EQ(saved->at(0).conditions.size(), 1u);
  EXPECT_EQ(saved->at(0).conditions[0].function, 14);

  const auto names = order(reloaded, k_scene);
  const auto lcep = index_of(names, "LCEP");
  ASSERT_LT(lcep + 5, names.size());
  EXPECT_EQ(names[lcep + 1], "INTT");
  EXPECT_EQ(names[lcep + 2], "SSPN");
  EXPECT_EQ(names[lcep + 3], "CITC");
  EXPECT_EQ(names[lcep + 4], "CTDA");
  EXPECT_EQ(names[lcep + 5], "ANAM");
  EXPECT_EQ(count_of(names, "LCEP"), 1u);
  EXPECT_EQ(count_of(names, "CITC"), 1u);
  EXPECT_EQ(count_of(names, "CTDA"), 3u);
  EXPECT_EQ(payloads(reloaded, k_scene, "LCEP"), (std::vector<Bytes>{words({k_template})}));
  EXPECT_EQ(payloads(reloaded, k_scene, "INTT"), (std::vector<Bytes>{halves({2})}));
  EXPECT_EQ(payloads(reloaded, k_scene, "SSPN"), (std::vector<Bytes>{zstr("Start")}));
  EXPECT_EQ(payloads(reloaded, k_scene, "CITC"), (std::vector<Bytes>{words({1})}));
  EXPECT_EQ(count_of(order(reloaded, k_scene), "ANAM"), 8u);
}

// Regression guard: the type-4 payload ends with an HTID marker, so a Start Scene entry must land ahead of it or the marker moves into the last entry's scope.
TEST(SceneWriter, StartSceneEntriesStayAheadOfTheEndSceneHeadTrackingMarker) {
  auto plugin = read(payload_scene());
  fo4::visit_records(plugin, [&](fo4::Record& record) {
    if (record.form_key != key(k_scene)) return;
    const auto stsc = std::find_if(record.subrecords.begin(), record.subrecords.end(), [](const fo4::Subrecord& sub) {
      return sub.signature == sig("STSC");
    });
    ASSERT_NE(stsc, record.subrecords.end());
    record.subrecords.insert(stsc + 1, fo4::Subrecord{sig("HTID"), {}, false});
    record.original_range.clear();
    record.decompressed_payload.clear();
    plugin.original_range.clear();
  });

  EXPECT_EQ(fo4::forms::insert_scene_start_scene(plugin, key(k_scene), 4, 0, start_scene(k_scene, 0, nullptr)),
            fo4::forms::SceneStartSceneStatus::ok);
  EXPECT_EQ(fo4::forms::insert_scene_start_scene(plugin, key(k_scene), 4, 1, start_scene(k_scene, 3, "Third")),
            fo4::forms::SceneStartSceneStatus::ok);

  const auto reloaded = reload(plugin);
  const auto names = order(reloaded, k_scene);
  const auto stsc = index_of(names, "STSC");
  const auto htid = index_of(names, "HTID");
  ASSERT_LT(htid, names.size());
  ASSERT_LT(stsc + 9, names.size());
  EXPECT_EQ(names[stsc + 1], "LCEP");
  EXPECT_EQ(names[stsc + 2], "INTT");
  EXPECT_EQ(names[stsc + 3], "CITC");
  EXPECT_EQ(names[stsc + 4], "LCEP");
  EXPECT_EQ(names[stsc + 5], "INTT");
  EXPECT_EQ(names[stsc + 6], "SSPN");
  EXPECT_EQ(names[stsc + 7], "CITC");
  EXPECT_EQ(names[stsc + 8], "HTID");
  EXPECT_EQ(names[stsc + 9], "ANAM");
  EXPECT_EQ(payloads(reloaded, k_scene, "HTID").back(), Bytes{});
  EXPECT_EQ(payloads(reloaded, k_scene, "INTT"), (std::vector<Bytes>{halves({0}), halves({3})}));

  const auto entries = fo4::forms::read_scene_start_scenes(reloaded, key(k_scene), 4);
  ASSERT_TRUE(entries.has_value());
  ASSERT_EQ(entries->size(), 2u);
  EXPECT_FALSE(entries->at(0).phase_name.has_value());
  EXPECT_TRUE(entries->at(0).conditions.empty());
  ASSERT_TRUE(entries->at(1).phase_name.has_value());
  EXPECT_EQ(*entries->at(1).phase_name, "Third");
  EXPECT_EQ(entries->at(1).phase_index, 3u);
}

// Regression guard: an update must rewrite every member of one entry (name, phase and conditions) and re-count CITC instead of leaving the old count behind.
TEST(SceneWriter, UpdatingAStartSceneEntryRewritesItsMembersAndCountsConditions) {
  auto plugin = read(build());
  ASSERT_EQ(fo4::insert_scene_action(plugin, key(k_scene), 3, 4), fo4::SubrecordGroupMutationStatus::updated);
  auto entry = start_scene(k_template, 2, "Start");
  entry.conditions.push_back(start_scene_condition(14));
  ASSERT_EQ(fo4::forms::insert_scene_start_scene(plugin, key(k_scene), 3, 0, entry),
            fo4::forms::SceneStartSceneStatus::ok);

  auto rewritten = start_scene(k_template, 5, nullptr);
  EXPECT_EQ(fo4::forms::update_scene_start_scene(plugin, key(k_scene), 3, 0, rewritten),
            fo4::forms::SceneStartSceneStatus::ok);
  EXPECT_EQ(fo4::forms::update_scene_start_scene(plugin, key(k_scene), 3, 0, rewritten),
            fo4::forms::SceneStartSceneStatus::unchanged);

  const auto reloaded = reload(plugin);
  const auto saved = fo4::forms::read_scene_start_scenes(reloaded, key(k_scene), 3);
  ASSERT_TRUE(saved.has_value());
  ASSERT_EQ(saved->size(), 1u);
  EXPECT_EQ(saved->at(0).scene, key(k_template));
  EXPECT_EQ(saved->at(0).phase_index, 5u);
  EXPECT_FALSE(saved->at(0).phase_name.has_value());
  EXPECT_TRUE(saved->at(0).conditions.empty());
  const auto names = order(reloaded, k_scene);
  EXPECT_EQ(count_of(names, "SSPN"), 0u);
  EXPECT_EQ(count_of(names, "CTDA"), 2u);
  EXPECT_EQ(payloads(reloaded, k_scene, "CITC"), (std::vector<Bytes>{words({0})}));
  EXPECT_EQ(payloads(reloaded, k_scene, "INTT"), (std::vector<Bytes>{halves({5})}));
}

// Regression guard: removing one Start Scene entry must erase just its LCEP run and leave the neighbouring entry and the HTID marker readable.
TEST(SceneWriter, RemovingAStartSceneEntryErasesOnlyItsOwnRun) {
  auto plugin = read(payload_scene());
  ASSERT_EQ(fo4::forms::insert_scene_start_scene(plugin, key(k_scene), 4, 0, start_scene(k_scene, 1, "First")),
            fo4::forms::SceneStartSceneStatus::ok);
  ASSERT_EQ(fo4::forms::insert_scene_start_scene(plugin, key(k_scene), 4, 1, start_scene(k_scene, 4, "Second")),
            fo4::forms::SceneStartSceneStatus::ok);

  EXPECT_EQ(fo4::forms::remove_scene_start_scene(plugin, key(k_scene), 4, 0),
            fo4::forms::SceneStartSceneStatus::ok);

  auto reloaded = reload(plugin);
  const auto names = order(reloaded, k_scene);
  EXPECT_EQ(count_of(names, "LCEP"), 1u);
  EXPECT_EQ(count_of(names, "INTT"), 1u);
  EXPECT_EQ(count_of(names, "SSPN"), 1u);
  EXPECT_EQ(payloads(reloaded, k_scene, "SSPN"), (std::vector<Bytes>{zstr("Second")}));
  EXPECT_EQ(payloads(reloaded, k_scene, "INTT"), (std::vector<Bytes>{halves({4})}));
  EXPECT_EQ(count_of(names, "STSC"), 1u);
  const auto saved = fo4::forms::read_scene_start_scenes(reloaded, key(k_scene), 4);
  ASSERT_TRUE(saved.has_value());
  ASSERT_EQ(saved->size(), 1u);
  ASSERT_TRUE(saved->at(0).phase_name.has_value());
  EXPECT_EQ(*saved->at(0).phase_name, "Second");

  EXPECT_EQ(fo4::forms::remove_scene_start_scene(reloaded, key(k_scene), 4, 0), fo4::forms::SceneStartSceneStatus::ok);
  const auto cleared = fo4::forms::read_scene_start_scenes(reload(reloaded), key(k_scene), 4);
  ASSERT_TRUE(cleared.has_value());
  EXPECT_TRUE(cleared->empty());
  EXPECT_EQ(count_of(order(reloaded, k_scene), "STSC"), 1u);
}

// Regression guard: a Start Scene write must refuse a foreign action type, a missing action, a DIAL scene target and an unresolvable key without changing a byte.
TEST(SceneWriter, StartSceneEditsRefuseMismatchedTargetsAndChangeNoByte) {
  auto plugin = read(payload_scene());
  const auto before = fo4::write_plugin(plugin);
  const auto entry = start_scene(k_scene, 0, nullptr);

  EXPECT_EQ(fo4::forms::insert_scene_start_scene(plugin, key(k_scene), 0, 0, entry),
            fo4::forms::SceneStartSceneStatus::action_not_start_scene);
  EXPECT_EQ(fo4::forms::insert_scene_start_scene(plugin, key(k_scene), 9, 0, entry),
            fo4::forms::SceneStartSceneStatus::action_missing);
  EXPECT_EQ(fo4::forms::insert_scene_start_scene(plugin, key(0x8FF), 0, 0, entry),
            fo4::forms::SceneStartSceneStatus::record_missing);
  EXPECT_EQ(fo4::forms::insert_scene_start_scene(plugin, key(k_scene), 4, 1, entry),
            fo4::forms::SceneStartSceneStatus::index_out_of_range);
  EXPECT_EQ(fo4::forms::remove_scene_start_scene(plugin, key(k_scene), 4, 0),
            fo4::forms::SceneStartSceneStatus::index_out_of_range);
  EXPECT_EQ(fo4::forms::update_scene_start_scene(plugin, key(k_scene), 4, 0, entry),
            fo4::forms::SceneStartSceneStatus::index_out_of_range);
  EXPECT_FALSE(fo4::forms::read_scene_start_scenes(plugin, key(k_scene), 0).has_value());
  EXPECT_FALSE(fo4::forms::read_scene_start_scenes(plugin, key(0x8FF), 4).has_value());
  EXPECT_EQ(fo4::write_plugin(plugin), before);

  EXPECT_TRUE(refused(plugin, [&] {
    fo4::forms::insert_scene_start_scene(plugin, key(k_scene), 4, 0, start_scene(k_topic, 0, nullptr));
  }));
  EXPECT_TRUE(refused(plugin, [&] {
    fo4::forms::insert_scene_start_scene(plugin, key(k_scene), 4, 0, start_scene(k_kywd, 0, nullptr));
  }));
  EXPECT_TRUE(refused(plugin, [&] {
    fo4::forms::insert_scene_start_scene(plugin, key(k_scene), 4, 0, start_scene(0, 0, nullptr));
  }));
  auto nul_name = start_scene(k_scene, 0, nullptr);
  nul_name.phase_name = std::string(std::string_view{"bad\0name", 8});
  EXPECT_TRUE(refused(plugin, [&] { fo4::forms::insert_scene_start_scene(plugin, key(k_scene), 4, 0, nul_name); }));
  EXPECT_TRUE(refused(plugin, [&] {
    fo4::forms::insert_scene_start_scene(plugin, key(k_topic), 4, 0, start_scene(k_scene, 0, nullptr));
  }));
  EXPECT_EQ(fo4::write_plugin(plugin), before);
}

// Regression guard: the scene index scalar must address the tail XNAM that create_scene writes.
TEST(SceneWriter, SceneIndexScalarWritesTheTailIndex) {
  auto plugin = read(build());
  const auto created = fo4::forms::create_scene(plugin, key(k_quest), fo4::forms::SceneSpec{"Made scene", 0, 7});
  ASSERT_EQ(created.status, fo4::forms::SceneAuthorStatus::ok);
  const auto id = created.key.local().value();
  EXPECT_EQ(set(plugin, "SCEN\\Index", 0, "9", id), Scalar::updated);
  EXPECT_EQ(set(plugin, "SCEN\\Index", 0, "9", id), Scalar::unchanged);
  EXPECT_EQ(set(plugin, "SCEN\\Index", 1, "9", id), Scalar::subrecord_missing);
  const auto saved = reload(plugin);
  EXPECT_EQ(payloads(saved, id, "XNAM"), (std::vector<Bytes>{words({9})}));
}
// Detects: scene headtracking edits must retain signed actor aliases without changing radio sound links.
TEST(SceneWriter, ActionHeadtrackingListsPreserveOtherPayloads) {
  auto plugin = read(payload_scene());
  fo4::visit_records(plugin, [&](fo4::Record& record) {
    if (record.form_key != key(k_scene)) return;
    for (const auto& [anchor, added, bytes] : std::vector<std::tuple<const char*, const char*, Bytes>>{
             {"DEVA", "HTID", words({4, 5})}, {"PTOP", "DTID", words({6})},
             {"STSC", "HTID", {}}, {"NPOS", "ZZZZ", words({42})}}) {
      for (auto it = record.subrecords.begin(); it != record.subrecords.end(); ++it) {
        if (it->signature != sig(anchor)) continue;
        record.subrecords.insert(it + 1, fo4::Subrecord{sig(added), bytes, false});
        break;
      }
    }
    record.original_range.clear();
    record.decompressed_payload.clear();
    plugin.original_range.clear();
  });
  const auto neighbors = [&](const fo4::Plugin& input) {
    std::vector<Sub> out;
    const auto* record = fo4::find_record(input, key(k_scene));
    for (const auto& sub : record->subrecords) {
      if (sub.signature != sig("HTID") && sub.signature != sig("DTID"))
        out.emplace_back(sub.signature.to_string(), sub.data);
    }
    return out;
  };
  const auto before = neighbors(plugin);
  const std::vector<std::int32_t> aliases{-1, 0, 7};
  EXPECT_EQ(fo4::set_scene_action_headtracking(plugin, key(k_scene), 0, aliases), Link::updated);
  EXPECT_EQ(fo4::set_scene_action_headtracking(plugin, key(k_scene), 2, aliases), Link::updated);
  const auto edited = fo4::write_plugin(plugin);
  EXPECT_EQ(fo4::set_scene_action_headtracking(plugin, key(k_scene), 0, aliases), Link::unchanged);
  EXPECT_EQ(fo4::set_scene_action_headtracking(plugin, key(k_scene), 2, aliases), Link::unchanged);
  EXPECT_EQ(fo4::set_scene_action_headtracking(plugin, key(k_scene), 3, aliases), Link::subrecord_missing);
  EXPECT_EQ(fo4::set_scene_action_headtracking(plugin, key(k_scene), 4, aliases), Link::subrecord_missing);
  EXPECT_EQ(fo4::write_plugin(plugin), edited);
  auto loaded = reload(plugin);
  EXPECT_EQ(payloads(loaded, k_scene, "HTID"),
            (std::vector<Bytes>{words({0xFFFFFFFF, 0, 7}), words({k_sndr}), Bytes{}}));
  EXPECT_EQ(payloads(loaded, k_scene, "DTID"), (std::vector<Bytes>{words({0xFFFFFFFF, 0, 7})}));
  EXPECT_EQ(neighbors(loaded), before);
  EXPECT_EQ(fo4::set_scene_action_headtracking(plugin, key(k_scene), 0, {}), Link::updated);
  EXPECT_EQ(fo4::set_scene_action_headtracking(plugin, key(k_scene), 2, {}), Link::updated);
  loaded = reload(plugin);
  EXPECT_EQ(payloads(loaded, k_scene, "HTID"), (std::vector<Bytes>{words({k_sndr}), Bytes{}}));
  EXPECT_TRUE(payloads(loaded, k_scene, "DTID").empty());
  EXPECT_EQ(neighbors(loaded), before);
}

// Detects: malformed or duplicated headtracking arrays must not be replaced or partially written.
TEST(SceneWriter, HeadtrackingRefusesMalformedArraysWithoutChangingBytes) {
  for (const bool duplicate : {false, true}) {
    auto plugin = read(payload_scene());
    fo4::visit_records(plugin, [&](fo4::Record& record) {
      if (record.form_key != key(k_scene)) return;
      for (auto it = record.subrecords.begin(); it != record.subrecords.end(); ++it) {
        if (it->signature != sig("DEVA")) continue;
        it = record.subrecords.insert(it + 1, fo4::Subrecord{sig("HTID"), duplicate ? words({1}) : halves({1}), false});
        if (duplicate) record.subrecords.insert(it + 1, fo4::Subrecord{sig("HTID"), words({2}), false});
        record.original_range.clear();
        record.decompressed_payload.clear();
        plugin.original_range.clear();
        break;
      }
    });
    EXPECT_TRUE(refused(plugin, [&] { fo4::set_scene_action_headtracking(plugin, key(k_scene), 0, {3}); }));
  }
}

// Detects: editing Start Scene links must preserve each neighboring entry's phase and condition bytes.
TEST(SceneWriter, StartSceneLinksAreEditedWithoutRewritingOtherEntryData) {
  auto plugin = read(payload_scene());
  fo4::visit_records(plugin, [&](fo4::Record& record) {
    if (record.form_key != key(k_scene)) return;
    for (auto it = record.subrecords.begin(); it != record.subrecords.end(); ++it) {
      if (it->signature != sig("STSC")) continue;
      const std::vector<fo4::Subrecord> entries{
          {sig("LCEP"), words({k_scene}), false}, {sig("INTT"), halves({2}), false},
          {sig("SSPN"), zstr("Keep phase"), false}, {sig("CITC"), words({1}), false},
          {sig("CTDA"), fo4::serialize_ctda_condition(fo4::CtdaCondition{}), false},
          {sig("LCEP"), words({k_scene}), false}, {sig("HTID"), {}, false}};
      record.subrecords.insert(it + 1, entries.begin(), entries.end());
      record.original_range.clear(); record.decompressed_payload.clear(); plugin.original_range.clear();
      break;
    }
  });
  const auto before_ctda = payloads(plugin, k_scene, "CTDA");
  const auto before_intt = payloads(plugin, k_scene, "INTT");
  const auto before_sspn = payloads(plugin, k_scene, "SSPN");
  EXPECT_EQ(fo4::insert_scene_action_start_scene(plugin, key(k_scene), 4, 1, key(k_scene)), Link::updated);
  EXPECT_EQ(fo4::set_scene_action_start_scene(plugin, key(k_scene), 4, 0, key(k_template)), Link::updated);
  EXPECT_EQ(fo4::set_scene_action_start_scene(plugin, key(k_scene), 4, 0, key(k_template)), Link::unchanged);
  EXPECT_TRUE(refused(plugin, [&] { fo4::set_scene_action_start_scene(plugin, key(k_scene), 4, 1, key(k_pack)); }));
  EXPECT_EQ(fo4::remove_scene_action_start_scene(plugin, key(k_scene), 4, 1), Link::updated);
  const auto loaded = reload(plugin);
  EXPECT_EQ(payloads(loaded, k_scene, "LCEP"), (std::vector<Bytes>{words({k_template}), words({k_scene})}));
  EXPECT_EQ(payloads(loaded, k_scene, "CTDA"), before_ctda);
  EXPECT_EQ(payloads(loaded, k_scene, "INTT"), before_intt);
  EXPECT_EQ(payloads(loaded, k_scene, "SSPN"), before_sspn);
  EXPECT_EQ(payloads(loaded, k_scene, "HTID"), (std::vector<Bytes>{words({k_sndr}), Bytes{}}));
  EXPECT_EQ(fo4::insert_scene_action_start_scene(plugin, key(k_scene), 3, 0, key(k_scene)), Link::subrecord_missing);
  EXPECT_EQ(fo4::remove_scene_action_start_scene(plugin, key(k_scene), 4, 2), Link::subrecord_missing);
}

// Detects: ambiguous Start Scene entries must be refused without changing any plugin bytes.
TEST(SceneWriter, StartSceneLinksRefuseUnknownOrMalformedEntryData) {
  for (const auto& bad : std::vector<fo4::Subrecord>{
           {sig("ZZZZ"), words({42}), false}, {sig("INTT"), words({2}), false},
           {sig("CITC"), words({1}), false}, {sig("CIS1"), zstr("orphan"), false}}) {
    auto plugin = read(payload_scene());
    fo4::visit_records(plugin, [&](fo4::Record& record) {
      if (record.form_key != key(k_scene)) return;
      for (auto it = record.subrecords.begin(); it != record.subrecords.end(); ++it) {
        if (it->signature != sig("STSC")) continue;
        it = record.subrecords.insert(it + 1, fo4::Subrecord{sig("LCEP"), words({k_scene}), false});
        record.subrecords.insert(it + 1, bad);
        record.original_range.clear(); record.decompressed_payload.clear(); plugin.original_range.clear();
        break;
      }
    });
    EXPECT_TRUE(refused(plugin, [&] { fo4::remove_scene_action_start_scene(plugin, key(k_scene), 4, 0); }));
  }
}

// Detects: Start Scene links must refuse an encoded null master reference without changing plugin bytes.
TEST(SceneWriter, StartSceneTargetsRejectEncodedNullAndAllowNonzeroMasterIds) {
  fo4::test::ByteWriter out;
  const auto tes4 = fo4::test::begin_record(out, "TES4", 0, 0);
  fo4::test::ByteWriter hedr;
  hedr.f32(1.0f); hedr.u32(0); hedr.u32(0x900);
  fo4::test::write_subrecord(out, "HEDR", hedr.bytes);
  for (const auto* master : {"First.esm", "Second.esm"}) {
    fo4::test::write_subrecord(out, "MAST", zstr(master));
    fo4::test::write_subrecord(out, "DATA", words({0, 0}));
  }
  fo4::test::end_record(out, tes4);
  const auto group = fo4::test::begin_group(out, "SCEN", 0);
  record(out, "SCEN", 0x02000803, {{"ANAM", halves({4})}, {"ALID", words({0xFFFFFFFF})}, {"ANAM", {}}});
  fo4::test::end_group(out, group);
  auto plugin = read(out.bytes);
  const auto first = fo4::PluginKey::from_filename("First.esm");
  const auto second = fo4::PluginKey::from_filename("Second.esm");
  const fo4::FormKey null_target{first, fo4::LocalFormId{0}};
  const fo4::FormKey first_nonzero{first, fo4::LocalFormId{1}};
  const fo4::FormKey second_zero{second, fo4::LocalFormId{0}};
  EXPECT_TRUE(refused(plugin, [&] { fo4::insert_scene_action_start_scene(plugin, key(k_scene), 0, 0, null_target); }));
  EXPECT_EQ(fo4::insert_scene_action_start_scene(plugin, key(k_scene), 0, 0, first_nonzero), Link::updated);
  EXPECT_TRUE(refused(plugin, [&] { fo4::set_scene_action_start_scene(plugin, key(k_scene), 0, 0, null_target); }));
  EXPECT_EQ(payloads(plugin, k_scene, "LCEP"), (std::vector<Bytes>{words({1})}));
  EXPECT_EQ(fo4::set_scene_action_start_scene(plugin, key(k_scene), 0, 0, second_zero), Link::updated);
  EXPECT_EQ(payloads(plugin, k_scene, "LCEP"), (std::vector<Bytes>{words({0x01000000})}));
}

// Detects: Start Scene phase edits must round-trip only the selected entry and preserve its conditions and neighbors.
TEST(SceneWriter, StartScenePhaseFieldsRoundTripIndependently) {
  auto plugin = read(payload_scene());
  fo4::visit_records(plugin, [&](fo4::Record& record) {
    if (record.form_key != key(k_scene)) return;
    for (auto it = record.subrecords.begin(); it != record.subrecords.end(); ++it) {
      if (it->signature != sig("STSC")) continue;
      const std::vector<fo4::Subrecord> entries{
          {sig("LCEP"), words({k_scene}), false}, {sig("INTT"), halves({2}), false},
          {sig("SSPN"), zstr("Old"), false}, {sig("CITC"), words({1}), false},
          {sig("CTDA"), fo4::serialize_ctda_condition(fo4::CtdaCondition{}), false},
          {sig("LCEP"), words({k_template}), false}, {sig("HTID"), {}, false}};
      record.subrecords.insert(it + 1, entries.begin(), entries.end());
      record.original_range.clear(); record.decompressed_payload.clear(); plugin.original_range.clear();
      break;
    }
  });
  const auto conditions = payloads(plugin, k_scene, "CTDA");
  auto first = fo4::read_scene_action_start_phase(plugin, key(k_scene), 4, 0);
  ASSERT_TRUE(first.has_value());
  EXPECT_EQ(first->index, 2);
  EXPECT_EQ(first->name, "Old");
  EXPECT_EQ(fo4::set_scene_action_start_phase_index(plugin, key(k_scene), 4, 1, 65535), Link::updated);
  EXPECT_EQ(fo4::set_scene_action_start_phase_name(plugin, key(k_scene), 4, 1, "Next"), Link::updated);
  EXPECT_EQ(fo4::set_scene_action_start_phase_index(plugin, key(k_scene), 4, 0, std::nullopt), Link::updated);
  EXPECT_EQ(fo4::set_scene_action_start_phase_name(plugin, key(k_scene), 4, 0, std::nullopt), Link::updated);
  const auto saved = reload(plugin);
  first = fo4::read_scene_action_start_phase(saved, key(k_scene), 4, 0);
  const auto second = fo4::read_scene_action_start_phase(saved, key(k_scene), 4, 1);
  ASSERT_TRUE(first.has_value()); ASSERT_TRUE(second.has_value());
  EXPECT_FALSE(first->index.has_value()); EXPECT_FALSE(first->name.has_value());
  EXPECT_EQ(second->index, 65535); EXPECT_EQ(second->name, "Next");
  EXPECT_EQ(payloads(saved, k_scene, "CTDA"), conditions);
  EXPECT_EQ(payloads(saved, k_scene, "LCEP"), (std::vector<Bytes>{words({k_scene}), words({k_template})}));
  EXPECT_EQ(payloads(saved, k_scene, "HTID"), (std::vector<Bytes>{words({k_sndr}), Bytes{}}));
}

// Detects: malformed stored Start Scene names and embedded input NULs must not be truncated during a phase edit.
TEST(SceneWriter, StartScenePhaseNamesRefuseEmbeddedNuls) {
  auto plugin = read(payload_scene());
  fo4::visit_records(plugin, [&](fo4::Record& record) {
    if (record.form_key != key(k_scene)) return;
    for (auto it = record.subrecords.begin(); it != record.subrecords.end(); ++it) {
      if (it->signature != sig("STSC")) continue;
      const std::vector<fo4::Subrecord> entry{
          {sig("LCEP"), words({k_scene}), false},
          {sig("SSPN"), {std::byte{0x41}, std::byte{0}, std::byte{0x42}, std::byte{0}}, false}};
      record.subrecords.insert(it + 1, entry.begin(), entry.end());
      record.original_range.clear(); record.decompressed_payload.clear(); plugin.original_range.clear();
      break;
    }
  });
  EXPECT_TRUE(refused(plugin, [&] { fo4::set_scene_action_start_phase_index(plugin, key(k_scene), 4, 0, 2); }));
  EXPECT_TRUE(refused(plugin, [&] { fo4::set_scene_action_start_phase_name(plugin, key(k_scene), 4, 0, "Next"); }));
  EXPECT_TRUE(refused(plugin, [&] { fo4::read_scene_action_start_phase(plugin, key(k_scene), 4, 0); }));
  auto clean = read(payload_scene());
  ASSERT_EQ(fo4::insert_scene_action_start_scene(clean, key(k_scene), 4, 0, key(k_scene)), Link::updated);
  const std::string name("A\0B", 3);
  EXPECT_TRUE(refused(clean, [&] { fo4::set_scene_action_start_phase_name(clean, key(k_scene), 4, 0, name); }));
  ASSERT_EQ(fo4::set_scene_action_start_phase_name(clean, key(k_scene), 4, 0, ""), Link::updated);
  const auto phase = fo4::read_scene_action_start_phase(reload(clean), key(k_scene), 4, 0);
  ASSERT_TRUE(phase.has_value());
  ASSERT_TRUE(phase->name.has_value());
  EXPECT_TRUE(phase->name->empty());
}

// Detects: Start Scene condition edits must update only one entry and keep CITC, phase data and sibling bytes.
TEST(SceneWriter, StartSceneScopedConditionsPreserveOtherEntries) {
  auto plugin = read(payload_scene());
  ASSERT_EQ(fo4::insert_scene_action_start_scene(plugin, key(k_scene), 4, 0, key(k_scene)), Link::updated);
  ASSERT_EQ(fo4::insert_scene_action_start_scene(plugin, key(k_scene), 4, 1, key(k_template)), Link::updated);
  ASSERT_EQ(fo4::set_scene_action_start_phase_index(plugin, key(k_scene), 4, 0, 2), Link::updated);
  ASSERT_EQ(fo4::set_scene_action_start_phase_name(plugin, key(k_scene), 4, 1, "Other"), Link::updated);
  const auto selected = fo4::schema::scoped_condition_path(sig("SCEN"), "Actions[4].StartScenes[0].Conditions");
  const auto sibling = fo4::schema::scoped_condition_path(sig("SCEN"), "Actions[4].StartScenes[1].Conditions");
  ASSERT_TRUE(selected.has_value()); ASSERT_TRUE(sibling.has_value());
  ASSERT_TRUE(selected->inner_index.has_value()); ASSERT_TRUE(sibling->inner_index.has_value());
  fo4::CtdaCondition condition; condition.function = 42; condition.padding[0] = std::byte{0x7f};
  using Status = fo4::schema::ScopedConditionStatus;
  ASSERT_EQ(fo4::schema::set_record_scoped_conditions(plugin, key(k_scene), selected->descriptor,
      selected->container_index, std::vector<fo4::CtdaCondition>{condition}, selected->inner_index), Status::updated);
  auto saved = reload(plugin);
  auto* record = fo4::find_record(saved, key(k_scene)); ASSERT_NE(record, nullptr);
  auto own = fo4::schema::read_scoped_conditions(*record, selected->descriptor, selected->container_index, selected->inner_index);
  auto other = fo4::schema::read_scoped_conditions(*record, sibling->descriptor, sibling->container_index, sibling->inner_index);
  ASSERT_TRUE(own.ok); ASSERT_EQ(own.conditions.size(), 1u); EXPECT_EQ(own.conditions[0].padding[0], std::byte{0x7f});
  ASSERT_TRUE(other.ok); EXPECT_TRUE(other.conditions.empty());
  EXPECT_EQ(payloads(saved, k_scene, "CITC"), (std::vector<Bytes>{words({1})}));
  EXPECT_EQ(payloads(saved, k_scene, "INTT"), (std::vector<Bytes>{halves({2})}));
  EXPECT_EQ(payloads(saved, k_scene, "SSPN"), (std::vector<Bytes>{zstr("Other")}));
  EXPECT_EQ(payloads(saved, k_scene, "LCEP"), (std::vector<Bytes>{words({k_scene}), words({k_template})}));
  ASSERT_EQ(fo4::schema::set_record_scoped_conditions(plugin, key(k_scene), selected->descriptor,
      selected->container_index, {}, selected->inner_index), Status::updated);
  saved = reload(plugin);
  EXPECT_EQ(payloads(saved, k_scene, "CITC"), (std::vector<Bytes>{words({0})}));
  EXPECT_EQ(payloads(saved, k_scene, "INTT"), (std::vector<Bytes>{halves({2})}));
  EXPECT_EQ(payloads(saved, k_scene, "SSPN"), (std::vector<Bytes>{zstr("Other")}));
}

// Detects: unknown bytes inside a Start Scene entry must not be discarded by a condition update.
TEST(SceneWriter, StartSceneScopedConditionsRefuseOpaqueEntry) {
  auto plugin = read(payload_scene());
  ASSERT_EQ(fo4::insert_scene_action_start_scene(plugin, key(k_scene), 4, 0, key(k_scene)), Link::updated);
  fo4::visit_records(plugin, [&](fo4::Record& record) {
    if (record.form_key != key(k_scene)) return;
    for (auto it = record.subrecords.begin(); it != record.subrecords.end(); ++it) {
      if (it->signature != sig("LCEP")) continue;
      record.subrecords.insert(it + 1, fo4::Subrecord{sig("ZZZZ"), words({9}), false});
      record.original_range.clear(); record.decompressed_payload.clear(); plugin.original_range.clear(); break;
    }
  });
  const auto selected = fo4::schema::scoped_condition_path(sig("SCEN"), "Actions[4].StartScenes[0].Conditions");
  ASSERT_TRUE(selected.has_value());
  fo4::CtdaCondition condition; condition.function = 42;
  const auto before = fo4::write_plugin(plugin);
  EXPECT_EQ(fo4::schema::set_record_scoped_conditions(plugin, key(k_scene), selected->descriptor,
      selected->container_index, std::vector<fo4::CtdaCondition>{condition}, selected->inner_index),
      fo4::schema::ScopedConditionStatus::alias_structure_invalid);
  EXPECT_EQ(fo4::write_plugin(plugin), before);
}

// Detects: nested Start Scene condition paths must not alias another entry or accept malformed indices.
TEST(SceneWriter, StartSceneConditionPathRequiresBothIndices) {
  const auto valid = fo4::schema::scoped_condition_path(sig("SCEN"), "Actions[4].StartScenes[2].Conditions");
  ASSERT_TRUE(valid.has_value()); EXPECT_EQ(valid->container_index, 4u); EXPECT_EQ(valid->inner_index, 2u);
  for (const auto* path : {"Actions[4].StartScenes", "Actions[4].StartScenes.Conditions",
                           "Actions[4].StartScenes[-1].Conditions", "Actions[4].StartScenes[2].Conditions.extra"})
    EXPECT_FALSE(fo4::schema::scoped_condition_path(sig("SCEN"), path).has_value());
  EXPECT_FALSE(fo4::schema::scoped_condition_path(sig("QUST"), "Actions[4].StartScenes[2].Conditions").has_value());
}

// Detects: typed package action reads must expose ordered links and reject malformed stored references.
TEST(SceneWriter, PackageActionListReadPreservesOrderAndRejectsMalformed) {
  auto plugin = read(payload_scene());
  ASSERT_EQ(fo4::set_scene_action_packages(plugin, key(k_scene), 5, {key(k_pack), key(k_pack)}), Link::updated);
  const auto loaded = reload(plugin);
  const auto packages = fo4::read_scene_action_packages(loaded, key(k_scene), 5);
  ASSERT_TRUE(packages.has_value());
  EXPECT_EQ(*packages, (std::vector<fo4::FormKey>{key(k_pack), key(k_pack)}));
  EXPECT_FALSE(fo4::read_scene_action_packages(loaded, key(k_scene), 0).has_value());
  auto malformed = read(payload_scene(true));
  EXPECT_TRUE(refused(malformed, [&] { fo4::read_scene_action_packages(malformed, key(k_scene), 5); }));
}

// Detects: a package link that encodes as raw null must be refused without changing the list.
TEST(SceneWriter, PackageActionRefusesEncodedNullMasterTarget) {
  fo4::test::ByteWriter out;
  const auto tes4 = fo4::test::begin_record(out, "TES4", 0, 0);
  fo4::test::ByteWriter hedr; hedr.f32(1.0f); hedr.u32(0); hedr.u32(0x900);
  fo4::test::write_subrecord(out, "HEDR", hedr.bytes);
  fo4::test::write_subrecord(out, "MAST", zstr("First.esm"));
  fo4::test::write_subrecord(out, "DATA", words({0, 0}));
  fo4::test::end_record(out, tes4);
  const auto group = fo4::test::begin_group(out, "SCEN", 0);
  record(out, "SCEN", 0x01000803, {{"ANAM", halves({1})}, {"ALID", words({0xFFFFFFFF})}, {"ANAM", {}}});
  fo4::test::end_group(out, group);
  auto plugin = read(out.bytes);
  const fo4::FormKey null_target{fo4::PluginKey::from_filename("First.esm"), fo4::LocalFormId{0}};
  EXPECT_TRUE(refused(plugin, [&] { fo4::set_scene_action_packages(plugin, key(k_scene), 0, {null_target}); }));
}

// Detects: malformed stored package FormIDs must not be silently discarded by package list replacement.
TEST(SceneWriter, PackageActionRefusesInvalidStoredFormIds) {
  for (const std::uint32_t raw : {0u, 0x02000000u, 0xFFFFFFFFu}) {
    auto plugin = read(payload_scene());
    fo4::visit_records(plugin, [&](fo4::Record& record) {
      if (record.form_key != key(k_scene)) return;
      for (auto& sub : record.subrecords) {
        if (sub.signature != sig("PNAM") || sub.data != words({k_pack})) continue;
        sub.data = words({raw});
        record.original_range.clear(); record.decompressed_payload.clear(); plugin.original_range.clear(); break;
      }
    });
    EXPECT_TRUE(refused(plugin, [&] { fo4::read_scene_action_packages(plugin, key(k_scene), 5); }));
    EXPECT_TRUE(refused(plugin, [&] { fo4::set_scene_action_packages(plugin, key(k_scene), 5, {key(k_pack)}); }));
  }
}
