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

Bytes zstr(std::string_view text) {
  Bytes out;
  for (const auto ch : text) out.push_back(std::byte{static_cast<std::uint8_t>(ch)});
  out.push_back(std::byte{0});
  return out;
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

Bytes plugin_with(const std::vector<Sub>& race_subrecords, bool with_local_records) {
  ByteWriter out;
  write_header(out);

  const auto group = fo4::test::begin_group(out, k_race, 0);
  const auto record = fo4::test::begin_record(out, k_race, 0, raw_id(0x0800));
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

TEST(RaceLinkRows, CollectsTheNewSubrecordsAsFormLinks) {
  const auto bytes = plugin_with({{"MTYP", u32_bytes(0x01000A21u)},
                                  {"MTYP", u32_bytes(0x01000A22u)},
                                  {"QNAM", u32_bytes(0x01000A23u)}},
                                 false);
  const auto plugin = fo4::read_plugin(bytes, "Patch.esp");
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

  EXPECT_EQ(fo4::set_record_scalar(plugin, key, scalar_row("RACE\\MorphLastIndex"), 0, "4"),
            fo4::ScalarMutationStatus::subrecord_missing);
  EXPECT_THROW(fo4::set_record_scalar(plugin, key, mask, 0, "65536"), fo4::schema::ScalarError);

  const auto reread = fo4::read_plugin(fo4::write_plugin(plugin), "Patch.esp");
  EXPECT_EQ(raw_at(live_subrecord(reread, "FMRI", 0), 0), 9u);
  EXPECT_EQ(fo4::schema::decode_scalar(live_subrecord(reread, "MPPK", 0), mask.scalar), "65535");
  EXPECT_EQ(live_subrecord(reread, "MPPF", 0), u8_bytes(0));
  EXPECT_EQ(raw_at(live_subrecord(reread, "MSID", 0), 0), 12u);
  EXPECT_EQ(live_subrecord(reread, "SAPT", 0), zstr("Anim\\Walk"));
  EXPECT_EQ(live_subrecord(reread, "MPGN", 0), zstr("Brow"));
  EXPECT_EQ(raw_at(live_subrecord(reread, "MPPT", 0), 0), 0x01000A20u);
}
