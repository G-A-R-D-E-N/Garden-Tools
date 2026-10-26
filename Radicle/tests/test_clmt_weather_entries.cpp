#include "fo4/plugin/mutation.hpp"
#include "fo4/plugin/reader.hpp"
#include "fo4/plugin/subrecord_authoring.hpp"
#include "fo4/plugin/writer.hpp"
#include "fo4/schema/scalar.hpp"
#include "plugin_bytes.hpp"

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace {

using Bytes = std::vector<std::byte>;

Bytes concat(const Bytes& left, const Bytes& right) {
  Bytes out = left;
  out.insert(out.end(), right.begin(), right.end());
  return out;
}

constexpr auto k_clmt = fo4::Signature::from_string("CLMT");
constexpr auto k_wlst = fo4::Signature::from_string("WLST");

fo4::FormKey climate_key() { return fo4::FormKey::parse("000800:Patch.esp"); }
fo4::FormKey weather_key() { return fo4::FormKey::parse("000801:Patch.esp"); }
fo4::FormKey global_key() { return fo4::FormKey::parse("000810:Patch.esp"); }

fo4::schema::CountedArrayDescriptor weather_types() {
  return fo4::schema::counted_array_descriptor(k_clmt, "CLMT\\WeatherTypes").value();
}

Bytes u32_bytes(std::uint32_t value) {
  fo4::test::ByteWriter out;
  out.u32(value);
  return out.bytes;
}

Bytes zstr(std::string_view text) {
  fo4::test::ByteWriter out;
  out.raw(text);
  out.u8(0);
  return out.bytes;
}

Bytes wlst_entry(std::uint32_t weather, std::int32_t chance, std::uint32_t global) {
  fo4::test::ByteWriter out;
  out.u32(weather);
  out.u32(static_cast<std::uint32_t>(chance));
  out.u32(global);
  return out.bytes;
}

Bytes timing() {
  fo4::test::ByteWriter out;
  out.u8(6);
  out.u8(18);
  out.u8(17);
  out.u8(22);
  out.u8(0);
  out.u8(3);
  return out.bytes;
}

std::vector<std::byte> make_plugin(const Bytes& wlst, bool include_wlst = true) {
  fo4::test::ByteWriter out;
  const auto tes4 = fo4::test::begin_record(out, "TES4", 0, 0);
  fo4::test::ByteWriter hedr;
  hedr.f32(1.0f);
  hedr.u32(0);
  hedr.u32(0x801);
  fo4::test::write_subrecord(out, "HEDR", hedr.bytes);
  fo4::test::end_record(out, tes4);

  const auto weathers = fo4::test::begin_group(out, "WTHR", 0);
  const auto weather = fo4::test::begin_record(out, "WTHR", 0, 0x00000801);
  fo4::test::write_subrecord(out, "EDID", zstr("RadicleWeather"));
  fo4::test::end_record(out, weather);
  fo4::test::end_group(out, weathers);

  const auto globals = fo4::test::begin_group(out, "GLOB", 0);
  const auto global = fo4::test::begin_record(out, "GLOB", 0, 0x00000810);
  fo4::test::write_subrecord(out, "EDID", zstr("RadicleGlobal"));
  fo4::test::end_record(out, global);
  fo4::test::end_group(out, globals);

  const auto group = fo4::test::begin_group(out, "CLMT", 0);
  const auto record = fo4::test::begin_record(out, "CLMT", 0, 0x00000800);
  fo4::test::write_subrecord(out, "EDID", zstr("RadicleClimate"));
  if (include_wlst) fo4::test::write_subrecord(out, "WLST", wlst);
  fo4::test::write_subrecord(out, "TNAM", timing());
  fo4::test::end_record(out, record);
  fo4::test::end_group(out, group);
  return out.bytes;
}

std::optional<Bytes> payload(const fo4::Plugin& plugin, fo4::Signature signature) {
  const auto* record = fo4::find_record(plugin, climate_key());
  if (record == nullptr) return std::nullopt;
  for (const auto& subrecord : record->subrecords) {
    if (subrecord.signature == signature) return subrecord.data;
  }
  return std::nullopt;
}

std::uint32_t read_u32(const Bytes& data, std::size_t offset) {
  std::uint32_t value = 0;
  for (std::size_t i = 0; i < 4; ++i) {
    value |= static_cast<std::uint32_t>(std::to_integer<std::uint8_t>(data[offset + i])) << (8 * i);
  }
  return value;
}

std::string read_row(const fo4::Plugin& plugin, std::string_view path, std::size_t index) {
  const auto field = fo4::schema::scalar_field_descriptor(path).value();
  const auto& record = *fo4::find_record(plugin, climate_key());
  const auto resolved = fo4::schema::resolve_field_slots(
      record, fo4::schema::scalar_field_shape(field.scalar), field.scalar.address);
  const auto slot = resolved.slots.at(index);
  return fo4::schema::decode_scalar(
      fo4::schema::scalar_slot_bytes(record.subrecords[slot.subrecord].data, slot, field.scalar),
      field.scalar);
}

}  // namespace

TEST(ClmtWeatherTypes, DescriptorIsAPayloadCountedTwelveByteArray) {
  const auto array = weather_types();
  EXPECT_EQ(array.record, k_clmt);
  EXPECT_EQ(array.subrecord, k_wlst);
  EXPECT_EQ(array.count_subrecord, fo4::Signature{});
  EXPECT_EQ(array.stride, 12u);
  EXPECT_FALSE(fo4::schema::is_counted_array_count_subrecord(k_clmt, k_wlst));
  EXPECT_EQ(fo4::record_field_count(fo4::read_plugin(
                                        make_plugin(wlst_entry(0x00000801u, 100, 0)), "Patch.esp"),
                                    climate_key(), "CLMT\\WeatherChance"),
            1u);
}

TEST(ClmtWeatherTypes, InsertAddsAZeroedEntryAndShiftsTheWeatherLanes) {
  const auto first = wlst_entry(0x00000801u, 75, 0x00000810u);
  const auto second = wlst_entry(0x00000801u, 25, 0);
  auto plugin = fo4::read_plugin(make_plugin(concat(first, second)), "Patch.esp");
  const auto before = *payload(plugin, k_wlst);
  ASSERT_EQ(before.size(), 24u);

  EXPECT_EQ(fo4::insert_record_counted_element(plugin, climate_key(), weather_types(), 1),
            fo4::CountedArrayMutationStatus::updated);
  const auto after = *payload(plugin, k_wlst);
  ASSERT_EQ(after.size(), 36u);
  EXPECT_EQ(Bytes(after.begin(), after.begin() + 12), first);
  EXPECT_EQ(Bytes(after.begin() + 12, after.begin() + 24), Bytes(12, std::byte{0}));
  EXPECT_EQ(Bytes(after.begin() + 24, after.end()), second);

  EXPECT_EQ(fo4::set_record_scalar(plugin, climate_key(),
                                   fo4::schema::scalar_field_descriptor("CLMT\\WeatherChance").value(), 1,
                                   "50"),
            fo4::ScalarMutationStatus::updated);
  EXPECT_EQ(fo4::set_record_formlink_field(
                plugin, climate_key(),
                fo4::schema::formlink_field_descriptor(k_clmt, "CLMT\\Weather").value(), 1, weather_key()),
            fo4::FormLinkFieldMutationStatus::updated);
  EXPECT_EQ(fo4::set_record_formlink_field(
                plugin, climate_key(),
                fo4::schema::formlink_field_descriptor(k_clmt, "CLMT\\WeatherGlobal").value(), 1,
                global_key()),
            fo4::FormLinkFieldMutationStatus::updated);

  const auto reread = fo4::read_plugin(fo4::write_plugin(plugin), "Patch.esp");
  const auto reread_payload = *payload(reread, k_wlst);
  ASSERT_EQ(reread_payload.size(), 36u);
  EXPECT_EQ(read_u32(reread_payload, 12), 0x00000801u);
  EXPECT_EQ(read_u32(reread_payload, 16), 50u);
  EXPECT_EQ(read_u32(reread_payload, 20), 0x00000810u);
  EXPECT_EQ(read_row(reread, "CLMT\\WeatherChance", 0), "75");
  EXPECT_EQ(read_row(reread, "CLMT\\WeatherChance", 2), "25");
}

TEST(ClmtWeatherTypes, RemoveDropsOneEntryAndRefusesToEmptyTheArray) {
  const auto first = wlst_entry(0x00000801u, 75, 0x00000810u);
  const auto second = wlst_entry(0x00000801u, 25, 0);
  auto plugin = fo4::read_plugin(make_plugin(concat(first, second)), "Patch.esp");

  EXPECT_EQ(fo4::remove_record_counted_element(plugin, climate_key(), weather_types(), 2),
            fo4::CountedArrayMutationStatus::index_out_of_range);
  EXPECT_EQ(fo4::remove_record_counted_element(plugin, climate_key(), weather_types(), 0),
            fo4::CountedArrayMutationStatus::updated);
  const auto after = *payload(plugin, k_wlst);
  ASSERT_EQ(after.size(), 12u);
  EXPECT_EQ(after, second);
  EXPECT_EQ(fo4::remove_record_counted_element(plugin, climate_key(), weather_types(), 0),
            fo4::CountedArrayMutationStatus::would_empty);

  auto bare = fo4::read_plugin(make_plugin({}, false), "Patch.esp");
  EXPECT_EQ(fo4::insert_record_counted_element(bare, climate_key(), weather_types(), 0),
            fo4::CountedArrayMutationStatus::subrecord_missing);
  EXPECT_EQ(fo4::remove_record_counted_element(bare, fo4::FormKey::parse("000899:Patch.esp"),
                                             weather_types(), 0),
            fo4::CountedArrayMutationStatus::record_missing);
}

TEST(ClmtWeatherTypes, CreatedClimateTakesWeatherEntriesFromProvenDefaults) {
  auto plugin = fo4::read_plugin(make_plugin({}, false), "Patch.esp");
  const auto created = fo4::create_record(plugin, k_clmt, "NewClimate");
  ASSERT_EQ(created.status, fo4::RecordCreateStatus::created);
  EXPECT_EQ(fo4::add_record_subrecord(plugin, created.key, k_wlst), fo4::SubrecordAddStatus::added);
  EXPECT_EQ(fo4::insert_record_counted_element(plugin, created.key, weather_types(), 0),
            fo4::CountedArrayMutationStatus::updated);
  EXPECT_EQ(fo4::insert_record_counted_element(plugin, created.key, weather_types(), 1),
            fo4::CountedArrayMutationStatus::updated);
  EXPECT_EQ(fo4::set_record_scalar(plugin, created.key,
                                   fo4::schema::scalar_field_descriptor("CLMT\\WeatherChance").value(), 0,
                                   "100"),
            fo4::ScalarMutationStatus::updated);

  const auto reread = fo4::read_plugin(fo4::write_plugin(plugin), "Patch.esp");
  const auto* record = fo4::find_record(reread, created.key);
  ASSERT_NE(record, nullptr);
  std::size_t wlst_payloads = 0;
  for (const auto& subrecord : record->subrecords) {
    if (subrecord.signature != k_wlst) continue;
    ++wlst_payloads;
    EXPECT_EQ(subrecord.data.size(), 24u);
  }
  EXPECT_EQ(wlst_payloads, 1u);
  EXPECT_EQ(fo4::record_field_count(reread, created.key, "CLMT\\WeatherChance"), 2u);
}
