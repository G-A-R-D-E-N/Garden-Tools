#include "fo4/c_api.h"
#include "fo4/c_api_armor.h"
#include "fo4/c_api_cell.h"
#include "fo4/c_api_components.h"
#include "fo4/c_api_env.h"
#include "fo4/c_api_eyes.h"
#include "fo4/c_api_faction.h"
#include "plugin_bytes.hpp"

#include <gtest/gtest.h>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace {

constexpr const char* kPlugin = "Forms.esp";

using EnvPtr = std::unique_ptr<radicle_env, decltype(&radicle_env_destroy)>;
using PluginPtr = std::unique_ptr<radicle_plugin, decltype(&radicle_plugin_destroy)>;
using ComponentsPtr = std::unique_ptr<radicle_components, decltype(&radicle_components_free)>;
using EyesPtr = std::unique_ptr<radicle_eyes_info, decltype(&radicle_eyes_free)>;
using ArmorPtr = std::unique_ptr<radicle_armor_info, decltype(&radicle_armor_free)>;
using ArmorAddonPtr = std::unique_ptr<radicle_armor_addon_info, decltype(&radicle_armor_addon_free)>;
using RelationsPtr = std::unique_ptr<radicle_faction_relations, decltype(&radicle_faction_relations_free)>;
using RanksPtr = std::unique_ptr<radicle_faction_ranks, decltype(&radicle_faction_ranks_free)>;

radicle_form_key key_of(std::uint32_t local_id) {
  return radicle_form_key{kPlugin, local_id};
}

std::vector<std::byte> fixture() {
  fo4::test::ByteWriter out;
  const auto tes4 = fo4::test::begin_record(out, "TES4", 0, 0);
  fo4::test::ByteWriter hedr;
  hedr.f32(1.0f);
  hedr.u32(2);
  hedr.u32(0x900);
  fo4::test::write_subrecord(out, "HEDR", hedr.bytes);
  fo4::test::end_record(out, tes4);

  const auto miscs = fo4::test::begin_group(out, "MISC", 0);
  const auto misc = fo4::test::begin_record(out, "MISC", 0, 0x800);
  fo4::test::write_subrecord(out, "EDID", std::string_view("MiscItem\0", 9));
  fo4::test::ByteWriter components;
  components.u32(0x800);
  components.u32(2);
  components.u32(0x804);
  components.u32(1);
  fo4::test::write_subrecord(out, "CVPA", components.bytes);
  fo4::test::end_record(out, misc);
  fo4::test::end_group(out, miscs);

  const auto armors = fo4::test::begin_group(out, "ARMO", 0);
  const auto armor = fo4::test::begin_record(out, "ARMO", 0, 0x804);
  fo4::test::write_subrecord(out, "EDID", std::string_view("Plain\0", 6));
  fo4::test::end_record(out, armor);
  fo4::test::end_group(out, armors);
  return out.bytes;
}

class FormsModulesCApi : public ::testing::Test {
protected:
  void SetUp() override {
    root_ = std::filesystem::temp_directory_path() /
            ("radicle_c_forms_modules_" +
             std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::create_directories(root_ / "out");
    input_ = root_ / kPlugin;
    const auto bytes = fixture();
    std::ofstream stream(input_, std::ios::binary);
    stream.write(reinterpret_cast<const char*>(bytes.data()),
                 static_cast<std::streamsize>(bytes.size()));
  }

  void TearDown() override {
    std::error_code error;
    std::filesystem::remove_all(root_, error);
  }

  PluginPtr read_plugin() {
    radicle_plugin* plugin = nullptr;
    const auto text = input_.string();
    EXPECT_EQ(radicle_plugin_read_file(text.c_str(), &plugin), RADICLE_STATUS_OK);
    return PluginPtr(plugin, radicle_plugin_destroy);
  }

  std::filesystem::path write_out(const radicle_plugin* plugin) {
    const auto output = root_ / "out" / kPlugin;
    const auto text = output.string();
    EXPECT_EQ(radicle_plugin_write_file(plugin, text.c_str()), RADICLE_STATUS_OK);
    return output;
  }

  EnvPtr open_env(const std::filesystem::path& path) {
    const auto text = path.string();
    const char* paths[] = {text.c_str()};
    radicle_env* env = nullptr;
    EXPECT_EQ(radicle_env_load(paths, 1, &env), RADICLE_STATUS_OK);
    return EnvPtr(env, radicle_env_destroy);
  }

  const radicle_record* winner(const radicle_env* env, std::uint32_t local_id) {
    const radicle_record* record = nullptr;
    EXPECT_EQ(radicle_env_winner(env, key_of(local_id), &record), RADICLE_STATUS_OK);
    return record;
  }

  const radicle_record* by_editor_id(const radicle_env* env,
                                     const char* editor_id,
                                     const char* signature) {
    const radicle_record* record = nullptr;
    EXPECT_EQ(radicle_env_find_editor_id(env, editor_id, signature, &record), RADICLE_STATUS_OK);
    return record;
  }

  std::filesystem::path root_;
  std::filesystem::path input_;
};

TEST_F(FormsModulesCApi, ComponentsDecodeReportsStoredEntries) {
  const auto env = open_env(input_);
  const auto* record = winner(env.get(), 0x800);
  ASSERT_NE(record, nullptr);
  radicle_components* components = nullptr;
  ASSERT_EQ(radicle_components_decode(env.get(), record, &components), RADICLE_STATUS_OK);
  const ComponentsPtr view(components, radicle_components_free);
  ASSERT_NE(view->entries, nullptr);
  ASSERT_EQ(view->entry_count, 2u);
  EXPECT_STREQ(view->entries[0].component.plugin, kPlugin);
  EXPECT_EQ(view->entries[0].component.local_id, 0x800u);
  EXPECT_EQ(view->entries[0].count, 2u);
  EXPECT_STREQ(view->entries[1].component.plugin, kPlugin);
  EXPECT_EQ(view->entries[1].component.local_id, 0x804u);
  EXPECT_EQ(view->entries[1].count, 1u);
}

TEST_F(FormsModulesCApi, ComponentsDecodeRefusesARecordWithoutComponents) {
  const auto env = open_env(input_);
  const auto* record = winner(env.get(), 0x804);
  ASSERT_NE(record, nullptr);
  radicle_components* components = nullptr;
  EXPECT_EQ(radicle_components_decode(env.get(), record, &components), RADICLE_STATUS_PARSE_ERROR);
  EXPECT_EQ(components, nullptr);
  EXPECT_EQ(radicle_components_decode(env.get(), nullptr, &components),
            RADICLE_STATUS_INVALID_ARGUMENT);
}

TEST_F(FormsModulesCApi, ComponentsSupportReportsCoveredSignatures) {
  std::int32_t supported = -1;
  EXPECT_EQ(radicle_record_supports_components("MISC", &supported), RADICLE_STATUS_OK);
  EXPECT_EQ(supported, 1);
  EXPECT_EQ(radicle_record_supports_components("COBJ", &supported), RADICLE_STATUS_OK);
  EXPECT_EQ(supported, 1);
  EXPECT_EQ(radicle_record_supports_components("ARMO", &supported), RADICLE_STATUS_OK);
  EXPECT_EQ(supported, 0);
  EXPECT_EQ(radicle_record_supports_components("MIS", &supported),
            RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_record_supports_components(nullptr, &supported),
            RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_record_supports_components("MISC", nullptr),
            RADICLE_STATUS_INVALID_ARGUMENT);
}

TEST_F(FormsModulesCApi, SetComponentsReplacesTheStoredArray) {
  auto plugin = read_plugin();
  const radicle_component_entry entries[] = {
      {key_of(0x804), 4},
      {key_of(0x800), 7},
  };
  ASSERT_EQ(radicle_plugin_set_record_components(plugin.get(), key_of(0x800), entries, 2),
            RADICLE_STATUS_OK);
  const auto out = write_out(plugin.get());
  const auto env = open_env(out);
  const auto* record = winner(env.get(), 0x800);
  ASSERT_NE(record, nullptr);
  radicle_components* components = nullptr;
  ASSERT_EQ(radicle_components_decode(env.get(), record, &components), RADICLE_STATUS_OK);
  const ComponentsPtr view(components, radicle_components_free);
  ASSERT_EQ(view->entry_count, 2u);
  ASSERT_NE(view->entries, nullptr);
  EXPECT_EQ(view->entries[0].component.local_id, 0x804u);
  EXPECT_EQ(view->entries[0].count, 4u);
  EXPECT_EQ(view->entries[1].component.local_id, 0x800u);
  EXPECT_EQ(view->entries[1].count, 7u);

  EXPECT_EQ(radicle_plugin_set_record_components(plugin.get(), key_of(0x999), entries, 2),
            RADICLE_STATUS_NOT_FOUND);
  EXPECT_EQ(radicle_plugin_set_record_components(plugin.get(), key_of(0x804), entries, 2),
            RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_plugin_set_record_components(plugin.get(), key_of(0x800), nullptr, 2),
            RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_plugin_set_record_components(nullptr, key_of(0x800), entries, 0),
            RADICLE_STATUS_INVALID_ARGUMENT);
}

TEST_F(FormsModulesCApi, EyesCreateDecodeAndSetText) {
  auto plugin = read_plugin();
  radicle_eyes_creation creation{};
  creation.editor_id = "TestEyes";
  creation.full_text = "Blue eyes";
  creation.icon = "eye.dds";
  creation.data_flags = 0x02;
  radicle_form_key created{};
  ASSERT_EQ(radicle_plugin_create_eyes(plugin.get(), &creation, &created), RADICLE_STATUS_OK);
  ASSERT_NE(created.plugin, nullptr);
  EXPECT_STREQ(created.plugin, kPlugin);
  EXPECT_GT(created.local_id, 0u);

  auto out = write_out(plugin.get());
  {
    const auto env = open_env(out);
    const auto* record = by_editor_id(env.get(), "TestEyes", "EYES");
    ASSERT_NE(record, nullptr);
    radicle_eyes_info* info = nullptr;
    ASSERT_EQ(radicle_eyes_decode(env.get(), record, &info), RADICLE_STATUS_OK);
    const EyesPtr view(info, radicle_eyes_free);
    ASSERT_NE(view->editor_id, nullptr);
    EXPECT_STREQ(view->editor_id, "TestEyes");
    ASSERT_NE(view->full_text, nullptr);
    EXPECT_STREQ(view->full_text, "Blue eyes");
    ASSERT_NE(view->icon, nullptr);
    EXPECT_STREQ(view->icon, "eye.dds");
    EXPECT_EQ(view->data_flags, 0x02);
    EXPECT_GT(view->full_raw_size, 0u);
    EXPECT_EQ(view->record.local_id, created.local_id);
  }

  ASSERT_EQ(radicle_plugin_set_eyes_text(plugin.get(), created, nullptr, "Green eyes", nullptr),
            RADICLE_STATUS_OK);
  out = write_out(plugin.get());
  {
    const auto env = open_env(out);
    const auto* record = by_editor_id(env.get(), "TestEyes", "EYES");
    ASSERT_NE(record, nullptr);
    radicle_eyes_info* info = nullptr;
    ASSERT_EQ(radicle_eyes_decode(env.get(), record, &info), RADICLE_STATUS_OK);
    const EyesPtr view(info, radicle_eyes_free);
    ASSERT_NE(view->full_text, nullptr);
    EXPECT_STREQ(view->full_text, "Green eyes");
    ASSERT_NE(view->icon, nullptr);
    EXPECT_STREQ(view->icon, "eye.dds");
  }

  EXPECT_EQ(radicle_plugin_set_eyes_text(plugin.get(), key_of(0x999), nullptr, "x", nullptr),
            RADICLE_STATUS_NOT_FOUND);
  creation.record_flags = 1;
  EXPECT_EQ(radicle_plugin_create_eyes(plugin.get(), &creation, &created),
            RADICLE_STATUS_INVALID_ARGUMENT);
  creation.record_flags = 0;
  creation.icon = nullptr;
  EXPECT_EQ(radicle_plugin_create_eyes(plugin.get(), &creation, &created),
            RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_plugin_create_eyes(nullptr, &creation, &created),
            RADICLE_STATUS_INVALID_ARGUMENT);
}

TEST_F(FormsModulesCApi, ArmorCreateDecodeAndSetModels) {
  auto plugin = read_plugin();
  radicle_armor_creation creation{};
  creation.editor_id = "TestArmor";
  creation.male_world_model = "male.nif";
  creation.male_icon = "icon.dds";
  creation.description = "An armor";
  creation.value = 12;
  creation.weight = 3.5f;
  creation.health = 20;
  radicle_form_key armor_key{};
  ASSERT_EQ(radicle_plugin_create_armor(plugin.get(), &creation, &armor_key), RADICLE_STATUS_OK);
  ASSERT_NE(armor_key.plugin, nullptr);
  EXPECT_GT(armor_key.local_id, 0u);

  radicle_armor_addon_creation addon{};
  addon.editor_id = "TestAddon";
  addon.male_biped_model = "m.nif";
  addon.female_biped_model = "f.nif";
  addon.male_first_person_model = "m1.nif";
  addon.female_first_person_model = "f1.nif";
  addon.male_priority = 5;
  addon.female_priority = 6;
  addon.male_weight_slider = 3;
  addon.female_weight_slider = 2;
  addon.unknown_before_detection[0] = 0x11;
  addon.unknown_before_detection[1] = 0x22;
  addon.detection_sound_value = 3;
  addon.unknown_before_weapon_adjust = 0x44;
  addon.weapon_adjust = 1.25f;
  radicle_form_key addon_key{};
  ASSERT_EQ(radicle_plugin_create_armor_addon(plugin.get(), &addon, &addon_key), RADICLE_STATUS_OK);
  EXPECT_GT(addon_key.local_id, 0u);

  auto out = write_out(plugin.get());
  {
    const auto env = open_env(out);
    const auto* record = by_editor_id(env.get(), "TestArmor", "ARMO");
    ASSERT_NE(record, nullptr);
    radicle_armor_info* info = nullptr;
    ASSERT_EQ(radicle_armor_decode(env.get(), record, &info), RADICLE_STATUS_OK);
    const ArmorPtr view(info, radicle_armor_free);
    ASSERT_NE(view->editor_id, nullptr);
    EXPECT_STREQ(view->editor_id, "TestArmor");
    ASSERT_NE(view->male_world_model, nullptr);
    EXPECT_STREQ(view->male_world_model, "male.nif");
    ASSERT_NE(view->male_icon, nullptr);
    EXPECT_STREQ(view->male_icon, "icon.dds");
    EXPECT_EQ(view->female_world_model, nullptr);
    EXPECT_EQ(view->male_message_icon, nullptr);
    EXPECT_EQ(view->value, 12);
    EXPECT_FLOAT_EQ(view->weight, 3.5f);
    EXPECT_EQ(view->health, 20u);
    EXPECT_EQ(view->full_name_kind, RADICLE_NAME_ABSENT);
    EXPECT_EQ(view->description_kind, RADICLE_NAME_INLINE);
    ASSERT_NE(view->description_text, nullptr);
    EXPECT_STREQ(view->description_text, "An armor");

    const auto* addon_record = by_editor_id(env.get(), "TestAddon", "ARMA");
    ASSERT_NE(addon_record, nullptr);
    radicle_armor_addon_info* addon_info = nullptr;
    ASSERT_EQ(radicle_armor_addon_decode(env.get(), addon_record, &addon_info), RADICLE_STATUS_OK);
    const ArmorAddonPtr addon_view(addon_info, radicle_armor_addon_free);
    ASSERT_NE(addon_view->male_biped_model, nullptr);
    EXPECT_STREQ(addon_view->male_biped_model, "m.nif");
    EXPECT_STREQ(addon_view->female_biped_model, "f.nif");
    EXPECT_STREQ(addon_view->male_first_person_model, "m1.nif");
    EXPECT_STREQ(addon_view->female_first_person_model, "f1.nif");
    EXPECT_EQ(addon_view->male_priority, 5);
    EXPECT_EQ(addon_view->female_priority, 6);
    EXPECT_EQ(addon_view->male_weight_slider, 3);
    EXPECT_EQ(addon_view->female_weight_slider, 2);
    EXPECT_EQ(addon_view->unknown_before_detection[0], 0x11);
    EXPECT_EQ(addon_view->unknown_before_detection[1], 0x22);
    EXPECT_EQ(addon_view->detection_sound_value, 3);
    EXPECT_EQ(addon_view->unknown_before_weapon_adjust, 0x44);
    EXPECT_FLOAT_EQ(addon_view->weapon_adjust, 1.25f);
  }

  ASSERT_EQ(radicle_plugin_set_armor_models(plugin.get(), armor_key, "male2.nif", nullptr,
                                            nullptr, nullptr, nullptr, nullptr),
            RADICLE_STATUS_OK);
  ASSERT_EQ(radicle_plugin_set_armor_addon_models(plugin.get(), addon_key, nullptr, "f2.nif",
                                                 nullptr, nullptr),
            RADICLE_STATUS_OK);
  out = write_out(plugin.get());
  {
    const auto env = open_env(out);
    radicle_armor_info* info = nullptr;
    ASSERT_EQ(radicle_armor_decode(env.get(), by_editor_id(env.get(), "TestArmor", "ARMO"), &info),
              RADICLE_STATUS_OK);
    const ArmorPtr view(info, radicle_armor_free);
    ASSERT_NE(view->male_world_model, nullptr);
    EXPECT_STREQ(view->male_world_model, "male2.nif");
    ASSERT_NE(view->male_icon, nullptr);
    EXPECT_STREQ(view->male_icon, "icon.dds");

    radicle_armor_addon_info* addon_info = nullptr;
    ASSERT_EQ(radicle_armor_addon_decode(
                  env.get(), by_editor_id(env.get(), "TestAddon", "ARMA"), &addon_info),
              RADICLE_STATUS_OK);
    const ArmorAddonPtr addon_view(addon_info, radicle_armor_addon_free);
    ASSERT_NE(addon_view->male_biped_model, nullptr);
    EXPECT_STREQ(addon_view->male_biped_model, "m.nif");
    ASSERT_NE(addon_view->female_biped_model, nullptr);
    EXPECT_STREQ(addon_view->female_biped_model, "f2.nif");
  }

  EXPECT_EQ(radicle_plugin_set_armor_models(plugin.get(), key_of(0x999), "a.nif", nullptr, nullptr,
                                            nullptr, nullptr, nullptr),
            RADICLE_STATUS_NOT_FOUND);
  radicle_armor_creation bad_text = creation;
  bad_text.editor_id = "\xE4\xB8\xAD";
  EXPECT_EQ(radicle_plugin_create_armor(plugin.get(), &bad_text, &armor_key),
            RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_plugin_create_armor(nullptr, &creation, &armor_key),
            RADICLE_STATUS_INVALID_ARGUMENT);
}

TEST_F(FormsModulesCApi, FactionCreateRelationAndRank) {
  auto plugin = read_plugin();
  radicle_form_key faction_key{};
  ASSERT_EQ(radicle_plugin_create_faction(plugin.get(), "TestFaction", "Test Faction", 0x1,
                                          &faction_key),
            RADICLE_STATUS_OK);
  ASSERT_NE(faction_key.plugin, nullptr);
  ASSERT_EQ(radicle_plugin_add_faction_relation(plugin.get(), faction_key, faction_key, -1, 1),
            RADICLE_STATUS_OK);

  radicle_faction_rank_input rank{};
  rank.male_title_kind = RADICLE_NAME_INLINE;
  rank.male_title_text = "Rank one";
  rank.female_title_kind = RADICLE_NAME_ABSENT;
  ASSERT_EQ(radicle_plugin_add_faction_rank(plugin.get(), faction_key, 0, &rank), RADICLE_STATUS_OK);

  auto out = write_out(plugin.get());
  {
    const auto env = open_env(out);
    const auto* record = by_editor_id(env.get(), "TestFaction", "FACT");
    ASSERT_NE(record, nullptr);
    radicle_faction_relations* relations = nullptr;
    ASSERT_EQ(radicle_faction_relations_decode(env.get(), record, &relations), RADICLE_STATUS_OK);
    const RelationsPtr relations_view(relations, radicle_faction_relations_free);
    ASSERT_EQ(relations_view->relation_count, 1u);
    ASSERT_NE(relations_view->relations, nullptr);
    EXPECT_STREQ(relations_view->relations[0].faction.plugin, kPlugin);
    EXPECT_EQ(relations_view->relations[0].faction.local_id, faction_key.local_id);
    EXPECT_EQ(relations_view->relations[0].modifier, -1);
    EXPECT_EQ(relations_view->relations[0].reaction, 1u);

    radicle_faction_ranks* ranks = nullptr;
    ASSERT_EQ(radicle_faction_ranks_decode(env.get(), record, &ranks), RADICLE_STATUS_OK);
    const RanksPtr ranks_view(ranks, radicle_faction_ranks_free);
    ASSERT_EQ(ranks_view->rank_count, 1u);
    ASSERT_NE(ranks_view->ranks, nullptr);
    EXPECT_EQ(ranks_view->ranks[0].number, 0u);
    EXPECT_EQ(ranks_view->ranks[0].male_title_kind, RADICLE_NAME_INLINE);
    ASSERT_NE(ranks_view->ranks[0].male_title_text, nullptr);
    EXPECT_STREQ(ranks_view->ranks[0].male_title_text, "Rank one");
    EXPECT_EQ(ranks_view->ranks[0].female_title_kind, RADICLE_NAME_ABSENT);
  }

  ASSERT_EQ(radicle_plugin_set_faction_relation(plugin.get(), faction_key, 0, faction_key, 7, 3),
            RADICLE_STATUS_OK);
  radicle_faction_rank_input second{};
  second.male_title_kind = RADICLE_NAME_INLINE;
  second.male_title_text = "Rank two";
  second.female_title_kind = RADICLE_NAME_INLINE;
  second.female_title_text = "Female two";
  ASSERT_EQ(radicle_plugin_add_faction_rank(plugin.get(), faction_key, 1, &second),
            RADICLE_STATUS_OK);
  out = write_out(plugin.get());
  {
    const auto env = open_env(out);
    const auto* record = by_editor_id(env.get(), "TestFaction", "FACT");
    ASSERT_NE(record, nullptr);
    radicle_faction_relations* relations = nullptr;
    ASSERT_EQ(radicle_faction_relations_decode(env.get(), record, &relations), RADICLE_STATUS_OK);
    const RelationsPtr relations_view(relations, radicle_faction_relations_free);
    ASSERT_EQ(relations_view->relation_count, 1u);
    ASSERT_NE(relations_view->relations, nullptr);
    EXPECT_EQ(relations_view->relations[0].modifier, 7);
    EXPECT_EQ(relations_view->relations[0].reaction, 3u);

    radicle_faction_ranks* ranks = nullptr;
    ASSERT_EQ(radicle_faction_ranks_decode(env.get(), record, &ranks), RADICLE_STATUS_OK);
    const RanksPtr ranks_view(ranks, radicle_faction_ranks_free);
    ASSERT_EQ(ranks_view->rank_count, 2u);
    ASSERT_NE(ranks_view->ranks, nullptr);
    EXPECT_EQ(ranks_view->ranks[1].number, 1u);
    ASSERT_NE(ranks_view->ranks[1].female_title_text, nullptr);
    EXPECT_STREQ(ranks_view->ranks[1].female_title_text, "Female two");
    EXPECT_EQ(ranks_view->ranks[1].female_title_kind, RADICLE_NAME_INLINE);
  }

  EXPECT_EQ(radicle_plugin_set_faction_relation(plugin.get(), faction_key, 9, faction_key, 0, 0),
            RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_plugin_remove_faction_relation(plugin.get(), faction_key, 9),
            RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_plugin_add_faction_relation(plugin.get(), faction_key, key_of(0x999), 0, 1),
            RADICLE_STATUS_INVALID_ARGUMENT);
  radicle_faction_rank_input orphan{};
  orphan.male_title_kind = RADICLE_NAME_INLINE;
  EXPECT_EQ(radicle_plugin_add_faction_rank(plugin.get(), faction_key, 0, &orphan),
            RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_plugin_add_faction_rank(plugin.get(), faction_key, 0, nullptr),
            RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_plugin_create_faction(nullptr, "X", nullptr, 0, &faction_key),
            RADICLE_STATUS_INVALID_ARGUMENT);
}

TEST_F(FormsModulesCApi, ExteriorCellLayoutUsesFlooredGridGroups) {
  radicle_exterior_cell_layout layout{};
  ASSERT_EQ(radicle_exterior_cell_layout_for(-33, 40, &layout), RADICLE_STATUS_OK);
  EXPECT_EQ(layout.cell_x, -33);
  EXPECT_EQ(layout.cell_y, 40);
  EXPECT_EQ(layout.block_x, -2);
  EXPECT_EQ(layout.block_y, 1);
  EXPECT_EQ(layout.subblock_x, -5);
  EXPECT_EQ(layout.subblock_y, 5);

  EXPECT_EQ(radicle_exterior_cell_layout_for(111, 0, &layout), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_exterior_cell_layout_for(0, -111, &layout), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_exterior_cell_layout_for(0, 0, nullptr), RADICLE_STATUS_INVALID_ARGUMENT);

  std::int32_t valid = -1;
  EXPECT_EQ(radicle_exterior_cell_coordinate_valid(110, &valid), RADICLE_STATUS_OK);
  EXPECT_EQ(valid, 1);
  EXPECT_EQ(radicle_exterior_cell_coordinate_valid(-111, &valid), RADICLE_STATUS_OK);
  EXPECT_EQ(valid, 0);
  EXPECT_EQ(radicle_exterior_cell_coordinate_valid(0, nullptr), RADICLE_STATUS_INVALID_ARGUMENT);
}

TEST_F(FormsModulesCApi, InteriorCellCreationAndExteriorRefusal) {
  auto plugin = read_plugin();
  radicle_form_key cell_key{};
  ASSERT_EQ(radicle_plugin_create_interior_cell(plugin.get(), "TestCell", &cell_key),
            RADICLE_STATUS_OK);
  ASSERT_NE(cell_key.plugin, nullptr);
  EXPECT_STREQ(cell_key.plugin, kPlugin);
  EXPECT_GT(cell_key.local_id, 0u);
  const auto out = write_out(plugin.get());
  const auto env = open_env(out);
  EXPECT_NE(by_editor_id(env.get(), "TestCell", "CELL"), nullptr);

  radicle_exterior_cell_creation creation{};
  creation.worldspace = key_of(0x800);
  creation.editor_id = "TestExterior";
  EXPECT_EQ(radicle_plugin_create_exterior_cell(plugin.get(), &creation, &cell_key),
            RADICLE_STATUS_INVALID_ARGUMENT);
  creation.flags = 1;
  EXPECT_EQ(radicle_plugin_create_exterior_cell(plugin.get(), &creation, &cell_key),
            RADICLE_STATUS_INVALID_ARGUMENT);
  creation.worldspace = radicle_form_key{nullptr, 0};
  EXPECT_EQ(radicle_plugin_create_exterior_cell(plugin.get(), &creation, &cell_key),
            RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_plugin_create_interior_cell(nullptr, "TestCell", &cell_key),
            RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_plugin_create_interior_cell(plugin.get(), nullptr, &cell_key),
            RADICLE_STATUS_INVALID_ARGUMENT);

  const radicle_record* record = nullptr;
  EXPECT_EQ(radicle_env_find_exterior_cell(env.get(), key_of(0x800), 0, 0, &record),
            RADICLE_STATUS_NOT_FOUND);
  EXPECT_EQ(radicle_env_find_persistent_exterior_cell(env.get(), key_of(0x800), &record),
            RADICLE_STATUS_NOT_FOUND);
  EXPECT_EQ(radicle_env_find_exterior_cell(env.get(), key_of(0x800), 111, 0, &record),
            RADICLE_STATUS_INVALID_ARGUMENT);
}

}  // namespace
