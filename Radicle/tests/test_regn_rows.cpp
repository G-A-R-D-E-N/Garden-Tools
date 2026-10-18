#include "fo4/plugin/mutation.hpp"
#include "fo4/plugin/reader.hpp"
#include "fo4/plugin/writer.hpp"
#include "fo4/schema/formlink_lists.hpp"
#include "fo4/schema/scalar.hpp"
#include "plugin_bytes.hpp"

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <initializer_list>
#include <optional>
#include <string_view>
#include <utility>
#include <vector>

namespace {

using Bytes = std::vector<std::byte>;
using fo4::test::ByteWriter;

constexpr auto f32 = fo4::schema::ScalarStorage::f32;
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
  EXPECT_EQ(sub_bytes(reread, reread_key, "RDAT", 1), rdat_bytes(5, 0, 60));
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

}  // namespace
