#include "fo4/plugin/mutation.hpp"
#include "fo4/plugin/reader.hpp"
#include "fo4/plugin/visit.hpp"
#include "fo4/plugin/writer.hpp"
#include "fo4/schema/field_address.hpp"
#include "fo4/schema/form_link_schema.hpp"
#include "fo4/schema/formlink_lists.hpp"
#include "fo4/schema/scalar.hpp"
#include "plugin_bytes.hpp"

#include <gtest/gtest.h>

#include <bit>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {

using Bytes = std::vector<std::byte>;
using fo4::test::ByteWriter;

const fo4::Signature k_achr = fo4::Signature::from_string("ACHR");

struct ScalarRow {
  std::string_view path;
  std::string_view subrecord;
  std::size_t offset;
  fo4::schema::ScalarStorage storage;
  bool embedded;
  std::size_t payload_size;
};

struct LinkRow {
  std::string_view path;
  std::string_view subrecord;
  std::size_t offset;
  std::uint32_t payload_size;
  std::string_view target;
};

constexpr auto f32 = fo4::schema::ScalarStorage::f32;
constexpr auto i32 = fo4::schema::ScalarStorage::i32;
constexpr auto u8 = fo4::schema::ScalarStorage::u8;
constexpr auto u32 = fo4::schema::ScalarStorage::u32;

constexpr ScalarRow k_scalar_rows[] = {
    {"ACHR\\Radius", "XRDS", 0, f32, false, 0},
    {"ACHR\\Count", "XCNT", 0, i32, false, 0},
    {"ACHR\\HealthPercent", "XHLT", 0, u32, false, 0},
    {"ACHR\\LockLevel", "XLOC", 0, u8, true, 16},
    {"ACHR\\LockFlags", "XLOC", 8, u8, true, 16},
    {"ACHR\\Scale", "XSCL", 0, f32, false, 0},
    {"ACHR\\HeadTrackingWeight", "XHTW", 0, f32, false, 0},
    {"ACHR\\FavorCost", "XFVC", 0, f32, false, 0},
    {"ACHR\\EnableParentFlags", "XESP", 4, u8, true, 8},
    {"ACHR\\OwnerRank", "XRNK", 0, i32, false, 0},
    {"ACHR\\OwnerNoCrime", "XOWN", 8, u8, true, 12},
    {"ACHR\\ReflectionType", "XPWR", 4, u32, true, 8},
    {"ACHR\\ActivateParentDelay", "XAPR", 4, f32, true, 8},
    {"ACHR\\WaterCurrentVelocityCount", "XWCN", 0, u32, false, 0},
    {"ACHR\\PositionX", "DATA", 0, f32, true, 24},
    {"ACHR\\PositionY", "DATA", 4, f32, true, 24},
    {"ACHR\\PositionZ", "DATA", 8, f32, true, 24},
    {"ACHR\\RotationX", "DATA", 12, f32, true, 24},
    {"ACHR\\RotationY", "DATA", 16, f32, true, 24},
    {"ACHR\\RotationZ", "DATA", 20, f32, true, 24},
    {"ACHR\\BipedRotationX", "XRGB", 0, f32, true, 12},
    {"ACHR\\BipedRotationY", "XRGB", 4, f32, true, 12},
    {"ACHR\\BipedRotationZ", "XRGB", 8, f32, true, 12},
};

constexpr LinkRow k_link_rows[] = {
    {"ACHR\\EncounterZone", "XEZN", 0, 4, "ECZN"},
    {"ACHR\\LockKey", "XLOC", 4, 16, "KEYM"},
    {"ACHR\\Emittance", "XEMI", 0, 4, "LIGH"},
    {"ACHR\\MultiboundReference", "XMBR", 0, 4, "REFR"},
    {"ACHR\\Base", "NAME", 0, 4, "NPC_"},
    {"ACHR\\EnableParent", "XESP", 0, 8, "PLYR"},
    {"ACHR\\Owner", "XOWN", 0, 12, "FACT"},
    {"ACHR\\ReferenceGroup", "XRFG", 0, 4, "RFGP"},
    {"ACHR\\Layer", "XLYR", 0, 4, "LAYR"},
    {"ACHR\\MaterialSwap", "XMSP", 0, 4, "MSWP"},
    {"ACHR\\AttachRef", "XATR", 0, 4, "ACHR"},
    {"ACHR\\PersistLocation", "XLCN", 0, 4, "LCTN"},
    {"ACHR\\LocationReference", "XLRL", 0, 4, "LCTN"},
    {"ACHR\\ReflectedBy", "XPWR", 0, 8, "ACHR"},
    {"ACHR\\LinkedReference", "XLKR", 4, 8, "REFR"},
    {"ACHR\\SplineConnection", "XPLK", 0, 8, "REFR"},
};

Bytes u32_bytes(std::uint32_t value) {
  ByteWriter out;
  out.u32(value);
  return out.bytes;
}

Bytes i32_bytes(std::int32_t value) {
  ByteWriter out;
  out.i32(value);
  return out.bytes;
}

Bytes f32_bytes(float value) {
  ByteWriter out;
  out.f32(value);
  return out.bytes;
}

std::string label_of(std::uint32_t value) {
  std::string out;
  for (int shift = 0; shift < 32; shift += 8) out.push_back(static_cast<char>(value >> shift));
  return out;
}

void write_actor(fo4::test::ByteWriter& out, std::uint32_t local, std::size_t lock_bytes) {
  const auto at = fo4::test::begin_record(out, "ACHR", 0, 0x01000000u | local);
  fo4::test::write_subrecord(out, "NAME", u32_bytes(0x00000A00u));
  ByteWriter transform;
  for (const float value : {1.0f, 2.0f, 3.0f, 0.0f, 0.0f, 0.5f}) transform.f32(value);
  fo4::test::write_subrecord(out, "DATA", transform.bytes);
  fo4::test::write_subrecord(out, "XRDS", f32_bytes(4.5f));
  fo4::test::write_subrecord(out, "XCNT", i32_bytes(-3));
  fo4::test::write_subrecord(out, "XHLT", u32_bytes(60));
  ByteWriter lock;
  lock.bytes.assign(lock_bytes, std::byte{0});
  if (lock_bytes >= 4) lock.patch_u32(0, 50);
  if (lock_bytes >= 8) lock.patch_u32(4, 0x00000A30u);
  if (lock_bytes >= 9) lock.bytes[8] = std::byte{0x04};
  fo4::test::write_subrecord(out, "XLOC", lock.bytes);
  fo4::test::write_subrecord(out, "XEZN", u32_bytes(0x00000A10u));
  fo4::test::write_subrecord(out, "XEMI", u32_bytes(0x00000A11u));
  fo4::test::write_subrecord(out, "XMBR", u32_bytes(0x00000A12u));
  fo4::test::write_subrecord(out, "XSCL", f32_bytes(1.5f));
  fo4::test::write_subrecord(out, "XHTW", f32_bytes(0.25f));
  fo4::test::write_subrecord(out, "XFVC", f32_bytes(7.0f));
  ByteWriter parent;
  parent.u32(0x00000A40u);
  parent.u8(0x01);
  parent.u8(0);
  parent.u8(0);
  parent.u8(0);
  fo4::test::write_subrecord(out, "XESP", parent.bytes);
  fo4::test::write_subrecord(out, "XRNK", i32_bytes(2));
  ByteWriter owner;
  owner.u32(0x00000A50u);
  owner.u32(0);
  owner.u8(0x01);
  owner.u8(0);
  owner.u8(0);
  owner.u8(0);
  fo4::test::write_subrecord(out, "XOWN", owner.bytes);
  fo4::test::write_subrecord(out, "XRFG", u32_bytes(0x00000A60u));
  fo4::test::write_subrecord(out, "XLYR", u32_bytes(0x00000A61u));
  fo4::test::write_subrecord(out, "XMSP", u32_bytes(0x00000A62u));
  fo4::test::write_subrecord(out, "XATR", u32_bytes(0x00000A63u));
  fo4::test::write_subrecord(out, "XPRD", f32_bytes(2.5f));
  fo4::test::write_subrecord(out, "INAM", u32_bytes(0x00000A80u));
  fo4::test::write_subrecord(out, "TNAM", u32_bytes(0x00000A81u));
  fo4::test::write_subrecord(out, "XLCM", i32_bytes(2));
  fo4::test::write_subrecord(out, "XLTW", u32_bytes(0x00000AA0u));
  ByteWriter ragdoll;
  for (const std::uint8_t bone : {std::uint8_t{3}, std::uint8_t{7}}) {
    ragdoll.u8(bone);
    ragdoll.u8(0);
    ragdoll.u8(0);
    ragdoll.u8(0);
    ragdoll.f32(1.0f);
    ragdoll.f32(2.0f);
    ragdoll.f32(3.0f);
    ragdoll.f32(4.0f);
    ragdoll.f32(5.0f);
    ragdoll.f32(6.0f);
  }
  fo4::test::write_subrecord(out, "XRGD", ragdoll.bytes);
  ByteWriter reflected;
  reflected.u32(0x00000A70u);
  reflected.u32(1);
  fo4::test::write_subrecord(out, "XPWR", reflected.bytes);
  ByteWriter refracted;
  refracted.u32(0x00000A71u);
  refracted.u32(2);
  fo4::test::write_subrecord(out, "XPWR", refracted.bytes);
  fo4::test::end_record(out, at);
}

std::vector<std::byte> make_plugin() {
  fo4::test::ByteWriter out;
  const auto tes4 = fo4::test::begin_record(out, "TES4", 0, 0);
  ByteWriter hedr;
  hedr.f32(1.0f);
  hedr.u32(0);
  hedr.u32(0x900);
  fo4::test::write_subrecord(out, "HEDR", hedr.bytes);
  fo4::test::write_subrecord(out, "MAST", std::string_view{"Fallout4.esm\0", 13});
  ByteWriter master_data;
  master_data.u32(0);
  master_data.u32(0);
  fo4::test::write_subrecord(out, "DATA", master_data.bytes);
  fo4::test::end_record(out, tes4);

  const auto cell_group = fo4::test::begin_group(out, "CELL", 0);
  const auto cell = fo4::test::begin_record(out, "CELL", 0, 0x01000000u | 0x800u);
  fo4::test::write_subrecord(out, "EDID", std::string_view{"Garden\0", 7});
  fo4::test::end_record(out, cell);
  const auto label = label_of(0x01000000u | 0x800u);
  const auto children = fo4::test::begin_group(out, label, 6);
  const auto persistent = fo4::test::begin_group(out, label, 8);
  write_actor(out, 0x810u, 16);
  fo4::test::end_group(out, persistent);
  const auto temporary = fo4::test::begin_group(out, label, 9);
  write_actor(out, 0x811u, 4);
  fo4::test::end_group(out, temporary);
  fo4::test::end_group(out, children);
  fo4::test::end_group(out, cell_group);
  return out.bytes;
}

fo4::FormKey patch_key(std::uint32_t local) {
  return fo4::FormKey{fo4::PluginKey::from_filename("Patch.esp"), fo4::LocalFormId{local}};
}

const fo4::Record& actor(const fo4::Plugin& plugin, std::uint32_t local) {
  const auto* record = fo4::find_record(plugin, patch_key(local));
  if (record == nullptr) throw std::runtime_error("missing ACHR");
  return *record;
}

std::size_t xloc_index(const fo4::Record& record) {
  std::size_t index = 0;
  for (const auto& sub : record.subrecords) {
    if (sub.signature == fo4::Signature::from_string("XLOC")) return index;
    ++index;
  }
  return index;
}

std::optional<std::span<const std::byte>> scalar_bytes(const fo4::Record& record,
                                                       std::string_view path) {
  const auto field = fo4::schema::scalar_field_descriptor(k_achr, path);
  if (!field) return std::nullopt;
  for (const auto& sub : record.subrecords) {
    if (sub.signature != field->scalar.subrecord) continue;
    return fo4::schema::scalar_field_bytes(sub.data, field->scalar);
  }
  return std::nullopt;
}

std::optional<std::uint32_t> link_raw(const fo4::Record& record, std::string_view path) {
  const auto field = fo4::schema::formlink_field_descriptor(k_achr, path);
  if (!field) return std::nullopt;
  for (const auto& sub : record.subrecords) {
    if (sub.signature != field->subrecord) continue;
    if (sub.data.size() != field->payload_sizes[0]) return std::nullopt;
    std::uint32_t value = 0;
    std::memcpy(&value, sub.data.data() + field->offset, sizeof(value));
    return value;
  }
  return std::nullopt;
}

std::optional<std::uint32_t> link_at(const fo4::Record& record,
                                     const fo4::schema::FormLinkFieldDescriptor& field,
                                     std::size_t index) {
  std::size_t seen = 0;
  for (const auto& sub : record.subrecords) {
    if (sub.signature != field.subrecord) continue;
    if (seen++ != index) continue;
    if (sub.data.size() < field.offset + 4) return std::nullopt;
    std::uint32_t value = 0;
    std::memcpy(&value, sub.data.data() + field.offset, sizeof(value));
    return value;
  }
  return std::nullopt;
}

std::string read_row(const fo4::Record& record,
                     const fo4::schema::ScalarFieldDescriptor& field,
                     std::size_t index) {
  const auto resolved = fo4::schema::resolve_field_slots(
      record, fo4::schema::scalar_field_shape(field.scalar), field.scalar.address);
  if (index >= resolved.slots.size()) return {};
  const auto slot = resolved.slots[index];
  return fo4::schema::decode_scalar(
      fo4::schema::scalar_slot_bytes(record.subrecords[slot.subrecord].data, slot, field.scalar),
      field.scalar);
}

std::vector<Bytes> xpwr_payloads(const fo4::Record& record) {
  std::vector<Bytes> out;
  for (const auto& sub : record.subrecords) {
    if (sub.signature == fo4::Signature::from_string("XPWR")) {
      out.emplace_back(sub.data.begin(), sub.data.end());
    }
  }
  return out;
}

Bytes sub_bytes(const fo4::Plugin& plugin, std::uint32_t local, std::string_view signature) {
  for (const auto& sub : actor(plugin, local).subrecords) {
    if (sub.signature == fo4::Signature::from_string(signature)) {
      return Bytes(sub.data.begin(), sub.data.end());
    }
  }
  throw std::runtime_error("missing subrecord");
}

}  // namespace

TEST(AchrRows, ScalarRowsHaveXEditOffsetsStorageAndPayloadGates) {
  EXPECT_EQ(fo4::schema::scalar_fields(k_achr).size(), 38u);
  for (const auto& row : k_scalar_rows) {
    SCOPED_TRACE(row.path);
    const auto field = fo4::schema::scalar_field_descriptor(k_achr, row.path);
    ASSERT_TRUE(field.has_value());
    EXPECT_EQ(field->scalar.subrecord, fo4::Signature::from_string(row.subrecord));
    EXPECT_EQ(field->scalar.embedded, row.embedded);
    EXPECT_EQ(field->scalar.offset, row.offset);
    EXPECT_EQ(field->scalar.storage, row.storage);
    EXPECT_EQ(field->scalar.payload_size, row.payload_size);
    EXPECT_EQ(field->scalar.address.repeated,
              std::string_view{row.path} == "ACHR\\ReflectionType" ||
                  std::string_view{row.path} == "ACHR\\ActivateParentDelay");
  }
  EXPECT_FALSE(fo4::schema::scalar_field_descriptor(k_achr, "ACHR\\AlphaBase").has_value());
}

TEST(AchrRows, LockRowsShareTheXlocPayloadAndTheKeyRowIsNullable) {
  const auto level = fo4::schema::scalar_field_descriptor(k_achr, "ACHR\\LockLevel");
  const auto flags = fo4::schema::scalar_field_descriptor(k_achr, "ACHR\\LockFlags");
  ASSERT_TRUE(level.has_value());
  ASSERT_TRUE(flags.has_value());
  EXPECT_EQ(level->scalar.subrecord, flags->scalar.subrecord);
  EXPECT_EQ(level->scalar.payload_size, 16u);
  const auto level_name = fo4::schema::scalar_value_name(level->scalar, 50);
  ASSERT_TRUE(level_name.has_value());
  EXPECT_EQ(*level_name, "Advanced");
  const auto flags_name = fo4::schema::scalar_value_name(flags->scalar, 0x04);
  ASSERT_TRUE(flags_name.has_value());
  EXPECT_EQ(*flags_name, "Leveled Lock");
  EXPECT_FALSE(fo4::schema::scalar_value_name(flags->scalar, 0x01).has_value());
}

TEST(AchrRows, LinkRowsNameTheirSubrecordsOffsetsAndTargets) {
  EXPECT_EQ(fo4::schema::formlink_fields(k_achr).size(), 21u);
  for (const auto& row : k_link_rows) {
    SCOPED_TRACE(row.path);
    const auto field = fo4::schema::formlink_field_descriptor(k_achr, row.path);
    ASSERT_TRUE(field.has_value());
    EXPECT_EQ(field->subrecord, fo4::Signature::from_string(row.subrecord));
    EXPECT_EQ(field->offset, row.offset);
    EXPECT_EQ(field->payload_sizes[0], row.payload_size);
    EXPECT_EQ(field->nullable,
              std::string_view{row.path} == "ACHR\\LockKey" ||
                  std::string_view{row.path} == "ACHR\\EnableParent" ||
                  std::string_view{row.path} == "ACHR\\LocationReference" ||
                  std::string_view{row.path} == "ACHR\\LinkedReference");
    EXPECT_TRUE(fo4::schema::formlink_field_target_allowed(
        *field, fo4::Signature::from_string(row.target)));
  }
  const auto owner = fo4::schema::formlink_field_descriptor(k_achr, "ACHR\\Owner");
  ASSERT_TRUE(owner.has_value());
  EXPECT_TRUE(fo4::schema::formlink_field_target_allowed(
      *owner, fo4::Signature::from_string("NPC_")));
  EXPECT_FALSE(fo4::schema::formlink_field_target_allowed(
      *owner, fo4::Signature::from_string("STAT")));
  const auto attach = fo4::schema::formlink_field_descriptor(k_achr, "ACHR\\AttachRef");
  ASSERT_TRUE(attach.has_value());
  EXPECT_TRUE(fo4::schema::formlink_field_target_allowed(
      *attach, fo4::Signature::from_string("REFR")));
  EXPECT_FALSE(fo4::schema::formlink_field_target_allowed(
      *attach, fo4::Signature::from_string("STAT")));
  const auto emittance = fo4::schema::formlink_field_descriptor(k_achr, "ACHR\\Emittance");
  ASSERT_TRUE(emittance.has_value());
  EXPECT_TRUE(fo4::schema::formlink_field_target_allowed(
      *emittance, fo4::Signature::from_string("REGN")));
  EXPECT_FALSE(fo4::schema::formlink_field_target_allowed(
      *emittance, fo4::Signature::from_string("STAT")));
  const auto multibound = fo4::schema::formlink_field_descriptor(k_achr, "ACHR\\MultiboundReference");
  ASSERT_TRUE(multibound.has_value());
  EXPECT_TRUE(fo4::schema::formlink_field_target_allowed(
      *multibound, fo4::Signature::from_string("ACHR")));
  EXPECT_TRUE(fo4::schema::formlink_field_target_allowed(
      *multibound, fo4::Signature::from_string("PLYR")));
  EXPECT_FALSE(fo4::schema::formlink_field_target_allowed(
      *multibound, fo4::Signature::from_string("STAT")));
}

TEST(AchrRows, ReadsScalarsAndLinksFromThePlugin) {
  auto plugin = fo4::read_plugin(make_plugin(), "Patch.esp");
  const auto& full = actor(plugin, 0x810u);

  const auto radius = scalar_bytes(full, "ACHR\\Radius");
  ASSERT_TRUE(radius.has_value());
  EXPECT_EQ(Bytes(radius->begin(), radius->end()), f32_bytes(4.5f));
  const auto count = scalar_bytes(full, "ACHR\\Count");
  ASSERT_TRUE(count.has_value());
  EXPECT_EQ(Bytes(count->begin(), count->end()), i32_bytes(-3));
  const auto health = scalar_bytes(full, "ACHR\\HealthPercent");
  ASSERT_TRUE(health.has_value());
  EXPECT_EQ(Bytes(health->begin(), health->end()), u32_bytes(60));
  const auto level = scalar_bytes(full, "ACHR\\LockLevel");
  ASSERT_TRUE(level.has_value());
  ASSERT_EQ(level->size(), 1u);
  EXPECT_EQ(std::to_integer<std::uint8_t>((*level)[0]), 50);
  const auto flags = scalar_bytes(full, "ACHR\\LockFlags");
  ASSERT_TRUE(flags.has_value());
  ASSERT_EQ(flags->size(), 1u);
  EXPECT_EQ(std::to_integer<std::uint8_t>((*flags)[0]), 0x04);

  EXPECT_EQ(link_raw(full, "ACHR\\LockKey"), 0x00000A30u);
  EXPECT_EQ(link_raw(full, "ACHR\\EncounterZone"), 0x00000A10u);
  EXPECT_EQ(link_raw(full, "ACHR\\Emittance"), 0x00000A11u);
  EXPECT_EQ(link_raw(full, "ACHR\\MultiboundReference"), 0x00000A12u);

  const auto scale = scalar_bytes(full, "ACHR\\Scale");
  ASSERT_TRUE(scale.has_value());
  EXPECT_EQ(Bytes(scale->begin(), scale->end()), f32_bytes(1.5f));
  const auto track = scalar_bytes(full, "ACHR\\HeadTrackingWeight");
  ASSERT_TRUE(track.has_value());
  EXPECT_EQ(Bytes(track->begin(), track->end()), f32_bytes(0.25f));
  const auto favor = scalar_bytes(full, "ACHR\\FavorCost");
  ASSERT_TRUE(favor.has_value());
  EXPECT_EQ(Bytes(favor->begin(), favor->end()), f32_bytes(7.0f));
  const auto rank = scalar_bytes(full, "ACHR\\OwnerRank");
  ASSERT_TRUE(rank.has_value());
  EXPECT_EQ(Bytes(rank->begin(), rank->end()), i32_bytes(2));

  const auto parent_flags = scalar_bytes(full, "ACHR\\EnableParentFlags");
  ASSERT_TRUE(parent_flags.has_value());
  ASSERT_EQ(parent_flags->size(), 1u);
  EXPECT_EQ(std::to_integer<std::uint8_t>((*parent_flags)[0]), 0x01);
  const auto no_crime = scalar_bytes(full, "ACHR\\OwnerNoCrime");
  ASSERT_TRUE(no_crime.has_value());
  ASSERT_EQ(no_crime->size(), 1u);
  EXPECT_EQ(std::to_integer<std::uint8_t>((*no_crime)[0]), 0x01);

  EXPECT_EQ(link_raw(full, "ACHR\\Base"), 0x00000A00u);
  EXPECT_EQ(link_raw(full, "ACHR\\EnableParent"), 0x00000A40u);
  EXPECT_EQ(link_raw(full, "ACHR\\Owner"), 0x00000A50u);
  EXPECT_EQ(link_raw(full, "ACHR\\ReferenceGroup"), 0x00000A60u);
  EXPECT_EQ(link_raw(full, "ACHR\\Layer"), 0x00000A61u);
  EXPECT_EQ(link_raw(full, "ACHR\\MaterialSwap"), 0x00000A62u);
  EXPECT_EQ(link_raw(full, "ACHR\\AttachRef"), 0x00000A63u);

  const auto reflection = scalar_bytes(full, "ACHR\\ReflectionType");
  ASSERT_TRUE(reflection.has_value());
  EXPECT_EQ(Bytes(reflection->begin(), reflection->end()), u32_bytes(1));
  EXPECT_EQ(link_raw(full, "ACHR\\ReflectedBy"), 0x00000A70u);
}

TEST(AchrRows, RepeatedXpwrRowsAddressEachOccurrenceAndGateShortPayloads) {
  auto plugin = fo4::read_plugin(make_plugin(), "Patch.esp");
  const auto& full = actor(plugin, 0x810u);
  const auto type = fo4::schema::scalar_field_descriptor(k_achr, "ACHR\\ReflectionType");
  const auto source = fo4::schema::formlink_field_descriptor(k_achr, "ACHR\\ReflectedBy");
  ASSERT_TRUE(type.has_value());
  ASSERT_TRUE(source.has_value());
  EXPECT_TRUE(type->scalar.address.repeated);
  EXPECT_TRUE(source->address.repeated);
  EXPECT_EQ(source->payload_sizes.size(), 2u);
  EXPECT_EQ(source->payload_sizes[0], 8u);
  EXPECT_EQ(source->payload_sizes[1], 4u);
  EXPECT_EQ(fo4::record_field_count(plugin, patch_key(0x810u), "ACHR\\ReflectionType"), 2u);
  EXPECT_EQ(fo4::record_field_count(plugin, patch_key(0x810u), "ACHR\\ReflectedBy"), 2u);

  EXPECT_EQ(read_row(full, *type, 0), "1");
  EXPECT_EQ(read_row(full, *type, 1), "2");
  EXPECT_EQ(read_row(full, *type, 2), "");
  ASSERT_TRUE(fo4::schema::scalar_value_name(type->scalar, 1).has_value());
  EXPECT_EQ(*fo4::schema::scalar_value_name(type->scalar, 1), "Reflection");
  ASSERT_TRUE(fo4::schema::scalar_value_name(type->scalar, 2).has_value());
  EXPECT_EQ(*fo4::schema::scalar_value_name(type->scalar, 2), "Refraction");
  EXPECT_FALSE(fo4::schema::scalar_value_name(type->scalar, 4).has_value());

  EXPECT_EQ(fo4::set_record_scalar(plugin, patch_key(0x810u), *type, 1, "3"),
            fo4::ScalarMutationStatus::updated);
  EXPECT_EQ(fo4::set_record_scalar(plugin, patch_key(0x810u), *type, 2, "3"),
            fo4::ScalarMutationStatus::subrecord_missing);
  EXPECT_EQ(fo4::set_record_formlink_field(plugin, patch_key(0x810u), *source, 1,
                                           fo4::FormKey::parse("00000A11:Fallout4.esm")),
            fo4::FormLinkFieldMutationStatus::updated);
  EXPECT_THROW(static_cast<void>(fo4::set_record_formlink_field(plugin, patch_key(0x810u), *source, 1,
                                                               fo4::FormKey{})),
               fo4::schema::FormLinkFieldError);

  const auto reread = fo4::read_plugin(fo4::write_plugin(plugin), "Patch.esp");
  const auto& written = actor(reread, 0x810u);
  const auto entries = xpwr_payloads(written);
  ASSERT_EQ(entries.size(), 2u);
  EXPECT_EQ(std::to_integer<std::uint8_t>(entries[0][4]), 1u);
  EXPECT_EQ(std::to_integer<std::uint8_t>(entries[1][4]), 3u);
  EXPECT_EQ(link_at(written, *source, 0), 0x00000A70u);
  EXPECT_EQ(link_at(written, *source, 1), 0x00000A11u);

  auto short_payload = fo4::read_plugin(make_plugin(), "Patch.esp");
  fo4::visit_records(short_payload, [](fo4::Record& record) {
    if (record.form_key.local().value() != 0x810u) return;
    for (auto& sub : record.subrecords) {
      if (sub.signature == fo4::Signature::from_string("XPWR")) sub.data.resize(4);
    }
  });
  const auto& shortened = actor(short_payload, 0x810u);
  const auto gated = scalar_bytes(shortened, "ACHR\\ReflectionType");
  ASSERT_TRUE(gated.has_value());
  EXPECT_TRUE(gated->empty());
  EXPECT_EQ(link_at(shortened, *source, 0), 0x00000A70u);
}

TEST(AchrRows, PatrolRowsReadAndWriteIdleTimeIdleAndTopic) {
  auto plugin = fo4::read_plugin(make_plugin(), "Patch.esp");
  const auto& full = actor(plugin, 0x810u);
  const auto time = fo4::schema::scalar_field_descriptor(k_achr, "ACHR\\PatrolIdleTime");
  const auto idle = fo4::schema::formlink_field_descriptor(k_achr, "ACHR\\PatrolIdle");
  const auto topic = fo4::schema::formlink_field_descriptor(k_achr, "ACHR\\PatrolTopic");
  ASSERT_TRUE(time.has_value());
  ASSERT_TRUE(idle.has_value());
  ASSERT_TRUE(topic.has_value());
  EXPECT_EQ(time->scalar.subrecord, fo4::Signature::from_string("XPRD"));
  EXPECT_FALSE(time->scalar.address.repeated);
  EXPECT_TRUE(idle->nullable);
  EXPECT_TRUE(topic->nullable);
  EXPECT_TRUE(fo4::schema::formlink_field_target_allowed(
      *idle, fo4::Signature::from_string("IDLE")));
  EXPECT_FALSE(fo4::schema::formlink_field_target_allowed(
      *idle, fo4::Signature::from_string("DIAL")));
  EXPECT_TRUE(fo4::schema::formlink_field_target_allowed(
      *topic, fo4::Signature::from_string("DIAL")));
  EXPECT_FALSE(fo4::schema::formlink_field_target_allowed(
      *topic, fo4::Signature::from_string("IDLE")));

  const auto before = scalar_bytes(full, "ACHR\\PatrolIdleTime");
  ASSERT_TRUE(before.has_value());
  EXPECT_EQ(Bytes(before->begin(), before->end()), f32_bytes(2.5f));
  EXPECT_EQ(link_raw(full, "ACHR\\PatrolIdle"), 0x00000A80u);
  EXPECT_EQ(link_raw(full, "ACHR\\PatrolTopic"), 0x00000A81u);

  EXPECT_EQ(fo4::set_record_scalar(plugin, patch_key(0x810u), *time, 0, "4.5"),
            fo4::ScalarMutationStatus::updated);
  EXPECT_EQ(fo4::set_record_scalar(plugin, patch_key(0x810u), *time, 1, "4.5"),
            fo4::ScalarMutationStatus::subrecord_missing);
  EXPECT_EQ(fo4::set_record_formlink_field(plugin, patch_key(0x810u), *idle, 0,
                                           fo4::FormKey::parse("00000A90:Fallout4.esm")),
            fo4::FormLinkFieldMutationStatus::updated);
  EXPECT_EQ(fo4::set_record_formlink_field(plugin, patch_key(0x810u), *topic, 1,
                                           fo4::FormKey::parse("00000A91:Fallout4.esm")),
            fo4::FormLinkFieldMutationStatus::subrecord_missing);

  const auto reread = fo4::read_plugin(fo4::write_plugin(plugin), "Patch.esp");
  const auto& written = actor(reread, 0x810u);
  const auto after = scalar_bytes(written, "ACHR\\PatrolIdleTime");
  ASSERT_TRUE(after.has_value());
  EXPECT_EQ(Bytes(after->begin(), after->end()), f32_bytes(4.5f));
  EXPECT_EQ(link_raw(written, "ACHR\\PatrolIdle"), 0x00000A90u);
  EXPECT_EQ(link_raw(written, "ACHR\\PatrolTopic"), 0x00000A81u);
  EXPECT_EQ(fo4::set_record_formlink_field(plugin, patch_key(0x810u), *topic, 0, fo4::FormKey{}),
            fo4::FormLinkFieldMutationStatus::updated);
  const auto cleared = fo4::read_plugin(fo4::write_plugin(plugin), "Patch.esp");
  EXPECT_EQ(link_raw(actor(cleared, 0x810u), "ACHR\\PatrolTopic"), 0u);
}

TEST(AchrRows, LevelModifierAndLitWaterRowsReadAndWrite) {
  auto plugin = fo4::read_plugin(make_plugin(), "Patch.esp");
  const auto& full = actor(plugin, 0x810u);
  const auto modifier = fo4::schema::scalar_field_descriptor(k_achr, "ACHR\\LevelModifier");
  const auto lit_water = fo4::schema::formlink_field_descriptor(k_achr, "ACHR\\LitWater");
  ASSERT_TRUE(modifier.has_value());
  ASSERT_TRUE(lit_water.has_value());
  EXPECT_EQ(modifier->scalar.subrecord, fo4::Signature::from_string("XLCM"));
  EXPECT_EQ(modifier->scalar.storage, fo4::schema::ScalarStorage::i32);
  EXPECT_FALSE(modifier->scalar.address.repeated);
  EXPECT_EQ(fo4::schema::scalar_value_name(modifier->scalar, 2), "Hard");
  EXPECT_EQ(fo4::schema::scalar_value_name(modifier->scalar, 3), "Very Hard");
  EXPECT_FALSE(fo4::schema::scalar_value_name(modifier->scalar, 4).has_value());
  EXPECT_TRUE(lit_water->address.repeated);
  EXPECT_FALSE(lit_water->nullable);
  EXPECT_TRUE(fo4::schema::formlink_field_target_allowed(
      *lit_water, fo4::Signature::from_string("REFR")));

  const auto before = scalar_bytes(full, "ACHR\\LevelModifier");
  ASSERT_TRUE(before.has_value());
  EXPECT_EQ(Bytes(before->begin(), before->end()), i32_bytes(2));
  EXPECT_EQ(link_raw(full, "ACHR\\LitWater"), 0x00000AA0u);

  EXPECT_EQ(fo4::set_record_scalar(plugin, patch_key(0x810u), *modifier, 0, "3"),
            fo4::ScalarMutationStatus::updated);
  EXPECT_EQ(fo4::set_record_formlink_field(plugin, patch_key(0x810u), *lit_water, 0,
                                           fo4::FormKey::parse("00000AA1:Fallout4.esm")),
            fo4::FormLinkFieldMutationStatus::updated);
  EXPECT_THROW(static_cast<void>(fo4::set_record_formlink_field(
                   plugin, patch_key(0x810u), *lit_water, 0, fo4::FormKey{})),
               fo4::schema::FormLinkFieldError);

  const auto reread = fo4::read_plugin(fo4::write_plugin(plugin), "Patch.esp");
  const auto& written = actor(reread, 0x810u);
  const auto after = scalar_bytes(written, "ACHR\\LevelModifier");
  ASSERT_TRUE(after.has_value());
  EXPECT_EQ(Bytes(after->begin(), after->end()), i32_bytes(3));
  EXPECT_EQ(link_raw(written, "ACHR\\LitWater"), 0x00000AA1u);
}

TEST(AchrRows, StridedRagdollRowsAddressEachBoneAndGateTheStride) {
  auto plugin = fo4::read_plugin(make_plugin(), "Patch.esp");
  const auto& full = actor(plugin, 0x810u);
  const auto bone = fo4::schema::scalar_field_descriptor(k_achr, "ACHR\\RagdollBoneId");
  const auto position_x = fo4::schema::scalar_field_descriptor(k_achr, "ACHR\\RagdollPositionX");
  const auto rotation_z = fo4::schema::scalar_field_descriptor(k_achr, "ACHR\\RagdollRotationZ");
  ASSERT_TRUE(bone.has_value());
  ASSERT_TRUE(position_x.has_value());
  ASSERT_TRUE(rotation_z.has_value());
  EXPECT_EQ(bone->scalar.subrecord, fo4::Signature::from_string("XRGD"));
  EXPECT_TRUE(bone->scalar.embedded);
  EXPECT_FALSE(bone->scalar.address.repeated);
  EXPECT_EQ(bone->scalar.address.stride, 28u);
  EXPECT_EQ(position_x->scalar.offset, 4u);
  EXPECT_EQ(rotation_z->scalar.offset, 24u);
  EXPECT_EQ(fo4::record_field_count(plugin, patch_key(0x810u), *bone), 2u);

  EXPECT_EQ(read_row(full, *bone, 0), "3");
  EXPECT_EQ(read_row(full, *bone, 1), "7");
  EXPECT_EQ(read_row(full, *bone, 2), "");
  EXPECT_EQ(read_row(full, *position_x, 1), "1");
  EXPECT_EQ(read_row(full, *rotation_z, 0), "6");
  EXPECT_EQ(read_row(full, *rotation_z, 1), "6");

  EXPECT_EQ(fo4::set_record_scalar(plugin, patch_key(0x810u), *bone, 1, "9"),
            fo4::ScalarMutationStatus::updated);
  EXPECT_EQ(fo4::set_record_scalar(plugin, patch_key(0x810u), *rotation_z, 1, "-2.5"),
            fo4::ScalarMutationStatus::updated);
  EXPECT_EQ(fo4::set_record_scalar(plugin, patch_key(0x810u), *rotation_z, 2, "1"),
            fo4::ScalarMutationStatus::subrecord_missing);

  const auto reread = fo4::read_plugin(fo4::write_plugin(plugin), "Patch.esp");
  const auto& written = actor(reread, 0x810u);
  EXPECT_EQ(read_row(written, *bone, 0), "3");
  EXPECT_EQ(read_row(written, *bone, 1), "9");
  EXPECT_EQ(read_row(written, *rotation_z, 0), "6");
  EXPECT_EQ(read_row(written, *rotation_z, 1), "-2.5");

  auto truncated = fo4::read_plugin(make_plugin(), "Patch.esp");
  fo4::visit_records(truncated, [](fo4::Record& record) {
    if (record.form_key.local().value() != 0x810u) return;
    for (auto& sub : record.subrecords) {
      if (sub.signature == fo4::Signature::from_string("XRGD")) sub.data.resize(30);
    }
  });
  const auto& misaligned = actor(truncated, 0x810u);
  EXPECT_EQ(fo4::record_field_count(truncated, patch_key(0x810u), *bone), 0u);
  EXPECT_EQ(read_row(misaligned, *bone, 0), "");
  EXPECT_EQ(read_row(misaligned, *rotation_z, 0), "");
}

TEST(AchrRows, ShortLockPayloadReadsAsAbsentAndDropsTheKeyLinkOffset) {
  auto plugin = fo4::read_plugin(make_plugin(), "Patch.esp");
  const auto& full = actor(plugin, 0x810u);
  const auto& short_lock = actor(plugin, 0x811u);
  EXPECT_EQ(short_lock.subrecords.size(), 27u);

  const auto level = scalar_bytes(short_lock, "ACHR\\LockLevel");
  ASSERT_TRUE(level.has_value());
  EXPECT_TRUE(level->empty());
  const auto flags = scalar_bytes(short_lock, "ACHR\\LockFlags");
  ASSERT_TRUE(flags.has_value());
  EXPECT_TRUE(flags->empty());
  const auto short_radius = scalar_bytes(short_lock, "ACHR\\Radius");
  ASSERT_TRUE(short_radius.has_value());
  EXPECT_FALSE(short_radius->empty());

  const auto offsets = fo4::schema::schema_formlink_offsets(short_lock);
  ASSERT_EQ(offsets.size(), short_lock.subrecords.size());
  EXPECT_TRUE(offsets[xloc_index(short_lock)].empty());
  const auto full_offsets = fo4::schema::schema_formlink_offsets(full);
  ASSERT_EQ(full_offsets.size(), full.subrecords.size());
  EXPECT_EQ(full_offsets[xloc_index(full)], std::vector<std::size_t>{4});
}

TEST(AchrRows, WholePayloadRowOfTheWrongWidthThrows) {
  auto plugin = fo4::read_plugin(make_plugin(), "Patch.esp");
  fo4::visit_records(plugin, [](fo4::Record& record) {
    if (record.form_key.local().value() != 0x810u) return;
    for (auto& sub : record.subrecords) {
      if (sub.signature == fo4::Signature::from_string("XRDS")) sub.data.resize(2);
    }
  });
  EXPECT_THROW(static_cast<void>(scalar_bytes(actor(plugin, 0x810u), "ACHR\\Radius")),
               fo4::schema::ScalarError);
}

TEST(AchrRows, WritesScalarsAndLinksThenRereads) {
  auto plugin = fo4::read_plugin(make_plugin(), "Patch.esp");
  const auto health = fo4::schema::scalar_field_descriptor(k_achr, "ACHR\\HealthPercent");
  const auto level = fo4::schema::scalar_field_descriptor(k_achr, "ACHR\\LockLevel");
  const auto lock_key = fo4::schema::formlink_field_descriptor(k_achr, "ACHR\\LockKey");
  const auto zone = fo4::schema::formlink_field_descriptor(k_achr, "ACHR\\EncounterZone");
  ASSERT_TRUE(health.has_value());
  ASSERT_TRUE(level.has_value());
  ASSERT_TRUE(lock_key.has_value());
  ASSERT_TRUE(zone.has_value());

  EXPECT_EQ(fo4::set_record_scalar(plugin, patch_key(0x810u), *health, "75"),
            fo4::ScalarMutationStatus::updated);
  EXPECT_EQ(fo4::set_record_scalar(plugin, patch_key(0x810u), *level, "75"),
            fo4::ScalarMutationStatus::updated);
  EXPECT_EQ(fo4::set_record_formlink_field(plugin, patch_key(0x810u), *lock_key,
                                           fo4::FormKey::parse("00000A31:Fallout4.esm")),
            fo4::FormLinkFieldMutationStatus::updated);
  EXPECT_EQ(fo4::set_record_formlink_field(plugin, patch_key(0x810u), *zone,
                                           fo4::FormKey::parse("00000005:Fallout4.esm")),
            fo4::FormLinkFieldMutationStatus::updated);
  EXPECT_EQ(fo4::set_record_scalar(plugin, patch_key(0x999u), *health, "75"),
            fo4::ScalarMutationStatus::record_missing);
  EXPECT_EQ(fo4::set_record_formlink_field(plugin, patch_key(0x999u), *lock_key,
                                           fo4::FormKey::parse("00000A30:Fallout4.esm")),
            fo4::FormLinkFieldMutationStatus::record_missing);

  const auto reread = fo4::read_plugin(fo4::write_plugin(plugin), "Patch.esp");
  const auto& full = actor(reread, 0x810u);
  EXPECT_EQ(sub_bytes(reread, 0x810u, "XHLT"), u32_bytes(75));
  EXPECT_EQ(sub_bytes(reread, 0x810u, "XCNT"), i32_bytes(-3));
  EXPECT_EQ(sub_bytes(reread, 0x810u, "XRDS"), f32_bytes(4.5f));
  const auto lock = sub_bytes(reread, 0x810u, "XLOC");
  ASSERT_EQ(lock.size(), 16u);
  EXPECT_EQ(std::to_integer<std::uint8_t>(lock[0]), 75);
  EXPECT_EQ(std::to_integer<std::uint8_t>(lock[8]), 0x04);
  EXPECT_EQ(link_raw(full, "ACHR\\LockKey"), 0x00000A31u);
  EXPECT_EQ(link_raw(full, "ACHR\\EncounterZone"), 0x00000005u);
}
