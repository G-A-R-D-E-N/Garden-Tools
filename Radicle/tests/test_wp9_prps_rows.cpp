#include "fo4/plugin/mutation.hpp"
#include "fo4/plugin/reader.hpp"
#include "fo4/plugin/writer.hpp"
#include "fo4/schema/form_links.hpp"
#include "fo4/schema/formlink_lists.hpp"
#include "fo4/schema/scalar.hpp"
#include "plugin_bytes.hpp"

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <initializer_list>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

using Bytes = std::vector<std::byte>;
using fo4::test::ByteWriter;

constexpr std::uint32_t k_retarget_raw = 0x01000A20;

Bytes prps_payload(std::initializer_list<std::pair<std::uint32_t, float>> entries) {
  ByteWriter out;
  for (const auto& [actor_value, value] : entries) {
    out.u32(actor_value);
    out.f32(value);
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

  const auto acti_group = fo4::test::begin_group(out, "ACTI", 0);
  const auto acti = fo4::test::begin_record(out, "ACTI", 0, 0x02000A01);
  fo4::test::write_subrecord(out, "EDID", std::string_view{"Radicle\0", 8});
  fo4::test::write_subrecord(out, "PRPS",
                             prps_payload({{0x00000A10u, 1.5f}, {0x00000A11u, 2.5f}}));
  fo4::test::end_record(out, acti);
  fo4::test::end_group(out, acti_group);
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
  std::uint32_t value{};
  std::memcpy(&value, data.data() + offset, sizeof value);
  return value;
}

float f32_at(const Bytes& data, std::size_t offset) {
  float value{};
  std::memcpy(&value, data.data() + offset, sizeof value);
  return value;
}

fo4::schema::FormLinkFieldDescriptor formlink_row(std::string_view record, std::string_view path) {
  const auto field =
      fo4::schema::formlink_field_descriptor(fo4::Signature::from_string(record), path);
  EXPECT_TRUE(field.has_value()) << path;
  return field.value_or(fo4::schema::FormLinkFieldDescriptor{});
}

fo4::schema::ScalarFieldDescriptor scalar(std::string_view record, std::string_view path) {
  const auto field =
      fo4::schema::scalar_field_descriptor(fo4::Signature::from_string(record), path);
  EXPECT_TRUE(field.has_value()) << path;
  return field.value_or(fo4::schema::ScalarFieldDescriptor{});
}

}  // namespace

TEST(WP9PrpsRows, NewRowsMatchXEditShapeOnEveryPropertyRecord) {
  for (const auto* record : {"ACTI", "CLAS", "CONT", "FLOR", "FURN", "LIGH", "MSTT", "NPC_", "RACE",
                             "STAT", "TERM"}) {
    SCOPED_TRACE(record);
    const std::string prefix = std::string(record) + "\\";
    const auto link = formlink_row(record, prefix + "PropertyActorValue");
    EXPECT_EQ(link.subrecord, fo4::Signature::from_string("PRPS"));
    EXPECT_EQ(link.offset, 0u);
    EXPECT_TRUE(link.nullable);
    EXPECT_EQ(link.address.stride, 8u);

    const auto value = scalar(record, prefix + "PropertyValue");
    EXPECT_EQ(value.scalar.subrecord, fo4::Signature::from_string("PRPS"));
    EXPECT_EQ(value.scalar.storage, fo4::schema::ScalarStorage::f32);
    EXPECT_EQ(value.scalar.offset, 4u);
    EXPECT_EQ(value.scalar.address.stride, 8u);
  }
}

TEST(WP9PrpsRows, ReadsLinkAndValueOfBothEntries) {
  const auto out = make_plugin();
  auto plugin = fo4::read_plugin(out.bytes, "Patch.esp");
  const auto payload = subrecord_data(plugin, "000A01:Patch.esp", "PRPS");
  ASSERT_EQ(payload.size(), 16u);
  EXPECT_EQ(u32_at(payload, 0), 0x00000A10u);
  EXPECT_FLOAT_EQ(f32_at(payload, 4), 1.5f);
  EXPECT_EQ(u32_at(payload, 8), 0x00000A11u);
  EXPECT_FLOAT_EQ(f32_at(payload, 12), 2.5f);

  std::vector<std::size_t> offsets;
  std::vector<std::uint32_t> raws;
  for (const auto& hit : fo4::collect_form_links(plugin)) {
    if (hit.subrecord.to_string() != "PRPS") continue;
    offsets.push_back(hit.offset);
    raws.push_back(hit.raw.value());
  }
  const std::vector<std::size_t> expected_offsets{0, 8};
  const std::vector<std::uint32_t> expected_raws{0x00000A10u, 0x00000A11u};
  EXPECT_EQ(offsets, expected_offsets);
  EXPECT_EQ(raws, expected_raws);
}

TEST(WP9PrpsRows, WritesValueByIndexAndKeepsTheOtherEntry) {
  const auto out = make_plugin();
  auto plugin = fo4::read_plugin(out.bytes, "Patch.esp");
  const auto key = fo4::FormKey::parse("000A01:Patch.esp");
  const auto value_row = scalar("ACTI", "ACTI\\PropertyValue");
  EXPECT_EQ(fo4::set_record_scalar(plugin, key, value_row, 1, "9"),
            fo4::ScalarMutationStatus::updated);

  const auto reread = fo4::read_plugin(fo4::write_plugin(plugin), "Patch.esp");
  const auto payload = subrecord_data(reread, "000A01:Patch.esp", "PRPS");
  ASSERT_EQ(payload.size(), 16u);
  EXPECT_EQ(u32_at(payload, 0), 0x00000A10u);
  EXPECT_FLOAT_EQ(f32_at(payload, 4), 1.5f);
  EXPECT_EQ(u32_at(payload, 8), 0x00000A11u);
  EXPECT_FLOAT_EQ(f32_at(payload, 12), 9.0f);
}

TEST(WP9PrpsRows, RetargetsLinkByIndexAndRefusesAnAppend) {
  const auto out = make_plugin();
  auto plugin = fo4::read_plugin(out.bytes, "Patch.esp");
  const auto key = fo4::FormKey::parse("000A01:Patch.esp");
  const auto link_row = formlink_row("ACTI", "ACTI\\PropertyActorValue");
  const auto target = fo4::FormKey::parse("000A20:Objects.esp");
  EXPECT_EQ(fo4::set_record_formlink_field(plugin, key, link_row, 2, target),
            fo4::FormLinkFieldMutationStatus::subrecord_missing);
  EXPECT_EQ(fo4::set_record_formlink_field(plugin, key, link_row, 1, target),
            fo4::FormLinkFieldMutationStatus::updated);

  const auto reread = fo4::read_plugin(fo4::write_plugin(plugin), "Patch.esp");
  const auto payload = subrecord_data(reread, "000A01:Patch.esp", "PRPS");
  ASSERT_EQ(payload.size(), 16u);
  EXPECT_EQ(u32_at(payload, 0), 0x00000A10u);
  EXPECT_EQ(u32_at(payload, 8), k_retarget_raw);
  EXPECT_FLOAT_EQ(f32_at(payload, 12), 2.5f);
}

TEST(WP9PrpsRows, ActorValueAllowsNullOnAnEntry) {
  const auto out = make_plugin();
  auto plugin = fo4::read_plugin(out.bytes, "Patch.esp");
  const auto key = fo4::FormKey::parse("000A01:Patch.esp");
  const auto link_row = formlink_row("ACTI", "ACTI\\PropertyActorValue");
  EXPECT_EQ(fo4::set_record_formlink_field(plugin, key, link_row, 0, fo4::FormKey{}),
            fo4::FormLinkFieldMutationStatus::updated);

  const auto reread = fo4::read_plugin(fo4::write_plugin(plugin), "Patch.esp");
  const auto payload = subrecord_data(reread, "000A01:Patch.esp", "PRPS");
  ASSERT_EQ(payload.size(), 16u);
  EXPECT_EQ(u32_at(payload, 0), 0u);
  EXPECT_EQ(u32_at(payload, 8), 0x00000A11u);
  EXPECT_FLOAT_EQ(f32_at(payload, 4), 1.5f);
}
