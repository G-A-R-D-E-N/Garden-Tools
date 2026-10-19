#include "fo4/c_api.h"
#include "fo4/c_api_conditions.h"
#include "fo4/c_api_env.h"
#include "fo4/schema/ctda.hpp"
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

TEST_F(ConditionsCApi, ListFreeAcceptsNull) {
  radicle_condition_list_free(nullptr);
}

}  // namespace
