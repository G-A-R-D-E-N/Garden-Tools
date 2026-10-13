// TODO-verify: DAMA entry width (8 bytes, 12 with a Curve Table from form version 152) is xEdit-derived and not checked against official masters.

#include "fo4/forms/weapon.hpp"
#include "fo4/plugin/reader.hpp"
#include "plugin_bytes.hpp"

#include <gtest/gtest.h>

#include <cstdint>
#include <initializer_list>
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

fo4::Plugin weapon_plugin(std::uint16_t form_version, const std::vector<std::byte>& dama) {
  fo4::test::ByteWriter out;
  const auto tes4 = fo4::test::begin_record(out, "TES4", 0, 0);
  fo4::test::ByteWriter hedr;
  hedr.f32(1.0f);
  hedr.u32(1);
  hedr.u32(0x800);
  fo4::test::write_subrecord(out, "HEDR", hedr.bytes);
  fo4::test::end_record(out, tes4);
  const auto group = fo4::test::begin_group(out, "WEAP", 0);
  const auto weap = fo4::test::begin_record(out, "WEAP", 0, 0x800, form_version);
  fo4::test::write_subrecord(out, "DAMA", dama);
  fo4::test::end_record(out, weap);
  fo4::test::end_group(out, group);
  return fo4::read_plugin(out.bytes, "Patch.esp");
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
