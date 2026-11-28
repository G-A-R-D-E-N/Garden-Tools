#include "fo4/plugin/mutation.hpp"
#include "fo4/plugin/reader.hpp"
#include "fo4/plugin/writer.hpp"
#include "fo4/schema/scalar.hpp"
#include "plugin_bytes.hpp"

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <initializer_list>
#include <utility>
#include <vector>

namespace {

using Bytes = std::vector<std::byte>;
using fo4::test::ByteWriter;

constexpr auto k_clmt = fo4::Signature::from_string("CLMT");
constexpr auto k_acti = fo4::Signature::from_string("ACTI");
constexpr auto k_wlst = fo4::Signature::from_string("WLST");
constexpr auto k_prps = fo4::Signature::from_string("PRPS");

fo4::FormKey climate_key() { return fo4::FormKey::parse("000A01:Patch.esp"); }
fo4::FormKey activator_key() { return fo4::FormKey::parse("000A02:Patch.esp"); }

struct WeatherEntry {
  std::uint32_t weather;
  std::int32_t chance;
  std::uint32_t global;
};

Bytes weather_payload(std::initializer_list<WeatherEntry> entries) {
  ByteWriter out;
  for (const auto& [weather, chance, global] : entries) {
    out.u32(weather);
    out.i32(chance);
    out.u32(global);
  }
  return out.bytes;
}

Bytes property_payload(std::initializer_list<std::pair<std::uint32_t, float>> entries) {
  ByteWriter out;
  for (const auto& [actor_value, value] : entries) {
    out.u32(actor_value);
    out.f32(value);
  }
  return out.bytes;
}

Bytes make_plugin(const Bytes& weather, const Bytes& properties) {
  ByteWriter out;
  const auto tes4 = fo4::test::begin_record(out, "TES4", 0, 0);
  ByteWriter hedr;
  hedr.f32(1.0f);
  hedr.u32(2);
  hedr.u32(0x800);
  fo4::test::write_subrecord(out, "HEDR", hedr.bytes);
  fo4::test::end_record(out, tes4);

  const auto clmt_group = fo4::test::begin_group(out, "CLMT", 0);
  const auto clmt = fo4::test::begin_record(out, "CLMT", 0, 0x00000A01);
  fo4::test::write_subrecord(out, "WLST", weather);
  fo4::test::end_record(out, clmt);
  fo4::test::end_group(out, clmt_group);

  const auto acti_group = fo4::test::begin_group(out, "ACTI", 0);
  const auto acti = fo4::test::begin_record(out, "ACTI", 0, 0x00000A02);
  fo4::test::write_subrecord(out, "PRPS", properties);
  fo4::test::end_record(out, acti);
  fo4::test::end_group(out, acti_group);
  return out.bytes;
}

Bytes payload(const fo4::Plugin& plugin, const fo4::FormKey& key, fo4::Signature signature) {
  const auto* record = fo4::find_record(plugin, key);
  if (record == nullptr) return {};
  for (const auto& subrecord : record->subrecords) {
    if (subrecord.signature == signature) return subrecord.data;
  }
  return {};
}

std::uint32_t u32_at(const Bytes& data, std::size_t offset) {
  std::uint32_t value{};
  std::memcpy(&value, data.data() + offset, sizeof value);
  return value;
}

std::int32_t i32_at(const Bytes& data, std::size_t offset) {
  return static_cast<std::int32_t>(u32_at(data, offset));
}

float f32_at(const Bytes& data, std::size_t offset) {
  float value{};
  std::memcpy(&value, data.data() + offset, sizeof value);
  return value;
}

fo4::schema::CountedArrayDescriptor weather_types() {
  return fo4::schema::counted_array_descriptor(k_clmt, "CLMT\\WeatherTypes").value();
}

fo4::schema::CountedArrayDescriptor properties() {
  return fo4::schema::counted_array_descriptor(k_acti, "ACTI\\Properties").value();
}

fo4::Plugin reread(const fo4::Plugin& plugin) {
  return fo4::read_plugin(fo4::write_plugin(plugin), "Patch.esp");
}

}  // namespace

TEST(UncountedArray, DescriptorHasNoCountSubrecord) {
  const auto array = weather_types();
  EXPECT_EQ(array.record, k_clmt);
  EXPECT_EQ(array.subrecord, k_wlst);
  EXPECT_EQ(array.count_subrecord, fo4::Signature{});
  EXPECT_EQ(array.stride, 12u);
  EXPECT_FALSE(fo4::schema::is_counted_array_count_subrecord(k_clmt, fo4::Signature{}));
  EXPECT_FALSE(fo4::schema::is_counted_array_count_subrecord(k_clmt, k_wlst));
  EXPECT_EQ(properties().stride, 8u);
}

TEST(UncountedArray, InsertAtFrontZeroFillsAndKeepsTheOldEntry) {
  auto plugin = fo4::read_plugin(make_plugin(weather_payload({{0x00000A10u, 7, 0x00000A11u}}), {}), "Patch.esp");
  EXPECT_EQ(fo4::insert_record_counted_element(plugin, climate_key(), weather_types(), 0),
            fo4::CountedArrayMutationStatus::updated);

  const auto data = payload(reread(plugin), climate_key(), k_wlst);
  ASSERT_EQ(data.size(), 24u);
  EXPECT_EQ(u32_at(data, 0), 0u);
  EXPECT_EQ(i32_at(data, 4), 0);
  EXPECT_EQ(u32_at(data, 8), 0u);
  EXPECT_EQ(u32_at(data, 12), 0x00000A10u);
  EXPECT_EQ(i32_at(data, 16), 7);
  EXPECT_EQ(u32_at(data, 20), 0x00000A11u);
}

TEST(UncountedArray, InsertAtEndAppendsAndEditsTheNewMember) {
  auto plugin = fo4::read_plugin(make_plugin(weather_payload({{0x00000A10u, 7, 0x00000A11u}}), {}), "Patch.esp");
  EXPECT_EQ(fo4::insert_record_counted_element(plugin, climate_key(), weather_types(), 1),
            fo4::CountedArrayMutationStatus::updated);
  const auto chance = fo4::schema::scalar_field_descriptor("CLMT\\WeatherChance").value();
  EXPECT_EQ(fo4::set_record_scalar(plugin, climate_key(), chance, 1, "-3"),
            fo4::ScalarMutationStatus::updated);

  const auto data = payload(reread(plugin), climate_key(), k_wlst);
  ASSERT_EQ(data.size(), 24u);
  EXPECT_EQ(u32_at(data, 0), 0x00000A10u);
  EXPECT_EQ(i32_at(data, 4), 7);
  EXPECT_EQ(u32_at(data, 8), 0x00000A11u);
  EXPECT_EQ(u32_at(data, 12), 0u);
  EXPECT_EQ(i32_at(data, 16), -3);
  EXPECT_EQ(u32_at(data, 20), 0u);
}

TEST(UncountedArray, RemoveDropsTheElementAndRefusesToEmptyThePayload) {
  auto plugin = fo4::read_plugin(
      make_plugin(weather_payload({{0x00000A10u, 7, 0x00000A11u}, {0x00000A12u, 9, 0x00000A13u}}), {}),
      "Patch.esp");
  EXPECT_EQ(fo4::remove_record_counted_element(plugin, climate_key(), weather_types(), 0),
            fo4::CountedArrayMutationStatus::updated);

  const auto remaining = payload(reread(plugin), climate_key(), k_wlst);
  ASSERT_EQ(remaining.size(), 12u);
  EXPECT_EQ(u32_at(remaining, 0), 0x00000A12u);
  EXPECT_EQ(i32_at(remaining, 4), 9);
  EXPECT_EQ(u32_at(remaining, 8), 0x00000A13u);

  EXPECT_EQ(fo4::remove_record_counted_element(plugin, climate_key(), weather_types(), 0),
            fo4::CountedArrayMutationStatus::would_empty);
  const auto kept = payload(reread(plugin), climate_key(), k_wlst);
  ASSERT_EQ(kept.size(), 12u);
  EXPECT_EQ(u32_at(kept, 0), 0x00000A12u);
  EXPECT_EQ(u32_at(kept, 8), 0x00000A13u);
}

TEST(UncountedArray, InsertsAndEditsPropertiesOnAnActivator) {
  auto plugin = fo4::read_plugin(make_plugin({}, property_payload({{0x00000A20u, 1.5f}})), "Patch.esp");
  EXPECT_EQ(fo4::insert_record_counted_element(plugin, activator_key(), properties(), 0),
            fo4::CountedArrayMutationStatus::updated);
  const auto value = fo4::schema::scalar_field_descriptor("ACTI\\PropertyValue").value();
  EXPECT_EQ(fo4::set_record_scalar(plugin, activator_key(), value, 0, "2.5"),
            fo4::ScalarMutationStatus::updated);

  const auto data = payload(reread(plugin), activator_key(), k_prps);
  ASSERT_EQ(data.size(), 16u);
  EXPECT_EQ(u32_at(data, 0), 0u);
  EXPECT_EQ(f32_at(data, 4), 2.5f);
  EXPECT_EQ(u32_at(data, 8), 0x00000A20u);
  EXPECT_EQ(f32_at(data, 12), 1.5f);
}

TEST(UncountedArray, RefusesOutOfRangeIndexesWithoutMutation) {
  auto plugin = fo4::read_plugin(make_plugin(weather_payload({{0x00000A10u, 7, 0x00000A11u}}), {}), "Patch.esp");
  const auto original = fo4::test::original_bytes(plugin);
  EXPECT_EQ(fo4::insert_record_counted_element(plugin, climate_key(), weather_types(), 2),
            fo4::CountedArrayMutationStatus::index_out_of_range);
  EXPECT_EQ(fo4::remove_record_counted_element(plugin, climate_key(), weather_types(), 1),
            fo4::CountedArrayMutationStatus::index_out_of_range);
  EXPECT_EQ(fo4::test::original_bytes(plugin), original);
}

TEST(UncountedArray, PayloadThatIsNotWholeElementsFailsClosed) {
  auto odd = weather_payload({{0x00000A10u, 7, 0x00000A11u}});
  odd.resize(20);
  auto plugin = fo4::read_plugin(make_plugin(odd, {}), "Patch.esp");
  const auto original = fo4::test::original_bytes(plugin);
  EXPECT_THROW(fo4::insert_record_counted_element(plugin, climate_key(), weather_types(), 0),
               fo4::schema::CountedArrayError);
  EXPECT_THROW(fo4::remove_record_counted_element(plugin, climate_key(), weather_types(), 0),
               fo4::schema::CountedArrayError);
  EXPECT_EQ(fo4::test::original_bytes(plugin), original);
}
