#include "dialogue_writer_fixture.hpp"

#include <gtest/gtest.h>

namespace {

using namespace fo4::test::dialogue;
using Scalar = fo4::ScalarMutationStatus;
using Link = fo4::FormLinkFieldMutationStatus;

Scalar set(fo4::Plugin& plugin, const char* path, std::size_t group, const char* value, std::uint32_t id = k_scene) {
  return fo4::set_scene_scalar(plugin, key(id), path, group, value);
}

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
