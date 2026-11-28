#include "fo4/plugin/reader.hpp"
#include "fo4/plugin/text_export.hpp"
#include "fo4/plugin/writer.hpp"
#include "plugin_bytes.hpp"
#include "temp_data_dir.hpp"

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <system_error>
#include <variant>
#include <vector>

namespace {

std::vector<std::byte> misc_plugin(const std::vector<std::uint32_t>& form_ids) {
  fo4::test::ByteWriter out;
  const auto tes4 = fo4::test::begin_record(out, "TES4", 0, 0);
  fo4::test::ByteWriter hedr;
  hedr.f32(1.0f);
  hedr.u32(static_cast<std::uint32_t>(form_ids.size() + 1));
  hedr.u32(0x800);
  fo4::test::write_subrecord(out, "HEDR", hedr.bytes);
  fo4::test::end_record(out, tes4);
  const auto grup = fo4::test::begin_group(out, "MISC", 0);
  for (const auto form_id : form_ids) {
    const auto rec = fo4::test::begin_record(out, "MISC", 0, form_id);
    fo4::test::write_subrecord(out, "EDID", std::string_view{"Item\0", 5});
    fo4::test::end_record(out, rec);
  }
  fo4::test::end_group(out, grup);
  return out.bytes;
}

fo4::Plugin items_plugin(const std::vector<std::uint32_t>& form_ids) {
  return fo4::read_plugin(misc_plugin(form_ids), "Items.esp");
}

void replace_in_file(const std::filesystem::path& path, const std::string& from, const std::string& to) {
  std::ifstream in{path, std::ios::binary};
  std::string text{std::istreambuf_iterator<char>{in}, {}};
  in.close();
  const auto at = text.find(from);
  ASSERT_NE(at, std::string::npos) << from;
  text.replace(at, from.size(), to);
  std::ofstream out{path, std::ios::binary | std::ios::trunc};
  out << text;
}

std::string form_id_label(std::uint32_t form_id) {
  std::string label(4, '\0');
  for (std::size_t byte = 0; byte < 4; ++byte) {
    label[byte] = static_cast<char>(form_id >> (8 * byte));
  }
  return label;
}

void write_tes4(fo4::test::ByteWriter& out, std::uint32_t record_count) {
  const auto tes4 = fo4::test::begin_record(out, "TES4", 0, 0);
  fo4::test::ByteWriter hedr;
  hedr.f32(1.0f);
  hedr.u32(record_count);
  hedr.u32(0x800);
  fo4::test::write_subrecord(out, "HEDR", hedr.bytes);
  fo4::test::end_record(out, tes4);
}

fo4::Plugin non_plain_subrecord_plugin() {
  fo4::test::ByteWriter out;
  write_tes4(out, 2);
  const auto grup = fo4::test::begin_group(out, "MISC", 0);
  const auto rec = fo4::test::begin_record(out, "MISC", 0, 0x801);
  fo4::test::write_subrecord(out, std::string_view{"\x01IAD", 4}, std::string_view{"Imad\0", 5});
  fo4::test::write_subrecord(out, "@IAD", std::string_view{"Tail\0", 5});
  fo4::test::end_record(out, rec);
  fo4::test::end_group(out, grup);
  return fo4::read_plugin(out.bytes, "Items.esp");
}

fo4::Plugin nested_group_plugin() {
  fo4::test::ByteWriter out;
  write_tes4(out, 5);
  const auto wrld_group = fo4::test::begin_group(out, "WRLD", 0);
  const auto wrld = fo4::test::begin_record(out, "WRLD", 0, 0x800);
  fo4::test::write_subrecord(out, "EDID", std::string_view{"World\0", 6});
  fo4::test::end_record(out, wrld);
  const auto world_children = fo4::test::begin_group(out, form_id_label(0x800), 1);
  const auto cell = fo4::test::begin_record(out, "CELL", 0, 0x801);
  fo4::test::write_subrecord(out, "EDID", std::string_view{"Cell\0", 5});
  fo4::test::end_record(out, cell);
  const auto cell_children = fo4::test::begin_group(out, form_id_label(0x801), 6);
  const auto refr = fo4::test::begin_record(out, "REFR", 0, 0x802);
  fo4::test::write_subrecord(out, "NAME", std::string_view{"\x01\x00\x00\x00", 4});
  fo4::test::end_record(out, refr);
  const auto persistent = fo4::test::begin_group(out, form_id_label(0x801), 8);
  const auto persistent_refr = fo4::test::begin_record(out, "REFR", 0, 0x803);
  fo4::test::end_record(out, persistent_refr);
  fo4::test::end_group(out, persistent);
  const auto temporary = fo4::test::begin_group(out, form_id_label(0x801), 9);
  const auto temporary_refr = fo4::test::begin_record(out, "REFR", 0, 0x804);
  fo4::test::end_record(out, temporary_refr);
  fo4::test::end_group(out, temporary);
  fo4::test::end_group(out, cell_children);
  fo4::test::end_group(out, world_children);
  fo4::test::end_group(out, wrld_group);
  return fo4::read_plugin(out.bytes, "Items.esp");
}

fo4::Plugin repeated_top_level_group_plugin() {
  fo4::test::ByteWriter out;
  write_tes4(out, 4);
  for (const std::uint32_t form_id : {0x801u, 0x802u}) {
    const auto grup = fo4::test::begin_group(out, "MISC", 0);
    const auto rec = fo4::test::begin_record(out, "MISC", 0, form_id);
    fo4::test::write_subrecord(out, "EDID", std::string_view{"Item\0", 5});
    fo4::test::end_record(out, rec);
    fo4::test::end_group(out, grup);
  }
  return fo4::read_plugin(out.bytes, "Items.esp");
}

fo4::Plugin destruction_plugin() {
  fo4::test::ByteWriter out;
  write_tes4(out, 2);
  const auto grup = fo4::test::begin_group(out, "MISC", 0);
  const auto rec = fo4::test::begin_record(out, "MISC", 0, 0x801);
  fo4::test::write_subrecord(out, "EDID", std::string_view{"Item\0", 5});
  fo4::test::write_subrecord(out, "DEST", std::string_view{"\x2C\x01\x00\x00\x02\x00\x00\x00", 8});
  fo4::test::end_record(out, rec);
  fo4::test::end_group(out, grup);
  return fo4::read_plugin(out.bytes, "Items.esp");
}

fo4::Plugin single_subrecord_plugin(std::string_view record, std::string_view subrecord, std::string_view payload) {
  fo4::test::ByteWriter out;
  write_tes4(out, 2);
  const auto grup = fo4::test::begin_group(out, record, 0);
  const auto rec = fo4::test::begin_record(out, record, 0, 0x801);
  fo4::test::write_subrecord(out, subrecord, payload);
  fo4::test::end_record(out, rec);
  fo4::test::end_group(out, grup);
  return fo4::read_plugin(out.bytes, "Items.esp");
}

std::string destruction_payload(std::uint8_t flags) {
  std::string payload{"\x2C\x01\x00\x00\x02\x00\x00\x00", 8};
  payload[5] = static_cast<char>(flags);
  return payload;
}

nlohmann::json exported_labels(const fo4::Plugin& plugin, const std::string& file) {
  fo4::test::TempDataDir scratch;
  const auto dir = fo4::export_plugin_text(plugin, scratch.path(), fo4::TextExportOptions{true});
  const auto record = nlohmann::json::parse(fo4::test::folder_contents(dir).at(file));
  return record.at("labels");
}

void tamper_labels(nlohmann::json& node) {
  if (node.is_object() && node.contains("labels")) {
    for (auto& label : node.at("labels")) {
      label["subrecord"] = "ZZZZ";
      label["field"] = "forged";
      label["value"] = "tampered";
    }
  }
  if (node.is_object() || node.is_array()) {
    for (auto& child : node) tamper_labels(child);
  }
}

void tamper_export(const std::filesystem::path& dir) {
  for (const auto& entry : std::filesystem::recursive_directory_iterator(dir)) {
    if (!entry.is_regular_file() || entry.path().extension() != ".json") continue;
    std::ifstream in{entry.path(), std::ios::binary};
    auto document = nlohmann::json::parse(in);
    in.close();
    tamper_labels(document);
    std::ofstream out{entry.path(), std::ios::binary | std::ios::trunc};
    out << document.dump(2) << "\n";
  }
}

}  // namespace

TEST(TextExportFailClosed, RejectsOddLengthHexOnImport) {
  const auto plugin = items_plugin({0x801});
  fo4::test::TempDataDir scratch;
  const auto dir = fo4::export_plugin_text(plugin, scratch.path());
  replace_in_file(dir / "MISC/000801@Items.esp.json", "\"hex\": \"4974656d00\"",
                  "\"hex\": \"4974656d0\"");
  EXPECT_THROW(static_cast<void>(fo4::import_plugin_text(dir)), std::invalid_argument);
}

TEST(TextExportFailClosed, RejectsNonHexDigitInSubrecordOnImport) {
  const auto plugin = items_plugin({0x801});
  fo4::test::TempDataDir scratch;
  const auto dir = fo4::export_plugin_text(plugin, scratch.path());
  replace_in_file(dir / "MISC/000801@Items.esp.json", "\"hex\": \"4974656d00\"",
                  "\"hex\": \"4974656d0g\"");
  EXPECT_THROW(static_cast<void>(fo4::import_plugin_text(dir)), std::invalid_argument);
}

TEST(TextExportFailClosed, RejectsWrongFormatMarkerOrMissingHeaderOnImport) {
  const auto plugin = items_plugin({0x801});
  fo4::test::TempDataDir scratch;
  const auto dir = fo4::export_plugin_text(plugin, scratch.path());
  replace_in_file(dir / "header.json", "\"format\": \"fo4txt/1\"", "\"format\": \"fo4txt/9\"");
  EXPECT_THROW(static_cast<void>(fo4::import_plugin_text(dir)), std::invalid_argument);
  std::filesystem::remove(dir / "header.json");
  EXPECT_THROW(static_cast<void>(fo4::import_plugin_text(dir)), std::invalid_argument);
}

TEST(TextExportFailClosed, RejectsNonWindows1252PluginFilenameOnExport) {
  const auto plugin = fo4::make_empty_plugin("\xE4\xB8\xAD.esp");
  fo4::test::TempDataDir scratch;
  EXPECT_THROW(static_cast<void>(fo4::export_plugin_text(plugin, scratch.path())), std::invalid_argument);
  EXPECT_TRUE(std::filesystem::is_empty(scratch.path()));
}

TEST(TextExportFailClosed, RejectsNonWindows1252PluginFilenameOnImport) {
  const auto plugin = items_plugin({0x801});
  fo4::test::TempDataDir scratch;
  const auto dir = fo4::export_plugin_text(plugin, scratch.path());
  replace_in_file(dir / "header.json", "\"filename\": \"Items.esp\"", "\"filename\": \"\xE4\xB8\xAD.esp\"");
  EXPECT_THROW(static_cast<void>(fo4::import_plugin_text(dir)), std::invalid_argument);
}

TEST(TextExportFailClosed, RejectsInvalidUtf8InJsonStringOnImport) {
  const auto plugin = items_plugin({0x801});
  fo4::test::TempDataDir scratch;
  const auto dir = fo4::export_plugin_text(plugin, scratch.path());
  replace_in_file(dir / "header.json", "\"filename\": \"Items.esp\"", "\"filename\": \"\xFF.esp\"");
  EXPECT_THROW(static_cast<void>(fo4::import_plugin_text(dir)), std::invalid_argument);
}

TEST(TextExportFailClosed, RejectsManifestNamingMissingRecordFileOnImport) {
  const auto plugin = items_plugin({0x801});
  fo4::test::TempDataDir scratch;
  const auto dir = fo4::export_plugin_text(plugin, scratch.path());
  std::filesystem::remove(dir / "MISC/000801@Items.esp.json");
  EXPECT_THROW(static_cast<void>(fo4::import_plugin_text(dir)), std::invalid_argument);
}

TEST(TextExportFailClosed, RefusesToExportIntoNonEmptyForeignFolder) {
  const auto plugin = items_plugin({0x801});
  fo4::test::TempDataDir scratch;
  const auto foreign = scratch.path() / "Items.esp.fo4txt";
  std::filesystem::create_directories(foreign);
  {
    std::ofstream notes{foreign / "notes.txt", std::ios::binary};
    notes << "mine";
  }
  EXPECT_THROW(static_cast<void>(fo4::export_plugin_text(plugin, scratch.path())), std::runtime_error);
  EXPECT_TRUE(std::filesystem::exists(foreign / "notes.txt"));
}

TEST(TextExportFailClosed, SameFormKeyTwiceGetsDeterministicCollisionSuffix) {
  const auto plugin = items_plugin({0x801, 0x801});
  fo4::test::TempDataDir scratch;
  const auto dir = fo4::export_plugin_text(plugin, scratch.path());
  EXPECT_TRUE(std::filesystem::exists(dir / "MISC/000801@Items.esp.json"));
  EXPECT_TRUE(std::filesystem::exists(dir / "MISC/000801@Items.esp~2.json"));
  const auto imported = fo4::import_plugin_text(dir);
  EXPECT_EQ(fo4::write_plugin(imported), fo4::write_plugin(fo4::test::canonical_plugin(plugin)));
}

TEST(TextExportFailClosed, ReexportRemovesFilesOfRecordsThatWereDeleted) {
  fo4::test::TempDataDir scratch;
  static_cast<void>(fo4::export_plugin_text(items_plugin({0x801, 0x802}), scratch.path()));
  const auto dir = fo4::export_plugin_text(items_plugin({0x801}), scratch.path());
  EXPECT_TRUE(std::filesystem::exists(dir / "MISC/000801@Items.esp.json"));
  EXPECT_FALSE(std::filesystem::exists(dir / "MISC/000802@Items.esp.json"));
}

TEST(TextExportFailClosed, TimestampChangeRewritesOnlyThatRecordFile) {
  const auto plugin = items_plugin({0x801, 0x802});
  auto retimed = plugin;
  std::get<fo4::Record>(retimed.groups[0].children[1]).header.timestamp = 7;
  fo4::test::TempDataDir before;
  fo4::test::TempDataDir after;
  const auto old_files = fo4::test::folder_contents(fo4::export_plugin_text(plugin, before.path()));
  const auto new_files = fo4::test::folder_contents(fo4::export_plugin_text(retimed, after.path()));
  ASSERT_EQ(old_files.size(), new_files.size());
  std::vector<std::string> changed;
  for (const auto& [name, text] : old_files) {
    if (new_files.at(name) != text) changed.push_back(name);
  }
  EXPECT_EQ(changed, std::vector<std::string>{"MISC/000802@Items.esp.json"});
}

TEST(TextExportFailClosed, ReexportRefusesForeignFileInsideSignatureFolderAndDeletesNothing) {
  fo4::test::TempDataDir scratch;
  const auto dir = fo4::export_plugin_text(items_plugin({0x801, 0x802}), scratch.path());
  {
    std::ofstream notes{dir / "MISC/notes.txt", std::ios::binary};
    notes << "mine";
  }
  EXPECT_THROW(static_cast<void>(fo4::export_plugin_text(items_plugin({0x801}), scratch.path())),
               std::runtime_error);
  EXPECT_TRUE(std::filesystem::exists(dir / "header.json"));
  EXPECT_TRUE(std::filesystem::exists(dir / "MISC/000802@Items.esp.json"));
  EXPECT_TRUE(std::filesystem::exists(dir / "MISC/notes.txt"));
}

TEST(TextExportFailClosed, ReexportRefusesForeignJsonInRepeatedGroupFolders) {
  fo4::test::TempDataDir scratch;
  const auto plugin = repeated_top_level_group_plugin();
  const auto dir = fo4::export_plugin_text(plugin, scratch.path());
  for (const auto& folder : {"MISC~0", "MISC~2"}) {
    std::filesystem::create_directories(dir / folder);
    const auto notes = dir / folder / "notes.json";
    {
      std::ofstream out{notes, std::ios::binary};
      out << "mine";
    }
    EXPECT_THROW(static_cast<void>(fo4::export_plugin_text(plugin, scratch.path())), std::runtime_error);
    EXPECT_TRUE(std::filesystem::exists(dir / "header.json"));
    EXPECT_TRUE(std::filesystem::exists(dir / "MISC~2/_group.json"));
    EXPECT_TRUE(std::filesystem::exists(notes));
    std::filesystem::remove(notes);
    if (std::string_view(folder) == "MISC~0") std::filesystem::remove(dir / folder);
  }
}

TEST(TextExportFailClosed, ReexportRefusesLinkedGroupFolderAndKeepsTarget) {
  fo4::test::TempDataDir scratch;
  fo4::test::TempDataDir outside;
  const auto plugin = repeated_top_level_group_plugin();
  const auto dir = fo4::export_plugin_text(plugin, scratch.path());
  const auto notes = outside.path() / "notes.json";
  {
    std::ofstream out{notes, std::ios::binary};
    out << "mine";
  }
  std::error_code error;
  std::filesystem::create_directory_symlink(outside.path(), dir / "MISC~3", error);
  if (error) GTEST_SKIP() << "directory symlinks unavailable: " << error.message();
  EXPECT_THROW(static_cast<void>(fo4::export_plugin_text(plugin, scratch.path())), std::runtime_error);
  EXPECT_TRUE(std::filesystem::exists(notes));
  EXPECT_TRUE(std::filesystem::exists(dir / "header.json"));
}

TEST(TextExportFailClosed, RecordWithoutOwnerIsNamedRawAndDoesNotFailExport) {
  fo4::test::TempDataDir scratch;
  const auto dir = fo4::export_plugin_text(items_plugin({0, 0, 0x02000801}), scratch.path());
  EXPECT_TRUE(std::filesystem::exists(dir / "MISC/RAW_00000000.json"));
  EXPECT_TRUE(std::filesystem::exists(dir / "MISC/RAW_00000000~2.json"));
  EXPECT_TRUE(std::filesystem::exists(dir / "MISC/RAW_02000801.json"));
  EXPECT_NO_THROW(static_cast<void>(fo4::import_plugin_text(dir)));
}

TEST(TextExportRoundTrip, NonPlainSubrecordSignaturesRoundTripToCanonicalBytes) {
  const auto plugin = non_plain_subrecord_plugin();
  fo4::test::TempDataDir scratch;
  const auto dir = fo4::export_plugin_text(plugin, scratch.path());
  const auto files = fo4::test::folder_contents(dir);
  const auto& record = files.at("MISC/000801@Items.esp.json");
  EXPECT_NE(record.find("\"sig\": \"0x44414901\""), std::string::npos);
  EXPECT_NE(record.find("\"sig\": \"@IAD\""), std::string::npos);
  EXPECT_EQ(fo4::write_plugin(fo4::import_plugin_text(dir)),
            fo4::write_plugin(fo4::test::canonical_plugin(plugin)));
}

TEST(TextExportRoundTrip, NestedFormIdGroupsRoundTripAndReexportIdentically) {
  const auto plugin = nested_group_plugin();
  fo4::test::TempDataDir first;
  const auto first_dir = fo4::export_plugin_text(plugin, first.path());
  EXPECT_TRUE(std::filesystem::exists(first_dir / "REFR/000804@Items.esp.json"));
  const auto imported = fo4::import_plugin_text(first_dir);
  EXPECT_EQ(fo4::write_plugin(imported), fo4::write_plugin(fo4::test::canonical_plugin(plugin)));
  fo4::test::TempDataDir second;
  const auto second_dir = fo4::export_plugin_text(imported, second.path());
  EXPECT_EQ(fo4::test::folder_contents(second_dir), fo4::test::folder_contents(first_dir));
}

TEST(TextExportRoundTrip, RepeatedTopLevelGroupLabelsKeepEveryGroupAndReexportIdentically) {
  const auto plugin = repeated_top_level_group_plugin();
  ASSERT_EQ(plugin.groups.size(), 2u);
  fo4::test::TempDataDir first;
  const auto first_dir = fo4::export_plugin_text(plugin, first.path());
  const auto imported = fo4::import_plugin_text(first_dir);
  EXPECT_EQ(fo4::write_plugin(imported), fo4::write_plugin(fo4::test::canonical_plugin(plugin)));
  fo4::test::TempDataDir second;
  const auto second_dir = fo4::export_plugin_text(imported, second.path());
  EXPECT_EQ(fo4::test::folder_contents(second_dir), fo4::test::folder_contents(first_dir));
  EXPECT_NO_THROW(static_cast<void>(fo4::export_plugin_text(imported, second.path())));
}

TEST(TextExportSchemaLabels, LabelsOnAndOffImportToIdenticalBytes) {
  const auto plugin = destruction_plugin();
  fo4::test::TempDataDir plain;
  fo4::test::TempDataDir labelled;
  const auto plain_dir = fo4::export_plugin_text(plugin, plain.path());
  const auto labelled_dir = fo4::export_plugin_text(plugin, labelled.path(), fo4::TextExportOptions{true});
  EXPECT_NE(fo4::test::folder_contents(labelled_dir).at("MISC/000801@Items.esp.json").find("\"labels\""),
            std::string::npos);
  const auto expected = fo4::write_plugin(fo4::test::canonical_plugin(plugin));
  EXPECT_EQ(fo4::write_plugin(fo4::import_plugin_text(plain_dir)), expected);
  EXPECT_EQ(fo4::write_plugin(fo4::import_plugin_text(labelled_dir)), expected);
}

TEST(TextExportSchemaLabels, TamperedLabelValuesDoNotChangeImportedBytes) {
  const auto plugin = destruction_plugin();
  fo4::test::TempDataDir scratch;
  const auto dir = fo4::export_plugin_text(plugin, scratch.path(), fo4::TextExportOptions{true});
  tamper_export(dir);
  EXPECT_NE(fo4::test::folder_contents(dir).at("MISC/000801@Items.esp.json").find("tampered"),
            std::string::npos);
  EXPECT_EQ(fo4::write_plugin(fo4::import_plugin_text(dir)),
            fo4::write_plugin(fo4::test::canonical_plugin(plugin)));
}

TEST(TextExportSchemaLabels, DestructionHealthLabelNamesFieldAndDecodedValue) {
  const auto plugin = destruction_plugin();
  fo4::test::TempDataDir scratch;
  const auto dir = fo4::export_plugin_text(plugin, scratch.path(), fo4::TextExportOptions{true});
  const auto record =
      nlohmann::json::parse(fo4::test::folder_contents(dir).at("MISC/000801@Items.esp.json"));
  const auto& labels = record.at("labels");
  EXPECT_NE(std::find(labels.begin(), labels.end(),
                      nlohmann::json{{"subrecord", "DEST"}, {"field", "MISC\\DestructionHealth"},
                                     {"offset", 0}, {"value", "300"}}),
            labels.end());
  EXPECT_NE(std::find(labels.begin(), labels.end(),
                      nlohmann::json{{"subrecord", "DEST"}, {"field", "MISC\\DestructionStageCount"},
                                     {"offset", 4}, {"value", "2"}}),
            labels.end());
}

TEST(TextExportSchemaLabels, ZstringLabelConvertsWindows1252ToUtf8) {
  const auto labels = exported_labels(single_subrecord_plugin("DIAL", "FULL", std::string{"Caf\xE9\0", 5}),
                                      "DIAL/000801@Items.esp.json");
  EXPECT_NE(std::find(labels.begin(), labels.end(),
                      nlohmann::json{{"subrecord", "FULL"}, {"field", "DIAL\\Name"}, {"value", "Caf\xC3\xA9"}}),
            labels.end());
}

TEST(TextExportSchemaLabels, FixedStringLabelDropsTrailingNuls) {
  const auto labels = exported_labels(single_subrecord_plugin("RACE", "MTNM", std::string{"AB\0\0", 4}),
                                      "RACE/000801@Items.esp.json");
  EXPECT_NE(std::find(labels.begin(), labels.end(),
                      nlohmann::json{{"subrecord", "MTNM"}, {"field", "RACE\\MovementTypeNames"}, {"value", "AB"}}),
            labels.end());
}

TEST(TextExportSchemaLabels, FlagsLabelJoinsNamedBitsAndKeepsUnnamedRemainder) {
  const auto two_bits = exported_labels(single_subrecord_plugin("MISC", "DEST", destruction_payload(0x03)),
                                        "MISC/000801@Items.esp.json");
  EXPECT_NE(std::find(two_bits.begin(), two_bits.end(),
                      nlohmann::json{{"subrecord", "DEST"}, {"field", "MISC\\DestructionFlags"}, {"offset", 5},
                                     {"value", "VATS Targetable|Large Actor Destroys"}}),
            two_bits.end());
  const auto with_remainder = exported_labels(single_subrecord_plugin("MISC", "DEST", destruction_payload(0x07)),
                                              "MISC/000801@Items.esp.json");
  EXPECT_NE(std::find(with_remainder.begin(), with_remainder.end(),
                      nlohmann::json{{"subrecord", "DEST"}, {"field", "MISC\\DestructionFlags"}, {"offset", 5},
                                     {"value", "VATS Targetable|Large Actor Destroys|4"}}),
            with_remainder.end());
}

TEST(TextExportSchemaLabels, EnumerationLabelShowsNameForStoredValue) {
  const auto labels = exported_labels(single_subrecord_plugin("ADDN", "DNAM", std::string{"\x00\x00\x01\x00", 4}),
                                      "ADDN/000801@Items.esp.json");
  EXPECT_NE(std::find(labels.begin(), labels.end(),
                      nlohmann::json{{"subrecord", "DNAM"}, {"field", "ADDN\\Flags"}, {"offset", 2},
                                     {"value", "Master Particle System"}}),
            labels.end());
}

TEST(TextExportSchemaLabels, DefaultOptionsEmitNoLabelKeyAndMatchLabelsOff) {
  const auto plugin = destruction_plugin();
  fo4::test::TempDataDir implicit;
  fo4::test::TempDataDir explicit_off;
  const auto implicit_files = fo4::test::folder_contents(fo4::export_plugin_text(plugin, implicit.path()));
  const auto off_files =
      fo4::test::folder_contents(fo4::export_plugin_text(plugin, explicit_off.path(), fo4::TextExportOptions{false}));
  EXPECT_EQ(implicit_files, off_files);
  for (const auto& [name, text] : implicit_files) {
    EXPECT_EQ(text.find("\"labels\""), std::string::npos) << name;
  }
}

namespace {

nlohmann::json dial_full_labels(std::string_view payload, bool localized) {
  auto plugin = single_subrecord_plugin("DIAL", "FULL", payload);
  if (localized) plugin.header.tes4_flags = fo4::RecordFlags{fo4::RecordFlags::localized};
  return exported_labels(plugin, "DIAL/000801@Items.esp.json");
}

}  // namespace

TEST(TextExportSchemaLabels, LocalizedPluginStringIdGetsNoLabelButInlineTextStillDoes) {
  const std::string string_id{"\x45\x23\x01\x00", 4};
  EXPECT_TRUE(dial_full_labels(string_id, true).empty());
  EXPECT_EQ(dial_full_labels(std::string{"Abc\0", 4}, false),
            (nlohmann::json{{{"subrecord", "FULL"}, {"field", "DIAL\\Name"}, {"value", "Abc"}}}));
  EXPECT_FALSE(dial_full_labels(std::string{"Caf\xE9\0", 5}, true).empty());
}
