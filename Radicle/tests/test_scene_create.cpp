#include "dialogue_writer_fixture.hpp"

#include "fo4/c_api.h"
#include "fo4/c_api_dialogue.h"
#include "fo4/c_api_env.h"
#include "fo4/forms/scene.hpp"

#include <gtest/gtest.h>

#include <fstream>
#include <string>

namespace {

using namespace fo4::test::dialogue;

Bytes quest_only_plugin() {
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
  fo4::test::end_group(out, quests);
  return out.bytes;
}

const fo4::Record* find(fo4::Plugin& plugin, std::uint32_t local) {
  return fo4::find_record(plugin, key(local));
}

const fo4::Group* quest_children(const fo4::Plugin& plugin) {
  for (const auto& group : plugin.groups) {
    if (group.header.group_type != 0) continue;
    for (const auto& child : group.children) {
      const auto* nested = std::get_if<fo4::Group>(&child);
      if (nested != nullptr && nested->header.group_type == 10 &&
          nested->header.label_as_u32() == k_quest) {
        return nested;
      }
    }
  }
  return nullptr;
}

}  // namespace

TEST(SceneCreate, NewSceneCarriesEdidAndParentQuestLink) {
  auto plugin = read(build());
  const auto created = fo4::forms::create_scene(plugin, key(k_quest), "Encore");
  EXPECT_EQ(created, key(0x900));

  const auto* record = find(plugin, 0x900);
  ASSERT_NE(record, nullptr);
  EXPECT_EQ(record->header.signature, sig("SCEN"));
  ASSERT_EQ(record->subrecords.size(), 2u);
  EXPECT_EQ(record->subrecords[0].signature, sig("EDID"));
  EXPECT_EQ(record->subrecords[1].signature, sig("PNAM"));
  EXPECT_EQ(record->subrecords[1].data, words({k_quest}));

  const auto* children = quest_children(plugin);
  ASSERT_NE(children, nullptr);
  ASSERT_FALSE(children->children.empty());
  const auto* last = std::get_if<fo4::Record>(&children->children.back());
  ASSERT_NE(last, nullptr);
  EXPECT_EQ(last->form_key, created);

  const auto reloaded = reload(plugin);
  const auto* reread = fo4::find_record(reloaded, created);
  ASSERT_NE(reread, nullptr);
  ASSERT_EQ(reread->subrecords.size(), 2u);
  EXPECT_EQ(reread->subrecords[1].signature, sig("PNAM"));
  EXPECT_EQ(reread->subrecords[1].data, words({k_quest}));
}

TEST(SceneCreate, QuestWithoutChildrenGroupGainsAnAdjacentGroup) {
  auto plugin = read(quest_only_plugin());
  const auto created = fo4::forms::create_scene(plugin, key(k_quest), "Encore");
  EXPECT_EQ(created, key(0x900));

  ASSERT_EQ(plugin.groups.size(), 1u);
  const auto& top = plugin.groups.front();
  ASSERT_EQ(top.children.size(), 2u);
  const auto* quest = std::get_if<fo4::Record>(&top.children[0]);
  ASSERT_NE(quest, nullptr);
  EXPECT_EQ(quest->form_key, key(k_quest));
  const auto* children = std::get_if<fo4::Group>(&top.children[1]);
  ASSERT_NE(children, nullptr);
  EXPECT_EQ(children->header.group_type, 10);
  EXPECT_EQ(children->header.label_as_u32(), k_quest);
  ASSERT_EQ(children->children.size(), 1u);
  const auto* scene = std::get_if<fo4::Record>(&children->children[0]);
  ASSERT_NE(scene, nullptr);
  EXPECT_EQ(scene->form_key, created);
}

Bytes one_master_plugin() {
  fo4::test::ByteWriter out;
  const auto tes4 = fo4::test::begin_record(out, "TES4", 0, 0);
  fo4::test::ByteWriter hedr;
  hedr.f32(1.0f);
  hedr.u32(1);
  hedr.u32(0x900);
  fo4::test::write_subrecord(out, "HEDR", hedr.bytes);
  std::string master{"Base.esm"};
  master.push_back('\0');
  fo4::test::write_subrecord(out, "MAST", std::string_view{master});
  fo4::test::ByteWriter data;
  data.u32(0);
  data.u32(0);
  fo4::test::write_subrecord(out, "DATA", data.bytes);
  fo4::test::end_record(out, tes4);
  const auto quests = fo4::test::begin_group(out, "QUST", 0);
  record(out, "QUST", 0x01000800, {{"EDID", zstr("Quest")}});
  fo4::test::end_group(out, quests);
  return out.bytes;
}

TEST(SceneCreate, ChildGroupLabelHoldsTheEncodedQuestFormId) {
  auto plugin = read(one_master_plugin());
  const auto created = fo4::forms::create_scene(plugin, key(k_quest), "Encore");
  const fo4::Group* children = nullptr;
  for (const auto& group : plugin.groups) {
    if (group.header.group_type != 0) continue;
    for (const auto& child : group.children) {
      const auto* nested = std::get_if<fo4::Group>(&child);
      if (nested != nullptr && nested->header.group_type == 10) children = nested;
    }
  }
  ASSERT_NE(children, nullptr);
  EXPECT_EQ(children->header.label_as_u32(), 0x01000800u);
  const auto reloaded = reload(plugin);
  const auto* reread = fo4::find_record(reloaded, created);
  ASSERT_NE(reread, nullptr);
}

Bytes two_quest_plugin() {
  fo4::test::ByteWriter out;
  const auto tes4 = fo4::test::begin_record(out, "TES4", 0, 0);
  fo4::test::ByteWriter hedr;
  hedr.f32(1.0f);
  hedr.u32(0);
  hedr.u32(0x900);
  fo4::test::write_subrecord(out, "HEDR", hedr.bytes);
  fo4::test::end_record(out, tes4);
  const auto quests = fo4::test::begin_group(out, "QUST", 0);
  record(out, "QUST", k_quest, {{"EDID", zstr("First")}});
  const auto first = child_group(out, k_quest, 10);
  record(out, "SCEN", 0x820, {{"EDID", zstr("Old")}, {"PNAM", words({k_quest})}});
  fo4::test::end_group(out, first);
  record(out, "QUST", 0x810, {{"EDID", zstr("Second")}});
  fo4::test::end_group(out, quests);
  return out.bytes;
}

TEST(SceneCreate, SceneForSecondQuestCreatesGroupBesideFirstQuestGroup) {
  auto plugin = read(two_quest_plugin());
  const auto created = fo4::forms::create_scene(plugin, key(0x810), "Encore");
  const fo4::Group* second = nullptr;
  for (const auto& group : plugin.groups) {
    if (group.header.group_type != 0) continue;
    for (const auto& child : group.children) {
      const auto* nested = std::get_if<fo4::Group>(&child);
      if (nested != nullptr && nested->header.group_type == 10 &&
          nested->header.label_as_u32() == 0x810) {
        second = nested;
      }
    }
  }
  ASSERT_NE(second, nullptr);
  ASSERT_EQ(second->children.size(), 1u);
  const auto* scene = std::get_if<fo4::Record>(&second->children[0]);
  ASSERT_NE(scene, nullptr);
  EXPECT_EQ(scene->form_key, created);
}

Bytes both_quest_children_plugin() {
  fo4::test::ByteWriter out;
  const auto tes4 = fo4::test::begin_record(out, "TES4", 0, 0);
  fo4::test::ByteWriter hedr;
  hedr.f32(1.0f);
  hedr.u32(0);
  hedr.u32(0x900);
  fo4::test::write_subrecord(out, "HEDR", hedr.bytes);
  fo4::test::end_record(out, tes4);
  const auto quests = fo4::test::begin_group(out, "QUST", 0);
  record(out, "QUST", k_quest, {{"EDID", zstr("First")}});
  const auto first = child_group(out, k_quest, 10);
  record(out, "SCEN", 0x820, {{"EDID", zstr("Old")}, {"PNAM", words({k_quest})}});
  fo4::test::end_group(out, first);
  record(out, "QUST", 0x810, {{"EDID", zstr("Second")}});
  const auto second = child_group(out, 0x810, 10);
  record(out, "SCEN", 0x830, {{"EDID", zstr("Prior")}, {"PNAM", words({0x810})}});
  fo4::test::end_group(out, second);
  fo4::test::end_group(out, quests);
  return out.bytes;
}

TEST(SceneCreate, SceneForSecondQuestReusesItsExistingChildrenGroup) {
  auto plugin = read(both_quest_children_plugin());
  const auto created = fo4::forms::create_scene(plugin, key(0x810), "Encore");
  std::size_t children_groups = 0;
  const fo4::Group* second = nullptr;
  for (const auto& group : plugin.groups) {
    if (group.header.group_type != 0) continue;
    for (const auto& child : group.children) {
      const auto* nested = std::get_if<fo4::Group>(&child);
      if (nested == nullptr || nested->header.group_type != 10) continue;
      ++children_groups;
      if (nested->header.label_as_u32() == 0x810) second = nested;
    }
  }
  EXPECT_EQ(children_groups, 2u);
  ASSERT_NE(second, nullptr);
  ASSERT_EQ(second->children.size(), 2u);
  const auto* scene = std::get_if<fo4::Record>(&second->children[1]);
  ASSERT_NE(scene, nullptr);
  EXPECT_EQ(scene->form_key, created);
}

TEST(SceneCreate, RefusalsLeaveThePluginUnchanged) {
  auto plugin = read(build());
  const auto before = fo4::write_plugin(plugin);
  EXPECT_THROW(static_cast<void>(fo4::forms::create_scene(plugin, key(0x8FF), "Encore")),
               fo4::forms::SceneError);
  EXPECT_THROW(static_cast<void>(fo4::forms::create_scene(plugin, key(k_topic), "Encore")),
               fo4::forms::SceneError);
  EXPECT_THROW(static_cast<void>(fo4::forms::create_scene(plugin, key(k_quest), "")),
               fo4::forms::SceneError);
  EXPECT_THROW(static_cast<void>(fo4::forms::create_scene(plugin, key(k_quest), "Scene")),
               fo4::forms::SceneError);
  EXPECT_THROW(static_cast<void>(fo4::forms::create_scene(plugin, key(k_quest), "SCENE")),
               fo4::forms::SceneError);
  EXPECT_EQ(fo4::write_plugin(plugin), before);
}

namespace {

radicle_form_key rkey(std::uint32_t id) { return radicle_form_key{k_plugin, id}; }

class SceneCreateCApi : public testing::Test {
protected:
  void SetUp() override {
    std::filesystem::create_directories(root.path / "out");
    path = root.path / k_plugin;
    const auto bytes = build();
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    file.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
  }
  TempRoot root;
  std::filesystem::path path;
};

}  // namespace

TEST_F(SceneCreateCApi, CreateSceneMapsStatusesAndSurvivesReload) {
  radicle_plugin* plugin = nullptr;
  ASSERT_EQ(radicle_plugin_read_file(path.string().c_str(), &plugin), RADICLE_STATUS_OK);
  const radicle_scene_spec encore{"Encore", 0, 0};
  radicle_form_key created{nullptr, 0};
  EXPECT_EQ(radicle_plugin_create_scene(plugin, rkey(k_quest), &encore, &created), RADICLE_STATUS_OK);
  EXPECT_STREQ(created.plugin, k_plugin);
  EXPECT_EQ(created.local_id, 0x900u);
  size_t phases = 99;
  EXPECT_EQ(radicle_plugin_scene_group_count(plugin, created, RADICLE_SCENE_PART_PHASE, &phases),
            RADICLE_STATUS_OK);
  EXPECT_EQ(phases, 0u);

  radicle_form_key refused{nullptr, 0};
  EXPECT_EQ(radicle_plugin_create_scene(plugin, rkey(0x8FF), &encore, &refused),
            RADICLE_STATUS_NOT_FOUND);
  EXPECT_EQ(radicle_plugin_create_scene(plugin, rkey(k_topic), &encore, &refused),
            RADICLE_STATUS_NOT_FOUND);
  EXPECT_EQ(radicle_plugin_create_scene(plugin, rkey(k_quest), nullptr, &refused),
            RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_plugin_create_scene(plugin, rkey(k_quest), &encore, nullptr),
            RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_plugin_create_scene(nullptr, rkey(k_quest), &encore, &refused),
            RADICLE_STATUS_INVALID_ARGUMENT);

  // Constraint: created.plugin points into the plugin handle, so keep the ID before the handle goes.
  const auto created_local = created.local_id;
  const auto saved = (root.path / "out" / k_plugin).string();
  ASSERT_EQ(radicle_plugin_write_file(plugin, saved.c_str()), RADICLE_STATUS_OK);
  radicle_plugin_destroy(plugin);

  const char* paths[] = {saved.c_str()};
  radicle_env* env = nullptr;
  ASSERT_EQ(radicle_env_load(paths, 1, &env), RADICLE_STATUS_OK) << radicle_last_error();
  const radicle_record* handle = nullptr;
  ASSERT_EQ(radicle_env_winner(env, rkey(created_local), &handle), RADICLE_STATUS_OK);
  const radicle_scene_info* decoded = nullptr;
  ASSERT_EQ(radicle_scene_decode(env, handle, &decoded), RADICLE_STATUS_OK);
  EXPECT_EQ(decoded->phase_count, 0u);
  EXPECT_EQ(decoded->parent_quest.local_id, k_quest);
  radicle_scene_free(decoded);
  radicle_env_destroy(env);
}
