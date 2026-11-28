#include "fo4/c_api.h"
#include "fo4/c_api_authoring.h"
#include "fo4/localization/strings.hpp"
#include "fo4/c_api_authoring.h"
#include "fo4/c_api_cell.h"
#include "fo4/c_api_cell_edit.h"
#include "fo4/c_api_components.h"
#include "fo4/c_api_env.h"
#include "fo4/c_api_faction.h"
#include "fo4/c_api_pet_write.h"
#include "fo4/c_api_records.h"
#include "fo4/forms/faction.hpp"
#include "fo4/plugin_core/reader.hpp"
#include "plugin_bytes.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

constexpr const char* kPlugin = "Forms.esp";

using EnvPtr = std::unique_ptr<radicle_env, decltype(&radicle_env_destroy)>;
using PluginPtr = std::unique_ptr<radicle_plugin, decltype(&radicle_plugin_destroy)>;

radicle_form_key key_of(std::uint32_t local_id) {
  return radicle_form_key{kPlugin, local_id};
}

std::string link_of(std::uint32_t local_id) {
  char text[32]{};
  std::snprintf(text, sizeof(text), "%06X:%s", static_cast<unsigned>(local_id), kPlugin);
  return text;
}

std::string field_of(const radicle_env* env, const radicle_record* record, const char* path, std::size_t index) {
  char* text = nullptr;
  EXPECT_EQ(radicle_record_get_field(env, record, path, index, &text), RADICLE_STATUS_OK) << path << " " << index;
  std::string out = text == nullptr ? std::string{} : std::string(text);
  radicle_record_scalar_free(text);
  return out;
}

std::vector<fo4::forms::FactionRelation> relations_of(const std::filesystem::path& path, std::uint32_t local_id) {
  const auto plugin = fo4::read_plugin_file(path);
  return fo4::forms::decode_faction_relations(plugin, fo4::FormKey::parse(link_of(local_id)));
}

std::vector<fo4::forms::FactionRank> ranks_of(const std::filesystem::path& path, std::uint32_t local_id) {
  const auto plugin = fo4::read_plugin_file(path);
  return fo4::forms::decode_faction_ranks(plugin, fo4::FormKey::parse(link_of(local_id)));
}

std::size_t list_size_of(const radicle_env* env, const radicle_record* record, const char* path) {
  std::size_t size = 0;
  EXPECT_EQ(radicle_record_list_size(env, record, path, &size), RADICLE_STATUS_OK) << path;
  return size;
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
  fo4::test::ByteWriter resistances;
  resistances.u32(0x800);
  resistances.u32(5);
  resistances.u32(0x804);
  resistances.u32(7);
  fo4::test::write_subrecord(out, "DAMC", resistances.bytes);
  fo4::test::end_record(out, misc);
  fo4::test::end_group(out, miscs);

  const auto armors = fo4::test::begin_group(out, "ARMO", 0);
  const auto armor = fo4::test::begin_record(out, "ARMO", 0, 0x804);
  fo4::test::write_subrecord(out, "EDID", std::string_view("Plain\0", 6));
  fo4::test::end_record(out, armor);
  fo4::test::end_group(out, armors);

  const auto potions = fo4::test::begin_group(out, "ALCH", 0);
  const auto potion = fo4::test::begin_record(out, "ALCH", 0, 0x810);
  fo4::test::write_subrecord(out, "EDID", std::string_view("Potion\0", 7));
  fo4::test::write_subrecord(out, "ICON", std::string_view("caf\xE9\0", 5));
  fo4::test::ByteWriter enit;
  enit.u32(25);
  enit.u32(2);
  for (int word = 0; word < 3; ++word) enit.u32(0);
  fo4::test::write_subrecord(out, "ENIT", enit.bytes);
  fo4::test::ByteWriter weight;
  weight.f32(2.5f);
  fo4::test::write_subrecord(out, "DATA", weight.bytes);
  fo4::test::end_record(out, potion);
  fo4::test::end_group(out, potions);

  const auto messages = fo4::test::begin_group(out, "MESG", 0);
  const auto message = fo4::test::begin_record(out, "MESG", 0, 0x811);
  fo4::test::write_subrecord(out, "EDID", std::string_view("Message\0", 8));
  fo4::test::ByteWriter flags;
  flags.u32(3);
  fo4::test::write_subrecord(out, "DNAM", flags.bytes);
  fo4::test::ByteWriter time;
  time.u32(7);
  fo4::test::write_subrecord(out, "TNAM", time.bytes);
  fo4::test::end_record(out, message);
  fo4::test::end_group(out, messages);
  return out.bytes;
}

std::vector<std::byte> localized_fixture() {
  fo4::test::ByteWriter out;
  const auto tes4 = fo4::test::begin_record(out, "TES4", 0x80, 0);
  fo4::test::ByteWriter hedr;
  hedr.f32(1.0f);
  hedr.u32(1);
  hedr.u32(0x900);
  fo4::test::write_subrecord(out, "HEDR", hedr.bytes);
  fo4::test::end_record(out, tes4);

  const auto settings = fo4::test::begin_group(out, "GMST", 0);
  const auto setting = fo4::test::begin_record(out, "GMST", 0, 0x800);
  fo4::test::write_subrecord(out, "EDID", std::string_view("sLabel\0", 7));
  fo4::test::ByteWriter string_id;
  string_id.u32(0x1234);
  fo4::test::write_subrecord(out, "DATA", string_id.bytes);
  fo4::test::end_record(out, setting);
  fo4::test::end_group(out, settings);
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

  EnvPtr open_env_paths(const std::vector<std::filesystem::path>& plugins) {
    std::vector<std::string> storage;
    storage.reserve(plugins.size());
    std::vector<const char*> paths;
    paths.reserve(plugins.size());
    for (const auto& plugin : plugins) {
      storage.push_back(plugin.string());
      paths.push_back(storage.back().c_str());
    }
    radicle_env* env = nullptr;
    EXPECT_EQ(radicle_env_load(paths.data(), paths.size(), &env), RADICLE_STATUS_OK) << radicle_last_error();
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

// Constraint: detects the MISC component rows reading a count or component link from the wrong word of a CVPA pair, or dropping an entry.
TEST_F(FormsModulesCApi, ComponentRowsReadStoredEntries) {
  const auto env = open_env(input_);
  const auto* record = winner(env.get(), 0x800);
  ASSERT_NE(record, nullptr);
  ASSERT_EQ(list_size_of(env.get(), record, "MISC\\Components"), 2u);
  EXPECT_EQ(field_of(env.get(), record, "MISC\\ComponentObject", 0), link_of(0x800));
  EXPECT_EQ(field_of(env.get(), record, "MISC\\ComponentCount", 0), "2");
  EXPECT_EQ(field_of(env.get(), record, "MISC\\ComponentObject", 1), link_of(0x804));
  EXPECT_EQ(field_of(env.get(), record, "MISC\\ComponentCount", 1), "1");
}

// Constraint: detects a component row answering for a record type that carries no component array, or for a null record.
TEST_F(FormsModulesCApi, ComponentRowsRefuseARecordWithoutComponents) {
  const auto env = open_env(input_);
  const auto* record = winner(env.get(), 0x804);
  ASSERT_NE(record, nullptr);
  char* text = nullptr;
  EXPECT_EQ(radicle_record_get_field(env.get(), record, "MISC\\ComponentObject", 0, &text),
            RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(text, nullptr);
  EXPECT_EQ(radicle_record_get_field(env.get(), nullptr, "MISC\\ComponentObject", 0, &text),
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
  ASSERT_EQ(list_size_of(env.get(), record, "MISC\\Components"), 2u);
  EXPECT_EQ(field_of(env.get(), record, "MISC\\ComponentObject", 0), link_of(0x804));
  EXPECT_EQ(field_of(env.get(), record, "MISC\\ComponentCount", 0), "4");
  EXPECT_EQ(field_of(env.get(), record, "MISC\\ComponentObject", 1), link_of(0x800));
  EXPECT_EQ(field_of(env.get(), record, "MISC\\ComponentCount", 1), "7");

  EXPECT_EQ(radicle_plugin_set_record_components(plugin.get(), key_of(0x999), entries, 2),
            RADICLE_STATUS_NOT_FOUND);
  EXPECT_EQ(radicle_plugin_set_record_components(plugin.get(), key_of(0x804), entries, 2),
            RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_plugin_set_record_components(plugin.get(), key_of(0x800), nullptr, 2),
            RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_plugin_set_record_components(nullptr, key_of(0x800), entries, 0),
            RADICLE_STATUS_INVALID_ARGUMENT);
}

// Constraint: detects the faction create, relation and rank exports writing XNAM or rank subrecords that the shared FACT decoder reads back differently (faction link, modifier, reaction, rank number, inline titles).
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
    const auto relations = relations_of(out, faction_key.local_id);
    ASSERT_EQ(relations.size(), 1u);
    EXPECT_EQ(relations[0].faction.to_string(), link_of(faction_key.local_id));
    EXPECT_EQ(relations[0].modifier, -1);
    EXPECT_EQ(static_cast<std::uint32_t>(relations[0].reaction), 1u);

    const auto ranks = ranks_of(out, faction_key.local_id);
    ASSERT_EQ(ranks.size(), 1u);
    EXPECT_EQ(ranks[0].number, 0u);
    ASSERT_TRUE(ranks[0].male_title.has_value());
    EXPECT_FALSE(ranks[0].male_title->localized());
    EXPECT_EQ(ranks[0].male_title->inline_text(), "Rank one");
    EXPECT_FALSE(ranks[0].female_title.has_value());
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
    const auto relations = relations_of(out, faction_key.local_id);
    ASSERT_EQ(relations.size(), 1u);
    EXPECT_EQ(relations[0].modifier, 7);
    EXPECT_EQ(static_cast<std::uint32_t>(relations[0].reaction), 3u);

    const auto ranks = ranks_of(out, faction_key.local_id);
    ASSERT_EQ(ranks.size(), 2u);
    EXPECT_EQ(ranks[1].number, 1u);
    ASSERT_TRUE(ranks[1].female_title.has_value());
    EXPECT_FALSE(ranks[1].female_title->localized());
    EXPECT_EQ(ranks[1].female_title->inline_text(), "Female two");
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

std::string scalar_of(const radicle_env* env, const radicle_record* record, const char* path,
                      std::size_t index = 0) {
  char* text = nullptr;
  EXPECT_EQ(radicle_record_get_scalar(env, record, path, index, &text), RADICLE_STATUS_OK)
      << radicle_last_error();
  const std::string result = text == nullptr ? std::string("<null>") : std::string(text);
  radicle_record_scalar_free(text);
  return result;
}

// Detects: a read that returns the wrong field of a subrecord (whole-payload f32 versus embedded i32/u32), or text the writer would not accept back, so a UI shows a wrong Value, Weight, flag word or display time; each value must read in the writer's own text form and survive a write and reload.
TEST_F(FormsModulesCApi, RecordGetScalarReadsEmbeddedAndWholePayloadFieldsInWriterText) {
  const auto env = open_env(input_);
  const auto* potion = winner(env.get(), 0x810);
  const auto* message = winner(env.get(), 0x811);
  ASSERT_NE(potion, nullptr);
  ASSERT_NE(message, nullptr);
  EXPECT_EQ(scalar_of(env.get(), potion, "ALCH\\Value"), "25");
  EXPECT_EQ(scalar_of(env.get(), potion, "ALCH\\Weight"), "2.5");
  EXPECT_EQ(scalar_of(env.get(), potion, "ALCH\\Flags"), "2");
  EXPECT_EQ(scalar_of(env.get(), message, "MESG\\Flags"), "3");
  EXPECT_EQ(scalar_of(env.get(), message, "MESG\\DisplayTime"), "7");

  auto plugin = read_plugin();
  ASSERT_EQ(radicle_plugin_set_record_scalar_at(plugin.get(), key_of(0x810), "ALCH\\Value", 0, "30"),
            RADICLE_STATUS_OK);
  ASSERT_EQ(radicle_plugin_set_record_scalar_at(plugin.get(), key_of(0x810), "ALCH\\Weight", 0, "0.25"),
            RADICLE_STATUS_OK);
  const auto edited = open_env(write_out(plugin.get()));
  const auto* reloaded = winner(edited.get(), 0x810);
  ASSERT_NE(reloaded, nullptr);
  EXPECT_EQ(scalar_of(edited.get(), reloaded, "ALCH\\Value"), "30");
  EXPECT_EQ(scalar_of(edited.get(), reloaded, "ALCH\\Weight"), "0.25");
}

// Detects: a string field read that returns the stored Windows-1252 byte 0xE9 unconverted (invalid UTF-8 for a UI) or text radicle_plugin_set_record_string would not store back; the field must read as UTF-8 and survive a write and reload.
TEST_F(FormsModulesCApi, RecordGetScalarReadsAWindows1252StringFieldAsUtf8AndItWritesBack) {
  const auto env = open_env(input_);
  const auto* potion = winner(env.get(), 0x810);
  ASSERT_NE(potion, nullptr);
  EXPECT_EQ(scalar_of(env.get(), potion, "ALCH\\Icon"), "caf\xC3\xA9");

  auto plugin = read_plugin();
  ASSERT_EQ(radicle_plugin_set_record_string(plugin.get(), key_of(0x810), "ALCH\\Icon", 0, "caf\xC3\xA9"),
            RADICLE_STATUS_OK);
  ASSERT_EQ(radicle_plugin_set_record_string(plugin.get(), key_of(0x810), "ALCH\\Icon", 0, "\xC3\xA9t\xC3\xA9"),
            RADICLE_STATUS_OK);
  const auto edited = open_env(write_out(plugin.get()));
  const auto* reloaded = winner(edited.get(), 0x810);
  ASSERT_NE(reloaded, nullptr);
  EXPECT_EQ(scalar_of(edited.get(), reloaded, "ALCH\\Icon"), "\xC3\xA9t\xC3\xA9");
}

// Constraint: detects an EYES name, texture or flag read or write through the generic C API going wrong now that radicle_eyes_decode is gone.
TEST_F(FormsModulesCApi, EyesTextureAndFlagsReadAndWriteThroughTheGenericFieldApi) {
  fo4::test::ByteWriter out;
  const auto tes4 = fo4::test::begin_record(out, "TES4", 0, 0);
  fo4::test::ByteWriter hedr;
  hedr.f32(1.0f);
  hedr.u32(1);
  hedr.u32(0x900);
  fo4::test::write_subrecord(out, "HEDR", hedr.bytes);
  fo4::test::end_record(out, tes4);
  const auto group = fo4::test::begin_group(out, "EYES", 0);
  const auto eyes = fo4::test::begin_record(out, "EYES", 0, 0x800);
  fo4::test::write_subrecord(out, "EDID", std::string_view("TestEyes\0", 9));
  fo4::test::write_subrecord(out, "FULL", std::string_view("Blue eyes\0", 10));
  fo4::test::write_subrecord(out, "ICON", std::string_view("eye.dds\0", 8));
  fo4::test::ByteWriter data;
  data.u8(0x02);
  fo4::test::write_subrecord(out, "DATA", data.bytes);
  fo4::test::end_record(out, eyes);
  fo4::test::end_group(out, group);
  const auto path = root_ / kPlugin;
  {
    std::ofstream stream(path, std::ios::binary);
    stream.write(reinterpret_cast<const char*>(out.bytes.data()),
                 static_cast<std::streamsize>(out.bytes.size()));
  }

  {
    const auto env = open_env(path);
    const auto* record = by_editor_id(env.get(), "TestEyes", "EYES");
    ASSERT_NE(record, nullptr);
    radicle_record_info info{};
    ASSERT_EQ(radicle_record_get_info(env.get(), record, &info), RADICLE_STATUS_OK);
    EXPECT_STREQ(info.editor_id, "TestEyes");
    EXPECT_EQ(info.name_kind, RADICLE_NAME_INLINE);
    EXPECT_STREQ(info.name_text, "Blue eyes");
    EXPECT_EQ(scalar_of(env.get(), record, "EYES\\Texture"), "eye.dds");
    EXPECT_EQ(scalar_of(env.get(), record, "EYES\\Flags"), "2");
  }

  radicle_plugin* raw = nullptr;
  ASSERT_EQ(radicle_plugin_read_file(path.string().c_str(), &raw), RADICLE_STATUS_OK);
  const PluginPtr plugin(raw, radicle_plugin_destroy);
  ASSERT_EQ(radicle_plugin_set_record_string(plugin.get(), key_of(0x800), "EYES\\Texture", 0, "green.dds"),
            RADICLE_STATUS_OK);
  ASSERT_EQ(radicle_plugin_set_record_string(plugin.get(), key_of(0x800), "EYES\\Name", 0, "Green eyes"),
            RADICLE_STATUS_OK);
  ASSERT_EQ(radicle_plugin_set_record_scalar_at(plugin.get(), key_of(0x800), "EYES\\Flags", 0, "7"),
            RADICLE_STATUS_OK);
  EXPECT_EQ(radicle_plugin_set_record_scalar_at(plugin.get(), key_of(0x800), "EYES\\Flags", 0, "256"),
            RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_plugin_set_record_scalar_at(plugin.get(), key_of(0x800), "EYES\\Flags", 0, "x"),
            RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_plugin_set_record_scalar_at(plugin.get(), key_of(0x999), "EYES\\Flags", 0, "1"),
            RADICLE_STATUS_NOT_FOUND);

  const auto edited = open_env(write_out(plugin.get()));
  const auto* reloaded = by_editor_id(edited.get(), "TestEyes", "EYES");
  ASSERT_NE(reloaded, nullptr);
  EXPECT_EQ(scalar_of(edited.get(), reloaded, "EYES\\Texture"), "green.dds");
  radicle_record_info edited_info{};
  ASSERT_EQ(radicle_record_get_info(edited.get(), reloaded, &edited_info), RADICLE_STATUS_OK);
  EXPECT_STREQ(edited_info.name_text, "Green eyes");
  EXPECT_EQ(scalar_of(edited.get(), reloaded, "EYES\\Name"), "Green eyes");
  EXPECT_EQ(scalar_of(edited.get(), reloaded, "EYES\\Flags"), "7");
}

// Detects: an ARMO or ARMA created through the generic API that cannot gain a model path (the model subrecords refusing the generic add, or the string rows refusing a record that has no model yet), a path, icon, value, priority or weapon-adjust row that writes the wrong subrecord or offset, an edit of one model that disturbs another, and a model that does not survive a write and reload, which must all fail here.
TEST_F(FormsModulesCApi, ArmorAndAddonAuthorModelsThroughTheGenericApi) {
  auto plugin = read_plugin();
  radicle_form_key armor_key{};
  ASSERT_EQ(radicle_plugin_create_record(plugin.get(), "ARMO", "TestArmor", &armor_key),
            RADICLE_STATUS_OK);
  ASSERT_NE(armor_key.plugin, nullptr);
  EXPECT_GT(armor_key.local_id, 0u);
  radicle_form_key addon_key{};
  ASSERT_EQ(radicle_plugin_create_record(plugin.get(), "ARMA", "TestAddon", &addon_key),
            RADICLE_STATUS_OK);
  EXPECT_GT(addon_key.local_id, 0u);

  EXPECT_EQ(radicle_plugin_set_record_string(plugin.get(), armor_key, "ARMO\\MaleWorldModel", 0, "x.nif"),
            RADICLE_STATUS_NOT_FOUND);
  for (const char* signature : {"MOD2", "ICON"}) {
    ASSERT_EQ(radicle_plugin_add_subrecord(plugin.get(), armor_key, signature), RADICLE_STATUS_OK)
        << signature << radicle_last_error();
  }
  ASSERT_EQ(radicle_plugin_set_record_string(plugin.get(), armor_key, "ARMO\\MaleWorldModel", 0, "male.nif"),
            RADICLE_STATUS_OK);
  ASSERT_EQ(radicle_plugin_set_record_string(plugin.get(), armor_key, "ARMO\\MaleIcon", 0, "icon.dds"),
            RADICLE_STATUS_OK);
  ASSERT_EQ(radicle_plugin_set_record_scalar_at(plugin.get(), armor_key, "ARMO\\Value", 0, "12"),
            RADICLE_STATUS_OK);
  ASSERT_EQ(radicle_plugin_set_record_scalar_at(plugin.get(), armor_key, "ARMO\\Weight", 0, "3.5"),
            RADICLE_STATUS_OK);
  ASSERT_EQ(radicle_plugin_set_record_scalar_at(plugin.get(), armor_key, "ARMO\\Health", 0, "20"),
            RADICLE_STATUS_OK);

  for (const char* signature : {"MOD5", "MOD3", "MOD2", "MOD4"}) {
    ASSERT_EQ(radicle_plugin_add_subrecord(plugin.get(), addon_key, signature), RADICLE_STATUS_OK)
        << signature << radicle_last_error();
  }
  const std::pair<const char*, const char*> addon_strings[] = {
      {"ARMA\\BipedMaleModel", "m.nif"},
      {"ARMA\\BipedFemaleModel", "f.nif"},
      {"ARMA\\FirstPersonMaleModel", "m1.nif"},
      {"ARMA\\FirstPersonFemaleModel", "f1.nif"}};
  for (const auto& [path, text] : addon_strings) {
    ASSERT_EQ(radicle_plugin_set_record_string(plugin.get(), addon_key, path, 0, text), RADICLE_STATUS_OK)
        << path;
  }
  const std::pair<const char*, const char*> addon_scalars[] = {
      {"ARMA\\MalePriority", "5"},      {"ARMA\\FemalePriority", "6"},
      {"ARMA\\WeightSliderMale", "3"},  {"ARMA\\WeightSliderFemale", "2"},
      {"ARMA\\DetectionSoundValue", "3"}, {"ARMA\\WeaponAdjust", "1.25"}};
  for (const auto& [path, text] : addon_scalars) {
    ASSERT_EQ(radicle_plugin_set_record_scalar_at(plugin.get(), addon_key, path, 0, text),
              RADICLE_STATUS_OK)
        << path;
  }

  auto out = write_out(plugin.get());
  {
    const auto env = open_env(out);
    const auto* record = by_editor_id(env.get(), "TestArmor", "ARMO");
    ASSERT_NE(record, nullptr);
    radicle_record_info info{};
    ASSERT_EQ(radicle_record_get_info(env.get(), record, &info), RADICLE_STATUS_OK);
    EXPECT_STREQ(info.editor_id, "TestArmor");
    EXPECT_EQ(info.name_kind, RADICLE_NAME_ABSENT);
    EXPECT_EQ(scalar_of(env.get(), record, "ARMO\\MaleWorldModel"), "male.nif");
    EXPECT_EQ(scalar_of(env.get(), record, "ARMO\\MaleIcon"), "icon.dds");
    EXPECT_EQ(scalar_of(env.get(), record, "ARMO\\Value"), "12");
    EXPECT_EQ(scalar_of(env.get(), record, "ARMO\\Weight"), "3.5");
    EXPECT_EQ(scalar_of(env.get(), record, "ARMO\\Health"), "20");
    char sentinel = 'x';
    char* absent = &sentinel;
    EXPECT_EQ(radicle_record_get_scalar(env.get(), record, "ARMO\\FemaleWorldModel", 0, &absent),
              RADICLE_STATUS_NOT_FOUND);
    EXPECT_EQ(absent, nullptr);

    const auto* addon_record = by_editor_id(env.get(), "TestAddon", "ARMA");
    ASSERT_NE(addon_record, nullptr);
    for (const auto& [path, text] : addon_strings) {
      EXPECT_EQ(scalar_of(env.get(), addon_record, path), text) << path;
    }
    for (const auto& [path, text] : addon_scalars) {
      EXPECT_EQ(scalar_of(env.get(), addon_record, path), text) << path;
    }
  }

  ASSERT_EQ(radicle_plugin_set_record_string(plugin.get(), armor_key, "ARMO\\MaleWorldModel", 0, "male2.nif"),
            RADICLE_STATUS_OK);
  ASSERT_EQ(radicle_plugin_set_record_string(plugin.get(), addon_key, "ARMA\\BipedFemaleModel", 0, "f2.nif"),
            RADICLE_STATUS_OK);
  out = write_out(plugin.get());
  {
    const auto env = open_env(out);
    const auto* record = by_editor_id(env.get(), "TestArmor", "ARMO");
    ASSERT_NE(record, nullptr);
    EXPECT_EQ(scalar_of(env.get(), record, "ARMO\\MaleWorldModel"), "male2.nif");
    EXPECT_EQ(scalar_of(env.get(), record, "ARMO\\MaleIcon"), "icon.dds");
    const auto* addon_record = by_editor_id(env.get(), "TestAddon", "ARMA");
    ASSERT_NE(addon_record, nullptr);
    EXPECT_EQ(scalar_of(env.get(), addon_record, "ARMA\\BipedMaleModel"), "m.nif");
    EXPECT_EQ(scalar_of(env.get(), addon_record, "ARMA\\BipedFemaleModel"), "f2.nif");
    EXPECT_EQ(scalar_of(env.get(), addon_record, "ARMA\\FirstPersonFemaleModel"), "f1.nif");
  }

  EXPECT_EQ(radicle_plugin_set_record_string(plugin.get(), key_of(0x999), "ARMO\\MaleWorldModel", 0, "a.nif"),
            RADICLE_STATUS_NOT_FOUND);
  EXPECT_EQ(radicle_plugin_create_record(plugin.get(), "ARMO", "\xE4\xB8\xAD", &armor_key),
            RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_plugin_create_record(nullptr, "ARMO", "TestArmor", &armor_key),
            RADICLE_STATUS_INVALID_ARGUMENT);
}

// Detects: a localized string in a localized plugin that reads as its 4-byte string id (or garbage text) instead of INVALID_ARGUMENT, or an absent occurrence of it that reports INVALID_ARGUMENT instead of NOT_FOUND, so a UI cannot tell a string-table reference from a missing field.
TEST_F(FormsModulesCApi, RecordGetScalarRefusesALocalizedStringButMapsItsAbsentOccurrence) {
  const auto path = root_ / "Localized.esp";
  const auto bytes = localized_fixture();
  {
    std::ofstream stream(path, std::ios::binary);
    stream.write(reinterpret_cast<const char*>(bytes.data()),
                 static_cast<std::streamsize>(bytes.size()));
  }
  const auto env = open_env(path);
  const radicle_record* record = nullptr;
  ASSERT_EQ(radicle_env_winner(env.get(), radicle_form_key{"Localized.esp", 0x800}, &record),
            RADICLE_STATUS_OK);
  ASSERT_NE(record, nullptr);
  char sentinel = 'x';
  char* text = &sentinel;
  EXPECT_EQ(radicle_record_get_scalar(env.get(), record, "GMST\\StringValue", 0, &text),
            RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(text, nullptr);
  text = &sentinel;
  EXPECT_EQ(radicle_record_get_scalar(env.get(), record, "GMST\\StringValue", 1, &text),
            RADICLE_STATUS_NOT_FOUND);
  EXPECT_EQ(text, nullptr);
}

// Detects: an unknown path or a missing occurrence that reads as 0 or as an empty string, or leaves a stale pointer in the caller's output, so a UI cannot tell absent from zero; both must be NOT_FOUND with the output nulled and a reason.
TEST_F(FormsModulesCApi, RecordGetScalarMapsAbsentFieldsAndNullArguments) {
  const auto env = open_env(input_);
  const auto* potion = winner(env.get(), 0x810);
  ASSERT_NE(potion, nullptr);
  char sentinel = 'x';
  char* text = &sentinel;
  EXPECT_EQ(radicle_record_get_scalar(env.get(), potion, "ALCH\\NoSuchField", 0, &text),
            RADICLE_STATUS_NOT_FOUND);
  EXPECT_EQ(text, nullptr);
  EXPECT_NE(std::string(radicle_last_error()).find("unknown scalar field path"), std::string::npos)
      << radicle_last_error();
  text = &sentinel;
  EXPECT_EQ(radicle_record_get_scalar(env.get(), potion, "MESG\\Flags", 0, &text),
            RADICLE_STATUS_NOT_FOUND);
  EXPECT_EQ(text, nullptr);
  text = &sentinel;
  EXPECT_EQ(radicle_record_get_scalar(env.get(), potion, "ALCH\\Value", 1, &text),
            RADICLE_STATUS_NOT_FOUND);
  EXPECT_EQ(text, nullptr);

  text = &sentinel;
  EXPECT_EQ(radicle_record_get_scalar(nullptr, potion, "ALCH\\Value", 0, &text),
            RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(text, nullptr);
  EXPECT_EQ(radicle_record_get_scalar(env.get(), nullptr, "ALCH\\Value", 0, &text),
            RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_record_get_scalar(env.get(), potion, nullptr, 0, &text),
            RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_record_get_scalar(env.get(), potion, "ALCH\\Value", 0, nullptr),
            RADICLE_STATUS_INVALID_ARGUMENT);
  radicle_record_scalar_free(nullptr);
}

// Constraint: a bulk read must return the winning field, must keep a row whose occurrence is absent, and must reject an unknown path.
TEST_F(FormsModulesCApi, FieldReadAndWinnerBulkReturnTheWinningScalar) {
  const auto env = open_env(input_);
  radicle_winner_field* rows = nullptr;
  size_t count = 0;
  ASSERT_EQ(radicle_env_read_winners(env.get(), nullptr, "ALCH\\Value", 0, &rows, &count), RADICLE_STATUS_OK);
  ASSERT_EQ(count, 1u);
  EXPECT_STREQ(rows[0].key.plugin, "Forms.esp");
  EXPECT_EQ(rows[0].key.local_id, 0x810u);
  ASSERT_NE(rows[0].text, nullptr);
  EXPECT_EQ(std::string(rows[0].text), "25");
  radicle_env_read_winners_free(rows, count);

  rows = reinterpret_cast<radicle_winner_field*>(1);
  count = 1;
  EXPECT_EQ(radicle_env_read_winners(env.get(), nullptr, "ALCH\\NoSuch", 0, &rows, &count),
            RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(rows, nullptr);
  EXPECT_EQ(count, 0u);

  ASSERT_EQ(radicle_env_read_winners(env.get(), nullptr, "ALCH\\Keywords", 0, &rows, &count), RADICLE_STATUS_OK)
      << radicle_last_error();
  ASSERT_EQ(count, 1u);
  EXPECT_EQ(rows[0].key.local_id, 0x810u);
  EXPECT_EQ(rows[0].text, nullptr);
  radicle_env_read_winners_free(rows, count);
}

TEST_F(FormsModulesCApi, WinnerFieldsBulkReadProjectsSeveralPaths) {
  const auto env = open_env(input_);
  const char* paths[] = {"ALCH\\Value", "ALCH\\Weight"};
  radicle_winner_fields_row* rows = nullptr;
  size_t count = 0;
  ASSERT_EQ(radicle_env_read_fields(env.get(), nullptr, "ALCH", paths, 2, 0, &rows, &count), RADICLE_STATUS_OK);
  ASSERT_EQ(count, 1u);
  ASSERT_NE(rows[0].texts, nullptr);
  ASSERT_NE(rows[0].texts[0], nullptr);
  ASSERT_NE(rows[0].texts[1], nullptr);
  EXPECT_EQ(std::string(rows[0].texts[0]), "25");
  EXPECT_EQ(std::string(rows[0].texts[1]), "2.5");
  radicle_env_read_fields_free(rows, count, 2);
  EXPECT_EQ(radicle_env_read_fields(env.get(), nullptr, "ALCH", paths, 2, 0, nullptr, &count),
            RADICLE_STATUS_INVALID_ARGUMENT);
}

// Constraint: a localized string must resolve through its own environment's table and must stay invalid without that table.
TEST_F(FormsModulesCApi, FieldReadResolvesALocalizedStringFromItsTable) {
  const auto path = root_ / "Localized.esp";
  const auto bytes = localized_fixture();
  {
    std::ofstream stream(path, std::ios::binary);
    stream.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
  }
  fo4::localization::LocalizedStringTable table{fo4::localization::StringTableKind::strings, {}};
  fo4::localization::insert_string(table, 0x1234, "Label");
  const auto strings_dir = root_ / "strings";
  std::filesystem::create_directories(strings_dir);
  fo4::localization::write_string_table_file(table, strings_dir / "Localized_en.STRINGS");

  const auto env = open_env(path);
  const radicle_record* record = nullptr;
  ASSERT_EQ(radicle_env_winner(env.get(), radicle_form_key{"Localized.esp", 0x800}, &record), RADICLE_STATUS_OK);
  char* text = reinterpret_cast<char*>(1);
  EXPECT_EQ(radicle_record_get_field(env.get(), record, "GMST\\StringValue", 0, &text),
            RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(text, nullptr);

  radicle_strings* strings = nullptr;
  ASSERT_EQ(radicle_strings_load_with_assets(env.get(), strings_dir.string().c_str(), "en", nullptr, &strings), RADICLE_STATUS_OK)
      << radicle_last_error();
  ASSERT_EQ(radicle_record_get_text(env.get(), strings, record, "GMST\\StringValue", 0, &text), RADICLE_STATUS_OK)
      << radicle_last_error();
  EXPECT_EQ(std::string(text), "Label");
  radicle_record_scalar_free(text);
  const auto other = open_env(input_);
  text = reinterpret_cast<char*>(1);
  EXPECT_EQ(radicle_record_get_text(other.get(), strings, record, "GMST\\StringValue", 0, &text),
            RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(text, nullptr);
  EXPECT_NE(std::string(radicle_last_error()).find("different environment"), std::string::npos);
  text = reinterpret_cast<char*>(1);
  EXPECT_EQ(radicle_record_get_scalar(env.get(), record, "GMST\\StringValue", 0, &text),
            RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(text, nullptr);
  radicle_strings_destroy(strings);
}

// Constraint: detects a localized write that does not reach the saved string table, so the reloaded plugin reads the old text or a missing string; also the no-tables and bad-argument refusals.
TEST_F(FormsModulesCApi, LocalizedStringWriteRoundTripsThroughSavedTables) {
  const auto path = root_ / "Localized.esp";
  {
    const auto bytes = localized_fixture();
    std::ofstream stream(path, std::ios::binary);
    stream.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
  }
  fo4::localization::LocalizedStringTable table{fo4::localization::StringTableKind::strings, {}};
  fo4::localization::insert_string(table, 0x1234, "Label");
  const auto strings_dir = root_ / "strings";
  std::filesystem::create_directories(strings_dir);
  fo4::localization::write_string_table_file(table, strings_dir / "LOCALIZED_en.strings");
  const auto strings_text = strings_dir.string();
  const radicle_form_key key{"Localized.esp", 0x800};
  const auto path_text = path.string();

  radicle_plugin* raw = nullptr;
  ASSERT_EQ(radicle_plugin_read_file(path_text.c_str(), &raw), RADICLE_STATUS_OK);
  const PluginPtr plugin(raw, radicle_plugin_destroy);
  EXPECT_EQ(radicle_plugin_set_record_field(plugin.get(), key, "GMST\\StringValue", 0, "x"),
            RADICLE_STATUS_INVALID_ARGUMENT);
  const auto out_dir = root_ / "out_strings";
  const auto out_text = out_dir.string();
  EXPECT_EQ(radicle_plugin_strings_save(plugin.get(), nullptr, out_text.c_str(), "en"), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_plugin_strings_load(plugin.get(), strings_text.c_str(), "../en"), RADICLE_STATUS_INVALID_ARGUMENT);
  ASSERT_EQ(radicle_plugin_strings_load(plugin.get(), strings_text.c_str(), "en"), RADICLE_STATUS_OK)
      << radicle_last_error();

  ASSERT_EQ(radicle_plugin_set_record_field(plugin.get(), key, "GMST\\StringValue", 0, "\xE4\xB8\xAD"),
            RADICLE_STATUS_INVALID_ARGUMENT);
  ASSERT_EQ(radicle_plugin_set_record_field(plugin.get(), key, "GMST\\StringValue", 0, "Caf\xC3\xA9"), RADICLE_STATUS_OK)
      << radicle_last_error();

  const auto out_plugin = root_ / "out" / "Localized.esp";
  const auto out_plugin_text = out_plugin.string();
  ASSERT_EQ(radicle_plugin_write_file(plugin.get(), out_plugin_text.c_str()), RADICLE_STATUS_OK);
  ASSERT_EQ(radicle_plugin_strings_save(plugin.get(), nullptr, (root_ / "out" / "strings").string().c_str(), "en"),
            RADICLE_STATUS_OK)
      << radicle_last_error();
  std::vector<std::string> saved;
  for (const auto& entry : std::filesystem::directory_iterator(root_ / "out" / "strings")) {
    saved.push_back(entry.path().filename().string());
  }
  std::sort(saved.begin(), saved.end());
  EXPECT_EQ(saved, (std::vector<std::string>{"LOCALIZED_en.strings", "Localized_en.DLSTRINGS", "Localized_en.ILSTRINGS"}));

  const auto env = open_env(out_plugin);
  const radicle_record* record = nullptr;
  ASSERT_EQ(radicle_env_winner(env.get(), key, &record), RADICLE_STATUS_OK);
  radicle_strings* strings = nullptr;
  ASSERT_EQ(radicle_strings_load_with_assets(env.get(), (root_ / "out" / "strings").string().c_str(), "en", nullptr, &strings),
            RADICLE_STATUS_OK)
      << radicle_last_error();
  char* text = nullptr;
  ASSERT_EQ(radicle_record_get_text(env.get(), strings, record, "GMST\\StringValue", 0, &text), RADICLE_STATUS_OK)
      << radicle_last_error();
  EXPECT_EQ(std::string(text), "Caf\xC3\xA9");
  radicle_record_scalar_free(text);
  radicle_strings_destroy(strings);

  const auto inline_plugin = read_plugin();
  EXPECT_EQ(radicle_plugin_strings_load(inline_plugin.get(), strings_text.c_str(), "en"), RADICLE_STATUS_INVALID_ARGUMENT);
}

// Constraint: detects a string id that two string settings share being rewritten in place, which changes the text of the setting that was not written.
TEST_F(FormsModulesCApi, LocalizedStringWriteKeepsAnIdAnotherSettingStillUses) {
  fo4::test::ByteWriter out;
  const auto tes4 = fo4::test::begin_record(out, "TES4", 0x80, 0);
  fo4::test::ByteWriter hedr;
  hedr.f32(1.0f);
  hedr.u32(2);
  hedr.u32(0x900);
  fo4::test::write_subrecord(out, "HEDR", hedr.bytes);
  fo4::test::end_record(out, tes4);
  const auto settings = fo4::test::begin_group(out, "GMST", 0);
  for (const std::uint32_t form_id : {0x800u, 0x801u}) {
    const auto setting = fo4::test::begin_record(out, "GMST", 0, form_id);
    fo4::test::write_subrecord(out, "EDID", std::string_view(form_id == 0x800u ? "sFirst\0" : "sSecond\0", form_id == 0x800u ? 7 : 8));
    fo4::test::ByteWriter string_id;
    string_id.u32(0x1234);
    fo4::test::write_subrecord(out, "DATA", string_id.bytes);
    fo4::test::end_record(out, setting);
  }
  fo4::test::end_group(out, settings);
  const auto path = root_ / "Shared.esp";
  {
    std::ofstream stream(path, std::ios::binary);
    stream.write(reinterpret_cast<const char*>(out.bytes.data()), static_cast<std::streamsize>(out.bytes.size()));
  }
  fo4::localization::LocalizedStringTable table{fo4::localization::StringTableKind::strings, {}};
  fo4::localization::insert_string(table, 0x1234, "Label");
  const auto strings_dir = root_ / "strings";
  std::filesystem::create_directories(strings_dir);
  fo4::localization::write_string_table_file(table, strings_dir / "SHARED_en.strings");

  radicle_plugin* raw = nullptr;
  const auto path_text = path.string();
  ASSERT_EQ(radicle_plugin_read_file(path_text.c_str(), &raw), RADICLE_STATUS_OK);
  const PluginPtr plugin(raw, radicle_plugin_destroy);
  ASSERT_EQ(radicle_plugin_strings_load(plugin.get(), strings_dir.string().c_str(), "en"), RADICLE_STATUS_OK)
      << radicle_last_error();
  ASSERT_EQ(radicle_plugin_set_record_field(plugin.get(), radicle_form_key{"Shared.esp", 0x800}, "GMST\\StringValue", 0, "Changed"),
            RADICLE_STATUS_OK)
      << radicle_last_error();
  const auto out_plugin = root_ / "out" / "Shared.esp";
  ASSERT_EQ(radicle_plugin_write_file(plugin.get(), out_plugin.string().c_str()), RADICLE_STATUS_OK);
  ASSERT_EQ(radicle_plugin_strings_save(plugin.get(), nullptr, (root_ / "out" / "strings").string().c_str(), "en"),
            RADICLE_STATUS_OK)
      << radicle_last_error();

  const auto env = open_env(out_plugin);
  radicle_strings* strings = nullptr;
  ASSERT_EQ(radicle_strings_load_with_assets(env.get(), (root_ / "out" / "strings").string().c_str(), "en", nullptr, &strings),
            RADICLE_STATUS_OK)
      << radicle_last_error();
  const auto read_text = [&](std::uint32_t local) {
    const radicle_record* record = nullptr;
    EXPECT_EQ(radicle_env_winner(env.get(), radicle_form_key{"Shared.esp", local}, &record), RADICLE_STATUS_OK);
    char* text = nullptr;
    EXPECT_EQ(radicle_record_get_text(env.get(), strings, record, "GMST\\StringValue", 0, &text), RADICLE_STATUS_OK)
        << radicle_last_error();
    const std::string value = text == nullptr ? std::string{} : std::string(text);
    radicle_record_scalar_free(text);
    return value;
  };
  EXPECT_EQ(read_text(0x800), "Changed");
  EXPECT_EQ(read_text(0x801), "Label");
  radicle_strings_destroy(strings);
}

// Constraint: detects a save in another language reusing the loaded file names, which writes the tables under the language they were loaded from.
TEST_F(FormsModulesCApi, LocalizedStringsSaveInAnotherLanguageDerivesItsFileNames) {
  const auto path = root_ / "Localized.esp";
  {
    const auto bytes = localized_fixture();
    std::ofstream stream(path, std::ios::binary);
    stream.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
  }
  fo4::localization::LocalizedStringTable table{fo4::localization::StringTableKind::strings, {}};
  fo4::localization::insert_string(table, 0x1234, "Label");
  const auto strings_dir = root_ / "strings";
  std::filesystem::create_directories(strings_dir);
  fo4::localization::write_string_table_file(table, strings_dir / "LOCALIZED_en.strings");
  const auto strings_text = strings_dir.string();
  const auto path_text = path.string();

  radicle_plugin* raw = nullptr;
  ASSERT_EQ(radicle_plugin_read_file(path_text.c_str(), &raw), RADICLE_STATUS_OK);
  const PluginPtr plugin(raw, radicle_plugin_destroy);
  ASSERT_EQ(radicle_plugin_strings_load(plugin.get(), strings_text.c_str(), "en"), RADICLE_STATUS_OK)
      << radicle_last_error();
  const auto list_saved = [&](const char* directory) {
    std::vector<std::string> saved;
    for (const auto& entry : std::filesystem::directory_iterator(root_ / directory)) {
      auto name = entry.path().filename().string();
      std::transform(name.begin(), name.end(), name.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
      saved.push_back(std::move(name));
    }
    std::sort(saved.begin(), saved.end());
    return saved;
  };

  ASSERT_EQ(radicle_plugin_strings_save(plugin.get(), nullptr, (root_ / "fr").string().c_str(), "fr"), RADICLE_STATUS_OK)
      << radicle_last_error();
  EXPECT_EQ(list_saved("fr"), (std::vector<std::string>{"localized_fr.dlstrings", "localized_fr.ilstrings", "localized_fr.strings"}));

  ASSERT_EQ(radicle_plugin_strings_save(plugin.get(), "Other.esp", (root_ / "other").string().c_str(), "en"),
            RADICLE_STATUS_OK)
      << radicle_last_error();
  EXPECT_EQ(list_saved("other"), (std::vector<std::string>{"other_en.dlstrings", "other_en.ilstrings", "other_en.strings"}));
}

// A relocation must survive a write and reload, or the game reads a cell that is still filed under its old grid.
TEST_F(FormsModulesCApi, RelocateExteriorCellWritesAReadableHierarchy) {
  auto plugin = read_plugin();
  radicle_form_key world{};
  ASSERT_EQ(radicle_plugin_create_record(plugin.get(), "WRLD", "RelocateWorld", &world),
            RADICLE_STATUS_OK)
      << radicle_last_error();

  radicle_exterior_cell_creation creation{};
  creation.worldspace = world;
  creation.x = 1;
  creation.y = 2;
  creation.editor_id = "RelocateCell";
  creation.flags = 0x0002;
  radicle_form_key cell{};
  ASSERT_EQ(radicle_plugin_create_exterior_cell(plugin.get(), &creation, &cell), RADICLE_STATUS_OK)
      << radicle_last_error();

  radicle_cell_relocate_status status = RADICLE_CELL_RELOCATE_CELL_MISSING;
  ASSERT_EQ(radicle_plugin_relocate_exterior_cell(plugin.get(), cell, -33, 40, &status),
            RADICLE_STATUS_OK)
      << radicle_last_error();
  EXPECT_EQ(status, RADICLE_CELL_RELOCATE_RELOCATED);

  const auto out = write_out(plugin.get());
  const auto env = open_env(out);
  const radicle_record* record = nullptr;
  ASSERT_EQ(radicle_env_find_exterior_cell(env.get(), world, -33, 40, &record), RADICLE_STATUS_OK)
      << radicle_last_error();
  ASSERT_NE(record, nullptr);
  radicle_record_info info{};
  ASSERT_EQ(radicle_record_get_info(env.get(), record, &info), RADICLE_STATUS_OK);
  EXPECT_EQ(info.local_id, cell.local_id);
  EXPECT_EQ(radicle_env_find_exterior_cell(env.get(), world, 1, 2, &record), RADICLE_STATUS_NOT_FOUND);

  ASSERT_EQ(radicle_plugin_relocate_exterior_cell(plugin.get(), cell, -33, 40, &status),
            RADICLE_STATUS_OK)
      << radicle_last_error();
  EXPECT_EQ(status, RADICLE_CELL_RELOCATE_UNCHANGED);
  EXPECT_EQ(radicle_plugin_relocate_exterior_cell(plugin.get(), cell, 111, 0, &status),
            RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(status, RADICLE_CELL_RELOCATE_INVALID_COORDINATE);
  EXPECT_EQ(radicle_plugin_relocate_exterior_cell(plugin.get(), world, 5, 5, &status),
            RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(status, RADICLE_CELL_RELOCATE_NOT_A_CELL);
  EXPECT_EQ(radicle_plugin_relocate_exterior_cell(plugin.get(), cell, 5, 5, nullptr),
            RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_plugin_relocate_exterior_cell(nullptr, cell, 5, 5, &status),
            RADICLE_STATUS_INVALID_ARGUMENT);
}

TEST_F(FormsModulesCApi, RecordListSizeThroughTheEnvironmentCountsRows) {
  const auto env = open_env(input_);
  const auto* misc = winner(env.get(), 0x800);
  ASSERT_NE(misc, nullptr);
  size_t size = 0;
  ASSERT_EQ(radicle_record_list_size(env.get(), misc, "MISC\\DestructionResistances", &size), RADICLE_STATUS_OK)
      << radicle_last_error();
  EXPECT_EQ(size, 2u);
}

TEST_F(FormsModulesCApi, BulkWinnerReadKeepsLocalizedRowsNullWithoutFailingScalars) {
  const auto localized = root_ / "Localized.esp";
  {
    const auto bytes = localized_fixture();
    std::ofstream stream(localized, std::ios::binary);
    stream.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
  }
  const auto env = open_env_paths({input_, localized});
  radicle_winner_field* rows = nullptr;
  size_t count = 0;
  ASSERT_EQ(radicle_env_read_winners(env.get(), nullptr, "GMST\\StringValue", 0, &rows, &count), RADICLE_STATUS_OK)
      << radicle_last_error();
  ASSERT_EQ(count, 1u);
  EXPECT_EQ(rows[0].text, nullptr);
  radicle_env_read_winners_free(rows, count);

  ASSERT_EQ(radicle_env_read_winners(env.get(), nullptr, "ALCH\\Value", 0, &rows, &count), RADICLE_STATUS_OK);
  ASSERT_EQ(count, 1u);
  ASSERT_NE(rows[0].text, nullptr);
  EXPECT_EQ(std::string(rows[0].text), "25");
  radicle_env_read_winners_free(rows, count);
}

TEST_F(FormsModulesCApi, BulkWinnerReadResolvesLocalizedRowsWhenAStringTableIsLoaded) {
  const auto localized = root_ / "Localized.esp";
  {
    const auto bytes = localized_fixture();
    std::ofstream stream(localized, std::ios::binary);
    stream.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
  }
  fo4::localization::LocalizedStringTable table{fo4::localization::StringTableKind::strings, {}};
  fo4::localization::insert_string(table, 0x1234, "Label");
  const auto strings_dir = root_ / "strings";
  std::filesystem::create_directories(strings_dir);
  fo4::localization::write_string_table_file(table, strings_dir / "Localized_en.STRINGS");

  const auto env = open_env_paths({input_, localized});
  radicle_strings* strings = nullptr;
  ASSERT_EQ(radicle_strings_load_with_assets(env.get(), strings_dir.string().c_str(), "en", nullptr, &strings), RADICLE_STATUS_OK)
      << radicle_last_error();
  radicle_winner_field* rows = nullptr;
  size_t count = 0;
  ASSERT_EQ(radicle_env_read_winners(env.get(), strings, "GMST\\StringValue", 0, &rows, &count), RADICLE_STATUS_OK)
      << radicle_last_error();
  ASSERT_EQ(count, 1u);
  ASSERT_NE(rows[0].text, nullptr);
  EXPECT_EQ(std::string(rows[0].text), "Label");
  radicle_env_read_winners_free(rows, count);
  radicle_strings_destroy(strings);
}

TEST_F(FormsModulesCApi, BulkWinnerReadUsesTheWinningPluginInTheLoadOrder) {
  const auto source = read_plugin();
  radicle_plugin* patch = nullptr;
  ASSERT_EQ(radicle_plugin_create("Patch.esp", &patch), RADICLE_STATUS_OK) << radicle_last_error();
  const radicle_form_key potion = key_of(0x810);
  radicle_record_copy_status copied = RADICLE_RECORD_COPY_ALREADY_EXISTS;
  ASSERT_EQ(radicle_plugin_copy_record_override(patch, source.get(), potion, 0, &copied), RADICLE_STATUS_OK)
      << radicle_last_error();
  EXPECT_EQ(copied, RADICLE_RECORD_COPY_INSERTED);
  ASSERT_EQ(radicle_plugin_set_record_scalar_at(patch, potion, "ALCH\\Value", 0, "99"), RADICLE_STATUS_OK)
      << radicle_last_error();
  const auto patch_path = root_ / "Patch.esp";
  ASSERT_EQ(radicle_plugin_write_file(patch, patch_path.string().c_str()), RADICLE_STATUS_OK) << radicle_last_error();
  radicle_plugin_destroy(patch);

  const auto env = open_env_paths({input_, patch_path});
  radicle_winner_field* rows = nullptr;
  size_t count = 0;
  ASSERT_EQ(radicle_env_read_winners(env.get(), nullptr, "ALCH\\Value", 0, &rows, &count), RADICLE_STATUS_OK)
      << radicle_last_error();
  ASSERT_EQ(count, 1u);
  EXPECT_STREQ(rows[0].key.plugin, kPlugin);
  ASSERT_NE(rows[0].text, nullptr);
  EXPECT_EQ(std::string(rows[0].text), "99");
  radicle_env_read_winners_free(rows, count);
}

}  // namespace
