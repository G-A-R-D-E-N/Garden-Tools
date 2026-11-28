#include "fo4/c_api_record_json_import.h"
#include "fo4/graph/record_json.hpp"
#include "fo4/plugin/reader.hpp"
#include "fo4/plugin/record_json_import.hpp"
#include "plugin_bytes.hpp"
#include "temp_data_dir.hpp"

#include <nlohmann/json.hpp>

#include <gtest/gtest.h>

#include <cstddef>
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
  std::string data;
};

struct Rec {
  std::string signature;
  std::uint32_t form_id;
  std::string editor_id;
  std::vector<Sub> subs;
};

std::string zstr(std::string_view text) {
  return std::string(text) + std::string(1, '\0');
}

std::vector<std::byte> make_plugin(const std::vector<Rec>& records) {
  fo4::test::ByteWriter out;
  out.bytes = fo4::test::make_tes4_plugin(0, 1.0f, {});
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

std::vector<Rec> kywd_records() {
  return {{"KYWD", 0x800, "KwWidget", {{"CNAM", std::string("\x10\x20\x30\x40", 4)}}}};
}

std::vector<Rec> misc_records() {
  return {{"MISC",
           0x802,
           "MiscBolt",
           {{"DEST", std::string("\x64\0\0\0\0\0\0\0", 8)},
            {"FULL", zstr("Bolt")},
            {"ZZZZ", std::string("\x0a\x0b", 2)}}}};
}

fo4::Plugin plugin_of(const std::vector<Rec>& records) {
  return fo4::read_plugin(make_plugin(records), "Base.esm");
}

std::string subrecord_bytes(const fo4::Plugin& plugin, const fo4::FormKey& key, std::string_view signature) {
  const auto* record = fo4::find_record(plugin, key);
  if (record == nullptr) throw std::runtime_error("record missing");
  for (const auto& sub : record->subrecords) {
    if (sub.signature.to_string() == signature) {
      return std::string(reinterpret_cast<const char*>(sub.data.data()), sub.data.size());
    }
  }
  return {};
}

nlohmann::json& subrecord_of(nlohmann::json& doc, std::string_view signature) {
  for (auto& sub : doc.at("subrecords")) {
    if (sub.at("sig").get<std::string>() == signature) return sub;
  }
  throw std::runtime_error("subrecord missing from JSON");
}

nlohmann::json& field_named(nlohmann::json& doc, std::string_view signature, std::string_view name) {
  for (auto& field : subrecord_of(doc, signature).at("fields")) {
    if (field.at("name").get<std::string>() == name) return field;
  }
  throw std::runtime_error("field missing from JSON");
}

bool has_status(const fo4::RecordJsonImportResult& result, fo4::RecordJsonEntryStatus status) {
  for (const auto& entry : result.entries) {
    if (entry.status == status) return true;
  }
  return false;
}

bool row_ends_with(const std::vector<std::string>& rows, std::string_view suffix) {
  for (const auto& row : rows) {
    if (row.size() >= suffix.size() && row.compare(row.size() - suffix.size(), suffix.size(), suffix) == 0) {
      return true;
    }
  }
  return false;
}

std::vector<std::string> take_rows(char** rows, std::size_t count) {
  std::vector<std::string> out;
  for (std::size_t index = 0; index < count; ++index) out.emplace_back(rows[index]);
  radicle_free_string_array(rows, count);
  return out;
}

}

TEST(RecordJsonImport, KywdColorRoundTripRestoresBytes) {
  const auto key = fo4::FormKey::parse("000800:Base.esm");
  const auto original = plugin_of(kywd_records());
  const auto original_json = fo4::record_to_json(original, key);

  auto doc = nlohmann::json::parse(original_json);
  field_named(doc, "CNAM", "Color red")["value"] = "200";
  auto changed = original;
  const auto applied = fo4::record_apply_json(changed, key, doc.dump());
  EXPECT_EQ(applied.status, fo4::RecordJsonImportStatus::committed);
  EXPECT_EQ(subrecord_bytes(changed, key, "CNAM"), std::string("\xc8\x20\x30\x40", 4));

  const auto restored = fo4::record_apply_json(changed, key, original_json);
  EXPECT_EQ(restored.status, fo4::RecordJsonImportStatus::committed);
  EXPECT_EQ(subrecord_bytes(changed, key, "CNAM"), subrecord_bytes(original, key, "CNAM"));
  EXPECT_EQ(fo4::record_to_json(changed, key), original_json);
}

TEST(RecordJsonImport, MiscRoundTripLeavesLocalizedStringUntouched) {
  const auto key = fo4::FormKey::parse("000802:Base.esm");
  const auto original = plugin_of(misc_records());
  const auto original_json = fo4::record_to_json(original, key);

  auto doc = nlohmann::json::parse(original_json);
  field_named(doc, "DEST", "Destruction health")["value"] = "50";
  auto changed = original;
  const auto applied = fo4::record_apply_json(changed, key, doc.dump());
  EXPECT_EQ(applied.status, fo4::RecordJsonImportStatus::committed);
  EXPECT_EQ(subrecord_bytes(changed, key, "DEST"), std::string("\x32\0\0\0\0\0\0\0", 8));

  const auto restored = fo4::record_apply_json(changed, key, original_json);
  EXPECT_EQ(restored.status, fo4::RecordJsonImportStatus::committed);
  EXPECT_TRUE(has_status(restored, fo4::RecordJsonEntryStatus::unchanged));
  EXPECT_EQ(subrecord_bytes(changed, key, "DEST"), subrecord_bytes(original, key, "DEST"));
  EXPECT_EQ(subrecord_bytes(changed, key, "FULL"), zstr("Bolt"));
  EXPECT_EQ(fo4::record_to_json(changed, key), original_json);
}

TEST(RecordJsonImport, Windows1252TextRoundTripIsUnchanged) {
  const auto key = fo4::FormKey::parse("000800:Base.esm");
  const auto original = plugin_of({{"KYWD", 0x800, "KwWidget", {{"FULL", zstr("Caf\xE9")}}}});
  const auto original_json = fo4::record_to_json(original, key);
  EXPECT_NE(original_json.find("\"value\":\"Caf\xC3\xA9\""), std::string::npos);

  auto changed = original;
  const auto restored = fo4::record_apply_json(changed, key, original_json);
  EXPECT_EQ(restored.status, fo4::RecordJsonImportStatus::committed);
  EXPECT_TRUE(has_status(restored, fo4::RecordJsonEntryStatus::unchanged));
  EXPECT_FALSE(has_status(restored, fo4::RecordJsonEntryStatus::invalid_value));
  EXPECT_EQ(subrecord_bytes(changed, key, "FULL"), zstr("Caf\xE9"));
}

TEST(RecordJsonImport, UnknownPathRefusedUnlessSkipped) {
  const auto key = fo4::FormKey::parse("000802:Base.esm");
  const auto original = plugin_of(misc_records());
  const auto original_json = fo4::record_to_json(original, key);

  auto doc = nlohmann::json::parse(original_json);
  subrecord_of(doc, "DEST")["fields"].push_back({{"name", "Bogus"}, {"path", "MISC\\Bogus"}, {"value", "1"}});
  const auto text = doc.dump();

  auto refused_plugin = original;
  const auto refused = fo4::record_apply_json(refused_plugin, key, text);
  EXPECT_EQ(refused.status, fo4::RecordJsonImportStatus::refused);
  EXPECT_TRUE(has_status(refused, fo4::RecordJsonEntryStatus::unknown_path));
  EXPECT_EQ(fo4::record_to_json(refused_plugin, key), original_json);

  auto skipped_plugin = original;
  fo4::RecordJsonImportOptions skip;
  skip.skip_unknown = true;
  const auto skipped = fo4::record_apply_json(skipped_plugin, key, text, skip);
  EXPECT_EQ(skipped.status, fo4::RecordJsonImportStatus::committed);
  EXPECT_TRUE(has_status(skipped, fo4::RecordJsonEntryStatus::unknown_path));
  EXPECT_EQ(fo4::record_to_json(skipped_plugin, key), original_json);
}

TEST(RecordJsonImport, InvalidValueLeavesBytesUnchanged) {
  const auto key = fo4::FormKey::parse("000802:Base.esm");
  const auto original = plugin_of(misc_records());
  const auto original_json = fo4::record_to_json(original, key);

  auto doc = nlohmann::json::parse(original_json);
  field_named(doc, "DEST", "Destruction health")["value"] = "not a number";
  auto changed = original;
  const auto result = fo4::record_apply_json(changed, key, doc.dump());
  EXPECT_EQ(result.status, fo4::RecordJsonImportStatus::refused);
  EXPECT_TRUE(has_status(result, fo4::RecordJsonEntryStatus::invalid_value));
  EXPECT_EQ(subrecord_bytes(changed, key, "DEST"), subrecord_bytes(original, key, "DEST"));
  EXPECT_EQ(fo4::record_to_json(changed, key), original_json);
}

TEST(RecordJsonImport, RawBytesApplyOnlyWithAllowRaw) {
  const auto key = fo4::FormKey::parse("000802:Base.esm");
  const auto original = plugin_of(misc_records());
  const auto original_json = fo4::record_to_json(original, key);

  auto doc = nlohmann::json::parse(original_json);
  subrecord_of(doc, "ZZZZ")["raw"] = "0c0d";
  const auto text = doc.dump();

  auto refused_plugin = original;
  const auto refused = fo4::record_apply_json(refused_plugin, key, text);
  EXPECT_EQ(refused.status, fo4::RecordJsonImportStatus::refused);
  EXPECT_EQ(subrecord_bytes(refused_plugin, key, "ZZZZ"), std::string("\x0a\x0b", 2));

  auto allowed_plugin = original;
  fo4::RecordJsonImportOptions allow;
  allow.allow_raw = true;
  const auto allowed = fo4::record_apply_json(allowed_plugin, key, text, allow);
  EXPECT_EQ(allowed.status, fo4::RecordJsonImportStatus::committed);
  EXPECT_EQ(subrecord_bytes(allowed_plugin, key, "ZZZZ"), std::string("\x0c\x0d", 2));
}

TEST(RecordJsonImport, FormKeyMismatchRefusedUnlessIgnored) {
  const auto key = fo4::FormKey::parse("000802:Base.esm");
  const auto original = plugin_of(misc_records());
  const auto original_json = fo4::record_to_json(original, key);

  auto doc = nlohmann::json::parse(original_json);
  doc["formKey"] = "000801:Base.esm";
  field_named(doc, "DEST", "Destruction health")["value"] = "50";
  const auto text = doc.dump();

  auto refused_plugin = original;
  const auto refused = fo4::record_apply_json(refused_plugin, key, text);
  EXPECT_EQ(refused.status, fo4::RecordJsonImportStatus::form_key_mismatch);
  EXPECT_EQ(fo4::record_to_json(refused_plugin, key), original_json);

  auto ignored_plugin = original;
  fo4::RecordJsonImportOptions ignore;
  ignore.ignore_form_key = true;
  const auto ignored = fo4::record_apply_json(ignored_plugin, key, text, ignore);
  EXPECT_EQ(ignored.status, fo4::RecordJsonImportStatus::committed);
  EXPECT_EQ(subrecord_bytes(ignored_plugin, key, "DEST"), std::string("\x32\0\0\0\0\0\0\0", 8));
}

TEST(RecordJsonImportCApi, StatusAndRowsFollowAllOrNothing) {
  const fo4::test::TempDataDir directory;
  const auto path = directory.path() / "Base.esm";
  const auto bytes = make_plugin(misc_records());
  {
    std::ofstream out(path, std::ios::binary);
    out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
  }

  radicle_plugin* plugin = nullptr;
  ASSERT_EQ(radicle_plugin_read_file(path.string().c_str(), &plugin), RADICLE_STATUS_OK) << radicle_last_error();
  const radicle_form_key key{"Base.esm", 0x802};
  const auto key_text = fo4::FormKey::parse("000802:Base.esm");
  const auto original_json = fo4::record_to_json(plugin_of(misc_records()), key_text);

  auto doc = nlohmann::json::parse(original_json);
  field_named(doc, "DEST", "Destruction health")["value"] = "50";
  const auto changed = doc.dump();

  char** rows = nullptr;
  std::size_t count = 0;
  EXPECT_EQ(radicle_plugin_apply_record_json(plugin, key, changed.c_str(), 0, &rows, &count), RADICLE_STATUS_OK)
      << radicle_last_error();
  EXPECT_TRUE(row_ends_with(take_rows(rows, count), "\tapplied"));

  subrecord_of(doc, "DEST")["fields"].push_back({{"name", "Bogus"}, {"path", "MISC\\Bogus"}, {"value", "1"}});
  const auto unknown = doc.dump();
  rows = nullptr;
  count = 0;
  EXPECT_EQ(radicle_plugin_apply_record_json(plugin, key, unknown.c_str(), 0, &rows, &count),
            RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_TRUE(row_ends_with(take_rows(rows, count), "\tunknown_path"));

  rows = nullptr;
  count = 0;
  EXPECT_EQ(radicle_plugin_apply_record_json(plugin, key, unknown.c_str(), RADICLE_RECORD_JSON_SKIP_UNKNOWN, &rows,
                                             &count),
            RADICLE_STATUS_OK)
      << radicle_last_error();
  EXPECT_TRUE(row_ends_with(take_rows(rows, count), "\tunknown_path"));

  radicle_plugin_destroy(plugin);
}
