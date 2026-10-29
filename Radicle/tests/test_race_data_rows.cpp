#include "fo4/plugin/mutation.hpp"
#include "fo4/plugin/reader.hpp"
#include "fo4/plugin/writer.hpp"
#include "fo4/schema/field_address.hpp"
#include "fo4/schema/scalar.hpp"
#include "plugin_bytes.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <string>
#include <string_view>
#include <vector>

namespace {

using Bytes = std::vector<std::byte>;
using fo4::test::ByteWriter;

fo4::FormKey key() { return fo4::FormKey::parse("000800:Legacy.esp"); }

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

Bytes u16_bytes(std::uint16_t value) {
  ByteWriter out;
  out.u16(value);
  return out.bytes;
}

void put(Bytes& data, std::size_t offset, const Bytes& value) {
  std::copy(value.begin(), value.end(), data.begin() + static_cast<std::ptrdiff_t>(offset));
}

Bytes race_data(std::size_t size) {
  Bytes data(200, std::byte{0});
  put(data, 0, f32_bytes(1.0f));
  put(data, 4, f32_bytes(0.5f));
  put(data, 32, u32_bytes(5));
  put(data, 44, u32_bytes(1));
  put(data, 60, u32_bytes(29));
  put(data, 64, u32_bytes(0xFFFFFFFFu));
  put(data, 88, u32_bytes(2));
  put(data, 132, u16_bytes(100));
  put(data, 192, f32_bytes(0.25f));
  put(data, 196, f32_bytes(-0.5f));
  data.resize(size);
  return data;
}

Bytes race_plugin(std::uint16_t form_version, const Bytes& data, const Bytes& spct = {}) {
  ByteWriter out;
  const auto tes4 = fo4::test::begin_record(out, "TES4", 0, 0);
  ByteWriter hedr;
  hedr.f32(1.0f);
  hedr.u32(2);
  hedr.u32(0x801);
  fo4::test::write_subrecord(out, "HEDR", hedr.bytes);
  fo4::test::end_record(out, tes4);

  const auto group = fo4::test::begin_group(out, "RACE", 0);
  const auto record = fo4::test::begin_record(out, "RACE", 0, 0x00000800, form_version);
  fo4::test::write_subrecord(out, "EDID", std::string_view{"Legacy\0", 7});
  fo4::test::write_subrecord(out, "DATA", data);
  if (!spct.empty()) fo4::test::write_subrecord(out, "SPCT", spct);
  fo4::test::end_record(out, record);
  fo4::test::end_group(out, group);
  return out.bytes;
}

fo4::schema::ScalarFieldDescriptor row(std::string_view path) {
  const auto descriptor = fo4::schema::scalar_field_descriptor(path);
  EXPECT_TRUE(descriptor.has_value()) << path;
  return descriptor.value_or(fo4::schema::ScalarFieldDescriptor{});
}

std::int64_t value_of(const fo4::schema::ScalarDescriptor& scalar, std::string_view name) {
  const auto found = std::find_if(scalar.names.begin(), scalar.names.end(),
                                  [name](const auto& entry) { return entry.name == name; });
  EXPECT_NE(found, scalar.names.end()) << name;
  return found == scalar.names.end() ? 0 : found->value;
}

Bytes subrecord_bytes(const fo4::Plugin& plugin, std::string_view signature) {
  const auto* record = fo4::find_record(plugin, key());
  EXPECT_NE(record, nullptr);
  if (record == nullptr) return {};
  for (const auto& sub : record->subrecords) {
    if (sub.signature.to_string() == signature) return sub.data;
  }
  return {};
}

Bytes data_of(const fo4::Plugin& plugin) { return subrecord_bytes(plugin, "DATA"); }

std::string read_row(const fo4::Plugin& plugin, std::string_view path) {
  const auto field = row(path);
  const auto bytes = subrecord_bytes(plugin, field.scalar.subrecord.to_string());
  return fo4::schema::decode_scalar(fo4::schema::scalar_field_bytes(bytes, field.scalar),
                                    field.scalar);
}

bool exposed(const fo4::Plugin& plugin, std::string_view path) {
  const auto* record = fo4::find_record(plugin, key());
  if (record == nullptr) return false;
  const auto field = row(path);
  const auto slots = fo4::schema::resolve_field_slots(
      *record, fo4::schema::scalar_field_shape(field.scalar), field.scalar.address);
  return slots.slots.size() == 1 && slots.slots[0].valid;
}

std::string read(const fo4::Plugin& plugin, std::string_view path) {
  const auto field = row(path);
  return fo4::schema::decode_scalar(
      fo4::schema::scalar_field_bytes(data_of(plugin), field.scalar), field.scalar);
}

}  // namespace

TEST(RaceDataRows, RowsMatchXEditOffsetsStorageAndGates) {
  const auto height = row("RACE\\MaleHeight");
  EXPECT_EQ(height.scalar.subrecord, fo4::Signature::from_string("DATA"));
  EXPECT_TRUE(height.scalar.embedded);
  EXPECT_EQ(height.scalar.offset, 0u);
  EXPECT_EQ(height.scalar.storage, fo4::schema::ScalarStorage::f32);
  EXPECT_EQ(height.scalar.min_form_version, 0);

  const auto flags = row("RACE\\Flags");
  EXPECT_EQ(flags.scalar.offset, 32u);
  EXPECT_EQ(flags.scalar.storage, fo4::schema::ScalarStorage::u32);
  EXPECT_EQ(flags.scalar.presentation, fo4::schema::ScalarPresentation::flags);
  EXPECT_EQ(flags.scalar.min_form_version, 109);
  EXPECT_EQ(flags.scalar.names.size(), 32u);
  EXPECT_EQ(value_of(flags.scalar, "Child"), 4);

  const auto flags2 = row("RACE\\Flags2");
  EXPECT_EQ(flags2.scalar.offset, 88u);
  EXPECT_EQ(flags2.scalar.min_form_version, 124);
  EXPECT_EQ(flags2.scalar.names.size(), 23u);
  EXPECT_EQ(value_of(flags2.scalar, "Non-Hostile"), 2);

  const auto size = row("RACE\\Size");
  EXPECT_EQ(size.scalar.offset, 44u);
  EXPECT_EQ(size.scalar.presentation, fo4::schema::ScalarPresentation::enumeration);
  EXPECT_EQ(size.scalar.maximum, 3.0);
  EXPECT_EQ(value_of(size.scalar, "Medium"), 1);

  const auto shield = row("RACE\\ShieldBipedObject");
  EXPECT_EQ(shield.scalar.offset, 60u);
  EXPECT_EQ(shield.scalar.storage, fo4::schema::ScalarStorage::i32);
  EXPECT_EQ(shield.scalar.min_form_version, 109);
  EXPECT_EQ(shield.scalar.names.size(), 33u);
  EXPECT_EQ(value_of(shield.scalar, "59 - Shield"), 29);
  EXPECT_EQ(value_of(shield.scalar, "None"), -1);

  const auto beard = row("RACE\\BeardBipedObject");
  EXPECT_EQ(beard.scalar.offset, 64u);
  EXPECT_EQ(beard.scalar.min_form_version, 124);

  const auto xp = row("RACE\\XPValue");
  EXPECT_EQ(xp.scalar.offset, 132u);
  EXPECT_EQ(xp.scalar.storage, fo4::schema::ScalarStorage::i16);
  EXPECT_EQ(xp.scalar.min_form_version, 124);

  const auto pitch = row("RACE\\OrientationPitch");
  EXPECT_EQ(pitch.scalar.offset, 192u);
  EXPECT_EQ(pitch.scalar.minimum_payload_size, 196u);
  const auto roll = row("RACE\\OrientationRoll");
  EXPECT_EQ(roll.scalar.offset, 196u);
  EXPECT_EQ(roll.scalar.minimum_payload_size, 200u);
}

TEST(RaceDataRows, VanillaLayoutReadsAndWritesAtFormVersion131) {
  const auto original = race_data(200);
  auto plugin = fo4::read_plugin(race_plugin(131, original), "Legacy.esp");
  EXPECT_EQ(read(plugin, "RACE\\Flags"), "5");
  EXPECT_EQ(read(plugin, "RACE\\Size"), "1");
  EXPECT_EQ(read(plugin, "RACE\\ShieldBipedObject"), "29");
  EXPECT_EQ(read(plugin, "RACE\\BeardBipedObject"), "-1");
  EXPECT_EQ(read(plugin, "RACE\\Flags2"), "2");
  EXPECT_EQ(read(plugin, "RACE\\XPValue"), "100");

  EXPECT_EQ(fo4::set_record_scalar(plugin, key(), row("RACE\\MaleHeight"), "1.25"),
            fo4::ScalarMutationStatus::updated);
  EXPECT_EQ(fo4::set_record_scalar(plugin, key(), row("RACE\\Flags"), "4"),
            fo4::ScalarMutationStatus::updated);
  EXPECT_EQ(fo4::set_record_scalar(plugin, key(), row("RACE\\Size"), "2"),
            fo4::ScalarMutationStatus::updated);
  EXPECT_EQ(fo4::set_record_scalar(plugin, key(), row("RACE\\ShieldBipedObject"), "-1"),
            fo4::ScalarMutationStatus::updated);

  auto expected = original;
  put(expected, 0, f32_bytes(1.25f));
  put(expected, 32, u32_bytes(4));
  put(expected, 44, u32_bytes(2));
  put(expected, 60, u32_bytes(0xFFFFFFFFu));
  const auto reread = fo4::read_plugin(fo4::write_plugin(plugin), "Legacy.esp");
  EXPECT_EQ(data_of(reread), expected);
  EXPECT_EQ(read(reread, "RACE\\Size"), "2");
  EXPECT_EQ(read(reread, "RACE\\ShieldBipedObject"), "-1");
}

TEST(RaceDataRows, SpellCountRowReadsAndWritesTheSpctSubrecord) {
  const auto count = row("RACE\\SpellCount");
  EXPECT_EQ(count.scalar.subrecord, fo4::Signature::from_string("SPCT"));
  EXPECT_FALSE(count.scalar.embedded);
  EXPECT_EQ(count.scalar.offset, 0u);
  EXPECT_EQ(count.scalar.storage, fo4::schema::ScalarStorage::u32);
  // Invariant: a whole-payload integer scalar leaves payload_size 0; its width is the storage width.
  EXPECT_EQ(count.scalar.payload_size, 0u);
  EXPECT_EQ(count.scalar.min_form_version, 0);

  auto plugin = fo4::read_plugin(race_plugin(131, race_data(200), u32_bytes(2)), "Legacy.esp");
  EXPECT_EQ(read_row(plugin, "RACE\\SpellCount"), "2");
  EXPECT_EQ(fo4::set_record_scalar(plugin, key(), row("RACE\\SpellCount"), "3"),
            fo4::ScalarMutationStatus::updated);
  const auto reread = fo4::read_plugin(fo4::write_plugin(plugin), "Legacy.esp");
  EXPECT_EQ(read_row(reread, "RACE\\SpellCount"), "3");
  EXPECT_EQ(subrecord_bytes(reread, "SPCT"), u32_bytes(3));
  EXPECT_EQ(data_of(reread), race_data(200));

  auto absent = fo4::read_plugin(race_plugin(131, race_data(200)), "Legacy.esp");
  const auto before = data_of(absent);
  EXPECT_EQ(fo4::set_record_scalar(absent, key(), row("RACE\\SpellCount"), "3"),
            fo4::ScalarMutationStatus::subrecord_missing);
  EXPECT_EQ(data_of(absent), before);
}

TEST(RaceDataRows, ShiftedMembersAreHiddenBelowTheirFormVersionGate) {
  const auto check = [](std::uint16_t version, std::size_t size, std::initializer_list<std::string_view> shown,
                        std::initializer_list<std::string_view> hidden) {
    auto plugin = fo4::read_plugin(race_plugin(version, race_data(size)), "Legacy.esp");
    const auto before = data_of(plugin);
    for (const auto path : shown) EXPECT_TRUE(exposed(plugin, path)) << path << " at " << version;
    for (const auto path : hidden) {
      EXPECT_FALSE(exposed(plugin, path)) << path << " at " << version;
      EXPECT_EQ(fo4::set_record_scalar(plugin, key(), row(path), "9"),
                fo4::ScalarMutationStatus::subrecord_missing)
          << path << " at " << version;
    }
    EXPECT_EQ(data_of(plugin), before);
  };
  check(108, 168, {"RACE\\MaleHeight", "RACE\\FemaleHeight"},
        {"RACE\\MaleWeightThin", "RACE\\Flags", "RACE\\ShieldBipedObject", "RACE\\XPValue",
         "RACE\\BeardBipedObject", "RACE\\OrientationPitch"});
  check(123, 196, {"RACE\\Flags", "RACE\\ShieldBipedObject", "RACE\\Size", "RACE\\MaleWeightThin"},
        {"RACE\\BeardBipedObject", "RACE\\Flags2", "RACE\\XPValue", "RACE\\OrientationPitch",
         "RACE\\OrientationRoll"});
  check(124, 200, {"RACE\\BeardBipedObject", "RACE\\Flags2", "RACE\\XPValue", "RACE\\OrientationPitch",
                   "RACE\\OrientationRoll"},
        {});
}

TEST(RaceDataRows, OrientationRollNeedsTheFull200BytePayload) {
  auto plugin = fo4::read_plugin(race_plugin(131, race_data(196)), "Legacy.esp");
  EXPECT_TRUE(exposed(plugin, "RACE\\OrientationPitch"));
  EXPECT_FALSE(exposed(plugin, "RACE\\OrientationRoll"));
  EXPECT_EQ(fo4::set_record_scalar(plugin, key(), row("RACE\\OrientationRoll"), "1"),
            fo4::ScalarMutationStatus::subrecord_missing);
  EXPECT_EQ(fo4::set_record_scalar(plugin, key(), row("RACE\\OrientationPitch"), "1"),
            fo4::ScalarMutationStatus::updated);
}
