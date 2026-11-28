// TODO-verify: DAMA entry width (8 bytes, 12 with a Curve Table from form version 152) is xEdit-derived and not checked against official masters.

#include "fo4/plugin/field_access.hpp"
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
#include <string>
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

std::vector<std::string> damage_values(const fo4::Plugin& plugin, const fo4::FormKey& key,
                                      std::string_view path) {
  std::vector<std::string> out;
  const auto* record = fo4::find_record(plugin, key);
  EXPECT_NE(record, nullptr);
  if (record == nullptr) return out;
  for (std::size_t index = 0;; ++index) {
    std::string text;
    const auto status = fo4::read_record_field(plugin, *record, path, index, text);
    if (status == fo4::FieldStatus::occurrence_missing) return out;
    EXPECT_EQ(status, fo4::FieldStatus::ok) << path << ' ' << index;
    if (status != fo4::FieldStatus::ok) return out;
    out.push_back(text);
  }
}

}  // namespace

TEST(WeaponDamageTypes, ReadsEightByteEntriesBeforeFormVersion152) {
  for (const std::uint16_t version : {std::uint16_t{131}, std::uint16_t{151}}) {
    auto plugin = weapon_plugin(version, words({0x801, 50, 0x802, 7}));
    const auto key = plugin_key(0x800);
    EXPECT_EQ(damage_values(plugin, key, "WEAP\\DamageType"),
              (std::vector<std::string>{plugin_key(0x801).to_string(), plugin_key(0x802).to_string()}))
        << version;
    EXPECT_EQ(damage_values(plugin, key, "WEAP\\DamageAmount"), (std::vector<std::string>{"50", "7"})) << version;
    EXPECT_TRUE(damage_values(plugin, key, "WEAP\\DamageTypeWithCurve").empty()) << version;
    EXPECT_TRUE(damage_values(plugin, key, "WEAP\\DamageAmountWithCurve").empty()) << version;
    EXPECT_TRUE(damage_values(plugin, key, "WEAP\\DamageCurveTable").empty()) << version;
  }
}

TEST(WeaponDamageTypes, ReadsTwelveByteEntriesFromFormVersion152) {
  auto plugin = weapon_plugin(152, words({0x801, 50, 0x803, 0x802, 7, 0}));
  const auto key = plugin_key(0x800);
  EXPECT_EQ(damage_values(plugin, key, "WEAP\\DamageTypeWithCurve"),
            (std::vector<std::string>{plugin_key(0x801).to_string(), plugin_key(0x802).to_string()}));
  EXPECT_EQ(damage_values(plugin, key, "WEAP\\DamageAmountWithCurve"), (std::vector<std::string>{"50", "7"}));
  EXPECT_EQ(damage_values(plugin, key, "WEAP\\DamageCurveTable"),
            (std::vector<std::string>{plugin_key(0x803).to_string(), std::string{}}));
  EXPECT_TRUE(damage_values(plugin, key, "WEAP\\DamageType").empty());
  EXPECT_TRUE(damage_values(plugin, key, "WEAP\\DamageAmount").empty());
}

TEST(WeaponDamageTypes, AnAbsentDamaSubrecordIsAnEmptyList) {
  auto plugin = weapon_plugin(131, {});
  EXPECT_TRUE(damage_values(plugin, plugin_key(0x800), "WEAP\\DamageType").empty());
  auto curved = weapon_plugin(152, {});
  EXPECT_TRUE(damage_values(curved, plugin_key(0x800), "WEAP\\DamageTypeWithCurve").empty());
}

TEST(WeaponDamageTypes, RejectsTwoDamaSubrecords) {
  auto plugin = weapon_plugin(131, words({0x801, 50}), "WEAP", 2);
  const auto* record = fo4::find_record(plugin, plugin_key(0x800));
  ASSERT_NE(record, nullptr);
  std::string text;
  EXPECT_THROW(static_cast<void>(fo4::read_record_field(plugin, *record, "WEAP\\DamageType", 0, text)),
               fo4::schema::FormLinkFieldError);
  EXPECT_THROW(static_cast<void>(fo4::read_record_field(plugin, *record, "WEAP\\DamageAmount", 0, text)),
               fo4::schema::ScalarError);
}

TEST(WeaponDamageTypes, RefusesAKeyThatIsNotAWeapon) {
  auto plugin = weapon_plugin(131, words({0x801, 50}), "MISC");
  const auto* record = fo4::find_record(plugin, plugin_key(0x800));
  ASSERT_NE(record, nullptr);
  std::string text;
  EXPECT_EQ(fo4::read_record_field(plugin, *record, "WEAP\\DamageType", 0, text),
            fo4::FieldStatus::record_mismatch);
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
