#include "fo4/environment/environment.hpp"
#include "fo4/plugin/mutation.hpp"
#include "fo4/plugin/reader.hpp"
#include "fo4/plugin/subrecord_authoring.hpp"
#include "fo4/plugin/writer.hpp"
#include "fo4/schema/form_links.hpp"
#include "fo4/schema/formlink_lists.hpp"
#include "fo4/schema/preflight.hpp"
#include "fo4/schema/scalar.hpp"
#include "plugin_bytes.hpp"

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <string>
#include <string_view>
#include <vector>

namespace {

using Bytes = std::vector<std::byte>;
using fo4::test::ByteWriter;

struct Sub {
  std::string_view signature;
  Bytes data;
};

struct ScalarRow {
  std::string_view path;
  std::string_view subrecord;
  std::size_t offset;
  fo4::schema::ScalarStorage storage;
  std::size_t minimum_payload_size;
  std::size_t payload_size{0};
  bool repeated{false};
};

struct StringRow {
  std::string_view path;
  std::string_view subrecord;
};

struct LinkRow {
  std::string_view path;
  std::string_view subrecord;
  std::string_view target;
  bool nullable;
};

constexpr auto f32 = fo4::schema::ScalarStorage::f32;
constexpr auto i16 = fo4::schema::ScalarStorage::i16;
constexpr auto i32 = fo4::schema::ScalarStorage::i32;
constexpr auto i8 = fo4::schema::ScalarStorage::i8;
constexpr auto u8 = fo4::schema::ScalarStorage::u8;
constexpr auto u16 = fo4::schema::ScalarStorage::u16;

constexpr ScalarRow k_scalar_rows[] = {
    {"WRLD\\BoundsMinX", "NAM0", 0, f32, 0},
    {"WRLD\\BoundsMinY", "NAM0", 4, f32, 0},
    {"WRLD\\BoundsMaxX", "NAM9", 0, f32, 0},
    {"WRLD\\BoundsMaxY", "NAM9", 4, f32, 0},
    {"WRLD\\DefaultLandHeight", "DNAM", 0, f32, 0},
    {"WRLD\\DefaultWaterHeight", "DNAM", 4, f32, 0},
    {"WRLD\\LodWaterHeight", "NAM4", 0, f32, 0},
    {"WRLD\\DistantLodMultiplier", "NAMA", 0, f32, 0},
    {"WRLD\\ParentFlags", "PNAM", 0, u16, 0},
    {"WRLD\\Flags", "DATA", 0, u8, 0},
    {"WRLD\\FixedCenterX", "WCTR", 0, i16, 0},
    {"WRLD\\FixedCenterY", "WCTR", 2, i16, 0},
    {"WRLD\\MapUsableX", "MNAM", 0, i32, 0},
    {"WRLD\\MapUsableY", "MNAM", 4, i32, 0},
    {"WRLD\\MapNwCellX", "MNAM", 8, i16, 0},
    {"WRLD\\MapNwCellY", "MNAM", 10, i16, 0},
    {"WRLD\\MapSeCellX", "MNAM", 12, i16, 0},
    {"WRLD\\MapSeCellY", "MNAM", 14, i16, 0},
    {"WRLD\\MapScale", "ONAM", 0, f32, 0},
    {"WRLD\\MapCellOffsetX", "ONAM", 4, f32, 0},
    {"WRLD\\MapCellOffsetY", "ONAM", 8, f32, 0},
    {"WRLD\\MapCellOffsetZ", "ONAM", 12, f32, 0},
    {"WRLD\\MaxHeightMinX", "MHDT", 0, i16, 8},
    {"WRLD\\MaxHeightMinY", "MHDT", 2, i16, 8},
    {"WRLD\\MaxHeightMaxX", "MHDT", 4, i16, 8},
    {"WRLD\\MaxHeightMaxY", "MHDT", 6, i16, 8},
    {"WRLD\\LevelNwCellX", "WLEV", 0, i8, 0, 4, true},
    {"WRLD\\LevelNwCellY", "WLEV", 1, i8, 0, 4, true},
    {"WRLD\\LevelWidth", "WLEV", 2, u8, 0, 4, true},
    {"WRLD\\LevelHeight", "WLEV", 3, u8, 0, 4, true},
};

constexpr StringRow k_string_rows[] = {
    {"WRLD\\MapImage", "ICON"},
    {"WRLD\\HdLodDiffuseTexture", "TNAM"},
    {"WRLD\\HdLodNormalTexture", "UNAM"},
    {"WRLD\\WaterEnvironmentMap", "XWEM"},
};

constexpr LinkRow k_link_rows[] = {
    {"WRLD\\EncounterZone", "XEZN", "ECZN", true},
    {"WRLD\\Location", "XLCN", "LCTN", true},
    {"WRLD\\ParentWorldspace", "WNAM", "WRLD", false},
    {"WRLD\\Climate", "CNAM", "CLMT", false},
    {"WRLD\\Water", "NAM2", "WATR", false},
    {"WRLD\\LodWater", "NAM3", "WATR", false},
    {"WRLD\\InteriorLighting", "LTMP", "LGTM", false},
    {"WRLD\\Music", "ZNAM", "MUSC", false},
};

constexpr std::uint32_t raw_id(std::uint32_t local_id) { return 0x02000000u | local_id; }

Bytes zstr(std::string_view text) {
  ByteWriter out;
  out.raw(text);
  out.u8(0);
  return out.bytes;
}

Bytes u32_bytes(std::uint32_t value) {
  ByteWriter out;
  out.u32(value);
  return out.bytes;
}

Bytes floats(std::initializer_list<float> values) {
  ByteWriter out;
  for (const auto value : values) out.f32(value);
  return out.bytes;
}

Bytes map_data() {
  ByteWriter out;
  out.i32(64);
  out.i32(32);
  out.u16(static_cast<std::uint16_t>(-33));
  out.u16(25);
  out.u16(28);
  out.u16(static_cast<std::uint16_t>(-36));
  return out.bytes;
}

Bytes level_dimension() {
  ByteWriter out;
  out.u8(0xDF);
  out.u8(0x19);
  out.u8(16);
  out.u8(12);
  return out.bytes;
}

Bytes level_cell_data() {
  return Bytes{std::byte{0x01}, std::byte{0x02}, std::byte{0x03},
               std::byte{0x04}, std::byte{0x05}, std::byte{0x06}};
}

Bytes max_height_data() {
  ByteWriter out;
  out.u16(static_cast<std::uint16_t>(-2));
  out.u16(static_cast<std::uint16_t>(-1));
  out.u16(3);
  out.u16(4);
  out.u32(0x0B0A0908u);
  return out.bytes;
}

void write_header(ByteWriter& out, std::uint32_t tes4_flags = 0) {
  const auto tes4 = fo4::test::begin_record(out, "TES4", tes4_flags, 0);
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

std::vector<std::byte> make_plugin(std::uint32_t tes4_flags = 0) {
  ByteWriter out;
  write_header(out, tes4_flags);
  const auto group = fo4::test::begin_group(out, "WRLD", 0);
  const auto record = fo4::test::begin_record(out, "WRLD", 0, raw_id(0x0C01));
  const std::vector<Sub> subs{
      {"EDID", zstr("RadicleWorld")},
      {"FULL", zstr("Radicle World")},
      {"ICON", zstr("Interface\\WorldMap_d.dds")},
      {"XLCN", u32_bytes(0x00000A10)},
      {"PNAM", Bytes{std::byte{0x16}, std::byte{0x00}}},
      {"DATA", Bytes{std::byte{0x41}}},
      {"WCTR", Bytes{std::byte{0xFF}, std::byte{0xFF}, std::byte{0xFB}, std::byte{0xFF}}},
      {"NAM0", floats({-100.0f, -200.0f})},
      {"NAM9", floats({100.0f, 200.0f})},
      {"DNAM", floats({-2048.0f, 450.0f})},
      {"MNAM", map_data()},
      {"ONAM", floats({1.0f, -16384.0f, 512.0f, 3072.0f})},
      {"MHDT", max_height_data()},
      {"WLEV", level_dimension()},
      {"WLEV", level_cell_data()},
  };
  for (const auto& sub : subs) fo4::test::write_subrecord(out, sub.signature, sub.data);
  fo4::test::end_record(out, record);
  fo4::test::end_group(out, group);
  return out.bytes;
}

std::vector<std::byte> make_blob_level_plugin() {
  ByteWriter out;
  write_header(out);
  const auto group = fo4::test::begin_group(out, "WRLD", 0);
  const auto record = fo4::test::begin_record(out, "WRLD", 0, raw_id(0x0C02));
  const std::vector<Sub> subs{
      {"EDID", zstr("BlobWorld")},
      {"WLEV", level_cell_data()},
  };
  for (const auto& sub : subs) fo4::test::write_subrecord(out, sub.signature, sub.data);
  fo4::test::end_record(out, record);
  fo4::test::end_group(out, group);
  return out.bytes;
}

std::vector<std::byte> make_localized_plugin() {
  ByteWriter out;
  write_header(out, 0x80);
  const auto group = fo4::test::begin_group(out, "WRLD", 0);
  const auto record = fo4::test::begin_record(out, "WRLD", 0, raw_id(0x0C03));
  const std::vector<Sub> subs{
      {"EDID", zstr("LocalizedWorld")},
      {"FULL", u32_bytes(0x00001A2B)},
      {"WLEV", level_dimension()},
  };
  for (const auto& sub : subs) fo4::test::write_subrecord(out, sub.signature, sub.data);
  fo4::test::end_record(out, record);
  fo4::test::end_group(out, group);
  return out.bytes;
}

Bytes sub_bytes_at(const fo4::Plugin& plugin, std::string_view key, std::string_view subrecord,
                   std::size_t ordinal) {
  const auto* record = fo4::find_record(plugin, fo4::FormKey::parse(key));
  if (record == nullptr) {
    ADD_FAILURE() << key;
    return {};
  }
  std::size_t seen = 0;
  for (const auto& sub : record->subrecords) {
    if (sub.signature.to_string() != subrecord) continue;
    if (seen == ordinal) return sub.data;
    ++seen;
  }
  ADD_FAILURE() << subrecord << '[' << ordinal << ']';
  return {};
}

Bytes sub_bytes(const fo4::Plugin& plugin, std::string_view key, std::string_view subrecord) {
  return sub_bytes_at(plugin, key, subrecord, 0);
}

Bytes scalar_bytes(const fo4::Plugin& plugin, std::string_view key, std::string_view path) {
  const auto field =
      fo4::schema::scalar_field_descriptor(fo4::Signature::from_string("WRLD"), path);
  if (!field.has_value()) {
    ADD_FAILURE() << path;
    return {};
  }
  const auto data = sub_bytes(plugin, key, field->scalar.subrecord.to_string());
  const auto bytes = fo4::schema::scalar_field_bytes(data, field->scalar);
  return Bytes(bytes.begin(), bytes.end());
}

std::uint32_t link_raw(const fo4::Plugin& plugin, std::string_view key,
                       std::string_view subrecord) {
  const auto record = fo4::FormKey::parse(key);
  for (const auto& hit : fo4::collect_form_links(plugin)) {
    if (hit.record == record && hit.subrecord.to_string() == subrecord) {
      return hit.raw.value();
    }
  }
  ADD_FAILURE() << key << ' ' << subrecord;
  return 0;
}

std::vector<std::byte> make_three_level_plugin() {
  ByteWriter out;
  write_header(out);
  const auto group = fo4::test::begin_group(out, "WRLD", 0);
  const auto record = fo4::test::begin_record(out, "WRLD", 0, raw_id(0x0C04));
  const std::vector<Sub> subs{
      {"EDID", zstr("ThreeLevelWorld")},
      {"WLEV", level_dimension()},
      {"WLEV", level_cell_data()},
      {"WLEV", level_cell_data()},
  };
  for (const auto& sub : subs) fo4::test::write_subrecord(out, sub.signature, sub.data);
  fo4::test::end_record(out, record);
  fo4::test::end_group(out, group);
  return out.bytes;
}

std::vector<std::byte> make_offset_cell_size_plugin() {
  ByteWriter out;
  write_header(out);
  const auto group = fo4::test::begin_group(out, "WRLD", 0);
  const auto record = fo4::test::begin_record(out, "WRLD", 0, raw_id(0x0C05));
  const std::vector<Sub> subs{
      {"EDID", zstr("OffsetWorld")},
      {"WLEV", level_dimension()},
      {"OFST", floats({1.0f, 2.0f})},
      {"CLSZ", floats({3.0f, 4.0f, 5.0f, 6.0f})},
  };
  for (const auto& sub : subs) fo4::test::write_subrecord(out, sub.signature, sub.data);
  fo4::test::end_record(out, record);
  fo4::test::end_group(out, group);
  return out.bytes;
}

}  // namespace

TEST(WRLDRows, ScalarRowsHaveXEditOffsetsStorageAndPayloadFloor) {
  for (const auto& row : k_scalar_rows) {
    SCOPED_TRACE(row.path);
    const auto field =
        fo4::schema::scalar_field_descriptor(fo4::Signature::from_string("WRLD"), row.path);
    ASSERT_TRUE(field.has_value());
    EXPECT_EQ(field->scalar.subrecord, fo4::Signature::from_string(row.subrecord));
    EXPECT_EQ(field->scalar.offset, row.offset);
    EXPECT_EQ(field->scalar.storage, row.storage);
    EXPECT_EQ(field->scalar.minimum_payload_size, row.minimum_payload_size);
    EXPECT_EQ(field->scalar.payload_size, row.payload_size);
    EXPECT_EQ(field->scalar.address.repeated, row.repeated);
    EXPECT_TRUE(field->scalar.embedded);
  }
}

TEST(WRLDRows, StringAndLinkRowsNameTheirSubrecordsAndTargets) {
  for (const auto& row : k_string_rows) {
    SCOPED_TRACE(row.path);
    const auto field =
        fo4::schema::scalar_field_descriptor(fo4::Signature::from_string("WRLD"), row.path);
    ASSERT_TRUE(field.has_value());
    EXPECT_EQ(field->scalar.subrecord, fo4::Signature::from_string(row.subrecord));
    EXPECT_FALSE(field->scalar.embedded);
  }
  for (const auto& row : k_link_rows) {
    SCOPED_TRACE(row.path);
    const auto field =
        fo4::schema::formlink_field_descriptor(fo4::Signature::from_string("WRLD"), row.path);
    ASSERT_TRUE(field.has_value());
    EXPECT_EQ(field->subrecord, fo4::Signature::from_string(row.subrecord));
    EXPECT_EQ(field->offset, 0u);
    EXPECT_EQ(field->nullable, row.nullable);
    EXPECT_EQ(field->payload_sizes[0], 4u);
    const auto targets = fo4::schema::formlink_field_targets(*field);
    ASSERT_EQ(targets.size(), 1u);
    EXPECT_EQ(targets[0], fo4::Signature::from_string(row.target));
  }
}

TEST(WRLDRows, ReadsWorldSubrecordsThroughTheRows) {
  const auto plugin = fo4::read_plugin(make_plugin(), "Patch.esp");
  const auto key = "000C01:Patch.esp";
  EXPECT_EQ(scalar_bytes(plugin, key, "WRLD\\Flags"), Bytes{std::byte{0x41}});
  EXPECT_EQ(scalar_bytes(plugin, key, "WRLD\\ParentFlags"),
            (Bytes{std::byte{0x16}, std::byte{0x00}}));
  EXPECT_EQ(scalar_bytes(plugin, key, "WRLD\\FixedCenterY"),
            (Bytes{std::byte{0xFB}, std::byte{0xFF}}));
  EXPECT_EQ(scalar_bytes(plugin, key, "WRLD\\MapUsableX"), u32_bytes(64));
  EXPECT_EQ(scalar_bytes(plugin, key, "WRLD\\MapSeCellY"),
            (Bytes{std::byte{0xDC}, std::byte{0xFF}}));
  EXPECT_EQ(scalar_bytes(plugin, key, "WRLD\\MapCellOffsetY"), floats({512.0f}));
  EXPECT_EQ(scalar_bytes(plugin, key, "WRLD\\DefaultWaterHeight"), floats({450.0f}));

  const auto field =
      fo4::schema::scalar_field_descriptor(fo4::Signature::from_string("WRLD"), "WRLD\\MapImage");
  ASSERT_TRUE(field.has_value());
  const auto data = sub_bytes(plugin, key, "ICON");
  EXPECT_EQ(fo4::schema::decode_scalar(fo4::schema::scalar_field_bytes(data, field->scalar),
                                       field->scalar),
            "Interface\\WorldMap_d.dds");
  EXPECT_EQ(link_raw(plugin, key, "XLCN"), 0x00000A10u);
}

TEST(WRLDRows, WritesScalarFlagStringAndLinkThenRereads) {
  auto plugin = fo4::read_plugin(make_plugin(), "Patch.esp");
  const auto key = fo4::FormKey::parse("000C01:Patch.esp");
  const auto target = fo4::FormKey::parse("000A20:Objects.esp");
  const auto land = fo4::schema::scalar_field_descriptor(fo4::Signature::from_string("WRLD"),
                                                         "WRLD\\DefaultLandHeight");
  const auto flags =
      fo4::schema::scalar_field_descriptor(fo4::Signature::from_string("WRLD"), "WRLD\\Flags");
  const auto location =
      fo4::schema::formlink_field_descriptor(fo4::Signature::from_string("WRLD"), "WRLD\\Location");
  ASSERT_TRUE(land.has_value());
  ASSERT_TRUE(flags.has_value());
  ASSERT_TRUE(location.has_value());
  EXPECT_EQ(fo4::set_record_scalar(plugin, key, *land, "-1024"),
            fo4::ScalarMutationStatus::updated);
  EXPECT_EQ(fo4::set_record_scalar(plugin, key, *flags, "2"), fo4::ScalarMutationStatus::updated);
  EXPECT_EQ(fo4::set_record_string(plugin, key, "WRLD\\MapImage", 0, "Interface\\New.dds"),
            fo4::ScalarMutationStatus::updated);
  EXPECT_EQ(fo4::set_record_formlink_field(plugin, key, *location, target),
            fo4::FormLinkFieldMutationStatus::updated);

  const auto reread = fo4::read_plugin(fo4::write_plugin(plugin), "Patch.esp");
  EXPECT_EQ(sub_bytes(reread, "000C01:Patch.esp", "DNAM"), floats({-1024.0f, 450.0f}));
  EXPECT_EQ(sub_bytes(reread, "000C01:Patch.esp", "DATA"), Bytes{std::byte{0x02}});
  EXPECT_EQ(sub_bytes(reread, "000C01:Patch.esp", "ICON"), zstr("Interface\\New.dds"));
  EXPECT_EQ(link_raw(reread, "000C01:Patch.esp", "XLCN"), 0x01000A20u);
  EXPECT_EQ(sub_bytes(reread, "000C01:Patch.esp", "PNAM"), (Bytes{std::byte{0x16}, std::byte{0x00}}));
}

TEST(WRLDRows, ReadsMaxHeightAndLevelRows) {
  const auto plugin = fo4::read_plugin(make_plugin(), "Patch.esp");
  const auto key = "000C01:Patch.esp";
  EXPECT_EQ(scalar_bytes(plugin, key, "WRLD\\MaxHeightMinX"),
            (Bytes{std::byte{0xFE}, std::byte{0xFF}}));
  EXPECT_EQ(scalar_bytes(plugin, key, "WRLD\\MaxHeightMaxY"),
            (Bytes{std::byte{0x04}, std::byte{0x00}}));
  EXPECT_EQ(scalar_bytes(plugin, key, "WRLD\\LevelNwCellX"), Bytes{std::byte{0xDF}});
  EXPECT_EQ(scalar_bytes(plugin, key, "WRLD\\LevelWidth"), Bytes{std::byte{0x10}});
  EXPECT_EQ(scalar_bytes(plugin, key, "WRLD\\LevelHeight"), Bytes{std::byte{0x0C}});

  const auto level_x = fo4::schema::scalar_field_descriptor(
      fo4::Signature::from_string("WRLD"), "WRLD\\LevelNwCellX");
  const auto max_height = fo4::schema::scalar_field_descriptor(
      fo4::Signature::from_string("WRLD"), "WRLD\\MaxHeightMinX");
  ASSERT_TRUE(level_x.has_value());
  ASSERT_TRUE(max_height.has_value());
  EXPECT_EQ(fo4::schema::decode_scalar(scalar_bytes(plugin, key, "WRLD\\LevelNwCellX"),
                                       level_x->scalar),
            "-33");
  EXPECT_EQ(fo4::schema::decode_scalar(scalar_bytes(plugin, key, "WRLD\\MaxHeightMinX"),
                                       max_height->scalar),
            "-2");
}

TEST(WRLDRows, WritesLevelAndMaxHeightWithoutTouchingTheCellDataBlob) {
  auto plugin = fo4::read_plugin(make_plugin(), "Patch.esp");
  const auto key = fo4::FormKey::parse("000C01:Patch.esp");
  const auto level_y = fo4::schema::scalar_field_descriptor(
      fo4::Signature::from_string("WRLD"), "WRLD\\LevelNwCellY");
  const auto level_width = fo4::schema::scalar_field_descriptor(
      fo4::Signature::from_string("WRLD"), "WRLD\\LevelWidth");
  const auto max_height = fo4::schema::scalar_field_descriptor(
      fo4::Signature::from_string("WRLD"), "WRLD\\MaxHeightMinX");
  ASSERT_TRUE(level_y.has_value());
  ASSERT_TRUE(level_width.has_value());
  ASSERT_TRUE(max_height.has_value());
  EXPECT_EQ(fo4::set_record_scalar(plugin, key, *level_y, "-5"),
            fo4::ScalarMutationStatus::updated);
  EXPECT_EQ(fo4::set_record_scalar(plugin, key, *level_width, "40"),
            fo4::ScalarMutationStatus::updated);
  EXPECT_EQ(fo4::set_record_scalar(plugin, key, *max_height, "-300"),
            fo4::ScalarMutationStatus::updated);

  const auto reread = fo4::read_plugin(fo4::write_plugin(plugin), "Patch.esp");
  EXPECT_EQ(sub_bytes_at(reread, "000C01:Patch.esp", "WLEV", 0),
            (Bytes{std::byte{0xDF}, std::byte{0xFB}, std::byte{0x28}, std::byte{0x0C}}));
  EXPECT_EQ(sub_bytes_at(reread, "000C01:Patch.esp", "WLEV", 1), level_cell_data());
  EXPECT_EQ(sub_bytes_at(reread, "000C01:Patch.esp", "MHDT", 0),
            (Bytes{std::byte{0xD4}, std::byte{0xFE}, std::byte{0xFF}, std::byte{0xFF},
                   std::byte{0x03}, std::byte{0x00}, std::byte{0x04}, std::byte{0x00},
                   std::byte{0x08}, std::byte{0x09}, std::byte{0x0A}, std::byte{0x0B}}));
}

TEST(WRLDRows, PreflightAcceptsTwoWlevSubrecordsAndLeavesTheCellDataBlobAlone) {
  const auto plugin = fo4::read_plugin(make_plugin(), "Patch.esp");
  const auto* record = fo4::find_record(plugin, fo4::FormKey::parse("000C01:Patch.esp"));
  ASSERT_NE(record, nullptr);
  for (const auto& issue : fo4::schema::preflight_record(*record)) {
    EXPECT_NE(issue.code, "duplicate_scalar_subrecord") << issue.path;
    EXPECT_NE(issue.code, "invalid_scalar_storage") << issue.path;
  }

  auto blob = fo4::read_plugin(make_blob_level_plugin(), "Patch.esp");
  const auto blob_key = fo4::FormKey::parse("000C02:Patch.esp");
  const auto* blob_record = fo4::find_record(blob, blob_key);
  ASSERT_NE(blob_record, nullptr);
  for (const auto& issue : fo4::schema::preflight_record(*blob_record)) {
    EXPECT_NE(issue.code, "duplicate_scalar_subrecord") << issue.path;
    EXPECT_NE(issue.code, "invalid_scalar_storage") << issue.path;
  }
  const auto level_width = fo4::schema::scalar_field_descriptor(
      fo4::Signature::from_string("WRLD"), "WRLD\\LevelWidth");
  const auto level_x = fo4::schema::scalar_field_descriptor(
      fo4::Signature::from_string("WRLD"), "WRLD\\LevelNwCellX");
  ASSERT_TRUE(level_width.has_value());
  ASSERT_TRUE(level_x.has_value());
  EXPECT_EQ(fo4::set_record_scalar(blob, blob_key, *level_width, "4"),
            fo4::ScalarMutationStatus::subrecord_missing);
  EXPECT_EQ(fo4::set_record_scalar(blob, blob_key, *level_x, "-1"),
            fo4::ScalarMutationStatus::subrecord_missing);
}

TEST(WRLDRows, PreflightAllowsTheSixByteLevelBlobBesideTheRowAndCountsTheRows) {
  const auto plugin = fo4::read_plugin(make_plugin(), "Patch.esp");
  for (const auto& issue : fo4::schema::preflight_authoring_record(
           plugin, fo4::FormKey::parse("000C01:Patch.esp"))) {
    EXPECT_NE(issue.code, "invalid_scalar_storage") << issue.path;
    EXPECT_NE(issue.code, "duplicate_scalar_subrecord") << issue.path;
    EXPECT_NE(issue.code, "subrecord_cardinality_invalid") << issue.path;
  }

  const auto three = fo4::read_plugin(make_three_level_plugin(), "Patch.esp");
  bool cardinality_reported = false;
  for (const auto& issue : fo4::schema::preflight_authoring_record(
           three, fo4::FormKey::parse("000C04:Patch.esp"))) {
    if (issue.code == "subrecord_cardinality_invalid" && issue.path == "WRLD\\WLEV") {
      cardinality_reported = true;
    }
    EXPECT_NE(issue.code, "invalid_scalar_storage") << issue.path;
  }
  EXPECT_TRUE(cardinality_reported);
}

TEST(WRLDRows, OffsetAndCellSizeBlobsArePreservedButNotAuthored) {
  auto plugin = fo4::read_plugin(make_offset_cell_size_plugin(), "Patch.esp");
  const auto key = fo4::FormKey::parse("000C05:Patch.esp");
  const auto ofst = sub_bytes(plugin, "000C05:Patch.esp", "OFST");
  const auto clsz = sub_bytes(plugin, "000C05:Patch.esp", "CLSZ");
  EXPECT_EQ(ofst.size(), 8u);
  EXPECT_EQ(clsz.size(), 16u);
  EXPECT_EQ(fo4::add_record_subrecord(plugin, key, fo4::Signature::from_string("OFST")),
            fo4::SubrecordAddStatus::already_present);
  EXPECT_EQ(fo4::add_record_subrecord(plugin, key, fo4::Signature::from_string("CLSZ")),
            fo4::SubrecordAddStatus::already_present);

  const auto level_width = fo4::schema::scalar_field_descriptor(
      fo4::Signature::from_string("WRLD"), "WRLD\\LevelWidth");
  ASSERT_TRUE(level_width.has_value());
  EXPECT_EQ(fo4::set_record_scalar(plugin, key, *level_width, "7"),
            fo4::ScalarMutationStatus::updated);

  const auto reread = fo4::read_plugin(fo4::write_plugin(plugin), "Patch.esp");
  EXPECT_EQ(sub_bytes_at(reread, "000C05:Patch.esp", "WLEV", 0),
            (Bytes{std::byte{0xDF}, std::byte{0x19}, std::byte{0x07}, std::byte{0x0C}}));
  EXPECT_EQ(sub_bytes(reread, "000C05:Patch.esp", "OFST"), ofst);
  EXPECT_EQ(sub_bytes(reread, "000C05:Patch.esp", "CLSZ"), clsz);

  auto plain = fo4::read_plugin(make_plugin(), "Patch.esp");
  EXPECT_EQ(fo4::add_record_subrecord(plain, fo4::FormKey::parse("000C01:Patch.esp"),
                                      fo4::Signature::from_string("OFST")),
            fo4::SubrecordAddStatus::no_default);
}

TEST(WRLDRows, LocalizedWorldKeepsItsStringIdAndStillWritesLevelRows) {
  auto plugin = fo4::read_plugin(make_localized_plugin(), "Patch.esp");
  const auto key = fo4::FormKey::parse("000C03:Patch.esp");
  const auto* record = fo4::find_record(plugin, key);
  ASSERT_NE(record, nullptr);
  const auto name = fo4::env::decode_record_name(plugin, *record);
  EXPECT_EQ(name.kind, fo4::env::NameKind::string_id);
  EXPECT_EQ(name.string_id, 0x00001A2Bu);
  for (const auto& issue : fo4::schema::preflight_record(*record, true)) {
    EXPECT_NE(issue.code, "duplicate_scalar_subrecord") << issue.path;
    EXPECT_NE(issue.code, "invalid_scalar_storage") << issue.path;
  }
  const auto level_height = fo4::schema::scalar_field_descriptor(
      fo4::Signature::from_string("WRLD"), "WRLD\\LevelHeight");
  ASSERT_TRUE(level_height.has_value());
  EXPECT_EQ(fo4::set_record_scalar(plugin, key, *level_height, "7"),
            fo4::ScalarMutationStatus::updated);
  const auto reread = fo4::read_plugin(fo4::write_plugin(plugin), "Patch.esp");
  EXPECT_EQ(sub_bytes_at(reread, "000C03:Patch.esp", "WLEV", 0),
            (Bytes{std::byte{0xDF}, std::byte{0x19}, std::byte{0x10}, std::byte{0x07}}));
  const auto* reread_record = fo4::find_record(reread, key);
  ASSERT_NE(reread_record, nullptr);
  EXPECT_EQ(fo4::env::decode_record_name(reread, *reread_record).string_id, 0x00001A2Bu);
}

TEST(WRLDRows, AuthoringRowsFollowXEditOrderAndRepeatRules) {
  const auto constraint =
      fo4::schema::record_authoring_constraint(fo4::Signature::from_string("WRLD"));
  ASSERT_TRUE(constraint.has_value());
  EXPECT_EQ(constraint->placement, fo4::schema::RecordPlacement::top_level);
  const auto rules = fo4::schema::authoring_subrecords(*constraint);
  const std::vector<std::string_view> expected{
      "EDID", "RNAM", "MHDT", "FULL", "WCTR", "LTMP", "XEZN", "XLCN", "WNAM", "PNAM", "CNAM",
      "NAM2", "NAM3", "NAM4", "DNAM", "ICON", "MODL", "MODT", "MODC", "MODS", "MODF", "MNAM",
      "ONAM", "NAMA", "DATA", "NAM0", "NAM9", "ZNAM", "NNAM", "XWEM", "TNAM", "UNAM", "WLEV",
      "OFST", "CLSZ"};
  ASSERT_EQ(rules.size(), expected.size());
  for (std::size_t index = 0; index < expected.size(); ++index) {
    EXPECT_EQ(rules[index].signature, fo4::Signature::from_string(expected[index])) << index;
  }

  const auto rule_for = [&](std::string_view signature) {
    const auto wanted = fo4::Signature::from_string(signature);
    for (std::size_t index = 0; index < rules.size(); ++index) {
      if (rules[index].signature == wanted) return rules[index];
    }
    ADD_FAILURE() << signature;
    return rules[0];
  };
  EXPECT_EQ(rule_for("RNAM").maximum, 65535);
  EXPECT_TRUE(rule_for("RNAM").uncounted);
  EXPECT_FALSE(rule_for("RNAM").has_default());
  EXPECT_EQ(rule_for("WLEV").maximum, 2);
  EXPECT_FALSE(rule_for("WLEV").uncounted);
  EXPECT_EQ(rule_for("NAM0").minimum, 1);
  EXPECT_EQ(rule_for("NAM4").minimum, 0);
  EXPECT_EQ(rule_for("PNAM").minimum, 0);
}

TEST(WRLDRows, CreatesAWorldspaceFromTheAuthoringDefaults) {
  auto plugin = fo4::read_plugin(make_plugin(), "Patch.esp");
  const auto created =
      fo4::create_record(plugin, fo4::Signature::from_string("WRLD"), "RadicleNewWorld");
  ASSERT_EQ(created.status, fo4::RecordCreateStatus::created);
  const auto* record = fo4::find_record(plugin, created.key);
  ASSERT_NE(record, nullptr);

  std::vector<std::string> order;
  for (const auto& sub : record->subrecords) order.push_back(sub.signature.to_string());
  EXPECT_EQ(order, (std::vector<std::string>{"EDID", "ONAM", "NAMA", "DATA", "NAM0", "NAM9"}));

  const auto key = created.key.to_string();
  EXPECT_EQ(sub_bytes(plugin, key, "EDID"), zstr("RadicleNewWorld"));
  EXPECT_EQ(sub_bytes(plugin, key, "ONAM"), floats({1.0f, 0.0f, 0.0f, 0.0f}));
  EXPECT_EQ(sub_bytes(plugin, key, "NAMA"), floats({1.0f}));
  EXPECT_EQ(sub_bytes(plugin, key, "DATA"), (Bytes{std::byte{0x01}}));
  EXPECT_EQ(sub_bytes(plugin, key, "NAM0"), Bytes(8));
  EXPECT_EQ(sub_bytes(plugin, key, "NAM9"), Bytes(8));

  for (const auto& issue : fo4::schema::preflight_authoring_record(plugin, created.key)) {
    ADD_FAILURE() << issue.code << ' ' << issue.path;
  }

  const auto reread = fo4::read_plugin(fo4::write_plugin(plugin), "Patch.esp");
  EXPECT_EQ(sub_bytes(reread, key, "ONAM"), floats({1.0f, 0.0f, 0.0f, 0.0f}));
  EXPECT_EQ(sub_bytes(reread, key, "NAM0"), Bytes(8));
  EXPECT_EQ(sub_bytes(reread, key, "NAM9"), Bytes(8));
}

TEST(WRLDRows, AuthoringPreflightReportsMissingNamaAndTheOutOfOrderTail) {
  const auto plugin = fo4::read_plugin(make_plugin(), "Patch.esp");
  const auto issues =
      fo4::schema::preflight_authoring_record(plugin, fo4::FormKey::parse("000C01:Patch.esp"));
  bool missing_nama = false;
  bool order_reported = false;
  for (const auto& issue : issues) {
    if (issue.code == "required_subrecord_missing" && issue.path == "WRLD\\NAMA") missing_nama = true;
    if (issue.code == "subrecord_order_invalid") order_reported = true;
  }
  EXPECT_TRUE(missing_nama);
  EXPECT_TRUE(order_reported);
}

TEST(WRLDRows, AuthoringRefusesWlevAndRnamBecauseNeitherHasADefault) {
  auto plugin = fo4::read_plugin(make_plugin(), "Patch.esp");
  const auto key = fo4::FormKey::parse("000C01:Patch.esp");
  EXPECT_EQ(fo4::add_record_subrecord(plugin, key, fo4::Signature::from_string("WLEV")),
            fo4::SubrecordAddStatus::not_authorable);
  EXPECT_EQ(fo4::add_record_subrecord(plugin, key, fo4::Signature::from_string("RNAM")),
            fo4::SubrecordAddStatus::no_default);
}
