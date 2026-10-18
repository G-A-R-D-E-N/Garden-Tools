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
constexpr auto k_txst_key = "000D02:Patch.esp";

constexpr std::uint32_t raw_id(std::uint32_t local_id) { return 0x02000000u | local_id; }

Bytes u32_bytes(std::uint32_t value) {
  ByteWriter out;
  out.u32(value);
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

std::uint32_t raw_at(const Bytes& data, std::size_t offset) {
  std::uint32_t value = 0;
  if (data.size() < offset + 4) return 0;
  std::memcpy(&value, data.data() + offset, sizeof(value));
  return value;
}

}  // namespace

TEST(RaceLinkRows, HaveXEditSubrecordsTargetsAndShapes) {
  const auto collar = row("RACE\\CollarTextureSet");
  EXPECT_EQ(collar.subrecord, fo4::Signature::from_string("NAM2"));
  EXPECT_EQ(collar.offset, 0u);
  EXPECT_FALSE(collar.nullable);
  EXPECT_EQ(collar.payload_sizes[0], 4u);
  EXPECT_FALSE(collar.address.repeated);
  EXPECT_EQ(fo4::schema::formlink_field_targets(collar).size(), 1u);
  EXPECT_EQ(fo4::schema::formlink_field_targets(collar)[0], fo4::Signature::from_string("TXST"));

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
  const auto bytes = plugin_with({{"NAM2", u32_bytes(0x01000A20u)},
                                  {"MTYP", u32_bytes(0x01000A21u)},
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
  EXPECT_EQ(count("NAM2"), 1u);
  EXPECT_EQ(count("MTYP"), 2u);
  EXPECT_EQ(count("QNAM"), 1u);
}

TEST(RaceLinkRows, WritesAndRereadsEachLink) {
  const auto bytes = plugin_with({{"NAM2", u32_bytes(0x01000A20u)},
                                  {"MTYP", u32_bytes(0x01000A21u)},
                                  {"MTYP", u32_bytes(0x01000A22u)},
                                  {"QNAM", u32_bytes(0x01000A23u)}},
                                 false);
  auto plugin = fo4::read_plugin(bytes, "Patch.esp");
  const auto key = fo4::FormKey::parse(k_race_key);

  EXPECT_EQ(fo4::set_record_formlink_field(plugin, key, row("RACE\\CollarTextureSet"), k_target),
            fo4::FormLinkFieldMutationStatus::updated);
  EXPECT_EQ(fo4::set_record_formlink_field(plugin, key, row("RACE\\EquipSlot"), 0, k_target),
            fo4::FormLinkFieldMutationStatus::updated);
  EXPECT_EQ(fo4::set_record_formlink_field(plugin, key, row("RACE\\MovementType"), 1, k_target),
            fo4::FormLinkFieldMutationStatus::updated);
  EXPECT_EQ(fo4::record_field_count(plugin, key, "RACE\\MovementType"), 2u);

  const auto reread = fo4::read_plugin(fo4::write_plugin(plugin), "Patch.esp");
  EXPECT_EQ(raw_at(live_subrecord(reread, "NAM2", 0), 0), 0x01000A20u);
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

TEST(RaceLinkRows, RefuseMarkersDuplicatesAndWrongTargetTypes) {
  const auto key = fo4::FormKey::parse(k_race_key);
  const auto collar = row("RACE\\CollarTextureSet");

  auto marker = fo4::read_plugin(plugin_with({{"NAM2", Bytes{}}}, false), "Patch.esp");
  EXPECT_THROW(fo4::set_record_formlink_field(marker, key, collar, k_target),
               fo4::schema::FormLinkFieldError);
  EXPECT_EQ(live_subrecord(marker, "NAM2", 0).size(), 0u);

  auto duplicated = fo4::read_plugin(
      plugin_with({{"NAM2", Bytes{}}, {"NAM2", u32_bytes(0x01000A20u)}}, false), "Patch.esp");
  EXPECT_THROW(fo4::set_record_formlink_field(duplicated, key, collar, k_target),
               fo4::schema::FormLinkFieldError);

  auto typed = fo4::read_plugin(plugin_with({{"NAM2", u32_bytes(0x01000A20u)}}, true), "Patch.esp");
  EXPECT_EQ(fo4::set_record_formlink_field(typed, key, collar, fo4::FormKey::parse(k_txst_key)),
            fo4::FormLinkFieldMutationStatus::updated);
  EXPECT_EQ(raw_at(live_subrecord(typed, "NAM2", 0), 0), 0x02000D02u);
  EXPECT_THROW(fo4::set_record_formlink_field(typed, key, collar,
                                              fo4::FormKey::parse(k_armo_key)),
               fo4::schema::FormLinkFieldError);
  EXPECT_EQ(raw_at(live_subrecord(typed, "NAM2", 0), 0), 0x02000D02u);
}
