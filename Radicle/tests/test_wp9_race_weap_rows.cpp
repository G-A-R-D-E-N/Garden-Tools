#include "fo4/forms/weapon.hpp"
#include "fo4/graph/check_errors.hpp"
#include "fo4/plugin/mutation.hpp"
#include "fo4/plugin/reader.hpp"
#include "fo4/plugin/writer.hpp"
#include "fo4/schema/field_address.hpp"
#include "fo4/schema/formlink_lists.hpp"
#include "fo4/schema/formlink_slots.hpp"
#include "fo4/schema/preflight.hpp"
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

Bytes text_bytes(std::string_view text) {
  ByteWriter out;
  out.raw(text);
  return out.bytes;
}

Bytes u16_bytes(std::uint16_t value) {
  ByteWriter out;
  out.u16(value);
  return out.bytes;
}

Bytes neck_fat(float x, float y) {
  ByteWriter out;
  out.u32(0);
  out.f32(x);
  out.f32(y);
  return out.bytes;
}

void add_record(ByteWriter& out, std::string_view signature, std::uint32_t local,
                std::initializer_list<std::pair<std::string_view, Bytes>> subrecords,
                std::uint16_t form_version = 131) {
  const auto group = fo4::test::begin_group(out, signature, 0);
  const auto record = fo4::test::begin_record(out, signature, 0, 0x02000000u | local, form_version);
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
  add_record(out, "RACE", 0xA03,
             {{"TINL", u16_bytes(5)},
              {"MPGS", u32_words({7u, 9u, 11u})},
              {"NNAM", neck_fat(1.0f, 2.0f)},
              {"NNAM", neck_fat(3.0f, 4.0f)}});
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
  EXPECT_EQ(damage_type.max_form_version, 152u);
  EXPECT_TRUE(fo4::schema::formlink_field_target_allowed(damage_type, fo4::Signature::from_string("DMGT")));
  const auto damage_amount = scalar_row("WEAP", "WEAP\\DamageAmount");
  EXPECT_EQ(damage_amount.scalar.subrecord, fo4::Signature::from_string("DAMA"));
  EXPECT_EQ(damage_amount.scalar.storage, fo4::schema::ScalarStorage::u32);
  EXPECT_EQ(damage_amount.scalar.offset, 4u);
  EXPECT_EQ(damage_amount.scalar.address.stride, 8u);
  EXPECT_EQ(damage_amount.scalar.max_form_version, 152u);
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

TEST(WP9RaceWeapRows, DamaSecondEntryIsRowedAtFormVersion131) {
  const auto out = make_plugin();
  auto plugin = fo4::read_plugin(out.bytes, "Patch.esp");
  const auto key = fo4::FormKey::parse("000B01:Patch.esp");
  const auto target = fo4::FormKey::parse("000A20:Objects.esp");

  EXPECT_EQ(fo4::set_record_scalar(plugin, key, scalar_row("WEAP", "WEAP\\DamageAmount"), 1, "77"),
            fo4::ScalarMutationStatus::updated);
  EXPECT_EQ(fo4::set_record_formlink_field(plugin, key, formlink_row("WEAP", "WEAP\\DamageType"), 1,
                                           target),
            fo4::FormLinkFieldMutationStatus::updated);

  const auto reread = fo4::read_plugin(fo4::write_plugin(plugin), "Patch.esp");
  const Bytes expected = u32_words({0x00000C10u, 5u, k_retarget_raw, 77u});
  EXPECT_EQ(payload(reread, "000B01:Patch.esp", "DAMA"), expected);

  const auto after = fo4::forms::decode_weapon_damage_types(reread, key);
  ASSERT_EQ(after.size(), 2u);
  EXPECT_EQ(after[1].type, target);
  EXPECT_EQ(after[1].amount, 77u);
}

TEST(WP9RaceWeapRows, DamaTwelveByteEntriesAreNotRowed) {
  auto out = make_plugin();
  add_record(out, "WEAP", 0xB02,
             {{"DAMA", u32_words({0x00000C10u, 5u, 0x00000C12u, 0x00000C11u, 9u, 0x00000C12u})}}, 152);
  auto plugin = fo4::read_plugin(out.bytes, "Patch.esp");
  const auto key = fo4::FormKey::parse("000B02:Patch.esp");
  const auto target = fo4::FormKey::parse("000A20:Objects.esp");
  const Bytes before = payload(plugin, "000B02:Patch.esp", "DAMA");
  ASSERT_EQ(before.size(), 24u);

  const auto* record = fo4::find_record(plugin, key);
  ASSERT_NE(record, nullptr);
  const auto amount = scalar_row("WEAP", "WEAP\\DamageAmount").scalar;
  const auto amount_slots = fo4::schema::resolve_field_slots(
      *record, fo4::schema::scalar_field_shape(amount), amount.address);
  const auto type = formlink_row("WEAP", "WEAP\\DamageType");
  const fo4::schema::FieldShape type_shape{.subrecord = type.subrecord, .offset = type.offset,
                                           .width = sizeof(std::uint32_t),
                                           .max_form_version = type.max_form_version};
  const auto type_slots = fo4::schema::resolve_field_slots(*record, type_shape, type.address);
  ASSERT_EQ(amount_slots.slots.size(), 3u);
  ASSERT_EQ(type_slots.slots.size(), 3u);
  for (const auto& slot : amount_slots.slots) EXPECT_FALSE(slot.valid);
  for (const auto& slot : type_slots.slots) EXPECT_FALSE(slot.valid);

  EXPECT_EQ(fo4::set_record_scalar(plugin, key, scalar_row("WEAP", "WEAP\\DamageAmount"), 0, "42"),
            fo4::ScalarMutationStatus::subrecord_missing);
  EXPECT_EQ(fo4::set_record_scalar(plugin, key, scalar_row("WEAP", "WEAP\\DamageAmount"), 1, "42"),
            fo4::ScalarMutationStatus::subrecord_missing);
  EXPECT_EQ(fo4::set_record_formlink_field(plugin, key, formlink_row("WEAP", "WEAP\\DamageType"), 0, target),
            fo4::FormLinkFieldMutationStatus::subrecord_missing);
  EXPECT_EQ(payload(plugin, "000B02:Patch.esp", "DAMA"), before);
}

TEST(WP9RaceWeapRows, FormVersion152DamaCurvSlotIsNotDamageType) {
  auto out = make_plugin();
  add_record(out, "CURV", 0xC12, {});
  add_record(out, "WEAP", 0xB03,
             {{"DAMA", u32_words({0x00000C10u, 5u, 0x02000C12u, 0x00000C11u, 9u, 0x02000C12u})}}, 152);
  auto plugin = fo4::read_plugin(out.bytes, "Patch.esp");
  const auto key = fo4::FormKey::parse("000B03:Patch.esp");
  const auto* record = fo4::find_record(plugin, key);
  ASSERT_NE(record, nullptr);

  const auto fields = fo4::schema::formlink_fields(fo4::Signature::from_string("WEAP"));
  const auto dama = fo4::Signature::from_string("DAMA");
  const auto at_dmgt = fo4::schema::formlink_field_at(fields, *record, dama, 0, 0);
  const auto at_curv = fo4::schema::formlink_field_at(fields, *record, dama, 0, 8);
  EXPECT_FALSE(at_dmgt.has_value());
  EXPECT_FALSE(at_curv.has_value());

  const std::vector<const fo4::Plugin*> load_order{&plugin};
  const auto findings = fo4::check_load_order(load_order);
  std::size_t wrong_type = 0;
  for (const auto& finding : findings) {
    if (finding.code == fo4::CheckCode::wrong_target_type && finding.record == key) ++wrong_type;
  }
  EXPECT_EQ(wrong_type, 0u);
}

TEST(WP9RaceWeapRows, MorphTintAndNeckFatRowsReadAndWrite) {
  const auto tint = scalar_row("RACE", "RACE\\TotalTintCount");
  EXPECT_EQ(tint.scalar.subrecord, fo4::Signature::from_string("TINL"));
  EXPECT_EQ(tint.scalar.storage, fo4::schema::ScalarStorage::u16);
  EXPECT_FALSE(tint.scalar.embedded);
  EXPECT_FALSE(tint.scalar.address.repeated);

  const auto sliders = scalar_row("RACE", "RACE\\MorphGroupSliders");
  EXPECT_EQ(sliders.scalar.subrecord, fo4::Signature::from_string("MPGS"));
  EXPECT_EQ(sliders.scalar.storage, fo4::schema::ScalarStorage::u32);
  EXPECT_EQ(sliders.scalar.address.stride, 4u);

  const auto fat_x = scalar_row("RACE", "RACE\\NeckFatAdjustmentsScaleX");
  const auto fat_y = scalar_row("RACE", "RACE\\NeckFatAdjustmentsScaleY");
  EXPECT_EQ(fat_x.scalar.subrecord, fo4::Signature::from_string("NNAM"));
  EXPECT_EQ(fat_x.scalar.storage, fo4::schema::ScalarStorage::f32);
  EXPECT_EQ(fat_x.scalar.offset, 4u);
  EXPECT_EQ(fat_x.scalar.payload_size, 12u);
  EXPECT_TRUE(fat_x.scalar.address.repeated);
  EXPECT_EQ(fat_y.scalar.offset, 8u);
  EXPECT_EQ(fat_y.scalar.payload_size, 12u);

  const auto out = make_plugin();
  auto plugin = fo4::read_plugin(out.bytes, "Patch.esp");
  const auto key = fo4::FormKey::parse("000A03:Patch.esp");
  const auto* record = fo4::find_record(plugin, key);
  ASSERT_NE(record, nullptr);
  const auto slots = [record](const fo4::schema::ScalarFieldDescriptor& field) {
    return fo4::schema::resolve_field_slots(
        *record, fo4::schema::scalar_field_shape(field.scalar), field.scalar.address);
  };
  EXPECT_EQ(slots(sliders).slots.size(), 3u);
  EXPECT_EQ(slots(fat_x).slots.size(), 2u);
  EXPECT_EQ(slots(tint).slots.size(), 1u);

  const auto updated = fo4::ScalarMutationStatus::updated;
  const auto missing = fo4::ScalarMutationStatus::subrecord_missing;
  EXPECT_EQ(fo4::set_record_scalar(plugin, key, tint, 0, "6"), updated);
  EXPECT_EQ(fo4::set_record_scalar(plugin, key, sliders, 1, "13"), updated);
  EXPECT_EQ(fo4::set_record_scalar(plugin, key, fat_x, 1, "4.5"), updated);
  EXPECT_EQ(fo4::set_record_scalar(plugin, key, fat_y, 0, "-2"), updated);

  const auto reread = fo4::read_plugin(fo4::write_plugin(plugin), "Patch.esp");
  EXPECT_EQ(payload(reread, "000A03:Patch.esp", "TINL"), u16_bytes(6));
  EXPECT_EQ(payload(reread, "000A03:Patch.esp", "MPGS"), u32_words({7u, 13u, 11u}));
  const std::vector<Bytes> necks = payloads(reread, "000A03:Patch.esp", "NNAM");
  ASSERT_EQ(necks.size(), 2u);
  EXPECT_EQ(necks[0], neck_fat(1.0f, -2.0f));
  EXPECT_EQ(necks[1], neck_fat(4.5f, 4.0f));

  EXPECT_EQ(fo4::set_record_scalar(plugin, key, tint, 1, "1"), missing);
  EXPECT_EQ(fo4::set_record_scalar(plugin, key, sliders, 3, "1"), missing);
  EXPECT_EQ(fo4::set_record_scalar(plugin, key, fat_x, 2, "1"), missing);
  auto wrong = make_plugin();
  add_record(wrong, "RACE", 0xA04, {{"NNAM", Bytes(8)}});
  auto truncated = fo4::read_plugin(wrong.bytes, "Patch.esp");
  EXPECT_EQ(
      fo4::set_record_scalar(truncated, fo4::FormKey::parse("000A04:Patch.esp"), fat_x, 0, "1"),
      missing);
}

TEST(WP9RaceWeapRows, MovementTypeNamesAreFourByteNames) {
  const auto names = scalar_row("RACE", "RACE\\MovementTypeNames");
  EXPECT_EQ(names.scalar.subrecord, fo4::Signature::from_string("MTNM"));
  EXPECT_EQ(names.scalar.storage, fo4::schema::ScalarStorage::fixed_string);
  EXPECT_EQ(names.scalar.payload_size, 4u);
  EXPECT_EQ(names.scalar.offset, 0u);
  EXPECT_FALSE(names.scalar.embedded);
  EXPECT_TRUE(names.scalar.address.repeated);

  auto out = make_plugin();
  add_record(out, "RACE", 0xA05,
             {{"MTNM", text_bytes("WALK")},
              {"MTNM", text_bytes("RUN1")},
              {"MTNM", text_bytes("SWIM")}});
  auto plugin = fo4::read_plugin(out.bytes, "Patch.esp");
  const auto key = fo4::FormKey::parse("000A05:Patch.esp");
  const auto* record = fo4::find_record(plugin, key);
  ASSERT_NE(record, nullptr);

  const auto slots = fo4::schema::resolve_field_slots(
      *record, fo4::schema::scalar_field_shape(names.scalar), names.scalar.address);
  ASSERT_EQ(slots.slots.size(), 3u);
  const std::string expected[] = {"WALK", "RUN1", "SWIM"};
  for (std::size_t index = 0; index < 3; ++index) {
    ASSERT_TRUE(slots.slots[index].valid) << index;
    const auto bytes = fo4::schema::scalar_slot_bytes(
        record->subrecords[slots.slots[index].subrecord].data, slots.slots[index], names.scalar);
    EXPECT_EQ(fo4::schema::decode_scalar(bytes, names.scalar), expected[index]);
  }

  EXPECT_EQ(fo4::set_record_string(plugin, key, names, 1, "FLY1"),
            fo4::ScalarMutationStatus::updated);
  const auto reread = fo4::read_plugin(fo4::write_plugin(plugin), "Patch.esp");
  const std::vector<Bytes> written = {text_bytes("WALK"), text_bytes("FLY1"), text_bytes("SWIM")};
  EXPECT_EQ(payloads(reread, "000A05:Patch.esp", "MTNM"), written);
}

TEST(WP9RaceWeapRows, MovementTypeNamesRefuseAnotherWidth) {
  const auto names = scalar_row("RACE", "RACE\\MovementTypeNames");
  auto out = make_plugin();
  add_record(out, "RACE", 0xA06, {{"MTNM", text_bytes("WAL")}});
  add_record(out, "RACE", 0xA07, {{"MTNM", text_bytes("WALKS")}});
  auto plugin = fo4::read_plugin(out.bytes, "Patch.esp");
  const auto short_key = fo4::FormKey::parse("000A06:Patch.esp");
  const auto long_key = fo4::FormKey::parse("000A07:Patch.esp");
  EXPECT_THROW(fo4::record_field_count(plugin, short_key, "RACE\\MovementTypeNames"),
               fo4::schema::ScalarError);
  EXPECT_THROW(fo4::record_field_count(plugin, long_key, "RACE\\MovementTypeNames"),
               fo4::schema::ScalarError);
  EXPECT_THROW(fo4::set_record_string(plugin, short_key, names, 0, "WALK"),
               fo4::schema::ScalarError);
  EXPECT_THROW(fo4::set_record_string(plugin, long_key, names, 0, "WALK"),
               fo4::schema::ScalarError);

  auto writable = make_plugin();
  add_record(writable, "RACE", 0xA08, {{"MTNM", text_bytes("WALK")}});
  auto editable = fo4::read_plugin(writable.bytes, "Patch.esp");
  const auto key = fo4::FormKey::parse("000A08:Patch.esp");
  EXPECT_THROW(fo4::set_record_string(editable, key, names, 0, "WALKS"),
               fo4::schema::ScalarError);
  EXPECT_THROW(fo4::set_record_string(editable, key, names, 0, "WAL"),
               fo4::schema::ScalarError);
  EXPECT_EQ(fo4::set_record_string(editable, key, names, 0, "WALK"),
            fo4::ScalarMutationStatus::unchanged);
  EXPECT_EQ(payload(editable, "000A08:Patch.esp", "MTNM"), text_bytes("WALK"));
}

TEST(WP9RaceWeapRows, ZstringRowsKeepTheTerminatorRule) {
  const auto lod = scalar_row("STAT", "STAT\\DistantLodLevel0").scalar;
  EXPECT_THROW(fo4::schema::scalar_field_bytes(Bytes(260, std::byte{0x61}), lod),
               fo4::schema::ScalarError);
}

TEST(WP9RaceWeapRows, OnlyTheFirstMorphGroupSlidersRowIsAddressed) {
  auto out = make_plugin();
  add_record(out, "RACE", 0xA05,
             {{"MPGN", u32_bytes(1)},
              {"MPGS", u32_words({7u, 9u, 11u})},
              {"MPGN", u32_bytes(2)},
              {"MPGS", u32_words({20u, 21u, 22u})}});
  const auto plugin = fo4::read_plugin(out.bytes, "Patch.esp");
  const auto* record = fo4::find_record(plugin, fo4::FormKey::parse("000A05:Patch.esp"));
  ASSERT_NE(record, nullptr);

  for (const auto& issue : fo4::schema::preflight_record(*record)) {
    EXPECT_NE(issue.code, "duplicate_scalar_subrecord") << issue.path;
  }

  const auto sliders = scalar_row("RACE", "RACE\\MorphGroupSliders");
  const auto resolved = fo4::schema::resolve_field_slots(
      *record, fo4::schema::scalar_field_shape(sliders.scalar), sliders.scalar.address);
  ASSERT_EQ(resolved.slots.size(), 3u);
  const auto first = resolved.slots.front();
  EXPECT_EQ(first.subrecord, 2u);
  const auto bytes =
      fo4::schema::scalar_slot_bytes(record->subrecords[first.subrecord].data, first, sliders.scalar);
  EXPECT_EQ(fo4::schema::decode_scalar(bytes, sliders.scalar), "7");
}
