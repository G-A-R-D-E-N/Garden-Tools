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
#include <initializer_list>
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

std::vector<std::byte> make_plugin() {
  ByteWriter out;
  write_header(out);
  const auto group = fo4::test::begin_group(out, "WRLD", 0);
  const auto record = fo4::test::begin_record(out, "WRLD", 0, raw_id(0x0C01));
  const std::vector<Sub> subs{
      {"EDID", zstr("RadicleWorld")},
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
  };
  for (const auto& sub : subs) fo4::test::write_subrecord(out, sub.signature, sub.data);
  fo4::test::end_record(out, record);
  fo4::test::end_group(out, group);
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
