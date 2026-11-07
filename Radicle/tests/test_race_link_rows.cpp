#include "fo4/plugin/mutation.hpp"
#include "fo4/plugin/reader.hpp"
#include "fo4/plugin/writer.hpp"
#include "fo4/schema/form_links.hpp"
#include "fo4/schema/formlink_lists.hpp"
#include "fo4/schema/scalar.hpp"
#include "plugin_bytes.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string_view>
#include <utility>
#include <vector>

namespace {

using Bytes = std::vector<std::byte>;
using fo4::test::ByteWriter;

constexpr auto k_race = "RACE";
constexpr auto k_race_key = "000800:Patch.esp";
constexpr auto k_armo_key = "000D01:Patch.esp";

constexpr std::uint32_t raw_id(std::uint32_t local_id) { return 0x02000000u | local_id; }

Bytes u32_bytes(std::uint32_t value) {
  ByteWriter out;
  out.u32(value);
  return out.bytes;
}

Bytes u16_bytes(std::uint16_t value) {
  ByteWriter out;
  out.u16(value);
  return out.bytes;
}

Bytes u8_bytes(std::uint8_t value) {
  ByteWriter out;
  out.u8(value);
  return out.bytes;
}

Bytes f32_bytes(float value) {
  ByteWriter out;
  out.f32(value);
  return out.bytes;
}

Bytes zstr(std::string_view text) {
  Bytes out;
  for (const auto ch : text) out.push_back(std::byte{static_cast<std::uint8_t>(ch)});
  out.push_back(std::byte{0});
  return out;
}

Bytes sraf_bytes(std::uint16_t role, std::uint16_t perspective) {
  ByteWriter out;
  out.u16(role);
  out.u16(perspective);
  return out.bytes;
}

Bytes pair_bytes(std::uint32_t first, std::uint32_t second) {
  ByteWriter out;
  out.u32(first);
  out.u32(second);
  return out.bytes;
}

Bytes sped_bytes(float base) {
  ByteWriter out;
  for (int index = 0; index < 28; ++index) out.f32(base + static_cast<float>(index));
  return out.bytes;
}

float read_f32(const Bytes& data, std::size_t offset) {
  float value = 0.0F;
  if (data.size() < offset + 4) return value;
  std::memcpy(&value, data.data() + offset, sizeof(value));
  return value;
}

const auto k_target = fo4::FormKey::parse("000A20:Objects.esp");

void write_header(ByteWriter& out) {
  const auto tes4 = fo4::test::begin_record(out, "TES4", 0, 0);
  ByteWriter hedr;
  hedr.f32(1.0f);
  hedr.u32(2);
  hedr.u32(0x900);
  fo4::test::write_subrecord(out, "HEDR", hedr.bytes);
  fo4::test::write_subrecord(out, "MAST", std::string_view{"Fallout4.esm\0", 13});
  fo4::test::write_subrecord(out, "DATA", Bytes(8));
  fo4::test::write_subrecord(out, "MAST", std::string_view{"Objects.esp\0", 12});
  fo4::test::write_subrecord(out, "DATA", Bytes(8));
  fo4::test::end_record(out, tes4);
}

struct Sub {
  std::string_view signature;
  Bytes data;
};

Bytes plugin_with(const std::vector<Sub>& race_subrecords, bool with_local_records,
                  std::uint16_t form_version = 131) {
  ByteWriter out;
  write_header(out);

  const auto group = fo4::test::begin_group(out, k_race, 0);
  const auto record = fo4::test::begin_record(out, k_race, 0, raw_id(0x0800), form_version);
  fo4::test::write_subrecord(out, "EDID", std::string_view{"Legacy\0", 7});
  for (const auto& sub : race_subrecords) {
    fo4::test::write_subrecord(out, sub.signature, sub.data);
  }
  fo4::test::end_record(out, record);
  fo4::test::end_group(out, group);

  if (with_local_records) {
    for (const auto& entry : {std::pair{"ARMO", 0x0D01u}, std::pair{"TXST", 0x0D02u}}) {
      const auto local_group = fo4::test::begin_group(out, entry.first, 0);
      const auto local = fo4::test::begin_record(out, entry.first, 0, raw_id(entry.second));
      fo4::test::write_subrecord(out, "EDID", std::string_view{"RadicleTarget\0", 14});
      fo4::test::end_record(out, local);
      fo4::test::end_group(out, local_group);
    }
  }
  return out.bytes;
}

Bytes live_subrecord(const fo4::Plugin& plugin, std::string_view signature, std::size_t ordinal) {
  const auto* record = fo4::find_record(plugin, fo4::FormKey::parse(k_race_key));
  if (record == nullptr) return {};
  for (const auto& sub : record->subrecords) {
    if (sub.signature.to_string() != signature) continue;
    if (ordinal-- == 0) return sub.data;
  }
  return {};
}

fo4::schema::FormLinkFieldDescriptor row(std::string_view path) {
  const auto descriptor =
      fo4::schema::formlink_field_descriptor(fo4::Signature::from_string(k_race), path);
  EXPECT_TRUE(descriptor.has_value()) << path;
  return descriptor.value_or(fo4::schema::FormLinkFieldDescriptor{});
}

fo4::schema::ScalarFieldDescriptor scalar_row(std::string_view path) {
  const auto descriptor =
      fo4::schema::scalar_field_descriptor(fo4::Signature::from_string(k_race), path);
  EXPECT_TRUE(descriptor.has_value()) << path;
  return descriptor.value_or(fo4::schema::ScalarFieldDescriptor{});
}

std::uint32_t raw_at(const Bytes& data, std::size_t offset) {
  std::uint32_t value = 0;
  if (data.size() < offset + 4) return 0;
  std::memcpy(&value, data.data() + offset, sizeof(value));
  return value;
}

std::string read_scalar_member(const fo4::Plugin& plugin,
                               std::string_view path,
                               std::size_t index) {
  const auto field = scalar_row(path);
  const auto* record = fo4::find_record(plugin, fo4::FormKey::parse(k_race_key));
  if (record == nullptr) return {};
  const auto resolved = fo4::schema::resolve_field_slots(
      *record, fo4::schema::scalar_field_shape(field.scalar), field.scalar.address);
  if (index >= resolved.slots.size() || !resolved.slots[index].valid) return {};
  const auto slot = resolved.slots[index];
  return fo4::schema::decode_scalar(
      fo4::schema::scalar_slot_bytes(record->subrecords[slot.subrecord].data, slot, field.scalar),
      field.scalar);
}

}  // namespace

TEST(RaceLinkRows, HaveXEditSubrecordsTargetsAndShapes) {
  const auto movement = row("RACE\\MovementType");
  EXPECT_EQ(movement.subrecord, fo4::Signature::from_string("MTYP"));
  EXPECT_TRUE(movement.address.repeated);
  EXPECT_EQ(movement.payload_sizes[0], 4u);
  EXPECT_EQ(fo4::schema::formlink_field_targets(movement)[0], fo4::Signature::from_string("MOVT"));

  const auto slot = row("RACE\\EquipSlot");
  EXPECT_EQ(slot.subrecord, fo4::Signature::from_string("QNAM"));
  EXPECT_TRUE(slot.address.repeated);
  EXPECT_EQ(slot.payload_sizes[0], 4u);
  EXPECT_EQ(fo4::schema::formlink_field_targets(slot)[0], fo4::Signature::from_string("EQUP"));
}

TEST(RaceLinkRows, VoiceAndHairColorSlotsEditTheirOwnHalf) {
  auto plugin = fo4::read_plugin(plugin_with({{"HCLF", pair_bytes(0x01000A21u, 0x01000A22u)},
                                              {"VTCK", pair_bytes(0x01000A23u, 0x01000A24u)}},
                                             true),
                                "Patch.esp");
  const auto key = fo4::FormKey::parse(k_race_key);

  const std::pair<std::string_view, std::string_view> slots[] = {
      {"RACE\\HairColorMale", "HCLF"}, {"RACE\\HairColorFemale", "HCLF"},
      {"RACE\\VoiceTypeMale", "VTCK"}, {"RACE\\VoiceTypeFemale", "VTCK"}};
  for (const auto& [path, subrecord] : slots) {
    const auto field = row(path);
    EXPECT_EQ(field.subrecord, fo4::Signature::from_string(subrecord)) << path;
    EXPECT_FALSE(field.address.repeated) << path;
    EXPECT_EQ(field.payload_sizes[0], 0u) << path;
    EXPECT_EQ(fo4::record_field_count(plugin, key, field), 1u) << path;
    EXPECT_EQ(fo4::schema::formlink_field_targets(field).size(), 1u) << path;
  }
  EXPECT_EQ(row("RACE\\HairColorMale").offset, 0u);
  EXPECT_EQ(row("RACE\\HairColorFemale").offset, 4u);
  EXPECT_EQ(row("RACE\\VoiceTypeMale").offset, 0u);
  EXPECT_EQ(row("RACE\\VoiceTypeFemale").offset, 4u);
  EXPECT_FALSE(row("RACE\\VoiceTypeMale").nullable);
  EXPECT_EQ(fo4::schema::formlink_field_targets(row("RACE\\VoiceTypeFemale"))[0],
            fo4::Signature::from_string("VTYP"));
  EXPECT_TRUE(row("RACE\\HairColorMale").nullable);
  EXPECT_EQ(fo4::schema::formlink_field_targets(row("RACE\\HairColorFemale"))[0],
            fo4::Signature::from_string("CLFM"));
  EXPECT_EQ(raw_at(live_subrecord(plugin, "VTCK", 0), 0), 0x01000A23u);
  EXPECT_EQ(raw_at(live_subrecord(plugin, "VTCK", 0), 4), 0x01000A24u);

  EXPECT_EQ(fo4::set_record_formlink_field(plugin, key, row("RACE\\VoiceTypeFemale"), k_target),
            fo4::FormLinkFieldMutationStatus::updated);
  EXPECT_EQ(fo4::set_record_formlink_field(plugin, key, row("RACE\\HairColorMale"), k_target),
            fo4::FormLinkFieldMutationStatus::updated);
  EXPECT_THROW(fo4::set_record_formlink_field(plugin, key, row("RACE\\VoiceTypeFemale"),
                                             fo4::FormKey::parse(k_armo_key)),
               fo4::schema::FormLinkFieldError);
  EXPECT_THROW(fo4::set_record_formlink_field(plugin, key, row("RACE\\HairColorFemale"),
                                             fo4::FormKey::parse(k_armo_key)),
               fo4::schema::FormLinkFieldError);

  const auto reread = fo4::read_plugin(fo4::write_plugin(plugin), "Patch.esp");
  EXPECT_EQ(raw_at(live_subrecord(reread, "VTCK", 0), 0), 0x01000A23u);
  EXPECT_EQ(raw_at(live_subrecord(reread, "VTCK", 0), 4), 0x01000A20u);
  EXPECT_EQ(raw_at(live_subrecord(reread, "HCLF", 0), 0), 0x01000A20u);
  EXPECT_EQ(raw_at(live_subrecord(reread, "HCLF", 0), 4), 0x01000A22u);
}

TEST(RaceLinkRows, RefuseAVoiceSlotThatDoesNotFitThePayload) {
  auto plugin = fo4::read_plugin(plugin_with({{"VTCK", u32_bytes(0x01000A23u)}}, true), "Patch.esp");
  const auto key = fo4::FormKey::parse(k_race_key);
  EXPECT_EQ(fo4::set_record_formlink_field(plugin, key, row("RACE\\VoiceTypeMale"), k_target),
            fo4::FormLinkFieldMutationStatus::updated);
  EXPECT_THROW(fo4::set_record_formlink_field(plugin, key, row("RACE\\VoiceTypeFemale"), k_target),
               fo4::schema::FormLinkFieldError);
}

TEST(RaceLinkRows, CollectsTheNewSubrecordsAsFormLinks) {
  const auto bytes = plugin_with({{"MTYP", u32_bytes(0x01000A21u)},
                                  {"MTYP", u32_bytes(0x01000A22u)},
                                  {"QNAM", u32_bytes(0x01000A23u)}},
                                 false);
  auto plugin = fo4::read_plugin(bytes, "Patch.esp");
  const auto hits = fo4::collect_form_links(plugin);

  const auto count = [&](std::string_view signature) {
    return static_cast<std::size_t>(
        std::count_if(hits.begin(), hits.end(), [&](const fo4::FormLinkHit& hit) {
          return hit.subrecord.to_string() == signature && hit.offset == 0;
        }));
  };
  EXPECT_EQ(count("MTYP"), 2u);
  EXPECT_EQ(count("QNAM"), 1u);
}

TEST(RaceLinkRows, WritesAndRereadsEachLink) {
  const auto bytes = plugin_with({{"MTYP", u32_bytes(0x01000A21u)},
                                  {"MTYP", u32_bytes(0x01000A22u)},
                                  {"QNAM", u32_bytes(0x01000A23u)}},
                                 false);
  auto plugin = fo4::read_plugin(bytes, "Patch.esp");
  const auto key = fo4::FormKey::parse(k_race_key);

  EXPECT_EQ(fo4::set_record_formlink_field(plugin, key, row("RACE\\EquipSlot"), 0, k_target),
            fo4::FormLinkFieldMutationStatus::updated);
  EXPECT_EQ(fo4::set_record_formlink_field(plugin, key, row("RACE\\MovementType"), 1, k_target),
            fo4::FormLinkFieldMutationStatus::updated);
  EXPECT_EQ(fo4::record_field_count(plugin, key, "RACE\\MovementType"), 2u);

  const auto reread = fo4::read_plugin(fo4::write_plugin(plugin), "Patch.esp");
  EXPECT_EQ(raw_at(live_subrecord(reread, "MTYP", 0), 0), 0x01000A21u);
  EXPECT_EQ(raw_at(live_subrecord(reread, "MTYP", 1), 0), 0x01000A20u);
  EXPECT_EQ(raw_at(live_subrecord(reread, "QNAM", 0), 0), 0x01000A20u);
}

TEST(RaceLinkRows, EquipSlotNodesAreEditableStrings) {
  auto plugin = fo4::read_plugin(
      plugin_with({{"QNAM", u32_bytes(0x01000A23u)}, {"ZNAM", zstr("Weapon")}}, false),
      "Patch.esp");
  const auto key = fo4::FormKey::parse(k_race_key);

  EXPECT_EQ(fo4::record_field_count(plugin, key, "RACE\\EquipSlotNodes"), 1u);
  EXPECT_EQ(fo4::set_record_string(plugin, key, "RACE\\EquipSlotNodes", 0, "Shield"),
            fo4::ScalarMutationStatus::updated);
  EXPECT_EQ(fo4::set_record_string(plugin, key, "RACE\\EquipSlotNodes", 1, "Shield"),
            fo4::ScalarMutationStatus::subrecord_missing);

  const auto reread = fo4::read_plugin(fo4::write_plugin(plugin), "Patch.esp");
  EXPECT_EQ(live_subrecord(reread, "ZNAM", 0), zstr("Shield"));
  EXPECT_EQ(raw_at(live_subrecord(reread, "QNAM", 0), 0), 0x01000A23u);
}

TEST(RaceLinkRows, RefuseWrongTargetTypes) {
  const auto key = fo4::FormKey::parse(k_race_key);
  const auto movement = row("RACE\\MovementType");
  EXPECT_FALSE(fo4::schema::formlink_field_target_allowed(movement, fo4::Signature::from_string("ARMO")));

  auto typed = fo4::read_plugin(plugin_with({{"MTYP", u32_bytes(0x01000A21u)}}, true), "Patch.esp");
  EXPECT_THROW(fo4::set_record_formlink_field(typed, key, movement, fo4::FormKey::parse(k_armo_key)),
               fo4::schema::FormLinkFieldError);
  EXPECT_EQ(raw_at(live_subrecord(typed, "MTYP", 0), 0), 0x01000A21u);
}

TEST(RaceMorphRows, RowsMatchXEditSubrecordsStorageAndNames) {
  const auto indices = scalar_row("RACE\\FaceMorphIndices");
  EXPECT_EQ(indices.scalar.subrecord, fo4::Signature::from_string("FMRI"));
  EXPECT_EQ(indices.scalar.storage, fo4::schema::ScalarStorage::u32);
  EXPECT_TRUE(indices.scalar.address.repeated);

  const auto mask = scalar_row("RACE\\MorphGroupMask");
  EXPECT_EQ(mask.scalar.subrecord, fo4::Signature::from_string("MPPK"));
  EXPECT_EQ(mask.scalar.storage, fo4::schema::ScalarStorage::u16);
  EXPECT_EQ(mask.scalar.presentation, fo4::schema::ScalarPresentation::enumeration);
  EXPECT_EQ(mask.scalar.names.size(), 15u);
  EXPECT_EQ(fo4::schema::scalar_name_value(mask.scalar, "None").value_or(0), 65535);
  EXPECT_EQ(fo4::schema::scalar_name_value(mask.scalar, "Female Neck Mask").value_or(0), 1227);
  EXPECT_FALSE(fo4::schema::scalar_name_value(mask.scalar, "Not A Mask").has_value());

  const auto groups = scalar_row("RACE\\MorphGroupNames");
  EXPECT_EQ(groups.scalar.subrecord, fo4::Signature::from_string("MPGN"));
  EXPECT_EQ(groups.scalar.storage, fo4::schema::ScalarStorage::zstring);
  EXPECT_TRUE(groups.scalar.address.repeated);

  const auto playable = scalar_row("RACE\\MorphPresetPlayable");
  EXPECT_EQ(playable.scalar.subrecord, fo4::Signature::from_string("MPPF"));
  EXPECT_EQ(playable.scalar.storage, fo4::schema::ScalarStorage::u8);
  EXPECT_EQ(playable.scalar.maximum, 1.0);
  EXPECT_TRUE(playable.scalar.address.repeated);

  const auto last = scalar_row("RACE\\MorphLastIndex");
  EXPECT_EQ(last.scalar.subrecord, fo4::Signature::from_string("MLSI"));
  EXPECT_EQ(last.scalar.storage, fo4::schema::ScalarStorage::u32);
  EXPECT_FALSE(last.scalar.address.repeated);

  const std::pair<std::string_view, std::string_view> integers[] = {
      {"RACE\\MorphPresetIndices", "MPPI"}, {"RACE\\MorphValueIndices", "MSID"}};
  for (const auto& [path, subrecord] : integers) {
    const auto field = scalar_row(path);
    EXPECT_EQ(field.scalar.subrecord, fo4::Signature::from_string(subrecord)) << path;
    EXPECT_EQ(field.scalar.storage, fo4::schema::ScalarStorage::u32) << path;
    EXPECT_TRUE(field.scalar.address.repeated) << path;
  }

  const std::pair<std::string_view, std::string_view> strings[] = {
      {"RACE\\MorphPresetMorphs", "MPPM"}, {"RACE\\MorphValueMinNames", "MSM0"},
      {"RACE\\MorphValueMaxNames", "MSM1"}, {"RACE\\SubgraphAnimationPaths", "SAPT"},
      {"RACE\\SubgraphBehaviorGraphs", "SGNM"}};
  for (const auto& [path, subrecord] : strings) {
    const auto field = scalar_row(path);
    EXPECT_EQ(field.scalar.subrecord, fo4::Signature::from_string(subrecord)) << path;
    EXPECT_EQ(field.scalar.storage, fo4::schema::ScalarStorage::zstring) << path;
    EXPECT_TRUE(field.scalar.address.repeated) << path;
  }

  const auto role = scalar_row("RACE\\SubgraphRole");
  EXPECT_EQ(role.scalar.subrecord, fo4::Signature::from_string("SRAF"));
  EXPECT_EQ(role.scalar.storage, fo4::schema::ScalarStorage::u16);
  EXPECT_EQ(role.scalar.presentation, fo4::schema::ScalarPresentation::enumeration);
  EXPECT_TRUE(role.scalar.embedded);
  EXPECT_TRUE(role.scalar.address.repeated);
  EXPECT_EQ(role.scalar.payload_size, 4u);
  EXPECT_EQ(role.scalar.offset, 0u);
  EXPECT_EQ(fo4::schema::scalar_name_value(role.scalar, "Pipboy").value_or(-1), 4);
  EXPECT_FALSE(fo4::schema::scalar_name_value(role.scalar, "Rifle").has_value());

  const auto perspective = scalar_row("RACE\\SubgraphPerspective");
  EXPECT_EQ(perspective.scalar.subrecord, fo4::Signature::from_string("SRAF"));
  EXPECT_EQ(perspective.scalar.storage, fo4::schema::ScalarStorage::u16);
  EXPECT_EQ(perspective.scalar.presentation, fo4::schema::ScalarPresentation::enumeration);
  EXPECT_TRUE(perspective.scalar.embedded);
  EXPECT_TRUE(perspective.scalar.address.repeated);
  EXPECT_EQ(perspective.scalar.payload_size, 4u);
  EXPECT_EQ(perspective.scalar.offset, 2u);
  EXPECT_EQ(fo4::schema::scalar_name_value(perspective.scalar, "1st").value_or(-1), 1);
  EXPECT_FALSE(fo4::schema::scalar_name_value(perspective.scalar, "2nd").has_value());

  const auto texture = row("RACE\\MorphPresetTexture");
  EXPECT_EQ(texture.subrecord, fo4::Signature::from_string("MPPT"));
  EXPECT_TRUE(texture.address.repeated);
  EXPECT_FALSE(texture.nullable);
  EXPECT_EQ(fo4::schema::formlink_field_targets(texture).size(), 1u);
  EXPECT_EQ(fo4::schema::formlink_field_targets(texture)[0], fo4::Signature::from_string("TXST"));
}

TEST(RaceMorphRows, WritesMorphAndSubgraphValuesInPlace) {
  auto plugin = fo4::read_plugin(plugin_with({{"FMRI", u32_bytes(0x12)},
                                              {"MSID", u32_bytes(7)},
                                              {"MPPK", u16_bytes(1224)},
                                              {"MPPF", u8_bytes(1)},
                                              {"MPGN", zstr("Brow")},
                                              {"SAPT", zstr("Anim\\Idle")},
                                              {"SRAF", sraf_bytes(1, 0)},
                                              {"SRAF", sraf_bytes(2, 1)},
                                              {"MPPT", u32_bytes(0x01000D02u)}},
                                             false),
                                "Patch.esp");
  const auto key = fo4::FormKey::parse(k_race_key);
  const auto mask = scalar_row("RACE\\MorphGroupMask");
  EXPECT_EQ(fo4::schema::decode_scalar(live_subrecord(plugin, "MPPK", 0), mask.scalar), "1224");
  EXPECT_EQ(fo4::schema::scalar_value_name(mask.scalar, 1224).value_or(""), "Female Ears Mask");

  EXPECT_EQ(fo4::set_record_scalar(plugin, key, scalar_row("RACE\\FaceMorphIndices"), 0, "9"),
            fo4::ScalarMutationStatus::updated);
  EXPECT_EQ(fo4::set_record_scalar(plugin, key, mask, 0, "65535"),
            fo4::ScalarMutationStatus::updated);
  EXPECT_EQ(fo4::set_record_scalar(plugin, key, scalar_row("RACE\\MorphValueIndices"), 0, "12"),
            fo4::ScalarMutationStatus::updated);
  EXPECT_EQ(fo4::set_record_scalar(plugin, key, scalar_row("RACE\\MorphPresetPlayable"), 0, "0"),
            fo4::ScalarMutationStatus::updated);
  EXPECT_EQ(fo4::set_record_string(plugin, key, scalar_row("RACE\\SubgraphAnimationPaths"), 0,
                                   "Anim\\Walk"),
            fo4::ScalarMutationStatus::updated);
  EXPECT_EQ(fo4::set_record_formlink_field(plugin, key, row("RACE\\MorphPresetTexture"), k_target),
            fo4::FormLinkFieldMutationStatus::updated);

  const auto role = scalar_row("RACE\\SubgraphRole");
  const auto perspective = scalar_row("RACE\\SubgraphPerspective");
  EXPECT_EQ(fo4::record_field_count(plugin, key, role), 2u);
  EXPECT_EQ(read_scalar_member(plugin, "RACE\\SubgraphRole", 1), "2");
  EXPECT_EQ(read_scalar_member(plugin, "RACE\\SubgraphPerspective", 1), "1");
  EXPECT_EQ(fo4::set_record_scalar(plugin, key, role, 1, "3"),
            fo4::ScalarMutationStatus::updated);
  EXPECT_EQ(fo4::set_record_scalar(plugin, key, perspective, 0, "1"),
            fo4::ScalarMutationStatus::updated);

  EXPECT_EQ(fo4::set_record_scalar(plugin, key, scalar_row("RACE\\MorphLastIndex"), 0, "4"),
            fo4::ScalarMutationStatus::subrecord_missing);
  EXPECT_EQ(fo4::set_record_scalar(plugin, key, role, 2, "1"),
            fo4::ScalarMutationStatus::subrecord_missing);
  EXPECT_THROW(fo4::set_record_scalar(plugin, key, mask, 0, "65536"), fo4::schema::ScalarError);
  EXPECT_THROW(fo4::set_record_scalar(plugin, key, perspective, 0, "2"), fo4::schema::ScalarError);

  const auto reread = fo4::read_plugin(fo4::write_plugin(plugin), "Patch.esp");
  EXPECT_EQ(raw_at(live_subrecord(reread, "FMRI", 0), 0), 9u);
  EXPECT_EQ(fo4::schema::decode_scalar(live_subrecord(reread, "MPPK", 0), mask.scalar), "65535");
  EXPECT_EQ(live_subrecord(reread, "MPPF", 0), u8_bytes(0));
  EXPECT_EQ(raw_at(live_subrecord(reread, "MSID", 0), 0), 12u);
  EXPECT_EQ(live_subrecord(reread, "SAPT", 0), zstr("Anim\\Walk"));
  EXPECT_EQ(live_subrecord(reread, "MPGN", 0), zstr("Brow"));
  EXPECT_EQ(live_subrecord(reread, "SRAF", 0), sraf_bytes(1, 1));
  EXPECT_EQ(live_subrecord(reread, "SRAF", 1), sraf_bytes(3, 1));
  EXPECT_EQ(raw_at(live_subrecord(reread, "MPPT", 0), 0), 0x01000A20u);
}

TEST(RaceScalarRows, FaceGenClampsChatterAndWrinkleMapsEditInPlace) {
  auto plugin = fo4::read_plugin(
      plugin_with({{"PNAM", f32_bytes(1.5f)},
                   {"UNAM", f32_bytes(0.25f)},
                   {"PTOP", f32_bytes(3.0f)},
                   {"NTOP", f32_bytes(9.0f)},
                   {"WMAP", zstr("Actors\\Character\\MaleWrinkle.dds")},
                   {"WMAP", zstr("Actors\\Character\\FemaleWrinkle.dds")}},
                  false),
      "Patch.esp");
  const auto key = fo4::FormKey::parse(k_race_key);

  const std::pair<std::string_view, std::string_view> clamps[] = {
      {"RACE\\FaceGenMainClamp", "PNAM"},
      {"RACE\\FaceGenFaceClamp", "UNAM"},
      {"RACE\\IdleChatterTimeMin", "PTOP"},
      {"RACE\\IdleChatterTimeMax", "NTOP"}};
  for (const auto& [path, subrecord] : clamps) {
    const auto field = scalar_row(path);
    EXPECT_EQ(field.scalar.subrecord, fo4::Signature::from_string(subrecord)) << path;
    EXPECT_EQ(field.scalar.storage, fo4::schema::ScalarStorage::f32) << path;
    EXPECT_FALSE(field.scalar.embedded) << path;
    EXPECT_FALSE(field.scalar.address.repeated) << path;
  }
  EXPECT_EQ(read_scalar_member(plugin, "RACE\\FaceGenMainClamp", 0), "1.5");
  EXPECT_EQ(read_scalar_member(plugin, "RACE\\FaceGenFaceClamp", 0), "0.25");
  EXPECT_EQ(read_scalar_member(plugin, "RACE\\IdleChatterTimeMin", 0), "3");
  EXPECT_EQ(read_scalar_member(plugin, "RACE\\IdleChatterTimeMax", 0), "9");
  EXPECT_EQ(fo4::record_field_count(plugin, key, "RACE\\WrinkleMapPath"), 2u);
  EXPECT_EQ(read_scalar_member(plugin, "RACE\\WrinkleMapPath", 0),
            "Actors\\Character\\MaleWrinkle.dds");
  EXPECT_EQ(read_scalar_member(plugin, "RACE\\WrinkleMapPath", 1),
            "Actors\\Character\\FemaleWrinkle.dds");

  EXPECT_EQ(fo4::set_record_scalar(plugin, key, scalar_row("RACE\\FaceGenMainClamp"), 0, "-2.5"),
            fo4::ScalarMutationStatus::updated);
  EXPECT_EQ(fo4::set_record_scalar(plugin, key, scalar_row("RACE\\IdleChatterTimeMax"), 0, "12"),
            fo4::ScalarMutationStatus::updated);
  EXPECT_EQ(fo4::set_record_string(plugin, key, scalar_row("RACE\\WrinkleMapPath"), 1,
                                   "Actors\\Character\\Other.dds"),
            fo4::ScalarMutationStatus::updated);
  EXPECT_EQ(fo4::set_record_scalar(plugin, key, scalar_row("RACE\\FaceGenFaceClamp"), 0, "0.25"),
            fo4::ScalarMutationStatus::unchanged);

  EXPECT_EQ(fo4::set_record_scalar(plugin, key, scalar_row("RACE\\IdleChatterTimeMin"), 1, "0"),
            fo4::ScalarMutationStatus::subrecord_missing);
  EXPECT_EQ(read_scalar_member(plugin, "RACE\\FaceGenMainClamp", 1), "");

  const auto reread = fo4::read_plugin(fo4::write_plugin(plugin), "Patch.esp");
  EXPECT_EQ(live_subrecord(reread, "PNAM", 0), f32_bytes(-2.5f));
  EXPECT_EQ(live_subrecord(reread, "UNAM", 0), f32_bytes(0.25f));
  EXPECT_EQ(live_subrecord(reread, "NTOP", 0), f32_bytes(12.0f));
  EXPECT_EQ(live_subrecord(reread, "WMAP", 0), zstr("Actors\\Character\\MaleWrinkle.dds"));
  EXPECT_EQ(live_subrecord(reread, "WMAP", 1), zstr("Actors\\Character\\Other.dds"));
}

TEST(RaceScalarRows, SkeletalModelPathsEditInPlace) {
  auto plugin = fo4::read_plugin(
      plugin_with({{"ANAM", zstr("Actors\\Character\\MaleSkeleton.nif")},
                   {"ANAM", zstr("Actors\\Character\\FemaleSkeleton.nif")}},
                  false),
      "Patch.esp");
  const auto key = fo4::FormKey::parse(k_race_key);

  const auto model = scalar_row("RACE\\SkeletalModelPath");
  EXPECT_EQ(model.scalar.subrecord, fo4::Signature::from_string("ANAM"));
  EXPECT_EQ(model.scalar.storage, fo4::schema::ScalarStorage::zstring);
  EXPECT_TRUE(model.scalar.address.repeated);

  EXPECT_EQ(fo4::record_field_count(plugin, key, "RACE\\SkeletalModelPath"), 2u);
  EXPECT_EQ(read_scalar_member(plugin, "RACE\\SkeletalModelPath", 0),
            "Actors\\Character\\MaleSkeleton.nif");
  EXPECT_EQ(read_scalar_member(plugin, "RACE\\SkeletalModelPath", 1),
            "Actors\\Character\\FemaleSkeleton.nif");

  EXPECT_EQ(fo4::set_record_string(plugin, key, model, 2, "Actors\\Character\\Other.nif"),
            fo4::ScalarMutationStatus::subrecord_missing);
  EXPECT_EQ(read_scalar_member(plugin, "RACE\\SkeletalModelPath", 2), "");
  EXPECT_EQ(fo4::set_record_string(plugin, key, model, 1, "Actors\\Character\\Other.nif"),
            fo4::ScalarMutationStatus::updated);
  EXPECT_EQ(fo4::set_record_string(plugin, key, model, 2, "Actors\\Character\\Third.nif"),
            fo4::ScalarMutationStatus::subrecord_missing);
  EXPECT_EQ(read_scalar_member(plugin, "RACE\\SkeletalModelPath", 2), "");

  const auto reread = fo4::read_plugin(fo4::write_plugin(plugin), "Patch.esp");
  EXPECT_EQ(live_subrecord(reread, "ANAM", 0), zstr("Actors\\Character\\MaleSkeleton.nif"));
  EXPECT_EQ(live_subrecord(reread, "ANAM", 1), zstr("Actors\\Character\\Other.nif"));
}

TEST(RaceScalarRows, RefuseANonFloatPayloadForAWholePayloadRow) {
  auto plugin = fo4::read_plugin(plugin_with({{"PNAM", u16_bytes(1)}}, true), "Patch.esp");
  const auto key = fo4::FormKey::parse(k_race_key);
  const auto before = fo4::write_plugin(plugin);
  EXPECT_THROW(fo4::record_field_count(plugin, key, "RACE\\FaceGenMainClamp"),
               fo4::schema::ScalarError);
  EXPECT_THROW(fo4::set_record_scalar(plugin, key, scalar_row("RACE\\FaceGenMainClamp"), 0, "1"),
               fo4::schema::ScalarError);
  EXPECT_EQ(fo4::write_plugin(plugin), before);
  EXPECT_EQ(live_subrecord(plugin, "PNAM", 0), u16_bytes(1));
}

TEST(RaceMorphRows, RejectASubgraphFlagsPayloadOfTheWrongWidth) {
  auto plugin = fo4::read_plugin(plugin_with({{"SRAF", u16_bytes(1)}}, false), "Patch.esp");
  const auto key = fo4::FormKey::parse(k_race_key);
  EXPECT_EQ(read_scalar_member(plugin, "RACE\\SubgraphRole", 0), "");
  EXPECT_EQ(fo4::set_record_scalar(plugin, key, scalar_row("RACE\\SubgraphRole"), 0, "3"),
            fo4::ScalarMutationStatus::subrecord_missing);
  EXPECT_EQ(live_subrecord(plugin, "SRAF", 0), u16_bytes(1));
}

TEST(RaceLinkRows, CollarTextureSetIsTheNam2AfterTheEmptyMarkers) {
  auto plugin = fo4::read_plugin(
      plugin_with({{"NAM2", Bytes{}}, {"NAM2", Bytes{}}, {"NAM2", u32_bytes(0x01000A21u)}}, false),
      "Patch.esp");
  const auto key = fo4::FormKey::parse(k_race_key);
  const auto collar = row("RACE\\CollarTextureSet");
  EXPECT_EQ(collar.subrecord, fo4::Signature::from_string("NAM2"));
  EXPECT_TRUE(collar.address.repeated);
  EXPECT_EQ(collar.payload_sizes[0], 0u);
  EXPECT_EQ(fo4::schema::formlink_field_targets(collar)[0], fo4::Signature::from_string("TXST"));
  EXPECT_EQ(fo4::record_field_count(plugin, key, "RACE\\CollarTextureSet"), 3u);
  EXPECT_THROW(fo4::set_record_formlink_field(plugin, key, collar, 0, k_target),
               fo4::schema::FormLinkFieldError);
  EXPECT_EQ(fo4::set_record_formlink_field(plugin, key, collar, 2, k_target),
            fo4::FormLinkFieldMutationStatus::updated);

  const auto reread = fo4::read_plugin(fo4::write_plugin(plugin), "Patch.esp");
  EXPECT_TRUE(live_subrecord(reread, "NAM2", 0).empty());
  EXPECT_TRUE(live_subrecord(reread, "NAM2", 1).empty());
  EXPECT_EQ(raw_at(live_subrecord(reread, "NAM2", 2), 0), 0x01000A20u);
}

TEST(RaceScalarRows, MovementOverrideRowsAddressEachSpedByOverrideIndex) {
  auto plugin = fo4::read_plugin(
      plugin_with({{"MTYP", u32_bytes(0x01000A21u)}, {"SPED", sped_bytes(1.0F)},
                   {"MTYP", u32_bytes(0x01000A22u)}, {"SPED", sped_bytes(101.0F)}},
                  false),
      "Patch.esp");
  const auto key = fo4::FormKey::parse(k_race_key);
  const auto left_standing = scalar_row("RACE\\MovementOverrideLeftStanding");
  EXPECT_EQ(left_standing.scalar.subrecord, fo4::Signature::from_string("SPED"));
  EXPECT_EQ(left_standing.scalar.offset, 0u);
  EXPECT_TRUE(left_standing.scalar.address.repeated);
  EXPECT_EQ(left_standing.scalar.min_form_version, 104);
  EXPECT_EQ(left_standing.scalar.minimum_payload_size, 112u);
  EXPECT_EQ(fo4::record_field_count(plugin, key, "RACE\\MovementOverrideLeftStanding"), 2u);
  EXPECT_EQ(read_scalar_member(plugin, "RACE\\MovementOverrideLeftStanding", 0), "1");
  EXPECT_EQ(read_scalar_member(plugin, "RACE\\MovementOverrideRightSprint", 1), "108");

  EXPECT_EQ(fo4::set_record_scalar(plugin, key, scalar_row("RACE\\MovementOverrideRightSprint"), 1,
                                   "7.5"),
            fo4::ScalarMutationStatus::updated);
  EXPECT_EQ(fo4::set_record_scalar(plugin, key, scalar_row("RACE\\MovementOverrideLeftWalk"), 2, "1"),
            fo4::ScalarMutationStatus::subrecord_missing);

  const auto reread = fo4::read_plugin(fo4::write_plugin(plugin), "Patch.esp");
  EXPECT_EQ(live_subrecord(reread, "SPED", 1).size(), 112u);
  EXPECT_FLOAT_EQ(read_f32(live_subrecord(reread, "SPED", 1), 28), 7.5F);
  EXPECT_FLOAT_EQ(read_f32(live_subrecord(reread, "SPED", 0), 4), 2.0F);
}

TEST(RaceScalarRows, MovementOverrideRowsHideOutsideTheirLayoutWindow) {
  const auto key = fo4::FormKey::parse(k_race_key);
  auto modern = fo4::read_plugin(
      plugin_with({{"MTYP", u32_bytes(0x01000A21u)}, {"SPED", Bytes(68)}}, false), "Patch.esp");
  EXPECT_EQ(fo4::record_field_count(modern, key, "RACE\\MovementOverrideLeftStanding"), 1u);
  EXPECT_EQ(read_scalar_member(modern, "RACE\\MovementOverrideLeftStanding", 0), "");
  EXPECT_EQ(fo4::set_record_scalar(modern, key, scalar_row("RACE\\MovementOverrideLeftStanding"), 0,
                                   "1"),
            fo4::ScalarMutationStatus::subrecord_missing);
  EXPECT_EQ(fo4::set_record_scalar(modern, key, scalar_row("RACE\\MovementOverrideLeftWalkBelow60"), 0,
                                   "1"),
            fo4::ScalarMutationStatus::subrecord_missing);

  auto legacy = fo4::read_plugin(
      plugin_with({{"MTYP", u32_bytes(0x01000A21u)}, {"SPED", sped_bytes(1.0F)}}, false, 40),
      "Patch.esp");
  EXPECT_EQ(fo4::record_field_count(legacy, key, "RACE\\MovementOverrideLeftWalkBelow60"), 1u);
  EXPECT_EQ(fo4::set_record_scalar(legacy, key, scalar_row("RACE\\MovementOverrideLeftStanding"), 0,
                                   "1"),
            fo4::ScalarMutationStatus::subrecord_missing);
  EXPECT_EQ(fo4::set_record_scalar(legacy, key, scalar_row("RACE\\MovementOverrideLeftWalkBelow60"), 0,
                                   "9"),
            fo4::ScalarMutationStatus::updated);

  const auto reread = fo4::read_plugin(fo4::write_plugin(legacy), "Patch.esp");
  EXPECT_FLOAT_EQ(read_f32(live_subrecord(reread, "SPED", 0), 0), 9.0F);
}
