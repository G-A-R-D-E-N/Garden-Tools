#include "fo4/c_api_search.h"
#include "fo4/localization/strings.hpp"
#include "plugin_bytes.hpp"

#include <gtest/gtest.h>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace {

struct Sub {
  std::string signature;
  std::string data;
};

struct Rec {
  std::string signature;
  std::uint32_t form_id;
  std::string editor_id;
  std::vector<Sub> subs;
};

std::string zstr(const std::string& text) {
  return text + std::string(1, '\0');
}

std::vector<std::byte> make_plugin(const std::vector<Rec>& records,
                                   const std::vector<std::string>& masters,
                                   std::uint32_t flags = 0) {
  fo4::test::ByteWriter out;
  out.bytes = fo4::test::make_tes4_plugin(flags, 1.0f, masters);
  for (const auto& spec : records) {
    const auto group = fo4::test::begin_group(out, spec.signature, 0);
    const auto record = fo4::test::begin_record(out, spec.signature, 0, spec.form_id);
    fo4::test::write_subrecord(out, "EDID", zstr(spec.editor_id));
    for (const auto& sub : spec.subs) fo4::test::write_subrecord(out, sub.signature, sub.data);
    fo4::test::end_record(out, record);
    fo4::test::end_group(out, group);
  }
  return out.bytes;
}

std::string write_file(const std::filesystem::path& path, const std::vector<std::byte>& bytes) {
  std::ofstream out(path, std::ios::binary);
  out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
  return path.string();
}

bool has_row(char** rows, size_t count, const std::string& expected) {
  for (size_t i = 0; i < count; ++i) {
    if (std::string(rows[i]) == expected) return true;
  }
  return false;
}

struct Fixture {
  std::filesystem::path root;
  std::string base;
  std::string patch;
  radicle_env* env{nullptr};

  Fixture() {
    root = std::filesystem::temp_directory_path() /
           ("radicle_c_search_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::create_directories(root);
    base = write_file(root / "Base.esm",
                      make_plugin({
                                      {"KYWD", 0x800, "KwWidget", {{"DNAM", zstr("Hello Widget")}, {"FULL", zstr("Shiny Gadget")}}},
                                      {"KYWD", 0x801, "KwPlain", {{"DNAM", zstr("Plain text")}}},
                                      {"MISC", 0x802, "MiscBolt", {}},
                                  },
                                  {}));
    patch = write_file(root / "Patch.esp",
                       make_plugin({
                                       {"KYWD", 0x800, "KwWidget", {{"DNAM", zstr("Hello Widget v2")}}},
                                   },
                                   {"Base.esm"}));
    const char* paths[] = {base.c_str(), patch.c_str()};
    EXPECT_EQ(radicle_env_load(paths, 2, &env), RADICLE_STATUS_OK);
  }

  ~Fixture() {
    radicle_env_destroy(env);
    std::error_code ec;
    std::filesystem::remove_all(root, ec);
  }
};

}  // namespace

TEST(CApiSearch, DefaultQueryListsEveryVersionWithPluginIndex) {
  Fixture fixture;
  ASSERT_NE(fixture.env, nullptr);
  radicle_record_search query{};
  char** rows = nullptr;
  size_t count = 0;
  int32_t truncated = -1;
  ASSERT_EQ(radicle_env_search_records(fixture.env, &query, &rows, &count, &truncated), RADICLE_STATUS_OK);
  EXPECT_EQ(count, 4u);
  EXPECT_EQ(truncated, 0);
  EXPECT_TRUE(has_row(rows, count, "000800:Base.esm\tKYWD\t0\tKwWidget"));
  EXPECT_TRUE(has_row(rows, count, "000800:Base.esm\tKYWD\t1\tKwWidget"));
  EXPECT_TRUE(has_row(rows, count, "000802:Base.esm\tMISC\t0\tMiscBolt"));
  radicle_free_string_array(rows, count);
}

TEST(CApiSearch, FieldValueAndFormIdFiltersCombine) {
  Fixture fixture;
  ASSERT_NE(fixture.env, nullptr);
  radicle_record_search query{};
  query.field_value = "hello";
  query.form_id = "00000800";
  char** rows = nullptr;
  size_t count = 0;
  ASSERT_EQ(radicle_env_search_records(fixture.env, &query, &rows, &count, nullptr), RADICLE_STATUS_OK);
  EXPECT_EQ(count, 2u);
  EXPECT_TRUE(has_row(rows, count, "000800:Base.esm\tKYWD\t0\tKwWidget"));
  EXPECT_TRUE(has_row(rows, count, "000800:Base.esm\tKYWD\t1\tKwWidget"));
  radicle_free_string_array(rows, count);
}

TEST(CApiSearch, ConflictMaskSelectsSingleVersionRecords) {
  Fixture fixture;
  ASSERT_NE(fixture.env, nullptr);
  radicle_record_search query{};
  query.conflict_category_mask = RADICLE_CONFLICT_BIT_ONLY_ONE;
  char** rows = nullptr;
  size_t count = 0;
  ASSERT_EQ(radicle_env_search_records(fixture.env, &query, &rows, &count, nullptr), RADICLE_STATUS_OK);
  EXPECT_EQ(count, 2u);
  EXPECT_TRUE(has_row(rows, count, "000801:Base.esm\tKYWD\t0\tKwPlain"));
  EXPECT_TRUE(has_row(rows, count, "000802:Base.esm\tMISC\t0\tMiscBolt"));
  radicle_free_string_array(rows, count);
}

TEST(CApiSearch, LimitTruncatesAndReports) {
  Fixture fixture;
  ASSERT_NE(fixture.env, nullptr);
  radicle_record_search query{};
  query.limit = 1;
  char** rows = nullptr;
  size_t count = 0;
  int32_t truncated = 0;
  ASSERT_EQ(radicle_env_search_records(fixture.env, &query, &rows, &count, &truncated), RADICLE_STATUS_OK);
  EXPECT_EQ(count, 1u);
  EXPECT_EQ(truncated, 1);
  radicle_free_string_array(rows, count);
}

TEST(CApiSearch, InvalidQueriesAreRejected) {
  Fixture fixture;
  ASSERT_NE(fixture.env, nullptr);
  char** rows = nullptr;
  size_t count = 0;

  radicle_record_search bad_signature{};
  const char* signatures[] = {"KY"};
  bad_signature.signatures = signatures;
  bad_signature.signature_count = 1;
  EXPECT_EQ(radicle_env_search_records(fixture.env, &bad_signature, &rows, &count, nullptr),
            RADICLE_STATUS_INVALID_ARGUMENT);

  radicle_record_search bad_mask{};
  bad_mask.conflict_category_mask = 1u << 20;
  EXPECT_EQ(radicle_env_search_records(fixture.env, &bad_mask, &rows, &count, nullptr),
            RADICLE_STATUS_INVALID_ARGUMENT);

  radicle_record_search bad_pattern{};
  const std::string long_pattern(129, '*');
  bad_pattern.editor_id_pattern = long_pattern.c_str();
  EXPECT_EQ(radicle_env_search_records(fixture.env, &bad_pattern, &rows, &count, nullptr),
            RADICLE_STATUS_INVALID_ARGUMENT);

  radicle_record_search bad_form_id{};
  bad_form_id.form_id = "not-hex";
  EXPECT_EQ(radicle_env_search_records(fixture.env, &bad_form_id, &rows, &count, nullptr),
            RADICLE_STATUS_INVALID_ARGUMENT);

  EXPECT_EQ(radicle_env_search_records(nullptr, &bad_form_id, &rows, &count, nullptr),
            RADICLE_STATUS_INVALID_ARGUMENT);
}

TEST(CApiSearch, EightDigitFormIdFollowsTheRuntimeLoadOrderAroundLightPlugins) {
  Fixture fixture;
  ASSERT_NE(fixture.env, nullptr);
  const auto light = write_file(fixture.root / "Light.esl", make_plugin({{"MISC", 0x801, "LightMisc", {}}}, {}));
  const auto later = write_file(fixture.root / "Later.esp", make_plugin({{"MISC", 0x803, "LaterMisc", {}}}, {}));
  const char* paths[] = {fixture.base.c_str(), light.c_str(), later.c_str()};
  radicle_env* env = nullptr;
  ASSERT_EQ(radicle_env_load(paths, 3, &env), RADICLE_STATUS_OK);
  ASSERT_NE(env, nullptr);

  const auto count_for = [&](const char* form_id, const char* text) {
    radicle_record_search query{};
    query.form_id = form_id;
    query.text = text;
    char** rows = nullptr;
    size_t count = 0;
    EXPECT_EQ(radicle_env_search_records(env, &query, &rows, &count, nullptr), RADICLE_STATUS_OK);
    radicle_free_string_array(rows, count);
    return count;
  };
  EXPECT_EQ(count_for("01000803", nullptr), 1u);
  EXPECT_EQ(count_for("FE000801", nullptr), 1u);
  EXPECT_EQ(count_for("FE001801", nullptr), 0u);
  EXPECT_EQ(count_for("02000803", nullptr), 0u);
  EXPECT_EQ(count_for(nullptr, "shiny"), 1u);
  radicle_env_destroy(env);
}

TEST(CApiSearch, LocalizedDisplayNameResolvesThroughLoadedStringTables) {
  Fixture fixture;
  ASSERT_NE(fixture.env, nullptr);
  const auto localized = write_file(
      fixture.root / "Loc.esm",
      make_plugin({{"MISC", 0x800, "LocBlade", {{"FULL", std::string("\x07\x00\x00\x00", 4)}}}}, {}, 0x80));
  fo4::localization::LocalizedStringTable table;
  const std::string name = "Gleaming Blade";
  table.entries.push_back({7, {reinterpret_cast<const std::byte*>(name.data()),
                               reinterpret_cast<const std::byte*>(name.data()) + name.size()}});
  fo4::localization::write_string_table_file(table, fixture.root / "Loc_en.STRINGS");

  const char* paths[] = {fixture.base.c_str(), localized.c_str()};
  radicle_env* env = nullptr;
  ASSERT_EQ(radicle_env_load(paths, 2, &env), RADICLE_STATUS_OK) << radicle_last_error();
  radicle_strings* strings = nullptr;
  ASSERT_EQ(radicle_strings_load_with_assets(env, fixture.root.string().c_str(), "en", nullptr, &strings), RADICLE_STATUS_OK)
      << radicle_last_error();

  const auto count_for = [&](const radicle_strings* tables, bool general) {
    radicle_record_search query{};
    (general ? query.text : query.display_name) = "gleaming";
    char** rows = nullptr;
    size_t count = 0;
    EXPECT_EQ(radicle_env_search_records_with_strings(env, &query, tables, &rows, &count, nullptr),
              RADICLE_STATUS_OK)
        << radicle_last_error();
    radicle_free_string_array(rows, count);
    return count;
  };
  EXPECT_EQ(count_for(strings, false), 1u) << "display_name filter missed a localized FULL";
  EXPECT_EQ(count_for(strings, true), 1u) << "general text filter missed a localized FULL";
  EXPECT_EQ(count_for(nullptr, false), 0u);

  radicle_record_search query{};
  char** rows = nullptr;
  size_t count = 0;
  EXPECT_EQ(radicle_env_search_records_with_strings(fixture.env, &query, strings, &rows, &count, nullptr),
            RADICLE_STATUS_INVALID_ARGUMENT)
      << "string tables of another environment were accepted";

  radicle_strings_destroy(strings);
  radicle_env_destroy(env);
}

TEST(CApiSearch, DiffExportsChangedAndRemovedRecords) {
  Fixture fixture;
  ASSERT_NE(fixture.env, nullptr);
  char** rows = nullptr;
  size_t count = 0;
  ASSERT_EQ(radicle_env_diff_plugins(fixture.env, "Base.esm", "Patch.esp", &rows, &count), RADICLE_STATUS_OK);
  EXPECT_EQ(count, 3u);
  EXPECT_TRUE(has_row(rows, count, "000800:Base.esm\tchanged\tKYWD\t0\t0\tDNAM,FULL"));
  EXPECT_TRUE(has_row(rows, count, "000801:Base.esm\tremoved\tKYWD\t0\t0\t"));
  EXPECT_TRUE(has_row(rows, count, "000802:Base.esm\tremoved\tMISC\t0\t0\t"));
  radicle_free_string_array(rows, count);
}

TEST(CApiSearch, DiffOfAPluginThatAddsAMasterHasNoTes4Row) {
  Fixture fixture;
  ASSERT_NE(fixture.env, nullptr);
  const auto adds = write_file(fixture.root / "Adds.esp",
                               make_plugin({
                                   {"KYWD", 0x800, "KwWidget", {{"DNAM", zstr("Hello Widget")}, {"FULL", zstr("Shiny Gadget")}}},
                               },
                               {"Base.esm"}));
  const char* paths[] = {fixture.base.c_str(), adds.c_str()};
  radicle_env* env = nullptr;
  ASSERT_EQ(radicle_env_load(paths, 2, &env), RADICLE_STATUS_OK);
  ASSERT_NE(env, nullptr);
  char** rows = nullptr;
  size_t count = 0;
  ASSERT_EQ(radicle_env_diff_plugins(env, "Base.esm", "Adds.esp", &rows, &count), RADICLE_STATUS_OK);
  EXPECT_EQ(count, 2u);
  EXPECT_TRUE(has_row(rows, count, "000801:Base.esm\tremoved\tKYWD\t0\t0\t"));
  EXPECT_TRUE(has_row(rows, count, "000802:Base.esm\tremoved\tMISC\t0\t0\t"));
  for (size_t i = 0; i < count; ++i) {
    EXPECT_EQ(std::string(rows[i]).find("\tTES4\t"), std::string::npos);
  }
  radicle_free_string_array(rows, count);
  radicle_env_destroy(env);
}

TEST(CApiSearch, DiffOfAPluginWithItselfIsEmpty) {
  Fixture fixture;
  ASSERT_NE(fixture.env, nullptr);
  char** rows = nullptr;
  size_t count = 0;
  ASSERT_EQ(radicle_env_diff_plugins(fixture.env, "Base.esm", "Base.esm", &rows, &count), RADICLE_STATUS_OK);
  EXPECT_EQ(count, 0u);
  EXPECT_EQ(rows, nullptr);
}

TEST(CApiSearch, DiffRejectsUnknownPlugin) {
  Fixture fixture;
  ASSERT_NE(fixture.env, nullptr);
  char** rows = nullptr;
  size_t count = 0;
  EXPECT_EQ(radicle_env_diff_plugins(fixture.env, "Base.esm", "Missing.esp", &rows, &count),
            RADICLE_STATUS_NOT_FOUND);
}
