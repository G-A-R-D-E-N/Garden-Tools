#include "fo4/forms/faction.hpp"
#include "fo4/plugin/reader.hpp"
#include "fo4/plugin/writer.hpp"
#include "plugin_bytes.hpp"

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace {

std::vector<std::byte> make_plugin(std::uint32_t tes4_flags) {
  fo4::test::ByteWriter out;
  const auto tes4 = fo4::test::begin_record(out, "TES4", tes4_flags, 0);
  fo4::test::ByteWriter hedr;
  hedr.f32(1.0f);
  hedr.u32(0);
  hedr.u32(0x800);
  fo4::test::write_subrecord(out, "HEDR", hedr.bytes);
  fo4::test::end_record(out, tes4);
  return out.bytes;
}

std::vector<std::byte> zstring_bytes(std::string_view value) {
  std::vector<std::byte> out;
  for (const char ch : value) out.push_back(std::byte{static_cast<std::uint8_t>(ch)});
  out.push_back(std::byte{0});
  return out;
}

std::vector<std::string> signature_names(const fo4::Record& record) {
  std::vector<std::string> names;
  for (const auto& subrecord : record.subrecords) names.push_back(subrecord.signature.to_string());
  return names;
}

}  // namespace

TEST(FactionCreation, CreatesNamedFactionAndReadsItBack) {
  auto plugin = fo4::read_plugin(make_plugin(0), "Patch.esp");
  const auto key = fo4::forms::create_faction(plugin, std::string_view{"CreatedFaction"},
                                              std::string_view{"Created Faction"}, 0x81u);
  EXPECT_EQ(key, fo4::FormKey::parse("000800:Patch.esp"));
  EXPECT_EQ(plugin.header.next_form_id, 0x801u);

  const auto reread = fo4::read_plugin(fo4::write_plugin(plugin), "Patch.esp");
  const auto* record = fo4::find_record(reread, key);
  ASSERT_NE(record, nullptr);
  EXPECT_EQ(record->header.signature.to_string(), "FACT");
  EXPECT_EQ(signature_names(*record),
            (std::vector<std::string>{"EDID", "FULL", "DATA", "CRVA", "VENV"}));
  EXPECT_EQ(record->subrecords[0].data, zstring_bytes("CreatedFaction"));
  EXPECT_EQ(record->subrecords[1].data, zstring_bytes("Created Faction"));
  EXPECT_EQ(record->subrecords[2].data,
            (std::vector<std::byte>{std::byte{0x81}, std::byte{0}, std::byte{0}, std::byte{0}}));
  EXPECT_EQ(record->subrecords[3].data.size(), 20u);
  EXPECT_EQ(record->subrecords[4].data.size(), 12u);

  EXPECT_TRUE(fo4::forms::decode_faction_relations(reread, key).empty());
  EXPECT_TRUE(fo4::forms::decode_faction_ranks(reread, key).empty());
}

TEST(FactionCreation, UnnamedFactionHasOnlyRequiredSubrecords) {
  auto plugin = fo4::read_plugin(make_plugin(0), "Patch.esp");
  const auto key = fo4::forms::create_faction(plugin, std::nullopt, std::nullopt, 0u);

  const auto reread = fo4::read_plugin(fo4::write_plugin(plugin), "Patch.esp");
  const auto* record = fo4::find_record(reread, key);
  ASSERT_NE(record, nullptr);
  EXPECT_EQ(signature_names(*record), (std::vector<std::string>{"DATA", "CRVA", "VENV"}));
  EXPECT_TRUE(fo4::forms::decode_faction_relations(reread, key).empty());
}

TEST(FactionCreation, RefusesUndefinedFlagsAndLocalizedFullName) {
  auto plugin = fo4::read_plugin(make_plugin(0), "Patch.esp");
  EXPECT_THROW(static_cast<void>(fo4::forms::create_faction(
                   plugin, std::nullopt, std::nullopt, 0x00020000u)),
               fo4::forms::FactionError);
  EXPECT_EQ(plugin.header.next_form_id, 0x800u);
  EXPECT_TRUE(plugin.groups.empty());

  auto localized = fo4::read_plugin(make_plugin(0x80), "Patch.esp");
  EXPECT_THROW(static_cast<void>(fo4::forms::create_faction(
                   localized, std::nullopt, std::string_view{"Named"}, 0u)),
               fo4::forms::FactionError);
  EXPECT_EQ(localized.header.next_form_id, 0x800u);
  EXPECT_NO_THROW(static_cast<void>(
      fo4::forms::create_faction(localized, std::nullopt, std::nullopt, 0u)));
}

TEST(FactionCreation, RefusesEmbeddedNulInEditorIdOrFullName) {
  auto plugin = fo4::read_plugin(make_plugin(0), "Patch.esp");
  EXPECT_THROW(static_cast<void>(fo4::forms::create_faction(
                   plugin, std::string_view{"A\0B", 3}, std::nullopt, 0u)),
               fo4::forms::FactionError);
  EXPECT_THROW(static_cast<void>(fo4::forms::create_faction(
                   plugin, std::nullopt, std::string_view{"A\0B", 3}, 0u)),
               fo4::forms::FactionError);
  EXPECT_EQ(plugin.header.next_form_id, 0x800u);
  EXPECT_TRUE(plugin.groups.empty());
}
