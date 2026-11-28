#include "fo4/c_api.h"
#include "fo4/c_api_conditions.h"
#include "fo4/c_api_env.h"
#include "fo4/c_api_records.h"
#include "fo4/schema/condition_functions.hpp"
#include "fo4/schema/ctda.hpp"
#include "plugin_bytes.hpp"

#include <gtest/gtest.h>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace {

constexpr const char* kPlugin = "Conditions.esp";

using EnvPtr = std::unique_ptr<radicle_env, decltype(&radicle_env_destroy)>;
using PluginPtr = std::unique_ptr<radicle_plugin, decltype(&radicle_plugin_destroy)>;
using ListPtr = std::unique_ptr<const radicle_condition_list, decltype(&radicle_condition_list_free)>;

fo4::CtdaCondition first_condition() {
  fo4::CtdaCondition c;
  c.flags_byte = 0x20;
  c.comparison_raw = 0x3F800000u;
  c.function = 1;
  c.param1 = 0x800;
  c.parameter1_string = "Hello";
  return c;
}

fo4::CtdaCondition second_condition() {
  fo4::CtdaCondition c;
  c.flags_byte = 0x01;
  c.function = 0x10;
  c.param1 = -1;
  c.param2 = 3;
  c.reference = 0x800;
  return c;
}

fo4::CtdaCondition third_condition() {
  fo4::CtdaCondition c;
  c.comparison_raw = 2;
  c.function = 0x20;
  c.param1 = 7;
  c.reference = 0x804;
  return c;
}

void write_run(fo4::test::ByteWriter& out, const std::vector<fo4::CtdaCondition>& conditions) {
  for (const auto& sub : fo4::serialize_ctda_run(conditions)) {
    fo4::test::write_subrecord(out, sub.signature.to_string(), sub.data);
  }
}

std::vector<std::byte> fixture() {
  fo4::test::ByteWriter out;
  fo4::test::ByteWriter hedr;
  hedr.f32(1.0f);
  hedr.u32(0);
  hedr.u32(0x900);
  const auto tes4 = fo4::test::begin_record(out, "TES4", 0, 0);
  fo4::test::write_subrecord(out, "HEDR", hedr.bytes);
  fo4::test::end_record(out, tes4);

  const auto perks = fo4::test::begin_group(out, "PERK", 0);
  const auto perk = fo4::test::begin_record(out, "PERK", 0, 0x800);
  fo4::test::write_subrecord(out, "EDID", std::string_view("Perk\0", 5));
  write_run(out, {first_condition(), second_condition()});
  fo4::test::end_record(out, perk);

  const auto tabbed = fo4::test::begin_record(out, "PERK", 0, 0x804);
  fo4::test::write_subrecord(out, "EDID", std::string_view("Tabbed\0", 7));
  write_run(out, {first_condition()});
  fo4::test::write_subrecord(out, "PRKC", std::string_view("\0", 1));
  write_run(out, {second_condition()});
  fo4::test::write_subrecord(out, "DATA", std::string_view("\0\0\0\0", 4));
  write_run(out, {third_condition()});
  fo4::test::end_record(out, tabbed);

  const auto broken = fo4::test::begin_record(out, "PERK", 0x00040000, 0x805);
  out.u32(16);
  for (int i = 0; i < 4; ++i) out.u8(0xFF);
  fo4::test::end_record(out, broken);
  fo4::test::end_group(out, perks);

  const std::string effect_data(12, '\0');
  const auto spells = fo4::test::begin_group(out, "SPEL", 0);
  const auto with_list = fo4::test::begin_record(out, "SPEL", 0, 0x801);
  fo4::test::write_subrecord(out, "EDID", std::string_view("Spel\0", 5));
  fo4::test::write_subrecord(out, "EFID", std::string_view("\0\0\0\0", 4));
  fo4::test::write_subrecord(out, "EFIT", std::string_view(effect_data));
  write_run(out, {third_condition()});
  fo4::test::end_record(out, with_list);
  const auto empty_effect = fo4::test::begin_record(out, "SPEL", 0, 0x802);
  fo4::test::write_subrecord(out, "EDID", std::string_view("Empty\0", 6));
  fo4::test::write_subrecord(out, "EFID", std::string_view("\0\0\0\0", 4));
  fo4::test::write_subrecord(out, "EFIT", std::string_view(effect_data));
  fo4::test::end_record(out, empty_effect);
  const auto plain = fo4::test::begin_record(out, "SPEL", 0, 0x803);
  fo4::test::write_subrecord(out, "EDID", std::string_view("Plain\0", 6));
  fo4::test::end_record(out, plain);
  fo4::test::end_group(out, spells);

  const auto messages = fo4::test::begin_group(out, "MESG", 0);
  const auto message = fo4::test::begin_record(out, "MESG", 0, 0x810);
  fo4::test::write_subrecord(out, "DESC", std::string_view("Text\0", 5));
  fo4::test::write_subrecord(out, "ITXT", std::string_view("Yes\0", 4));
  fo4::test::write_subrecord(out, "ITXT", std::string_view("No\0", 3));
  fo4::test::end_record(out, message);
  fo4::test::end_group(out, messages);
  return out.bytes;
}

EnvPtr open_env(const std::filesystem::path& path) {
  const auto text = path.string();
  const char* paths[] = {text.c_str()};
  radicle_env* env = nullptr;
  EXPECT_EQ(radicle_env_load(paths, 1, &env), RADICLE_STATUS_OK);
  return EnvPtr(env, radicle_env_destroy);
}

PluginPtr read_plugin(const std::filesystem::path& path) {
  const auto text = path.string();
  radicle_plugin* plugin = nullptr;
  EXPECT_EQ(radicle_plugin_read_file(text.c_str(), &plugin), RADICLE_STATUS_OK);
  return PluginPtr(plugin, radicle_plugin_destroy);
}

const radicle_record* winner(const radicle_env* env, std::uint32_t local) {
  const radicle_record* record = nullptr;
  EXPECT_EQ(radicle_env_winner(env, radicle_form_key{kPlugin, local}, &record), RADICLE_STATUS_OK);
  return record;
}

ListPtr conditions_of(const radicle_env* env, const radicle_record* record, const char* scope_path) {
  const radicle_condition_list* list = nullptr;
  EXPECT_EQ(radicle_record_conditions(env, record, scope_path, &list), RADICLE_STATUS_OK);
  return ListPtr(list, radicle_condition_list_free);
}

class ConditionsCApi : public ::testing::Test {
protected:
  void SetUp() override {
    root_ = std::filesystem::temp_directory_path() /
            ("radicle_c_conditions_" +
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

  std::filesystem::path save(radicle_plugin* plugin) {
    const auto output = root_ / "out" / kPlugin;
    const auto text = output.string();
    EXPECT_EQ(radicle_plugin_write_file(plugin, text.c_str()), RADICLE_STATUS_OK);
    return output;
  }

  std::filesystem::path root_;
  std::filesystem::path input_;
};

TEST_F(ConditionsCApi, RootReadRoundTripsEveryField) {
  const auto env = open_env(input_);
  const auto list = conditions_of(env.get(), winner(env.get(), 0x800), nullptr);
  ASSERT_EQ(list->count, 2u);
  const auto* rows = list->rows;
  EXPECT_EQ(rows[0].flags_byte, 0x20);
  EXPECT_EQ(rows[0].comparison_raw, 0x3F800000u);
  EXPECT_EQ(rows[0].function, 1);
  EXPECT_EQ(rows[0].unknown2, 0);
  EXPECT_EQ(rows[0].param1, 0x800);
  EXPECT_EQ(rows[0].param2, 0);
  EXPECT_EQ(rows[0].run_on, 0u);
  EXPECT_EQ(rows[0].reference, 0u);
  EXPECT_EQ(rows[0].unknown3, -1);
  ASSERT_NE(rows[0].parameter1_string, nullptr);
  EXPECT_STREQ(rows[0].parameter1_string, "Hello");
  EXPECT_EQ(rows[0].parameter2_string, nullptr);
  EXPECT_EQ(rows[1].flags_byte, 0x01);
  EXPECT_EQ(rows[1].comparison_raw, 0u);
  EXPECT_EQ(rows[1].function, 0x10);
  EXPECT_EQ(rows[1].unknown2, 0);
  EXPECT_EQ(rows[1].param1, -1);
  EXPECT_EQ(rows[1].param2, 3);
  EXPECT_EQ(rows[1].run_on, 0u);
  EXPECT_EQ(rows[1].reference, 0x800u);
  EXPECT_EQ(rows[1].unknown3, -1);
  EXPECT_EQ(rows[1].parameter1_string, nullptr);
  EXPECT_EQ(rows[1].parameter2_string, nullptr);
}

TEST_F(ConditionsCApi, SetReplacesTheRootListAndRereadsIt) {
  auto plugin = read_plugin(input_);
  const auto rows = std::vector<radicle_condition>{
      {0x01, 0u, 0x10, 0, -1, 3, 0, 0x800, -1, nullptr, nullptr},
      {0x20, 0x3F800000u, 1, 0, 0x800, 0, 0, 0, -1, "Hello", nullptr}};
  radicle_condition_write_status status{};
  ASSERT_EQ(radicle_plugin_set_conditions(plugin.get(), radicle_form_key{kPlugin, 0x800}, nullptr,
                                          rows.data(), rows.size(), &status),
            RADICLE_STATUS_OK);
  EXPECT_EQ(status, RADICLE_CONDITION_UPDATED);
  ASSERT_EQ(radicle_plugin_set_conditions(plugin.get(), radicle_form_key{kPlugin, 0x800}, nullptr,
                                          rows.data(), rows.size(), &status),
            RADICLE_STATUS_OK);
  EXPECT_EQ(status, RADICLE_CONDITION_UNCHANGED);

  const auto env = open_env(save(plugin.get()));
  const auto list = conditions_of(env.get(), winner(env.get(), 0x800), nullptr);
  ASSERT_EQ(list->count, 2u);
  EXPECT_EQ(list->rows[0].function, 0x10);
  EXPECT_EQ(list->rows[0].param2, 3);
  EXPECT_EQ(list->rows[1].function, 1);
  ASSERT_NE(list->rows[1].parameter1_string, nullptr);
  EXPECT_STREQ(list->rows[1].parameter1_string, "Hello");
}

TEST_F(ConditionsCApi, ScopedReadAndWriteOnSpellEffects) {
  auto plugin = read_plugin(input_);
  const radicle_form_key with_list{kPlugin, 0x801};
  const radicle_form_key empty_effect{kPlugin, 0x802};
  radicle_condition_write_status status{};
  const auto fourth = radicle_condition{0x04, 0u, 0x30, 0, 9, 0, 0, 0, -1, nullptr, nullptr};
  ASSERT_EQ(radicle_plugin_set_conditions(plugin.get(), with_list, "Effects[0].Conditions",
                                          &fourth, 1, &status),
            RADICLE_STATUS_OK);
  EXPECT_EQ(status, RADICLE_CONDITION_UPDATED);
  const auto third = radicle_condition{0x00, 2u, 0x20, 0, 7, 0, 0, 0x804, -1, nullptr, nullptr};
  ASSERT_EQ(radicle_plugin_set_conditions(plugin.get(), empty_effect, "Effects[0].Conditions",
                                          &third, 1, &status),
            RADICLE_STATUS_OK);
  EXPECT_EQ(status, RADICLE_CONDITION_UPDATED);

  const auto env = open_env(save(plugin.get()));
  const auto scoped = conditions_of(env.get(), winner(env.get(), 0x801), "Effects[0].Conditions");
  ASSERT_EQ(scoped->count, 1u);
  EXPECT_EQ(scoped->rows[0].function, 0x30);
  EXPECT_EQ(scoped->rows[0].param1, 9);
  const auto inserted = conditions_of(env.get(), winner(env.get(), 0x802), "Effects[0].Conditions");
  ASSERT_EQ(inserted->count, 1u);
  EXPECT_EQ(inserted->rows[0].function, 0x20);
  EXPECT_EQ(inserted->rows[0].reference, 0x804u);
  const auto root = conditions_of(env.get(), winner(env.get(), 0x801), nullptr);
  EXPECT_EQ(root->count, 0u);
}

TEST_F(ConditionsCApi, IdenticalRowsReportUnchanged) {
  auto plugin = read_plugin(input_);
  const auto rows = std::vector<radicle_condition>{
      {0x20, 0x3F800000u, 1, 0, 0x800, 0, 0, 0, -1, "Hello", nullptr},
      {0x01, 0u, 0x10, 0, -1, 3, 0, 0x800, -1, nullptr, nullptr}};
  radicle_condition_write_status status{};
  ASSERT_EQ(radicle_plugin_set_conditions(plugin.get(), radicle_form_key{kPlugin, 0x800}, nullptr,
                                          rows.data(), rows.size(), &status),
            RADICLE_STATUS_OK);
  EXPECT_EQ(status, RADICLE_CONDITION_UNCHANGED);

  const auto spell = radicle_condition{0x00, 2u, 0x20, 0, 7, 0, 0, 0x804, -1, nullptr, nullptr};
  ASSERT_EQ(radicle_plugin_set_conditions(plugin.get(), radicle_form_key{kPlugin, 0x801},
                                          "Effects[0].Conditions", &spell, 1, &status),
            RADICLE_STATUS_OK);
  EXPECT_EQ(status, RADICLE_CONDITION_UNCHANGED);
}

TEST_F(ConditionsCApi, RecordWithoutAListReportsNoList) {
  auto plugin = read_plugin(input_);
  const auto row = radicle_condition{0x00, 0u, 0x20, 0, 7, 0, 0, 0, -1, nullptr, nullptr};
  radicle_condition_write_status status{};
  ASSERT_EQ(radicle_plugin_set_conditions(plugin.get(), radicle_form_key{kPlugin, 0x803}, nullptr,
                                          &row, 1, &status),
            RADICLE_STATUS_OK);
  EXPECT_EQ(status, RADICLE_CONDITION_NO_LIST);

  const auto env = open_env(input_);
  const auto list = conditions_of(env.get(), winner(env.get(), 0x803), nullptr);
  EXPECT_EQ(list->count, 0u);
}

TEST_F(ConditionsCApi, ScopeIndexOutOfRangeIsAStatusAndAReadError) {
  auto plugin = read_plugin(input_);
  const auto row = radicle_condition{0x00, 0u, 0x20, 0, 7, 0, 0, 0, -1, nullptr, nullptr};
  radicle_condition_write_status status{};
  ASSERT_EQ(radicle_plugin_set_conditions(plugin.get(), radicle_form_key{kPlugin, 0x801},
                                          "Effects[1].Conditions", &row, 1, &status),
            RADICLE_STATUS_OK);
  EXPECT_EQ(status, RADICLE_CONDITION_SCOPE_INDEX_OUT_OF_RANGE);

  const auto env = open_env(input_);
  const radicle_condition_list* list = nullptr;
  EXPECT_EQ(radicle_record_conditions(env.get(), winner(env.get(), 0x801), "Effects[1].Conditions",
                                      &list),
            RADICLE_STATUS_NOT_FOUND);
  EXPECT_EQ(list, nullptr);
}

TEST_F(ConditionsCApi, UnknownScopePathIsInvalidArgument) {
  auto plugin = read_plugin(input_);
  const auto row = radicle_condition{0x00, 0u, 0x20, 0, 7, 0, 0, 0, -1, nullptr, nullptr};
  radicle_condition_write_status status{};
  EXPECT_EQ(radicle_plugin_set_conditions(plugin.get(), radicle_form_key{kPlugin, 0x801},
                                          "Effects[0].Bogus", &row, 1, &status),
            RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_plugin_set_conditions(plugin.get(), radicle_form_key{kPlugin, 0x800},
                                          "Effects[0].Conditions", &row, 1, &status),
            RADICLE_STATUS_INVALID_ARGUMENT);

  const auto env = open_env(input_);
  const radicle_condition_list* list = nullptr;
  EXPECT_EQ(radicle_record_conditions(env.get(), winner(env.get(), 0x801), "Effects[0].Bogus",
                                      &list),
            RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(list, nullptr);
}

TEST_F(ConditionsCApi, MissingRecordIsNotFound) {
  auto plugin = read_plugin(input_);
  const auto row = radicle_condition{0x00, 0u, 0x20, 0, 7, 0, 0, 0, -1, nullptr, nullptr};
  radicle_condition_write_status status{};
  EXPECT_EQ(radicle_plugin_set_conditions(plugin.get(), radicle_form_key{kPlugin, 0x999}, nullptr,
                                          &row, 1, &status),
            RADICLE_STATUS_NOT_FOUND);
  EXPECT_EQ(radicle_plugin_set_conditions(plugin.get(), radicle_form_key{kPlugin, 0x999},
                                          "Effects[0].Conditions", &row, 1, &status),
            RADICLE_STATUS_NOT_FOUND);
}

TEST_F(ConditionsCApi, NonWindows1252TextAndNullRowsAreInvalidArgument) {
  auto plugin = read_plugin(input_);
  const auto unrepresentable =
      radicle_condition{0x00, 0u, 0x20, 0, 7, 0, 0, 0, -1, "\xE4\xB8\xAD", nullptr};
  radicle_condition_write_status status{};
  EXPECT_EQ(radicle_plugin_set_conditions(plugin.get(), radicle_form_key{kPlugin, 0x800}, nullptr,
                                          &unrepresentable, 1, &status),
            RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_plugin_set_conditions(plugin.get(), radicle_form_key{kPlugin, 0x800}, nullptr,
                                          nullptr, 1, &status),
            RADICLE_STATUS_INVALID_ARGUMENT);
}

TEST_F(ConditionsCApi, UndecodableRecordRefusesEveryWrite) {
  auto plugin = read_plugin(input_);
  const auto row = radicle_condition{0x00, 0u, 0x20, 0, 7, 0, 0, 0, -1, nullptr, nullptr};
  radicle_condition_write_status status{};
  EXPECT_EQ(radicle_plugin_set_conditions(plugin.get(), radicle_form_key{kPlugin, 0x805}, nullptr,
                                          &row, 1, &status),
            RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_plugin_set_conditions(plugin.get(), radicle_form_key{kPlugin, 0x805}, nullptr,
                                          nullptr, 0, &status),
            RADICLE_STATUS_INVALID_ARGUMENT);
}

TEST_F(ConditionsCApi, RecordFromAnotherEnvironmentIsInvalidArgument) {
  const auto env = open_env(input_);
  const auto other = open_env(input_);
  const radicle_condition_list* list = nullptr;
  EXPECT_EQ(radicle_record_conditions(env.get(), winner(other.get(), 0x800), nullptr, &list),
            RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(list, nullptr);
}

TEST_F(ConditionsCApi, RootReadWithMultipleListsIsAnErrorStatus) {
  const auto env = open_env(input_);
  const radicle_condition_list* list = nullptr;
  EXPECT_EQ(radicle_record_conditions(env.get(), winner(env.get(), 0x804), nullptr, &list),
            RADICLE_STATUS_PARSE_ERROR);
  EXPECT_EQ(list, nullptr);
}

TEST_F(ConditionsCApi, MesgMenuButtonScopeWritesThroughTheRadicleExport) {
  auto plugin = read_plugin(input_);
  const radicle_form_key message{kPlugin, 0x810};
  const auto row = radicle_condition{0x00, 0u, 0x20, 0, 7, 0, 0, 0, -1, "Button", "Param"};
  radicle_condition_write_status status{};
  ASSERT_EQ(radicle_plugin_set_conditions(plugin.get(), message, "Menu Buttons[1].Conditions",
                                          &row, 1, &status),
            RADICLE_STATUS_OK);
  EXPECT_EQ(status, RADICLE_CONDITION_UPDATED);

  const auto env = open_env(save(plugin.get()));
  const auto list =
      conditions_of(env.get(), winner(env.get(), 0x810), "Menu Buttons[1].Conditions");
  ASSERT_EQ(list->count, 1u);
  EXPECT_EQ(list->rows[0].function, 0x20);
  ASSERT_NE(list->rows[0].parameter1_string, nullptr);
  EXPECT_STREQ(list->rows[0].parameter1_string, "Button");
  ASSERT_NE(list->rows[0].parameter2_string, nullptr);
  EXPECT_STREQ(list->rows[0].parameter2_string, "Param");

  const auto first =
      conditions_of(env.get(), winner(env.get(), 0x810), "Menu Buttons[0].Conditions");
  EXPECT_EQ(first->count, 0u);

  const radicle_condition_list* missing = nullptr;
  EXPECT_EQ(radicle_record_conditions(env.get(), winner(env.get(), 0x810),
                                      "Menu Buttons[2].Conditions", &missing),
            RADICLE_STATUS_NOT_FOUND);
  EXPECT_EQ(missing, nullptr);
}

fo4::CtdaCondition typed_condition(std::string_view function, std::int32_t param1, std::int32_t param2) {
  fo4::CtdaCondition c;
  c.function = fo4::schema::condition_function_by_name(function)->index;
  c.param1 = param1;
  c.param2 = param2;
  return c;
}

std::int32_t float_bits(float value) {
  std::int32_t bits = 0;
  std::memcpy(&bits, &value, sizeof(bits));
  return bits;
}

void write_typed_plugin(const std::filesystem::path& path, std::uint32_t flags,
                        const std::vector<std::string>& masters,
                        const std::vector<fo4::CtdaCondition>& conditions) {
  fo4::test::ByteWriter out;
  out.bytes = fo4::test::make_tes4_plugin(flags, 1.0f, masters);
  if (!conditions.empty()) {
    const auto group = fo4::test::begin_group(out, "PERK", 0);
    const auto perk = fo4::test::begin_record(out, "PERK", 0, 0x01000800);
    fo4::test::write_subrecord(out, "EDID", std::string_view("Typed\0", 6));
    write_run(out, conditions);
    fo4::test::end_record(out, perk);
    fo4::test::end_group(out, group);
  }
  std::ofstream stream(path, std::ios::binary);
  stream.write(reinterpret_cast<const char*>(out.bytes.data()),
               static_cast<std::streamsize>(out.bytes.size()));
}

// Constraint: Regression: the engine typed condition parameters itself, so a float parameter was read as an int, an alias-switched slot as a FormID, a master-indexed id against the wrong plugin, and one bad FormID failed the whole condition list.
TEST_F(ConditionsCApi, TypedParametersFollowTheFunctionTableAndTheMasterTable) {
  ASSERT_EQ(fo4::schema::condition_function(3), nullptr);
  auto stage = typed_condition("GetStageDone", 0x801, 10);
  stage.flags_byte = 0x04;
  stage.comparison_raw = 0x801;
  stage.run_on = 2;
  stage.reference = 0x01000802;
  const auto distance = typed_condition("GetWithinDistance", 0x01000802, float_bits(5.5f));
  auto alias = typed_condition("GetWithinDistance", 3, 0);
  alias.flags_byte = 0x02;
  const auto race = typed_condition("GetIsRace", 0x07000800, 0);
  fo4::CtdaCondition unknown;
  unknown.function = 3;
  unknown.param1 = 7;
  unknown.param2 = 9;
  auto variable = typed_condition("GetVMQuestVariable", 0x801, 0);
  variable.parameter2_string = "MyVar";
  write_typed_plugin(root_ / "TypedMaster.esm", 0x1, {}, {});
  write_typed_plugin(root_ / "Typed.esp", 0, {"TypedMaster.esm"},
                     {stage, distance, alias, race, unknown, variable});
  const auto master_path = (root_ / "TypedMaster.esm").string();
  const auto patch_path = (root_ / "Typed.esp").string();
  const char* paths[] = {master_path.c_str(), patch_path.c_str()};
  radicle_env* loaded = nullptr;
  ASSERT_EQ(radicle_env_load(paths, 2, &loaded), RADICLE_STATUS_OK);
  const EnvPtr env(loaded, radicle_env_destroy);
  const radicle_record* record = nullptr;
  ASSERT_EQ(radicle_env_winner(env.get(), radicle_form_key{"Typed.esp", 0x800}, &record),
            RADICLE_STATUS_OK);
  const auto list = conditions_of(env.get(), record, nullptr);
  ASSERT_EQ(list->count, 6u);
  std::vector<radicle_condition_typed> typed(list->count);
  ASSERT_EQ(radicle_condition_params(env.get(), record, list->rows, list->count, typed.data()),
            RADICLE_STATUS_OK);

  EXPECT_STREQ(typed[0].function_name, "GetStageDone");
  EXPECT_EQ(typed[0].param1.kind, RADICLE_CONDITION_PARAM_KIND_FORM);
  EXPECT_STREQ(typed[0].param1.type_name, "quest");
  ASSERT_NE(typed[0].param1.form.plugin, nullptr);
  EXPECT_STREQ(typed[0].param1.form.plugin, "TypedMaster.esm");
  EXPECT_EQ(typed[0].param1.form.local_id, 0x801u);
  radicle_form_key resolved{};
  ASSERT_EQ(radicle_record_resolve_form_id(env.get(), record, 0x801, &resolved), RADICLE_STATUS_OK);
  EXPECT_STREQ(resolved.plugin, typed[0].param1.form.plugin);
  EXPECT_EQ(resolved.local_id, typed[0].param1.form.local_id);
  radicle_form_key own{nullptr, 0};
  ASSERT_EQ(radicle_record_resolve_form_id(env.get(), record, 0x01000800u, &own), RADICLE_STATUS_OK);
  EXPECT_STREQ(own.plugin, "Typed.esp");
  EXPECT_EQ(own.local_id, 0x800u);
  radicle_form_key past{nullptr, 0};
  EXPECT_EQ(radicle_record_resolve_form_id(env.get(), record, 0x02000800u, &past), RADICLE_STATUS_PARSE_ERROR);
  EXPECT_EQ(past.plugin, nullptr);
  EXPECT_EQ(past.local_id, 0u);
  radicle_form_key created{nullptr, 0};
  EXPECT_EQ(radicle_record_resolve_form_id(env.get(), record, 0xFF000800u, &created), RADICLE_STATUS_PARSE_ERROR);
  EXPECT_EQ(created.plugin, nullptr);
  radicle_form_key none{nullptr, 0};
  EXPECT_EQ(radicle_record_resolve_form_id(env.get(), record, 0xFFFFFFFFu, &none), RADICLE_STATUS_OK);
  EXPECT_EQ(none.plugin, nullptr);
  EXPECT_EQ(typed[0].param2.kind, RADICLE_CONDITION_PARAM_KIND_OTHER);
  EXPECT_STREQ(typed[0].param2.type_name, "quest_stage");
  EXPECT_EQ(typed[0].param2.number, 10.0);
  EXPECT_EQ(typed[0].comparison.kind, RADICLE_CONDITION_PARAM_KIND_FORM);
  EXPECT_STREQ(typed[0].comparison.type_name, "global");
  EXPECT_STREQ(typed[0].comparison.form.plugin, "TypedMaster.esm");
  EXPECT_EQ(typed[0].reference.kind, RADICLE_CONDITION_PARAM_KIND_FORM);
  EXPECT_STREQ(typed[0].reference.type_name, "reference");
  EXPECT_STREQ(typed[0].reference.form.plugin, "Typed.esp");
  EXPECT_EQ(typed[0].reference.form.local_id, 0x802u);

  EXPECT_STREQ(typed[1].param1.form.plugin, "Typed.esp");
  EXPECT_EQ(typed[1].param1.form.local_id, 0x802u);
  EXPECT_EQ(typed[1].param2.kind, RADICLE_CONDITION_PARAM_KIND_FLOAT);
  EXPECT_EQ(typed[1].param2.number, 5.5);
  EXPECT_EQ(typed[1].comparison.kind, RADICLE_CONDITION_PARAM_KIND_FLOAT);

  EXPECT_EQ(typed[2].param1.kind, RADICLE_CONDITION_PARAM_KIND_INDEX);
  EXPECT_EQ(typed[2].param1.number, 3.0);
  EXPECT_EQ(typed[2].param1.form.plugin, nullptr);

  EXPECT_EQ(typed[3].param1.kind, RADICLE_CONDITION_PARAM_KIND_FORM);
  EXPECT_EQ(typed[3].param1.form.plugin, nullptr);
  EXPECT_EQ(typed[3].param1.form_unresolved, 1);

  EXPECT_EQ(typed[4].function_name, nullptr);
  EXPECT_EQ(typed[4].param1.kind, RADICLE_CONDITION_PARAM_KIND_NONE);
  EXPECT_EQ(typed[4].param1.raw, 7);
  EXPECT_EQ(typed[4].param2.raw, 9);

  EXPECT_STREQ(typed[5].param1.form.plugin, "TypedMaster.esm");
  EXPECT_EQ(typed[5].param2.kind, RADICLE_CONDITION_PARAM_KIND_STRING);
  ASSERT_NE(typed[5].param2.text, nullptr);
  EXPECT_STREQ(typed[5].param2.text, "MyVar");
}

// Constraint: Regression: a null, none or created FormID, a record of another environment or a missing buffer must come back as a plain status or an unresolved slot, never as a thrown error or a key into the wrong plugin.
TEST_F(ConditionsCApi, TypedParametersRefuseBadArgumentsAndLeaveNullAndCreatedIdsWithoutKeys) {
  const auto env = open_env(input_);
  const auto other = open_env(input_);
  const auto* record = winner(env.get(), 0x800);
  radicle_condition rows[3]{};
  for (auto& row : rows) row.function = 69;
  rows[1].param1 = -1;
  rows[2].param1 = static_cast<std::int32_t>(0xFF000800u);
  radicle_condition_typed out[3]{};

  EXPECT_EQ(radicle_condition_params(nullptr, record, rows, 3, out), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_condition_params(env.get(), nullptr, rows, 3, out), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_condition_params(env.get(), record, nullptr, 3, out), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_condition_params(env.get(), record, rows, 3, nullptr), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_condition_params(env.get(), winner(other.get(), 0x800), rows, 3, out),
            RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_condition_params(env.get(), record, nullptr, 0, nullptr), RADICLE_STATUS_OK);

  ASSERT_EQ(radicle_condition_params(env.get(), record, rows, 3, out), RADICLE_STATUS_OK);
  for (const auto& typed : out) {
    EXPECT_EQ(typed.param1.kind, RADICLE_CONDITION_PARAM_KIND_FORM);
    EXPECT_EQ(typed.param1.form.plugin, nullptr);
  }
  EXPECT_EQ(out[0].param1.form_unresolved, 0);
  EXPECT_EQ(out[1].param1.form_unresolved, 0);
  EXPECT_EQ(out[2].param1.form_unresolved, 1);
}

TEST_F(ConditionsCApi, ListFreeAcceptsNull) {
  radicle_condition_list_free(nullptr);
}

// Constraint: Regression: the managed editor used to keep its own copy of the condition
// function table. This export is what replaced it, so it must hand out exactly
// Constraint: the generated rows, parameter names and editor flags, not a subset or a paraphrase.
TEST(CApiConditions, SchemaExportMatchesGeneratedTable) {
  const radicle_condition_schema_meta* schema = nullptr;
  ASSERT_EQ(radicle_condition_schema_get(&schema), RADICLE_STATUS_OK);
  ASSERT_NE(schema, nullptr);
  ASSERT_EQ(schema->function_count, fo4::schema::condition_functions().size());
  ASSERT_EQ(schema->run_on_count, fo4::schema::condition_run_ons().size());

  std::size_t editor_count = 0;
  for (std::size_t i = 0; i < schema->function_count; ++i) {
    const radicle_condition_function_meta& row = schema->functions[i];
    const fo4::schema::ConditionFunction* function = fo4::schema::condition_function(row.index);
    ASSERT_NE(function, nullptr) << row.index;
    EXPECT_EQ(std::string_view(row.name), function->name);
    std::size_t declared = 0;
    for (const auto* parameter : function->parameters) {
      if (parameter == nullptr) break;
      ASSERT_LT(declared, 3u) << function->name;
      EXPECT_EQ(std::string_view(row.parameter_names[declared]), parameter->name) << function->name;
      ++declared;
    }
    EXPECT_EQ(row.parameter_count, declared) << function->name;
    for (std::size_t slot = declared; slot < 3; ++slot) {
      EXPECT_EQ(row.parameter_names[slot], nullptr) << function->name;
    }
    if (row.editor != 0) ++editor_count;
  }
  EXPECT_EQ(editor_count, fo4::schema::condition_editor_function_indices().size());

  for (std::size_t i = 0; i < schema->run_on_count; ++i) {
    const fo4::schema::ConditionRunOn* expected = fo4::schema::condition_run_on(schema->run_ons[i].value);
    ASSERT_NE(expected, nullptr) << schema->run_ons[i].value;
    EXPECT_EQ(std::string_view(schema->run_ons[i].name), expected->name);
  }

  radicle_condition_schema_free(schema);
  radicle_condition_schema_free(nullptr);
}

}  // namespace

TEST(ConditionFunctionId, NamesMapToTheCtdaFunctionIdsAndIgnoreAsciiCase) {
  uint16_t id = 0;
  ASSERT_EQ(radicle_condition_function_id("GetStage", &id), RADICLE_STATUS_OK);
  EXPECT_EQ(id, 58u);
  ASSERT_EQ(radicle_condition_function_id("GetStageDone", &id), RADICLE_STATUS_OK);
  EXPECT_EQ(id, 59u);
  ASSERT_EQ(radicle_condition_function_id("GetIsRace", &id), RADICLE_STATUS_OK);
  EXPECT_EQ(id, 69u);
  ASSERT_EQ(radicle_condition_function_id("getisid", &id), RADICLE_STATUS_OK);
  EXPECT_EQ(id, 72u);
}

TEST(ConditionFunctionId, UnknownNamesAreNotFoundAndNullArgumentsAreRejected) {
  uint16_t id = 7;
  EXPECT_EQ(radicle_condition_function_id("NoSuchFunction", &id), RADICLE_STATUS_NOT_FOUND);
  EXPECT_EQ(id, 7u);
  EXPECT_EQ(radicle_condition_function_id(nullptr, &id), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_condition_function_id("GetStage", nullptr), RADICLE_STATUS_INVALID_ARGUMENT);
}

TEST(ConditionFunctionInfo, FormLinkFirstParametersAreTakenFromTheCtdaTableAndCoverTheEngineList) {
  constexpr std::uint16_t engine_form_param_functions[] = {58, 59, 69, 71, 72, 73, 74, 560};
  for (const auto id : engine_form_param_functions) {
    radicle_condition_function_facts facts{};
    ASSERT_EQ(radicle_condition_function_info(id, &facts), RADICLE_STATUS_OK) << id;
    EXPECT_EQ(facts.takes_form_param, 1) << id;
  }
  radicle_condition_function_facts facts{};
  ASSERT_EQ(radicle_condition_function_info(58, &facts), RADICLE_STATUS_OK);
  EXPECT_STREQ(facts.name, "GetStage");
  ASSERT_EQ(radicle_condition_function_info(426, &facts), RADICLE_STATUS_OK);
  EXPECT_STREQ(facts.name, "GetIsVoiceType");
  EXPECT_EQ(facts.takes_form_param, 1);
  ASSERT_EQ(radicle_condition_function_info(18, &facts), RADICLE_STATUS_OK);
  EXPECT_EQ(facts.takes_form_param, 0);
  EXPECT_EQ(facts.first_param_is_quest, 0);
}

TEST(ConditionFunctionInfo, QuestFirstParametersAreFlaggedAndUnknownIdsAndNullOutputsAreRejected) {
  constexpr std::uint16_t quest_functions[] = {56, 58, 59, 543, 629, 664};
  for (const auto id : quest_functions) {
    radicle_condition_function_facts facts{};
    ASSERT_EQ(radicle_condition_function_info(id, &facts), RADICLE_STATUS_OK) << id;
    EXPECT_EQ(facts.first_param_is_quest, 1) << id;
  }
  radicle_condition_function_facts facts{};
  ASSERT_EQ(radicle_condition_function_info(69, &facts), RADICLE_STATUS_OK);
  EXPECT_EQ(facts.first_param_is_quest, 0);
  EXPECT_EQ(radicle_condition_function_info(65535, &facts), RADICLE_STATUS_NOT_FOUND);
  EXPECT_EQ(radicle_condition_function_info(58, nullptr), RADICLE_STATUS_INVALID_ARGUMENT);
}
