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

struct ScalarRow {
  std::string_view record;
  std::string_view path;
  std::string_view subrecord;
  std::size_t offset;
  fo4::schema::ScalarStorage storage;
  bool embedded;
  std::size_t payload_size;
  std::size_t minimum_payload_size;
};

struct LinkRow {
  std::string_view record;
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
    {"REFR", "REFR\\Radius", "XRDS", 0, f32, false, 0, 0},
    {"REFR", "REFR\\ItemCount", "XCNT", 0, i32, false, 0, 0},
    {"REFR", "REFR\\AmmoCount", "XAMC", 0, u32, false, 0, 0},
    {"REFR", "REFR\\CollisionLayer", "XTRI", 0, u32, false, 0, 0},
    {"REFR", "REFR\\HeadTrackingWeight", "XHTW", 0, f32, false, 0, 0},
    {"REFR", "REFR\\FavorCost", "XFVC", 0, f32, false, 0, 0},
    {"REFR", "REFR\\AlphaCutoff", "XALP", 0, u8, true, 2, 0},
    {"REFR", "REFR\\AlphaBase", "XALP", 1, u8, true, 2, 0},
    {"REFR", "REFR\\LockLevel", "XLOC", 0, u8, true, 16, 0},
    {"REFR", "REFR\\LockFlags", "XLOC", 8, u8, true, 16, 0},
    {"REFR", "REFR\\LightFov", "XLIG", 0, f32, true, 0, 0},
    {"REFR", "REFR\\LightEndDistanceCap", "XLIG", 8, f32, true, 0, 0},
    {"REFR", "REFR\\LightNearClip", "XLIG", 16, f32, true, 0, 20},
    {"REFR", "REFR\\LightVolumetricIntensity", "XLIG", 20, f32, true, 0, 24},
    {"PGRE", "PGRE\\AmmoCount", "XAMC", 0, u32, false, 0, 0},
    {"PGRE", "PGRE\\HeadTrackingWeight", "XHTW", 0, f32, false, 0, 0},
};

constexpr LinkRow k_link_rows[] = {
    {"REFR", "REFR\\EncounterZone", "XEZN", 0, 4, "ECZN"},
    {"REFR", "REFR\\Layer", "XLYR", 0, 4, "LAYR"},
    {"REFR", "REFR\\MaterialSwap", "XMSP", 0, 4, "MSWP"},
    {"REFR", "REFR\\ReferenceGroup", "XRFG", 0, 4, "RFGP"},
    {"REFR", "REFR\\LeveledItemBase", "XLIB", 0, 4, "LVLI"},
    {"REFR", "REFR\\TeleportLocationName", "XTNM", 0, 4, "MESG"},
    {"REFR", "REFR\\Emittance", "XEMI", 0, 4, "LIGH"},
    {"REFR", "REFR\\AcousticRestriction", "XASP", 0, 4, "REFR"},
    {"REFR", "REFR\\MultiboundReference", "XMBR", 0, 4, "REFR"},
    {"REFR", "REFR\\SpawnContainer", "XSPC", 0, 4, "REFR"},
    {"REFR", "REFR\\LockKey", "XLOC", 4, 16, "KEYM"},
    {"PGRE", "PGRE\\EncounterZone", "XEZN", 0, 4, "ECZN"},
    {"PGRE", "PGRE\\Emittance", "XEMI", 0, 4, "REGN"},
};

constexpr std::uint32_t raw_id(std::uint32_t local_id) { return 0x02000000u | local_id; }

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

Bytes floats(std::initializer_list<float> values) {
  ByteWriter out;
  for (const auto value : values) out.f32(value);
  return out.bytes;
}

Bytes lock_bytes(std::uint8_t level, std::uint32_t key, std::uint8_t flags) {
  ByteWriter out;
  out.u8(level);
  out.u8(0);
  out.u8(0);
  out.u8(0);
  out.u32(key);
  out.u8(flags);
  out.u8(0);
  out.u8(0);
  out.u8(0);
  out.u32(0);
  return out.bytes;
}

void write_header(ByteWriter& out) {
  const auto tes4 = fo4::test::begin_record(out, "TES4", 0, 0);
  ByteWriter hedr;
  hedr.f32(1.0f);
  hedr.u32(2);
  hedr.u32(0x900);
  fo4::test::write_subrecord(out, "HEDR", hedr.bytes);
  fo4::test::write_subrecord(out, "MAST", std::string_view{"Fallout4.esm\0", 13});
  fo4::test::write_subrecord(out, "DATA", Bytes(8));
  fo4::test::write_subrecord(out, "MAST", std::string_view{"Objects.esp\0", 12});
  fo4::test::write_subrecord(out, "DATA", Bytes(8));
  fo4::test::end_record(out, tes4);
}

void write_record(ByteWriter& out, std::string_view signature, std::uint32_t local_id,
                  std::initializer_list<std::pair<std::string_view, Bytes>> subs) {
  const auto group = fo4::test::begin_group(out, signature, 0);
  const auto record = fo4::test::begin_record(out, signature, 0, raw_id(local_id));
  for (const auto& sub : subs) fo4::test::write_subrecord(out, sub.first, sub.second);
  fo4::test::end_record(out, record);
  fo4::test::end_group(out, group);
}

std::vector<std::byte> make_plugin() {
  ByteWriter out;
  write_header(out);
  write_record(out, "REFR", 0x0C10,
               {{"XRDS", floats({4.5f})},
                {"XCNT", i32_bytes(-3)},
                {"XAMC", u32_bytes(7)},
                {"XTRI", u32_bytes(12)},
                {"XHTW", floats({7.5f})},
                {"XFVC", floats({0.25f})},
                {"XALP", Bytes{std::byte{0x80}, std::byte{0x20}}},
                {"XLOC", lock_bytes(50, 0x00000A30, 0x04)},
                {"XEZN", u32_bytes(0x00000A10)},
                {"XEMI", u32_bytes(0x00000B20)},
                {"XLIG", floats({90.0f, 1.0f, 2048.0f, 0.5f, 10.0f, 0.75f})}});
  write_record(out, "REFR", 0x0C11,
               {{"XLIG", floats({90.0f, 1.0f, 2048.0f, 0.5f})}});
  write_record(out, "PGRE", 0x0C20,
               {{"XAMC", u32_bytes(30)},
                {"XHTW", floats({3.5f})},
                {"XEZN", u32_bytes(0x00000A10)}});
  return out.bytes;
}

Bytes sub_bytes(const fo4::Plugin& plugin, std::string_view key, std::string_view subrecord) {
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

Bytes scalar_bytes(const fo4::Plugin& plugin, std::string_view record, std::string_view key,
                   std::string_view path) {
  const auto field =
      fo4::schema::scalar_field_descriptor(fo4::Signature::from_string(record), path);
  if (!field.has_value()) {
    ADD_FAILURE() << path;
    return {};
  }
  const auto data = sub_bytes(plugin, key, field->scalar.subrecord.to_string());
  const auto bytes = fo4::schema::scalar_field_bytes(data, field->scalar);
  return Bytes(bytes.begin(), bytes.end());
}

std::uint32_t link_raw(const fo4::Plugin& plugin, std::string_view record, std::string_view key,
                       std::string_view path) {
  const auto field =
      fo4::schema::formlink_field_descriptor(fo4::Signature::from_string(record), path);
  if (!field.has_value()) {
    ADD_FAILURE() << path;
    return 0;
  }
  const auto data = sub_bytes(plugin, key, field->subrecord.to_string());
  std::uint32_t value = 0;
  for (std::size_t i = 0; i < 4 && field->offset + i < data.size(); ++i) {
    value |= static_cast<std::uint32_t>(std::to_integer<std::uint8_t>(data[field->offset + i]))
             << (8 * i);
  }
  return value;
}

}  // namespace

TEST(REFRExtraRows, ScalarRowsHaveXEditOffsetsStorageAndGates) {
  for (const auto& row : k_scalar_rows) {
    SCOPED_TRACE(row.path);
    const auto field = fo4::schema::scalar_field_descriptor(fo4::Signature::from_string(row.record),
                                                            row.path);
    ASSERT_TRUE(field.has_value());
    EXPECT_EQ(field->scalar.subrecord, fo4::Signature::from_string(row.subrecord));
    EXPECT_EQ(field->scalar.embedded, row.embedded);
    EXPECT_EQ(field->scalar.offset, row.offset);
    EXPECT_EQ(field->scalar.storage, row.storage);
    EXPECT_EQ(field->scalar.payload_size, row.payload_size);
    EXPECT_EQ(field->scalar.minimum_payload_size, row.minimum_payload_size);
  }
}

TEST(REFRExtraRows, LinkRowsNameTheirSubrecordsOffsetsAndTargets) {
  for (const auto& row : k_link_rows) {
    SCOPED_TRACE(row.path);
    const auto field = fo4::schema::formlink_field_descriptor(
        fo4::Signature::from_string(row.record), row.path);
    ASSERT_TRUE(field.has_value());
    EXPECT_EQ(field->subrecord, fo4::Signature::from_string(row.subrecord));
    EXPECT_EQ(field->offset, row.offset);
    EXPECT_TRUE(field->nullable);
    EXPECT_EQ(field->payload_sizes[0], row.payload_size);
    EXPECT_TRUE(fo4::schema::formlink_field_target_allowed(
        *field, fo4::Signature::from_string(row.target)));
  }
  const auto key = fo4::schema::formlink_field_descriptor(fo4::Signature::from_string("REFR"),
                                                          "REFR\\LockKey");
  ASSERT_TRUE(key.has_value());
  EXPECT_FALSE(fo4::schema::formlink_field_target_allowed(*key, fo4::Signature::from_string("STAT")));
}

TEST(REFRExtraRows, ReadsScalarsAndLinksFromThePlugin) {
  const auto plugin = fo4::read_plugin(make_plugin(), "Patch.esp");
  const auto key = "000C10:Patch.esp";
  EXPECT_EQ(scalar_bytes(plugin, "REFR", key, "REFR\\Radius"), floats({4.5f}));
  EXPECT_EQ(scalar_bytes(plugin, "REFR", key, "REFR\\ItemCount"), i32_bytes(-3));
  EXPECT_EQ(scalar_bytes(plugin, "REFR", key, "REFR\\AmmoCount"), u32_bytes(7));
  EXPECT_EQ(scalar_bytes(plugin, "REFR", key, "REFR\\CollisionLayer"), u32_bytes(12));
  EXPECT_EQ(scalar_bytes(plugin, "REFR", key, "REFR\\AlphaBase"), Bytes{std::byte{0x20}});
  EXPECT_EQ(scalar_bytes(plugin, "REFR", key, "REFR\\LockLevel"), Bytes{std::byte{50}});
  EXPECT_EQ(scalar_bytes(plugin, "REFR", key, "REFR\\LockFlags"), Bytes{std::byte{0x04}});
  EXPECT_EQ(scalar_bytes(plugin, "REFR", key, "REFR\\LightEndDistanceCap"), floats({2048.0f}));
  EXPECT_EQ(scalar_bytes(plugin, "REFR", key, "REFR\\LightNearClip"), floats({10.0f}));
  EXPECT_EQ(scalar_bytes(plugin, "REFR", key, "REFR\\LightVolumetricIntensity"), floats({0.75f}));
  EXPECT_EQ(link_raw(plugin, "REFR", key, "REFR\\LockKey"), 0x00000A30u);
  EXPECT_EQ(link_raw(plugin, "REFR", key, "REFR\\EncounterZone"), 0x00000A10u);

  const auto short_key = "000C11:Patch.esp";
  EXPECT_EQ(scalar_bytes(plugin, "REFR", short_key, "REFR\\LightFov"), floats({90.0f}));
  EXPECT_TRUE(scalar_bytes(plugin, "REFR", short_key, "REFR\\LightNearClip").empty());

  EXPECT_EQ(scalar_bytes(plugin, "PGRE", "000C20:Patch.esp", "PGRE\\AmmoCount"), u32_bytes(30));
  EXPECT_EQ(scalar_bytes(plugin, "PGRE", "000C20:Patch.esp", "PGRE\\HeadTrackingWeight"),
            floats({3.5f}));
  EXPECT_EQ(link_raw(plugin, "PGRE", "000C20:Patch.esp", "PGRE\\EncounterZone"), 0x00000A10u);
}

TEST(REFRExtraRows, WritesScalarsAndLinksThenRereads) {
  auto plugin = fo4::read_plugin(make_plugin(), "Patch.esp");
  const auto key = fo4::FormKey::parse("000C10:Patch.esp");
  const auto lock_key = fo4::schema::formlink_field_descriptor(fo4::Signature::from_string("REFR"),
                                                               "REFR\\LockKey");
  const auto zone = fo4::schema::formlink_field_descriptor(fo4::Signature::from_string("REFR"),
                                                           "REFR\\EncounterZone");
  const auto radius = fo4::schema::scalar_field_descriptor(fo4::Signature::from_string("REFR"),
                                                           "REFR\\Radius");
  const auto lock_level = fo4::schema::scalar_field_descriptor(fo4::Signature::from_string("REFR"),
                                                               "REFR\\LockLevel");
  const auto alpha = fo4::schema::scalar_field_descriptor(fo4::Signature::from_string("REFR"),
                                                          "REFR\\AlphaCutoff");
  ASSERT_TRUE(lock_key.has_value());
  ASSERT_TRUE(zone.has_value());
  ASSERT_TRUE(radius.has_value());
  ASSERT_TRUE(lock_level.has_value());
  ASSERT_TRUE(alpha.has_value());
  EXPECT_EQ(fo4::set_record_scalar(plugin, key, *radius, "2048"),
            fo4::ScalarMutationStatus::updated);
  EXPECT_EQ(fo4::set_record_scalar(plugin, key, *lock_level, "75"),
            fo4::ScalarMutationStatus::updated);
  EXPECT_EQ(fo4::set_record_scalar(plugin, key, *alpha, "128"),
            fo4::ScalarMutationStatus::updated);
  EXPECT_EQ(fo4::set_record_formlink_field(plugin, key, *lock_key,
                                           fo4::FormKey::parse("000A20:Objects.esp")),
            fo4::FormLinkFieldMutationStatus::updated);
  EXPECT_EQ(fo4::set_record_formlink_field(plugin, key, *zone,
                                           fo4::FormKey::parse("000A40:Objects.esp")),
            fo4::FormLinkFieldMutationStatus::updated);

  const auto reread = fo4::read_plugin(fo4::write_plugin(plugin), "Patch.esp");
  EXPECT_EQ(sub_bytes(reread, "000C10:Patch.esp", "XRDS"), floats({2048.0f}));
  EXPECT_EQ(sub_bytes(reread, "000C10:Patch.esp", "XALP"),
            (Bytes{std::byte{0x80}, std::byte{0x20}}));
  EXPECT_EQ(sub_bytes(reread, "000C10:Patch.esp", "XLOC"), lock_bytes(75, 0x01000A20, 0x04));
  EXPECT_EQ(sub_bytes(reread, "000C10:Patch.esp", "XEZN"), u32_bytes(0x01000A40));
  EXPECT_EQ(sub_bytes(reread, "000C10:Patch.esp", "XCNT"), i32_bytes(-3));
}
