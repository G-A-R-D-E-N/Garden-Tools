#include "fo4/plugin/mutation.hpp"
#include "fo4/plugin/reader.hpp"
#include "fo4/plugin/writer.hpp"
#include "fo4/schema/formlink_lists.hpp"
#include "fo4/schema/scalar.hpp"
#include "plugin_bytes.hpp"

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <string_view>
#include <utility>
#include <vector>

namespace {

using Bytes = std::vector<std::byte>;
using fo4::test::ByteWriter;

constexpr std::uint32_t k_retarget_raw = 0x01000A20;

Bytes u32_bytes(std::uint32_t value) {
  ByteWriter out;
  out.u32(value);
  return out.bytes;
}

Bytes llkc_payload(std::initializer_list<std::pair<std::uint32_t, std::uint32_t>> entries) {
  ByteWriter out;
  for (const auto& [keyword, chance] : entries) {
    out.u32(keyword);
    out.u32(chance);
  }
  return out.bytes;
}

ByteWriter make_plugin() {
  ByteWriter out;
  const auto tes4 = fo4::test::begin_record(out, "TES4", 0, 0);
  ByteWriter hedr;
  hedr.f32(1.0f);
  hedr.u32(4);
  hedr.u32(0x900);
  fo4::test::write_subrecord(out, "HEDR", hedr.bytes);
  fo4::test::write_subrecord(out, "MAST", std::string_view{"Fallout4.esm\0", 13});
  fo4::test::write_subrecord(out, "DATA", Bytes(8));
  fo4::test::write_subrecord(out, "MAST", std::string_view{"Objects.esp\0", 12});
  fo4::test::write_subrecord(out, "DATA", Bytes(8));
  fo4::test::end_record(out, tes4);

  const auto ltex_group = fo4::test::begin_group(out, "LTEX", 0);
  const auto ltex = fo4::test::begin_record(out, "LTEX", 0, 0x02000A01);
  fo4::test::write_subrecord(out, "EDID", std::string_view{"Radicle\0", 8});
  fo4::test::write_subrecord(out, "TNAM", u32_bytes(0x00000A10));
  fo4::test::write_subrecord(out, "MNAM", u32_bytes(0x00000B10));
  fo4::test::write_subrecord(out, "HNAM", Bytes{std::byte{30}, std::byte{30}});
  fo4::test::write_subrecord(out, "SNAM", Bytes{std::byte{30}});
  fo4::test::end_record(out, ltex);
  fo4::test::end_group(out, ltex_group);

  const auto lvli_group = fo4::test::begin_group(out, "LVLI", 0);
  const auto lvli = fo4::test::begin_record(out, "LVLI", 0, 0x02000C01);
  fo4::test::write_subrecord(out, "EDID", std::string_view{"Radicle\0", 8});
  fo4::test::write_subrecord(out, "LLKC", llkc_payload({{0x00000C10u, 5u}, {0x00000C11u, 6u}}));
  fo4::test::end_record(out, lvli);
  fo4::test::end_group(out, lvli_group);

  const auto lvln_group = fo4::test::begin_group(out, "LVLN", 0);
  const auto lvln = fo4::test::begin_record(out, "LVLN", 0, 0x02000D01);
  fo4::test::write_subrecord(out, "EDID", std::string_view{"Radicle\0", 8});
  fo4::test::write_subrecord(out, "LLKC", llkc_payload({{0x00000D10u, 3u}, {0x00000D11u, 4u}}));
  fo4::test::end_record(out, lvln);
  fo4::test::end_group(out, lvln_group);
  return out;
}

Bytes subrecord_data(const fo4::Plugin& plugin, std::string_view key, std::string_view subrecord) {
  const auto* record = fo4::find_record(plugin, fo4::FormKey::parse(key));
  if (record == nullptr) {
    ADD_FAILURE() << key;
    return {};
  }
  for (const auto& sub : record->subrecords) {
    if (sub.signature.to_string() == subrecord) return sub.data;
  }
  ADD_FAILURE() << subrecord;
  return {};
}

fo4::schema::FormLinkFieldDescriptor formlink_row(std::string_view record, std::string_view path) {
  const auto field =
      fo4::schema::formlink_field_descriptor(fo4::Signature::from_string(record), path);
  EXPECT_TRUE(field.has_value()) << path;
  return field.value_or(fo4::schema::FormLinkFieldDescriptor{});
}

fo4::schema::ScalarFieldDescriptor scalar(std::string_view record, std::string_view path) {
  const auto field = fo4::schema::scalar_field_descriptor(fo4::Signature::from_string(record), path);
  EXPECT_TRUE(field.has_value()) << path;
  return field.value_or(fo4::schema::ScalarFieldDescriptor{});
}

}  // namespace

TEST(WP9LtexLlkcRows, NewRowsMatchXEditShape) {
  const auto texture_set = formlink_row("LTEX", "LTEX\\TextureSet");
  EXPECT_EQ(texture_set.subrecord, fo4::Signature::from_string("TNAM"));
  EXPECT_FALSE(texture_set.nullable);
  EXPECT_EQ(texture_set.payload_sizes[0], 4u);
  EXPECT_TRUE(formlink_row("LTEX", "LTEX\\MaterialType").nullable);

  const auto friction = scalar("LTEX", "LTEX\\Friction");
  EXPECT_EQ(friction.scalar.subrecord, fo4::Signature::from_string("HNAM"));
  EXPECT_EQ(friction.scalar.storage, fo4::schema::ScalarStorage::u8);
  EXPECT_EQ(friction.scalar.payload_size, 2u);
  EXPECT_EQ(scalar("LTEX", "LTEX\\Restitution").scalar.offset, 1u);
  EXPECT_EQ(scalar("LTEX", "LTEX\\SpecularExponent").scalar.subrecord,
            fo4::Signature::from_string("SNAM"));

  for (const auto* record : {"LVLI", "LVLN"}) {
    SCOPED_TRACE(record);
    const auto keyword = formlink_row(record, std::string(record) + "\\FilterKeyword");
    EXPECT_EQ(keyword.subrecord, fo4::Signature::from_string("LLKC"));
    EXPECT_FALSE(keyword.nullable);
    EXPECT_EQ(keyword.address.stride, 8u);
    const auto chance = scalar(record, std::string(record) + "\\FilterChance");
    EXPECT_EQ(chance.scalar.storage, fo4::schema::ScalarStorage::u32);
    EXPECT_EQ(chance.scalar.offset, 4u);
    EXPECT_EQ(chance.scalar.address.stride, 8u);
  }
}

TEST(WP9LtexLlkcRows, LtexRowsReadAndWriteTheirSubrecords) {
  const auto out = make_plugin();
  auto plugin = fo4::read_plugin(out.bytes, "Patch.esp");
  const auto key = fo4::FormKey::parse("000A01:Patch.esp");
  EXPECT_EQ(fo4::set_record_formlink_field(plugin, key, formlink_row("LTEX", "LTEX\\TextureSet"),
                                           fo4::FormKey::parse("000A20:Objects.esp")),
            fo4::FormLinkFieldMutationStatus::updated);
  EXPECT_EQ(fo4::set_record_scalar(plugin, key, scalar("LTEX", "LTEX\\Friction"), 0, "42"),
            fo4::ScalarMutationStatus::updated);
  EXPECT_EQ(fo4::set_record_scalar(plugin, key, scalar("LTEX", "LTEX\\Restitution"), 0, "7"),
            fo4::ScalarMutationStatus::updated);
  EXPECT_EQ(fo4::set_record_scalar(plugin, key, scalar("LTEX", "LTEX\\SpecularExponent"), 0, "9"),
            fo4::ScalarMutationStatus::updated);

  const auto reread = fo4::read_plugin(fo4::write_plugin(plugin), "Patch.esp");
  EXPECT_EQ(subrecord_data(reread, "000A01:Patch.esp", "TNAM"), u32_bytes(k_retarget_raw));
  EXPECT_EQ(subrecord_data(reread, "000A01:Patch.esp", "HNAM"),
            (Bytes{std::byte{42}, std::byte{7}}));
  EXPECT_EQ(subrecord_data(reread, "000A01:Patch.esp", "SNAM"), Bytes{std::byte{9}});
  EXPECT_EQ(subrecord_data(reread, "000A01:Patch.esp", "MNAM"), u32_bytes(0x00000B10));
}

TEST(WP9LtexLlkcRows, MaterialTypeAllowsNullAndTextureSetRefusesIt) {
  const auto out = make_plugin();
  auto plugin = fo4::read_plugin(out.bytes, "Patch.esp");
  const auto key = fo4::FormKey::parse("000A01:Patch.esp");
  EXPECT_EQ(fo4::set_record_formlink_field(plugin, key, formlink_row("LTEX", "LTEX\\MaterialType"),
                                           fo4::FormKey{}),
            fo4::FormLinkFieldMutationStatus::updated);
  EXPECT_THROW(fo4::set_record_formlink_field(plugin, key, formlink_row("LTEX", "LTEX\\TextureSet"),
                                              fo4::FormKey{}),
               fo4::schema::FormLinkFieldError);

  const auto reread = fo4::read_plugin(fo4::write_plugin(plugin), "Patch.esp");
  EXPECT_EQ(subrecord_data(reread, "000A01:Patch.esp", "MNAM"), u32_bytes(0));
  EXPECT_EQ(subrecord_data(reread, "000A01:Patch.esp", "TNAM"), u32_bytes(0x00000A10));
}

TEST(WP9LtexLlkcRows, LeveledListKeywordRetargetsEntryIndexOneAndChanceIsPerEntry) {
  const auto out = make_plugin();
  auto plugin = fo4::read_plugin(out.bytes, "Patch.esp");
  const std::pair<std::string_view, std::string_view> lists[] = {{"LVLI", "000C01:Patch.esp"},
                                                                 {"LVLN", "000D01:Patch.esp"}};
  for (const auto& [record, key_text] : lists) {
    SCOPED_TRACE(record);
    const auto key = fo4::FormKey::parse(key_text);
    const auto keyword = formlink_row(record, std::string(record) + "\\FilterKeyword");
    const auto chance = scalar(record, std::string(record) + "\\FilterChance");
    EXPECT_EQ(fo4::set_record_formlink_field(plugin, key, keyword, 2,
                                             fo4::FormKey::parse("000A20:Objects.esp")),
              fo4::FormLinkFieldMutationStatus::subrecord_missing);
    EXPECT_EQ(fo4::set_record_formlink_field(plugin, key, keyword, 1,
                                             fo4::FormKey::parse("000A20:Objects.esp")),
              fo4::FormLinkFieldMutationStatus::updated);
    EXPECT_EQ(fo4::set_record_scalar(plugin, key, chance, 1, "9"),
              fo4::ScalarMutationStatus::updated);
  }

  const auto reread = fo4::read_plugin(fo4::write_plugin(plugin), "Patch.esp");
  EXPECT_EQ(subrecord_data(reread, "000C01:Patch.esp", "LLKC"),
            llkc_payload({{0x00000C10u, 5u}, {k_retarget_raw, 9u}}));
  EXPECT_EQ(subrecord_data(reread, "000D01:Patch.esp", "LLKC"),
            llkc_payload({{0x00000D10u, 3u}, {k_retarget_raw, 9u}}));
}
