#include "fo4/plugin/mutation.hpp"
#include "fo4/plugin/reader.hpp"
#include "fo4/plugin/writer.hpp"
#include "fo4/schema/scalar.hpp"
#include "plugin_bytes.hpp"

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace {

using Bytes = std::vector<std::byte>;
using fo4::test::ByteWriter;

constexpr auto k_npc = fo4::Signature::from_string("NPC_");

struct RowExpectation {
  std::string path;
  std::size_t offset;
  fo4::schema::ScalarStorage storage;
  std::size_t minimum_payload_size;
};

const std::array k_face_morph_rows{
    RowExpectation{"NPC_\\FaceMorphPositionX", 0, fo4::schema::ScalarStorage::f32, 28},
    RowExpectation{"NPC_\\FaceMorphPositionY", 4, fo4::schema::ScalarStorage::f32, 28},
    RowExpectation{"NPC_\\FaceMorphPositionZ", 8, fo4::schema::ScalarStorage::f32, 28},
    RowExpectation{"NPC_\\FaceMorphRotationX", 12, fo4::schema::ScalarStorage::f32, 28},
    RowExpectation{"NPC_\\FaceMorphRotationY", 16, fo4::schema::ScalarStorage::f32, 28},
    RowExpectation{"NPC_\\FaceMorphRotationZ", 20, fo4::schema::ScalarStorage::f32, 28},
    RowExpectation{"NPC_\\FaceMorphScale", 24, fo4::schema::ScalarStorage::f32, 28},
};

const std::array k_tint_rows{
    RowExpectation{"NPC_\\TintLayerValue", 0, fo4::schema::ScalarStorage::u8, 1},
    RowExpectation{"NPC_\\TintLayerColorRed", 1, fo4::schema::ScalarStorage::u8, 5},
    RowExpectation{"NPC_\\TintLayerColorGreen", 2, fo4::schema::ScalarStorage::u8, 5},
    RowExpectation{"NPC_\\TintLayerColorBlue", 3, fo4::schema::ScalarStorage::u8, 5},
    RowExpectation{"NPC_\\TintLayerTemplateColorIndex", 5, fo4::schema::ScalarStorage::i16, 7},
};

fo4::FormKey npc_key() { return fo4::FormKey::parse("000800:Patch.esp"); }

fo4::schema::ScalarFieldDescriptor scalar(std::string_view path) {
  return fo4::schema::scalar_field_descriptor(path).value();
}

std::uint32_t read_u32(const Bytes& data, std::size_t offset) {
  std::uint32_t value = 0;
  for (std::size_t i = 0; i < 4; ++i) {
    value |= static_cast<std::uint32_t>(std::to_integer<std::uint8_t>(data[offset + i])) << (8 * i);
  }
  return value;
}

Bytes fmrs() {
  ByteWriter out;
  out.f32(1.25f);
  out.f32(-2.5f);
  out.f32(3.75f);
  out.f32(0.5f);
  out.f32(1.0f);
  out.f32(-0.25f);
  out.f32(2.0f);
  for (int i = 0; i < 8; ++i) out.u8(0xAB);
  return out.bytes;
}

Bytes tend7() {
  ByteWriter out;
  out.u8(42);
  out.u8(10);
  out.u8(20);
  out.u8(30);
  out.u8(0);
  out.u16(0xFFFD);
  return out.bytes;
}

std::vector<std::byte> make_plugin(const std::vector<Bytes>& fmris, const std::vector<Bytes>& fmrses,
                                   const std::vector<Bytes>& tetis, const std::vector<Bytes>& tends) {
  ByteWriter out;
  const auto tes4 = fo4::test::begin_record(out, "TES4", 0, 0);
  ByteWriter hedr;
  hedr.f32(1.0f);
  hedr.u32(1);
  hedr.u32(0x911);
  fo4::test::write_subrecord(out, "HEDR", hedr.bytes);
  fo4::test::end_record(out, tes4);

  const auto group = fo4::test::begin_group(out, "NPC_", 0);
  const auto record = fo4::test::begin_record(out, "NPC_", 0, 0x000800);
  for (const auto& instance : fmris) fo4::test::write_subrecord(out, "FMRI", instance);
  for (const auto& instance : fmrses) fo4::test::write_subrecord(out, "FMRS", instance);
  for (const auto& instance : tetis) fo4::test::write_subrecord(out, "TETI", instance);
  for (const auto& instance : tends) fo4::test::write_subrecord(out, "TEND", instance);
  fo4::test::end_record(out, record);
  fo4::test::end_group(out, group);
  return out.bytes;
}

Bytes subrecord_payload(const fo4::Plugin& plugin, std::string_view signature, std::size_t index) {
  const auto* record = fo4::find_record(plugin, npc_key());
  if (record == nullptr) return {};
  const auto wanted = fo4::Signature::from_string(signature);
  std::size_t seen = 0;
  for (const auto& subrecord : record->subrecords) {
    if (subrecord.signature != wanted) continue;
    if (seen == index) return subrecord.data;
    ++seen;
  }
  return {};
}

std::string read_member(const fo4::Plugin& plugin, std::string_view path, std::size_t index) {
  const auto field = scalar(path);
  const auto* record = fo4::find_record(plugin, npc_key());
  if (record == nullptr) return "absent";
  const auto resolved = fo4::schema::resolve_field_slots(
      *record, fo4::schema::scalar_field_shape(field.scalar), field.scalar.address);
  if (index >= resolved.slots.size() || !resolved.slots[index].valid) return "absent";
  const auto slot = resolved.slots[index];
  return fo4::schema::decode_scalar(
      fo4::schema::scalar_slot_bytes(record->subrecords[slot.subrecord].data, slot, field.scalar),
      field.scalar);
}

}  // namespace

TEST(NpcFaceTintRows, RowsDeclareEveryFaceMorphAndTintMember) {
  const auto index = scalar("NPC_\\FaceMorphIndex");
  EXPECT_EQ(index.record, k_npc);
  EXPECT_EQ(index.scalar.subrecord, fo4::Signature::from_string("FMRI"));
  EXPECT_EQ(index.scalar.storage, fo4::schema::ScalarStorage::u32);
  EXPECT_FALSE(index.scalar.embedded);
  EXPECT_TRUE(index.scalar.address.repeated);
  EXPECT_FALSE(index.can_insert);

  for (const auto& expected : k_face_morph_rows) {
    const auto field = scalar(expected.path);
    EXPECT_EQ(field.record, k_npc) << expected.path;
    EXPECT_EQ(field.scalar.subrecord, fo4::Signature::from_string("FMRS")) << expected.path;
    EXPECT_EQ(field.scalar.offset, expected.offset) << expected.path;
    EXPECT_EQ(field.scalar.storage, expected.storage) << expected.path;
    EXPECT_EQ(field.scalar.payload_size, 0u) << expected.path;
    EXPECT_EQ(field.scalar.minimum_payload_size, expected.minimum_payload_size) << expected.path;
    EXPECT_TRUE(field.scalar.embedded) << expected.path;
    EXPECT_TRUE(field.scalar.address.repeated) << expected.path;
    EXPECT_FALSE(field.can_insert) << expected.path;
  }

  const auto data_type = scalar("NPC_\\TintLayerDataType");
  EXPECT_EQ(data_type.scalar.subrecord, fo4::Signature::from_string("TETI"));
  EXPECT_EQ(data_type.scalar.payload_size, 4u);
  EXPECT_TRUE(data_type.scalar.address.repeated);

  const auto layer_index = scalar("NPC_\\TintLayerIndex");
  EXPECT_EQ(layer_index.scalar.subrecord, fo4::Signature::from_string("TETI"));
  EXPECT_EQ(layer_index.scalar.offset, 2u);
  EXPECT_EQ(layer_index.scalar.payload_size, 4u);
  EXPECT_TRUE(layer_index.scalar.address.repeated);

  for (const auto& expected : k_tint_rows) {
    const auto field = scalar(expected.path);
    EXPECT_EQ(field.record, k_npc) << expected.path;
    EXPECT_EQ(field.scalar.subrecord, fo4::Signature::from_string("TEND")) << expected.path;
    EXPECT_EQ(field.scalar.offset, expected.offset) << expected.path;
    EXPECT_EQ(field.scalar.storage, expected.storage) << expected.path;
    EXPECT_EQ(field.scalar.minimum_payload_size, expected.minimum_payload_size) << expected.path;
    EXPECT_EQ(field.scalar.payload_size, 0u) << expected.path;
    EXPECT_TRUE(field.scalar.address.repeated) << expected.path;
  }
}

TEST(NpcFaceTintRows, FaceMorphsReadAndWriteAtTheirOwnOffsets) {
  ByteWriter fmri;
  fmri.u32(0x0000020F);
  ByteWriter teti;
  teti.u16(2);
  teti.u16(0x0014);

  auto plugin = fo4::read_plugin(
      make_plugin({fmri.bytes}, {fmrs()}, {teti.bytes}, {tend7()}), "Patch.esp");
  EXPECT_EQ(read_member(plugin, "NPC_\\FaceMorphIndex", 0), "527");
  EXPECT_EQ(read_member(plugin, "NPC_\\FaceMorphPositionX", 0), "1.25");
  EXPECT_EQ(read_member(plugin, "NPC_\\FaceMorphPositionY", 0), "-2.5");
  EXPECT_EQ(read_member(plugin, "NPC_\\FaceMorphRotationZ", 0), "-0.25");
  EXPECT_EQ(read_member(plugin, "NPC_\\FaceMorphScale", 0), "2");
  EXPECT_EQ(read_member(plugin, "NPC_\\TintLayerDataType", 0), "2");
  EXPECT_EQ(read_member(plugin, "NPC_\\TintLayerIndex", 0), "20");
  EXPECT_EQ(read_member(plugin, "NPC_\\TintLayerValue", 0), "42");
  EXPECT_EQ(read_member(plugin, "NPC_\\TintLayerColorRed", 0), "10");
  EXPECT_EQ(read_member(plugin, "NPC_\\TintLayerTemplateColorIndex", 0), "-3");

  EXPECT_EQ(fo4::set_record_scalar(plugin, npc_key(), scalar("NPC_\\FaceMorphPositionY"), 0, "-4.5"),
            fo4::ScalarMutationStatus::updated);
  EXPECT_EQ(fo4::set_record_scalar(plugin, npc_key(), scalar("NPC_\\TintLayerValue"), 0, "7"),
            fo4::ScalarMutationStatus::updated);
  EXPECT_EQ(fo4::set_record_scalar(plugin, npc_key(), scalar("NPC_\\TintLayerColorBlue"), 0, "255"),
            fo4::ScalarMutationStatus::updated);
  EXPECT_EQ(fo4::set_record_scalar(plugin, npc_key(), scalar("NPC_\\FaceMorphIndex"), 0, "16"),
            fo4::ScalarMutationStatus::updated);

  const auto after = fo4::read_plugin(fo4::write_plugin(plugin), "Patch.esp");
  EXPECT_EQ(read_member(after, "NPC_\\FaceMorphPositionY", 0), "-4.5");
  EXPECT_EQ(read_member(after, "NPC_\\TintLayerValue", 0), "7");
  EXPECT_EQ(read_member(after, "NPC_\\TintLayerColorBlue", 0), "255");
  EXPECT_EQ(read_member(after, "NPC_\\FaceMorphIndex", 0), "16");
  EXPECT_EQ(read_member(after, "NPC_\\FaceMorphPositionX", 0), "1.25");
  EXPECT_EQ(read_member(after, "NPC_\\TintLayerColorGreen", 0), "20");
  EXPECT_EQ(read_member(after, "NPC_\\TintLayerTemplateColorIndex", 0), "-3");

  const auto payload = subrecord_payload(after, "FMRS", 0);
  ASSERT_EQ(payload.size(), 36u);
  EXPECT_EQ(read_u32(payload, 0), 0x3FA00000u);
  EXPECT_EQ(read_u32(payload, 4), 0xC0900000u);
  EXPECT_EQ(read_u32(payload, 24), 0x40000000u);
  EXPECT_EQ(read_u32(payload, 28), 0xABABABABu);

  const auto tint = subrecord_payload(after, "TEND", 0);
  ASSERT_EQ(tint.size(), 7u);
  EXPECT_EQ(std::to_integer<std::uint8_t>(tint[0]), 7u);
  EXPECT_EQ(std::to_integer<std::uint8_t>(tint[3]), 255u);
  EXPECT_EQ(std::to_integer<std::uint8_t>(tint[4]), 0u);
}

TEST(NpcFaceTintRows, ShortTendExposesOnlyTheValue) {
  auto plugin = fo4::read_plugin(make_plugin({}, {}, {}, {Bytes{std::byte{9}}}), "Patch.esp");
  EXPECT_EQ(read_member(plugin, "NPC_\\TintLayerValue", 0), "9");
  EXPECT_EQ(read_member(plugin, "NPC_\\TintLayerColorRed", 0), "absent");
  EXPECT_EQ(read_member(plugin, "NPC_\\TintLayerColorBlue", 0), "absent");
  EXPECT_EQ(read_member(plugin, "NPC_\\TintLayerTemplateColorIndex", 0), "absent");

  EXPECT_EQ(fo4::set_record_scalar(plugin, npc_key(), scalar("NPC_\\TintLayerValue"), 0, "3"),
            fo4::ScalarMutationStatus::updated);
  EXPECT_EQ(fo4::set_record_scalar(plugin, npc_key(), scalar("NPC_\\TintLayerColorRed"), 0, "1"),
            fo4::ScalarMutationStatus::subrecord_missing);
  EXPECT_EQ(fo4::set_record_scalar(plugin, npc_key(), scalar("NPC_\\TintLayerTemplateColorIndex"), 0, "1"),
            fo4::ScalarMutationStatus::subrecord_missing);

  const auto after = fo4::read_plugin(fo4::write_plugin(plugin), "Patch.esp");
  const auto tint = subrecord_payload(after, "TEND", 0);
  ASSERT_EQ(tint.size(), 1u);
  EXPECT_EQ(std::to_integer<std::uint8_t>(tint[0]), 3u);
}

TEST(NpcFaceTintRows, ShortOrMissingPayloadsHideEveryMember) {
  auto plugin = fo4::read_plugin(make_plugin({}, {}, {Bytes(2, std::byte{0})}, {}), "Patch.esp");
  EXPECT_EQ(read_member(plugin, "NPC_\\FaceMorphPositionX", 0), "absent");
  EXPECT_EQ(read_member(plugin, "NPC_\\FaceMorphScale", 0), "absent");
  EXPECT_EQ(read_member(plugin, "NPC_\\TintLayerIndex", 0), "absent");
  EXPECT_EQ(read_member(plugin, "NPC_\\TintLayerValue", 0), "absent");
  EXPECT_EQ(fo4::set_record_scalar(plugin, npc_key(), scalar("NPC_\\FaceMorphScale"), 0, "1"),
            fo4::ScalarMutationStatus::subrecord_missing);
  EXPECT_EQ(fo4::set_record_scalar(plugin, npc_key(), scalar("NPC_\\TintLayerDataType"), 0, "1"),
            fo4::ScalarMutationStatus::subrecord_missing);

  const auto after = fo4::read_plugin(fo4::write_plugin(plugin), "Patch.esp");
  EXPECT_EQ(subrecord_payload(after, "TETI", 0), Bytes(2, std::byte{0}));
  EXPECT_EQ(subrecord_payload(after, "FMRS", 0), Bytes{});
}

TEST(NpcFaceTintRows, FaceMorphPayloadsBelowTheMinimumHideEveryMember) {
  auto plugin = fo4::read_plugin(make_plugin({}, {Bytes(27, std::byte{0})}, {}, {}), "Patch.esp");
  EXPECT_EQ(read_member(plugin, "NPC_\\FaceMorphPositionX", 0), "absent");
  EXPECT_EQ(read_member(plugin, "NPC_\\FaceMorphScale", 0), "absent");
  EXPECT_EQ(fo4::set_record_scalar(plugin, npc_key(), scalar("NPC_\\FaceMorphScale"), 0, "1"),
            fo4::ScalarMutationStatus::subrecord_missing);

  auto sized = fo4::read_plugin(make_plugin({}, {Bytes(28, std::byte{0})}, {}, {}), "Patch.esp");
  EXPECT_EQ(read_member(sized, "NPC_\\FaceMorphPositionX", 0), "0");
  EXPECT_EQ(read_member(sized, "NPC_\\FaceMorphScale", 0), "0");
  EXPECT_EQ(fo4::set_record_scalar(sized, npc_key(), scalar("NPC_\\FaceMorphScale"), 0, "3"),
            fo4::ScalarMutationStatus::updated);

  const auto after = fo4::read_plugin(fo4::write_plugin(sized), "Patch.esp");
  EXPECT_EQ(read_member(after, "NPC_\\FaceMorphScale", 0), "3");
  EXPECT_EQ(subrecord_payload(after, "FMRS", 0).size(), 28u);
}

TEST(NpcFaceTintRows, SecondInstanceIsAddressedByIndexAndUnknownIndexIsRefused) {
  ByteWriter first;
  first.u32(1);
  ByteWriter second;
  second.u32(2);
  ByteWriter teti_first;
  teti_first.u16(1);
  teti_first.u16(3);
  ByteWriter teti_second;
  teti_second.u16(2);
  teti_second.u16(4);

  auto plugin = fo4::read_plugin(
      make_plugin({first.bytes, second.bytes}, {fmrs(), fmrs()}, {teti_first.bytes, teti_second.bytes},
                  {tend7(), tend7()}),
      "Patch.esp");
  EXPECT_EQ(read_member(plugin, "NPC_\\FaceMorphIndex", 1), "2");
  EXPECT_EQ(read_member(plugin, "NPC_\\TintLayerColorRed", 8), "absent");
  EXPECT_EQ(read_member(plugin, "NPC_\\TintLayerIndex", 1), "4");

  EXPECT_EQ(fo4::set_record_scalar(plugin, npc_key(), scalar("NPC_\\FaceMorphScale"), 1, "0.5"),
            fo4::ScalarMutationStatus::updated);
  EXPECT_EQ(fo4::set_record_scalar(plugin, npc_key(), scalar("NPC_\\FaceMorphScale"), 2, "0.5"),
            fo4::ScalarMutationStatus::subrecord_missing);

  const auto after = fo4::read_plugin(fo4::write_plugin(plugin), "Patch.esp");
  EXPECT_EQ(subrecord_payload(after, "FMRS", 0), fmrs());
  const auto second_payload = subrecord_payload(after, "FMRS", 1);
  ASSERT_EQ(second_payload.size(), 36u);
  EXPECT_EQ(read_u32(second_payload, 24), 0x3F000000u);
  EXPECT_EQ(read_u32(second_payload, 0), 0x3FA00000u);
  EXPECT_EQ(read_u32(second_payload, 28), 0xABABABABu);
}

TEST(NpcFaceTintRows, OutOfRangeValuesAreRefused) {
  ByteWriter teti;
  teti.u16(0);
  teti.u16(0);
  auto plugin = fo4::read_plugin(make_plugin({}, {}, {teti.bytes}, {tend7()}), "Patch.esp");
  EXPECT_THROW(fo4::set_record_scalar(plugin, npc_key(), scalar("NPC_\\TintLayerDataType"), 0, "3"),
               fo4::schema::ScalarError);
  EXPECT_THROW(fo4::set_record_scalar(plugin, npc_key(), scalar("NPC_\\TintLayerValue"), 0, "-1"),
               fo4::schema::ScalarError);
  EXPECT_THROW(fo4::set_record_scalar(plugin, npc_key(), scalar("NPC_\\TintLayerColorRed"), 0, "256"),
               fo4::schema::ScalarError);
  EXPECT_THROW(fo4::set_record_scalar(plugin, npc_key(), scalar("NPC_\\TintLayerTemplateColorIndex"), 0,
                                      "32768"),
               fo4::schema::ScalarError);
  EXPECT_EQ(subrecord_payload(plugin, "TETI", 0), teti.bytes);
  EXPECT_EQ(subrecord_payload(plugin, "TEND", 0), tend7());
}
