// TODO-verify: DAMA entry width (8 bytes, 12 with a Curve Table from form version 152) is xEdit-derived and not checked against official masters.

#include "fo4/forms/weapon.hpp"
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
#include <string_view>
#include <vector>

namespace {

fo4::FormKey plugin_key(std::uint32_t id) {
  return fo4::FormKey{fo4::PluginKey::from_filename("Patch.esp"), fo4::LocalFormId{id}};
}

std::vector<std::byte> words(std::initializer_list<std::uint32_t> values) {
  fo4::test::ByteWriter out;
  for (const auto value : values) out.u32(value);
  return out.bytes;
}

fo4::Plugin weapon_plugin(std::uint16_t form_version, const std::vector<std::byte>& dama,
                          std::string_view record_signature = "WEAP", int dama_count = 1) {
  fo4::test::ByteWriter out;
  const auto tes4 = fo4::test::begin_record(out, "TES4", 0, 0);
  fo4::test::ByteWriter hedr;
  hedr.f32(1.0f);
  hedr.u32(1);
  hedr.u32(0x800);
  fo4::test::write_subrecord(out, "HEDR", hedr.bytes);
  fo4::test::end_record(out, tes4);
  const auto group = fo4::test::begin_group(out, record_signature, 0);
  const auto weap = fo4::test::begin_record(out, record_signature, 0, 0x800, form_version);
  for (int index = 0; index < dama_count; ++index) fo4::test::write_subrecord(out, "DAMA", dama);
  fo4::test::end_record(out, weap);
  fo4::test::end_group(out, group);
  return fo4::read_plugin(out.bytes, "Patch.esp");
}

using Bytes = std::vector<std::byte>;

Bytes pattern(std::size_t size, std::size_t step) {
  Bytes out(size);
  for (std::size_t i = 0; i < size; ++i) out[i] = static_cast<std::byte>((i * step) & 0xFF);
  return out;
}

void patch_u16(Bytes& data, std::size_t offset, std::uint16_t value) {
  data[offset] = static_cast<std::byte>(value & 0xFF);
  data[offset + 1] = static_cast<std::byte>((value >> 8) & 0xFF);
}

void patch_u32(Bytes& data, std::size_t offset, std::uint32_t value) {
  for (std::size_t i = 0; i < 4; ++i) {
    data[offset + i] = static_cast<std::byte>((value >> (8 * i)) & 0xFF);
  }
}

void patch_f32(Bytes& data, std::size_t offset, float value) {
  std::uint32_t raw = 0;
  std::memcpy(&raw, &value, sizeof(raw));
  patch_u32(data, offset, raw);
}

fo4::Plugin scalar_weapon_plugin(const Bytes& dnam, const Bytes& fnam) {
  fo4::test::ByteWriter out;
  const auto tes4 = fo4::test::begin_record(out, "TES4", 0, 0);
  fo4::test::ByteWriter hedr;
  hedr.f32(1.0f);
  hedr.u32(1);
  hedr.u32(0x800);
  fo4::test::write_subrecord(out, "HEDR", hedr.bytes);
  fo4::test::end_record(out, tes4);
  const auto group = fo4::test::begin_group(out, "WEAP", 0);
  const auto edited = fo4::test::begin_record(out, "WEAP", 0, 0x800);
  fo4::test::write_subrecord(out, "DNAM", dnam);
  fo4::test::write_subrecord(out, "FNAM", fnam);
  fo4::test::end_record(out, edited);
  const auto other = fo4::test::begin_record(out, "WEAP", 0, 0x801);
  fo4::test::write_subrecord(out, "DNAM", dnam);
  fo4::test::end_record(out, other);
  fo4::test::end_group(out, group);
  return fo4::read_plugin(out.bytes, "Patch.esp");
}

Bytes payload(const fo4::Plugin& plugin, std::uint32_t local, std::string_view name) {
  const auto* record = fo4::find_record(plugin, plugin_key(local));
  if (record == nullptr) return {};
  for (const auto& subrecord : record->subrecords) {
    if (subrecord.signature == fo4::Signature::from_string(name)) return subrecord.data;
  }
  return {};
}

fo4::ScalarMutationStatus scalar(fo4::Plugin& plugin, std::uint32_t local, std::string_view path,
                                 std::size_t index, std::string_view value) {
  const auto field = fo4::schema::scalar_field_descriptor(path);
  EXPECT_TRUE(field.has_value()) << path;
  if (!field.has_value()) return fo4::ScalarMutationStatus::record_missing;
  return fo4::set_record_scalar(plugin, plugin_key(local), field.value(), index, value);
}

}  // namespace

TEST(WeaponDamageTypes, ReadsEightByteEntriesBeforeFormVersion152) {
  const auto plugin = weapon_plugin(131, words({0x801, 50, 0x802, 7}));
  const auto types = fo4::forms::decode_weapon_damage_types(plugin, plugin_key(0x800));
  ASSERT_EQ(types.size(), 2u);
  EXPECT_EQ(types[0].type, plugin_key(0x801));
  EXPECT_EQ(types[0].amount, 50u);
  EXPECT_EQ(types[1].type, plugin_key(0x802));
  EXPECT_EQ(types[1].amount, 7u);
  EXPECT_EQ(types[1].curve_table, fo4::FormKey{});
}

TEST(WeaponDamageTypes, ReadsTwelveByteEntriesFromFormVersion152) {
  const auto plugin = weapon_plugin(152, words({0x801, 50, 0x803, 0x802, 7, 0}));
  const auto types = fo4::forms::decode_weapon_damage_types(plugin, plugin_key(0x800));
  ASSERT_EQ(types.size(), 2u);
  EXPECT_EQ(types[0].curve_table, plugin_key(0x803));
  EXPECT_EQ(types[1].type, plugin_key(0x802));
  EXPECT_EQ(types[1].amount, 7u);
  EXPECT_EQ(types[1].curve_table, fo4::FormKey{});
}

TEST(WeaponDamageTypes, FailsClosedOnAPayloadThatIsNotWholeEntries) {
  const auto plugin = weapon_plugin(131, std::vector<std::byte>(10, std::byte{0}));
  EXPECT_THROW(static_cast<void>(fo4::forms::decode_weapon_damage_types(plugin, plugin_key(0x800))),
               fo4::forms::WeaponError);
}

TEST(WeaponDamageTypes, AnAbsentDamaSubrecordIsAnEmptyList) {
  const auto plugin = weapon_plugin(131, {});
  EXPECT_TRUE(fo4::forms::decode_weapon_damage_types(plugin, plugin_key(0x800)).empty());
}

TEST(WeaponDamageTypes, RejectsAKeyWithNoWeaponRecord) {
  const auto plugin = weapon_plugin(131, words({0x801, 50}));
  EXPECT_THROW(static_cast<void>(fo4::forms::decode_weapon_damage_types(plugin, plugin_key(0x999))),
               fo4::forms::WeaponError);
}

TEST(WeaponDamageTypes, RejectsTwoDamaSubrecords) {
  const auto plugin = weapon_plugin(131, words({0x801, 50}), "WEAP", 2);
  EXPECT_THROW(static_cast<void>(fo4::forms::decode_weapon_damage_types(plugin, plugin_key(0x800))),
               fo4::forms::WeaponError);
}

TEST(WeaponDamageTypes, Version151StillReadsEightByteEntries) {
  const auto plugin = weapon_plugin(151, words({0x801, 50, 0x802, 7}));
  const auto types = fo4::forms::decode_weapon_damage_types(plugin, plugin_key(0x800));
  ASSERT_EQ(types.size(), 2u);
  EXPECT_EQ(types[1].amount, 7u);
  EXPECT_EQ(types[0].curve_table, fo4::FormKey{});
  EXPECT_EQ(types[1].curve_table, fo4::FormKey{});
}

TEST(WeaponDamageTypes, RejectsAKeyThatIsNotAWeapon) {
  const auto plugin = weapon_plugin(131, words({0x801, 50}), "MISC");
  EXPECT_THROW(static_cast<void>(fo4::forms::decode_weapon_damage_types(plugin, plugin_key(0x800))),
               fo4::forms::WeaponError);
}

TEST(WeaponScalarWrites, SaveAndReparseChangesOnlyTheAddressedWords) {
  const auto dnam = pattern(132, 1);
  const auto fnam = pattern(41, 3);
  auto plugin = scalar_weapon_plugin(dnam, fnam);

  EXPECT_EQ(scalar(plugin, 0x800, "WEAP\\Speed", 0, "42.5"), fo4::ScalarMutationStatus::updated);
  EXPECT_EQ(scalar(plugin, 0x800, "WEAP\\UnknownAfterAnimationAttackSeconds", 0, "7"),
            fo4::ScalarMutationStatus::updated);
  EXPECT_EQ(scalar(plugin, 0x800, "WEAP\\Stagger", 0, "3"), fo4::ScalarMutationStatus::updated);
  EXPECT_EQ(scalar(plugin, 0x800, "WEAP\\UnknownEnd", 0, "3"), fo4::ScalarMutationStatus::updated);
  EXPECT_EQ(scalar(plugin, 0x800, "WEAP\\BoltAnimationSeconds", 0, "4294967295"),
            fo4::ScalarMutationStatus::updated);

  auto expected_dnam = dnam;
  patch_f32(expected_dnam, 4, 42.5f);
  patch_u16(expected_dnam, 110, 7);
  patch_u32(expected_dnam, 124, 3);
  patch_u32(expected_dnam, 128, 3);
  auto expected_fnam = fnam;
  patch_u32(expected_fnam, 20, 0xFFFFFFFFu);

  const auto reparsed = fo4::read_plugin(fo4::write_plugin(plugin), "Patch.esp");
  EXPECT_EQ(payload(reparsed, 0x800, "DNAM"), expected_dnam);
  EXPECT_EQ(payload(reparsed, 0x800, "FNAM"), expected_fnam);
  EXPECT_EQ(payload(reparsed, 0x801, "DNAM"), dnam);
  EXPECT_EQ(payload(reparsed, 0x800, "DNAM").size(), 132u);
  EXPECT_EQ(payload(reparsed, 0x800, "FNAM").size(), 41u);

  auto again = reparsed;
  const auto before = fo4::write_plugin(again);
  EXPECT_EQ(scalar(again, 0x800, "WEAP\\Speed", 0, "42.5"), fo4::ScalarMutationStatus::unchanged);
  EXPECT_EQ(scalar(again, 0x800, "WEAP\\BoltAnimationSeconds", 0, "4294967295"),
            fo4::ScalarMutationStatus::unchanged);
  EXPECT_EQ(fo4::write_plugin(again), before);
}

TEST(WeaponScalarWrites, RefusesTheFieldsOfAShortDnamOrFnamAndKeepsTheirBytes) {
  auto short_dnam = pattern(132, 1);
  short_dnam.pop_back();
  auto plugin = scalar_weapon_plugin(short_dnam, pattern(41, 3));

  EXPECT_EQ(scalar(plugin, 0x800, "WEAP\\Speed", 0, "42.5"), fo4::ScalarMutationStatus::subrecord_missing);
  EXPECT_EQ(scalar(plugin, 0x800, "WEAP\\UnknownEnd", 0, "3"), fo4::ScalarMutationStatus::subrecord_missing);
  EXPECT_EQ(scalar(plugin, 0x800, "WEAP\\AnimationFireSeconds", 0, "1.5"),
            fo4::ScalarMutationStatus::updated);

  auto expected_fnam = pattern(41, 3);
  patch_f32(expected_fnam, 0, 1.5f);
  const auto reparsed = fo4::read_plugin(fo4::write_plugin(plugin), "Patch.esp");
  EXPECT_EQ(payload(reparsed, 0x800, "DNAM"), short_dnam);
  EXPECT_EQ(payload(reparsed, 0x800, "FNAM"), expected_fnam);

  auto short_fnam = pattern(41, 3);
  short_fnam.pop_back();
  auto other = scalar_weapon_plugin(pattern(132, 1), short_fnam);
  EXPECT_EQ(scalar(other, 0x800, "WEAP\\AnimationFireSeconds", 0, "1.5"),
            fo4::ScalarMutationStatus::subrecord_missing);
  EXPECT_EQ(scalar(other, 0x800, "WEAP\\Speed", 0, "42.5"), fo4::ScalarMutationStatus::updated);

  auto expected_dnam = pattern(132, 1);
  patch_f32(expected_dnam, 4, 42.5f);
  const auto other_reparsed = fo4::read_plugin(fo4::write_plugin(other), "Patch.esp");
  EXPECT_EQ(payload(other_reparsed, 0x800, "FNAM"), short_fnam);
  EXPECT_EQ(payload(other_reparsed, 0x800, "DNAM"), expected_dnam);
}
