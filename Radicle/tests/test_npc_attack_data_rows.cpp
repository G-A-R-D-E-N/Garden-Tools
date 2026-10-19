#include "fo4/plugin/mutation.hpp"
#include "fo4/plugin/reader.hpp"
#include "fo4/plugin/writer.hpp"
#include "fo4/schema/formlink_lists.hpp"
#include "fo4/schema/scalar.hpp"
#include "plugin_bytes.hpp"

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <string>
#include <string_view>
#include <vector>

namespace {

using Bytes = std::vector<std::byte>;
using fo4::test::ByteWriter;

constexpr auto k_npc = fo4::Signature::from_string("NPC_");
constexpr auto k_atkd = fo4::Signature::from_string("ATKD");
constexpr auto k_spel = fo4::Signature::from_string("SPEL");

struct RowExpectation {
  std::string path;
  std::size_t offset;
  fo4::schema::ScalarStorage storage;
};

const std::array k_scalar_rows{
    RowExpectation{"NPC_\\AttackDamageMult", 0, fo4::schema::ScalarStorage::f32},
    RowExpectation{"NPC_\\AttackChance", 4, fo4::schema::ScalarStorage::f32},
    RowExpectation{"NPC_\\AttackFlags", 12, fo4::schema::ScalarStorage::u32},
    RowExpectation{"NPC_\\AttackAngle", 16, fo4::schema::ScalarStorage::f32},
    RowExpectation{"NPC_\\StrikeAngle", 20, fo4::schema::ScalarStorage::f32},
    RowExpectation{"NPC_\\AttackStagger", 24, fo4::schema::ScalarStorage::f32},
    RowExpectation{"NPC_\\AttackKnockdown", 28, fo4::schema::ScalarStorage::f32},
    RowExpectation{"NPC_\\AttackRecoveryTime", 32, fo4::schema::ScalarStorage::f32},
    RowExpectation{"NPC_\\AttackActionPointsMult", 36, fo4::schema::ScalarStorage::f32},
    RowExpectation{"NPC_\\AttackStaggerOffset", 40, fo4::schema::ScalarStorage::i32},
};

fo4::FormKey key(std::uint32_t local) {
  char text[64];
  std::snprintf(text, sizeof(text), "%06X:Patch.esp", local);
  return fo4::FormKey::parse(text);
}

fo4::FormKey npc_key() { return key(0x000800); }
fo4::FormKey spel_key() { return key(0x000A00); }

fo4::schema::ScalarFieldDescriptor scalar(std::string_view path) {
  return fo4::schema::scalar_field_descriptor(path).value();
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

Bytes atkd(float damage_mult = 1.5f) {
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

void add_group(ByteWriter& out, std::string_view signature, const std::vector<std::uint32_t>& locals) {
  const auto group = fo4::test::begin_group(out, signature, 0);
  for (const auto local : locals) {
    const auto record = fo4::test::begin_record(out, signature, 0, local);
    fo4::test::end_record(out, record);
  }
  fo4::test::end_group(out, group);
}

std::vector<std::byte> make_plugin(const std::vector<Bytes>& instances) {
  ByteWriter out;
  const auto tes4 = fo4::test::begin_record(out, "TES4", 0, 0);
  ByteWriter hedr;
  hedr.f32(1.0f);
  hedr.u32(1);
  hedr.u32(0x911);
  fo4::test::write_subrecord(out, "HEDR", hedr.bytes);
  fo4::test::end_record(out, tes4);

  const auto group = fo4::test::begin_group(out, "NPC_", 0);
  const auto record = fo4::test::begin_record(out, "NPC_", 0, 0x000800);
  for (const auto& instance : instances) {
    fo4::test::write_subrecord(out, "ATKD", instance);
  }
  fo4::test::end_record(out, record);
  fo4::test::end_group(out, group);

  add_group(out, "SPEL", {0x00000A00u});
  add_group(out, "WEAP", {0x00000A01u});
  return out.bytes;
}

Bytes attack_payload(const fo4::Plugin& plugin, std::size_t index) {
  const auto* record = fo4::find_record(plugin, npc_key());
  if (record == nullptr) return {};
  std::size_t seen = 0;
  for (const auto& subrecord : record->subrecords) {
    if (subrecord.signature != k_atkd) continue;
    if (seen == index) return subrecord.data;
    ++seen;
  }
  return {};
}

std::string read_member(const fo4::Plugin& plugin, std::string_view path, std::size_t index) {
  const auto field = scalar(path);
  const auto* record = fo4::find_record(plugin, npc_key());
  if (record == nullptr) return "absent";
  const auto resolved = fo4::schema::resolve_field_slots(
      *record, fo4::schema::scalar_field_shape(field.scalar), field.scalar.address);
  if (index >= resolved.slots.size() || !resolved.slots[index].valid) return "absent";
  const auto slot = resolved.slots[index];
  return fo4::schema::decode_scalar(
      fo4::schema::scalar_slot_bytes(record->subrecords[slot.subrecord].data, slot, field.scalar),
      field.scalar);
}

Bytes u32_bytes(std::uint32_t value) {
  ByteWriter out;
  out.u32(value);
  return out.bytes;
}

Bytes text_bytes(std::string_view text) {
  Bytes out;
  for (const char ch : text) {
    out.push_back(static_cast<std::byte>(static_cast<unsigned char>(ch)));
  }
  out.push_back(std::byte{0});
  return out;
}

std::vector<std::byte> make_attack_extra_plugin(bool short_slot = false) {
  ByteWriter out;
  const auto tes4 = fo4::test::begin_record(out, "TES4", 0, 0);
  ByteWriter hedr;
  hedr.f32(1.0f);
  hedr.u32(1);
  hedr.u32(0x911);
  fo4::test::write_subrecord(out, "HEDR", hedr.bytes);
  fo4::test::end_record(out, tes4);

  const auto group = fo4::test::begin_group(out, "NPC_", 0);
  const auto record = fo4::test::begin_record(out, "NPC_", 0, 0x000800);
  fo4::test::write_subrecord(out, "ATKD", atkd());
  fo4::test::write_subrecord(out, "ATKE", text_bytes("event_a"));
  fo4::test::write_subrecord(out, "ATKW", short_slot ? Bytes(2, std::byte{0}) : u32_bytes(0x00000A01u));
  fo4::test::write_subrecord(out, "ATKS", u32_bytes(0x00000A02u));
  fo4::test::write_subrecord(out, "ATKT", text_bytes("desc"));
  fo4::test::end_record(out, record);
  fo4::test::end_group(out, group);

  add_group(out, "EQUP", {0x00000A01u, 0x00000A02u, 0x00000A03u});
  add_group(out, "SPEL", {0x00000A00u});
  return out.bytes;
}

std::vector<std::uint32_t> raws(const fo4::Plugin& plugin, fo4::Signature signature) {
  std::vector<std::uint32_t> out;
  const auto* record = fo4::find_record(plugin, npc_key());
  if (record == nullptr) return out;
  for (const auto& subrecord : record->subrecords) {
    if (subrecord.signature != signature) continue;
    std::uint32_t value = 0;
    const std::size_t width = subrecord.data.size() < 4 ? subrecord.data.size() : 4;
    for (std::size_t i = 0; i < width; ++i) {
      value |= static_cast<std::uint32_t>(std::to_integer<std::uint8_t>(subrecord.data[i])) << (8 * i);
    }
    out.push_back(value);
  }
  return out;
}

std::string text_at(const fo4::Plugin& plugin, fo4::Signature signature) {
  const auto* record = fo4::find_record(plugin, npc_key());
  if (record == nullptr) return "absent";
  for (const auto& subrecord : record->subrecords) {
    if (subrecord.signature != signature) continue;
    std::string out;
    for (const auto byte : subrecord.data) {
      const char ch = std::to_integer<char>(byte);
      if (ch == '\0') break;
      out.push_back(ch);
    }
    return out;
  }
  return "absent";
}
}  // namespace

TEST(NpcAttackDataRows, RowsDeclareEveryAttackDataMember) {
  for (const auto& expected : k_scalar_rows) {
    const auto field = scalar(expected.path);
    EXPECT_EQ(field.record, k_npc) << expected.path;
    EXPECT_EQ(field.scalar.subrecord, k_atkd) << expected.path;
    EXPECT_EQ(field.scalar.offset, expected.offset) << expected.path;
    EXPECT_EQ(field.scalar.storage, expected.storage) << expected.path;
    EXPECT_EQ(field.scalar.payload_size, 44u) << expected.path;
    EXPECT_TRUE(field.scalar.address.repeated) << expected.path;
    EXPECT_TRUE(field.scalar.embedded) << expected.path;
    EXPECT_FALSE(field.can_insert) << expected.path;
    EXPECT_TRUE(fo4::schema::scalar_field_descriptor(k_npc, expected.path).has_value()) << expected.path;
  }

  const auto spell = link("NPC_\\AttackSpell");
  EXPECT_EQ(spell.record, k_npc);
  EXPECT_EQ(spell.subrecord, k_atkd);
  EXPECT_EQ(spell.offset, 8u);
  EXPECT_TRUE(spell.nullable);
  EXPECT_TRUE(spell.address.repeated);
  EXPECT_EQ(spell.payload_sizes[0], 44u);
  EXPECT_TRUE(fo4::schema::formlink_field_target_allowed(spell, k_spel));
  EXPECT_FALSE(fo4::schema::formlink_field_target_allowed(spell, fo4::Signature::from_string("WEAP")));
}

TEST(NpcAttackDataRows, MembersReadAndWriteAtTheirOwnOffsets) {
  auto plugin = fo4::read_plugin(make_plugin({atkd()}), "Patch.esp");
  EXPECT_EQ(read_member(plugin, "NPC_\\AttackDamageMult", 0), "1.5");
  EXPECT_EQ(read_member(plugin, "NPC_\\AttackChance", 0), "0.5");
  EXPECT_EQ(read_member(plugin, "NPC_\\AttackFlags", 0), "3");
  EXPECT_EQ(read_member(plugin, "NPC_\\StrikeAngle", 0), "15");
  EXPECT_EQ(read_member(plugin, "NPC_\\AttackStaggerOffset", 0), "-2");
  EXPECT_EQ(read_u32(attack_payload(plugin, 0), 8), 0u);

  EXPECT_EQ(fo4::set_record_scalar(plugin, npc_key(), scalar("NPC_\\AttackDamageMult"), 0, "2.5"),
            fo4::ScalarMutationStatus::updated);
  EXPECT_EQ(fo4::set_record_scalar(plugin, npc_key(), scalar("NPC_\\AttackFlags"), 0, "5"),
            fo4::ScalarMutationStatus::updated);
  EXPECT_EQ(fo4::set_record_scalar(plugin, npc_key(), scalar("NPC_\\AttackStaggerOffset"), 0, "-7"),
            fo4::ScalarMutationStatus::updated);
  EXPECT_EQ(fo4::set_record_formlink_field(plugin, npc_key(), link("NPC_\\AttackSpell"), 0, spel_key()),
            fo4::FormLinkFieldMutationStatus::updated);

  const auto after = fo4::read_plugin(fo4::write_plugin(plugin), "Patch.esp");
  EXPECT_EQ(read_member(after, "NPC_\\AttackDamageMult", 0), "2.5");
  EXPECT_EQ(read_member(after, "NPC_\\AttackFlags", 0), "5");
  EXPECT_EQ(read_member(after, "NPC_\\AttackStaggerOffset", 0), "-7");
  EXPECT_EQ(read_member(after, "NPC_\\AttackChance", 0), "0.5");
  EXPECT_EQ(read_member(after, "NPC_\\AttackAngle", 0), "30");

  const auto payload = attack_payload(after, 0);
  ASSERT_EQ(payload.size(), 44u);
  EXPECT_EQ(read_u32(payload, 8), 0x00000A00u);
  EXPECT_EQ(read_u32(payload, 4), 0x3F000000u);
  EXPECT_EQ(read_u32(payload, 16), 0x41F00000u);
  EXPECT_EQ(read_u32(payload, 12), 5u);
  EXPECT_EQ(read_u32(payload, 40), 0xFFFFFFF9u);
}

TEST(NpcAttackDataRows, SecondAttackInstanceIsAddressedByIndex) {
  auto plugin = fo4::read_plugin(make_plugin({atkd(), atkd(4.5f)}), "Patch.esp");
  EXPECT_EQ(read_member(plugin, "NPC_\\AttackDamageMult", 0), "1.5");
  EXPECT_EQ(read_member(plugin, "NPC_\\AttackDamageMult", 1), "4.5");
  EXPECT_EQ(read_member(plugin, "NPC_\\AttackDamageMult", 2), "absent");
  EXPECT_EQ(read_u32(attack_payload(plugin, 1), 8), 0u);

  EXPECT_EQ(fo4::set_record_scalar(plugin, npc_key(), scalar("NPC_\\AttackDamageMult"), 1, "7.5"),
            fo4::ScalarMutationStatus::updated);
  EXPECT_EQ(fo4::set_record_formlink_field(plugin, npc_key(), link("NPC_\\AttackSpell"), 1, spel_key()),
            fo4::FormLinkFieldMutationStatus::updated);
  EXPECT_EQ(fo4::set_record_scalar(plugin, npc_key(), scalar("NPC_\\AttackDamageMult"), 2, "9.5"),
            fo4::ScalarMutationStatus::subrecord_missing);

  const auto after = fo4::read_plugin(fo4::write_plugin(plugin), "Patch.esp");
  EXPECT_EQ(attack_payload(after, 0), atkd());
  const auto second = attack_payload(after, 1);
  ASSERT_EQ(second.size(), 44u);
  EXPECT_EQ(read_u32(second, 0), 0x40F00000u);
  EXPECT_EQ(read_u32(second, 8), 0x00000A00u);
  EXPECT_EQ(read_u32(second, 12), 3u);
  EXPECT_EQ(attack_payload(after, 2), Bytes{});
}

TEST(NpcAttackDataRows, ShortPayloadHidesEveryMember) {
  auto plugin = fo4::read_plugin(make_plugin({Bytes(24, std::byte{0})}), "Patch.esp");
  for (const auto& expected : k_scalar_rows) {
    EXPECT_EQ(read_member(plugin, expected.path, 0), "absent") << expected.path;
  }

  EXPECT_EQ(fo4::set_record_scalar(plugin, npc_key(), scalar("NPC_\\AttackFlags"), 0, "1"),
            fo4::ScalarMutationStatus::subrecord_missing);
  EXPECT_THROW(fo4::set_record_formlink_field(plugin, npc_key(), link("NPC_\\AttackSpell"), 0, spel_key()),
               fo4::schema::FormLinkFieldError);

  const auto after = fo4::read_plugin(fo4::write_plugin(plugin), "Patch.esp");
  EXPECT_EQ(attack_payload(after, 0), Bytes(24, std::byte{0}));
}

TEST(NpcAttackDataRows, WrongTargetTypeAndUnsupportedValuesAreRefused) {
  auto plugin = fo4::read_plugin(make_plugin({atkd()}), "Patch.esp");
  EXPECT_THROW(fo4::set_record_formlink_field(plugin, npc_key(), link("NPC_\\AttackSpell"), 0, key(0x000A01)),
               fo4::schema::FormLinkFieldError);
  EXPECT_EQ(fo4::set_record_formlink_field(plugin, npc_key(), link("NPC_\\AttackSpell"), 1, fo4::FormKey{}),
            fo4::FormLinkFieldMutationStatus::subrecord_missing);
  EXPECT_THROW(fo4::set_record_scalar(plugin, npc_key(), scalar("NPC_\\AttackFlags"), 0, "4294967296"),
               fo4::schema::ScalarError);
  EXPECT_EQ(attack_payload(plugin, 0), atkd());
}

TEST(NpcAttackDataRows, SlotAndTextRowsSitBesideTheAttackStruct) {
  const auto weapon = link("NPC_\\AttackWeaponSlot");
  EXPECT_EQ(weapon.record, k_npc);
  EXPECT_EQ(weapon.subrecord, fo4::Signature::from_string("ATKW"));
  EXPECT_EQ(weapon.offset, 0u);
  EXPECT_FALSE(weapon.nullable);
  EXPECT_TRUE(weapon.address.repeated);
  EXPECT_EQ(weapon.payload_sizes[0], 4u);
  EXPECT_TRUE(fo4::schema::formlink_field_target_allowed(weapon, fo4::Signature::from_string("EQUP")));
  EXPECT_FALSE(fo4::schema::formlink_field_target_allowed(weapon, k_spel));

  const auto required = link("NPC_\\AttackRequiredSlot");
  EXPECT_EQ(required.subrecord, fo4::Signature::from_string("ATKS"));
  EXPECT_EQ(required.offset, 0u);
  EXPECT_FALSE(required.nullable);
  EXPECT_TRUE(required.address.repeated);
  EXPECT_TRUE(fo4::schema::formlink_field_target_allowed(required, fo4::Signature::from_string("EQUP")));
  EXPECT_FALSE(fo4::schema::formlink_field_target_allowed(required, k_spel));

  const auto event = scalar("NPC_\\AttackEvent");
  EXPECT_EQ(event.record, k_npc);
  EXPECT_EQ(event.scalar.subrecord, fo4::Signature::from_string("ATKE"));
  EXPECT_EQ(event.scalar.storage, fo4::schema::ScalarStorage::zstring);
  EXPECT_TRUE(event.scalar.address.repeated);
  EXPECT_FALSE(event.scalar.embedded);
  EXPECT_FALSE(event.can_insert);

  const auto description = scalar("NPC_\\AttackDescription");
  EXPECT_EQ(description.scalar.subrecord, fo4::Signature::from_string("ATKT"));
  EXPECT_EQ(description.scalar.storage, fo4::schema::ScalarStorage::zstring);
  EXPECT_TRUE(description.scalar.address.repeated);
}

TEST(NpcAttackDataRows, SlotAndTextRowsReadWriteAndRefuseForeignTargets) {
  auto plugin = fo4::read_plugin(make_attack_extra_plugin(), "Patch.esp");
  EXPECT_EQ(raws(plugin, fo4::Signature::from_string("ATKW")), (std::vector<std::uint32_t>{0x00000A01u}));
  EXPECT_EQ(raws(plugin, fo4::Signature::from_string("ATKS")), (std::vector<std::uint32_t>{0x00000A02u}));
  EXPECT_EQ(text_at(plugin, fo4::Signature::from_string("ATKE")), "event_a");
  EXPECT_EQ(text_at(plugin, fo4::Signature::from_string("ATKT")), "desc");
  EXPECT_EQ(fo4::record_field_count(plugin, npc_key(), link("NPC_\\AttackWeaponSlot")), 1u);
  EXPECT_EQ(fo4::record_field_count(plugin, npc_key(), link("NPC_\\AttackRequiredSlot")), 1u);

  EXPECT_EQ(fo4::set_record_formlink_field(plugin, npc_key(), link("NPC_\\AttackWeaponSlot"), 0, key(0x000A03)),
            fo4::FormLinkFieldMutationStatus::updated);
  EXPECT_EQ(fo4::set_record_formlink_field(plugin, npc_key(), link("NPC_\\AttackRequiredSlot"), 0, key(0x000A03)),
            fo4::FormLinkFieldMutationStatus::updated);
  EXPECT_EQ(fo4::set_record_string(plugin, npc_key(), scalar("NPC_\\AttackEvent"), 0, "event_b"),
            fo4::ScalarMutationStatus::updated);
  EXPECT_EQ(fo4::set_record_string(plugin, npc_key(), scalar("NPC_\\AttackDescription"), 0, "desc2"),
            fo4::ScalarMutationStatus::updated);

  EXPECT_THROW(fo4::set_record_formlink_field(plugin, npc_key(), link("NPC_\\AttackWeaponSlot"), 0, key(0x000A00)),
               fo4::schema::FormLinkFieldError);
  EXPECT_EQ(fo4::set_record_formlink_field(plugin, npc_key(), link("NPC_\\AttackRequiredSlot"), 1, key(0x000A03)),
            fo4::FormLinkFieldMutationStatus::subrecord_missing);
  EXPECT_EQ(fo4::set_record_string(plugin, npc_key(), scalar("NPC_\\AttackEvent"), 1, "event_c"),
            fo4::ScalarMutationStatus::subrecord_missing);

  const auto after = fo4::read_plugin(fo4::write_plugin(plugin), "Patch.esp");
  EXPECT_EQ(raws(after, fo4::Signature::from_string("ATKW")), (std::vector<std::uint32_t>{0x00000A03u}));
  EXPECT_EQ(raws(after, fo4::Signature::from_string("ATKS")), (std::vector<std::uint32_t>{0x00000A03u}));
  EXPECT_EQ(text_at(after, fo4::Signature::from_string("ATKE")), "event_b");
  EXPECT_EQ(text_at(after, fo4::Signature::from_string("ATKT")), "desc2");
  EXPECT_EQ(attack_payload(after, 0), atkd());
}

TEST(NpcAttackDataRows, ShortSlotPayloadIsRefused) {
  auto plugin = fo4::read_plugin(make_attack_extra_plugin(true), "Patch.esp");
  EXPECT_THROW(fo4::set_record_formlink_field(plugin, npc_key(), link("NPC_\\AttackWeaponSlot"), 0, key(0x000A03)),
               fo4::schema::FormLinkFieldError);
  EXPECT_EQ(raws(plugin, fo4::Signature::from_string("ATKW")), (std::vector<std::uint32_t>{0x00000000u}));

  const auto after = fo4::read_plugin(fo4::write_plugin(plugin), "Patch.esp");
  EXPECT_EQ(raws(after, fo4::Signature::from_string("ATKW")).size(), 1u);
  EXPECT_EQ(text_at(after, fo4::Signature::from_string("ATKE")), "event_a");
}
