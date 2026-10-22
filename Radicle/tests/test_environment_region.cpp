#include "fo4/environment/region.hpp"
#include "env_node_fixture.hpp"
#include "plugin_bytes.hpp"
#include "temp_data_dir.hpp"

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <initializer_list>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

using Bytes = std::vector<std::byte>;
using fo4::test::ByteWriter;
using fo4::test::envnode::bytes_of;
using fo4::test::envnode::floats;
using fo4::test::envnode::rec;
using fo4::test::envnode::top;

Bytes u32_bytes(std::uint32_t value) {
  ByteWriter out;
  out.u32(value);
  return out.bytes;
}

Bytes text_bytes(std::string_view text) {
  Bytes bytes;
  for (const char ch : text) bytes.push_back(std::byte{static_cast<std::uint8_t>(ch)});
  bytes.push_back(std::byte{0});
  return bytes;
}

Bytes rdat_bytes(std::uint32_t type, std::uint8_t override_value, std::uint8_t priority) {
  ByteWriter out;
  out.u32(type);
  out.u8(override_value);
  out.u8(priority);
  out.u8(0);
  out.u8(0);
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

void write_grouped_record(ByteWriter& out, std::string_view signature, std::uint32_t local_id,
                          std::initializer_list<std::pair<std::string_view, Bytes>> subs) {
  const auto group = fo4::test::begin_group(out, signature, 0);
  const auto record = fo4::test::begin_record(out, signature, 0, 0x02000000u | local_id);
  for (const auto& sub : subs) fo4::test::write_subrecord(out, sub.first, sub.second);
  fo4::test::end_record(out, record);
  fo4::test::end_group(out, group);
}

Bytes point_bytes(std::initializer_list<float> values) {
  ByteWriter out;
  for (const auto value : values) out.f32(value);
  return out.bytes;
}

Bytes rdot_bytes() {
  ByteWriter out;
  out.u32(0x02000D40);
  out.u16(3);
  out.u16(0);
  out.f32(0.25f);
  out.u8(1);
  out.u8(2);
  out.u8(3);
  out.u8(0x81);
  out.u16(7);
  out.u16(9);
  out.f32(-10.0f);
  out.f32(20.0f);
  out.f32(0.5f);
  out.f32(0.75f);
  out.f32(1.25f);
  out.u16(11);
  out.u16(12);
  out.u16(13);
  out.u16(0);
  out.u32(0);
  return out.bytes;
}

Bytes rdwt_bytes() {
  ByteWriter out;
  out.u32(0x02000D50);
  out.u32(100);
  out.u32(0x02000D51);
  out.u32(0x02000D52);
  out.u32(0);
  out.u32(0);
  return out.bytes;
}

Bytes rdgs_bytes(std::uint32_t object) {
  ByteWriter out;
  out.u32(object);
  out.u32(0);
  return out.bytes;
}

Bytes rdsa_bytes() {
  ByteWriter out;
  out.u32(0x02000D70);
  out.u32(3);
  out.f32(0.5f);
  return out.bytes;
}

Bytes typed_region_plugin() {
  ByteWriter out;
  write_header(out);
  write_grouped_record(
      out, "REGN", 0x0D10,
      {{"EDID", text_bytes("TypedRegion")},
       {"RCLR", Bytes{std::byte{0x11}, std::byte{0x22}, std::byte{0x33}, std::byte{0x00}}},
       {"WNAM", u32_bytes(0x00000D20)},
       {"RPLI", u32_bytes(80)},
       {"RPLD", point_bytes({1.5f, -2.5f, 3.5f, 4.5f})},
       {"RPLI", u32_bytes(120)},
       {"RPLD", point_bytes({6.5f, 7.5f})},
       {"RDAT", rdat_bytes(3, 1, 40)},
       {"RDWT", rdwt_bytes()},
       {"RDAT", rdat_bytes(2, 0, 50)},
       {"RDOT", rdot_bytes()},
       {"RDAT", rdat_bytes(6, 0, 60)},
       {"RDGS", rdgs_bytes(0x02000D60)},
       {"RDAT", rdat_bytes(7, 1, 70)},
       {"RDMO", u32_bytes(0x02000D21)},
       {"RDSA", rdsa_bytes()},
       {"RDAT", rdat_bytes(4, 0, 80)},
       {"RDMP", text_bytes("Map Name")},
       {"RDAT", rdat_bytes(5, 0, 90)}});
  return out.bytes;
}

TEST(RegionEnvironmentTest, DecodesTypedRegionAreasEntriesAndPayloads) {
  fo4::test::TempDataDir dir;
  dir.write("Regions.esp", typed_region_plugin(), 0);
  const std::vector<std::filesystem::path> ordered{dir.path() / "Regions.esp"};
  const auto env = fo4::env::Environment::load(ordered);
  const auto* ref = env.winner(fo4::FormKey::parse("000D10:Regions.esp"));
  ASSERT_NE(ref, nullptr);
  const auto region = fo4::env::decode_region(env, *ref);

  ASSERT_TRUE(region.map_color.has_value());
  EXPECT_EQ(region.map_color->red, 0x11);
  EXPECT_EQ(region.map_color->green, 0x22);
  EXPECT_EQ(region.map_color->blue, 0x33);
  EXPECT_EQ(region.worldspace, fo4::FormKey::parse("000D20:Fallout4.esm"));

  ASSERT_EQ(region.areas.size(), 2u);
  EXPECT_EQ(region.areas[0].edge_falloff, 80u);
  ASSERT_EQ(region.areas[0].points.size(), 2u);
  EXPECT_EQ(region.areas[0].points[0].x, 1.5f);
  EXPECT_EQ(region.areas[0].points[0].y, -2.5f);
  EXPECT_EQ(region.areas[0].points[1].x, 3.5f);
  EXPECT_EQ(region.areas[0].points[1].y, 4.5f);
  EXPECT_EQ(region.areas[1].edge_falloff, 120u);
  ASSERT_EQ(region.areas[1].points.size(), 1u);
  EXPECT_EQ(region.areas[1].points[0].x, 6.5f);

  ASSERT_EQ(region.entries.size(), 6u);
  EXPECT_EQ(region.entries[0].type, 3u);
  EXPECT_EQ(region.entries[0].override_value, 1);
  EXPECT_EQ(region.entries[0].priority, 40);
  ASSERT_EQ(region.entries[0].weathers.size(), 2u);
  EXPECT_EQ(region.entries[0].weathers[0].weather, fo4::FormKey::parse("000D50:Regions.esp"));
  EXPECT_EQ(region.entries[0].weathers[0].chance, 100u);
  EXPECT_EQ(region.entries[0].weathers[0].global, fo4::FormKey::parse("000D51:Regions.esp"));
  EXPECT_EQ(region.entries[0].weathers[1].weather, fo4::FormKey::parse("000D52:Regions.esp"));
  EXPECT_EQ(region.entries[0].weathers[1].chance, 0u);
  EXPECT_FALSE(region.entries[0].weathers[1].global.has_value());

  ASSERT_EQ(region.entries[1].objects.size(), 1u);
  const auto& object = region.entries[1].objects[0];
  EXPECT_EQ(object.object, fo4::FormKey::parse("000D40:Regions.esp"));
  EXPECT_EQ(object.parent_index, 3);
  EXPECT_EQ(object.density, 0.25f);
  EXPECT_EQ(object.clustering, 1);
  EXPECT_EQ(object.min_slope, 2);
  EXPECT_EQ(object.max_slope, 3);
  EXPECT_EQ(object.flags, 0x81);
  EXPECT_EQ(object.radius_with_parent, 7);
  EXPECT_EQ(object.radius, 9);
  EXPECT_EQ(object.min_height, -10.0f);
  EXPECT_EQ(object.max_height, 20.0f);
  EXPECT_EQ(object.sink, 0.5f);
  EXPECT_EQ(object.sink_variance, 0.75f);
  EXPECT_EQ(object.size_variance, 1.25f);
  EXPECT_EQ(object.angle_variance[0], 11);
  EXPECT_EQ(object.angle_variance[1], 12);
  EXPECT_EQ(object.angle_variance[2], 13);

  ASSERT_EQ(region.entries[2].grasses.size(), 1u);
  EXPECT_EQ(region.entries[2].grasses[0], fo4::FormKey::parse("000D60:Regions.esp"));

  ASSERT_EQ(region.entries[3].sounds.size(), 1u);
  EXPECT_EQ(region.entries[3].sounds[0].sound, fo4::FormKey::parse("000D70:Regions.esp"));
  EXPECT_EQ(region.entries[3].sounds[0].flags, 3u);
  EXPECT_EQ(region.entries[3].sounds[0].chance, 0.5f);
  EXPECT_EQ(region.entries[3].music, fo4::FormKey::parse("000D21:Regions.esp"));

  EXPECT_EQ(region.entries[4].type, 4u);
  EXPECT_EQ(region.entries[4].map_name.kind, fo4::env::NameKind::inline_text);
  EXPECT_EQ(region.entries[4].map_name.text, "Map Name");
  EXPECT_EQ(region.entries[5].type, 5u);
  EXPECT_TRUE(region.entries[5].objects.empty());
  EXPECT_TRUE(region.entries[5].grasses.empty());
  EXPECT_TRUE(region.entries[5].weathers.empty());
  EXPECT_TRUE(region.entries[5].sounds.empty());
}

TEST(RegionEnvironmentTest, RejectsMalformedTypedRegionPayloads) {
  const auto bytes = fo4::test::envnode::make_plugin(
      {}, {top("REGN",
               {rec("REGN", 0x0E01, {{"RPLD", floats({1.0f, 2.0f})}}),
                rec("REGN", 0x0E02, {{"RPLI", bytes_of({80})}, {"RPLD", floats({1.0f, 2.0f, 3.0f})}}),
                rec("REGN", 0x0E03, {{"RPLI", {std::byte{0x50}, std::byte{0}}}}),
                rec("REGN", 0x0E04, {{"RDAT", bytes_of({3})}}),
                rec("REGN", 0x0E05, {{"RDAT", bytes_of({2, 0})}, {"RDOT", Bytes(51)}}),
                rec("REGN", 0x0E06, {{"RDAT", bytes_of({3, 0})}, {"RDWT", Bytes(13)}}),
                rec("REGN", 0x0E07, {{"RDAT", bytes_of({6, 0})}, {"RDGS", Bytes(7)}}),
                rec("REGN", 0x0E08, {{"RDAT", bytes_of({7, 0})}, {"RDSA", Bytes(11)}}),
                rec("REGN", 0x0E09, {{"RDGS", Bytes(8)}}),
                rec("REGN", 0x0E0A, {{"RDAT", bytes_of({4, 0})}, {"RDMP", Bytes(2)}}),
                rec("REGN", 0x0E0B, {{"WNAM", Bytes(2)}})}),
           top("CELL", {rec("CELL", 0x0E0C, {})})});
  fo4::test::TempDataDir dir;
  dir.write("Bad.esp", bytes, 0);
  const std::vector<std::filesystem::path> ordered{dir.path() / "Bad.esp"};
  const auto env = fo4::env::Environment::load(ordered);

  for (const auto* local : {"000E01", "000E02", "000E03", "000E04", "000E05", "000E06",
                            "000E07", "000E08", "000E09", "000E0A", "000E0B", "000E0C"}) {
    const auto* ref = env.winner(fo4::FormKey::parse(std::string(local) + ":Bad.esp"));
    ASSERT_NE(ref, nullptr) << local;
    EXPECT_THROW(fo4::env::decode_region(env, *ref), fo4::env::WorldDecodeError) << local;
  }
}

}  // namespace