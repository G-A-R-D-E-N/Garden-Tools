#include "fo4/plugin/mutation.hpp"
#include "fo4/plugin/reader.hpp"
#include "fo4/plugin/writer.hpp"
#include "fo4/schema/field_address.hpp"
#include "fo4/schema/formlink_lists.hpp"
#include "fo4/schema/scalar.hpp"
#include "plugin_bytes.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <initializer_list>
#include <string>
#include <string_view>
#include <utility>
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

constexpr auto k_atkd = fo4::Signature::from_string("ATKD");
constexpr auto k_rbpc = fo4::Signature::from_string("RBPC");
constexpr auto k_avif = fo4::Signature::from_string("AVIF");
constexpr auto k_equp = fo4::Signature::from_string("EQUP");
constexpr auto k_spel = fo4::Signature::from_string("SPEL");

fo4::FormKey local_key(std::uint32_t local) {
  char text[64];
  std::snprintf(text, sizeof(text), "%06X:Legacy.esp", local);
  return fo4::FormKey::parse(text);
}

fo4::schema::FormLinkFieldDescriptor link(std::string_view path) {
  return fo4::schema::formlink_field_descriptor(path).value();
}

std::uint32_t read_u32(const Bytes& data, std::size_t offset) {
  std::uint32_t value = 0;
  for (std::size_t i = 0; i < 4; ++i) {
    value |= static_cast<std::uint32_t>(std::to_integer<std::uint8_t>(data[offset + i])) << (8 * i);
  }
  return value;
}

Bytes text_bytes(std::string_view text) {
  Bytes out;
  for (const char ch : text) out.push_back(static_cast<std::byte>(static_cast<unsigned char>(ch)));
  out.push_back(std::byte{0});
  return out;
}

Bytes atkd(float damage_mult) {
  ByteWriter out;
  out.f32(damage_mult);
  out.f32(0.5f);
  out.u32(0);
  out.u32(3);
  out.f32(30.0f);
  out.f32(15.0f);
  out.f32(0.75f);
  out.f32(0.25f);
  out.f32(1.25f);
  out.f32(0.5f);
  out.i32(-2);
  return out.bytes;
}

void add_group(ByteWriter& out, std::string_view signature,
               std::initializer_list<std::uint32_t> locals) {
  const auto group = fo4::test::begin_group(out, signature, 0);
  for (const auto local : locals) {
    const auto record = fo4::test::begin_record(out, signature, 0, local);
    fo4::test::end_record(out, record);
  }
  fo4::test::end_group(out, group);
}

std::vector<std::byte> race_record_plugin(
    std::uint16_t form_version,
    std::initializer_list<std::pair<std::string_view, Bytes>> subrecords) {
  ByteWriter out;
  const auto tes4 = fo4::test::begin_record(out, "TES4", 0, 0);
  ByteWriter hedr;
  hedr.f32(1.0f);
  hedr.u32(1);
  hedr.u32(0x911);
  fo4::test::write_subrecord(out, "HEDR", hedr.bytes);
  fo4::test::end_record(out, tes4);

  const auto group = fo4::test::begin_group(out, "RACE", 0);
  const auto record = fo4::test::begin_record(out, "RACE", 0, 0x00000800, form_version);
  fo4::test::write_subrecord(out, "EDID", std::string_view{"Legacy\0", 7});
  for (const auto& [signature, payload] : subrecords) {
    fo4::test::write_subrecord(out, signature, payload);
  }
  fo4::test::end_record(out, record);
  fo4::test::end_group(out, group);

  add_group(out, "AVIF", {0x0000036C, 0x0000036D, 0x0000036E, 0x0000036F, 0x00000370});
  add_group(out, "EQUP", {0x00000A01, 0x00000A02, 0x00000A03});
  add_group(out, "SPEL", {0x00000A00});
  return out.bytes;
}

const Bytes* payload_of(const fo4::Plugin& plugin, fo4::Signature signature, std::size_t index) {
  const auto* record = fo4::find_record(plugin, key());
  if (record == nullptr) return nullptr;
  std::size_t seen = 0;
  for (const auto& subrecord : record->subrecords) {
    if (subrecord.signature != signature) continue;
    if (seen == index) return &subrecord.data;
    ++seen;
  }
  return nullptr;
}

std::string read_member(const fo4::Plugin& plugin, std::string_view path, std::size_t index) {
  const auto field = row(path);
  const auto* record = fo4::find_record(plugin, key());
  if (record == nullptr) return "absent";
  const auto resolved = fo4::schema::resolve_field_slots(
      *record, fo4::schema::scalar_field_shape(field.scalar), field.scalar.address);
  if (index >= resolved.slots.size() || !resolved.slots[index].valid) return "absent";
  const auto slot = resolved.slots[index];
  return fo4::schema::decode_scalar(
      fo4::schema::scalar_slot_bytes(record->subrecords[slot.subrecord].data, slot, field.scalar),
      field.scalar);
}

std::string first_text(const fo4::Plugin& plugin, fo4::Signature signature) {
  const auto* payload = payload_of(plugin, signature, 0);
  if (payload == nullptr) return "absent";
  std::string out;
  for (const auto byte : *payload) {
    const char ch = std::to_integer<char>(byte);
    if (ch == '\0') break;
    out.push_back(ch);
  }
  return out;
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

TEST(RaceDataRows, AttackRowsMirrorTheSharedAttackStruct) {
  struct Expectation {
    std::string_view path;
    std::size_t offset;
    fo4::schema::ScalarStorage storage;
  };
  const Expectation rows[] = {
      {"RACE\\AttackDamageMult", 0, fo4::schema::ScalarStorage::f32},
      {"RACE\\AttackChance", 4, fo4::schema::ScalarStorage::f32},
      {"RACE\\AttackFlags", 12, fo4::schema::ScalarStorage::u32},
      {"RACE\\AttackAngle", 16, fo4::schema::ScalarStorage::f32},
      {"RACE\\StrikeAngle", 20, fo4::schema::ScalarStorage::f32},
      {"RACE\\AttackStagger", 24, fo4::schema::ScalarStorage::f32},
      {"RACE\\AttackKnockdown", 28, fo4::schema::ScalarStorage::f32},
      {"RACE\\AttackRecoveryTime", 32, fo4::schema::ScalarStorage::f32},
      {"RACE\\AttackActionPointsMult", 36, fo4::schema::ScalarStorage::f32},
      {"RACE\\AttackStaggerOffset", 40, fo4::schema::ScalarStorage::i32},
  };
  for (const auto& expected : rows) {
    SCOPED_TRACE(expected.path);
    const auto field = row(expected.path);
    EXPECT_EQ(field.scalar.subrecord, k_atkd);
    EXPECT_EQ(field.scalar.offset, expected.offset);
    EXPECT_EQ(field.scalar.storage, expected.storage);
    EXPECT_EQ(field.scalar.payload_size, 44u);
    EXPECT_TRUE(field.scalar.address.repeated);
    EXPECT_TRUE(field.scalar.embedded);
    EXPECT_FALSE(field.can_insert);
    EXPECT_EQ(field.scalar.min_form_version, 0);
  }

  const auto flags = row("RACE\\AttackFlags");
  EXPECT_EQ(flags.scalar.presentation, fo4::schema::ScalarPresentation::flags);
  ASSERT_EQ(flags.scalar.names.size(), 32u);
  EXPECT_EQ(value_of(flags.scalar, "Bash Attack"), 2);
  EXPECT_EQ(value_of(flags.scalar, "Override Data"), 2147483648LL);

  const auto spell = link("RACE\\AttackSpell");
  EXPECT_EQ(spell.record, fo4::Signature::from_string("RACE"));
  EXPECT_EQ(spell.subrecord, k_atkd);
  EXPECT_EQ(spell.offset, 8u);
  EXPECT_EQ(spell.payload_sizes[0], 44u);
  EXPECT_TRUE(spell.address.repeated);
  EXPECT_TRUE(spell.nullable);
  EXPECT_TRUE(fo4::schema::formlink_field_target_allowed(spell, k_spel));
  EXPECT_FALSE(fo4::schema::formlink_field_target_allowed(spell, k_equp));

  const auto weapon = link("RACE\\AttackWeaponSlot");
  EXPECT_EQ(weapon.subrecord, fo4::Signature::from_string("ATKW"));
  EXPECT_EQ(weapon.offset, 0u);
  EXPECT_EQ(weapon.payload_sizes[0], 4u);
  EXPECT_TRUE(weapon.address.repeated);
  EXPECT_FALSE(weapon.nullable);
  EXPECT_TRUE(fo4::schema::formlink_field_target_allowed(weapon, k_equp));
  EXPECT_FALSE(fo4::schema::formlink_field_target_allowed(weapon, k_spel));

  const auto required = link("RACE\\AttackRequiredSlot");
  EXPECT_EQ(required.subrecord, fo4::Signature::from_string("ATKS"));
  EXPECT_EQ(required.payload_sizes[0], 4u);
  EXPECT_TRUE(required.address.repeated);
  EXPECT_TRUE(fo4::schema::formlink_field_target_allowed(required, k_equp));

  const auto event = row("RACE\\AttackEvent");
  EXPECT_EQ(event.scalar.subrecord, fo4::Signature::from_string("ATKE"));
  EXPECT_EQ(event.scalar.storage, fo4::schema::ScalarStorage::zstring);
  EXPECT_TRUE(event.scalar.address.repeated);
  EXPECT_FALSE(event.scalar.embedded);

  const auto description = row("RACE\\AttackDescription");
  EXPECT_EQ(description.scalar.subrecord, fo4::Signature::from_string("ATKT"));
  EXPECT_EQ(description.scalar.storage, fo4::schema::ScalarStorage::zstring);
  EXPECT_TRUE(description.scalar.address.repeated);
}

TEST(RaceDataRows, AttackMembersReadAndWriteAcrossTwoInstances) {
  auto plugin = fo4::read_plugin(race_record_plugin(131, {{std::string_view{"ATKD"}, atkd(1.5f)},
                                                         {std::string_view{"ATKD"}, atkd(4.5f)},
                                                         {std::string_view{"ATKE"}, text_bytes("event_a")},
                                                         {std::string_view{"ATKW"}, u32_bytes(0x00000A01u)}}),
                                 "Legacy.esp");
  EXPECT_EQ(read_member(plugin, "RACE\\AttackDamageMult", 0), "1.5");
  EXPECT_EQ(read_member(plugin, "RACE\\AttackDamageMult", 1), "4.5");
  EXPECT_EQ(read_member(plugin, "RACE\\AttackDamageMult", 2), "absent");
  EXPECT_EQ(read_member(plugin, "RACE\\AttackStaggerOffset", 0), "-2");
  EXPECT_EQ(first_text(plugin, fo4::Signature::from_string("ATKE")), "event_a");

  EXPECT_EQ(fo4::set_record_scalar(plugin, key(), row("RACE\\AttackChance"), 1, "0.25"),
            fo4::ScalarMutationStatus::updated);
  EXPECT_EQ(fo4::set_record_scalar(plugin, key(), row("RACE\\AttackStaggerOffset"), 0, "-7"),
            fo4::ScalarMutationStatus::updated);
  EXPECT_EQ(fo4::set_record_scalar(plugin, key(), row("RACE\\AttackFlags"), 1, "5"),
            fo4::ScalarMutationStatus::updated);
  EXPECT_EQ(fo4::set_record_formlink_field(plugin, key(), link("RACE\\AttackSpell"), 1,
                                           local_key(0x00000A00)),
            fo4::FormLinkFieldMutationStatus::updated);
  EXPECT_EQ(fo4::set_record_formlink_field(plugin, key(), link("RACE\\AttackWeaponSlot"), 0,
                                           local_key(0x00000A03)),
            fo4::FormLinkFieldMutationStatus::updated);
  EXPECT_EQ(fo4::set_record_string(plugin, key(), row("RACE\\AttackEvent"), 0, "event_b"),
            fo4::ScalarMutationStatus::updated);

  const auto after = fo4::read_plugin(fo4::write_plugin(plugin), "Legacy.esp");
  const auto* first = payload_of(after, k_atkd, 0);
  const auto* second = payload_of(after, k_atkd, 1);
  ASSERT_NE(first, nullptr);
  ASSERT_NE(second, nullptr);
  ASSERT_EQ(first->size(), 44u);
  ASSERT_EQ(second->size(), 44u);

  auto expected_first = atkd(1.5f);
  put(expected_first, 40, u32_bytes(0xFFFFFFF9u));
  EXPECT_EQ(*first, expected_first);
  EXPECT_EQ(read_u32(*second, 0), 0x40900000u);
  EXPECT_EQ(read_u32(*second, 4), 0x3E800000u);
  EXPECT_EQ(read_u32(*second, 8), 0x00000A00u);
  EXPECT_EQ(read_u32(*second, 12), 5u);
  EXPECT_EQ(read_u32(*second, 24), 0x3F400000u);
  EXPECT_EQ(first_text(after, fo4::Signature::from_string("ATKE")), "event_b");
  const auto* weapon = payload_of(after, fo4::Signature::from_string("ATKW"), 0);
  ASSERT_NE(weapon, nullptr);
  EXPECT_EQ(read_u32(*weapon, 0), 0x00000A03u);
}

TEST(RaceDataRows, ShortAttackPayloadHidesEveryMember) {
  auto plugin = fo4::read_plugin(
      race_record_plugin(131, {{std::string_view{"ATKD"}, Bytes(24, std::byte{0})}}), "Legacy.esp");
  for (const auto path : {"RACE\\AttackDamageMult", "RACE\\AttackChance", "RACE\\AttackFlags",
                          "RACE\\AttackStaggerOffset"}) {
    EXPECT_EQ(read_member(plugin, path, 0), "absent") << path;
  }
  EXPECT_EQ(fo4::set_record_scalar(plugin, key(), row("RACE\\AttackFlags"), 0, "1"),
            fo4::ScalarMutationStatus::subrecord_missing);
  EXPECT_THROW(fo4::set_record_formlink_field(plugin, key(), link("RACE\\AttackSpell"), 0,
                                             local_key(0x00000A00)),
               fo4::schema::FormLinkFieldError);

  const auto after = fo4::read_plugin(fo4::write_plugin(plugin), "Legacy.esp");
  const auto* payload = payload_of(after, k_atkd, 0);
  ASSERT_NE(payload, nullptr);
  EXPECT_EQ(*payload, Bytes(24, std::byte{0}));
}

TEST(RaceDataRows, BipedObjectConditionIsThirtyTwoFourByteSlots) {
  const auto condition = link("RACE\\BipedObjectCondition");
  EXPECT_EQ(condition.record, fo4::Signature::from_string("RACE"));
  EXPECT_EQ(condition.subrecord, k_rbpc);
  EXPECT_EQ(condition.offset, 0u);
  EXPECT_EQ(condition.address.stride, 4u);
  EXPECT_FALSE(condition.address.repeated);
  EXPECT_EQ(condition.min_form_version, 78u);
  EXPECT_EQ(condition.max_form_version, 0);
  EXPECT_TRUE(condition.nullable);
  EXPECT_TRUE(fo4::schema::formlink_field_target_allowed(condition, k_avif));
  EXPECT_FALSE(fo4::schema::formlink_field_target_allowed(condition, k_equp));

  Bytes slots(128, std::byte{0});
  put(slots, 0, u32_bytes(0x0000036Cu));
  put(slots, 124, u32_bytes(0x00000370u));
  auto plugin = fo4::read_plugin(
      race_record_plugin(131, {{std::string_view{"RBPC"}, slots}}), "Legacy.esp");
  EXPECT_EQ(fo4::set_record_formlink_field(plugin, key(), condition, 7, local_key(0x0000036E)),
            fo4::FormLinkFieldMutationStatus::updated);
  EXPECT_EQ(fo4::set_record_formlink_field(plugin, key(), condition, 32, local_key(0x0000036E)),
            fo4::FormLinkFieldMutationStatus::subrecord_missing);
  EXPECT_THROW(fo4::set_record_formlink_field(plugin, key(), condition, 1, local_key(0x00000A01)),
               fo4::schema::FormLinkFieldError);

  const auto after = fo4::read_plugin(fo4::write_plugin(plugin), "Legacy.esp");
  const auto* payload = payload_of(after, k_rbpc, 0);
  ASSERT_NE(payload, nullptr);
  ASSERT_EQ(payload->size(), 128u);
  EXPECT_EQ(read_u32(*payload, 28), 0x0000036Eu);
  EXPECT_EQ(read_u32(*payload, 0), 0x0000036Cu);
  EXPECT_EQ(read_u32(*payload, 124), 0x00000370u);
  EXPECT_EQ(read_u32(*payload, 24), 0u);
}

TEST(RaceDataRows, BipedObjectConditionHidesOutsideItsFormVersionWindow) {
  Bytes slots(128, std::byte{0});
  auto legacy = fo4::read_plugin(
      race_record_plugin(77, {{std::string_view{"RBPC"}, slots}}), "Legacy.esp");
  const auto condition = link("RACE\\BipedObjectCondition");
  const auto legacy_count = fo4::record_field_count(legacy, key(), condition);
  ASSERT_TRUE(legacy_count.has_value());
  // Constraint: the count is positional, so the form-version gate only clears slot validity.
  EXPECT_EQ(*legacy_count, 32u);
  EXPECT_EQ(fo4::set_record_formlink_field(legacy, key(), condition, 0, local_key(0x0000036C)),
            fo4::FormLinkFieldMutationStatus::subrecord_missing);
  const auto kept = fo4::read_plugin(fo4::write_plugin(legacy), "Legacy.esp");
  const auto* payload = payload_of(kept, k_rbpc, 0);
  ASSERT_NE(payload, nullptr);
  EXPECT_EQ(*payload, slots);

  auto ragged = fo4::read_plugin(
      race_record_plugin(131, {{std::string_view{"RBPC"}, Bytes(130, std::byte{0})}}), "Legacy.esp");
  EXPECT_EQ(fo4::set_record_formlink_field(ragged, key(), link("RACE\\BipedObjectCondition"), 0,
                                           local_key(0x0000036C)),
            fo4::FormLinkFieldMutationStatus::subrecord_missing);

  auto boundary = fo4::read_plugin(
      race_record_plugin(78, {{std::string_view{"RBPC"}, slots}}), "Legacy.esp");
  const auto boundary_count = fo4::record_field_count(boundary, key(), condition);
  ASSERT_TRUE(boundary_count.has_value());
  EXPECT_EQ(*boundary_count, 32u);
  EXPECT_EQ(fo4::set_record_formlink_field(boundary, key(), condition, 7, local_key(0x0000036E)),
            fo4::FormLinkFieldMutationStatus::updated);
}

TEST(RaceDataRows, ModelPathAndMorphPresetCountRowsCoverTheirSubrecords) {
  const auto model = row("RACE\\ModelPath");
  EXPECT_EQ(model.scalar.subrecord, fo4::Signature::from_string("MODL"));
  EXPECT_EQ(model.scalar.storage, fo4::schema::ScalarStorage::zstring);
  EXPECT_TRUE(model.scalar.address.repeated);
  EXPECT_FALSE(model.scalar.embedded);

  const auto count = row("RACE\\MorphPresetCount");
  EXPECT_EQ(count.scalar.subrecord, fo4::Signature::from_string("MPPC"));
  EXPECT_EQ(count.scalar.storage, fo4::schema::ScalarStorage::u32);
  EXPECT_FALSE(count.scalar.embedded);
  EXPECT_TRUE(count.scalar.address.repeated);

  auto plugin = fo4::read_plugin(race_record_plugin(131, {{std::string_view{"MODL"},
                                                          text_bytes("Actors\\Human\\Body.nif")},
                                                         {std::string_view{"MPPC"}, u32_bytes(2)},
                                                         {std::string_view{"MPPC"}, u32_bytes(5)},
                                                          {std::string_view{"MPPI"}, u32_bytes(0x0000036C)}}),
                                 "Legacy.esp");
  EXPECT_EQ(read_member(plugin, "RACE\\MorphPresetCount", 0), "2");
  EXPECT_EQ(read_member(plugin, "RACE\\MorphPresetCount", 1), "5");
  EXPECT_EQ(read_member(plugin, "RACE\\MorphPresetCount", 2), "absent");
  EXPECT_EQ(fo4::set_record_scalar(plugin, key(), row("RACE\\MorphPresetCount"), 1, "7"),
            fo4::ScalarMutationStatus::updated);
  EXPECT_EQ(fo4::set_record_string(plugin, key(), model, 0, "Actors\\Human\\Head.nif"),
            fo4::ScalarMutationStatus::updated);

  // Constraint: MPPC is a raw stored count, so a write to it must not resync the MPPI array.
  const auto after = fo4::read_plugin(fo4::write_plugin(plugin), "Legacy.esp");
  EXPECT_EQ(read_member(after, "RACE\\MorphPresetCount", 1), "7");
  EXPECT_EQ(subrecord_bytes(after, "MPPI"), u32_bytes(0x0000036C));
  EXPECT_EQ(first_text(after, fo4::Signature::from_string("MODL")), "Actors\\Human\\Head.nif");
}
