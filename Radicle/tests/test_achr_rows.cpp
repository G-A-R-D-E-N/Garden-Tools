#include "fo4/plugin/mutation.hpp"
#include "fo4/plugin/reader.hpp"
#include "fo4/plugin/visit.hpp"
#include "fo4/plugin/writer.hpp"
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
};

constexpr LinkRow k_link_rows[] = {
    {"ACHR\\EncounterZone", "XEZN", 0, 4, "ECZN"},
    {"ACHR\\LockKey", "XLOC", 4, 16, "KEYM"},
    {"ACHR\\Emittance", "XEMI", 0, 4, "LIGH"},
    {"ACHR\\MultiboundReference", "XMBR", 0, 4, "REFR"},
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
  if (lock_bytes >= 4) {
    lock.patch_u32(0, 50);
    lock.patch_u32(4, 0x00000A30u);
  }
  if (lock_bytes >= 9) lock.bytes[8] = std::byte{0x04};
  fo4::test::write_subrecord(out, "XLOC", lock.bytes);
  fo4::test::write_subrecord(out, "XEZN", u32_bytes(0x00000A10u));
  fo4::test::write_subrecord(out, "XEMI", u32_bytes(0x00000A11u));
  fo4::test::write_subrecord(out, "XMBR", u32_bytes(0x00000A12u));
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
  EXPECT_EQ(fo4::schema::scalar_fields(k_achr).size(), 5u);
  for (const auto& row : k_scalar_rows) {
    SCOPED_TRACE(row.path);
    const auto field = fo4::schema::scalar_field_descriptor(k_achr, row.path);
    ASSERT_TRUE(field.has_value());
    EXPECT_EQ(field->scalar.subrecord, fo4::Signature::from_string(row.subrecord));
    EXPECT_EQ(field->scalar.embedded, row.embedded);
    EXPECT_EQ(field->scalar.offset, row.offset);
    EXPECT_EQ(field->scalar.storage, row.storage);
    EXPECT_EQ(field->scalar.payload_size, row.payload_size);
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
  EXPECT_EQ(fo4::schema::formlink_fields(k_achr).size(), 4u);
  for (const auto& row : k_link_rows) {
    SCOPED_TRACE(row.path);
    const auto field = fo4::schema::formlink_field_descriptor(k_achr, row.path);
    ASSERT_TRUE(field.has_value());
    EXPECT_EQ(field->subrecord, fo4::Signature::from_string(row.subrecord));
    EXPECT_EQ(field->offset, row.offset);
    EXPECT_EQ(field->payload_sizes[0], row.payload_size);
    EXPECT_EQ(field->nullable, std::string_view{row.path} == "ACHR\\LockKey");
    EXPECT_TRUE(fo4::schema::formlink_field_target_allowed(
        *field, fo4::Signature::from_string(row.target)));
  }
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
  const auto plugin = fo4::read_plugin(make_plugin(), "Patch.esp");
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
}

TEST(AchrRows, ShortLockPayloadReadsAsAbsentAndDropsTheKeyLinkOffset) {
  const auto plugin = fo4::read_plugin(make_plugin(), "Patch.esp");
  const auto& full = actor(plugin, 0x810u);
  const auto& short_lock = actor(plugin, 0x811u);
  EXPECT_EQ(short_lock.subrecords.size(), 9u);

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
                                           fo4::FormKey::parse("00000A30:Fallout4.esm")),
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
  EXPECT_EQ(link_raw(full, "ACHR\\LockKey"), 0x00000A30u);
  EXPECT_EQ(link_raw(full, "ACHR\\EncounterZone"), 0x00000005u);
}
