#include "fo4/schema/localized_fields.hpp"

#include "plugin_bytes.hpp"
#include "fo4/plugin/reader.hpp"

#include <gtest/gtest.h>

namespace {

constexpr auto k_race = fo4::Signature::from_string("RACE");
constexpr auto k_npc = fo4::Signature::from_string("NPC_");
constexpr auto k_desc = fo4::Signature::from_string("DESC");
constexpr auto k_full = fo4::Signature::from_string("FULL");
constexpr auto k_cnam = fo4::Signature::from_string("CNAM");
constexpr auto k_edid = fo4::Signature::from_string("EDID");

fo4::Subrecord make(fo4::Signature signature, std::vector<std::byte> data) {
  fo4::Subrecord sub;
  sub.signature = signature;
  sub.data = std::move(data);
  return sub;
}

std::vector<std::byte> text(std::string_view value) {
  std::vector<std::byte> out(value.size() + 1);
  for (std::size_t i = 0; i < value.size(); ++i) {
    out[i] = std::byte{static_cast<std::uint8_t>(value[i])};
  }
  return out;
}

std::vector<std::byte> id(std::uint32_t value) {
  std::vector<std::byte> out(4);
  std::memcpy(out.data(), &value, 4);
  return out;
}

fo4::Plugin plugin_with_flags(std::uint32_t tes4_flags) {
  fo4::test::ByteWriter out;
  const auto tes4 = fo4::test::begin_record(out, "TES4", tes4_flags, 0);
  fo4::test::ByteWriter hedr;
  hedr.f32(1.0f);
  hedr.u32(0);
  hedr.u32(0x800);
  fo4::test::write_subrecord(out, "HEDR", hedr.bytes);
  fo4::test::end_record(out, tes4);
  return fo4::read_plugin(out.bytes, "Test.esp");
}

}  // namespace

// Membership is declared per (record, subrecord); size never decides it.
TEST(LocalizedFields, MembershipIsPerRecordAndSubrecord) {
  EXPECT_TRUE(fo4::schema::is_localized_field(k_race, k_desc));
  EXPECT_TRUE(fo4::schema::is_localized_field(k_race, k_full));
  EXPECT_FALSE(fo4::schema::is_localized_field(k_race, k_edid));
  // NPC_ CNAM is four bytes in every shipped instance but is a Class FormID,
  // so it must never be treated as a string ID.
  EXPECT_FALSE(fo4::schema::is_localized_field(k_npc, k_cnam));
}

TEST(LocalizedFields, LocalizedPluginYieldsStringIds) {
  const auto plugin = plugin_with_flags(0x80);
  const auto value =
      fo4::schema::read_localized_string(plugin, k_race, k_desc, make(k_desc, id(0x1234)));
  ASSERT_TRUE(value.localized());
  EXPECT_EQ(*value.id, 0x1234u);
  EXPECT_FALSE(value.text.has_value());
  EXPECT_EQ(fo4::schema::write_localized_string(value), id(0x1234));
}

TEST(LocalizedFields, NonLocalizedPluginYieldsInlineText) {
  const auto plugin = plugin_with_flags(0);
  const auto value =
      fo4::schema::read_localized_string(plugin, k_race, k_desc, make(k_desc, text("hello")));
  EXPECT_FALSE(value.localized());
  EXPECT_EQ(value.inline_text(), "hello");
  EXPECT_EQ(fo4::schema::write_localized_string(value), text("hello"));
}

TEST(LocalizedFields, BitSevenSelectsRepresentationForTheSameFourBytes) {
  const auto payload = make(k_full, text("abc"));
  const auto inline_plugin = plugin_with_flags(0);
  const auto external_plugin = plugin_with_flags(0x80);
  const auto inline_value =
      fo4::schema::read_localized_string(inline_plugin, k_race, k_full, payload);
  const auto external_value =
      fo4::schema::read_localized_string(external_plugin, k_race, k_full, payload);
  EXPECT_EQ(inline_value.inline_text(), "abc");
  EXPECT_FALSE(inline_value.localized());
  ASSERT_TRUE(external_value.localized());
  EXPECT_EQ(*external_value.id, 0x00636261u);
  EXPECT_FALSE(external_value.text.has_value());
  EXPECT_EQ(fo4::schema::write_localized_string(inline_value), payload.data);
  EXPECT_EQ(fo4::schema::write_localized_string(external_value), payload.data);
  EXPECT_EQ(inline_plugin.header.tes4_flags.value(), 0u);
  EXPECT_EQ(external_plugin.header.tes4_flags.value(), 0x80u);
}

TEST(LocalizedFields, ZeroExternalIdIsPreservedWithoutTextLookup) {
  const auto plugin = plugin_with_flags(0x80);
  const auto value =
      fo4::schema::read_localized_string(plugin, k_race, k_desc, make(k_desc, id(0)));
  ASSERT_TRUE(value.localized());
  EXPECT_EQ(*value.id, 0u);
  EXPECT_FALSE(value.text.has_value());
  EXPECT_EQ(fo4::schema::write_localized_string(value), id(0));
}

// A non-lstring field stays inline even in a localized plugin.
TEST(LocalizedFields, NonLocalizedFieldStaysInlineInALocalizedPlugin) {
  const auto plugin = plugin_with_flags(0x80);
  const auto value =
      fo4::schema::read_localized_string(plugin, k_race, k_edid, make(k_edid, text("EdId")));
  EXPECT_FALSE(value.localized());
  EXPECT_EQ(value.inline_text(), "EdId");
}

TEST(LocalizedFields, NonFourByteLocalizedPayloadHasNoIdAndRoundTrips) {
  const auto plugin = plugin_with_flags(0x80);
  const std::vector<std::vector<std::byte>> payloads{
      {std::byte{0x34}, std::byte{0x12}},
      {std::byte{'n'}, std::byte{'o'}, std::byte{'p'}, std::byte{'e'}, std::byte{0}},
  };
  for (const auto& payload : payloads) {
    const auto value =
        fo4::schema::read_localized_string(plugin, k_race, k_desc, make(k_desc, payload));
    EXPECT_FALSE(value.localized());
    EXPECT_FALSE(value.id.has_value());
    EXPECT_FALSE(value.text.has_value());
    EXPECT_EQ(fo4::schema::write_localized_string(value), payload);
  }
}

TEST(LocalizedFields, InlineTextMustBeSingleNulTerminated) {
  const auto plugin = plugin_with_flags(0);
  EXPECT_THROW(
      static_cast<void>(fo4::schema::read_localized_string(
          plugin, k_race, k_desc, make(k_desc, std::vector<std::byte>{std::byte{'a'}}))),
      std::exception);
}
