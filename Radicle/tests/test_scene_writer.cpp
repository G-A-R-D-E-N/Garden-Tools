#include "dialogue_writer_fixture.hpp"
#include "fo4/forms/scene.hpp"

#include <gtest/gtest.h>

namespace {

using namespace fo4::test::dialogue;
using Scalar = fo4::ScalarMutationStatus;
using Link = fo4::FormLinkFieldMutationStatus;

Scalar set(fo4::Plugin& plugin, const char* path, std::size_t group, const char* value, std::uint32_t id = k_scene) {
  return fo4::set_scene_scalar(plugin, key(id), path, group, value);
}

inline constexpr std::uint32_t k_subtype = 0x830;

Bytes payload_scene() {
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
             {"PNAM", words({k_pack})},
             {"ANAM", {}},
             {"PNAM", words({k_quest})}, {"INAM", words({5})}, {"VNAM", words({0, 0, 0, 0})},
         });
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
  EXPECT_TRUE(refused(plugin, [&] {
    fo4::set_scene_action_formlink(plugin, key(k_scene), 0, "SCEN\\Action\\Topic", fo4::FormKey{});
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
