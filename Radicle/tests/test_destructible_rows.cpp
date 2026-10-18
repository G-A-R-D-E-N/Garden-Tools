#include "fo4/plugin/destructible_stage.hpp"
#include "fo4/plugin/mutation.hpp"
#include "fo4/plugin/reader.hpp"
#include "fo4/plugin/subrecord_authoring.hpp"
#include "fo4/plugin/writer.hpp"
#include "fo4/schema/field_address.hpp"
#include "fo4/schema/formlink_lists.hpp"
#include "fo4/schema/preflight.hpp"
#include "fo4/schema/record_constraints.hpp"
#include "fo4/schema/scalar.hpp"
#include "plugin_bytes.hpp"

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {

using Bytes = std::vector<std::byte>;
using fo4::test::ByteWriter;

constexpr std::string_view k_carriers[] = {
    "ACTI", "ALCH", "AMMO", "ARMO", "BOOK", "CONT", "DOOR", "FLOR", "FURN",
    "INGR", "KEYM", "LIGH", "MISC", "MSTT", "NPC_", "PROJ", "TACT", "WEAP",
};

constexpr std::uint32_t k_damc_type_zero_raw = 0x01000A10u;
constexpr std::uint32_t k_damc_type_one_raw = 0x01000A11u;
constexpr std::uint32_t k_dmds_raw = 0x01000A30u;

fo4::FormKey record_key() { return fo4::FormKey::parse("000A01:Patch.esp"); }

Bytes dest_payload(std::int32_t health, std::uint8_t stages, std::uint8_t flags) {
  ByteWriter out;
  out.i32(health);
  out.u8(stages);
  out.u8(flags);
  out.u16(0);
  return out.bytes;
}

Bytes dstd_payload(std::uint8_t health_percent, std::uint8_t index, std::uint8_t model_stage,
                   std::uint8_t flags, std::int32_t self_damage) {
  ByteWriter out;
  out.u8(health_percent);
  out.u8(index);
  out.u8(model_stage);
  out.u8(flags);
  out.i32(self_damage);
  out.u32(0x01000A40u);
  out.u32(0x01000A41u);
  out.i32(0);
  return out.bytes;
}

Bytes make_plugin() {
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

  const auto group = fo4::test::begin_group(out, "ACTI", 0);
  const auto acti = fo4::test::begin_record(out, "ACTI", 0, 0x02000A01);
  fo4::test::write_subrecord(out, "EDID", std::string_view{"Radicle\0", 8});
  fo4::test::write_subrecord(out, "DEST", dest_payload(1500, 2, 1));

  ByteWriter damc;
  damc.u32(k_damc_type_zero_raw);
  damc.u32(5);
  damc.u32(k_damc_type_one_raw);
  damc.u32(7);
  fo4::test::write_subrecord(out, "DAMC", damc.bytes);

  fo4::test::write_subrecord(out, "DSTD", dstd_payload(10, 0, 0, 1, 1));
  fo4::test::write_subrecord(out, "DSTD", dstd_payload(50, 1, 0, 0, 2));
  fo4::test::write_subrecord(out, "DSTA", std::string_view{"seq0\0", 5});
  fo4::test::write_subrecord(out, "DMDL", std::string_view{"a.nif\0", 6});
  fo4::test::write_subrecord(out, "DMDL", std::string_view{"c.nif\0", 6});

  ByteWriter dmdc;
  dmdc.f32(0.0f);
  fo4::test::write_subrecord(out, "DMDC", dmdc.bytes);

  ByteWriter dmds;
  dmds.u32(k_dmds_raw);
  fo4::test::write_subrecord(out, "DMDS", dmds.bytes);

  fo4::test::write_subrecord(out, "DSTF", Bytes{});
  fo4::test::end_record(out, acti);

  const auto bare = fo4::test::begin_record(out, "ACTI", 0, 0x02000A04);
  fo4::test::write_subrecord(out, "EDID", std::string_view{"Bare\0", 5});
  fo4::test::end_record(out, bare);
  fo4::test::end_group(out, group);

  const auto door_group = fo4::test::begin_group(out, "DOOR", 0);
  const auto door = fo4::test::begin_record(out, "DOOR", 0, 0x02000A02);
  fo4::test::write_subrecord(out, "EDID", std::string_view{"Plain\0", 6});
  fo4::test::end_record(out, door);
  fo4::test::end_group(out, door_group);

  const auto stat_group = fo4::test::begin_group(out, "STAT", 0);
  const auto stat = fo4::test::begin_record(out, "STAT", 0, 0x02000A03);
  fo4::test::write_subrecord(out, "EDID", std::string_view{"Prop\0", 5});
  fo4::test::end_record(out, stat);
  fo4::test::end_group(out, stat_group);
  return out.bytes;
}

Bytes make_proj_plugin() {
  ByteWriter out;
  const auto tes4 = fo4::test::begin_record(out, "TES4", 0, 0);
  ByteWriter hedr;
  hedr.f32(1.0f);
  hedr.u32(4);
  hedr.u32(0x900);
  fo4::test::write_subrecord(out, "HEDR", hedr.bytes);
  fo4::test::write_subrecord(out, "MAST", std::string_view{"Fallout4.esm\0", 13});
  fo4::test::write_subrecord(out, "DATA", Bytes(8));
  fo4::test::end_record(out, tes4);

  const auto group = fo4::test::begin_group(out, "PROJ", 0);
  const auto staged = fo4::test::begin_record(out, "PROJ", 0, 0x01000C01);
  fo4::test::write_subrecord(out, "EDID", std::string_view{"Shot\0", 5});
  fo4::test::write_subrecord(out, "OBND", Bytes(12));
  fo4::test::write_subrecord(out, "MODL", std::string_view{"p.nif\0", 6});
  fo4::test::write_subrecord(out, "MODT", Bytes(12));
  fo4::test::write_subrecord(out, "DEST", dest_payload(300, 2, 0));
  fo4::test::write_subrecord(out, "DSTD", dstd_payload(20, 0, 0, 1, 0));
  fo4::test::write_subrecord(out, "DSTF", Bytes{});
  fo4::test::write_subrecord(out, "DSTD", dstd_payload(60, 1, 0, 2, 0));
  fo4::test::write_subrecord(out, "DSTF", Bytes{});
  fo4::test::write_subrecord(out, "DATA", Bytes(92));
  fo4::test::write_subrecord(out, "DNAM", Bytes(4));
  fo4::test::write_subrecord(out, "NAM1", std::string_view{"m.nif\0", 6});
  fo4::test::write_subrecord(out, "VNAM", Bytes(4));
  fo4::test::end_record(out, staged);

  const auto doubled = fo4::test::begin_record(out, "PROJ", 0, 0x01000C02);
  fo4::test::write_subrecord(out, "EDID", std::string_view{"Twice\0", 6});
  fo4::test::write_subrecord(out, "OBND", Bytes(12));
  fo4::test::write_subrecord(out, "MODL", std::string_view{"p.nif\0", 6});
  fo4::test::write_subrecord(out, "MODT", Bytes(12));
  fo4::test::write_subrecord(out, "DEST", dest_payload(300, 1, 0));
  fo4::test::write_subrecord(out, "DEST", dest_payload(200, 1, 0));
  fo4::test::write_subrecord(out, "DATA", Bytes(92));
  fo4::test::write_subrecord(out, "DNAM", Bytes(4));
  fo4::test::write_subrecord(out, "NAM1", std::string_view{"m.nif\0", 6});
  fo4::test::write_subrecord(out, "VNAM", Bytes(4));
  fo4::test::end_record(out, doubled);
  fo4::test::end_group(out, group);
  return out.bytes;
}

Bytes make_authoring_plugin() {
  ByteWriter out;
  const auto tes4 = fo4::test::begin_record(out, "TES4", 0, 0);
  ByteWriter hedr;
  hedr.f32(1.0f);
  hedr.u32(4);
  hedr.u32(0x900);
  fo4::test::write_subrecord(out, "HEDR", hedr.bytes);
  fo4::test::write_subrecord(out, "MAST", std::string_view{"Fallout4.esm\0", 13});
  fo4::test::write_subrecord(out, "DATA", Bytes(8));
  fo4::test::end_record(out, tes4);

  const auto group = fo4::test::begin_group(out, "ARMO", 0);
  const auto one = fo4::test::begin_record(out, "ARMO", 0, 0x01000B01);
  fo4::test::write_subrecord(out, "EDID", std::string_view{"One\0", 4});
  fo4::test::write_subrecord(out, "DEST", dest_payload(100, 1, 0));
  fo4::test::write_subrecord(out, "DATA", Bytes(12));
  fo4::test::end_record(out, one);

  const auto two = fo4::test::begin_record(out, "ARMO", 0, 0x01000B02);
  fo4::test::write_subrecord(out, "EDID", std::string_view{"Two\0", 4});
  fo4::test::write_subrecord(out, "DEST", dest_payload(100, 1, 0));
  fo4::test::write_subrecord(out, "DEST", dest_payload(50, 1, 0));
  fo4::test::write_subrecord(out, "DATA", Bytes(12));
  fo4::test::end_record(out, two);
  fo4::test::end_group(out, group);
  return out.bytes;
}

std::vector<std::string> signatures_at(const fo4::Plugin& plugin, std::string_view key) {
  std::vector<std::string> out;
  const auto* record = fo4::find_record(plugin, fo4::FormKey::parse(key));
  if (record == nullptr) return out;
  for (const auto& subrecord : record->subrecords) out.push_back(subrecord.signature.to_string());
  return out;
}

const fo4::schema::SubrecordAuthoringConstraint* authoring_rule(
    const fo4::schema::RecordAuthoringConstraint& constraint, std::string_view signature) {
  for (const auto& rule : fo4::schema::authoring_subrecords(constraint)) {
    if (rule.signature.to_string() == signature) return &rule;
  }
  return nullptr;
}

bool has_authoring_issue(const std::vector<fo4::schema::PreflightIssue>& issues,
                         std::string_view path, std::string_view code) {
  for (const auto& issue : issues) {
    if (issue.path == path && issue.code == code) return true;
  }
  return false;
}

std::uint8_t byte_at(const Bytes& data, std::size_t offset) {
  return std::to_integer<std::uint8_t>(data[offset]);
}

std::uint32_t u32_at(const Bytes& data, std::size_t offset) {
  std::uint32_t value = 0;
  for (std::size_t i = 0; i < 4; ++i) {
    value |= static_cast<std::uint32_t>(byte_at(data, offset + i)) << (8 * i);
  }
  return value;
}

std::int32_t i32_at(const Bytes& data, std::size_t offset) {
  return static_cast<std::int32_t>(u32_at(data, offset));
}

float f32_at(const Bytes& data, std::size_t offset) {
  const auto raw = u32_at(data, offset);
  float value = 0.0f;
  std::memcpy(&value, &raw, sizeof(value));
  return value;
}

const fo4::Subrecord* subrecord_at(const fo4::Plugin& plugin, std::string_view key,
                                   std::string_view signature, std::size_t ordinal) {
  const auto* record = fo4::find_record(plugin, fo4::FormKey::parse(key));
  if (record == nullptr) return nullptr;
  std::size_t seen = 0;
  for (const auto& subrecord : record->subrecords) {
    if (subrecord.signature.to_string() != signature) continue;
    if (seen == ordinal) return &subrecord;
    ++seen;
  }
  return nullptr;
}

Bytes payload_at(const fo4::Plugin& plugin, std::string_view signature, std::size_t ordinal) {
  const auto* subrecord = subrecord_at(plugin, "000A01:Patch.esp", signature, ordinal);
  if (subrecord == nullptr) {
    ADD_FAILURE() << signature;
    return {};
  }
  return subrecord->data;
}

fo4::schema::ScalarFieldDescriptor scalar_row(std::string_view path) {
  const auto field = fo4::schema::scalar_field_descriptor(
      fo4::Signature::from_string("ACTI"), path);
  if (!field.has_value()) throw std::runtime_error(std::string(path));
  return *field;
}

fo4::schema::FormLinkFieldDescriptor link_row(std::string_view path) {
  const auto field = fo4::schema::formlink_field_descriptor(
      fo4::Signature::from_string("ACTI"), path);
  if (!field.has_value()) throw std::runtime_error(std::string(path));
  return *field;
}

std::string read_row(const fo4::Plugin& plugin, const fo4::schema::ScalarFieldDescriptor& field,
                     std::size_t index) {
  const auto& record = *fo4::find_record(plugin, record_key());
  const auto resolved = fo4::schema::resolve_field_slots(
      record, fo4::schema::scalar_field_shape(field.scalar), field.scalar.address);
  if (index >= resolved.slots.size()) return {};
  const auto slot = resolved.slots[index];
  return fo4::schema::decode_scalar(
      fo4::schema::scalar_slot_bytes(record.subrecords[slot.subrecord].data, slot, field.scalar),
      field.scalar);
}

}  // namespace

TEST(DestructibleRows, DescribeTheGroupOnEveryCarrier) {
  for (const auto signature_text : k_carriers) {
    const auto signature = fo4::Signature::from_string(signature_text);
    const std::string prefix = std::string(signature_text) + "\\";
    SCOPED_TRACE(prefix);

    const auto health = fo4::schema::scalar_field_descriptor(signature, prefix + "DestructionHealth");
    ASSERT_TRUE(health.has_value());
    EXPECT_EQ(health->scalar.storage, fo4::schema::ScalarStorage::i32);
    EXPECT_EQ(health->scalar.offset, 0u);
    EXPECT_EQ(health->scalar.payload_size, 8u);
    EXPECT_FALSE(health->can_insert);

    const auto flags = fo4::schema::scalar_field_descriptor(signature, prefix + "DestructionFlags");
    ASSERT_TRUE(flags.has_value());
    EXPECT_EQ(flags->scalar.offset, 5u);

    const auto stage_count =
        fo4::schema::scalar_field_descriptor(signature, prefix + "DestructionStageCount");
    ASSERT_TRUE(stage_count.has_value());
    EXPECT_EQ(stage_count->scalar.offset, 4u);
    EXPECT_EQ(stage_count->scalar.storage, fo4::schema::ScalarStorage::u8);

    const auto resistances =
        fo4::schema::counted_array_descriptor(signature, prefix + "DestructionResistances");
    ASSERT_TRUE(resistances.has_value());
    EXPECT_EQ(resistances->subrecord, fo4::Signature::from_string("DAMC"));
    EXPECT_EQ(resistances->stride, 8u);

    const auto stage = fo4::schema::scalar_field_descriptor(signature, prefix + "DestructionStageIndex");
    ASSERT_TRUE(stage.has_value());
    EXPECT_TRUE(stage->scalar.address.repeated);
    EXPECT_EQ(stage->scalar.offset, 1u);
    EXPECT_EQ(stage->scalar.payload_size, 20u);

    const auto explosion =
        fo4::schema::formlink_field_descriptor(signature, prefix + "DestructionStageExplosion");
    ASSERT_TRUE(explosion.has_value());
    EXPECT_EQ(explosion->subrecord, fo4::Signature::from_string("DSTD"));
    EXPECT_EQ(explosion->offset, 8u);
    EXPECT_TRUE(explosion->address.repeated);
    EXPECT_TRUE(explosion->nullable);

    const auto damage_type =
        fo4::schema::formlink_field_descriptor(signature, prefix + "DestructionDamageType");
    ASSERT_TRUE(damage_type.has_value());
    EXPECT_EQ(damage_type->offset, 0u);
    EXPECT_EQ(damage_type->address.stride, 8u);
    EXPECT_FALSE(damage_type->nullable);
  }

  EXPECT_FALSE(fo4::schema::scalar_field_descriptor(fo4::Signature::from_string("STAT"),
                                                    "STAT\\DestructionHealth")
                   .has_value());
  EXPECT_FALSE(fo4::schema::counted_array_descriptor(fo4::Signature::from_string("STAT"),
                                                     "STAT\\DestructionResistances")
                   .has_value());
  EXPECT_FALSE(fo4::schema::formlink_field_descriptor(fo4::Signature::from_string("STAT"),
                                                      "STAT\\DestructionDamageType")
                   .has_value());
}

TEST(DestructibleRows, HeaderHealthAndFlagsCoverEightBytes) {
  auto plugin = fo4::read_plugin(make_plugin(), "Patch.esp");
  const auto health = scalar_row("ACTI\\DestructionHealth");
  const auto stage_count = scalar_row("ACTI\\DestructionStageCount");
  const auto flags = scalar_row("ACTI\\DestructionFlags");

  EXPECT_EQ(read_row(plugin, health, 0), "1500");
  EXPECT_EQ(read_row(plugin, stage_count, 0), "2");
  EXPECT_EQ(read_row(plugin, flags, 0), "1");
  const auto named = fo4::schema::scalar_value_name(flags.scalar, 1);
  ASSERT_TRUE(named.has_value());
  EXPECT_EQ(*named, "VATS Targetable");
  const auto second = fo4::schema::scalar_value_name(flags.scalar, 2);
  ASSERT_TRUE(second.has_value());
  EXPECT_EQ(*second, "Large Actor Destroys");
  EXPECT_FALSE(fo4::schema::scalar_value_name(flags.scalar, 4).has_value());
  EXPECT_TRUE(read_row(plugin, health, 1).empty());

  EXPECT_EQ(fo4::set_record_scalar(plugin, record_key(), health, 0, "900"),
            fo4::ScalarMutationStatus::updated);
  EXPECT_EQ(fo4::set_record_scalar(plugin, record_key(), flags, 0, "3"),
            fo4::ScalarMutationStatus::updated);
  EXPECT_EQ(fo4::set_record_scalar(plugin, record_key(), stage_count, 0, "3"),
            fo4::ScalarMutationStatus::updated);
  EXPECT_THROW(
      static_cast<void>(fo4::set_record_scalar(plugin, record_key(), stage_count, 0, "300")),
      fo4::schema::ScalarError);

  const auto reread = fo4::read_plugin(fo4::write_plugin(plugin), "Patch.esp");
  const auto dest = payload_at(reread, "DEST", 0);
  ASSERT_EQ(dest.size(), 8u);
  EXPECT_EQ(i32_at(dest, 0), 900);
  EXPECT_EQ(byte_at(dest, 4), 3u);
  EXPECT_EQ(byte_at(dest, 5), 3u);
  EXPECT_EQ(byte_at(dest, 6), 0u);
  EXPECT_EQ(byte_at(dest, 7), 0u);
}

TEST(DestructibleRows, ResistanceArrayReadsWritesInsertsAndRemoves) {
  auto plugin = fo4::read_plugin(make_plugin(), "Patch.esp");
  const auto value = scalar_row("ACTI\\DestructionResistanceValue");
  const auto array = fo4::schema::counted_array_descriptor(
                         fo4::Signature::from_string("ACTI"), "ACTI\\DestructionResistances")
                         .value();

  EXPECT_EQ(fo4::record_field_count(plugin, record_key(), "ACTI\\DestructionResistanceValue"), 2u);
  EXPECT_EQ(read_row(plugin, value, 0), "5");
  EXPECT_EQ(read_row(plugin, value, 1), "7");
  EXPECT_EQ(read_row(plugin, value, 2), "");

  EXPECT_EQ(fo4::set_record_scalar(plugin, record_key(), value, 1, "9"),
            fo4::ScalarMutationStatus::updated);
  EXPECT_EQ(fo4::set_record_scalar(plugin, record_key(), value, 2, "9"),
            fo4::ScalarMutationStatus::subrecord_missing);
  EXPECT_TRUE(fo4::test::original_bytes(plugin).empty());

  EXPECT_EQ(fo4::insert_record_counted_element(plugin, record_key(), array, 3),
            fo4::CountedArrayMutationStatus::index_out_of_range);
  EXPECT_EQ(fo4::insert_record_counted_element(plugin, record_key(), array, 1),
            fo4::CountedArrayMutationStatus::updated);

  const auto inserted = fo4::read_plugin(fo4::write_plugin(plugin), "Patch.esp");
  EXPECT_EQ(payload_at(inserted, "DAMC", 0).size(), 24u);
  EXPECT_EQ(read_row(inserted, value, 0), "5");
  EXPECT_EQ(read_row(inserted, value, 1), "0");
  EXPECT_EQ(read_row(inserted, value, 2), "9");
  EXPECT_EQ(u32_at(payload_at(inserted, "DAMC", 0), 8), 0u);

  EXPECT_EQ(fo4::remove_record_counted_element(plugin, record_key(), array, 0),
            fo4::CountedArrayMutationStatus::updated);
  const auto removed = fo4::read_plugin(fo4::write_plugin(plugin), "Patch.esp");
  EXPECT_EQ(payload_at(removed, "DAMC", 0).size(), 16u);
  EXPECT_EQ(read_row(removed, value, 0), "0");
  EXPECT_EQ(read_row(removed, value, 1), "9");

  EXPECT_EQ(fo4::remove_record_counted_element(plugin, record_key(), array, 0),
            fo4::CountedArrayMutationStatus::updated);
  EXPECT_EQ(fo4::remove_record_counted_element(plugin, record_key(), array, 0),
            fo4::CountedArrayMutationStatus::would_empty);
  const auto single = fo4::read_plugin(fo4::write_plugin(plugin), "Patch.esp");
  EXPECT_EQ(payload_at(single, "DAMC", 0).size(), 8u);
  EXPECT_EQ(read_row(single, value, 0), "9");
  EXPECT_EQ(read_row(single, value, 1), "");
}

TEST(DestructibleRows, ResistanceArrayRefusesARecordWithoutDamc) {
  auto plugin = fo4::read_plugin(make_plugin(), "Patch.esp");
  const auto acti_array = fo4::schema::counted_array_descriptor(
                              fo4::Signature::from_string("ACTI"), "ACTI\\DestructionResistances")
                              .value();
  const auto door_array = fo4::schema::counted_array_descriptor(
                              fo4::Signature::from_string("DOOR"), "DOOR\\DestructionResistances")
                              .value();
  const auto door_key = fo4::FormKey::parse("000A02:Patch.esp");
  const auto original = fo4::test::original_bytes(plugin);

  EXPECT_EQ(fo4::insert_record_counted_element(plugin, fo4::FormKey::parse("000999:Patch.esp"),
                                               acti_array, 0),
            fo4::CountedArrayMutationStatus::record_missing);
  EXPECT_EQ(fo4::insert_record_counted_element(plugin, door_key, door_array, 0),
            fo4::CountedArrayMutationStatus::subrecord_missing);
  EXPECT_EQ(fo4::remove_record_counted_element(plugin, door_key, door_array, 0),
            fo4::CountedArrayMutationStatus::subrecord_missing);
  EXPECT_THROW(fo4::insert_record_counted_element(plugin, door_key, acti_array, 0),
               fo4::schema::CountedArrayError);
  EXPECT_EQ(fo4::test::original_bytes(plugin), original);
}

TEST(DestructibleRows, DamageTypeLinkAddressesOneResistance) {
  const auto link = link_row("ACTI\\DestructionDamageType");
  const auto target = fo4::FormKey::parse("000A12:Fallout4.esm");

  auto plugin = fo4::read_plugin(make_plugin(), "Patch.esp");
  const auto original = fo4::test::original_bytes(plugin);
  EXPECT_EQ(fo4::set_record_formlink_field(plugin, record_key(), link, 2, target),
            fo4::FormLinkFieldMutationStatus::subrecord_missing);
  EXPECT_EQ(fo4::test::original_bytes(plugin), original);
  EXPECT_EQ(fo4::set_record_formlink_field(plugin, record_key(), link, 1, target),
            fo4::FormLinkFieldMutationStatus::updated);

  const auto reread = fo4::read_plugin(fo4::write_plugin(plugin), "Patch.esp");
  const auto payload = payload_at(reread, "DAMC", 0);
  ASSERT_EQ(payload.size(), 16u);
  EXPECT_EQ(u32_at(payload, 0), k_damc_type_zero_raw);
  EXPECT_NE(u32_at(payload, 8), k_damc_type_one_raw);
  EXPECT_EQ(u32_at(payload, 4), 5u);
  EXPECT_EQ(u32_at(payload, 12), 7u);

  auto first_entry = fo4::read_plugin(make_plugin(), "Patch.esp");
  EXPECT_EQ(fo4::set_record_formlink_field(first_entry, record_key(), link, 0, target),
            fo4::FormLinkFieldMutationStatus::updated);
  const auto first_reread = fo4::read_plugin(fo4::write_plugin(first_entry), "Patch.esp");
  EXPECT_EQ(u32_at(payload_at(first_reread, "DAMC", 0), 0), u32_at(payload, 8));

  auto null_plugin = fo4::read_plugin(make_plugin(), "Patch.esp");
  EXPECT_THROW(fo4::set_record_formlink_field(null_plugin, record_key(), link, 0, fo4::FormKey{}),
               fo4::schema::FormLinkFieldError);
  EXPECT_EQ(fo4::test::original_bytes(null_plugin), original);
}

TEST(DestructibleRows, StageRowsAddressEachRepeatedDstd) {
  auto plugin = fo4::read_plugin(make_plugin(), "Patch.esp");
  const auto health_percent = scalar_row("ACTI\\DestructionStageHealthPercent");
  const auto stage_flags = scalar_row("ACTI\\DestructionStageFlags");
  const auto self_damage = scalar_row("ACTI\\DestructionStageSelfDamagePerSecond");
  const auto debris_count = scalar_row("ACTI\\DestructionStageDebrisCount");
  const auto explosion = link_row("ACTI\\DestructionStageExplosion");

  EXPECT_EQ(read_row(plugin, health_percent, 0), "10");
  EXPECT_EQ(read_row(plugin, health_percent, 1), "50");
  EXPECT_EQ(read_row(plugin, health_percent, 2), "");
  EXPECT_EQ(read_row(plugin, debris_count, 1), "0");
  const auto dynamic = fo4::schema::scalar_value_name(stage_flags.scalar, 16);
  ASSERT_TRUE(dynamic.has_value());
  EXPECT_EQ(*dynamic, "Becomes Dynamic");
  EXPECT_FALSE(fo4::schema::scalar_value_name(stage_flags.scalar, 5).has_value());
  EXPECT_EQ(explosion.payload_sizes[0], 20u);

  EXPECT_EQ(fo4::set_record_scalar(plugin, record_key(), health_percent, 1, "75"),
            fo4::ScalarMutationStatus::updated);
  EXPECT_EQ(fo4::set_record_scalar(plugin, record_key(), self_damage, 1, "-4"),
            fo4::ScalarMutationStatus::updated);
  EXPECT_EQ(fo4::set_record_scalar(plugin, record_key(), debris_count, 1, "3"),
            fo4::ScalarMutationStatus::updated);
  EXPECT_EQ(fo4::set_record_scalar(plugin, record_key(), health_percent, 2, "75"),
            fo4::ScalarMutationStatus::subrecord_missing);

  const auto reread = fo4::read_plugin(fo4::write_plugin(plugin), "Patch.esp");
  const auto first = payload_at(reread, "DSTD", 0);
  const auto second = payload_at(reread, "DSTD", 1);
  ASSERT_EQ(first.size(), 20u);
  ASSERT_EQ(second.size(), 20u);
  EXPECT_EQ(byte_at(first, 0), 10u);
  EXPECT_EQ(i32_at(first, 4), 1);
  EXPECT_EQ(byte_at(second, 0), 75u);
  EXPECT_EQ(i32_at(second, 4), -4);
  EXPECT_EQ(i32_at(second, 16), 3);
  EXPECT_EQ(u32_at(second, 8), 0x01000A40u);
}

TEST(DestructibleRows, WholeStageInsertAndRemoveKeepTheHeaderCount) {
  auto plugin = fo4::read_plugin(make_plugin(), "Patch.esp");

  EXPECT_EQ(fo4::insert_destructible_stage(plugin, record_key(), 3),
            fo4::DestructibleStageMutationStatus::index_out_of_range);
  EXPECT_EQ(fo4::insert_destructible_stage(plugin, record_key(), 1),
            fo4::DestructibleStageMutationStatus::updated);
  EXPECT_EQ(fo4::insert_destructible_stage(plugin, fo4::FormKey::parse("000A03:Patch.esp"), 0),
            fo4::DestructibleStageMutationStatus::record_mismatch);
  EXPECT_EQ(fo4::insert_destructible_stage(plugin, fo4::FormKey::parse("000A04:Patch.esp"), 0),
            fo4::DestructibleStageMutationStatus::header_missing);
  EXPECT_EQ(fo4::insert_destructible_stage(plugin, fo4::FormKey::parse("000A99:Patch.esp"), 0),
            fo4::DestructibleStageMutationStatus::record_missing);

  const auto inserted = fo4::read_plugin(fo4::write_plugin(plugin), "Patch.esp");
  EXPECT_EQ(signatures_at(inserted, "000A01:Patch.esp"),
            (std::vector<std::string>{"EDID", "DEST", "DAMC", "DSTD", "DSTD", "DSTF", "DSTD",
                                      "DSTA", "DMDL", "DMDL", "DMDC", "DMDS", "DSTF"}));
  EXPECT_EQ(byte_at(payload_at(inserted, "DEST", 0), 4), 3u);
  EXPECT_EQ(i32_at(payload_at(inserted, "DEST", 0), 0), 1500);
  EXPECT_EQ(byte_at(payload_at(inserted, "DSTD", 0), 0), 10u);
  const auto added = payload_at(inserted, "DSTD", 1);
  ASSERT_EQ(added.size(), 20u);
  EXPECT_EQ(u32_at(added, 0), 0u);
  EXPECT_EQ(u32_at(added, 16), 0u);
  EXPECT_EQ(payload_at(inserted, "DSTF", 0).size(), 0u);
  EXPECT_EQ(byte_at(payload_at(inserted, "DSTD", 2), 0), 50u);

  auto removal = fo4::read_plugin(make_plugin(), "Patch.esp");
  EXPECT_EQ(fo4::remove_destructible_stage(removal, record_key(), 0),
            fo4::DestructibleStageMutationStatus::updated);
  const auto first = fo4::read_plugin(fo4::write_plugin(removal), "Patch.esp");
  EXPECT_EQ(signatures_at(first, "000A01:Patch.esp"),
            (std::vector<std::string>{"EDID", "DEST", "DAMC", "DSTD", "DSTA", "DMDL", "DMDL",
                                      "DMDC", "DMDS", "DSTF"}));
  EXPECT_EQ(byte_at(payload_at(first, "DEST", 0), 4), 1u);
  EXPECT_EQ(byte_at(payload_at(first, "DSTD", 0), 0), 50u);

  EXPECT_EQ(fo4::remove_destructible_stage(removal, record_key(), 0),
            fo4::DestructibleStageMutationStatus::updated);
  EXPECT_EQ(fo4::remove_destructible_stage(removal, record_key(), 0),
            fo4::DestructibleStageMutationStatus::index_out_of_range);
  const auto empty = fo4::read_plugin(fo4::write_plugin(removal), "Patch.esp");
  EXPECT_EQ(signatures_at(empty, "000A01:Patch.esp"),
            (std::vector<std::string>{"EDID", "DEST", "DAMC"}));
  EXPECT_EQ(byte_at(payload_at(empty, "DEST", 0), 4), 0u);

  EXPECT_EQ(fo4::insert_destructible_stage(removal, record_key(), 0),
            fo4::DestructibleStageMutationStatus::updated);
  const auto restaged = fo4::read_plugin(fo4::write_plugin(removal), "Patch.esp");
  EXPECT_EQ(signatures_at(restaged, "000A01:Patch.esp"),
            (std::vector<std::string>{"EDID", "DEST", "DAMC", "DSTD", "DSTF"}));
  EXPECT_EQ(byte_at(payload_at(restaged, "DEST", 0), 4), 1u);

  auto full = fo4::read_plugin(make_plugin(), "Patch.esp");
  for (int i = 0; i < 253; ++i) {
    ASSERT_EQ(fo4::insert_destructible_stage(full, record_key(), 0),
              fo4::DestructibleStageMutationStatus::updated) << i;
  }
  EXPECT_EQ(byte_at(payload_at(full, "DEST", 0), 4), 255u);
  EXPECT_EQ(fo4::insert_destructible_stage(full, record_key(), 0),
            fo4::DestructibleStageMutationStatus::too_many_stages);
}

TEST(DestructibleRows, AuthoringListsCoverTheHeaderAndTheStageMembers) {
  const auto armo = fo4::schema::record_authoring_constraint(fo4::Signature::from_string("ARMO"));
  ASSERT_TRUE(armo.has_value());
  const auto* armo_dest = authoring_rule(*armo, "DEST");
  ASSERT_NE(armo_dest, nullptr);
  EXPECT_TRUE(armo_dest->coupled);
  EXPECT_EQ(armo_dest->minimum, 0);
  EXPECT_EQ(armo_dest->maximum, 1);

  const auto proj = fo4::schema::record_authoring_constraint(fo4::Signature::from_string("PROJ"));
  ASSERT_TRUE(proj.has_value());
  const auto* damc = authoring_rule(*proj, "DAMC");
  ASSERT_NE(damc, nullptr);
  EXPECT_EQ(damc->minimum, 0);
  EXPECT_EQ(damc->maximum, 1);
  for (const char* name : {"DSTA", "DMDC", "DMDS"}) {
    const auto* rule = authoring_rule(*proj, name);
    ASSERT_NE(rule, nullptr) << name;
    EXPECT_EQ(rule->minimum, 0);
    EXPECT_EQ(rule->maximum, 255);
  }

  auto plugin = fo4::read_plugin(make_authoring_plugin(), "Patch.esp");
  const auto single = fo4::schema::preflight_authoring_record(plugin, fo4::FormKey::parse("000B01:Patch.esp"));
  const auto doubled = fo4::schema::preflight_authoring_record(plugin, fo4::FormKey::parse("000B02:Patch.esp"));
  EXPECT_FALSE(has_authoring_issue(single, "ARMO\\DEST", "subrecord_cardinality_invalid"));
  EXPECT_TRUE(has_authoring_issue(doubled, "ARMO\\DEST", "subrecord_cardinality_invalid"));
}

TEST(DestructibleRows, TheStageGroupCarriesTheSameAuthoringRulesOnEveryCarrier) {
  const struct {
    const char* signature;
    std::uint16_t maximum;
    bool coupled;
  } expected[] = {
      {"DEST", 1, true},   {"DAMC", 1, false},   {"DSTD", 255, false}, {"DSTA", 255, false},
      {"DMDL", 255, false}, {"DMDT", 255, false}, {"DMDC", 255, false}, {"DMDS", 255, false},
      {"DSTF", 255, false}};

  for (const auto signature_text : k_carriers) {
    const auto signature = fo4::Signature::from_string(signature_text);
    SCOPED_TRACE(std::string(signature_text));
    const auto constraint = fo4::schema::record_authoring_constraint(signature);
    ASSERT_TRUE(constraint.has_value());
    for (const auto& want : expected) {
      const auto* rule = authoring_rule(*constraint, want.signature);
      ASSERT_NE(rule, nullptr) << want.signature;
      EXPECT_EQ(rule->minimum, 0) << want.signature;
      EXPECT_EQ(rule->maximum, want.maximum) << want.signature;
      EXPECT_EQ(rule->coupled, want.coupled) << want.signature;
    }
  }
}

TEST(DestructibleRows, MultiStageProjectilePassesAuthoringCardinality) {
  auto plugin = fo4::read_plugin(make_proj_plugin(), "Patch.esp");
  const auto staged_key = fo4::FormKey::parse("000C01:Patch.esp");
  const auto doubled_key = fo4::FormKey::parse("000C02:Patch.esp");

  const auto staged = fo4::schema::preflight_authoring_record(plugin, staged_key);
  EXPECT_FALSE(has_authoring_issue(staged, "PROJ\\DSTD", "subrecord_cardinality_invalid"));
  EXPECT_FALSE(has_authoring_issue(staged, "PROJ\\DSTF", "subrecord_cardinality_invalid"));
  EXPECT_FALSE(has_authoring_issue(staged, "PROJ\\DSTD", "required_subrecord_missing"));

  const auto doubled = fo4::schema::preflight_authoring_record(plugin, doubled_key);
  EXPECT_TRUE(has_authoring_issue(doubled, "PROJ\\DEST", "subrecord_cardinality_invalid"));

  EXPECT_EQ(fo4::add_record_subrecord(plugin, staged_key, fo4::Signature::from_string("DSTD")),
            fo4::SubrecordAddStatus::not_authorable);
  EXPECT_EQ(fo4::add_record_subrecord(plugin, staged_key, fo4::Signature::from_string("OBND")),
            fo4::SubrecordAddStatus::already_present);
}

TEST(DestructibleRows, StageModelRowsAreIndexedPerSubrecordNotPerStage) {
  auto plugin = fo4::read_plugin(make_plugin(), "Patch.esp");
  const auto model = scalar_row("ACTI\\DestructionStageModel");
  const auto sequence = scalar_row("ACTI\\DestructionStageSequenceName");
  const auto color = scalar_row("ACTI\\DestructionStageColorRemapping");
  const auto material = link_row("ACTI\\DestructionStageMaterialSwap");

  EXPECT_EQ(fo4::set_record_string(plugin, record_key(), model, 1, "b.nif"),
            fo4::ScalarMutationStatus::updated);
  EXPECT_EQ(fo4::set_record_string(plugin, record_key(), model, 2, "b.nif"),
            fo4::ScalarMutationStatus::subrecord_missing);
  EXPECT_EQ(fo4::set_record_string(plugin, record_key(), sequence, 0, "seq1"),
            fo4::ScalarMutationStatus::updated);
  EXPECT_EQ(fo4::set_record_string(plugin, record_key(), sequence, 1, "seq1"),
            fo4::ScalarMutationStatus::subrecord_missing);
  EXPECT_EQ(fo4::set_record_scalar(plugin, record_key(), color, 0, "0.25"),
            fo4::ScalarMutationStatus::updated);
  EXPECT_EQ(fo4::set_record_scalar(plugin, record_key(), color, 1, "0.25"),
            fo4::ScalarMutationStatus::subrecord_missing);
  EXPECT_EQ(fo4::set_record_formlink_field(plugin, record_key(), material, 1,
                                           fo4::FormKey::parse("000A50:Fallout4.esm")),
            fo4::FormLinkFieldMutationStatus::subrecord_missing);
  EXPECT_EQ(material.payload_sizes[0], 4u);

  const auto reread = fo4::read_plugin(fo4::write_plugin(plugin), "Patch.esp");
  const auto first = payload_at(reread, "DMDL", 0);
  const auto second = payload_at(reread, "DMDL", 1);
  ASSERT_EQ(first.size(), 6u);
  ASSERT_EQ(second.size(), 6u);
  EXPECT_EQ(byte_at(first, 0), static_cast<std::uint8_t>('a'));
  EXPECT_EQ(byte_at(second, 0), static_cast<std::uint8_t>('b'));
  EXPECT_EQ(byte_at(second, 5), 0u);
  ByteWriter expected_sequence;
  expected_sequence.raw(std::string_view{"seq1\0", 5});
  EXPECT_EQ(payload_at(reread, "DSTA", 0), expected_sequence.bytes);
  EXPECT_FLOAT_EQ(f32_at(payload_at(reread, "DMDC", 0), 0), 0.25f);
  EXPECT_EQ(u32_at(payload_at(reread, "DMDS", 0), 0), k_dmds_raw);
}
