#include "fo4/plugin/mutation.hpp"
#include "fo4/plugin/reader.hpp"
#include "fo4/plugin/subrecord_authoring.hpp"
#include "fo4/plugin/writer.hpp"
#include "fo4/schema/formlink_lists.hpp"
#include "fo4/schema/preflight.hpp"
#include "fo4/schema/scalar.hpp"
#include "env_node_fixture.hpp"
#include "plugin_bytes.hpp"

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <cstring>
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
using fo4::test::envnode::rec;
using fo4::test::envnode::top;

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

Bytes rdgs_bytes(std::uint32_t object) {
  ByteWriter out;
  out.u32(object);
  out.u32(0);
  return out.bytes;
}

Bytes rdsa_bytes(std::uint32_t sound, std::uint32_t flags, float chance) {
  ByteWriter out;
  out.u32(sound);
  out.u32(flags);
  out.f32(chance);
  return out.bytes;
}

Bytes concat(Bytes first, const Bytes& second) {
  first.insert(first.end(), second.begin(), second.end());
  return first;
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
                        {"RDAT", rdat_bytes(7, 1, 40)},
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

  const auto override_value = scalar_row("REGN\\RegionDataOverride");
  ASSERT_TRUE(override_value.has_value());
  EXPECT_EQ(override_value->scalar.presentation, fo4::schema::ScalarPresentation::enumeration);
  EXPECT_EQ(fo4::schema::scalar_value_name(override_value->scalar, 1), std::string_view("True"));
  EXPECT_FALSE(fo4::schema::scalar_value_name(override_value->scalar, 2).has_value());

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
  auto plugin = fo4::read_plugin(make_plugin(), "Patch.esp");
  const auto key = "000D10:Patch.esp";

  EXPECT_EQ(field_bytes(plugin, key, "REGN\\MapColorRed"), u8_bytes(0x11));
  EXPECT_EQ(field_bytes(plugin, key, "REGN\\MapColorGreen"), u8_bytes(0x22));
  EXPECT_EQ(field_bytes(plugin, key, "REGN\\MapColorBlue"), u8_bytes(0x33));
  EXPECT_EQ(field_bytes(plugin, key, "REGN\\RegionDataType"), u32_bytes(7));
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
  auto plugin = fo4::read_plugin(make_plugin(), "Patch.esp");
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
  EXPECT_EQ(weather_chance->scalar.storage, u32);

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
                        {"RDWT", concat(rdwt_bytes(0x00000F42, 100, 0x00000F43),
                                        rdwt_bytes(0x00000F4A, 50, 0x00000F4B))},
                        {"RDSA", rdsa_bytes(0x00000F44, 4, 0.5f)},
                        {"RDAT", rdat_bytes(2, 1, 20)},
                        {"RDOT", rdot_bytes(0x00000F47, 5, 0.75f)},
                        {"RDWT", rdwt_bytes(0x00000F48, 200, 0x00000F49)}});
  write_grouped_record(out, "GLOB", 0x0F45, {{"EDID", text_bytes("TestGlobal")}});
  auto plugin = fo4::read_plugin(out.bytes, "Patch.esp");
  const auto key = "000F10:Patch.esp";
  const auto record_key = fo4::FormKey::parse(key);

  EXPECT_EQ(field_bytes(plugin, key, "REGN\\Icon"), text_bytes("Icons\\Region.dds"));
  EXPECT_EQ(link_raw(plugin, key, "REGN\\Object"), 0x00000F40u);
  EXPECT_EQ(link_raw(plugin, key, "REGN\\Object", 1), 0x00000F47u);
  EXPECT_EQ(link_raw(plugin, key, "REGN\\Grass"), 0x00000F41u);
  EXPECT_EQ(link_raw(plugin, key, "REGN\\Weather"), 0x00000F42u);
  EXPECT_EQ(sub_bytes(plugin, key, "RDWT"),
            concat(rdwt_bytes(0x00000F42, 100, 0x00000F43),
                   rdwt_bytes(0x00000F4A, 50, 0x00000F4B)));
  EXPECT_EQ(sub_bytes(plugin, key, "RDWT", 1), rdwt_bytes(0x00000F48, 200, 0x00000F49));
  EXPECT_EQ(link_raw(plugin, key, "REGN\\Sound"), 0x00000F44u);
  EXPECT_EQ(fo4::record_field_count(plugin, record_key, "REGN\\Grass"), 1u);
  EXPECT_EQ(fo4::record_field_count(plugin, record_key, "REGN\\SoundChance"), 1u);
  EXPECT_EQ(fo4::record_field_count(plugin, record_key, "REGN\\Object"), 2u);
  EXPECT_EQ(fo4::record_field_count(plugin, record_key, "REGN\\WeatherChance"), 3u);
  EXPECT_EQ(fo4::record_field_count(plugin, record_key, "REGN\\Weather"), 3u);
  EXPECT_EQ(fo4::record_field_count(plugin, record_key, "REGN\\WeatherGlobal"), 3u);
  for (const auto& issue : fo4::schema::preflight_plugin(plugin)) {
    EXPECT_NE(issue.code, "duplicate_scalar_subrecord") << issue.path;
  }

  EXPECT_EQ(fo4::set_record_scalar(plugin, record_key, *sound_flags, "16"),
            fo4::ScalarMutationStatus::updated);
  EXPECT_THROW(fo4::set_record_scalar(plugin, record_key, *weather_chance, "-1"),
               fo4::schema::ScalarError);
  EXPECT_EQ(fo4::set_record_scalar(plugin, record_key, *weather_chance, "4000000000"),
            fo4::ScalarMutationStatus::updated);
  EXPECT_EQ(fo4::set_record_formlink_field(plugin, record_key, *weather_global,
                                           fo4::FormKey::parse("00000F45:Patch.esp")),
            fo4::FormLinkFieldMutationStatus::updated);
  EXPECT_EQ(fo4::set_record_scalar(plugin, record_key, *weather_chance, 1, "250"),
            fo4::ScalarMutationStatus::updated);
  EXPECT_EQ(fo4::set_record_scalar(plugin, record_key, *weather_chance, 2, "250"),
            fo4::ScalarMutationStatus::updated);
  EXPECT_EQ(fo4::set_record_scalar(plugin, record_key, *weather_chance, 3, "250"),
            fo4::ScalarMutationStatus::subrecord_missing);

  const auto reread = fo4::read_plugin(fo4::write_plugin(plugin), "Patch.esp");
  EXPECT_EQ(sub_bytes(reread, key, "RPLD"), f32_list({1.5f, -2.5f, 3.5f, 4.5f}));
  EXPECT_EQ(sub_bytes(reread, key, "RDSA"), rdsa_bytes(0x00000F44, 16, 0.5f));
  EXPECT_EQ(sub_bytes(reread, key, "RDWT"),
            concat(rdwt_bytes(0x00000F42, 4000000000u, 0x02000F45),
                   rdwt_bytes(0x00000F4A, 250, 0x00000F4B)));
  EXPECT_EQ(sub_bytes(reread, key, "RDOT"), rdot_bytes(0x00000F40, 3, 0.25f));
  EXPECT_EQ(sub_bytes(reread, key, "RDWT", 1), rdwt_bytes(0x00000F48, 250, 0x00000F49));
}

TEST(RegnRows, RefusesEntryArrayRowsOnAMisalignedPayload) {
  ByteWriter out;
  write_header(out);
  write_grouped_record(out, "REGN", 0x0F20,
                       {{"RDAT", rdat_bytes(3, 0, 0)},
                        {"RDWT", Bytes(13)},
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

TEST(RegnRows, RefusesTwoPayloadsInsideOneEntry) {
  const auto bytes = fo4::test::envnode::make_plugin(
      {}, {top("REGN", {rec("REGN", 0x0F51, {{"RDAT", bytes_of({3, 0})},
                                              {"RDWT", Bytes(12)},
                                              {"RDWT", Bytes(12)}})})});
  auto plugin = fo4::read_plugin(bytes, "Patch.esp");
  const auto key = fo4::FormKey::parse("000F51:Patch.esp");
  const auto weather = fo4::schema::formlink_field_descriptor(
      fo4::Signature::from_string("REGN"), "REGN\\Weather");
  const auto chance = scalar_row("REGN\\WeatherChance");
  ASSERT_TRUE(weather.has_value() && chance.has_value());
  std::vector<std::string> duplicates;
  for (const auto& issue : fo4::schema::preflight_plugin(plugin)) {
    if (issue.code == "duplicate_scalar_subrecord") duplicates.push_back(issue.path);
  }
  ASSERT_EQ(duplicates.size(), 1u) << (duplicates.empty() ? std::string{} : duplicates[0]);
  EXPECT_THROW(fo4::set_record_scalar(plugin, key, *chance, 0, "5"),
               fo4::schema::ScalarError);
  EXPECT_THROW(fo4::set_record_formlink_field(plugin, key, *weather, 0,
                                              fo4::FormKey::parse("00000F49:Fallout4.esm")),
               fo4::schema::FormLinkFieldError);
}

TEST(RegnRows, PayloadsWithoutAnyDataHeaderStillFailClosed) {
  const auto bytes = fo4::test::envnode::make_plugin(
      {}, {top("REGN", {rec("REGN", 0x0F71, {{"RDWT", Bytes(12)},
                                              {"RDWT", Bytes(12)}})})});
  auto plugin = fo4::read_plugin(bytes, "Patch.esp");
  const auto key = fo4::FormKey::parse("000F71:Patch.esp");
  const auto chance = scalar_row("REGN\\WeatherChance");
  const auto weather = fo4::schema::formlink_field_descriptor(
      fo4::Signature::from_string("REGN"), "REGN\\Weather");
  ASSERT_TRUE(chance.has_value() && weather.has_value());
  std::vector<std::string> duplicates;
  for (const auto& issue : fo4::schema::preflight_plugin(plugin)) {
    if (issue.code == "duplicate_scalar_subrecord") duplicates.push_back(issue.path);
  }
  ASSERT_EQ(duplicates.size(), 1u) << (duplicates.empty() ? std::string{} : duplicates[0]);
  EXPECT_EQ(duplicates[0], "REGN\\WeatherChance");
  EXPECT_THROW(fo4::record_field_count(plugin, key, "REGN\\WeatherChance"),
               fo4::schema::ScalarError);
  EXPECT_THROW(fo4::set_record_scalar(plugin, key, *chance, 0, "5"),
               fo4::schema::ScalarError);
  EXPECT_THROW(fo4::set_record_formlink_field(plugin, key, *weather, 0,
                                              fo4::FormKey::parse("00000F49:Fallout4.esm")),
               fo4::schema::FormLinkFieldError);
}

TEST(RegnRows, PayloadBeforeTheFirstDataHeaderGetsNoSlot) {
  const auto bytes = fo4::test::envnode::make_plugin(
      {}, {top("REGN", {rec("REGN", 0x0F61, {{"RDWT", Bytes(12)},
                                              {"RDAT", bytes_of({3, 0})},
                                              {"RDWT", Bytes(12)}})})});
  auto plugin = fo4::read_plugin(bytes, "Patch.esp");
  const auto key = fo4::FormKey::parse("000F61:Patch.esp");
  const auto chance = scalar_row("REGN\\WeatherChance");
  const auto weather = fo4::schema::formlink_field_descriptor(
      fo4::Signature::from_string("REGN"), "REGN\\Weather");
  ASSERT_TRUE(chance.has_value());
  ASSERT_TRUE(weather.has_value());

  EXPECT_EQ(fo4::record_field_count(plugin, key, "REGN\\WeatherChance"), 1u);
  EXPECT_EQ(fo4::record_field_count(plugin, key, "REGN\\Weather"), 1u);
  EXPECT_EQ(fo4::set_record_scalar(plugin, key, *chance, 1, "5"),
            fo4::ScalarMutationStatus::subrecord_missing);
  EXPECT_EQ(fo4::set_record_formlink_field(plugin, key, *weather, 1,
                                           fo4::FormKey::parse("00000F49:Fallout4.esm")),
            fo4::FormLinkFieldMutationStatus::subrecord_missing);
  EXPECT_EQ(fo4::set_record_scalar(plugin, key, *chance, 0, "5"),
            fo4::ScalarMutationStatus::updated);
  EXPECT_EQ(sub_bytes(plugin, "000F61:Patch.esp", "RDWT"), Bytes(12));
  ByteWriter expected;
  expected.u32(0);
  expected.u32(5);
  expected.u32(0);
  EXPECT_EQ(sub_bytes(plugin, "000F61:Patch.esp", "RDWT", 1), expected.bytes);
}

}  // namespace
