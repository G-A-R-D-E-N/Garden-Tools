#include "fo4/c_api.h"
#include "fo4/c_api_localization.h"
#include "fo4/localization/strings.hpp"
#include "fo4/plugin/reader.hpp"
#include "plugin_bytes.hpp"
#include "temp_data_dir.hpp"
#include "../src/localization/string_table_rollback.hpp"

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace {

constexpr const char* kPlugin = "Localized.esp";
constexpr const char* kRaceKey = "000800:Localized.esp";
constexpr auto k_full = fo4::Signature::from_string("FULL");
constexpr auto k_desc = fo4::Signature::from_string("DESC");

using PluginPtr = std::unique_ptr<radicle_plugin, decltype(&radicle_plugin_destroy)>;
using TablesPtr = std::unique_ptr<radicle_localization_tables, decltype(&radicle_localization_tables_destroy)>;

std::vector<std::byte> zstr(std::string_view value) {
  std::vector<std::byte> out(value.size() + 1);
  for (std::size_t i = 0; i < value.size(); ++i) {
    out[i] = std::byte{static_cast<std::uint8_t>(value[i])};
  }
  return out;
}

std::vector<std::byte> words(std::uint32_t value) {
  std::vector<std::byte> out(4);
  std::memcpy(out.data(), &value, 4);
  return out;
}

std::vector<std::byte> race_plugin(std::uint32_t tes4_flags,
                                   const std::vector<std::byte>& full,
                                   const std::vector<std::byte>& desc) {
  fo4::test::ByteWriter out;
  fo4::test::ByteWriter hedr;
  hedr.f32(1.0f);
  hedr.u32(0);
  hedr.u32(0x800);
  const auto tes4 = fo4::test::begin_record(out, "TES4", tes4_flags, 0);
  fo4::test::write_subrecord(out, "HEDR", hedr.bytes);
  fo4::test::end_record(out, tes4);

  const auto races = fo4::test::begin_group(out, "RACE", 0);
  const auto race = fo4::test::begin_record(out, "RACE", 0, 0x800);
  fo4::test::write_subrecord(out, "EDID", zstr("RadicleRace"));
  fo4::test::write_subrecord(out, "FULL", full);
  fo4::test::write_subrecord(out, "DESC", desc);
  fo4::test::end_record(out, race);
  fo4::test::end_group(out, races);
  return out.bytes;
}

PluginPtr open_plugin(const std::filesystem::path& path) {
  const auto text = path.string();
  radicle_plugin* plugin = nullptr;
  EXPECT_EQ(radicle_plugin_read_file(text.c_str(), &plugin), RADICLE_STATUS_OK) << radicle_last_error();
  return PluginPtr(plugin, radicle_plugin_destroy);
}

PluginPtr open_fixture(fo4::test::TempDataDir& dir,
                       std::uint32_t tes4_flags,
                       const std::vector<std::byte>& full,
                       const std::vector<std::byte>& desc) {
  dir.write(kPlugin, race_plugin(tes4_flags, full, desc), 0);
  return open_plugin(dir.path() / kPlugin);
}

TablesPtr new_tables() {
  radicle_localization_tables* tables = nullptr;
  EXPECT_EQ(radicle_localization_tables_create(&tables), RADICLE_STATUS_OK);
  return TablesPtr(tables, radicle_localization_tables_destroy);
}

fo4::Plugin snapshot(const radicle_plugin* plugin) {
  std::uint8_t* bytes = nullptr;
  std::size_t size = 0;
  EXPECT_EQ(radicle_plugin_write_bytes(plugin, &bytes, &size), RADICLE_STATUS_OK) << radicle_last_error();
  std::vector<std::byte> copy(reinterpret_cast<std::byte*>(bytes), reinterpret_cast<std::byte*>(bytes) + size);
  radicle_free_bytes(bytes);
  return fo4::read_plugin(copy, kPlugin);
}

std::vector<std::byte> payload(const fo4::Plugin& plugin, fo4::Signature signature) {
  const auto* record = fo4::find_record(plugin, fo4::FormKey::parse(kRaceKey));
  EXPECT_NE(record, nullptr);
  if (record == nullptr) return {};
  for (const auto& subrecord : record->subrecords) {
    if (subrecord.signature == signature) return subrecord.data;
  }
  return {};
}

radicle_form_key race_key(std::uint32_t local = 0x800) { return radicle_form_key{kPlugin, local}; }

std::vector<std::byte> plugin_bytes(const radicle_plugin* plugin) {
  std::uint8_t* bytes = nullptr;
  std::size_t size = 0;
  EXPECT_EQ(radicle_plugin_write_bytes(plugin, &bytes, &size), RADICLE_STATUS_OK) << radicle_last_error();
  std::vector<std::byte> copy(reinterpret_cast<std::byte*>(bytes), reinterpret_cast<std::byte*>(bytes) + size);
  radicle_free_bytes(bytes);
  return copy;
}

void write_text_file(const std::filesystem::path& path, std::string_view text) {
  std::filesystem::create_directories(path.parent_path());
  std::ofstream(path, std::ios::binary) << text;
}

std::string read_text_file(const std::filesystem::path& path) {
  std::ifstream in(path, std::ios::binary);
  return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

using Kind = fo4::localization::StringTableKind;

std::filesystem::path sidecar(const std::filesystem::path& data, Kind kind) {
  return data / fo4::localization::string_table_path(kPlugin, kind, "en");
}

TablesPtr load_three_table_set(const std::filesystem::path& data) {
  const auto bytes = [](std::string_view text) {
    std::vector<std::byte> out;
    for (const char ch : text) out.push_back(std::byte{static_cast<std::uint8_t>(ch)});
    return out;
  };
  fo4::localization::write_string_table_file({Kind::strings, {{1, bytes("Name")}}}, sidecar(data, Kind::strings));
  fo4::localization::write_string_table_file({Kind::dlstrings, {{2, bytes("Description")}}}, sidecar(data, Kind::dlstrings));
  fo4::localization::write_string_table_file({Kind::ilstrings, {{3, bytes("Info")}}}, sidecar(data, Kind::ilstrings));
  const auto text = data.string();
  radicle_localization_tables* raw = nullptr;
  EXPECT_EQ(radicle_localization_tables_load(text.c_str(), kPlugin, "en", &raw), RADICLE_STATUS_OK) << radicle_last_error();
  return TablesPtr(raw, radicle_localization_tables_destroy);
}

std::size_t staging_leftovers(const std::filesystem::path& directory) {
  std::size_t count = 0;
  for (const auto& entry : std::filesystem::directory_iterator(directory)) {
    if (entry.path().filename().string().find("radicle-") != std::string::npos) ++count;
  }
  return count;
}

}  // namespace

TEST(CApiLocalization, TablesCreateAndDestroyAcceptNull) {
  EXPECT_EQ(radicle_localization_tables_create(nullptr), RADICLE_STATUS_INVALID_ARGUMENT);
  radicle_localization_tables* tables = nullptr;
  ASSERT_EQ(radicle_localization_tables_create(&tables), RADICLE_STATUS_OK);
  ASSERT_NE(tables, nullptr);
  radicle_localization_tables_destroy(tables);
  radicle_localization_tables_destroy(nullptr);
}

TEST(CApiLocalization, LoadAndWriteRejectNullArguments) {
  radicle_localization_tables* tables = nullptr;
  EXPECT_EQ(radicle_localization_tables_load(nullptr, kPlugin, "en", &tables), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_localization_tables_load(".", nullptr, "en", &tables), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_localization_tables_load(".", kPlugin, nullptr, &tables), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_localization_tables_load(".", kPlugin, "en", nullptr), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(tables, nullptr);
  EXPECT_EQ(radicle_localization_tables_write(nullptr, ".", kPlugin, "en"), RADICLE_STATUS_INVALID_ARGUMENT);
}

TEST(CApiLocalization, LoadWithoutTableFilesYieldsEmptyTables) {
  fo4::test::TempDataDir dir;
  const auto data = dir.path().string();
  radicle_localization_tables* raw = nullptr;
  ASSERT_EQ(radicle_localization_tables_load(data.c_str(), kPlugin, "en", &raw), RADICLE_STATUS_OK);
  TablesPtr tables(raw, radicle_localization_tables_destroy);

  fo4::test::TempDataDir plugin_dir;
  auto plugin = open_fixture(plugin_dir, 0x80u, words(1), words(0));
  radicle_plugin_localization_result result{};
  EXPECT_EQ(radicle_plugin_delocalize(plugin.get(), tables.get(), &result), RADICLE_STATUS_NOT_FOUND);
  EXPECT_EQ(result.status, RADICLE_PLUGIN_LOCALIZATION_UNRESOLVED_STRING_ID);
}

TEST(CApiLocalization, LocalizeThenDelocalizeRestoresPayloads) {
  fo4::test::TempDataDir dir;
  auto plugin = open_fixture(dir, 0u, zstr("Race name"), zstr("Race description"));
  auto tables = new_tables();

  radicle_plugin_localization_result result{};
  ASSERT_EQ(radicle_plugin_localize(plugin.get(), tables.get(), &result), RADICLE_STATUS_OK) << radicle_last_error();
  EXPECT_EQ(result.status, RADICLE_PLUGIN_LOCALIZATION_CONVERTED);
  EXPECT_EQ(result.converted, 2u);
  EXPECT_EQ(result.empty_text, 0u);
  const auto localized = snapshot(plugin.get());
  EXPECT_TRUE(localized.header.tes4_flags.is_localized());
  EXPECT_EQ(payload(localized, k_full), words(1));
  EXPECT_EQ(payload(localized, k_desc), words(1));

  ASSERT_EQ(radicle_plugin_delocalize(plugin.get(), tables.get(), &result), RADICLE_STATUS_OK) << radicle_last_error();
  EXPECT_EQ(result.status, RADICLE_PLUGIN_LOCALIZATION_CONVERTED);
  EXPECT_EQ(result.converted, 2u);
  const auto restored = snapshot(plugin.get());
  EXPECT_FALSE(restored.header.tes4_flags.is_localized());
  EXPECT_EQ(payload(restored, k_full), zstr("Race name"));
  EXPECT_EQ(payload(restored, k_desc), zstr("Race description"));
}

TEST(CApiLocalization, WrittenTablesLoadBackForDelocalize) {
  fo4::test::TempDataDir dir;
  auto plugin = open_fixture(dir, 0u, zstr("Race name"), zstr("Race description"));
  auto tables = new_tables();
  radicle_plugin_localization_result result{};
  ASSERT_EQ(radicle_plugin_localize(plugin.get(), tables.get(), &result), RADICLE_STATUS_OK);

  const auto data = dir.path().string();
  ASSERT_EQ(radicle_localization_tables_write(tables.get(), data.c_str(), kPlugin, "en"), RADICLE_STATUS_OK)
      << radicle_last_error();
  EXPECT_TRUE(std::filesystem::exists(
      dir.path() / fo4::localization::string_table_path(kPlugin, fo4::localization::StringTableKind::strings, "en")));
  EXPECT_TRUE(std::filesystem::exists(
      dir.path() / fo4::localization::string_table_path(kPlugin, fo4::localization::StringTableKind::dlstrings, "en")));

  radicle_localization_tables* raw = nullptr;
  ASSERT_EQ(radicle_localization_tables_load(data.c_str(), kPlugin, "en", &raw), RADICLE_STATUS_OK);
  TablesPtr loaded(raw, radicle_localization_tables_destroy);
  ASSERT_EQ(radicle_plugin_delocalize(plugin.get(), loaded.get(), &result), RADICLE_STATUS_OK)
      << radicle_last_error();
  EXPECT_EQ(result.converted, 2u);
  const auto restored = snapshot(plugin.get());
  EXPECT_EQ(payload(restored, k_full), zstr("Race name"));
  EXPECT_EQ(payload(restored, k_desc), zstr("Race description"));
}

TEST(CApiLocalization, SetLocalizedStringAllocatesAnIdAndDelocalizesToText) {
  fo4::test::TempDataDir dir;
  auto plugin = open_fixture(dir, 0x80u, words(0), words(0));
  auto tables = new_tables();

  radicle_localized_value_status status = RADICLE_LOCALIZED_VALUE_RECORD_MISSING;
  ASSERT_EQ(radicle_plugin_set_localized_string(plugin.get(), race_key(), "FULL", 0, "New name", tables.get(), &status),
            RADICLE_STATUS_OK)
      << radicle_last_error();
  EXPECT_EQ(status, RADICLE_LOCALIZED_VALUE_UPDATED);
  EXPECT_EQ(payload(snapshot(plugin.get()), k_full), words(1));

  radicle_plugin_localization_result result{};
  ASSERT_EQ(radicle_plugin_delocalize(plugin.get(), tables.get(), &result), RADICLE_STATUS_OK)
      << radicle_last_error();
  EXPECT_EQ(result.converted, 2u);
  EXPECT_EQ(result.empty_text, 1u);
  const auto restored = snapshot(plugin.get());
  EXPECT_EQ(payload(restored, k_full), zstr("New name"));
  EXPECT_EQ(payload(restored, k_desc), zstr(""));
}

TEST(CApiLocalization, SetLocalizedStringRefusesWithStatuses) {
  fo4::test::TempDataDir dir;
  auto plugin = open_fixture(dir, 0x80u, words(0), words(0));
  auto tables = new_tables();
  radicle_localized_value_status status = RADICLE_LOCALIZED_VALUE_UPDATED;

  EXPECT_EQ(radicle_plugin_set_localized_string(plugin.get(), race_key(), "FULL", 0, "", tables.get(), &status),
            RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(status, RADICLE_LOCALIZED_VALUE_EMPTY_TEXT);

  EXPECT_EQ(radicle_plugin_set_localized_string(plugin.get(), race_key(), "EDID", 0, "x", tables.get(), &status),
            RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(status, RADICLE_LOCALIZED_VALUE_FIELD_NOT_LOCALIZED);

  EXPECT_EQ(radicle_plugin_set_localized_string(plugin.get(), race_key(), "FULL", 1, "x", tables.get(), &status),
            RADICLE_STATUS_NOT_FOUND);
  EXPECT_EQ(status, RADICLE_LOCALIZED_VALUE_SUBRECORD_MISSING);

  EXPECT_EQ(radicle_plugin_set_localized_string(plugin.get(), race_key(0x8FF), "FULL", 0, "x", tables.get(), &status),
            RADICLE_STATUS_NOT_FOUND);
  EXPECT_EQ(status, RADICLE_LOCALIZED_VALUE_RECORD_MISSING);

  EXPECT_EQ(radicle_plugin_set_localized_string(plugin.get(), race_key(), "FUL", 0, "x", tables.get(), &status),
            RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_plugin_set_localized_string(plugin.get(), race_key(), "FULL", 0, "\xE4\xB8\x80", tables.get(), &status),
            RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_plugin_set_localized_string(plugin.get(), race_key(), "FULL", 0, "x", tables.get(), nullptr),
            RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(payload(snapshot(plugin.get()), k_full), words(0));

  fo4::test::TempDataDir plain_dir;
  auto plain = open_fixture(plain_dir, 0u, zstr("Race name"), zstr("Race description"));
  EXPECT_EQ(radicle_plugin_set_localized_string(plain.get(), race_key(), "FULL", 0, "x", tables.get(), &status),
            RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(status, RADICLE_LOCALIZED_VALUE_PLUGIN_NOT_LOCALIZED);
}

TEST(CApiLocalization, LocalizeAndDelocalizeRefusals) {
  radicle_plugin_localization_result result{};

  fo4::test::TempDataDir localized_dir;
  auto localized = open_fixture(localized_dir, 0x80u, words(0), words(0));
  auto tables = new_tables();
  EXPECT_EQ(radicle_plugin_localize(localized.get(), tables.get(), &result), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(result.status, RADICLE_PLUGIN_LOCALIZATION_ALREADY_LOCALIZED);
  EXPECT_EQ(result.converted, 0u);
  EXPECT_EQ(radicle_plugin_localize(localized.get(), tables.get(), nullptr), RADICLE_STATUS_INVALID_ARGUMENT);

  fo4::test::TempDataDir unconvertible_dir;
  auto unconvertible = open_fixture(unconvertible_dir, 0u, zstr("Race name"),
                                    std::vector<std::byte>{std::byte{1}, std::byte{2}, std::byte{3}, std::byte{4}});
  const auto before = snapshot(unconvertible.get());
  EXPECT_EQ(radicle_plugin_localize(unconvertible.get(), tables.get(), &result), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(result.status, RADICLE_PLUGIN_LOCALIZATION_UNCONVERTIBLE_FIELD);
  EXPECT_EQ(result.converted, 0u);
  EXPECT_FALSE(snapshot(unconvertible.get()).header.tes4_flags.is_localized());
  EXPECT_EQ(payload(snapshot(unconvertible.get()), k_full), payload(before, k_full));

  fo4::test::TempDataDir plain_dir;
  auto plain = open_fixture(plain_dir, 0u, zstr("Race name"), zstr("Race description"));
  EXPECT_EQ(radicle_plugin_delocalize(plain.get(), tables.get(), &result), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(result.status, RADICLE_PLUGIN_LOCALIZATION_NOT_LOCALIZED);
  EXPECT_EQ(result.converted, 0u);

  fo4::test::TempDataDir bad_payload_dir;
  auto bad_payload = open_fixture(bad_payload_dir, 0x80u, zstr("Race name"), words(0));
  EXPECT_EQ(radicle_plugin_delocalize(bad_payload.get(), tables.get(), &result), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(result.status, RADICLE_PLUGIN_LOCALIZATION_UNCONVERTIBLE_FIELD);
  EXPECT_TRUE(snapshot(bad_payload.get()).header.tes4_flags.is_localized());

  fo4::test::TempDataDir missing_dir;
  auto missing = open_fixture(missing_dir, 0x80u, words(1), words(0));
  EXPECT_EQ(radicle_plugin_delocalize(missing.get(), tables.get(), &result), RADICLE_STATUS_NOT_FOUND);
  EXPECT_EQ(result.status, RADICLE_PLUGIN_LOCALIZATION_UNRESOLVED_STRING_ID);
  EXPECT_EQ(result.converted, 0u);
  EXPECT_TRUE(snapshot(missing.get()).header.tes4_flags.is_localized());
  EXPECT_EQ(payload(snapshot(missing.get()), k_full), words(1));
}

TEST(CApiLocalization, FailedLocalizeAndDelocalizeLeavePluginBytesAndTablesUnchanged) {
  radicle_plugin_localization_result result{};
  fo4::test::TempDataDir dir;
  auto unconvertible = open_fixture(dir, 0u, zstr("Race name"),
                                    std::vector<std::byte>{std::byte{1}, std::byte{2}, std::byte{3}, std::byte{4}});
  auto tables = new_tables();
  const auto before = plugin_bytes(unconvertible.get());
  EXPECT_EQ(radicle_plugin_localize(unconvertible.get(), tables.get(), &result), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(plugin_bytes(unconvertible.get()), before);
  const auto out = dir.path() / "out";
  const auto out_text = out.string();
  ASSERT_EQ(radicle_localization_tables_write(tables.get(), out_text.c_str(), kPlugin, "en"), RADICLE_STATUS_OK);
  EXPECT_FALSE(std::filesystem::exists(out / "Strings"));

  fo4::test::TempDataDir localized_dir;
  auto localized = open_fixture(localized_dir, 0x80u, words(1), words(0));
  const auto localized_before = plugin_bytes(localized.get());
  EXPECT_EQ(radicle_plugin_delocalize(localized.get(), tables.get(), &result), RADICLE_STATUS_NOT_FOUND);
  EXPECT_EQ(plugin_bytes(localized.get()), localized_before);
}

TEST(CApiLocalization, WriteKeepsExistingStringFilesForUnchangedEmptyTables) {
  fo4::test::TempDataDir dir;
  const auto strings_dir = dir.path() / "Strings";
  write_text_file(strings_dir / "Localized_en.DLSTRINGS", "existing dlstrings");
  write_text_file(strings_dir / "Localized_en.ILSTRINGS", "existing ilstrings");
  auto plugin = open_fixture(dir, 0x80u, words(0), words(0));
  auto tables = new_tables();
  radicle_localized_value_status status = RADICLE_LOCALIZED_VALUE_RECORD_MISSING;
  ASSERT_EQ(radicle_plugin_set_localized_string(plugin.get(), race_key(), "FULL", 0, "New name", tables.get(), &status),
            RADICLE_STATUS_OK)
      << radicle_last_error();

  const auto data = dir.path().string();
  ASSERT_EQ(radicle_localization_tables_write(tables.get(), data.c_str(), kPlugin, "en"), RADICLE_STATUS_OK)
      << radicle_last_error();
  EXPECT_TRUE(std::filesystem::exists(
      dir.path() / fo4::localization::string_table_path(kPlugin, fo4::localization::StringTableKind::strings, "en")));
  EXPECT_EQ(read_text_file(strings_dir / "Localized_en.DLSTRINGS"), "existing dlstrings");
  EXPECT_EQ(read_text_file(strings_dir / "Localized_en.ILSTRINGS"), "existing ilstrings");
}

TEST(CApiLocalization, WriteReplacesAllThreeExistingSidecarsAndLeavesNoStagingFiles) {
  fo4::test::TempDataDir source;
  auto tables = load_three_table_set(source.path());

  fo4::test::TempDataDir dir;
  for (const auto kind : {Kind::strings, Kind::dlstrings, Kind::ilstrings}) {
    write_text_file(sidecar(dir.path(), kind), "old");
  }
  const auto data = dir.path().string();
  ASSERT_EQ(radicle_localization_tables_write(tables.get(), data.c_str(), kPlugin, "en"), RADICLE_STATUS_OK)
      << radicle_last_error();
  for (const auto kind : {Kind::strings, Kind::dlstrings, Kind::ilstrings}) {
    EXPECT_EQ(read_text_file(sidecar(dir.path(), kind)), read_text_file(sidecar(source.path(), kind)));
  }
  EXPECT_EQ(staging_leftovers(dir.path() / "Strings"), 0u);
}

TEST(CApiLocalization, WriteFailureAfterFirstSidecarRestoresTheWholeSet) {
  fo4::test::TempDataDir source;
  auto tables = load_three_table_set(source.path());

  fo4::test::TempDataDir dir;
  const auto strings = sidecar(dir.path(), Kind::strings);
  const auto dlstrings = sidecar(dir.path(), Kind::dlstrings);
  write_text_file(strings, "original strings");
  write_text_file(dlstrings / "keep.txt", "occupied");

  const auto data = dir.path().string();
  EXPECT_EQ(radicle_localization_tables_write(tables.get(), data.c_str(), kPlugin, "en"), RADICLE_STATUS_IO_ERROR);
  EXPECT_EQ(read_text_file(strings), "original strings");
  EXPECT_EQ(read_text_file(dlstrings / "keep.txt"), "occupied");
  EXPECT_FALSE(std::filesystem::exists(sidecar(dir.path(), Kind::ilstrings)));
  EXPECT_EQ(staging_leftovers(dir.path() / "Strings"), 0u);

  std::filesystem::remove_all(dlstrings);
  ASSERT_EQ(radicle_localization_tables_write(tables.get(), data.c_str(), kPlugin, "en"), RADICLE_STATUS_OK)
      << radicle_last_error();
  for (const auto kind : {Kind::strings, Kind::dlstrings, Kind::ilstrings}) {
    EXPECT_EQ(read_text_file(sidecar(dir.path(), kind)), read_text_file(sidecar(source.path(), kind)));
  }
  EXPECT_EQ(staging_leftovers(dir.path() / "Strings"), 0u);
}

TEST(CApiLocalization, FailedRollbackReportsBackupPath) {
  fo4::test::TempDataDir dir;
  const auto backup = dir.path() / "missing-backup";
  const auto destination = dir.path() / "original";
  const std::vector<fo4::localization::detail::StringTableRollback> steps{
      {destination, backup, false}};
  const auto error = fo4::localization::detail::rollback_string_tables(steps);
  EXPECT_NE(error.find(backup.string()), std::string::npos);
  EXPECT_NE(error.find(destination.string()), std::string::npos);
}

TEST(CApiLocalization, TablesRefuseLanguagesThatAreNotAsciiLetters) {
  fo4::test::TempDataDir dir;
  const auto data = dir.path().string();
  auto tables = new_tables();
  for (const char* language : {"../x", "", "e.n", "en1", "en/x", "\xC3\xA9"}) {
    radicle_localization_tables* raw = nullptr;
    EXPECT_EQ(radicle_localization_tables_load(data.c_str(), kPlugin, language, &raw), RADICLE_STATUS_INVALID_ARGUMENT)
        << language;
    EXPECT_EQ(raw, nullptr);
    EXPECT_EQ(radicle_localization_tables_write(tables.get(), data.c_str(), kPlugin, language),
              RADICLE_STATUS_INVALID_ARGUMENT)
        << language;
  }
  EXPECT_FALSE(std::filesystem::exists(dir.path() / "Strings"));
}
