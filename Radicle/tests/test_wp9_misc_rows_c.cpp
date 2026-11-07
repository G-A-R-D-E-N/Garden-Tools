#include "fo4/plugin/mutation.hpp"
#include "fo4/plugin/reader.hpp"
#include "fo4/plugin/writer.hpp"
#include "fo4/schema/formlink_lists.hpp"
#include "fo4/schema/scalar.hpp"
#include "plugin_bytes.hpp"

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <string_view>
#include <utility>
#include <vector>

namespace {

using Bytes = std::vector<std::byte>;
using fo4::test::ByteWriter;

constexpr std::uint32_t k_retarget_raw = 0x01000A20;

struct Link {
  std::string_view record;
  std::string_view key;
  std::string_view path;
  std::string_view subrecord;
  std::string_view target;
  std::uint32_t initial_raw;
};

constexpr Link k_links[] = {
    {"STAT", "000F01:Patch.esp", "STAT\\PreviewTransform", "PTRN", "TRNS", 0x01000F10},
    {"STAT", "000F01:Patch.esp", "STAT\\ForcedLocRefType", "FTYP", "LCRT", 0x00000F20},
};

Bytes u32_bytes(std::uint32_t value) {
  ByteWriter out;
  out.u32(value);
  return out.bytes;
}

Bytes proj_dnam(std::uint32_t vats_raw) {
  ByteWriter out;
  out.bytes.resize(89, std::byte{0});
  out.u32(vats_raw);
  return out.bytes;
}

void write_record(ByteWriter& out, std::string_view record, std::uint32_t self_raw,
                  const std::vector<std::pair<std::string_view, Bytes>>& subrecords) {
  const auto group = fo4::test::begin_group(out, record, 0);
  const auto header = fo4::test::begin_record(out, record, 0, self_raw);
  fo4::test::write_subrecord(out, "EDID", std::string_view{"Radicle\0", 8});
  for (const auto& [signature, data] : subrecords) {
    fo4::test::write_subrecord(out, signature, data);
  }
  fo4::test::end_record(out, header);
  fo4::test::end_group(out, group);
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
  write_record(out, "PROJ", 0x02000E01, {{"DNAM", proj_dnam(0x00000E10)}});
  write_record(out, "STAT", 0x02000F01,
               {{"PTRN", u32_bytes(0x01000F10)}, {"FTYP", u32_bytes(0x00000F20)}});
  write_record(out, "SNDR", 0x02001001, {{"CNAM", u32_bytes(0x54651A43)}});
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

std::uint32_t u32_at(const Bytes& data, std::size_t offset) {
  std::uint32_t value = 0;
  for (std::size_t i = 0; i < 4; ++i) {
    value |= static_cast<std::uint32_t>(std::to_integer<std::uint8_t>(data.at(offset + i)))
             << (8 * i);
  }
  return value;
}

}  // namespace

TEST(WP9MiscRowsC, StatTransformAndLocRefRowsAreNonNullableLinks) {
  const auto out = make_plugin();
  auto plugin = fo4::read_plugin(out.bytes, "Patch.esp");
  for (const auto& link : k_links) {
    SCOPED_TRACE(link.path);
    const auto field =
        fo4::schema::formlink_field_descriptor(fo4::Signature::from_string(link.record), link.path);
    ASSERT_TRUE(field.has_value());
    EXPECT_EQ(field->subrecord, fo4::Signature::from_string(link.subrecord));
    EXPECT_EQ(field->offset, 0u);
    EXPECT_FALSE(field->nullable);
    const auto targets = fo4::schema::formlink_field_targets(*field);
    ASSERT_EQ(targets.size(), 1u);
    EXPECT_EQ(targets[0], fo4::Signature::from_string(link.target));
    EXPECT_EQ(u32_at(subrecord_data(plugin, link.key, link.subrecord), 0), link.initial_raw);
    EXPECT_THROW(fo4::set_record_formlink_field(plugin, fo4::FormKey::parse(link.key), *field,
                                                fo4::FormKey{}),
                 fo4::schema::FormLinkFieldError);
    EXPECT_EQ(fo4::set_record_formlink_field(plugin, fo4::FormKey::parse(link.key), *field,
                                             fo4::FormKey::parse("000A20:Objects.esp")),
              fo4::FormLinkFieldMutationStatus::updated);
  }
  const auto reread = fo4::read_plugin(fo4::write_plugin(plugin), "Patch.esp");
  for (const auto& link : k_links) {
    SCOPED_TRACE(link.path);
    EXPECT_EQ(u32_at(subrecord_data(reread, link.key, link.subrecord), 0), k_retarget_raw);
  }
}

TEST(WP9MiscRowsC, ProjVatsProjectileIsNullableAtOffset89) {
  const auto field = fo4::schema::formlink_field_descriptor(fo4::Signature::from_string("PROJ"),
                                                            "PROJ\\VATSProjectile");
  ASSERT_TRUE(field.has_value());
  EXPECT_EQ(field->subrecord, fo4::Signature::from_string("DNAM"));
  EXPECT_EQ(field->offset, 89u);
  EXPECT_TRUE(field->nullable);
  const auto targets = fo4::schema::formlink_field_targets(*field);
  ASSERT_EQ(targets.size(), 1u);
  EXPECT_EQ(targets[0], fo4::Signature::from_string("PROJ"));

  const auto out = make_plugin();
  auto plugin = fo4::read_plugin(out.bytes, "Patch.esp");
  EXPECT_EQ(u32_at(subrecord_data(plugin, "000E01:Patch.esp", "DNAM"), 89), 0x00000E10u);
  EXPECT_EQ(fo4::set_record_formlink_field(plugin, fo4::FormKey::parse("000E01:Patch.esp"), *field,
                                           fo4::FormKey{}),
            fo4::FormLinkFieldMutationStatus::updated);
  const auto reread = fo4::read_plugin(fo4::write_plugin(plugin), "Patch.esp");
  EXPECT_EQ(u32_at(subrecord_data(reread, "000E01:Patch.esp", "DNAM"), 89), 0u);
}

TEST(WP9MiscRowsC, SndrDescriptorTypeUsesXEditNames) {
  const auto field = fo4::schema::scalar_field_descriptor(fo4::Signature::from_string("SNDR"),
                                                          "SNDR\\DescriptorType");
  ASSERT_TRUE(field.has_value());
  EXPECT_EQ(field->scalar.subrecord, fo4::Signature::from_string("CNAM"));
  EXPECT_EQ(field->scalar.storage, fo4::schema::ScalarStorage::u32);
  EXPECT_EQ(field->scalar.presentation, fo4::schema::ScalarPresentation::enumeration);
  EXPECT_EQ(*fo4::schema::scalar_value_name(field->scalar, 0x1EEF540A), "Standard");
  EXPECT_EQ(*fo4::schema::scalar_value_name(field->scalar, 0x54651A43), "Compound");
  EXPECT_EQ(*fo4::schema::scalar_value_name(field->scalar, 0xED157AE3), "AutoWeapon");
  EXPECT_FALSE(fo4::schema::scalar_value_name(field->scalar, 1).has_value());

  const auto out = make_plugin();
  auto plugin = fo4::read_plugin(out.bytes, "Patch.esp");
  const auto raw = u32_at(subrecord_data(plugin, "001001:Patch.esp", "CNAM"), 0);
  EXPECT_EQ(raw, 0x54651A43u);
  EXPECT_EQ(*fo4::schema::scalar_value_name(field->scalar, raw), "Compound");
}

TEST(WP9MiscRowsC, ProjMuzzleFlashModelIsZstring) {
  const auto field = fo4::schema::scalar_field_descriptor(fo4::Signature::from_string("PROJ"),
                                                          "PROJ\\MuzzleFlashModel");
  ASSERT_TRUE(field.has_value());
  EXPECT_EQ(field->scalar.subrecord, fo4::Signature::from_string("NAM1"));
  EXPECT_EQ(field->scalar.storage, fo4::schema::ScalarStorage::zstring);
}
