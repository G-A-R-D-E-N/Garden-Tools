#include "fo4/environment/region.hpp"
#include "fo4/plugin/mutation.hpp"
#include "fo4/plugin/reader.hpp"
#include "fo4/plugin/subrecord_authoring.hpp"
#include "fo4/plugin/writer.hpp"
#include "fo4/schema/formlink_lists.hpp"
#include "fo4/schema/preflight.hpp"
#include "fo4/schema/scalar.hpp"
#include "env_node_fixture.hpp"
#include "plugin_bytes.hpp"
#include "temp_data_dir.hpp"

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <initializer_list>
#include <optional>
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

constexpr auto f32 = fo4::schema::ScalarStorage::f32;
constexpr auto i32 = fo4::schema::ScalarStorage::i32;
constexpr auto u8 = fo4::schema::ScalarStorage::u8;
constexpr auto u32 = fo4::schema::ScalarStorage::u32;

Bytes u8_bytes(std::uint8_t value) { return Bytes{std::byte{value}}; }

Bytes u32_bytes(std::uint32_t value) {
  ByteWriter out;
  out.u32(value);
  return out.bytes;
}

Bytes f32_bytes(float value) {
  ByteWriter out;
  out.f32(value);
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

Bytes f32_list(std::initializer_list<float> values) {
  ByteWriter out;
  for (const float value : values) out.f32(value);
  return out.bytes;
}

Bytes rdot_bytes(std::uint32_t object, std::uint16_t parent_index, float density) {
  ByteWriter out;
  out.u32(object);
  out.u16(parent_index);
  out.u16(0);
  out.f32(density);
  out.bytes.resize(52);
  return out.bytes;
}

Bytes rdwt_bytes(std::uint32_t weather, std::uint32_t chance, std::uint32_t global) {
  ByteWriter out;
  out.u32(weather);
  out.u32(chance);
  out.u32(global);
  return out.bytes;
}

Bytes rdsa_bytes(std::uint32_t sound, std::uint32_t flags, float chance) {
  ByteWriter out;
  out.u32(sound);
  out.u32(flags);
  out.f32(chance);
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

std::vector<std::byte> make_plugin() {
  ByteWriter out;
  write_header(out);
  write_grouped_record(out, "REGN", 0x0D10,
                       {{"EDID", text_bytes("TestRegion")},
                        {"RCLR", Bytes{std::byte{0x11}, std::byte{0x22}, std::byte{0x33},
                                       std::byte{0x00}}},
                        {"WNAM", u32_bytes(0x00000D20)},
                        {"RPLI", u32_bytes(80)},
                        {"RPLI", u32_bytes(120)},
                        {"RDAT", rdat_bytes(3, 1, 40)},
                        {"RDMO", u32_bytes(0x00000D21)},
                        {"ANAM", f32_bytes(0.5f)},
                        {"RDAT", rdat_bytes(5, 0, 60)},
                        {"RLDM", f32_bytes(2.5f)}});
  write_grouped_record(out, "WRLD", 0x0D30, {{"EDID", text_bytes("TestWorldspace")}});
  return out.bytes;
}

Bytes sub_bytes(const fo4::Plugin& plugin, std::string_view key, std::string_view subrecord,
                std::size_t occurrence = 0) {
  const auto* record = fo4::find_record(plugin, fo4::FormKey::parse(key));
  if (record == nullptr) {
    ADD_FAILURE() << key;
    return {};
  }
  std::size_t seen = 0;
  for (const auto& sub : record->subrecords) {
    if (sub.signature.to_string() != subrecord) continue;
    if (seen++ == occurrence) return sub.data;
  }
  return {};
}

std::optional<fo4::schema::ScalarFieldDescriptor> scalar_row(std::string_view path) {
  const auto found = fo4::schema::scalar_field_descriptor(
      fo4::Signature::from_string("REGN"), path);
  if (!found.has_value()) ADD_FAILURE() << path;
  return found;
}

Bytes field_bytes(const fo4::Plugin& plugin, std::string_view key, std::string_view path,
                  std::size_t occurrence = 0) {
  const auto field = scalar_row(path);
  if (!field.has_value()) return {};
  const auto data = sub_bytes(plugin, key, field->scalar.subrecord.to_string(), occurrence);
  if (data.empty()) return {};
  const auto bytes = fo4::schema::scalar_field_bytes(data, field->scalar);
  return Bytes(bytes.begin(), bytes.end());
}

std::uint32_t link_raw(const fo4::Plugin& plugin, std::string_view key, std::string_view path,
                       std::size_t occurrence = 0) {
  const auto field = fo4::schema::formlink_field_descriptor(fo4::Signature::from_string("REGN"),
                                                            path);
  if (!field.has_value()) {
    ADD_FAILURE() << path;
    return 0;
  }
  const auto data = sub_bytes(plugin, key, field->subrecord.to_string(), occurrence);
  if (data.size() < 4) return 0;
  std::uint32_t value = 0;
  std::memcpy(&value, data.data(), sizeof(value));
  return value;
}

TEST(RegnRows, DeclaresColorsDataEntryFieldsAndLinks) {
  const auto color = scalar_row("REGN\\MapColorRed");
  ASSERT_TRUE(color.has_value());
  EXPECT_EQ(color->scalar.subrecord, fo4::Signature::from_string("RCLR"));
  EXPECT_EQ(color->scalar.storage, u8);

  const auto type = scalar_row("REGN\\RegionDataType");
  ASSERT_TRUE(type.has_value());
  EXPECT_EQ(type->scalar.subrecord, fo4::Signature::from_string("RDAT"));
  EXPECT_EQ(type->scalar.storage, u32);
  EXPECT_EQ(type->scalar.presentation, fo4::schema::ScalarPresentation::enumeration);
  EXPECT_EQ(type->scalar.names.size(), 6u);
  EXPECT_EQ(fo4::schema::scalar_value_name(type->scalar, 3), std::string_view("Weather"));
  EXPECT_FALSE(fo4::schema::scalar_value_name(type->scalar, 1).has_value());

  const auto priority = scalar_row("REGN\\RegionDataPriority");
  ASSERT_TRUE(priority.has_value());
  EXPECT_EQ(priority->scalar.offset, 5u);

  const auto multiplier = scalar_row("REGN\\LodDisplayDistanceMultiplier");
  ASSERT_TRUE(multiplier.has_value());
  EXPECT_EQ(multiplier->scalar.storage, f32);

  const auto dist = scalar_row("REGN\\OcclusionAccuracyDist");
  ASSERT_TRUE(dist.has_value());
  EXPECT_EQ(dist->scalar.subrecord, fo4::Signature::from_string("ANAM"));

  const auto falloff = scalar_row("REGN\\RegionAreaEdgeFalloff");
  ASSERT_TRUE(falloff.has_value());
  EXPECT_EQ(falloff->scalar.subrecord, fo4::Signature::from_string("RPLI"));
  EXPECT_EQ(falloff->scalar.storage, u32);
  EXPECT_FALSE(falloff->scalar.embedded);
  EXPECT_TRUE(falloff->scalar.address.repeated);

  const auto worldspace = fo4::schema::formlink_field_descriptor(
      fo4::Signature::from_string("REGN"), "REGN\\Worldspace");
  ASSERT_TRUE(worldspace.has_value());
  EXPECT_EQ(worldspace->subrecord, fo4::Signature::from_string("WNAM"));
  EXPECT_FALSE(worldspace->nullable);
  EXPECT_TRUE(fo4::schema::formlink_field_target_allowed(
      *worldspace, fo4::Signature::from_string("WRLD")));
  EXPECT_FALSE(fo4::schema::formlink_field_target_allowed(
      *worldspace, fo4::Signature::from_string("REGN")));

  const auto music = fo4::schema::formlink_field_descriptor(
      fo4::Signature::from_string("REGN"), "REGN\\Music");
  ASSERT_TRUE(music.has_value());
  EXPECT_EQ(music->subrecord, fo4::Signature::from_string("RDMO"));
  EXPECT_TRUE(fo4::schema::formlink_field_target_allowed(
      *music, fo4::Signature::from_string("MUSC")));
  EXPECT_FALSE(fo4::schema::formlink_field_target_allowed(
      *music, fo4::Signature::from_string("WRLD")));
}

TEST(RegnRows, ReadsColorsDataEntriesAndLinks) {
  const auto plugin = fo4::read_plugin(make_plugin(), "Patch.esp");
  const auto key = "000D10:Patch.esp";

  EXPECT_EQ(field_bytes(plugin, key, "REGN\\MapColorRed"), u8_bytes(0x11));
  EXPECT_EQ(field_bytes(plugin, key, "REGN\\MapColorGreen"), u8_bytes(0x22));
  EXPECT_EQ(field_bytes(plugin, key, "REGN\\MapColorBlue"), u8_bytes(0x33));
  EXPECT_EQ(field_bytes(plugin, key, "REGN\\RegionDataType"), u32_bytes(3));
  EXPECT_EQ(field_bytes(plugin, key, "REGN\\RegionDataType", 1), u32_bytes(5));
  EXPECT_EQ(field_bytes(plugin, key, "REGN\\RegionDataOverride"), u8_bytes(1));
  EXPECT_EQ(field_bytes(plugin, key, "REGN\\RegionDataPriority"), u8_bytes(40));
  EXPECT_EQ(field_bytes(plugin, key, "REGN\\RegionDataPriority", 1), u8_bytes(60));
  EXPECT_EQ(field_bytes(plugin, key, "REGN\\OcclusionAccuracyDist"), f32_bytes(0.5f));
  EXPECT_EQ(field_bytes(plugin, key, "REGN\\LodDisplayDistanceMultiplier"), f32_bytes(2.5f));
  EXPECT_EQ(field_bytes(plugin, key, "REGN\\RegionAreaEdgeFalloff"), u32_bytes(80));
  EXPECT_EQ(field_bytes(plugin, key, "REGN\\RegionAreaEdgeFalloff", 1), u32_bytes(120));

  EXPECT_EQ(link_raw(plugin, key, "REGN\\Worldspace"), 0x00000D20u);
  EXPECT_EQ(link_raw(plugin, key, "REGN\\Music"), 0x00000D21u);
}

TEST(RegnRows, CountsRepeatedDataEntries) {
  const auto plugin = fo4::read_plugin(make_plugin(), "Patch.esp");
  const auto key = fo4::FormKey::parse("000D10:Patch.esp");
  const auto type = scalar_row("REGN\\RegionDataType");
  const auto color = scalar_row("REGN\\MapColorRed");
  const auto worldspace = fo4::schema::formlink_field_descriptor(
      fo4::Signature::from_string("REGN"), "REGN\\Worldspace");
  ASSERT_TRUE(type.has_value());
  ASSERT_TRUE(color.has_value());
  ASSERT_TRUE(worldspace.has_value());

  const auto entries = fo4::record_field_count(plugin, key, *type);
  ASSERT_TRUE(entries.has_value());
  EXPECT_EQ(*entries, 2u);
  const auto colors = fo4::record_field_count(plugin, key, *color);
  ASSERT_TRUE(colors.has_value());
  EXPECT_EQ(*colors, 1u);
  const auto links = fo4::record_field_count(plugin, key, *worldspace);
  ASSERT_TRUE(links.has_value());
  EXPECT_EQ(*links, 1u);
  const auto falloff = scalar_row("REGN\\RegionAreaEdgeFalloff");
  ASSERT_TRUE(falloff.has_value());
  const auto areas = fo4::record_field_count(plugin, key, *falloff);
  ASSERT_TRUE(areas.has_value());
  EXPECT_EQ(*areas, 2u);
}

TEST(RegnRows, WritesColorsDataEntryFieldsAndLinksThenRereads) {
  auto plugin = fo4::read_plugin(make_plugin(), "Patch.esp");
  const auto key = fo4::FormKey::parse("000D10:Patch.esp");
  const auto blue = scalar_row("REGN\\MapColorBlue");
  const auto priority = scalar_row("REGN\\RegionDataPriority");
  const auto dist = scalar_row("REGN\\OcclusionAccuracyDist");
  const auto falloff = scalar_row("REGN\\RegionAreaEdgeFalloff");
  const auto worldspace = fo4::schema::formlink_field_descriptor(
      fo4::Signature::from_string("REGN"), "REGN\\Worldspace");
  ASSERT_TRUE(blue.has_value());
  ASSERT_TRUE(priority.has_value());
  ASSERT_TRUE(dist.has_value());
  ASSERT_TRUE(falloff.has_value());
  ASSERT_TRUE(worldspace.has_value());

  EXPECT_EQ(fo4::set_record_scalar(plugin, key, *blue, "200"),
            fo4::ScalarMutationStatus::updated);
  EXPECT_EQ(fo4::set_record_scalar(plugin, key, *priority, 1, "90"),
            fo4::ScalarMutationStatus::updated);
  EXPECT_EQ(fo4::set_record_scalar(plugin, key, *dist, "1.5"),
            fo4::ScalarMutationStatus::updated);
  EXPECT_EQ(fo4::set_record_scalar(plugin, key, *falloff, 1, "121"),
            fo4::ScalarMutationStatus::updated);
  EXPECT_EQ(fo4::set_record_scalar(plugin, key, *falloff, 2, "9"),
            fo4::ScalarMutationStatus::subrecord_missing);
  EXPECT_EQ(fo4::set_record_scalar(plugin, key, *priority, 4, "10"),
            fo4::ScalarMutationStatus::subrecord_missing);
  EXPECT_EQ(fo4::set_record_scalar(plugin, key, *blue, "200"),
            fo4::ScalarMutationStatus::unchanged);
  EXPECT_EQ(fo4::set_record_formlink_field(plugin, key, *worldspace,
                                           fo4::FormKey::parse("000D30:Patch.esp")),
            fo4::FormLinkFieldMutationStatus::updated);

  const auto reread = fo4::read_plugin(fo4::write_plugin(plugin), "Patch.esp");
  const auto reread_key = "000D10:Patch.esp";
  EXPECT_EQ(sub_bytes(reread, reread_key, "RCLR"),
            (Bytes{std::byte{0x11}, std::byte{0x22}, std::byte{200}, std::byte{0x00}}));
  EXPECT_EQ(field_bytes(reread, reread_key, "REGN\\RegionDataPriority"), u8_bytes(40));
  EXPECT_EQ(field_bytes(reread, reread_key, "REGN\\RegionDataPriority", 1), u8_bytes(90));
  EXPECT_EQ(sub_bytes(reread, reread_key, "ANAM"), f32_bytes(1.5f));
  EXPECT_EQ(sub_bytes(reread, reread_key, "RLDM"), f32_bytes(2.5f));
  EXPECT_EQ(sub_bytes(reread, reread_key, "RDAT", 1), rdat_bytes(5, 0, 90));
  EXPECT_EQ(sub_bytes(reread, reread_key, "RPLI"), u32_bytes(80));
  EXPECT_EQ(sub_bytes(reread, reread_key, "RPLI", 1), u32_bytes(121));
  EXPECT_EQ(link_raw(reread, reread_key, "REGN\\Worldspace"), 0x02000D30u);
  EXPECT_EQ(link_raw(reread, reread_key, "REGN\\Music"), 0x00000D21u);
}

TEST(RegnRows, MalformedPayloadsHideOrRefuseRows) {
  ByteWriter out;
  write_header(out);
  write_grouped_record(out, "REGN", 0x0D40,
                       {{"RCLR", Bytes{std::byte{0x11}, std::byte{0x22}}},
                        {"RDAT", u32_bytes(3)},
                        {"ANAM", Bytes{std::byte{0x00}, std::byte{0x00}}}});
  auto plugin = fo4::read_plugin(out.bytes, "Patch.esp");
  const auto key = "000D40:Patch.esp";

  EXPECT_EQ(field_bytes(plugin, key, "REGN\\MapColorRed"), u8_bytes(0x11));
  EXPECT_TRUE(field_bytes(plugin, key, "REGN\\MapColorBlue").empty());
  EXPECT_EQ(field_bytes(plugin, key, "REGN\\RegionDataType"), u32_bytes(3));
  EXPECT_TRUE(field_bytes(plugin, key, "REGN\\RegionDataPriority").empty());
  EXPECT_THROW(static_cast<void>(field_bytes(plugin, key, "REGN\\OcclusionAccuracyDist")),
               fo4::schema::ScalarError);

  const auto priority = scalar_row("REGN\\RegionDataPriority");
  const auto dist = scalar_row("REGN\\OcclusionAccuracyDist");
  ASSERT_TRUE(priority.has_value());
  ASSERT_TRUE(dist.has_value());
  EXPECT_EQ(fo4::set_record_scalar(plugin, fo4::FormKey::parse(key), *priority, "5"),
            fo4::ScalarMutationStatus::subrecord_missing);
  EXPECT_THROW(fo4::set_record_scalar(plugin, fo4::FormKey::parse(key), *dist, "1.0"),
               fo4::schema::ScalarError);
  EXPECT_EQ(sub_bytes(plugin, key, "ANAM"), (Bytes{std::byte{0x00}, std::byte{0x00}}));

  ByteWriter short_area;
  write_header(short_area);
  write_grouped_record(short_area, "REGN", 0x0D41,
                       {{"RPLI", Bytes{std::byte{0x50}, std::byte{0x00}}}});
  auto short_plugin = fo4::read_plugin(short_area.bytes, "Patch.esp");
  const auto short_key = "000D41:Patch.esp";
  const auto falloff = scalar_row("REGN\\RegionAreaEdgeFalloff");
  ASSERT_TRUE(falloff.has_value());
  EXPECT_THROW(static_cast<void>(field_bytes(short_plugin, short_key, "REGN\\RegionAreaEdgeFalloff")),
               fo4::schema::ScalarError);
  EXPECT_THROW(
      fo4::set_record_scalar(short_plugin, fo4::FormKey::parse(short_key), *falloff, "3"),
      fo4::schema::ScalarError);
  EXPECT_EQ(sub_bytes(short_plugin, short_key, "RPLI"), (Bytes{std::byte{0x50}, std::byte{0x00}}));
}

TEST(RegnRows, CreatesARegionInAuthoringOrder) {
  auto plugin = fo4::read_plugin(make_plugin(), "Patch.esp");
  const auto created =
      fo4::create_record(plugin, fo4::Signature::from_string("REGN"), "RadicleNewRegion");
  ASSERT_EQ(created.status, fo4::RecordCreateStatus::created);
  const auto* record = fo4::find_record(plugin, created.key);
  ASSERT_NE(record, nullptr);
  ASSERT_EQ(record->subrecords.size(), 2u);
  EXPECT_EQ(record->subrecords[0].signature, fo4::Signature::from_string("EDID"));
  EXPECT_EQ(record->subrecords[1].signature, fo4::Signature::from_string("RCLR"));
  EXPECT_EQ(record->subrecords[1].data,
            (Bytes{std::byte{0}, std::byte{0}, std::byte{0}, std::byte{0}}));
  EXPECT_TRUE(fo4::schema::preflight_authoring_record(plugin, created.key).empty());

  const auto stored = created.key.to_string();
  EXPECT_EQ(field_bytes(plugin, stored, "REGN\\MapColorRed"), u8_bytes(0));
  EXPECT_EQ(fo4::add_record_subrecord(plugin, created.key, fo4::Signature::from_string("ANAM")),
            fo4::SubrecordAddStatus::not_authorable);
  EXPECT_EQ(
      fo4::remove_record_subrecord(plugin, created.key, fo4::Signature::from_string("RCLR"), 0),
      fo4::SubrecordRemoveStatus::below_minimum);

  const auto reread = fo4::read_plugin(fo4::write_plugin(plugin), "Patch.esp");
  ASSERT_NE(fo4::find_record(reread, created.key), nullptr);
  EXPECT_TRUE(fo4::schema::preflight_authoring_record(reread, created.key).empty());
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

TEST(RegnRows, DecodesTypedRegionAreasEntriesAndPayloads) {
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

TEST(RegnRows, RejectsMalformedTypedRegionPayloads) {
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

TEST(RegnRows, EntryArraysAndEntryPayloadsEditInPlace) {
  const auto sound_flags = scalar_row("REGN\\SoundFlags");
  const auto sound_chance = scalar_row("REGN\\SoundChance");
  const auto weather_chance = scalar_row("REGN\\WeatherChance");
  ASSERT_TRUE(sound_flags.has_value());
  ASSERT_TRUE(sound_chance.has_value());
  ASSERT_TRUE(weather_chance.has_value());
  EXPECT_EQ(sound_flags->scalar.address.stride, 12u);
  EXPECT_EQ(sound_flags->scalar.offset, 4u);
  EXPECT_EQ(sound_flags->scalar.presentation, fo4::schema::ScalarPresentation::flags);
  EXPECT_EQ(sound_flags->scalar.names.size(), 4u);
  ASSERT_TRUE(fo4::schema::scalar_value_name(sound_flags->scalar, 4).has_value());
  EXPECT_EQ(*fo4::schema::scalar_value_name(sound_flags->scalar, 4), "Rainy");
  EXPECT_FALSE(fo4::schema::scalar_value_name(sound_flags->scalar, 16).has_value());
  EXPECT_EQ(sound_chance->scalar.offset, 8u);
  EXPECT_EQ(weather_chance->scalar.subrecord, fo4::Signature::from_string("RDWT"));
  EXPECT_EQ(weather_chance->scalar.storage, i32);

  const auto object = fo4::schema::formlink_field_descriptor(
      fo4::Signature::from_string("REGN"), "REGN\\Object");
  const auto grass = fo4::schema::formlink_field_descriptor(
      fo4::Signature::from_string("REGN"), "REGN\\Grass");
  const auto weather = fo4::schema::formlink_field_descriptor(
      fo4::Signature::from_string("REGN"), "REGN\\Weather");
  const auto weather_global = fo4::schema::formlink_field_descriptor(
      fo4::Signature::from_string("REGN"), "REGN\\WeatherGlobal");
  const auto sound = fo4::schema::formlink_field_descriptor(
      fo4::Signature::from_string("REGN"), "REGN\\Sound");
  ASSERT_TRUE(object.has_value());
  ASSERT_TRUE(grass.has_value());
  ASSERT_TRUE(weather.has_value());
  ASSERT_TRUE(weather_global.has_value());
  ASSERT_TRUE(sound.has_value());
  EXPECT_EQ(object->address.stride, 52u);
  EXPECT_EQ(grass->address.stride, 8u);
  EXPECT_EQ(weather->address.stride, 12u);
  EXPECT_EQ(weather_global->offset, 8u);
  EXPECT_TRUE(weather_global->nullable);
  EXPECT_FALSE(weather->nullable);
  EXPECT_TRUE(sound->nullable);
  EXPECT_TRUE(fo4::schema::formlink_field_target_allowed(
      *object, fo4::Signature::from_string("SCOL")));
  EXPECT_FALSE(fo4::schema::formlink_field_target_allowed(
      *object, fo4::Signature::from_string("REGN")));
  EXPECT_TRUE(fo4::schema::formlink_field_target_allowed(
      *sound, fo4::Signature::from_string("SOUN")));

  ByteWriter out;
  write_header(out);
  write_grouped_record(out, "REGN", 0x0F10,
                       {{"EDID", text_bytes("EntryRegion")},
                        {"RCLR", Bytes{std::byte{0x01}, std::byte{0x02}, std::byte{0x03},
                                       std::byte{0x00}}},
                        {"ICON", text_bytes("Icons\\Region.dds")},
                        {"RPLI", u32_bytes(64)},
                        {"RPLD", f32_list({1.5f, -2.5f, 3.5f, 4.5f})},
                        {"RDAT", rdat_bytes(2, 0, 10)},
                        {"RDOT", rdot_bytes(0x00000F40, 3, 0.25f)},
                        {"RDGS", rdgs_bytes(0x00000F41)},
                        {"RDWT", rdwt_bytes(0x00000F42, 100, 0x00000F43)},
                        {"RDSA", rdsa_bytes(0x00000F44, 4, 0.5f)}});
  write_grouped_record(out, "GLOB", 0x0F45, {{"EDID", text_bytes("TestGlobal")}});
  auto plugin = fo4::read_plugin(out.bytes, "Patch.esp");
  const auto key = "000F10:Patch.esp";

  EXPECT_EQ(field_bytes(plugin, key, "REGN\\Icon"), text_bytes("Icons\\Region.dds"));
  EXPECT_EQ(link_raw(plugin, key, "REGN\\Object"), 0x00000F40u);
  EXPECT_EQ(link_raw(plugin, key, "REGN\\Grass"), 0x00000F41u);
  EXPECT_EQ(link_raw(plugin, key, "REGN\\Weather"), 0x00000F42u);
  EXPECT_EQ(sub_bytes(plugin, key, "RDWT"), rdwt_bytes(0x00000F42, 100, 0x00000F43));
  EXPECT_EQ(link_raw(plugin, key, "REGN\\Sound"), 0x00000F44u);
  EXPECT_EQ(fo4::record_field_count(plugin, key, "REGN\\Grass"), 1u);
  EXPECT_EQ(fo4::record_field_count(plugin, key, "REGN\\SoundChance"), 1u);

  EXPECT_EQ(fo4::set_record_scalar(plugin, fo4::FormKey::parse(key), *sound_flags, "16"),
            fo4::ScalarMutationStatus::updated);
  EXPECT_EQ(fo4::set_record_scalar(plugin, fo4::FormKey::parse(key), *weather_chance, "250"),
            fo4::ScalarMutationStatus::updated);
  EXPECT_EQ(fo4::set_record_formlink_field(plugin, fo4::FormKey::parse(key), *weather_global,
                                           fo4::FormKey::parse("00000F45:Patch.esp")),
            fo4::FormLinkFieldMutationStatus::updated);

  const auto reread = fo4::read_plugin(fo4::write_plugin(plugin), "Patch.esp");
  EXPECT_EQ(sub_bytes(reread, key, "RPLD"), f32_list({1.5f, -2.5f, 3.5f, 4.5f}));
  EXPECT_EQ(sub_bytes(reread, key, "RDSA"), rdsa_bytes(0x00000F44, 16, 0.5f));
  EXPECT_EQ(sub_bytes(reread, key, "RDWT"), rdwt_bytes(0x00000F42, 250, 0x02000F45));
  EXPECT_EQ(sub_bytes(reread, key, "RDOT"), rdot_bytes(0x00000F40, 3, 0.25f));
}

TEST(RegnRows, RefusesEntryArrayRowsOnAMisalignedPayload) {
  ByteWriter out;
  write_header(out);
  write_grouped_record(out, "REGN", 0x0F20,
                       {{"RDWT", Bytes(13)},
                        {"RDSA", Bytes(11)}});
  auto plugin = fo4::read_plugin(out.bytes, "Patch.esp");
  const auto key = fo4::FormKey::parse("000F20:Patch.esp");
  const auto weather_chance = scalar_row("REGN\\WeatherChance");
  const auto weather = fo4::schema::formlink_field_descriptor(
      fo4::Signature::from_string("REGN"), "REGN\\Weather");
  ASSERT_TRUE(weather_chance.has_value());
  ASSERT_TRUE(weather.has_value());

  EXPECT_EQ(fo4::record_field_count(plugin, key, "REGN\\WeatherChance"), 0u);
  EXPECT_EQ(fo4::set_record_scalar(plugin, key, *weather_chance, "5"),
            fo4::ScalarMutationStatus::subrecord_missing);
  EXPECT_EQ(fo4::set_record_formlink_field(plugin, key, *weather,
                                           fo4::FormKey::parse("00000F46:Fallout4.esm")),
            fo4::FormLinkFieldMutationStatus::subrecord_missing);
  EXPECT_EQ(sub_bytes(plugin, "000F20:Patch.esp", "RDWT"), Bytes(13));
}

}  // namespace
