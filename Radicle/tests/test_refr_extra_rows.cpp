#include "fo4/plugin/mutation.hpp"
#include "fo4/plugin/reader.hpp"
#include "fo4/plugin/writer.hpp"
#include "fo4/schema/formlink_lists.hpp"
#include "fo4/schema/scalar.hpp"
#include "plugin_bytes.hpp"

#include <gtest/gtest.h>

#include <bit>
#include <cstddef>
#include <cstdint>
#include <cstring>
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
  bool repeated = false;
};

struct LinkRow {
  std::string_view record;
  std::string_view path;
  std::string_view subrecord;
  std::size_t offset;
  std::uint32_t payload_size;
  std::string_view target;
  bool nullable = false;
  bool repeated = false;
  std::size_t stride = 0;
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
    {"REFR", "REFR\\MapFlags", "FNAM", 0, u8, false, 0, 0},
    {"REFR", "REFR\\MapMarkerType", "TNAM", 0, u8, true, 2, 0},
    {"REFR", "REFR\\LightFov", "XLIG", 0, f32, true, 0, 0},
    {"REFR", "REFR\\LightEndDistanceCap", "XLIG", 8, f32, true, 0, 0},
    {"REFR", "REFR\\LightNearClip", "XLIG", 16, f32, true, 0, 20},
    {"REFR", "REFR\\LightVolumetricIntensity", "XLIG", 20, f32, true, 0, 24},
    {"PGRE", "PGRE\\AmmoCount", "XAMC", 0, u32, false, 0, 0},
    {"PGRE", "PGRE\\HeadTrackingWeight", "XHTW", 0, f32, false, 0, 0},
    {"REFR", "REFR\\ActivateParentOnly", "XAPD", 0, u8, false, 0, 0},
    {"REFR", "REFR\\ActivateParentDelay", "XAPR", 4, f32, true, 8, 0, true},
    {"REFR", "REFR\\WaterCurrentLinearVelocityX", "XCVL", 0, f32, true, 12, 0},
    {"REFR", "REFR\\WaterCurrentLinearVelocityY", "XCVL", 4, f32, true, 12, 0},
    {"REFR", "REFR\\WaterCurrentLinearVelocityZ", "XCVL", 8, f32, true, 12, 0},
    {"REFR", "REFR\\WaterCurrentRotationalVelocityX", "XCVR", 0, f32, true, 12, 0},
    {"REFR", "REFR\\WaterCurrentRotationalVelocityY", "XCVR", 4, f32, true, 12, 0},
    {"REFR", "REFR\\WaterCurrentRotationalVelocityZ", "XCVR", 8, f32, true, 12, 0},
    {"REFR", "REFR\\PowerGridCount", "XWPG", 0, u32, false, 0, 0, true},
    {"REFR", "REFR\\SplineSlack", "XBSD", 0, f32, true, 0, 20},
    {"REFR", "REFR\\SplineThickness", "XBSD", 4, f32, true, 0, 20},
    {"REFR", "REFR\\SplineHalfExtentX", "XBSD", 8, f32, true, 0, 20},
    {"REFR", "REFR\\SplineHalfExtentY", "XBSD", 12, f32, true, 0, 20},
    {"REFR", "REFR\\SplineHalfExtentZ", "XBSD", 16, f32, true, 0, 20},
    {"REFR", "REFR\\SplineWindDetachedEnd", "XBSD", 20, u8, true, 0, 21},
    {"REFR", "REFR\\DecalWidthScale", "XPDD", 0, f32, true, 8, 0},
    {"REFR", "REFR\\DecalHeightScale", "XPDD", 4, f32, true, 8, 0},
    {"REFR", "REFR\\TeleportPositionX", "XTEL", 4, f32, true, 36, 0},
    {"REFR", "REFR\\TeleportPositionY", "XTEL", 8, f32, true, 36, 0},
    {"REFR", "REFR\\TeleportPositionZ", "XTEL", 12, f32, true, 36, 0},
    {"REFR", "REFR\\TeleportRotationX", "XTEL", 16, f32, true, 36, 0},
    {"REFR", "REFR\\TeleportRotationY", "XTEL", 20, f32, true, 36, 0},
    {"REFR", "REFR\\TeleportRotationZ", "XTEL", 24, f32, true, 36, 0},
    {"REFR", "REFR\\TeleportFlags", "XTEL", 28, u32, true, 36, 0},
    {"REFR", "REFR\\OcclusionPlaneWidth", "XOCP", 0, f32, true, 36, 0},
    {"REFR", "REFR\\OcclusionPlaneHeight", "XOCP", 4, f32, true, 36, 0},
    {"REFR", "REFR\\OcclusionPlanePositionX", "XOCP", 8, f32, true, 36, 0},
    {"REFR", "REFR\\OcclusionPlanePositionY", "XOCP", 12, f32, true, 36, 0},
    {"REFR", "REFR\\OcclusionPlanePositionZ", "XOCP", 16, f32, true, 36, 0},
    {"REFR", "REFR\\OcclusionPlaneRotationQ1", "XOCP", 20, f32, true, 36, 0},
    {"REFR", "REFR\\OcclusionPlaneRotationQ2", "XOCP", 24, f32, true, 36, 0},
    {"REFR", "REFR\\OcclusionPlaneRotationQ3", "XOCP", 28, f32, true, 36, 0},
    {"REFR", "REFR\\OcclusionPlaneRotationQ4", "XOCP", 32, f32, true, 36, 0},
    {"REFR", "REFR\\RadioFrequency", "XRDO", 0, f32, true, 16, 0},
    {"REFR", "REFR\\RadioMinWeakDistance", "XRDO", 4, f32, true, 16, 0},
    {"REFR", "REFR\\RadioMaxWeakDistance", "XRDO", 8, f32, true, 16, 0},
    {"REFR", "REFR\\RadioFlags", "XRDO", 12, u32, true, 16, 0},
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
    {"REFR", "REFR\\LockKey", "XLOC", 4, 16, "KEYM", true},
    {"REFR", "REFR\\PersistLocation", "XLCN", 0, 4, "LCTN"},
    {"REFR", "REFR\\LocationReference", "XLRL", 0, 4, "LCRT", true},
    {"PGRE", "PGRE\\EncounterZone", "XEZN", 0, 4, "ECZN"},
    {"PGRE", "PGRE\\Emittance", "XEMI", 0, 4, "REGN"},
    {"REFR", "REFR\\LinkedKeyword", "XLKR", 0, 4, "KYWD", true, true},
    {"REFR", "REFR\\LinkedReference", "XLKR", 4, 8, "REFR", true, true},
    {"REFR", "REFR\\ActivateParentRef", "XAPR", 0, 8, "ACHR", true, true},
    {"REFR", "REFR\\SplineConnection", "XPLK", 0, 8, "REFR", false, true},
    {"REFR", "REFR\\PortalOrigin", "XPOD", 0, 0, "REFR", true, false, 8},
    {"REFR", "REFR\\PortalDestination", "XPOD", 4, 0, "REFR", true, false, 8},
    {"REFR", "REFR\\PowerGridNode1", "XWPN", 0, 12, "ACHR", true, true},
    {"REFR", "REFR\\PowerGridNode2", "XWPN", 4, 12, "REFR", true, true},
    {"REFR", "REFR\\PowerGridLine", "XWPN", 8, 12, "REFR", true, true},
    {"REFR", "REFR\\TeleportDoor", "XTEL", 0, 36, "REFR"},
    {"REFR", "REFR\\TeleportTransitionInterior", "XTEL", 32, 36, "CELL", true},
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

Bytes ragdoll_bytes(std::initializer_list<std::uint8_t> bones) {
  ByteWriter out;
  for (const auto bone : bones) {
    out.u8(bone);
    out.u8(0);
    out.u8(0);
    out.u8(0);
    for (const auto value : {1.0f, 2.0f, 3.0f, 4.0f, 5.0f, 6.0f}) out.f32(value);
  }
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

Bytes u32s(std::initializer_list<std::uint32_t> values) {
  ByteWriter out;
  for (const auto value : values) out.u32(value);
  return out.bytes;
}

Bytes activate_parent_bytes() {
  ByteWriter out;
  out.u32(0x00000A93u);
  out.f32(1.25f);
  return out.bytes;
}

Bytes spline_bytes() {
  ByteWriter out;
  for (const float value : {0.5f, 0.25f, 1.0f, 2.0f, 3.0f}) out.f32(value);
  out.u8(0x01);
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
                {"FNAM", Bytes{std::byte{0x0B}}},
                {"TNAM", Bytes{std::byte{13}, std::byte{0}}},
                {"XLCN", u32_bytes(0x00000A70)},
                {"XLRL", u32_bytes(0x00000A80)},
                {"XPRD", floats({2.5f})},
                {"INAM", u32_bytes(0x00000A90)},
                {"XPRD", floats({9.0f})},
                {"INAM", u32_bytes(0x00000A91)},
                {"XLCM", i32_bytes(2)},
                {"XLTW", u32_bytes(0x00000AA0)},
                {"XRGD", ragdoll_bytes({3, 7})},
                {"XLKR", u32_bytes(0x00000A90u)},
                {"XLKR", u32s({0x00000A91u, 0x00000A92u})},
                {"XAPD", Bytes{std::byte{0x01}}},
                {"XAPR", activate_parent_bytes()},
                {"XPLK", u32s({0x00000A94u, 0})},
                {"XLRT", u32_bytes(0x00000A95u)},
                {"XCVL", floats({1.0f, 2.0f, 3.0f})},
                {"XCVR", floats({4.0f, 5.0f, 6.0f})},
                {"XWPG", u32_bytes(1)},
                {"XWPN", u32s({0x00000A96u, 0x00000A97u, 0x00000A98u})},
                {"XWPG", u32_bytes(2)},
                {"XWPN", u32s({0x00000AC1u, 0x00000AC2u, 0x00000AC3u})},
                {"XWPN", u32s({0x00000AC4u, 0x00000AC5u, 0x00000AC6u})},
                {"XPOD", u32s({0x00000A99u, 0x00000A9Au})},
                {"XBSD", spline_bytes()},
                {"XPDD", floats({2.0f, 3.0f})},
                {"XLIG", floats({90.0f, 1.0f, 2048.0f, 0.5f, 10.0f, 0.75f})}});
  write_record(out, "REFR", 0x0C11,
               {{"XLIG", floats({90.0f, 1.0f, 2048.0f, 0.5f})}});
  write_record(out, "REFR", 0x0C12,
               {{"XPOD", u32s({0x00000A99u, 0x00000A9Au, 0x00000AA1u, 0x00000AA2u})},
                {"XPLK", u32_bytes(0x00000AA4u)}});
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

Bytes sub_bytes_at(const fo4::Plugin& plugin, std::string_view key, std::string_view subrecord,
                   std::size_t wanted) {
  const auto* record = fo4::find_record(plugin, fo4::FormKey::parse(key));
  if (record == nullptr) {
    ADD_FAILURE() << key;
    return {};
  }
  std::size_t seen = 0;
  for (const auto& sub : record->subrecords) {
    if (sub.signature.to_string() != subrecord) continue;
    if (seen++ == wanted) return sub.data;
  }
  ADD_FAILURE() << subrecord << " #" << wanted;
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
    EXPECT_EQ(field->scalar.address.repeated, row.repeated);
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
    EXPECT_EQ(field->nullable, row.nullable);
    EXPECT_EQ(field->payload_sizes[0], row.payload_size);
    EXPECT_EQ(field->address.repeated, row.repeated);
    EXPECT_EQ(field->address.stride, row.stride);
    EXPECT_TRUE(fo4::schema::formlink_field_target_allowed(
        *field, fo4::Signature::from_string(row.target)));
  }
  const auto key = fo4::schema::formlink_field_descriptor(fo4::Signature::from_string("REFR"),
                                                          "REFR\\LockKey");
  ASSERT_TRUE(key.has_value());
  EXPECT_TRUE(fo4::schema::formlink_field_target_allowed(
      *key, fo4::Signature::from_string("KEYM")));
  EXPECT_FALSE(fo4::schema::formlink_field_target_allowed(
      *key, fo4::Signature::from_string("STAT")));
  const auto portal = fo4::schema::formlink_field_descriptor(fo4::Signature::from_string("REFR"),
                                                             "REFR\\PortalOrigin");
  ASSERT_TRUE(portal.has_value());
  EXPECT_FALSE(fo4::schema::formlink_field_target_allowed(
      *portal, fo4::Signature::from_string("ACHR")));
}

TEST(REFRExtraRows, ReadsScalarsAndLinksFromThePlugin) {
  auto plugin = fo4::read_plugin(make_plugin(), "Patch.esp");
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
  EXPECT_EQ(scalar_bytes(plugin, "REFR", key, "REFR\\MapFlags"), Bytes{std::byte{0x0B}});
  EXPECT_EQ(scalar_bytes(plugin, "REFR", key, "REFR\\MapMarkerType"), Bytes{std::byte{13}});
  EXPECT_EQ(link_raw(plugin, "REFR", key, "REFR\\LockKey"), 0x00000A30u);
  EXPECT_EQ(link_raw(plugin, "REFR", key, "REFR\\EncounterZone"), 0x00000A10u);
  EXPECT_EQ(link_raw(plugin, "REFR", key, "REFR\\PersistLocation"), 0x00000A70u);
  EXPECT_EQ(link_raw(plugin, "REFR", key, "REFR\\LocationReference"), 0x00000A80u);

  const auto short_key = "000C11:Patch.esp";
  EXPECT_EQ(scalar_bytes(plugin, "REFR", short_key, "REFR\\LightFov"), floats({90.0f}));
  EXPECT_TRUE(scalar_bytes(plugin, "REFR", short_key, "REFR\\LightNearClip").empty());

  EXPECT_EQ(scalar_bytes(plugin, "PGRE", "000C20:Patch.esp", "PGRE\\AmmoCount"), u32_bytes(30));
  EXPECT_EQ(scalar_bytes(plugin, "PGRE", "000C20:Patch.esp", "PGRE\\HeadTrackingWeight"),
            floats({3.5f}));
  EXPECT_EQ(link_raw(plugin, "PGRE", "000C20:Patch.esp", "PGRE\\EncounterZone"), 0x00000A10u);
}

TEST(REFRExtraRows, RepeatedPatrolRowsAddressEachOccurrence) {
  auto plugin = fo4::read_plugin(make_plugin(), "Patch.esp");
  const auto key = fo4::FormKey::parse("000C10:Patch.esp");
  const auto refr = fo4::Signature::from_string("REFR");
  const auto time = fo4::schema::scalar_field_descriptor(refr, "REFR\\PatrolIdleTime");
  const auto idle = fo4::schema::formlink_field_descriptor(refr, "REFR\\PatrolIdle");
  ASSERT_TRUE(time.has_value());
  ASSERT_TRUE(idle.has_value());
  EXPECT_EQ(time->scalar.subrecord, fo4::Signature::from_string("XPRD"));
  EXPECT_TRUE(time->scalar.address.repeated);
  EXPECT_TRUE(idle->address.repeated);
  EXPECT_TRUE(idle->nullable);
  EXPECT_TRUE(fo4::schema::formlink_field_target_allowed(
      *idle, fo4::Signature::from_string("IDLE")));
  EXPECT_FALSE(fo4::schema::formlink_field_target_allowed(
      *idle, fo4::Signature::from_string("DIAL")));
  ASSERT_EQ(fo4::record_field_count(plugin, key, *time), 2u);
  ASSERT_EQ(fo4::record_field_count(plugin, key, *idle), 2u);

  EXPECT_EQ(fo4::set_record_scalar(plugin, key, *time, 1, "9.5"),
            fo4::ScalarMutationStatus::updated);
  EXPECT_EQ(fo4::set_record_scalar(plugin, key, *time, 2, "9.5"),
            fo4::ScalarMutationStatus::subrecord_missing);
  EXPECT_EQ(fo4::set_record_formlink_field(plugin, key, *idle, 1,
                                           fo4::FormKey::parse("000A50:Objects.esp")),
            fo4::FormLinkFieldMutationStatus::updated);

  const auto reread = fo4::read_plugin(fo4::write_plugin(plugin), "Patch.esp");
  const auto patrol_key = "000C10:Patch.esp";
  EXPECT_EQ(sub_bytes_at(reread, patrol_key, "XPRD", 0), floats({2.5f}));
  EXPECT_EQ(sub_bytes_at(reread, patrol_key, "XPRD", 1), floats({9.5f}));
  EXPECT_EQ(sub_bytes_at(reread, patrol_key, "INAM", 0), u32_bytes(0x00000A90));
  EXPECT_EQ(sub_bytes_at(reread, patrol_key, "INAM", 1), u32_bytes(0x01000A50));
  EXPECT_EQ(fo4::set_record_formlink_field(plugin, key, *idle, 1, fo4::FormKey{}),
            fo4::FormLinkFieldMutationStatus::updated);
  const auto cleared = fo4::read_plugin(fo4::write_plugin(plugin), "Patch.esp");
  EXPECT_EQ(sub_bytes_at(cleared, patrol_key, "INAM", 1), u32_bytes(0));
}

TEST(REFRExtraRows, LevelModifierAndLitWaterRowsReadAndWrite) {
  auto plugin = fo4::read_plugin(make_plugin(), "Patch.esp");
  const auto key = fo4::FormKey::parse("000C10:Patch.esp");
  const auto refr = fo4::Signature::from_string("REFR");
  const auto modifier = fo4::schema::scalar_field_descriptor(refr, "REFR\\LevelModifier");
  const auto lit_water = fo4::schema::formlink_field_descriptor(refr, "REFR\\LitWater");
  ASSERT_TRUE(modifier.has_value());
  ASSERT_TRUE(lit_water.has_value());
  EXPECT_EQ(modifier->scalar.subrecord, fo4::Signature::from_string("XLCM"));
  EXPECT_EQ(modifier->scalar.storage, fo4::schema::ScalarStorage::i32);
  EXPECT_EQ(fo4::schema::scalar_value_name(modifier->scalar, 1), "Medium");
  EXPECT_FALSE(fo4::schema::scalar_value_name(modifier->scalar, 5).has_value());
  EXPECT_TRUE(lit_water->address.repeated);
  EXPECT_EQ(link_raw(plugin, "REFR", "000C10:Patch.esp", "REFR\\LitWater"), 0x00000AA0u);
  EXPECT_EQ(scalar_bytes(plugin, "REFR", "000C10:Patch.esp", "REFR\\LevelModifier"),
            i32_bytes(2));

  EXPECT_EQ(fo4::set_record_scalar(plugin, key, *modifier, 0, "3"),
            fo4::ScalarMutationStatus::updated);
  EXPECT_EQ(fo4::set_record_formlink_field(plugin, key, *lit_water, 0,
                                           fo4::FormKey::parse("000A60:Objects.esp")),
            fo4::FormLinkFieldMutationStatus::updated);
  EXPECT_THROW(static_cast<void>(fo4::set_record_formlink_field(plugin, key, *lit_water, 0,
                                                               fo4::FormKey{})),
               fo4::schema::FormLinkFieldError);

  const auto reread = fo4::read_plugin(fo4::write_plugin(plugin), "Patch.esp");
  EXPECT_EQ(sub_bytes(reread, "000C10:Patch.esp", "XLCM"), i32_bytes(3));
  EXPECT_EQ(sub_bytes(reread, "000C10:Patch.esp", "XLTW"), u32_bytes(0x01000A60));
}

TEST(REFRExtraRows, StridedRagdollRowsAddressEachBone) {
  auto plugin = fo4::read_plugin(make_plugin(), "Patch.esp");
  const auto key = fo4::FormKey::parse("000C10:Patch.esp");
  const auto refr = fo4::Signature::from_string("REFR");
  const auto bone = fo4::schema::scalar_field_descriptor(refr, "REFR\\RagdollBoneId");
  const auto rotation_z = fo4::schema::scalar_field_descriptor(refr, "REFR\\RagdollRotationZ");
  ASSERT_TRUE(bone.has_value());
  ASSERT_TRUE(rotation_z.has_value());
  EXPECT_EQ(bone->scalar.subrecord, fo4::Signature::from_string("XRGD"));
  EXPECT_TRUE(bone->scalar.embedded);
  EXPECT_EQ(bone->scalar.address.stride, 28u);
  EXPECT_EQ(rotation_z->scalar.offset, 24u);
  ASSERT_EQ(fo4::record_field_count(plugin, key, *bone), 2u);

  EXPECT_EQ(fo4::set_record_scalar(plugin, key, *bone, 1, "9"),
            fo4::ScalarMutationStatus::updated);
  EXPECT_EQ(fo4::set_record_scalar(plugin, key, *rotation_z, 1, "-2.5"),
            fo4::ScalarMutationStatus::updated);
  EXPECT_THROW(static_cast<void>(fo4::set_record_scalar(plugin, key, *bone, 0, "256")),
               fo4::schema::ScalarError);
  EXPECT_EQ(fo4::set_record_scalar(plugin, key, *bone, 2, "9"),
            fo4::ScalarMutationStatus::subrecord_missing);

  const auto reread = fo4::read_plugin(fo4::write_plugin(plugin), "Patch.esp");
  const auto payload = sub_bytes(reread, "000C10:Patch.esp", "XRGD");
  ASSERT_EQ(payload.size(), 56u);
  EXPECT_EQ(std::to_integer<std::uint8_t>(payload[0]), 3u);
  EXPECT_EQ(std::to_integer<std::uint8_t>(payload[28]), 9u);
  float position = 0.0F;
  std::memcpy(&position, payload.data() + 4, sizeof(position));
  EXPECT_EQ(position, 1.0f);
  float rotation = 0.0F;
  std::memcpy(&rotation, payload.data() + 28 + 24, sizeof(rotation));
  EXPECT_EQ(rotation, -2.5f);
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
  EXPECT_EQ(fo4::set_record_scalar(plugin, key, *alpha, "64"),
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
            (Bytes{std::byte{0x40}, std::byte{0x20}}));
  EXPECT_EQ(sub_bytes(reread, "000C10:Patch.esp", "XLOC"), lock_bytes(75, 0x01000A20, 0x04));
  EXPECT_EQ(sub_bytes(reread, "000C10:Patch.esp", "XEZN"), u32_bytes(0x01000A40));
  EXPECT_EQ(sub_bytes(reread, "000C10:Patch.esp", "XCNT"), i32_bytes(-3));
}

TEST(REFRExtraRows, LinkedActivateParentPortalPowerGridSplineAndWaterRowsReadFromThePlugin) {
  auto plugin = fo4::read_plugin(make_plugin(), "Patch.esp");
  const auto key = "000C10:Patch.esp";

  EXPECT_EQ(link_raw(plugin, "REFR", key, "REFR\\LinkedKeyword"), 0x00000A90u);
  const auto pair = sub_bytes_at(plugin, key, "XLKR", 1);
  ASSERT_EQ(pair.size(), 8u);
  EXPECT_EQ(Bytes(pair.begin() + 4, pair.end()), u32_bytes(0x00000A92u));
  EXPECT_EQ(link_raw(plugin, "REFR", key, "REFR\\ActivateParentRef"), 0x00000A93u);
  EXPECT_EQ(link_raw(plugin, "REFR", key, "REFR\\SplineConnection"), 0x00000A94u);
  EXPECT_EQ(link_raw(plugin, "REFR", key, "REFR\\PortalOrigin"), 0x00000A99u);
  EXPECT_EQ(link_raw(plugin, "REFR", key, "REFR\\PortalDestination"), 0x00000A9Au);
  EXPECT_EQ(link_raw(plugin, "REFR", key, "REFR\\PowerGridNode1"), 0x00000A96u);
  EXPECT_EQ(link_raw(plugin, "REFR", key, "REFR\\PowerGridNode2"), 0x00000A97u);
  EXPECT_EQ(link_raw(plugin, "REFR", key, "REFR\\PowerGridLine"), 0x00000A98u);

  EXPECT_EQ(scalar_bytes(plugin, "REFR", key, "REFR\\ActivateParentOnly"), Bytes{std::byte{0x01}});
  EXPECT_EQ(scalar_bytes(plugin, "REFR", key, "REFR\\ActivateParentDelay"), floats({1.25f}));
  EXPECT_EQ(scalar_bytes(plugin, "REFR", key, "REFR\\PowerGridCount"), u32_bytes(1));
  EXPECT_EQ(scalar_bytes(plugin, "REFR", key, "REFR\\WaterCurrentLinearVelocityY"), floats({2.0f}));
  EXPECT_EQ(scalar_bytes(plugin, "REFR", key, "REFR\\WaterCurrentRotationalVelocityZ"),
            floats({6.0f}));
  EXPECT_EQ(scalar_bytes(plugin, "REFR", key, "REFR\\SplineSlack"), floats({0.5f}));
  EXPECT_EQ(scalar_bytes(plugin, "REFR", key, "REFR\\SplineHalfExtentY"), floats({2.0f}));
  EXPECT_EQ(scalar_bytes(plugin, "REFR", key, "REFR\\SplineWindDetachedEnd"),
            Bytes{std::byte{0x01}});
  EXPECT_EQ(scalar_bytes(plugin, "REFR", key, "REFR\\DecalHeightScale"), floats({3.0f}));

  const auto ref_types = fo4::schema::formlink_list_descriptor(fo4::Signature::from_string("REFR"),
                                                               "REFR\\LocationRefTypes");
  ASSERT_TRUE(ref_types.has_value());
  EXPECT_EQ(ref_types->subrecord, fo4::Signature::from_string("XLRT"));
  EXPECT_EQ(ref_types->storage, fo4::schema::FormLinkListStorage::packed_array);
  EXPECT_TRUE(ref_types->nullable);
  EXPECT_FALSE(ref_types->can_insert);
  const auto values = fo4::schema::record_formlink_list_values(
      plugin, fo4::FormKey::parse(key), *ref_types);
  ASSERT_TRUE(values.has_value());
  ASSERT_EQ(values->size(), 1u);
  EXPECT_EQ((*values)[0], fo4::FormKey::parse("00000A95:Fallout4.esm"));
}

TEST(REFRExtraRows, WritesTheNewRowsInPlaceAndRefusesALinkedReferenceBesideAShortXlkr) {
  auto plugin = fo4::read_plugin(make_plugin(), "Patch.esp");
  const auto key = fo4::FormKey::parse("000C10:Patch.esp");
  const auto refr = fo4::Signature::from_string("REFR");
  const auto activate_only = fo4::schema::scalar_field_descriptor(refr, "REFR\\ActivateParentOnly");
  const auto delay = fo4::schema::scalar_field_descriptor(refr, "REFR\\ActivateParentDelay");
  const auto slack = fo4::schema::scalar_field_descriptor(refr, "REFR\\SplineSlack");
  const auto decal = fo4::schema::scalar_field_descriptor(refr, "REFR\\DecalHeightScale");
  const auto count = fo4::schema::scalar_field_descriptor(refr, "REFR\\PowerGridCount");
  const auto keyword = fo4::schema::formlink_field_descriptor(refr, "REFR\\LinkedKeyword");
  const auto reference = fo4::schema::formlink_field_descriptor(refr, "REFR\\LinkedReference");
  ASSERT_TRUE(activate_only.has_value());
  ASSERT_TRUE(delay.has_value());
  ASSERT_TRUE(slack.has_value());
  ASSERT_TRUE(decal.has_value());
  ASSERT_TRUE(count.has_value());
  ASSERT_TRUE(keyword.has_value());
  ASSERT_TRUE(reference.has_value());
  EXPECT_EQ(keyword->payload_sizes[0], 4u);
  EXPECT_EQ(keyword->payload_sizes[1], 8u);

  EXPECT_THROW(fo4::set_record_formlink_field(plugin, key, *reference,
                                              fo4::FormKey::parse("00000A9B:Fallout4.esm")),
               fo4::schema::FormLinkFieldError);
  EXPECT_EQ(fo4::set_record_formlink_field(plugin, key, *keyword,
                                           fo4::FormKey::parse("00000A9B:Fallout4.esm")),
            fo4::FormLinkFieldMutationStatus::updated);
  EXPECT_EQ(fo4::set_record_scalar(plugin, key, *activate_only, "0"),
            fo4::ScalarMutationStatus::updated);
  EXPECT_EQ(fo4::set_record_scalar(plugin, key, *delay, "2.5"),
            fo4::ScalarMutationStatus::updated);
  EXPECT_EQ(fo4::set_record_scalar(plugin, key, *slack, "1.5"),
            fo4::ScalarMutationStatus::updated);
  EXPECT_EQ(fo4::set_record_scalar(plugin, key, *decal, "4.0"),
            fo4::ScalarMutationStatus::updated);
  EXPECT_FALSE(fo4::schema::is_derived_scalar_row(refr, fo4::Signature::from_string("XWPG")));
  ASSERT_EQ(fo4::record_field_count(plugin, key, *count), 2u);
  EXPECT_EQ(fo4::set_record_scalar(plugin, key, *count, 1, "5"),
            fo4::ScalarMutationStatus::updated);
  EXPECT_EQ(fo4::set_record_scalar(plugin, key, *count, 2, "5"),
            fo4::ScalarMutationStatus::subrecord_missing);

  const auto reread = fo4::read_plugin(fo4::write_plugin(plugin), "Patch.esp");
  const auto linked_keyword = sub_bytes_at(reread, "000C10:Patch.esp", "XLKR", 0);
  ASSERT_EQ(linked_keyword.size(), 4u);
  EXPECT_EQ(Bytes(linked_keyword.begin(), linked_keyword.end()), u32_bytes(0x00000A9Bu));
  const auto linked_pair = sub_bytes_at(reread, "000C10:Patch.esp", "XLKR", 1);
  ASSERT_EQ(linked_pair.size(), 8u);
  EXPECT_EQ(Bytes(linked_pair.begin() + 4, linked_pair.end()), u32_bytes(0x00000A92u));

  const auto activate_parent_only = sub_bytes(reread, "000C10:Patch.esp", "XAPD");
  ASSERT_EQ(activate_parent_only.size(), 1u);
  EXPECT_EQ(std::to_integer<std::uint8_t>(activate_parent_only[0]), 0x00);
  const auto activate_parent = sub_bytes(reread, "000C10:Patch.esp", "XAPR");
  ASSERT_EQ(activate_parent.size(), 8u);
  EXPECT_EQ(Bytes(activate_parent.begin(), activate_parent.begin() + 4), u32_bytes(0x00000A93u));
  EXPECT_EQ(Bytes(activate_parent.begin() + 4, activate_parent.end()), floats({2.5f}));

  const auto spline = sub_bytes(reread, "000C10:Patch.esp", "XBSD");
  ASSERT_EQ(spline.size(), 21u);
  EXPECT_EQ(Bytes(spline.begin(), spline.begin() + 4), floats({1.5f}));
  EXPECT_EQ(Bytes(spline.begin() + 4, spline.begin() + 20), floats({0.25f, 1.0f, 2.0f, 3.0f}));
  EXPECT_EQ(std::to_integer<std::uint8_t>(spline[20]), 0x01);
  EXPECT_EQ(sub_bytes(reread, "000C10:Patch.esp", "XPDD"), floats({2.0f, 4.0f}));
  EXPECT_EQ(sub_bytes_at(reread, "000C10:Patch.esp", "XWPG", 0), u32_bytes(1));
  EXPECT_EQ(sub_bytes_at(reread, "000C10:Patch.esp", "XWPG", 1), u32_bytes(5));

  EXPECT_EQ(sub_bytes(reread, "000C10:Patch.esp", "XCVL"), floats({1.0f, 2.0f, 3.0f}));
  EXPECT_EQ(sub_bytes(reread, "000C10:Patch.esp", "XWPN"), u32s({0x00000A96u, 0x00000A97u, 0x00000A98u}));
  EXPECT_EQ(sub_bytes_at(reread, "000C10:Patch.esp", "XWPN", 1), u32s({0x00000AC1u, 0x00000AC2u, 0x00000AC3u}));
  EXPECT_EQ(sub_bytes_at(reread, "000C10:Patch.esp", "XWPN", 2), u32s({0x00000AC4u, 0x00000AC5u, 0x00000AC6u}));
  EXPECT_EQ(sub_bytes(reread, "000C10:Patch.esp", "XPOD"), u32s({0x00000A99u, 0x00000A9Au}));
  EXPECT_EQ(link_raw(reread, "REFR", "000C10:Patch.esp", "REFR\\LinkedKeyword"), 0x00000A9Bu);
}

TEST(REFRExtraRows, PackedPortalPairsAddressEachElement) {
  auto plugin = fo4::read_plugin(make_plugin(), "Patch.esp");
  const auto key = fo4::FormKey::parse("000C12:Patch.esp");
  const auto refr = fo4::Signature::from_string("REFR");
  const auto origin = fo4::schema::formlink_field_descriptor(refr, "REFR\\PortalOrigin");
  const auto destination =
      fo4::schema::formlink_field_descriptor(refr, "REFR\\PortalDestination");
  ASSERT_TRUE(origin.has_value());
  ASSERT_TRUE(destination.has_value());
  EXPECT_FALSE(origin->address.repeated);
  EXPECT_EQ(origin->address.stride, 8u);
  EXPECT_EQ(fo4::record_field_count(plugin, key, *origin), 2u);
  EXPECT_EQ(fo4::record_field_count(plugin, key, *destination), 2u);
  EXPECT_EQ(fo4::set_record_formlink_field(plugin, key, *origin, 1,
                                           fo4::FormKey::parse("000AA3:Objects.esp")),
            fo4::FormLinkFieldMutationStatus::updated);

  const auto reread = fo4::read_plugin(fo4::write_plugin(plugin), "Patch.esp");
  EXPECT_EQ(sub_bytes(reread, "000C12:Patch.esp", "XPOD"),
            u32s({0x00000A99u, 0x00000A9Au, 0x01000AA3u, 0x00000AA2u}));
  EXPECT_EQ(link_raw(reread, "REFR", "000C12:Patch.esp", "REFR\\PortalOrigin"), 0x00000A99u);
  EXPECT_EQ(link_raw(reread, "REFR", "000C12:Patch.esp", "REFR\\PortalDestination"),
            0x00000A9Au);
}

TEST(REFRExtraRows, ShortSplineConnectionPayloadReadsAndWrites) {
  auto plugin = fo4::read_plugin(make_plugin(), "Patch.esp");
  const auto key = fo4::FormKey::parse("000C12:Patch.esp");
  const auto spline = fo4::schema::formlink_field_descriptor(fo4::Signature::from_string("REFR"),
                                                             "REFR\\SplineConnection");
  ASSERT_TRUE(spline.has_value());
  EXPECT_EQ(spline->payload_sizes[0], 8u);
  EXPECT_EQ(spline->payload_sizes[1], 4u);
  EXPECT_EQ(fo4::record_field_count(plugin, key, *spline), 1u);
  EXPECT_EQ(fo4::set_record_formlink_field(plugin, key, *spline, 0,
                                           fo4::FormKey::parse("000AA5:Objects.esp")),
            fo4::FormLinkFieldMutationStatus::updated);

  const auto reread = fo4::read_plugin(fo4::write_plugin(plugin), "Patch.esp");
  EXPECT_EQ(sub_bytes(reread, "000C12:Patch.esp", "XPLK"), u32_bytes(0x01000AA5u));
}
