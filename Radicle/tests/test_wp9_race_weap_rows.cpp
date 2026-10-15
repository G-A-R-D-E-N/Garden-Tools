#include "fo4/forms/weapon.hpp"
#include "fo4/plugin/mutation.hpp"
#include "fo4/plugin/reader.hpp"
#include "fo4/plugin/writer.hpp"
#include "fo4/schema/formlink_lists.hpp"
#include "fo4/schema/scalar.hpp"
#include "plugin_bytes.hpp"

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

using Bytes = std::vector<std::byte>;
using fo4::test::ByteWriter;

constexpr std::uint32_t k_retarget_raw = 0x01000A20;

Bytes u32_bytes(std::uint32_t value) {
  ByteWriter out;
  out.u32(value);
  return out.bytes;
}

Bytes u32_words(std::initializer_list<std::uint32_t> values) {
  ByteWriter out;
  for (const auto value : values) out.u32(value);
  return out.bytes;
}

void add_record(ByteWriter& out, std::string_view signature, std::uint32_t local,
                std::initializer_list<std::pair<std::string_view, Bytes>> subrecords) {
  const auto group = fo4::test::begin_group(out, signature, 0);
  const auto record = fo4::test::begin_record(out, signature, 0, 0x02000000u | local);
  fo4::test::write_subrecord(out, "EDID", std::string_view{"Radicle\0", 8});
  for (const auto& [subrecord, data] : subrecords) {
    fo4::test::write_subrecord(out, subrecord, data);
  }
  fo4::test::end_record(out, record);
  fo4::test::end_group(out, group);
}

ByteWriter make_plugin() {
  ByteWriter out;
  const auto tes4 = fo4::test::begin_record(out, "TES4", 0, 0);
  ByteWriter hedr;
  hedr.f32(1.0f);
  hedr.u32(4);
  hedr.u32(0x900);
  fo4::test::write_subrecord(out, "HEDR", hedr.bytes);
  fo4::test::write_subrecord(out, "MAST", std::string_view{"Fallout4.esm\0", 13});
  fo4::test::write_subrecord(out, "DATA", Bytes(8));
  fo4::test::write_subrecord(out, "MAST", std::string_view{"Objects.esp\0", 12});
  fo4::test::write_subrecord(out, "DATA", Bytes(8));
  fo4::test::end_record(out, tes4);
  add_record(out, "RACE", 0xA01,
             {{"STCP", u32_bytes(0x00000A10)},
              {"SPCT", u32_bytes(2)},
              {"SPLO", u32_bytes(0x00000A11)},
              {"SPLO", u32_bytes(0x00000A12)},
              {"BOD2", u32_bytes(7)},
              {"VNAM", u32_bytes(3)},
              {"APPR", u32_words({0x00000A30u, 0x00000A31u})}});
  add_record(out, "RACE", 0xA02, {{"DATA", Bytes(4)}});
  add_record(out, "WEAP", 0xB01,
             {{"DAMA", u32_words({0x00000C10u, 5u, 0x00000C11u, 9u})}});
  return out;
}

std::vector<Bytes> payloads(const fo4::Plugin& plugin, std::string_view key,
                            std::string_view signature) {
  std::vector<Bytes> out;
  const auto* record = fo4::find_record(plugin, fo4::FormKey::parse(key));
  if (record == nullptr) {
    ADD_FAILURE() << key;
    return out;
  }
  for (const auto& sub : record->subrecords) {
    if (sub.signature.to_string() == signature) out.push_back(sub.data);
  }
  return out;
}

Bytes payload(const fo4::Plugin& plugin, std::string_view key, std::string_view signature) {
  const auto all = payloads(plugin, key, signature);
  if (all.empty()) {
    ADD_FAILURE() << key << ' ' << signature;
    return {};
  }
  return all.front();
}

std::string signature_list(const fo4::Plugin& plugin, std::string_view key) {
  std::string out;
  const auto* record = fo4::find_record(plugin, fo4::FormKey::parse(key));
  if (record == nullptr) {
    ADD_FAILURE() << key;
    return out;
  }
  for (std::size_t index = 0; index < record->subrecords.size(); ++index) {
    if (index != 0) out += ',';
    out += record->subrecords[index].signature.to_string();
  }
  return out;
}

fo4::schema::FormLinkFieldDescriptor formlink_row(std::string_view record, std::string_view path) {
  const auto field =
      fo4::schema::formlink_field_descriptor(fo4::Signature::from_string(record), path);
  EXPECT_TRUE(field.has_value()) << path;
  return field.value_or(fo4::schema::FormLinkFieldDescriptor{});
}

fo4::schema::ScalarFieldDescriptor scalar_row(std::string_view record, std::string_view path) {
  const auto field = fo4::schema::scalar_field_descriptor(fo4::Signature::from_string(record), path);
  EXPECT_TRUE(field.has_value()) << path;
  return field.value_or(fo4::schema::ScalarFieldDescriptor{});
}

fo4::schema::FormLinkListDescriptor list_row(std::string_view record, std::string_view path) {
  const auto descriptor =
      fo4::schema::formlink_list_descriptor(fo4::Signature::from_string(record), path);
  if (!descriptor) throw std::invalid_argument("unknown FormLink list row " + std::string(path));
  return *descriptor;
}

}  // namespace

TEST(WP9RaceWeapRows, NewRowsMatchXEditShape) {
  const auto animation = formlink_row("RACE", "RACE\\AnimationSound");
  EXPECT_EQ(animation.subrecord, fo4::Signature::from_string("STCP"));
  EXPECT_FALSE(animation.nullable);
  EXPECT_EQ(animation.payload_sizes[0], 4u);
  EXPECT_TRUE(fo4::schema::formlink_field_target_allowed(animation, fo4::Signature::from_string("STAG")));

  const auto effects = formlink_row("RACE", "RACE\\ActorEffects");
  EXPECT_EQ(effects.subrecord, fo4::Signature::from_string("SPLO"));
  EXPECT_TRUE(effects.address.repeated);
  EXPECT_FALSE(effects.nullable);
  EXPECT_TRUE(fo4::schema::formlink_field_target_allowed(effects, fo4::Signature::from_string("SPEL")));
  EXPECT_TRUE(fo4::schema::formlink_field_target_allowed(effects, fo4::Signature::from_string("LVSP")));

  const auto flags = scalar_row("RACE", "RACE\\FirstPersonFlags");
  EXPECT_EQ(flags.scalar.subrecord, fo4::Signature::from_string("BOD2"));
  EXPECT_EQ(flags.scalar.storage, fo4::schema::ScalarStorage::u32);
  EXPECT_FALSE(flags.scalar.embedded);
  const auto equipment = scalar_row("RACE", "RACE\\EquipmentFlags");
  EXPECT_EQ(equipment.scalar.subrecord, fo4::Signature::from_string("VNAM"));
  EXPECT_EQ(equipment.scalar.storage, fo4::schema::ScalarStorage::u32);
  EXPECT_FALSE(equipment.scalar.embedded);

  const auto appr = list_row("RACE", "RACE\\AttachParentSlots");
  EXPECT_EQ(appr.storage, fo4::schema::FormLinkListStorage::packed_array);
  EXPECT_EQ(appr.subrecord, fo4::Signature::from_string("APPR"));
  EXPECT_FALSE(appr.nullable);

  const auto damage_type = formlink_row("WEAP", "WEAP\\DamageType");
  EXPECT_EQ(damage_type.subrecord, fo4::Signature::from_string("DAMA"));
  EXPECT_EQ(damage_type.offset, 0u);
  EXPECT_EQ(damage_type.address.stride, 8u);
  EXPECT_FALSE(damage_type.nullable);
  EXPECT_TRUE(fo4::schema::formlink_field_target_allowed(damage_type, fo4::Signature::from_string("DMGT")));
  const auto damage_amount = scalar_row("WEAP", "WEAP\\DamageAmount");
  EXPECT_EQ(damage_amount.scalar.subrecord, fo4::Signature::from_string("DAMA"));
  EXPECT_EQ(damage_amount.scalar.storage, fo4::schema::ScalarStorage::u32);
  EXPECT_EQ(damage_amount.scalar.offset, 4u);
  EXPECT_EQ(damage_amount.scalar.address.stride, 8u);
}

TEST(WP9RaceWeapRows, RaceRowsReadAndWriteTheirSubrecords) {
  const auto out = make_plugin();
  auto plugin = fo4::read_plugin(out.bytes, "Patch.esp");
  const auto key = fo4::FormKey::parse("000A01:Patch.esp");
  const auto target = fo4::FormKey::parse("000A20:Objects.esp");
  EXPECT_EQ(fo4::set_record_formlink_field(plugin, key, formlink_row("RACE", "RACE\\AnimationSound"),
                                           target),
            fo4::FormLinkFieldMutationStatus::updated);
  EXPECT_EQ(fo4::set_record_formlink_field(plugin, key, formlink_row("RACE", "RACE\\ActorEffects"),
                                           1, target),
            fo4::FormLinkFieldMutationStatus::updated);
  EXPECT_EQ(fo4::set_record_scalar(plugin, key, scalar_row("RACE", "RACE\\FirstPersonFlags"), 0, "12"),
            fo4::ScalarMutationStatus::updated);
  EXPECT_EQ(fo4::set_record_scalar(plugin, key, scalar_row("RACE", "RACE\\EquipmentFlags"), 0, "5"),
            fo4::ScalarMutationStatus::updated);
  EXPECT_EQ(fo4::schema::set_record_formlink_list_element(
                plugin, key, list_row("RACE", "RACE\\AttachParentSlots"), 1, target),
            fo4::schema::FormLinkListMutationStatus::updated);

  const auto reread = fo4::read_plugin(fo4::write_plugin(plugin), "Patch.esp");
  const std::vector<Bytes> spells = {u32_bytes(0x00000A11u), u32_bytes(k_retarget_raw)};
  const Bytes appr = u32_words({0x00000A30u, k_retarget_raw});
  EXPECT_EQ(payload(reread, "000A01:Patch.esp", "STCP"), u32_bytes(k_retarget_raw));
  EXPECT_EQ(payloads(reread, "000A01:Patch.esp", "SPLO"), spells);
  EXPECT_EQ(payload(reread, "000A01:Patch.esp", "SPCT"), u32_bytes(2));
  EXPECT_EQ(payload(reread, "000A01:Patch.esp", "BOD2"), u32_bytes(12));
  EXPECT_EQ(payload(reread, "000A01:Patch.esp", "VNAM"), u32_bytes(5));
  EXPECT_EQ(payload(reread, "000A01:Patch.esp", "APPR"), appr);
}

TEST(WP9RaceWeapRows, AttachParentSlotsInsertIntoAbsentListAnchorsAfterEdid) {
  const auto out = make_plugin();
  auto plugin = fo4::read_plugin(out.bytes, "Patch.esp");
  const auto descriptor = list_row("RACE", "RACE\\AttachParentSlots");
  const auto key = fo4::FormKey::parse("000A02:Patch.esp");
  const auto target = fo4::FormKey::parse("000A20:Objects.esp");
  const auto updated = fo4::schema::FormLinkListMutationStatus::updated;
  const std::string authored = "EDID,APPR,DATA";
  const std::string emptied = "EDID,DATA";
  const Bytes one = u32_words({k_retarget_raw});

  EXPECT_EQ(fo4::schema::record_formlink_list_size(plugin, key, descriptor), 0u);
  EXPECT_EQ(fo4::schema::insert_record_formlink_list_element(plugin, key, descriptor, 0, target),
            updated);
  EXPECT_EQ(signature_list(plugin, "000A02:Patch.esp"), authored);
  EXPECT_EQ(payload(plugin, "000A02:Patch.esp", "APPR"), one);

  EXPECT_EQ(fo4::schema::remove_record_formlink_list_element(plugin, key, descriptor, 0), updated);
  EXPECT_EQ(signature_list(plugin, "000A02:Patch.esp"), emptied);
}

TEST(WP9RaceWeapRows, DamaEntryOneValueAndLinkReadAndWrite) {
  const auto out = make_plugin();
  auto plugin = fo4::read_plugin(out.bytes, "Patch.esp");
  const auto key = fo4::FormKey::parse("000B01:Patch.esp");
  const auto target = fo4::FormKey::parse("000A20:Objects.esp");

  const auto before = fo4::forms::decode_weapon_damage_types(plugin, key);
  ASSERT_EQ(before.size(), 2u);
  EXPECT_EQ(before[0].amount, 5u);
  EXPECT_EQ(before[1].amount, 9u);
  EXPECT_EQ(before[0].type, fo4::FormKey::parse("000C10:Fallout4.esm"));

  EXPECT_EQ(fo4::set_record_formlink_field(plugin, key, formlink_row("WEAP", "WEAP\\DamageType"), 0,
                                           target),
            fo4::FormLinkFieldMutationStatus::updated);
  EXPECT_EQ(fo4::set_record_scalar(plugin, key, scalar_row("WEAP", "WEAP\\DamageAmount"), 0, "42"),
            fo4::ScalarMutationStatus::updated);

  const auto reread = fo4::read_plugin(fo4::write_plugin(plugin), "Patch.esp");
  const Bytes expected = u32_words({k_retarget_raw, 42u, 0x00000C11u, 9u});
  EXPECT_EQ(payload(reread, "000B01:Patch.esp", "DAMA"), expected);

  const auto after = fo4::forms::decode_weapon_damage_types(reread, key);
  ASSERT_EQ(after.size(), 2u);
  EXPECT_EQ(after[0].type, target);
  EXPECT_EQ(after[0].amount, 42u);
  EXPECT_EQ(after[1].type, fo4::FormKey::parse("000C11:Fallout4.esm"));
  EXPECT_EQ(after[1].amount, 9u);
}
