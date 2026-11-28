#include "fo4/c_api_authoring.h"
#include "fo4/c_api_cell.h"
#include "fo4/c_api.h"
#include "fo4/localization/strings.hpp"
#include "fo4/plugin/writer.hpp"
#include "fo4/plugin/reader.hpp"
#include "plugin_bytes.hpp"
#include "byte_helpers.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

namespace {

struct Plugin {
  radicle_plugin* handle{nullptr};
  explicit Plugin(const char* filename = "Authoring.esp") {
    EXPECT_EQ(radicle_plugin_create(filename, &handle), RADICLE_STATUS_OK);
  }
  ~Plugin() { radicle_plugin_destroy(handle); }
  Plugin(const Plugin&) = delete;
  Plugin& operator=(const Plugin&) = delete;

  fo4::Plugin snapshot() const {
    std::uint8_t* bytes = nullptr;
    std::size_t size = 0;
    EXPECT_EQ(radicle_plugin_write_bytes(handle, &bytes, &size), RADICLE_STATUS_OK);
    std::vector<std::byte> copy(reinterpret_cast<std::byte*>(bytes),
                                reinterpret_cast<std::byte*>(bytes) + size);
    radicle_free_bytes(bytes);
    return fo4::read_plugin(copy, "Authoring.esp");
  }
};

std::vector<std::string> order(const fo4::Plugin& plugin, std::uint32_t local) {
  std::vector<std::string> out;
  for (const auto* record : fo4::collect_records(plugin)) {
    if (record->header.form_id.value() != local) continue;
    for (const auto& subrecord : record->subrecords) out.push_back(subrecord.signature.to_string());
  }
  return out;
}

using fo4::test::zstr;

std::vector<std::byte> coupled_fixture() {
  fo4::test::ByteWriter out;
  const auto tes4 = fo4::test::begin_record(out, "TES4", 0, 0);
  fo4::test::ByteWriter hedr;
  hedr.f32(1.0f);
  hedr.u32(2);
  hedr.u32(0x900);
  fo4::test::write_subrecord(out, "HEDR", hedr.bytes);
  fo4::test::end_record(out, tes4);
  const std::pair<const char*, std::vector<std::pair<const char*, std::vector<std::byte>>>> records[] = {
      {"ARMO", {{"EDID", zstr("Armor")}, {"MOD2", zstr("a.nif")}, {"DESC", zstr("Desc")},
                {"DATA", std::vector<std::byte>(12)}}},
      {"PROJ", {{"EDID", zstr("Shot")}, {"OBND", std::vector<std::byte>(12)}, {"DEST", std::vector<std::byte>(8)},
                {"DSTD", std::vector<std::byte>(20)}, {"DSTF", {}}, {"DATA", std::vector<std::byte>(92)},
                {"DNAM", std::vector<std::byte>(4)}, {"NAM1", zstr("m.nif")}, {"VNAM", std::vector<std::byte>(4)}}}};
  std::uint32_t form_id = 0x800;
  for (const auto& [signature, subs] : records) {
    const auto group = fo4::test::begin_group(out, signature, 0);
    const auto record = fo4::test::begin_record(out, signature, 0, form_id++);
    for (const auto& [name, data] : subs) fo4::test::write_subrecord(out, name, data);
    fo4::test::end_record(out, record);
    fo4::test::end_group(out, group);
  }
  return out.bytes;
}

std::vector<std::byte> debr_fixture() {
  fo4::test::ByteWriter out;
  const auto tes4 = fo4::test::begin_record(out, "TES4", 0, 0);
  fo4::test::ByteWriter hedr;
  hedr.f32(1.0f);
  hedr.u32(2);
  hedr.u32(0x900);
  fo4::test::write_subrecord(out, "HEDR", hedr.bytes);
  fo4::test::end_record(out, tes4);
  const std::pair<std::uint32_t, std::vector<std::pair<const char*, std::vector<std::byte>>>> records[] = {
      {0x800, {{"EDID", zstr("Gen")}, {"DATA", std::vector<std::byte>(3)},
               {"MODT", std::vector<std::byte>(4, std::byte{1})}, {"DATA", std::vector<std::byte>(3)},
               {"MODT", std::vector<std::byte>(4, std::byte{2})}}},
      {0x801, {{"EDID", zstr("Two")}, {"DATA", std::vector<std::byte>(3)}, {"DATA", std::vector<std::byte>(3)}}}};
  for (const auto& [form_id, subs] : records) {
    const auto group = fo4::test::begin_group(out, "DEBR", 0);
    const auto record = fo4::test::begin_record(out, "DEBR", 0, form_id);
    for (const auto& [name, data] : subs) fo4::test::write_subrecord(out, name, data);
    fo4::test::end_record(out, record);
    fo4::test::end_group(out, group);
  }
  return out.bytes;
}

std::vector<std::byte> written(radicle_plugin* handle) {
  std::uint8_t* bytes = nullptr;
  std::size_t size = 0;
  EXPECT_EQ(radicle_plugin_write_bytes(handle, &bytes, &size), RADICLE_STATUS_OK);
  std::vector<std::byte> copy(reinterpret_cast<std::byte*>(bytes), reinterpret_cast<std::byte*>(bytes) + size);
  radicle_free_bytes(bytes);
  return copy;
}

radicle_form_key make_armor(Plugin& plugin, const char* editor_id = "NewArmor") {
  radicle_form_key key{};
  EXPECT_EQ(radicle_plugin_create_record(plugin.handle, "ARMO", editor_id, &key), RADICLE_STATUS_OK);
  return key;
}

std::vector<std::byte> lens_fixture() {
  fo4::test::ByteWriter out;
  const auto tes4 = fo4::test::begin_record(out, "TES4", 0, 0);
  fo4::test::ByteWriter hedr;
  hedr.f32(1.0f);
  hedr.u32(1);
  hedr.u32(0x900);
  fo4::test::write_subrecord(out, "HEDR", hedr.bytes);
  fo4::test::end_record(out, tes4);
  const auto group = fo4::test::begin_group(out, "LENS", 0);
  const auto record = fo4::test::begin_record(out, "LENS", 0, 0x800);
  fo4::test::write_subrecord(out, "EDID", zstr("Flare"));
  fo4::test::end_record(out, record);
  fo4::test::end_group(out, group);
  return out.bytes;
}

struct LensFixture {
  std::filesystem::path root;
  radicle_plugin* handle{nullptr};
  LensFixture() {
    root = std::filesystem::temp_directory_path() /
           ("radicle_c_lens_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::create_directories(root);
    const auto bytes = lens_fixture();
    {
      std::ofstream stream(root / "Authoring.esp", std::ios::binary | std::ios::trunc);
      stream.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    }
    EXPECT_EQ(radicle_plugin_read_file((root / "Authoring.esp").string().c_str(), &handle), RADICLE_STATUS_OK)
        << radicle_last_error();
  }
  ~LensFixture() {
    radicle_plugin_destroy(handle);
    std::error_code error;
    std::filesystem::remove_all(root, error);
  }
  LensFixture(const LensFixture&) = delete;
  LensFixture& operator=(const LensFixture&) = delete;
};

std::vector<std::string> lens_order(radicle_plugin* handle) {
  return order(fo4::read_plugin(written(handle), "Authoring.esp"), 0x800);
}

struct ScratchDir {
  std::filesystem::path path;
  explicit ScratchDir(std::filesystem::path value) : path(std::move(value)) {}
  ScratchDir(const ScratchDir&) = delete;
  ScratchDir& operator=(const ScratchDir&) = delete;
  ~ScratchDir() {
    std::error_code error;
    std::filesystem::remove_all(path, error);
  }
};

// Constraint: detects a create that reserves rows but never fills or writes the table, or that mutates the plugin on a row-count mismatch, so a save and reload must resolve the record with the caller's row id and text.
TEST(CApiAuthoring, StringRowCreateFillsAndPersistsTheExternalTable) {
  namespace fs = std::filesystem;
  const auto dir = fs::temp_directory_path() / "radicle_string_rows_c_api";
  const ScratchDir cleanup{dir};
  fs::remove_all(dir);
  fs::create_directories(dir);
  const auto plugin_path = dir / "Localized.esp";

  {
    fo4::test::ByteWriter out;
    // Constraint: TES4 record flag 0x00000080 marks the plugin localized, which is what routes strings externally.
    const auto tes4 = fo4::test::begin_record(out, "TES4", fo4::RecordFlags::localized, 0);
    fo4::test::ByteWriter hedr;
    hedr.f32(1.0f);
    hedr.u32(0);
    hedr.u32(0x900);
    fo4::test::write_subrecord(out, "HEDR", hedr.bytes);
    fo4::test::end_record(out, tes4);
    std::ofstream file(plugin_path, std::ios::binary);
    file.write(reinterpret_cast<const char*>(out.bytes.data()), static_cast<std::streamsize>(out.bytes.size()));
    ASSERT_TRUE(file.good());
  }

  ASSERT_TRUE(fo4::read_plugin_file(plugin_path).header.tes4_flags.is_localized());

  radicle_plugin* handle = nullptr;
  ASSERT_EQ(radicle_plugin_read_file(plugin_path.string().c_str(), &handle), RADICLE_STATUS_OK);
  const char* rows[] = {"Radicle description"};
  radicle_form_key key{};
  const auto created = radicle_plugin_create_record_with_string_rows(handle, "LSCR", "RadicleLSCR", rows, 1,
                                                                    dir.string().c_str(), "en", &key);
  ASSERT_EQ(created, RADICLE_STATUS_OK) << (radicle_last_error() == nullptr ? "" : radicle_last_error());
  ASSERT_NE(key.plugin, nullptr);
  EXPECT_STREQ(key.plugin, "Localized.esp");

  const char* extra[] = {"one", "two"};
  radicle_form_key refused{};
  EXPECT_EQ(radicle_plugin_create_record_with_string_rows(handle, "MESG", "RadicleMESG", extra, 2,
                                                          dir.string().c_str(), "en", &refused),
            RADICLE_STATUS_INVALID_ARGUMENT);
  ASSERT_EQ(radicle_plugin_write_file(handle, plugin_path.string().c_str()), RADICLE_STATUS_OK);
  radicle_plugin_destroy(handle);

  const auto table = fo4::localization::read_string_table_file(
      dir / "Strings" / "Localized_en.STRINGS", fo4::localization::StringTableKind::strings);
  ASSERT_EQ(table.entries.size(), 1u);
  EXPECT_EQ(table.entries[0].id, 1u);
  ASSERT_EQ(table.entries[0].text.size(), 19u);
  EXPECT_EQ(std::string(reinterpret_cast<const char*>(table.entries[0].text.data()), table.entries[0].text.size()),
            "Radicle description");

  const auto reloaded = fo4::read_plugin_file(plugin_path);
  EXPECT_FALSE(order(reloaded, key.local_id).empty());
}

std::filesystem::path write_localized_plugin(const std::filesystem::path& dir) {
  namespace fs = std::filesystem;
  fs::remove_all(dir);
  fs::create_directories(dir / "Strings");
  const auto plugin_path = dir / "Localized.esp";
  fo4::test::ByteWriter out;
  const auto tes4 = fo4::test::begin_record(out, "TES4", fo4::RecordFlags::localized, 0);
  fo4::test::ByteWriter hedr;
  hedr.f32(1.0f);
  hedr.u32(0);
  hedr.u32(0x900);
  fo4::test::write_subrecord(out, "HEDR", hedr.bytes);
  fo4::test::end_record(out, tes4);
  std::ofstream file(plugin_path, std::ios::binary);
  file.write(reinterpret_cast<const char*>(out.bytes.data()), static_cast<std::streamsize>(out.bytes.size()));
  return plugin_path;
}

std::string text_of(const fo4::localization::LocalizedStringTable& table, std::uint32_t id) {
  const auto* entry = fo4::localization::find_string(table, id);
  if (entry == nullptr) return "<missing>";
  return std::string(reinterpret_cast<const char*>(entry->text.data()), entry->text.size());
}

// Constraint: detects a create that writes only its own rows over an existing table file, so earlier strings vanish or the new row reuses an existing id.
TEST(CApiAuthoring, StringRowCreateKeepsStringsAlreadyOnDisk) {
  namespace fs = std::filesystem;
  const auto dir = fs::temp_directory_path() / "radicle_string_rows_existing";
  const ScratchDir cleanup{dir};
  const auto plugin_path = write_localized_plugin(dir);
  using fo4::localization::StringTableKind;
  fo4::localization::LocalizedStringTable strings{StringTableKind::strings, {}};
  fo4::localization::insert_string(strings, 1, "keep one");
  fo4::localization::insert_string(strings, 2, "keep two");
  fo4::localization::insert_string(strings, 5, "keep five");
  fo4::localization::write_string_table_file(strings, dir / "Strings" / "Localized_en.STRINGS");
  fo4::localization::LocalizedStringTable dlstrings{StringTableKind::dlstrings, {}};
  fo4::localization::insert_string(dlstrings, 3, "keep long");
  fo4::localization::write_string_table_file(dlstrings, dir / "Strings" / "Localized_en.DLSTRINGS");

  radicle_plugin* handle = nullptr;
  ASSERT_EQ(radicle_plugin_read_file(plugin_path.string().c_str(), &handle), RADICLE_STATUS_OK);
  const char* rows[] = {"new description"};
  radicle_form_key key{};
  ASSERT_EQ(radicle_plugin_create_record_with_string_rows(handle, "LSCR", "RadicleLSCR", rows, 1,
                                                          dir.string().c_str(), "en", &key),
            RADICLE_STATUS_OK)
      << (radicle_last_error() == nullptr ? "" : radicle_last_error());
  radicle_plugin_destroy(handle);

  const auto after = fo4::localization::read_string_table_file(dir / "Strings" / "Localized_en.STRINGS",
                                                               StringTableKind::strings);
  EXPECT_EQ(after.entries.size(), 4u);
  EXPECT_EQ(text_of(after, 1), "keep one");
  EXPECT_EQ(text_of(after, 2), "keep two");
  EXPECT_EQ(text_of(after, 5), "keep five");
  EXPECT_EQ(text_of(after, 6), "new description");
  const auto long_after = fo4::localization::read_string_table_file(
      dir / "Strings" / "Localized_en.DLSTRINGS", StringTableKind::dlstrings);
  ASSERT_EQ(long_after.entries.size(), 1u);
  EXPECT_EQ(text_of(long_after, 3), "keep long");
}

// Constraint: detects a second create that rebuilds its table from empty, so the first create's row is lost and its id reissued.
TEST(CApiAuthoring, SecondStringRowCreateKeepsTheFirstCreatesRow) {
  namespace fs = std::filesystem;
  const auto dir = fs::temp_directory_path() / "radicle_string_rows_second";
  const ScratchDir cleanup{dir};
  const auto plugin_path = write_localized_plugin(dir);

  radicle_plugin* handle = nullptr;
  ASSERT_EQ(radicle_plugin_read_file(plugin_path.string().c_str(), &handle), RADICLE_STATUS_OK);
  const char* first[] = {"first row"};
  const char* second[] = {"second row"};
  radicle_form_key first_key{};
  radicle_form_key second_key{};
  ASSERT_EQ(radicle_plugin_create_record_with_string_rows(handle, "LSCR", "RadicleFirst", first, 1,
                                                          dir.string().c_str(), "en", &first_key),
            RADICLE_STATUS_OK);
  ASSERT_EQ(radicle_plugin_create_record_with_string_rows(handle, "LSCR", "RadicleSecond", second, 1,
                                                          dir.string().c_str(), "en", &second_key),
            RADICLE_STATUS_OK);
  radicle_plugin_destroy(handle);

  const auto table = fo4::localization::read_string_table_file(dir / "Strings" / "Localized_en.STRINGS",
                                                               fo4::localization::StringTableKind::strings);
  ASSERT_EQ(table.entries.size(), 2u);
  EXPECT_EQ(text_of(table, 1), "first row");
  EXPECT_EQ(text_of(table, 2), "second row");
}

// Constraint: detects a create that replaces an existing string file it could not parse, which would destroy that file.
TEST(CApiAuthoring, StringRowCreateRefusesAnUnreadableExistingTableWithoutTouchingIt) {
  namespace fs = std::filesystem;
  const auto dir = fs::temp_directory_path() / "radicle_string_rows_corrupt";
  const ScratchDir cleanup{dir};
  const auto plugin_path = write_localized_plugin(dir);
  const auto table_path = dir / "Strings" / "Localized_en.STRINGS";
  {
    std::ofstream file(table_path, std::ios::binary);
    file.write("junk", 4);
  }

  radicle_plugin* handle = nullptr;
  ASSERT_EQ(radicle_plugin_read_file(plugin_path.string().c_str(), &handle), RADICLE_STATUS_OK);
  const char* rows[] = {"row"};
  radicle_form_key key{};
  EXPECT_EQ(radicle_plugin_create_record_with_string_rows(handle, "LSCR", "RadicleLSCR", rows, 1,
                                                          dir.string().c_str(), "en", &key),
            RADICLE_STATUS_PARSE_ERROR);
  radicle_plugin_destroy(handle);
  EXPECT_EQ(fs::file_size(table_path), 4u);
  EXPECT_FALSE(fs::exists(dir / "Strings" / "Localized_en.DLSTRINGS"));
}
}  // namespace

TEST(CApiAuthoring, CreatesAddsAndRemovesThroughTheExports) {
  Plugin plugin;
  const auto key = make_armor(plugin);
  ASSERT_NE(key.plugin, nullptr);
  EXPECT_STREQ(key.plugin, "Authoring.esp");
  EXPECT_EQ(key.local_id, 0x800u);

  EXPECT_EQ(radicle_plugin_add_subrecord(plugin.handle, key, "MICO"), RADICLE_STATUS_OK);
  EXPECT_EQ(radicle_plugin_add_subrecord(plugin.handle, key, "ICON"), RADICLE_STATUS_OK);
  EXPECT_EQ(radicle_plugin_add_subrecord(plugin.handle, key, "ICON"), RADICLE_STATUS_OK);
  EXPECT_EQ(order(plugin.snapshot(), 0x800),
            (std::vector<std::string>{"EDID", "ICON", "MICO", "DATA"}));

  EXPECT_EQ(radicle_plugin_remove_subrecord(plugin.handle, key, "MICO", 0), RADICLE_STATUS_OK);
  EXPECT_EQ(radicle_plugin_remove_subrecord(plugin.handle, key, "MICO", 0), RADICLE_STATUS_NOT_FOUND);
  EXPECT_EQ(order(plugin.snapshot(), 0x800), (std::vector<std::string>{"EDID", "ICON", "DATA"}));
}

TEST(CApiAuthoring, MapsEveryRefusalToAStatus) {
  Plugin plugin;
  const auto key = make_armor(plugin);
  const radicle_form_key missing{key.plugin, 0x8FF};

  EXPECT_EQ(radicle_plugin_add_subrecord(plugin.handle, missing, "ICON"), RADICLE_STATUS_NOT_FOUND);
  EXPECT_EQ(radicle_plugin_add_subrecord(plugin.handle, key, "DESC"), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_NE(std::string(radicle_last_error()).find("default"), std::string::npos);
  EXPECT_EQ(radicle_plugin_add_subrecord(plugin.handle, key, "KWDA"), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_plugin_add_subrecord(plugin.handle, key, "ICO"), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_plugin_add_subrecord(plugin.handle, key, nullptr), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_plugin_add_subrecord(nullptr, key, "ICON"), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_plugin_add_subrecord(plugin.handle, radicle_form_key{nullptr, 0}, "ICON"),
            RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_NE(std::strlen(radicle_last_error()), 0u);

  EXPECT_EQ(radicle_plugin_remove_subrecord(plugin.handle, missing, "DESC", 0), RADICLE_STATUS_NOT_FOUND);
  EXPECT_EQ(radicle_plugin_remove_subrecord(plugin.handle, key, "DESC", 0), RADICLE_STATUS_NOT_FOUND);
  EXPECT_EQ(radicle_plugin_remove_subrecord(plugin.handle, key, "DATA", 0),
            RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_plugin_remove_subrecord(plugin.handle, key, "KWDA", 0),
            RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_plugin_remove_subrecord(plugin.handle, key, "XX", 0), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(order(plugin.snapshot(), 0x800), (std::vector<std::string>{"EDID", "DATA"}));
}

TEST(CApiAuthoring, CreateRecordRefusalsLeaveThePluginUntouched) {
  Plugin plugin;
  radicle_form_key key{"x", 9};

  EXPECT_EQ(radicle_plugin_create_record(plugin.handle, "XXXX", "A", &key), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(key.plugin, nullptr);
  EXPECT_EQ(radicle_plugin_create_record(plugin.handle, "CLAS", "A", &key), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_plugin_create_record(plugin.handle, "ARMO", "\xE4\xB8\xAD", &key),
            RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_plugin_create_record(plugin.handle, "ARMO", "\xFF", &key), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_plugin_create_record(plugin.handle, "ARMO", "A", nullptr), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_plugin_create_record(plugin.handle, "ARMO", nullptr, &key), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_plugin_create_record(plugin.handle, "ARM", "A", &key), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_plugin_create_record(nullptr, "ARMO", "A", &key), RADICLE_STATUS_INVALID_ARGUMENT);

  const auto first = make_armor(plugin, "Caf\xC3\xA9");
  EXPECT_EQ(first.local_id, 0x800u);
  EXPECT_EQ(radicle_plugin_create_record(plugin.handle, "LVSP", "List", &key), RADICLE_STATUS_OK);
  EXPECT_EQ(key.local_id, 0x801u);
  const auto reread = plugin.snapshot();
  EXPECT_EQ(order(reread, 0x801), (std::vector<std::string>{"EDID", "OBND", "LVLD", "LVLM", "LVLF", "LLCT"}));
}

// Constraint: detects a generic EYES create that is still refused, or one that cannot take its inline FULL through the EYES Name row, so an editor that moved off the removed EYES create has no way to author a named eye record.
TEST(CApiAuthoring, CreateRecordMakesAnEyesWhoseInlineFullTheNameRowFills) {
  Plugin plugin;
  radicle_form_key key{};
  ASSERT_EQ(radicle_plugin_create_record(plugin.handle, "EYES", "CreatedEyes", &key), RADICLE_STATUS_OK);
  EXPECT_EQ(order(plugin.snapshot(), key.local_id), (std::vector<std::string>{"EDID", "FULL", "ICON", "DATA"}));

  ASSERT_EQ(radicle_plugin_set_record_string(plugin.handle, key, "EYES\\Name", 0, "Caf\xC3\xA9 Eyes"), RADICLE_STATUS_OK);
  ASSERT_EQ(radicle_plugin_set_record_string(plugin.handle, key, "EYES\\Texture", 0, "Textures\\Eyes\\Created.dds"),
            RADICLE_STATUS_OK);
  ASSERT_EQ(radicle_plugin_set_record_scalar_at(plugin.handle, key, "EYES\\Flags", 0, "133"), RADICLE_STATUS_OK);

  const auto reread = plugin.snapshot();
  EXPECT_EQ(order(reread, key.local_id), (std::vector<std::string>{"EDID", "FULL", "ICON", "DATA"}));
  const auto* record = fo4::find_record(reread, fo4::FormKey::parse("000800:Authoring.esp"));
  ASSERT_NE(record, nullptr);
  EXPECT_EQ(record->subrecords[1].data, zstr("Caf\xE9 Eyes"));
  EXPECT_EQ(record->subrecords[2].data, zstr("Textures\\Eyes\\Created.dds"));
  EXPECT_EQ(record->subrecords[3].data, std::vector<std::byte>{std::byte{0x85}});
}

TEST(CApiAuthoring, CoupledSubrecordsAreRefusedWithoutChangingBytes) {
  const auto root = std::filesystem::temp_directory_path() /
                    ("radicle_c_authoring_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
  std::filesystem::create_directories(root);
  const auto path = root / "Authoring.esp";
  {
    const auto bytes = coupled_fixture();
    std::ofstream stream(path, std::ios::binary | std::ios::trunc);
    stream.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
  }
  radicle_plugin* handle = nullptr;
  ASSERT_EQ(radicle_plugin_read_file(path.string().c_str(), &handle), RADICLE_STATUS_OK) << radicle_last_error();
  const auto before = written(handle);

  const radicle_form_key armor{"Authoring.esp", 0x800};
  const radicle_form_key projectile{"Authoring.esp", 0x801};
  const std::pair<radicle_form_key, const char*> coupled[] = {
      {projectile, "MODL"}, {projectile, "DEST"}, {projectile, "DSTD"}};
  for (const auto& [key, signature] : coupled) {
    EXPECT_EQ(radicle_plugin_add_subrecord(handle, key, signature), RADICLE_STATUS_INVALID_ARGUMENT) << signature;
    EXPECT_NE(std::string(radicle_last_error()).find("not authorable"), std::string::npos);
    EXPECT_EQ(radicle_plugin_remove_subrecord(handle, key, signature, 0), RADICLE_STATUS_INVALID_ARGUMENT)
        << signature;
    EXPECT_NE(std::string(radicle_last_error()).find("not authorable"), std::string::npos);
  }
  for (const char* signature : {"MOD2", "MOD4"}) {
    EXPECT_EQ(radicle_plugin_remove_subrecord(handle, armor, signature, 0), RADICLE_STATUS_INVALID_ARGUMENT)
        << signature;
    EXPECT_NE(std::string(radicle_last_error()).find("not authorable"), std::string::npos);
  }
  EXPECT_EQ(radicle_plugin_add_subrecord(handle, armor, "MOD2"), RADICLE_STATUS_OK);
  EXPECT_EQ(written(handle), before);
  EXPECT_EQ(radicle_plugin_add_subrecord(handle, armor, "MOD4"), RADICLE_STATUS_OK);
  EXPECT_EQ(radicle_plugin_remove_subrecord(handle, armor, "DESC", 0), RADICLE_STATUS_OK);
  EXPECT_EQ(order(fo4::read_plugin(written(handle), "Authoring.esp"), 0x800),
            (std::vector<std::string>{"EDID", "MOD2", "MOD4", "DATA"}));

  radicle_plugin_destroy(handle);
  std::error_code error;
  std::filesystem::remove_all(root, error);
}

TEST(CApiAuthoring, UnknownOrderRemovalIsRefusedAsInvalidArgumentWithoutChangingBytes) {
  const auto root = std::filesystem::temp_directory_path() /
                    ("radicle_c_authoring_debr_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
  std::filesystem::create_directories(root);
  const auto path = root / "Authoring.esp";
  {
    const auto bytes = debr_fixture();
    std::ofstream stream(path, std::ios::binary | std::ios::trunc);
    stream.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
  }
  radicle_plugin* handle = nullptr;
  ASSERT_EQ(radicle_plugin_read_file(path.string().c_str(), &handle), RADICLE_STATUS_OK) << radicle_last_error();
  const auto before = written(handle);

  const radicle_form_key generic{"Authoring.esp", 0x800};
  EXPECT_EQ(radicle_plugin_remove_subrecord(handle, generic, "DATA", 1), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_NE(std::string(radicle_last_error()).find("cannot be proven safe"), std::string::npos);
  EXPECT_EQ(written(handle), before);
  EXPECT_EQ(order(fo4::read_plugin(written(handle), "Authoring.esp"), 0x800),
            (std::vector<std::string>{"EDID", "DATA", "MODT", "DATA", "MODT"}));

  const radicle_form_key two{"Authoring.esp", 0x801};
  EXPECT_EQ(radicle_plugin_remove_subrecord(handle, two, "DATA", 1), RADICLE_STATUS_OK);
  EXPECT_EQ(order(fo4::read_plugin(written(handle), "Authoring.esp"), 0x801),
            (std::vector<std::string>{"EDID", "DATA"}));

  radicle_plugin_destroy(handle);
  std::error_code error;
  std::filesystem::remove_all(root, error);
}

TEST(CApiAuthoring, CopiesARecordAsAnOverrideThroughTheExport) {
  Plugin source{"Source.esp"};
  Plugin target{"Target.esp"};
  const auto key = make_armor(source);
  ASSERT_NE(key.plugin, nullptr);

  radicle_record_copy_status status = RADICLE_RECORD_COPY_ALREADY_EXISTS;
  EXPECT_EQ(radicle_plugin_copy_record_override(target.handle, source.handle, key, 0, &status),
            RADICLE_STATUS_OK);
  EXPECT_EQ(status, RADICLE_RECORD_COPY_INSERTED);
  EXPECT_EQ(order(target.snapshot(), 0x800), (std::vector<std::string>{"EDID", "DATA"}));

  EXPECT_EQ(radicle_plugin_copy_record_override(target.handle, source.handle, key, 0, &status),
            RADICLE_STATUS_OK);
  EXPECT_EQ(status, RADICLE_RECORD_COPY_ALREADY_EXISTS);
  EXPECT_EQ(radicle_plugin_copy_record_override(target.handle, source.handle, key, 1, &status),
            RADICLE_STATUS_OK);
  EXPECT_EQ(status, RADICLE_RECORD_COPY_REPLACED);

  const radicle_form_key missing{key.plugin, 0x8FF};
  EXPECT_EQ(radicle_plugin_copy_record_override(target.handle, source.handle, missing, 0, &status),
            RADICLE_STATUS_NOT_FOUND);
  EXPECT_EQ(radicle_plugin_copy_record_override(nullptr, source.handle, key, 0, &status),
            RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_plugin_copy_record_override(target.handle, source.handle, key, 0, nullptr),
            RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(order(target.snapshot(), 0x800), (std::vector<std::string>{"EDID", "DATA"}));
}

std::vector<std::byte> dial_source_fixture() {
  fo4::test::ByteWriter out;
  const auto tes4 = fo4::test::begin_record(out, "TES4", 0, 0);
  fo4::test::ByteWriter hedr;
  hedr.f32(1.0f);
  hedr.u32(0);
  hedr.u32(0x900);
  fo4::test::write_subrecord(out, "HEDR", hedr.bytes);
  fo4::test::end_record(out, tes4);
  const auto top = fo4::test::begin_group(out, "QUST", 0);
  const auto quest = fo4::test::begin_record(out, "QUST", 0, 0x800);
  fo4::test::write_subrecord(out, "EDID", zstr("Quest"));
  fo4::test::end_record(out, quest);
  const auto quest_children = fo4::test::begin_group(out, std::string_view{"\0\x08\0\0", 4}, 10);
  const auto dial = fo4::test::begin_record(out, "DIAL", 0, 0x801);
  fo4::test::write_subrecord(out, "EDID", zstr("Greeting"));
  fo4::test::end_record(out, dial);
  const auto children = fo4::test::begin_group(out, std::string_view{"\x01\x08\0\0", 4}, 7);
  for (const auto& [form_id, name] :
       {std::pair<std::uint32_t, const char*>{0x802, "Hi"}, {0x803, "Bye"}}) {
    const auto info = fo4::test::begin_record(out, "INFO", 0, form_id);
    fo4::test::write_subrecord(out, "EDID", zstr(name));
    fo4::test::end_record(out, info);
  }
  fo4::test::end_group(out, children);
  const auto second = fo4::test::begin_record(out, "DIAL", 0, 0x804);
  fo4::test::write_subrecord(out, "EDID", zstr("Second"));
  fo4::test::end_record(out, second);
  fo4::test::end_group(out, quest_children);
  fo4::test::end_group(out, top);
  return out.bytes;
}

const fo4::Group* quest_children_named(const fo4::Plugin& plugin, std::uint32_t label) {
  for (const auto& group : plugin.groups) {
    if (group.header.group_type != 0) continue;
    for (const auto& child : group.children) {
      const auto* nested = std::get_if<fo4::Group>(&child);
      if (nested != nullptr && nested->header.group_type == 10 &&
          nested->header.label_as_u32() == label) {
        return nested;
      }
    }
  }
  return nullptr;
}

bool group_holds(const fo4::Group& group, std::uint32_t local) {
  for (const auto& child : group.children) {
    const auto* record = std::get_if<fo4::Record>(&child);
    if (record != nullptr && record->form_key.local().value() == local) return true;
  }
  return false;
}

TEST(CApiAuthoring, CopiesARecordDeepThroughTheExport) {
  const auto root = std::filesystem::temp_directory_path() /
                    ("radicle_c_authoring_deep_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
  std::filesystem::create_directories(root);
  const auto path = root / "Source.esp";
  {
    const auto bytes = dial_source_fixture();
    std::ofstream stream(path, std::ios::binary | std::ios::trunc);
    stream.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
  }
  radicle_plugin* source = nullptr;
  ASSERT_EQ(radicle_plugin_read_file(path.string().c_str(), &source), RADICLE_STATUS_OK)
      << radicle_last_error();
  Plugin target{"Target.esp"};

  const radicle_form_key dial{"Source.esp", 0x801};
  radicle_record_copy_status status = RADICLE_RECORD_COPY_ALREADY_EXISTS;
  EXPECT_EQ(radicle_plugin_copy_record_deep(target.handle, source, dial, 0, &status),
            RADICLE_STATUS_OK);
  EXPECT_EQ(status, RADICLE_RECORD_COPY_INSERTED);
  EXPECT_EQ(order(target.snapshot(), 0x800), (std::vector<std::string>{"EDID"}));
  EXPECT_EQ(order(target.snapshot(), 0x801), (std::vector<std::string>{"EDID"}));
  EXPECT_EQ(order(target.snapshot(), 0x802), (std::vector<std::string>{"EDID"}));
  EXPECT_EQ(order(target.snapshot(), 0x803), (std::vector<std::string>{"EDID"}));
  const auto landed = target.snapshot();
  const auto* quest_children = quest_children_named(landed, 0x800);
  ASSERT_NE(quest_children, nullptr);
  EXPECT_TRUE(group_holds(*quest_children, 0x801));
  const fo4::Group* topics = nullptr;
  for (const auto& child : quest_children->children) {
    const auto* nested = std::get_if<fo4::Group>(&child);
    if (nested != nullptr && nested->header.group_type == 7 &&
        nested->header.label_as_u32() == 0x801) {
      topics = nested;
    }
  }
  ASSERT_NE(topics, nullptr);
  EXPECT_TRUE(group_holds(*topics, 0x802));
  EXPECT_TRUE(group_holds(*topics, 0x803));
  const radicle_form_key other{"Source.esp", 0x804};
  EXPECT_EQ(radicle_plugin_copy_record_deep(target.handle, source, other, 0, &status),
            RADICLE_STATUS_OK);
  EXPECT_EQ(status, RADICLE_RECORD_COPY_INSERTED);
  EXPECT_EQ(order(target.snapshot(), 0x804), (std::vector<std::string>{"EDID"}));
  std::size_t quest_groups = 0;
  for (const auto& group : target.snapshot().groups) {
    if (group.header.group_type != 0) continue;
    for (const auto& child : group.children) {
      const auto* nested = std::get_if<fo4::Group>(&child);
      if (nested != nullptr && nested->header.group_type == 10) ++quest_groups;
    }
  }
  EXPECT_EQ(quest_groups, 1u);
  EXPECT_TRUE(group_holds(*quest_children_named(target.snapshot(), 0x800), 0x804));

  const auto before = written(target.handle);
  EXPECT_EQ(radicle_plugin_copy_record_deep(target.handle, source, dial, 0, &status),
            RADICLE_STATUS_OK);
  EXPECT_EQ(status, RADICLE_RECORD_COPY_ALREADY_EXISTS);
  EXPECT_EQ(written(target.handle), before);
  EXPECT_EQ(radicle_plugin_copy_record_deep(target.handle, source, dial, 1, &status),
            RADICLE_STATUS_OK);
  EXPECT_EQ(status, RADICLE_RECORD_COPY_REPLACED);
  EXPECT_EQ(order(target.snapshot(), 0x801), (std::vector<std::string>{"EDID"}));
  const auto after_replace = written(target.handle);

  const radicle_form_key missing{"Source.esp", 0x8FF};
  EXPECT_EQ(radicle_plugin_copy_record_deep(target.handle, source, missing, 0, &status),
            RADICLE_STATUS_NOT_FOUND);
  const radicle_form_key nested{"Source.esp", 0x802};
  EXPECT_EQ(radicle_plugin_copy_record_deep(target.handle, source, nested, 0, &status),
            RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_plugin_copy_record_deep(nullptr, source, dial, 0, &status),
            RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_plugin_copy_record_deep(target.handle, source, dial, 0, nullptr),
            RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(written(target.handle), after_replace);

  radicle_plugin_destroy(source);
  std::error_code error;
  std::filesystem::remove_all(root, error);
}

TEST(CApiAuthoring, RenumbersARecordThroughTheExport) {
  Plugin plugin;
  const auto key = make_armor(plugin);
  ASSERT_NE(key.plugin, nullptr);

  EXPECT_EQ(radicle_plugin_renumber_record(plugin.handle, key, 0x801), RADICLE_STATUS_OK);
  EXPECT_EQ(order(plugin.snapshot(), 0x801), (std::vector<std::string>{"EDID", "DATA"}));
  EXPECT_EQ(order(plugin.snapshot(), 0x800), (std::vector<std::string>{}));
  EXPECT_EQ(radicle_plugin_renumber_record(plugin.handle, {key.plugin, 0x801}, 0x801),
            RADICLE_STATUS_OK);

  const radicle_form_key missing{key.plugin, 0x8FF};
  EXPECT_EQ(radicle_plugin_renumber_record(plugin.handle, missing, 0x802),
            RADICLE_STATUS_NOT_FOUND);
  const auto second = make_armor(plugin, "SecondArmor");
  const auto before = written(plugin.handle);
  EXPECT_EQ(radicle_plugin_renumber_record(plugin.handle, {key.plugin, 0x801}, second.local_id),
            RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_plugin_renumber_record(plugin.handle, {"Other.esp", 0x801}, 0x803),
            RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_plugin_renumber_record(plugin.handle, {key.plugin, 0x801}, 0x000000),
            RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(written(plugin.handle), before);
  EXPECT_EQ(radicle_plugin_renumber_record(nullptr, key, 0x803), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(order(plugin.snapshot(), 0x801), (std::vector<std::string>{"EDID", "DATA"}));
}

TEST(CApiAuthoring, InsertsAndRemovesSubrecordGroupMembersThroughTheExports) {
  LensFixture fixture;
  const radicle_form_key key{"Authoring.esp", 0x800};
  const char* group = "LENS\\Sprites";
  const auto before = lens_order(fixture.handle);

  EXPECT_EQ(radicle_plugin_subrecord_group_insert(fixture.handle, key, group, 0), RADICLE_STATUS_OK);
  const auto inserted = lens_order(fixture.handle);
  EXPECT_GT(inserted.size(), before.size());
  EXPECT_NE(std::find(inserted.begin(), inserted.end(), std::string("LFSP")), inserted.end());
  EXPECT_EQ(radicle_plugin_subrecord_group_insert(fixture.handle, key, group, 1), RADICLE_STATUS_OK);
  EXPECT_GT(lens_order(fixture.handle).size(), inserted.size());
  EXPECT_EQ(radicle_plugin_subrecord_group_insert(fixture.handle, key, group, 3), RADICLE_STATUS_NOT_FOUND);

  EXPECT_EQ(radicle_plugin_subrecord_group_remove(fixture.handle, key, group, 0), RADICLE_STATUS_OK);
  EXPECT_EQ(radicle_plugin_subrecord_group_remove(fixture.handle, key, group, 0), RADICLE_STATUS_OK);
  EXPECT_EQ(radicle_plugin_subrecord_group_remove(fixture.handle, key, group, 0), RADICLE_STATUS_NOT_FOUND);
  EXPECT_EQ(lens_order(fixture.handle), (std::vector<std::string>{"EDID", "LFSP"}));
}

TEST(CApiAuthoring, MapsSubrecordGroupRefusalsToStatuses) {
  LensFixture fixture;
  const radicle_form_key key{"Authoring.esp", 0x800};
  const char* group = "LENS\\Sprites";
  const radicle_form_key missing{"Authoring.esp", 0x8FF};

  EXPECT_EQ(radicle_plugin_subrecord_group_insert(fixture.handle, missing, group, 0), RADICLE_STATUS_NOT_FOUND);
  EXPECT_EQ(radicle_plugin_subrecord_group_insert(fixture.handle, key, "LENS\\Missing", 0),
            RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_plugin_subrecord_group_insert(fixture.handle, key, nullptr, 0),
            RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_plugin_subrecord_group_insert(nullptr, key, group, 0), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_plugin_subrecord_group_remove(fixture.handle, missing, group, 0), RADICLE_STATUS_NOT_FOUND);
  EXPECT_EQ(radicle_plugin_subrecord_group_remove(fixture.handle, key, "LENS\\Missing", 0),
            RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_plugin_subrecord_group_remove(fixture.handle, radicle_form_key{nullptr, 0}, group, 0),
            RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_plugin_subrecord_group_remove(fixture.handle, key, group, 0), RADICLE_STATUS_NOT_FOUND);
  EXPECT_EQ(lens_order(fixture.handle), (std::vector<std::string>{"EDID"}));
}

namespace {

const fo4::Record* record_at(const fo4::Plugin& plugin, std::uint32_t local) {
  for (const auto* record : fo4::collect_records(plugin)) {
    if (record->header.form_id.value() == local) return record;
  }
  return nullptr;
}

std::vector<std::byte> bytes_of(std::initializer_list<unsigned> values) {
  std::vector<std::byte> out;
  for (const auto value : values) out.push_back(static_cast<std::byte>(value));
  return out;
}

radicle_status set_bytes(radicle_plugin* handle, radicle_form_key key, const char* signature, std::size_t occurrence,
                         const std::vector<std::byte>& payload) {
  return radicle_plugin_set_subrecord_bytes(handle, key, signature, occurrence,
                                            reinterpret_cast<const std::uint8_t*>(payload.data()), payload.size());
}

}  // namespace

TEST(CApiAuthoring, RawRecordStoresCallerBytesAppendsAndReplaces) {
  Plugin plugin;
  const auto reference = make_armor(plugin, "Reference");
  const auto reference_version = record_at(plugin.snapshot(), reference.local_id)->header.form_version;

  radicle_form_key key{};
  ASSERT_EQ(radicle_plugin_create_raw_record(plugin.handle, "GLOB", 0, &key), RADICLE_STATUS_OK);
  EXPECT_EQ(key.local_id, 0x801u);

  const auto malformed = bytes_of({0x01, 0x02, 0x03});
  ASSERT_EQ(set_bytes(plugin.handle, key, "FLTV", 0, malformed), RADICLE_STATUS_OK);
  ASSERT_EQ(set_bytes(plugin.handle, key, "EDID", 0, bytes_of({'G', 0})), RADICLE_STATUS_OK);
  ASSERT_EQ(set_bytes(plugin.handle, key, "FLTV", 1, bytes_of({9})), RADICLE_STATUS_OK);
  ASSERT_EQ(set_bytes(plugin.handle, key, "EDID", 0, bytes_of({'G', 'H', 0})), RADICLE_STATUS_OK);
  ASSERT_EQ(set_bytes(plugin.handle, key, "ZERO", 0, {}), RADICLE_STATUS_OK);

  const auto reread = plugin.snapshot();
  const auto* record = record_at(reread, key.local_id);
  ASSERT_NE(record, nullptr);
  EXPECT_EQ(record->header.form_version, reference_version);
  ASSERT_EQ(record->subrecords.size(), 4u);
  EXPECT_EQ(record->subrecords[0].signature.to_string(), "FLTV");
  EXPECT_EQ(record->subrecords[0].data, malformed);
  EXPECT_EQ(record->subrecords[1].signature.to_string(), "EDID");
  EXPECT_EQ(record->subrecords[1].data, bytes_of({'G', 'H', 0}));
  EXPECT_EQ(record->subrecords[2].data, bytes_of({9}));
  EXPECT_TRUE(record->subrecords[3].data.empty());

  std::vector<std::byte> large(70000);
  for (std::size_t index = 0; index < large.size(); ++index) large[index] = static_cast<std::byte>(index * 7);
  ASSERT_EQ(set_bytes(plugin.handle, key, "FLTV", 0, large), RADICLE_STATUS_OK);
  const auto grown = plugin.snapshot();
  const auto* grown_record = record_at(grown, key.local_id);
  ASSERT_NE(grown_record, nullptr);
  ASSERT_EQ(grown_record->subrecords.size(), 5u);
  EXPECT_EQ(grown_record->subrecords[0].signature.to_string(), "XXXX");
  EXPECT_EQ(grown_record->subrecords[0].data, bytes_of({0x70, 0x11, 0x01, 0x00}));
  EXPECT_EQ(grown_record->subrecords[1].signature.to_string(), "FLTV");
  EXPECT_EQ(grown_record->subrecords[1].data, large);

  radicle_form_key versioned{};
  ASSERT_EQ(radicle_plugin_create_raw_record(plugin.handle, "GLOB", 131, &versioned), RADICLE_STATUS_OK);
  EXPECT_EQ(record_at(plugin.snapshot(), versioned.local_id)->header.form_version, 131u);
}

TEST(CApiAuthoring, SetSubrecordBytesRefusalsLeaveTheRecordUntouched) {
  Plugin plugin;
  radicle_form_key key{};
  ASSERT_EQ(radicle_plugin_create_raw_record(plugin.handle, "GLOB", 0, &key), RADICLE_STATUS_OK);
  ASSERT_EQ(set_bytes(plugin.handle, key, "FLTV", 0, bytes_of({1})), RADICLE_STATUS_OK);
  const auto before = written(plugin.handle);

  const radicle_form_key missing{key.plugin, 0x8FF};
  const auto payload = bytes_of({2});
  EXPECT_EQ(set_bytes(plugin.handle, missing, "FLTV", 0, payload), RADICLE_STATUS_NOT_FOUND);
  EXPECT_EQ(set_bytes(plugin.handle, key, "FLTV", 2, payload), RADICLE_STATUS_NOT_FOUND);
  EXPECT_EQ(set_bytes(plugin.handle, key, "FLT", 0, payload), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_plugin_set_subrecord_bytes(plugin.handle, key, nullptr, 0, nullptr, 0),
            RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_plugin_set_subrecord_bytes(plugin.handle, key, "FLTV", 0, nullptr, 4),
            RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(set_bytes(nullptr, key, "FLTV", 0, payload), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_plugin_set_subrecord_bytes(plugin.handle, radicle_form_key{nullptr, 0}, "FLTV", 0, nullptr, 0),
            RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(written(plugin.handle), before);
}

TEST(CApiAuthoring, RawRecordRefusesNestedAndReservedSignaturesWithoutAllocatingAnId) {
  Plugin plugin;
  radicle_form_key key{"x", 9};
  const auto before = written(plugin.handle);

  for (const char* signature : {"CELL", "REFR", "ACHR", "LAND", "NAVM", "INFO", "TES4", "GRUP", "XX", "\x01\x02\x03\x04"}) {
    EXPECT_EQ(radicle_plugin_create_raw_record(plugin.handle, signature, 0, &key), RADICLE_STATUS_INVALID_ARGUMENT)
        << signature;
    EXPECT_EQ(key.plugin, nullptr);
  }
  EXPECT_EQ(radicle_plugin_create_raw_record(plugin.handle, nullptr, 0, &key), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_plugin_create_raw_record(plugin.handle, "GLOB", 0, nullptr), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_plugin_create_raw_record(nullptr, "GLOB", 0, &key), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(written(plugin.handle), before);
  ASSERT_EQ(radicle_plugin_create_raw_record(plugin.handle, "GLOB", 0, &key), RADICLE_STATUS_OK);
  EXPECT_EQ(key.local_id, 0x800u);
}

namespace {

using GroupChain = std::vector<fo4::GroupType>;

void collect_chains(const std::vector<fo4::GroupChild>& children, GroupChain& chain,
                    std::map<std::uint32_t, GroupChain>& out) {
  for (const auto& child : children) {
    if (const auto* record = std::get_if<fo4::Record>(&child)) {
      out[record->header.form_id.value()] = chain;
    } else {
      const auto& group = std::get<fo4::Group>(child);
      chain.push_back(static_cast<fo4::GroupType>(group.header.group_type));
      collect_chains(group.children, chain, out);
      chain.pop_back();
    }
  }
}

std::map<std::uint32_t, GroupChain> chains_of(const fo4::Plugin& plugin) {
  std::map<std::uint32_t, GroupChain> out;
  GroupChain chain;
  for (const auto& group : plugin.groups) {
    chain.push_back(static_cast<fo4::GroupType>(group.header.group_type));
    collect_chains(group.children, chain, out);
    chain.pop_back();
  }
  return out;
}

std::uint32_t le32(const std::vector<std::byte>& bytes, std::size_t at) {
  std::uint32_t value = 0;
  for (int index = 3; index >= 0; --index) value = (value << 8) | std::to_integer<std::uint32_t>(bytes[at + index]);
  return value;
}

}  // namespace

TEST(CApiAuthoring, RawCellChildLandsInTheRequestedGroupWithTheChosenIdAndCallerBytes) {
  Plugin plugin;
  radicle_form_key cell{};
  ASSERT_EQ(radicle_plugin_create_interior_cell(plugin.handle, "FixtureCell", &cell), RADICLE_STATUS_OK);

  radicle_form_key refr{};
  radicle_form_key achr{};
  radicle_form_key land{};
  ASSERT_EQ(radicle_plugin_create_raw_cell_child(plugin.handle, cell, "REFR", 0, 0x900, 0, &refr), RADICLE_STATUS_OK);
  EXPECT_EQ(refr.local_id, 0x900u);
  ASSERT_EQ(radicle_plugin_create_raw_cell_child(plugin.handle, cell, "ACHR", 1, 0, 131, &achr), RADICLE_STATUS_OK);
  EXPECT_EQ(achr.local_id, 0x901u);
  ASSERT_EQ(radicle_plugin_create_raw_cell_child(plugin.handle, cell, "LAND", 0, 0, 0, &land), RADICLE_STATUS_OK);
  EXPECT_EQ(land.local_id, 0x902u);

  const auto malformed = bytes_of({0x01, 0x02, 0x03});
  ASSERT_EQ(set_bytes(plugin.handle, refr, "DATA", 0, malformed), RADICLE_STATUS_OK);
  ASSERT_EQ(set_bytes(plugin.handle, land, "VHGT", 0, bytes_of({7})), RADICLE_STATUS_OK);

  const auto reread = plugin.snapshot();
  const auto chains = chains_of(reread);
  using fo4::GroupType;
  const GroupChain temporary{GroupType::top, GroupType::interior_cell_block, GroupType::interior_cell_subblock,
                             GroupType::cell_children, GroupType::cell_temporary_children};
  GroupChain persistent = temporary;
  persistent.back() = GroupType::cell_persistent_children;
  EXPECT_EQ(chains.at(0x900), temporary);
  EXPECT_EQ(chains.at(0x901), persistent);
  EXPECT_EQ(chains.at(0x902), temporary);
  EXPECT_EQ(record_at(reread, 0x900)->subrecords[0].data, malformed);
  EXPECT_EQ(record_at(reread, 0x900)->header.signature.to_string(), "REFR");
  EXPECT_EQ(record_at(reread, 0x901)->header.form_version, 131u);
  EXPECT_EQ(record_at(reread, 0x902)->header.signature.to_string(), "LAND");

  radicle_form_key next{};
  ASSERT_EQ(radicle_plugin_create_raw_record(plugin.handle, "GLOB", 0, &next), RADICLE_STATUS_OK);
  EXPECT_EQ(next.local_id, 0x903u);
}

TEST(CApiAuthoring, RawCellChildRefusalsLeaveThePluginUntouched) {
  Plugin plugin;
  radicle_form_key cell{};
  ASSERT_EQ(radicle_plugin_create_interior_cell(plugin.handle, "FixtureCell", &cell), RADICLE_STATUS_OK);
  radicle_form_key glob{};
  ASSERT_EQ(radicle_plugin_create_raw_record(plugin.handle, "GLOB", 0, &glob), RADICLE_STATUS_OK);
  radicle_form_key placed{};
  ASSERT_EQ(radicle_plugin_create_raw_cell_child(plugin.handle, cell, "REFR", 0, 0x950, 0, &placed), RADICLE_STATUS_OK);
  const auto before = written(plugin.handle);

  radicle_form_key key{"x", 9};
  const radicle_form_key missing{cell.plugin, 0x8FF};
  EXPECT_EQ(radicle_plugin_create_raw_cell_child(plugin.handle, cell, "REFR", 0, 0x950, 0, &key), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(key.plugin, nullptr);
  EXPECT_EQ(radicle_plugin_create_raw_cell_child(plugin.handle, cell, "REFR", 0, 0x7FF, 0, &key), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_plugin_create_raw_cell_child(plugin.handle, cell, "REFR", 0, 0x1000000, 0, &key), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_plugin_create_raw_cell_child(plugin.handle, cell, "LAND", 1, 0, 0, &key), RADICLE_STATUS_INVALID_ARGUMENT);
  for (const char* signature : {"NPC_", "CELL", "NAVM", "GRUP", "REF", "TES4"}) {
    EXPECT_EQ(radicle_plugin_create_raw_cell_child(plugin.handle, cell, signature, 0, 0, 0, &key),
              RADICLE_STATUS_INVALID_ARGUMENT) << signature;
  }
  EXPECT_EQ(radicle_plugin_create_raw_cell_child(plugin.handle, glob, "REFR", 0, 0, 0, &key), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_plugin_create_raw_cell_child(plugin.handle, missing, "REFR", 0, 0, 0, &key), RADICLE_STATUS_NOT_FOUND);
  EXPECT_EQ(radicle_plugin_create_raw_cell_child(plugin.handle, cell, nullptr, 0, 0, 0, &key), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_plugin_create_raw_cell_child(plugin.handle, cell, "REFR", 0, 0, 0, nullptr), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_plugin_create_raw_cell_child(nullptr, cell, "REFR", 0, 0, 0, &key), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(written(plugin.handle), before);
}

TEST(CApiAuthoring, ExteriorFixtureNestsPersistentAndGridCellsWithTheirChildrenUnderTheWorldspace) {
  Plugin plugin;
  radicle_form_key world{};
  ASSERT_EQ(radicle_plugin_create_raw_record(plugin.handle, "WRLD", 0, &world), RADICLE_STATUS_OK);
  radicle_form_key top{};
  ASSERT_EQ(radicle_plugin_create_worldspace_persistent_cell(plugin.handle, world, 0, &top), RADICLE_STATUS_OK);
  const radicle_exterior_cell_creation creation{world, 3, -2, "Grid", 0};
  radicle_form_key grid{};
  ASSERT_EQ(radicle_plugin_create_exterior_cell(plugin.handle, &creation, &grid), RADICLE_STATUS_OK);

  radicle_form_key persistent_ref{};
  radicle_form_key land{};
  ASSERT_EQ(radicle_plugin_create_raw_cell_child(plugin.handle, top, "REFR", 1, 0xA00, 0, &persistent_ref), RADICLE_STATUS_OK);
  ASSERT_EQ(radicle_plugin_create_raw_cell_child(plugin.handle, grid, "LAND", 0, 0xA01, 0, &land), RADICLE_STATUS_OK);

  const auto before = written(plugin.handle);
  radicle_form_key again{"x", 9};
  EXPECT_EQ(radicle_plugin_create_worldspace_persistent_cell(plugin.handle, world, 0, &again), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(again.plugin, nullptr);
  EXPECT_EQ(radicle_plugin_create_worldspace_persistent_cell(plugin.handle, world, 1, &again), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_plugin_create_worldspace_persistent_cell(plugin.handle, grid, 0, &again), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(written(plugin.handle), before);

  const auto reread = plugin.snapshot();
  const auto chains = chains_of(reread);
  using fo4::GroupType;
  EXPECT_EQ(chains.at(top.local_id), (GroupChain{GroupType::top, GroupType::world_children}));
  EXPECT_EQ(chains.at(0xA00), (GroupChain{GroupType::top, GroupType::world_children, GroupType::cell_children,
                                         GroupType::cell_persistent_children}));
  EXPECT_EQ(chains.at(grid.local_id), (GroupChain{GroupType::top, GroupType::world_children,
                                                  GroupType::exterior_cell_block, GroupType::exterior_cell_subblock}));
  EXPECT_EQ(chains.at(0xA01), (GroupChain{GroupType::top, GroupType::world_children, GroupType::exterior_cell_block,
                                         GroupType::exterior_cell_subblock, GroupType::cell_children,
                                         GroupType::cell_temporary_children}));
  const auto* persistent_cell = record_at(reread, top.local_id);
  ASSERT_NE(persistent_cell, nullptr);
  EXPECT_EQ(persistent_cell->subrecords[0].signature.to_string(), "DATA");
}

TEST(CApiAuthoring, RawCompressedPayloadIsWrittenVerbatimAndSurvivesALaterEdit) {
  Plugin plugin;
  radicle_form_key cell{};
  ASSERT_EQ(radicle_plugin_create_interior_cell(plugin.handle, "FixtureCell", &cell), RADICLE_STATUS_OK);
  radicle_form_key refr{};
  ASSERT_EQ(radicle_plugin_create_raw_cell_child(plugin.handle, cell, "REFR", 0, 0x900, 0, &refr), RADICLE_STATUS_OK);
  ASSERT_EQ(set_bytes(plugin.handle, refr, "NAME", 0, bytes_of({1, 2, 3, 4})), RADICLE_STATUS_OK);

  const auto broken = bytes_of({'N', 'O', 'T', 'Z', 'L', 'I', 'B', 0xFF, 0xEE});
  ASSERT_EQ(radicle_plugin_set_record_raw_compressed_payload(
                plugin.handle, refr, 77, reinterpret_cast<const std::uint8_t*>(broken.data()), broken.size()),
            RADICLE_STATUS_OK);

  const auto bytes = written(plugin.handle);
  const std::array<std::byte, 4> signature{std::byte{'R'}, std::byte{'E'}, std::byte{'F'}, std::byte{'R'}};
  const auto found = std::search(bytes.begin(), bytes.end(), signature.begin(), signature.end());
  ASSERT_NE(found, bytes.end());
  const auto at = static_cast<std::size_t>(found - bytes.begin());
  EXPECT_EQ(le32(bytes, at + 4), 4u + broken.size());
  EXPECT_NE(le32(bytes, at + 8) & 0x00040000u, 0u);
  EXPECT_EQ(le32(bytes, at + 12), 0x900u);
  EXPECT_EQ(le32(bytes, at + 24), 77u);
  EXPECT_TRUE(std::equal(broken.begin(), broken.end(), bytes.begin() + static_cast<std::ptrdiff_t>(at + 28)));

  ASSERT_EQ(set_bytes(plugin.handle, refr, "XSCL", 0, bytes_of({5})), RADICLE_STATUS_OK);
  EXPECT_EQ(written(plugin.handle), bytes);

  const auto before = written(plugin.handle);
  const radicle_form_key missing{refr.plugin, 0x8FF};
  EXPECT_EQ(radicle_plugin_set_record_raw_compressed_payload(plugin.handle, missing, 1, nullptr, 0), RADICLE_STATUS_NOT_FOUND);
  EXPECT_EQ(radicle_plugin_set_record_raw_compressed_payload(plugin.handle, refr, 1, nullptr, 4), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_plugin_set_record_raw_compressed_payload(nullptr, refr, 1, nullptr, 0), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(written(plugin.handle), before);
}
