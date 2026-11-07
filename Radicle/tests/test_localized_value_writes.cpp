#include "fo4/plugin/mutation.hpp"

#include "fo4/localization/strings.hpp"
#include "fo4/plugin/reader.hpp"
#include "fo4/plugin/writer.hpp"
#include "fo4/schema/localized_fields.hpp"
#include "plugin_bytes.hpp"

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
#include <string_view>
#include <vector>

namespace {

constexpr auto k_race = fo4::Signature::from_string("RACE");
constexpr auto k_desc = fo4::Signature::from_string("DESC");
constexpr auto k_edid = fo4::Signature::from_string("EDID");
constexpr auto k_key = "000800:Patch.esp";

std::vector<std::byte> inline_text(std::string_view value) {
  std::vector<std::byte> out(value.size() + 1);
  for (std::size_t i = 0; i < value.size(); ++i) {
    out[i] = std::byte{static_cast<std::uint8_t>(value[i])};
  }
  return out;
}

std::vector<std::byte> string_id(std::uint32_t value) {
  std::vector<std::byte> out(4);
  std::memcpy(out.data(), &value, 4);
  return out;
}

std::vector<std::byte> localized_plugin(std::uint32_t tes4_flags,
                                        std::size_t desc_count,
                                        std::vector<std::byte> desc) {
  fo4::test::ByteWriter out;
  const auto tes4 = fo4::test::begin_record(out, "TES4", tes4_flags, 0);
  fo4::test::ByteWriter hedr;
  hedr.f32(1.0f);
  hedr.u32(0);
  hedr.u32(0x800);
  fo4::test::write_subrecord(out, "HEDR", hedr.bytes);
  fo4::test::end_record(out, tes4);
  const auto group = fo4::test::begin_group(out, "RACE", 0);
  const auto record = fo4::test::begin_record(out, "RACE", 0, 0x800);
  fo4::test::write_subrecord(out, "EDID", inline_text("RadicleRace"));
  for (std::size_t i = 0; i < desc_count; ++i) {
    fo4::test::write_subrecord(out, "DESC", desc);
  }
  fo4::test::end_record(out, record);
  fo4::test::end_group(out, group);
  return out.bytes;
}

fo4::Plugin read_localized(std::uint32_t tes4_flags,
                           std::size_t desc_count,
                           std::vector<std::byte> desc) {
  return fo4::read_plugin(localized_plugin(tes4_flags, desc_count, std::move(desc)),
                          "Patch.esp");
}

std::uint32_t stored_id(const fo4::Plugin& plugin, std::size_t desc_index) {
  const auto* record = fo4::find_record(plugin, fo4::FormKey::parse(k_key));
  if (record == nullptr) return 0xFFFFFFFFu;
  std::size_t ordinal = 0;
  for (const auto& subrecord : record->subrecords) {
    if (subrecord.signature != k_desc) continue;
    if (ordinal == desc_index) {
      std::uint32_t value = 0;
      if (subrecord.data.size() != 4) return 0xFFFFFFFFu;
      std::memcpy(&value, subrecord.data.data(), 4);
      return value;
    }
    ++ordinal;
  }
  return 0xFFFFFFFFu;
}

std::string entry_text(const fo4::localization::LocalizedStringTable& table, std::uint32_t id) {
  const auto* entry = fo4::localization::find_string(table, id);
  if (entry == nullptr) return "<missing>";
  std::string out;
  out.reserve(entry->text.size());
  for (const auto value : entry->text) {
    out.push_back(static_cast<char>(std::to_integer<std::uint8_t>(value)));
  }
  return out;
}

}  // namespace

TEST(LocalizedValueWrites, AddStringAllocatesPastTheHighestExistingId) {
  fo4::localization::LocalizedStringTable table;
  table.kind = fo4::localization::StringTableKind::strings;
  EXPECT_EQ(fo4::localization::next_string_id(table), 1u);
  EXPECT_EQ(fo4::localization::add_string(table, "first"), 1u);
  EXPECT_EQ(fo4::localization::add_string(table, "second"), 2u);

  fo4::localization::LocalizedStringTable sparse;
  sparse.entries.push_back({7, {}});
  sparse.entries.push_back({2, {}});
  EXPECT_EQ(fo4::localization::next_string_id(sparse), 8u);
  EXPECT_EQ(fo4::localization::add_string(sparse, "third"), 8u);
  EXPECT_EQ(sparse.entries.size(), 3u);
}

TEST(LocalizedValueWrites, AddStringRefusesEmptyTextAndEmbeddedNul) {
  fo4::localization::LocalizedStringTable table;
  EXPECT_EQ(fo4::localization::add_string(table, ""), 0u);
  EXPECT_TRUE(table.entries.empty());

  const std::string with_nul("a\0b", 3);
  EXPECT_THROW(fo4::localization::add_string(table, with_nul),
               fo4::localization::StringTableError);
  EXPECT_TRUE(table.entries.empty());
}

TEST(LocalizedValueWrites, AddStringThrowsWhenTheIdSpaceIsExhausted) {
  fo4::localization::LocalizedStringTable table;
  table.entries.push_back({0xFFFFFFFFu, {}});
  EXPECT_EQ(fo4::localization::next_string_id(table), 0u);
  EXPECT_THROW(fo4::localization::add_string(table, "overflow"),
               fo4::localization::StringTableError);
  EXPECT_EQ(table.entries.size(), 1u);
}

TEST(LocalizedValueWrites, StoresTheAllocatedIdInTheRecord) {
  auto plugin = read_localized(0x80, 1, string_id(0));
  fo4::localization::LocalizedStringTable table;
  table.kind = fo4::localization::StringTableKind::strings;
  table.entries.push_back({4, {}});

  const auto status = fo4::add_record_localized_string(
      plugin, fo4::FormKey::parse(k_key), k_desc, 0, "Radicle race", table);
  EXPECT_EQ(status, fo4::LocalizedValueStatus::updated);
  EXPECT_EQ(stored_id(plugin, 0), 5u);
  EXPECT_EQ(entry_text(table, 5u), "Radicle race");

  const auto reread = fo4::read_plugin(fo4::write_plugin(plugin), "Patch.esp");
  EXPECT_EQ(stored_id(reread, 0), 5u);
  const auto* record = fo4::find_record(reread, fo4::FormKey::parse(k_key));
  ASSERT_NE(record, nullptr);
  for (const auto& subrecord : record->subrecords) {
    if (subrecord.signature != k_desc) continue;
    const auto value = fo4::schema::read_localized_string(reread, k_race, k_desc, subrecord);
    ASSERT_TRUE(value.localized());
    EXPECT_EQ(*value.id, 5u);
  }
}

TEST(LocalizedValueWrites, SecondDescInstanceIsTheOneThatChanges) {
  auto plugin = read_localized(0x80, 2, string_id(3));
  fo4::localization::LocalizedStringTable table;
  EXPECT_EQ(fo4::add_record_localized_string(plugin, fo4::FormKey::parse(k_key), k_desc, 1,
                                             "second", table),
            fo4::LocalizedValueStatus::updated);
  EXPECT_EQ(stored_id(plugin, 0), 3u);
  EXPECT_EQ(stored_id(plugin, 1), 1u);
}

TEST(LocalizedValueWrites, RefusalsLeaveBytesAndTheTableAlone) {
  const auto key = fo4::FormKey::parse(k_key);

  auto inline_plugin = read_localized(0, 1, inline_text("Inline"));
  fo4::localization::LocalizedStringTable table;
  EXPECT_EQ(fo4::add_record_localized_string(inline_plugin, key, k_desc, 0, "text", table),
            fo4::LocalizedValueStatus::plugin_not_localized);
  EXPECT_TRUE(table.entries.empty());
  EXPECT_EQ(stored_id(inline_plugin, 0), 0xFFFFFFFFu);

  auto plugin = read_localized(0x80, 1, string_id(0));
  EXPECT_EQ(fo4::add_record_localized_string(plugin, key, k_edid, 0, "text", table),
            fo4::LocalizedValueStatus::field_not_localized);
  EXPECT_EQ(fo4::add_record_localized_string(plugin, fo4::FormKey::parse("000900:Patch.esp"),
                                             k_desc, 0, "text", table),
            fo4::LocalizedValueStatus::record_missing);
  EXPECT_EQ(fo4::add_record_localized_string(plugin, key, k_desc, 1, "text", table),
            fo4::LocalizedValueStatus::subrecord_missing);
  EXPECT_EQ(fo4::add_record_localized_string(plugin, key, k_desc, 0, "", table),
            fo4::LocalizedValueStatus::empty_text);
  EXPECT_TRUE(table.entries.empty());
  EXPECT_EQ(stored_id(plugin, 0), 0u);

  auto short_payload = read_localized(0x80, 1, std::vector<std::byte>{std::byte{1}});
  EXPECT_EQ(
      fo4::add_record_localized_string(short_payload, key, k_desc, 0, "text", table),
      fo4::LocalizedValueStatus::payload_not_an_id);
  EXPECT_TRUE(table.entries.empty());
}

TEST(LocalizedValueWrites, WritingTheSameIdBackIsUnchanged) {
  auto plugin = read_localized(0x80, 1, string_id(1));
  fo4::localization::LocalizedStringTable table;
  table.entries.push_back({0, {}});
  EXPECT_EQ(fo4::add_record_localized_string(plugin, fo4::FormKey::parse(k_key), k_desc, 0,
                                             "same", table),
            fo4::LocalizedValueStatus::unchanged);
  EXPECT_EQ(stored_id(plugin, 0), 1u);
  EXPECT_EQ(table.entries.size(), 2u);
}
