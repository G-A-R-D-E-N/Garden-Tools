#include "fo4/archive/assets.hpp"
#include "fo4/archive/ba2.hpp"
#include "fo4/c_api_env.h"
#include "fo4/environment/strings.hpp"
#include "fo4/localization/strings.hpp"
#include "plugin_bytes.hpp"

#include <gtest/gtest.h>

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {

struct Sub {
  std::string signature;
  std::vector<std::byte> data;
};

struct Rec {
  std::string signature;
  std::uint32_t form_id{0};
  std::vector<Sub> subrecords;
};

std::vector<std::byte> bytes_of(std::string_view text) {
  std::vector<std::byte> out;
  for (const char ch : text) out.push_back(std::byte{static_cast<std::uint8_t>(ch)});
  return out;
}

std::vector<std::byte> zstring(std::string_view text) {
  auto out = bytes_of(text);
  out.push_back(std::byte{0});
  return out;
}

std::vector<std::byte> u32_bytes(std::uint32_t value) {
  fo4::test::ByteWriter out;
  out.u32(value);
  return out.bytes;
}

std::vector<std::byte> make_plugin(std::uint32_t tes4_flags,
                                   const std::vector<std::string>& masters,
                                   const std::vector<Rec>& records) {
  fo4::test::ByteWriter out;
  const auto tes4 = fo4::test::begin_record(out, "TES4", tes4_flags, 0);
  fo4::test::ByteWriter hedr;
  hedr.f32(1.0f);
  hedr.u32(static_cast<std::uint32_t>(records.size()));
  hedr.u32(0x900);
  fo4::test::write_subrecord(out, "HEDR", hedr.bytes);
  for (const auto& master : masters) {
    fo4::test::write_subrecord(out, "MAST", zstring(master));
    fo4::test::ByteWriter data;
    data.u32(0);
    data.u32(0);
    fo4::test::write_subrecord(out, "DATA", data.bytes);
  }
  fo4::test::end_record(out, tes4);
  for (const auto& record : records) {
    const auto group = fo4::test::begin_group(out, record.signature, 0);
    const auto header = fo4::test::begin_record(out, record.signature, 0, record.form_id);
    for (const auto& sub : record.subrecords) fo4::test::write_subrecord(out, sub.signature, sub.data);
    fo4::test::end_record(out, header);
    fo4::test::end_group(out, group);
  }
  return out.bytes;
}

fo4::localization::LocalizedStringTable table(fo4::localization::StringTableKind kind,
                                              std::vector<std::pair<std::uint32_t, std::string>> rows) {
  fo4::localization::LocalizedStringTable out;
  out.kind = kind;
  for (auto& [id, text] : rows) out.entries.push_back({id, bytes_of(text)});
  return out;
}

class StringsEnvironmentTest : public ::testing::Test {
protected:
  void SetUp() override {
    const auto id = std::chrono::steady_clock::now().time_since_epoch().count();
    root_ = std::filesystem::temp_directory_path() / ("radicle_strings_" + std::to_string(id));
    strings_ = root_ / "Strings";
    std::filesystem::create_directories(strings_);
    paths_ = {
        write("Base.esm", make_plugin(0x81, {},
                                      {{"WRLD", 0x800, {{"FULL", u32_bytes(0x10)}}},
                                       {"NPC_", 0x801, {{"FULL", u32_bytes(0x99)}}},
                                       {"ARMO", 0x802, {{"FULL", u32_bytes(0x10)}, {"DESC", u32_bytes(0x20)}}},
                                       {"STAT", 0x803, {{"FULL", u32_bytes(0x1)}, {"EDID", zstring("Bad")}}}})),
        write("Patch.esp", make_plugin(0x80, {"Base.esm"}, {{"WRLD", 0x800, {{"FULL", u32_bytes(0x11)}}}})),
        write("Inline.esp", make_plugin(0, {"Base.esm"},
                                        {{"NPC_", 0x801, {{"FULL", zstring("Inline Name")}}},
                                         {"STAT", 0x01000900, {{"EDID", zstring("NoName")}}}})),
    };
    using fo4::localization::StringTableKind;
    fo4::localization::write_string_table_file(
        table(StringTableKind::strings, {{0x10, "Commonwealth"}}), strings_ / "base_EN.strings");
    fo4::localization::write_string_table_file(
        table(StringTableKind::dlstrings, {{0x20, "Caf\xE9 armor"}}), strings_ / "Base_en.DLSTRINGS");
    fo4::localization::write_string_table_file(
        table(StringTableKind::strings, {{0x10, "Commonwealth (de)"}}), strings_ / "Base_de.STRINGS");
    fo4::localization::write_string_table_file(
        table(StringTableKind::strings, {{0x11, "Patched Commonwealth"}}), strings_ / "Patch_en.STRINGS");
  }

  void TearDown() override {
    std::error_code ec;
    std::filesystem::remove_all(root_, ec);
  }

  std::filesystem::path write(std::string_view name, const std::vector<std::byte>& bytes) {
    const auto path = root_ / std::string(name);
    std::ofstream stream(path, std::ios::binary | std::ios::trunc);
    stream.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    stream.close();
    if (!stream) throw std::runtime_error("cannot write " + path.string());
    return path;
  }

  std::filesystem::path root_;
  std::filesystem::path strings_;
  std::vector<std::filesystem::path> paths_;
};

fo4::FormKey key(std::string_view plugin, std::uint32_t local) {
  return fo4::FormKey{fo4::PluginKey::from_filename(plugin), fo4::LocalFormId{local}};
}

fo4::Signature sig(std::string_view text) { return fo4::Signature::from_string(text); }

}  // namespace

TEST(StringSource, FollowsXEditAndMutagenFieldRules) {
  using fo4::env::StringSource;
  EXPECT_EQ(fo4::env::string_source(sig("ARMO"), sig("DESC")), StringSource::dl);
  EXPECT_EQ(fo4::env::string_source(sig("LSCR"), sig("DESC")), StringSource::normal);
  EXPECT_EQ(fo4::env::string_source(sig("QUST"), sig("CNAM")), StringSource::dl);
  EXPECT_EQ(fo4::env::string_source(sig("BOOK"), sig("CNAM")), StringSource::dl);
  EXPECT_EQ(fo4::env::string_source(sig("NPC_"), sig("CNAM")), StringSource::normal);
  EXPECT_EQ(fo4::env::string_source(sig("INFO"), sig("NAM1")), StringSource::il);
  EXPECT_EQ(fo4::env::string_source(sig("INFO"), sig("RNAM")), StringSource::normal);
  EXPECT_EQ(fo4::env::string_source(sig("WRLD"), sig("FULL")), StringSource::normal);
  EXPECT_EQ(fo4::env::string_source_extension(StringSource::il), "ILSTRINGS");
}

TEST_F(StringsEnvironmentTest, LoadsTablesPerPluginAndLanguage) {
  const auto env = fo4::env::Environment::load(paths_);
  const auto tables = fo4::env::StringTables::load(env, strings_, "en");
  using fo4::env::StringSource;
  ASSERT_NE(tables.table_path(0, StringSource::normal), nullptr);
  EXPECT_EQ(tables.table_path(0, StringSource::normal)->filename(), "base_EN.strings");
  EXPECT_NE(tables.table_path(0, StringSource::dl), nullptr);
  EXPECT_EQ(tables.table_path(0, StringSource::il), nullptr);
  EXPECT_EQ(tables.table_path(2, StringSource::normal), nullptr);
  EXPECT_EQ(tables.table_path(9, StringSource::normal), nullptr);

  EXPECT_EQ(tables.lookup(0, StringSource::normal, 0x10), std::optional<std::string_view>{"Commonwealth"});
  EXPECT_EQ(tables.lookup(0, StringSource::dl, 0x20), std::optional<std::string_view>{"Caf\xE9 armor"});
  EXPECT_FALSE(tables.lookup(0, StringSource::dl, 0x10).has_value());
  EXPECT_FALSE(tables.lookup(1, StringSource::normal, 0x10).has_value());

  const auto german = fo4::env::StringTables::load(env, strings_, "de");
  EXPECT_EQ(german.lookup(0, StringSource::normal, 0x10), std::optional<std::string_view>{"Commonwealth (de)"});
  EXPECT_FALSE(german.lookup(1, StringSource::normal, 0x11).has_value());
}

TEST_F(StringsEnvironmentTest, ResolvesNamesInTheOwningPluginsTable) {
  const auto env = fo4::env::Environment::load(paths_);
  const auto tables = fo4::env::StringTables::load(env, strings_, "en");
  const auto world = env.versions(key("Base.esm", 0x800));
  ASSERT_EQ(world.size(), 2u);
  EXPECT_EQ(tables.resolve_name(env, world[0]), std::optional<std::string_view>{"Commonwealth"});
  EXPECT_EQ(tables.resolve_name(env, world[1]), std::optional<std::string_view>{"Patched Commonwealth"});
  EXPECT_EQ(tables.resolve_name(env, *env.winner(key("Base.esm", 0x801))),
            std::optional<std::string_view>{"Inline Name"});
  EXPECT_FALSE(tables.resolve_name(env, env.versions(key("Base.esm", 0x801))[0]).has_value());
  EXPECT_FALSE(tables.resolve_name(env, *env.winner(key("Base.esm", 0x803))).has_value());
  EXPECT_FALSE(tables.resolve_name(env, *env.winner(key("Inline.esp", 0x900))).has_value());
}

TEST_F(StringsEnvironmentTest, PluginTablesResolveLooseThenArchivesAndSkipNonLocalized) {
  const auto env = fo4::env::Environment::load(paths_);
  const std::vector<std::filesystem::path> dirs{strings_};

  const auto base = fo4::env::load_plugin_string_tables(dirs, env.plugin(0), "en");
  ASSERT_EQ(base.size(), 2u);
  EXPECT_EQ(base[0].source, fo4::env::StringSource::normal);
  EXPECT_TRUE(base[0].archive.empty());
  EXPECT_EQ(base[0].path.filename(), "base_EN.strings");
  EXPECT_EQ(base[1].source, fo4::env::StringSource::dl);

  fo4::archive::Ba2GeneralEntry entry;
  entry.path = "Strings\\Patch_fr.STRINGS";
  entry.chunks.push_back(fo4::archive::make_stored_ba2_chunk(fo4::localization::write_string_table(
      table(fo4::localization::StringTableKind::strings, {{0x11, "Patched French"}}))));
  fo4::archive::Ba2GeneralArchive archive;
  archive.entries.push_back(std::move(entry));
  fo4::archive::write_ba2_general_file(archive, root_ / "Patch - Main.ba2");
  const std::vector<std::filesystem::path> archives{root_ / "Patch - Main.ba2"};
  const auto assets = fo4::archive::AssetResolver::open(root_, archives);

  const auto french = fo4::env::load_plugin_string_tables(dirs, env.plugin(1), "fr", &assets);
  ASSERT_EQ(french.size(), 1u);
  EXPECT_EQ(french[0].archive, "Patch - Main.ba2");
  EXPECT_EQ(french[0].path.filename(), "Patch_fr.STRINGS");
  EXPECT_EQ(french[0].table.entries.size(), 1u);
  EXPECT_TRUE(fo4::env::load_plugin_string_tables(dirs, env.plugin(1), "fr").empty());

  fo4::localization::write_string_table_file(
      table(fo4::localization::StringTableKind::strings, {{0x10, "Inline"}}), strings_ / "Inline_en.STRINGS");
  EXPECT_TRUE(fo4::env::load_plugin_string_tables(dirs, env.plugin(2), "en").empty());
}

// Detects: allocation snapshots must retain the exact bytes parsed from loose and archive-backed tables when either source later changes.
TEST_F(StringsEnvironmentTest, PluginTableSnapshotsRetainParsedLooseAndArchiveBytes) {
  const auto env = fo4::env::Environment::load(paths_);
  const auto archived_bytes = fo4::localization::write_string_table(
      table(fo4::localization::StringTableKind::ilstrings, {{0x30, "Archive original"}}));
  fo4::archive::Ba2GeneralEntry entry;
  entry.path = "Strings\\Base_en.ILSTRINGS";
  entry.chunks.push_back(fo4::archive::make_stored_ba2_chunk(archived_bytes));
  fo4::archive::Ba2GeneralArchive archive;
  archive.entries.push_back(std::move(entry));
  const auto archive_path = root_ / "Base - Main.ba2";
  fo4::archive::write_ba2_general_file(archive, archive_path);
  const std::vector<std::filesystem::path> archives{archive_path};
  const auto assets = fo4::archive::AssetResolver::open(root_, archives);
  const std::vector<std::filesystem::path> dirs{strings_};
  const auto snapshots = fo4::env::load_plugin_string_table_snapshots(dirs, env.plugin(0), "en", &assets);
  ASSERT_EQ(snapshots.size(), 3u);
  const auto original_loose = snapshots[0].bytes;
  EXPECT_EQ(fo4::localization::read_string_table(original_loose, fo4::localization::StringTableKind::strings)
                .entries[0].text, bytes_of("Commonwealth"));
  EXPECT_EQ(snapshots[2].bytes, archived_bytes);
  EXPECT_EQ(snapshots[2].resolved.archive, "Base - Main.ba2");
  fo4::localization::write_string_table_file(
      table(fo4::localization::StringTableKind::strings, {{0x10, "Changed"}}), strings_ / "base_EN.strings");
  EXPECT_EQ(snapshots[0].bytes, original_loose);
  EXPECT_EQ(snapshots[0].resolved.table.entries[0].text, bytes_of("Commonwealth"));
}

TEST_F(StringsEnvironmentTest, PluginTablesPreferTheEarliestDirectoryAndFallBackToLaterOnes) {
  const auto env = fo4::env::Environment::load(paths_);
  const auto second = root_ / "Strings2";
  std::filesystem::create_directories(second);
  using fo4::localization::StringTableKind;
  fo4::localization::write_string_table_file(table(StringTableKind::strings, {{0x10, "Second"}}), second / "Base_en.STRINGS");
  fo4::localization::write_string_table_file(table(StringTableKind::ilstrings, {{0x30, "Only second"}}),
                                             second / "Base_en.ILSTRINGS");

  const std::vector<std::filesystem::path> strings_first{strings_, second};
  const auto first = fo4::env::load_plugin_string_tables(strings_first, env.plugin(0), "en");
  ASSERT_EQ(first.size(), 3u);
  EXPECT_EQ(first[0].source, fo4::env::StringSource::normal);
  EXPECT_EQ(first[0].path.parent_path(), strings_);
  EXPECT_EQ(first[0].table.entries[0].text, bytes_of("Commonwealth"));
  EXPECT_EQ(first[1].source, fo4::env::StringSource::dl);
  EXPECT_EQ(first[1].path.parent_path(), strings_);
  EXPECT_EQ(first[2].source, fo4::env::StringSource::il);
  EXPECT_EQ(first[2].path.parent_path(), second);

  const std::vector<std::filesystem::path> second_first{second, strings_};
  const auto reversed = fo4::env::load_plugin_string_tables(second_first, env.plugin(0), "en");
  ASSERT_EQ(reversed.size(), 3u);
  EXPECT_EQ(reversed[0].path.parent_path(), second);
  EXPECT_EQ(reversed[0].table.entries[0].text, bytes_of("Second"));
  EXPECT_EQ(reversed[1].path.parent_path(), strings_);
  EXPECT_EQ(reversed[2].path.parent_path(), second);
}

TEST_F(StringsEnvironmentTest, LooseTableShadowsAnArchiveCopyOfTheSameName) {
  const auto env = fo4::env::Environment::load(paths_);
  fo4::archive::Ba2GeneralEntry entry;
  entry.path = "Strings\\Base_en.STRINGS";
  entry.chunks.push_back(fo4::archive::make_stored_ba2_chunk(fo4::localization::write_string_table(
      table(fo4::localization::StringTableKind::strings, {{0x10, "Archived"}}))));
  fo4::archive::Ba2GeneralArchive archive;
  archive.entries.push_back(std::move(entry));
  fo4::archive::write_ba2_general_file(archive, root_ / "Shadow - Main.ba2");
  const std::vector<std::filesystem::path> archives{root_ / "Shadow - Main.ba2"};
  const auto assets = fo4::archive::AssetResolver::open(root_, archives);

  const std::vector<std::filesystem::path> loose_dirs{strings_};
  const auto loose = fo4::env::load_plugin_string_tables(loose_dirs, env.plugin(0), "en", &assets);
  ASSERT_FALSE(loose.empty());
  EXPECT_TRUE(loose[0].archive.empty());
  EXPECT_EQ(loose[0].path.filename(), "base_EN.strings");
  EXPECT_EQ(loose[0].table.entries[0].text, bytes_of("Commonwealth"));

  const std::vector<std::filesystem::path> none;
  const auto archived = fo4::env::load_plugin_string_tables(none, env.plugin(0), "en", &assets);
  ASSERT_EQ(archived.size(), 1u);
  EXPECT_EQ(archived[0].archive, "Shadow - Main.ba2");
  EXPECT_EQ(archived[0].table.entries[0].text, bytes_of("Archived"));
}

TEST_F(StringsEnvironmentTest, MissingDirectoryYieldsNoTables) {
  const auto env = fo4::env::Environment::load(paths_);
  const auto tables = fo4::env::StringTables::load(env, root_ / "Nope", "en");
  EXPECT_EQ(tables.table_path(0, fo4::env::StringSource::normal), nullptr);
  EXPECT_FALSE(tables.resolve_name(env, env.versions(key("Base.esm", 0x800))[0]).has_value());
}

TEST_F(StringsEnvironmentTest, RejectsMalformedTablesAndLanguages) {
  const auto env = fo4::env::Environment::load(paths_);
  EXPECT_THROW(fo4::env::StringTables::load(env, strings_, ""), fo4::env::EnvironmentError);
  EXPECT_THROW(fo4::env::StringTables::load(env, strings_, "../en"), fo4::env::EnvironmentError);
  write("Strings/Base_en.ILSTRINGS", {std::byte{1}, std::byte{0}});
  EXPECT_THROW(fo4::env::StringTables::load(env, strings_, "en"), fo4::env::EnvironmentError);
  EXPECT_NO_THROW(fo4::env::StringTables::load(env, strings_, "de"));
}

TEST_F(StringsEnvironmentTest, PluginTablesRejectBadLanguagesAndMalformedTables) {
  const auto env = fo4::env::Environment::load(paths_);
  const std::vector<std::filesystem::path> dirs{strings_};
  for (const auto* language : {"", "../en", "e n"}) {
    EXPECT_THROW(fo4::env::load_plugin_string_tables(dirs, env.plugin(0), language), fo4::env::EnvironmentError) << language;
  }
  write("Strings/Base_en.ILSTRINGS", {std::byte{1}, std::byte{0}});
  try {
    fo4::env::load_plugin_string_tables(dirs, env.plugin(0), "en");
    ADD_FAILURE() << "malformed table loaded";
  } catch (const fo4::env::EnvironmentError& error) {
    EXPECT_NE(std::string_view(error.what()).find("malformed string table"), std::string_view::npos);
  }
  EXPECT_NO_THROW(fo4::env::load_plugin_string_tables(dirs, env.plugin(0), "de"));
}

TEST_F(StringsEnvironmentTest, HandlesNonAnsiFileNames) {
  const auto env = fo4::env::Environment::load(paths_);
  const std::u16string japanese{0x30C9, 0x30AD, 0x30E5, 0x30E1, 0x30F3, 0x30C8, '.', 't', 'x', 't'};
  std::ofstream(strings_ / std::filesystem::path(japanese)) << "x";
  const auto tables = fo4::env::StringTables::load(env, strings_, "en");
  EXPECT_EQ(tables.lookup(0, fo4::env::StringSource::normal, 0x10),
            std::optional<std::string_view>{"Commonwealth"});

  const auto cyrillic = root_ / std::filesystem::path(std::u16string{0x0421, 0x0442, 0x0440, 0x043E, 0x043A, 0x0438});
  std::filesystem::create_directories(cyrillic);
  std::ofstream(cyrillic / "Base_en.STRINGS", std::ios::binary) << '\x01';
  try {
    fo4::env::StringTables::load(env, cyrillic, "en");
    ADD_FAILURE() << "malformed table loaded";
  } catch (const fo4::env::EnvironmentError& error) {
    EXPECT_NE(std::string_view(error.what()).find("\xD0\xA1\xD1\x82\xD1\x80\xD0\xBE\xD0\xBA\xD0\xB8"),
              std::string_view::npos);
  }
}

TEST_F(StringsEnvironmentTest, CApiResolvesStrings) {
  std::vector<std::string> utf8;
  for (const auto& path : paths_) utf8.push_back(path.string());
  std::vector<const char*> raw;
  for (const auto& path : utf8) raw.push_back(path.c_str());
  radicle_env* env = nullptr;
  ASSERT_EQ(radicle_env_load(raw.data(), raw.size(), &env), RADICLE_STATUS_OK);

  radicle_string_source source{};
  EXPECT_EQ(radicle_string_source_for("INFO", "NAM1", &source), RADICLE_STATUS_OK);
  EXPECT_EQ(source, RADICLE_STRING_SOURCE_IL);
  EXPECT_EQ(radicle_string_source_for("INFO", "NAM", &source), RADICLE_STATUS_INVALID_ARGUMENT);

  radicle_strings* strings = nullptr;
  EXPECT_EQ(radicle_strings_load(env, strings_.string().c_str(), "e/n", &strings), RADICLE_STATUS_PARSE_ERROR);
  EXPECT_EQ(strings, nullptr);
  ASSERT_EQ(radicle_strings_load(env, strings_.string().c_str(), "en", &strings), RADICLE_STATUS_OK);

  const char* path = nullptr;
  ASSERT_EQ(radicle_strings_table_path(strings, 0, RADICLE_STRING_SOURCE_DL, &path), RADICLE_STATUS_OK);
  EXPECT_EQ(std::filesystem::path(path).filename(), "Base_en.DLSTRINGS");
  EXPECT_EQ(radicle_strings_table_path(strings, 2, RADICLE_STRING_SOURCE_NORMAL, &path), RADICLE_STATUS_NOT_FOUND);
  EXPECT_EQ(path, nullptr);
  EXPECT_EQ(radicle_strings_table_path(strings, 3, RADICLE_STRING_SOURCE_NORMAL, &path),
            RADICLE_STATUS_INVALID_ARGUMENT);

  const char* text = nullptr;
  size_t size = 0;
  ASSERT_EQ(radicle_strings_lookup(strings, 0, RADICLE_STRING_SOURCE_DL, 0x20, &text, &size), RADICLE_STATUS_OK);
  EXPECT_EQ(std::string_view(text, size), "Caf\xE9 armor");
  EXPECT_EQ(text[size], '\0');
  EXPECT_EQ(radicle_strings_lookup(strings, 0, RADICLE_STRING_SOURCE_NORMAL, 0x99, &text, &size),
            RADICLE_STATUS_NOT_FOUND);
  EXPECT_EQ(text, nullptr);
  EXPECT_EQ(radicle_strings_lookup(strings, 0, static_cast<radicle_string_source>(7), 0x20, &text, nullptr),
            RADICLE_STATUS_INVALID_ARGUMENT);

  const radicle_record* world = nullptr;
  ASSERT_EQ(radicle_env_winner(env, {"Base.esm", 0x800}, &world), RADICLE_STATUS_OK);
  ASSERT_EQ(radicle_strings_resolve_name(strings, world, &text, &size), RADICLE_STATUS_OK);
  EXPECT_EQ(std::string_view(text, size), "Patched Commonwealth");
  const radicle_record* unnamed = nullptr;
  ASSERT_EQ(radicle_env_winner(env, {"Inline.esp", 0x900}, &unnamed), RADICLE_STATUS_OK);
  EXPECT_EQ(radicle_strings_resolve_name(strings, unnamed, &text, nullptr), RADICLE_STATUS_NOT_FOUND);

  radicle_strings_destroy(strings);
  radicle_env_destroy(env);
}
