#include "fo4/graph/record_json.hpp"
#include "fo4/plugin/reader.hpp"
#include "plugin_bytes.hpp"

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

#include <cstddef>
#include <cstdint>
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

std::vector<std::byte> make_plugin(const std::vector<Rec>& records, std::uint32_t tes4_flags) {
  fo4::test::ByteWriter out;
  out.bytes = fo4::test::make_tes4_plugin(tes4_flags, 1.0f, {});
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

fo4::Plugin plugin_with(const std::vector<Rec>& records, std::uint32_t tes4_flags = 0) {
  return fo4::read_plugin(make_plugin(records, tes4_flags), "Base.esm");
}

bool contains(const std::string& text, std::string_view needle) {
  return text.find(needle) != std::string::npos;
}

}

TEST(RecordJson, KywdColorRowsDecodeEmbeddedBytes) {
  const auto plugin = plugin_with({{"KYWD", 0x800, "KwWidget", {{"CNAM", std::string("\x10\x20\x30\x40", 4)}}}});
  const auto key = fo4::FormKey::parse("000800:Base.esm");
  const auto json = fo4::record_to_json(plugin, key);

  EXPECT_TRUE(contains(json, "\"formKey\":\"" + key.to_string() + "\""));
  EXPECT_TRUE(contains(json, "\"signature\":\"KYWD\""));
  EXPECT_TRUE(contains(json, "\"editorId\":\"KwWidget\""));
  EXPECT_TRUE(contains(json, "\"sig\":\"CNAM\",\"fields\":["));
  EXPECT_TRUE(contains(json, "\"name\":\"Color red\""));
  EXPECT_TRUE(contains(json, "\"value\":\"16\""));
  EXPECT_TRUE(contains(json, "\"name\":\"Color alpha\""));
  EXPECT_TRUE(contains(json, "\"value\":\"64\""));
}

TEST(RecordJson, MiscDestructionRowDecodesHealth) {
  const auto plugin = plugin_with({{"MISC", 0x802, "MiscBolt", {{"DEST", std::string("\x64\0\0\0\0\0\0\0", 8)}}}});
  const auto json = fo4::record_to_json(plugin, fo4::FormKey::parse("000802:Base.esm"));

  EXPECT_TRUE(contains(json, "\"signature\":\"MISC\""));
  EXPECT_TRUE(contains(json, "\"name\":\"Destruction health\""));
  EXPECT_TRUE(contains(json, "\"value\":\"100\""));
}

TEST(RecordJson, UnmappedSubrecordFallsBackToRawHex) {
  const auto plugin = plugin_with({{"MISC", 0x802, "MiscBolt", {{"ZZZZ", std::string("\xAB\xCD", 2)}}}});
  const auto json = fo4::record_to_json(plugin, fo4::FormKey::parse("000802:Base.esm"));

  EXPECT_TRUE(contains(json, "{\"sig\":\"ZZZZ\",\"fields\":[],\"raw\":\"abcd\"}"));
}

TEST(RecordJson, RawFallbackCanBeDisabled) {
  const auto plugin = plugin_with({{"MISC", 0x802, "MiscBolt", {{"ZZZZ", std::string("\xAB\xCD", 2)}}}});
  const auto json =
      fo4::record_to_json(plugin, fo4::FormKey::parse("000802:Base.esm"), fo4::RecordJsonOptions{.include_raw = false});

  EXPECT_TRUE(contains(json, "{\"sig\":\"ZZZZ\",\"fields\":[]}"));
  EXPECT_FALSE(contains(json, "\"raw\""));
}

TEST(RecordJson, LocalizedFullReportsStringId) {
  const auto plugin =
      plugin_with({{"KYWD", 0x800, "KwWidget", {{"FULL", std::string("\x04\x03\x02\x01", 4)}}}}, 0x80);
  const auto json = fo4::record_to_json(plugin, fo4::FormKey::parse("000800:Base.esm"));

  EXPECT_TRUE(contains(json, "\"sig\":\"FULL\",\"fields\":[{\"name\":\"FULL\""));
  EXPECT_TRUE(contains(json, "\"value\":\"\""));
  EXPECT_TRUE(contains(json, "\"stringId\":16909060"));
}

TEST(RecordJson, InlineFullTextWhenPluginIsNotLocalized) {
  const auto plugin = plugin_with({{"KYWD", 0x800, "KwWidget", {{"FULL", zstr("Shiny Gadget")}}}});
  const auto json = fo4::record_to_json(plugin, fo4::FormKey::parse("000800:Base.esm"));

  EXPECT_TRUE(contains(json, "\"value\":\"Shiny Gadget\""));
  EXPECT_FALSE(contains(json, "\"stringId\""));
}

TEST(RecordJson, Windows1252InlineTextIsEmittedAsValidUtf8) {
  const auto plugin = plugin_with({{"KYWD", 0x800, "KwWidget", {{"FULL", zstr("Caf\xE9 \x92s")}}}});
  const auto json = fo4::record_to_json(plugin, fo4::FormKey::parse("000800:Base.esm"));

  EXPECT_TRUE(contains(json, "\"value\":\"Caf\xC3\xA9 \xE2\x80\x99s\""));
  EXPECT_NO_THROW(EXPECT_TRUE(nlohmann::json::parse(json).is_object()));
}

TEST(RecordJson, ValidUtf8InlineTextIsEmittedUnchanged) {
  const auto plugin = plugin_with({{"KYWD", 0x800, "KwWidget", {{"FULL", zstr("Caf\xC3\xA9 \xE2\x80\x99s")}}}});
  const auto json = fo4::record_to_json(plugin, fo4::FormKey::parse("000800:Base.esm"));

  EXPECT_TRUE(contains(json, "\"value\":\"Caf\xC3\xA9 \xE2\x80\x99s\""));
  EXPECT_FALSE(contains(json, "\xC3\x83"));
  EXPECT_NO_THROW(EXPECT_TRUE(nlohmann::json::parse(json).is_object()));
}

TEST(RecordJson, Windows1252EditorIdIsEmittedAsValidUtf8) {
  const auto plugin = plugin_with({{"KYWD", 0x800, "Caf\xE9Kw", {}}});
  const auto json = fo4::record_to_json(plugin, fo4::FormKey::parse("000800:Base.esm"));

  EXPECT_TRUE(contains(json, "\"editorId\":\"Caf\xC3\xA9Kw\""));
  EXPECT_NO_THROW(EXPECT_TRUE(nlohmann::json::parse(json).is_object()));
}

TEST(RecordJson, StringValueEscapesQuotesBackslashesNewlinesAndControlBytes) {
  const std::string text = std::string("Say \"hi\" \\ now\nline\x01") + "\x7F";
  const auto plugin = plugin_with({{"KYWD", 0x800, "KwWidget", {{"FULL", zstr(text)}}}});
  const auto json = fo4::record_to_json(plugin, fo4::FormKey::parse("000800:Base.esm"));

  EXPECT_TRUE(contains(json, "\"value\":\"Say \\\"hi\\\" \\\\ now\\nline\\u0001\x7F\""));
  const auto parsed = nlohmann::json::parse(json);
  std::string value;
  for (const auto& sub : parsed.at("subrecords")) {
    if (sub.at("sig").get<std::string>() != "FULL") continue;
    value = sub.at("fields").at(0).at("value").get<std::string>();
  }
  EXPECT_EQ(value, text);
}

TEST(RecordJson, UnknownRecordIsOutOfRange) {
  const auto plugin = plugin_with({{"KYWD", 0x800, "KwWidget", {}}});
  EXPECT_THROW(fo4::record_to_json(plugin, fo4::FormKey::parse("000900:Base.esm")), std::out_of_range);
}
