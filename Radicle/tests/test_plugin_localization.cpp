#include "fo4/plugin/mutation.hpp"

#include "fo4/localization/strings.hpp"
#include "fo4/plugin/reader.hpp"
#include "fo4/plugin/visit.hpp"
#include "plugin_bytes.hpp"
#include "temp_data_dir.hpp"

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

using fo4::localization::LocalizationTables;
using fo4::localization::LocalizedStringTable;
using fo4::localization::StringTableKind;
using Status = fo4::PluginLocalizationStatus;

constexpr auto k_edid = fo4::Signature::from_string("EDID");
constexpr auto k_full = fo4::Signature::from_string("FULL");
constexpr auto k_desc = fo4::Signature::from_string("DESC");
constexpr auto k_cnam = fo4::Signature::from_string("CNAM");
constexpr auto k_nam1 = fo4::Signature::from_string("NAM1");
constexpr auto k_rnam = fo4::Signature::from_string("RNAM");

constexpr auto k_race_key = "000800:Patch.esp";
constexpr auto k_quest_key = "000801:Patch.esp";
constexpr auto k_info_key = "000803:Patch.esp";

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

std::size_t child_group(fo4::test::ByteWriter& out, std::uint32_t parent, std::int32_t type) {
  const auto at = fo4::test::begin_group(out, "QUST", type);
  out.patch_u32(at + 8, parent);
  return at;
}

std::vector<std::byte> build(std::uint32_t tes4_flags,
                             std::vector<std::byte> desc = zstr("Race description")) {
  fo4::test::ByteWriter out;
  const auto tes4 = fo4::test::begin_record(out, "TES4", tes4_flags, 0);
  fo4::test::ByteWriter hedr;
  hedr.f32(1.0f);
  hedr.u32(0);
  hedr.u32(0x800);
  fo4::test::write_subrecord(out, "HEDR", hedr.bytes);
  fo4::test::end_record(out, tes4);

  const auto races = fo4::test::begin_group(out, "RACE", 0);
  const auto race = fo4::test::begin_record(out, "RACE", 0, 0x800);
  fo4::test::write_subrecord(out, "EDID", zstr("RadicleRace"));
  fo4::test::write_subrecord(out, "FULL", zstr("Race name"));
  fo4::test::write_subrecord(out, "DESC", desc);
  fo4::test::write_subrecord(out, "DESC", zstr(""));
  fo4::test::end_record(out, race);
  fo4::test::end_group(out, races);

  const auto quests = fo4::test::begin_group(out, "QUST", 0);
  const auto quest = fo4::test::begin_record(out, "QUST", 0, 0x801);
  fo4::test::write_subrecord(out, "EDID", zstr("RadicleQuest"));
  fo4::test::write_subrecord(out, "CNAM", zstr("Quest log entry"));
  fo4::test::end_record(out, quest);
  const auto topics = child_group(out, 0x801, 10);
  const auto topic = fo4::test::begin_record(out, "DIAL", 0, 0x802);
  fo4::test::write_subrecord(out, "EDID", zstr("RadicleTopic"));
  fo4::test::end_record(out, topic);
  const auto infos = child_group(out, 0x802, 7);
  const auto info = fo4::test::begin_record(out, "INFO", 0, 0x803);
  fo4::test::write_subrecord(out, "EDID", zstr("RadicleInfo"));
  fo4::test::write_subrecord(out, "NAM1", zstr("Response text"));
  fo4::test::write_subrecord(out, "RNAM", zstr("Prompt"));
  fo4::test::end_record(out, info);
  fo4::test::end_group(out, infos);
  fo4::test::end_group(out, topics);
  fo4::test::end_group(out, quests);

  return out.bytes;
}

std::vector<std::byte> build_localized_reference(std::uint32_t full_id = 7) {
  fo4::test::ByteWriter out;
  const auto tes4 = fo4::test::begin_record(out, "TES4", 0x80u, 0);
  fo4::test::ByteWriter hedr;
  hedr.f32(1.0f);
  hedr.u32(0);
  hedr.u32(0x800);
  fo4::test::write_subrecord(out, "HEDR", hedr.bytes);
  fo4::test::end_record(out, tes4);

  const auto races = fo4::test::begin_group(out, "RACE", 0);
  const auto race = fo4::test::begin_record(out, "RACE", 0, 0x800);
  fo4::test::write_subrecord(out, "EDID", zstr("RadicleRace"));
  fo4::test::write_subrecord(out, "FULL", words(full_id));
  fo4::test::write_subrecord(out, "DESC", words(1));
  fo4::test::end_record(out, race);
  fo4::test::end_group(out, races);
  return out.bytes;
}

fo4::Plugin read(std::uint32_t tes4_flags,
                 std::vector<std::byte> desc = zstr("Race description")) {
  return fo4::read_plugin(build(tes4_flags, std::move(desc)), "Patch.esp");
}

std::vector<std::vector<std::byte>> payloads(const fo4::Plugin& plugin,
                                             std::string_view record_key,
                                             fo4::Signature signature) {
  std::vector<std::vector<std::byte>> out;
  const auto* record = fo4::find_record(plugin, fo4::FormKey::parse(record_key));
  if (record == nullptr) return out;
  for (const auto& subrecord : record->subrecords) {
    if (subrecord.signature == signature) out.push_back(subrecord.data);
  }
  return out;
}

std::string stored_text(const LocalizedStringTable& table, std::uint32_t id) {
  const auto* entry = fo4::localization::find_string(table, id);
  if (entry == nullptr) return "<missing>";
  std::string out;
  out.reserve(entry->text.size());
  for (const auto byte : entry->text) {
    out.push_back(static_cast<char>(std::to_integer<std::uint8_t>(byte)));
  }
  return out;
}

std::vector<std::pair<std::string, std::vector<std::byte>>> subrecord_map(const fo4::Plugin& plugin) {
  std::vector<std::pair<std::string, std::vector<std::byte>>> out;
  fo4::visit_records(plugin, [&](const fo4::Record& record) {
    for (const auto& subrecord : record.subrecords) {
      std::string key = record.header.signature.to_string();
      key += '/';
      key += subrecord.signature.to_string();
      out.emplace_back(std::move(key), subrecord.data);
    }
  });
  return out;
}

TEST(PluginLocalization, LocalizeConvertsInlineStringsAndFlipsTheFlag) {
  auto plugin = read(0u);
  LocalizationTables tables;

  const auto result = fo4::localize_plugin(plugin, tables);

  EXPECT_EQ(result.status, Status::converted);
  EXPECT_EQ(result.converted, 6u);
  EXPECT_EQ(result.empty_text, 1u);
  EXPECT_TRUE(plugin.header.tes4_flags.is_localized());
  EXPECT_TRUE(plugin.tes4.header.flags.is_localized());
  EXPECT_EQ(payloads(plugin, k_race_key, k_edid).at(0), zstr("RadicleRace"));
  EXPECT_EQ(payloads(plugin, k_race_key, k_full).at(0), words(1));
  EXPECT_EQ(payloads(plugin, k_race_key, k_desc).at(0), words(1));
  EXPECT_EQ(payloads(plugin, k_race_key, k_desc).at(1), words(0));
  EXPECT_EQ(payloads(plugin, k_quest_key, k_cnam).at(0), words(2));
  EXPECT_EQ(payloads(plugin, k_info_key, k_nam1).at(0), words(1));
  EXPECT_EQ(payloads(plugin, k_info_key, k_rnam).at(0), words(2));

  EXPECT_EQ(tables.strings.entries.size(), 2u);
  EXPECT_EQ(tables.dlstrings.entries.size(), 2u);
  EXPECT_EQ(tables.ilstrings.entries.size(), 1u);
  EXPECT_EQ(stored_text(tables.strings, 1), "Race name");
  EXPECT_EQ(stored_text(tables.strings, 2), "Prompt");
  EXPECT_EQ(stored_text(tables.dlstrings, 1), "Race description");
  EXPECT_EQ(stored_text(tables.dlstrings, 2), "Quest log entry");
  EXPECT_EQ(stored_text(tables.ilstrings, 1), "Response text");
  EXPECT_EQ(stored_text(tables.strings, 3), "<missing>");
}

TEST(PluginLocalization, LocalizeThenDelocalizeRestoresEveryPayload) {
  auto plugin = read(0u);
  const auto before = subrecord_map(plugin);
  LocalizationTables tables;
  ASSERT_EQ(fo4::localize_plugin(plugin, tables).status, Status::converted);

  const auto result = fo4::delocalize_plugin(plugin, tables);

  EXPECT_EQ(result.status, Status::converted);
  EXPECT_EQ(result.converted, 6u);
  EXPECT_EQ(result.empty_text, 1u);
  EXPECT_FALSE(plugin.header.tes4_flags.is_localized());
  EXPECT_FALSE(plugin.tes4.header.flags.is_localized());
  EXPECT_EQ(subrecord_map(plugin), before);
}

TEST(PluginLocalization, LocalizeRefusesAPayloadThatIsNotOneTerminatedString) {
  auto plugin = read(0u, std::vector<std::byte>{std::byte{1}, std::byte{2}, std::byte{3}, std::byte{4}});
  const auto before = subrecord_map(plugin);
  LocalizationTables tables;

  const auto result = fo4::localize_plugin(plugin, tables);

  EXPECT_EQ(result.status, Status::unconvertible_field);
  EXPECT_EQ(result.converted, 0u);
  EXPECT_EQ(result.empty_text, 0u);
  EXPECT_FALSE(plugin.header.tes4_flags.is_localized());
  EXPECT_EQ(subrecord_map(plugin), before);
  EXPECT_TRUE(tables.strings.entries.empty());
  EXPECT_TRUE(tables.dlstrings.entries.empty());
  EXPECT_TRUE(tables.ilstrings.entries.empty());
}

TEST(PluginLocalization, LocalizeRefusesAnAlreadyLocalizedPlugin) {
  auto plugin = read(0u);
  LocalizationTables tables;
  ASSERT_EQ(fo4::localize_plugin(plugin, tables).status, Status::converted);
  const auto before = subrecord_map(plugin);

  const auto result = fo4::localize_plugin(plugin, tables);

  EXPECT_EQ(result.status, Status::already_localized);
  EXPECT_EQ(result.converted, 0u);
  EXPECT_EQ(subrecord_map(plugin), before);
  EXPECT_EQ(tables.strings.entries.size(), 2u);
  EXPECT_EQ(tables.dlstrings.entries.size(), 2u);
  EXPECT_EQ(tables.ilstrings.entries.size(), 1u);
}

TEST(PluginLocalization, DelocalizeRefusesAPluginThatIsNotLocalized) {
  auto plugin = read(0u);
  const auto before = subrecord_map(plugin);
  LocalizationTables tables;
  ASSERT_EQ(fo4::localize_plugin(plugin, tables).status, Status::converted);
  auto plain = read(0u);

  const auto result = fo4::delocalize_plugin(plain, tables);

  EXPECT_EQ(result.status, Status::not_localized);
  EXPECT_EQ(result.converted, 0u);
  EXPECT_EQ(subrecord_map(plain), before);
}

TEST(PluginLocalization, DelocalizeRefusesAPayloadThatIsNotAStringId) {
  auto plugin = read(0x80u);
  const auto before = subrecord_map(plugin);
  LocalizationTables tables;

  const auto result = fo4::delocalize_plugin(plugin, tables);

  EXPECT_EQ(result.status, Status::unconvertible_field);
  EXPECT_EQ(result.converted, 0u);
  EXPECT_TRUE(plugin.header.tes4_flags.is_localized());
  EXPECT_EQ(subrecord_map(plugin), before);
}

TEST(PluginLocalization, DelocalizeRefusesAStringIdMissingFromTheTable) {
  auto plugin = fo4::read_plugin(build_localized_reference(), "Patch.esp");
  LocalizationTables tables;
  fo4::localization::add_string(tables.dlstrings, "Race description");
  const auto before = subrecord_map(plugin);

  const auto result = fo4::delocalize_plugin(plugin, tables);

  EXPECT_EQ(result.status, Status::unresolved_string_id);
  EXPECT_EQ(result.converted, 0u);
  EXPECT_TRUE(plugin.header.tes4_flags.is_localized());
  EXPECT_EQ(subrecord_map(plugin), before);
}

TEST(PluginLocalization, DelocalizeResolvesAStoredStringId) {
  auto plugin = fo4::read_plugin(build_localized_reference(1), "Patch.esp");
  LocalizationTables tables;
  fo4::localization::add_string(tables.strings, "Race name");
  fo4::localization::add_string(tables.dlstrings, "Race description");

  const auto result = fo4::delocalize_plugin(plugin, tables);

  EXPECT_EQ(result.status, Status::converted);
  EXPECT_EQ(result.converted, 2u);
  EXPECT_EQ(result.empty_text, 0u);
  EXPECT_FALSE(plugin.header.tes4_flags.is_localized());
  EXPECT_EQ(payloads(plugin, k_race_key, k_full).at(0), zstr("Race name"));
  EXPECT_EQ(payloads(plugin, k_race_key, k_desc).at(0), zstr("Race description"));
}

TEST(PluginLocalization, LocalizedTablesWriteTheirThreeFiles) {
  auto plugin = read(0u);
  LocalizationTables tables;
  ASSERT_EQ(fo4::localize_plugin(plugin, tables).status, Status::converted);
  const fo4::test::TempDataDir dir;

  fo4::localization::write_localization_tables(tables, "Patch.esp", "en", dir.path());

  const auto strings_path = dir.path() / "Strings" / "Patch_en.STRINGS";
  const auto dlstrings_path = dir.path() / "Strings" / "Patch_en.DLSTRINGS";
  const auto ilstrings_path = dir.path() / "Strings" / "Patch_en.ILSTRINGS";
  EXPECT_TRUE(std::filesystem::exists(strings_path));
  EXPECT_TRUE(std::filesystem::exists(dlstrings_path));
  EXPECT_TRUE(std::filesystem::exists(ilstrings_path));

  const auto strings = fo4::localization::read_string_table_file(strings_path, StringTableKind::strings);
  const auto dlstrings = fo4::localization::read_string_table_file(dlstrings_path, StringTableKind::dlstrings);
  const auto ilstrings = fo4::localization::read_string_table_file(ilstrings_path, StringTableKind::ilstrings);
  EXPECT_EQ(strings.entries.size(), 2u);
  EXPECT_EQ(dlstrings.entries.size(), 2u);
  EXPECT_EQ(ilstrings.entries.size(), 1u);
  EXPECT_EQ(stored_text(strings, 1), "Race name");
  EXPECT_EQ(stored_text(strings, 2), "Prompt");
  EXPECT_EQ(stored_text(dlstrings, 1), "Race description");
  EXPECT_EQ(stored_text(dlstrings, 2), "Quest log entry");
  EXPECT_EQ(stored_text(ilstrings, 1), "Response text");
}

}  // namespace
