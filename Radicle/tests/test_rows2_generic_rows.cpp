#include "fo4/plugin/field_access.hpp"
#include "fo4/plugin/mutation.hpp"
#include "fo4/plugin/reader.hpp"
#include "fo4/schema/field_address.hpp"
#include "fo4/schema/formlink_lists.hpp"
#include "fo4/schema/scalar.hpp"
#include "byte_helpers.hpp"
#include "plugin_bytes.hpp"

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <stdexcept>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>
#include <vector>

namespace {

using Bytes = std::vector<std::byte>;
using fo4::test::ByteWriter;
using Piece = std::pair<std::string_view, Bytes>;

Bytes words(std::initializer_list<std::uint32_t> values) {
  ByteWriter out;
  for (const auto value : values) out.u32(value);
  return out.bytes;
}

void add_record(ByteWriter& out, std::string_view signature, std::uint32_t local,
                const std::vector<Piece>& pieces) {
  const auto group = fo4::test::begin_group(out, signature, 0);
  const auto record = fo4::test::begin_record(out, signature, 0, 0x01000000u | local);
  fo4::test::write_subrecord(out, "EDID", std::string_view{"Rows2\0", 6});
  for (const auto& [subrecord, data] : pieces) fo4::test::write_subrecord(out, subrecord, data);
  fo4::test::end_record(out, record);
  fo4::test::end_group(out, group);
}

fo4::Plugin build(const std::vector<std::tuple<std::string_view, std::uint32_t, std::vector<Piece>>>& records) {
  ByteWriter out;
  out.bytes = fo4::test::make_tes4_plugin(0, 1.0f, {"Fallout4.esm"});
  for (const auto& [signature, local, pieces] : records) add_record(out, signature, local, pieces);
  return fo4::read_plugin(out.bytes, "Patch.esp");
}

fo4::FormKey patch_key(std::uint32_t local) {
  return fo4::FormKey{fo4::PluginKey::from_filename("Patch.esp"), fo4::LocalFormId{local}};
}

std::string master_key(std::uint32_t local) {
  return fo4::FormKey{fo4::PluginKey::from_filename("Fallout4.esm"), fo4::LocalFormId{local}}.to_string();
}

fo4::Signature sig(std::string_view text) { return fo4::Signature::from_string(text); }

std::string read_row(const fo4::Plugin& plugin, std::uint32_t id, std::string_view path, std::size_t index,
                     fo4::FieldStatus expected = fo4::FieldStatus::ok) {
  const auto* record = fo4::find_record(plugin, patch_key(id));
  if (record == nullptr) throw std::runtime_error("missing record");
  std::string text;
  EXPECT_EQ(fo4::read_record_field(plugin, *record, path, index, text), expected) << path << "[" << index << "]";
  return text;
}

std::size_t row_count(const fo4::Plugin& plugin, std::uint32_t id, std::string_view path) {
  return fo4::record_field_count(plugin, patch_key(id), path).value_or(9999);
}

std::string read_member(const fo4::Plugin& plugin, std::uint32_t id, std::string_view path, std::size_t index) {
  const auto field = fo4::schema::scalar_field_descriptor(path).value();
  const auto& record = *fo4::find_record(plugin, patch_key(id));
  const auto resolved = fo4::schema::resolve_field_slots(
      record, fo4::schema::scalar_field_shape(field.scalar), field.scalar.address);
  const auto slot = resolved.slots.at(index);
  return fo4::schema::decode_scalar(
      fo4::schema::scalar_slot_bytes(record.subrecords[slot.subrecord].data, slot, field.scalar), field.scalar);
}

fo4::schema::FormLinkFieldDescriptor link_row(std::string_view record, std::string_view path) {
  const auto row = fo4::schema::formlink_field_descriptor(sig(record), path);
  EXPECT_TRUE(row.has_value()) << path;
  return row.value();
}

TEST(Rows2GenericRows, AttachParentSlotsReadByIndexOnArmorAndWeapon) {
  const auto plugin = build({
      {"ARMO", 0x800u, {{"APPR", words({0x00000A30u, 0x00000A31u})}}},
      {"WEAP", 0x801u, {{"APPR", words({0x00000A32u})}}},
  });
  const std::array<std::pair<std::string_view, std::uint32_t>, 2> rows{{{"ARMO", 0x800u}, {"WEAP", 0x801u}}};
  for (const auto& [record, id] : rows) {
    const auto path = std::string(record) + "\\AttachParentSlots";
    SCOPED_TRACE(path);
    const auto list = fo4::schema::formlink_list_descriptor(sig(record), path);
    ASSERT_TRUE(list.has_value());
    EXPECT_EQ(list->storage, fo4::schema::FormLinkListStorage::packed_array);
    EXPECT_EQ(list->subrecord, sig("APPR"));
    EXPECT_FALSE(list->nullable);
    EXPECT_FALSE(list->can_insert);
    EXPECT_TRUE(fo4::schema::formlink_list_target_allowed(*list, sig("KYWD")));
    EXPECT_FALSE(fo4::schema::formlink_list_target_allowed(*list, sig("SNDR")));
  }
  const auto armor = fo4::schema::record_formlink_list_values(
      plugin, patch_key(0x800u), *fo4::schema::formlink_list_descriptor(sig("ARMO"), "ARMO\\AttachParentSlots"));
  ASSERT_TRUE(armor.has_value());
  ASSERT_EQ(armor->size(), 2u);
  EXPECT_EQ((*armor)[0].to_string(), master_key(0xA30));
  EXPECT_EQ((*armor)[1].to_string(), master_key(0xA31));
  const auto weapon = fo4::schema::record_formlink_list_values(
      plugin, patch_key(0x801u), *fo4::schema::formlink_list_descriptor(sig("WEAP"), "WEAP\\AttachParentSlots"));
  ASSERT_TRUE(weapon.has_value());
  ASSERT_EQ(weapon->size(), 1u);
  EXPECT_EQ((*weapon)[0].to_string(), master_key(0xA32));
}

TEST(Rows2GenericRows, NpcFactionsPairLinkAndRankByIndex) {
  const auto faction = [](std::uint32_t id, std::int8_t rank) {
    ByteWriter out;
    out.u32(id);
    out.u8(static_cast<std::uint8_t>(rank));
    return out.bytes;
  };
  const auto plugin = build({{"NPC_", 0x800u, {{"SNAM", faction(0xA40, 2)}, {"SNAM", faction(0xA41, -1)}}}});
  const auto link = link_row("NPC_", "NPC_\\Faction");
  EXPECT_EQ(link.subrecord, sig("SNAM"));
  EXPECT_EQ(link.offset, 0u);
  EXPECT_EQ(link.payload_sizes[0], 5u);
  EXPECT_FALSE(link.nullable);
  EXPECT_TRUE(link.address.repeated);
  EXPECT_TRUE(fo4::schema::formlink_field_target_allowed(link, sig("FACT")));
  EXPECT_FALSE(fo4::schema::formlink_field_target_allowed(link, sig("KYWD")));

  EXPECT_EQ(row_count(plugin, 0x800u, "NPC_\\Faction"), 2u);
  EXPECT_EQ(row_count(plugin, 0x800u, "NPC_\\FactionRank"), 2u);
  EXPECT_EQ(read_row(plugin, 0x800u, "NPC_\\Faction", 0), master_key(0xA40));
  EXPECT_EQ(read_row(plugin, 0x800u, "NPC_\\Faction", 1), master_key(0xA41));
  EXPECT_EQ(read_row(plugin, 0x800u, "NPC_\\FactionRank", 0), "2");
  EXPECT_EQ(read_row(plugin, 0x800u, "NPC_\\FactionRank", 1), "-1");
  read_row(plugin, 0x800u, "NPC_\\Faction", 2, fo4::FieldStatus::occurrence_missing);
  read_row(plugin, 0x800u, "NPC_\\FactionRank", 2, fo4::FieldStatus::occurrence_missing);
}

TEST(Rows2GenericRows, NpcItemsReadCountAndItemPerRun) {
  const auto cnto = [](std::uint32_t item, std::int32_t count) {
    ByteWriter out;
    out.u32(item);
    out.i32(count);
    return out.bytes;
  };
  const auto plugin = build({{"NPC_", 0x800u,
                              {{"COCT", words({2})},
                               {"CNTO", cnto(0xA60, 5)},
                               {"COED", Bytes(12)},
                               {"CNTO", cnto(0xA61, 7)},
                               {"AIDT", Bytes(20)}}}});
  EXPECT_TRUE(fo4::schema::subrecord_group_descriptor(sig("NPC_"), "NPC_\\Items").has_value());
  EXPECT_EQ(read_member(plugin, 0x800u, "NPC_\\ItemTotal", 0), "2");
  EXPECT_EQ(read_member(plugin, 0x800u, "NPC_\\ItemCount", 0), "5");
  EXPECT_EQ(read_member(plugin, 0x800u, "NPC_\\ItemCount", 1), "7");
  const auto object = link_row("NPC_", "NPC_\\ItemObject");
  EXPECT_EQ(object.subrecord, sig("CNTO"));
  EXPECT_EQ(object.offset, 0u);
  EXPECT_TRUE(object.nullable);
  EXPECT_TRUE(fo4::schema::formlink_field_target_allowed(object, sig("ARMO")));
  EXPECT_TRUE(fo4::schema::formlink_field_target_allowed(object, sig("LVLI")));
  EXPECT_FALSE(fo4::schema::formlink_field_target_allowed(object, sig("FACT")));
  EXPECT_EQ(read_row(plugin, 0x800u, "NPC_\\ItemObject", 0), master_key(0xA60));
  EXPECT_EQ(read_row(plugin, 0x800u, "NPC_\\ItemObject", 1), master_key(0xA61));
  read_row(plugin, 0x800u, "NPC_\\ItemObject", 2, fo4::FieldStatus::occurrence_missing);
}

TEST(Rows2GenericRows, LeveledListEntriesReadLevelReferenceAndCountByIndex) {
  const auto lvlo = [](std::uint16_t level, std::uint32_t ref, std::uint16_t count, std::uint8_t chance) {
    ByteWriter out;
    out.u16(level);
    out.u16(0);
    out.u32(ref);
    out.u16(count);
    out.u8(chance);
    out.u8(0);
    return out.bytes;
  };
  const auto short_entry = [] {
    ByteWriter out;
    out.u16(2);
    out.u16(0);
    out.u32(0xA71);
    return out.bytes;
  };
  const auto plugin = build({
      {"LVLI", 0x800u, {{"LLCT", Bytes(1, std::byte{2})}, {"LVLO", lvlo(1, 0xA70, 3, 10)}, {"LVLO", short_entry()}}},
      {"LVLN", 0x801u, {{"LLCT", Bytes(1, std::byte{1})}, {"LVLO", lvlo(4, 0xA72, 1, 0)}}},
  });

  const auto li = link_row("LVLI", "LVLI\\EntryReference");
  const auto ln = link_row("LVLN", "LVLN\\EntryReference");
  EXPECT_EQ(li.subrecord, sig("LVLO"));
  EXPECT_EQ(li.offset, 4u);
  EXPECT_EQ(li.payload_sizes[0], 12u);
  EXPECT_EQ(li.payload_sizes[1], 8u);
  EXPECT_FALSE(li.nullable);
  EXPECT_TRUE(fo4::schema::formlink_field_target_allowed(li, sig("REFR")));
  EXPECT_TRUE(fo4::schema::formlink_field_target_allowed(li, sig("ARMO")));
  EXPECT_FALSE(fo4::schema::formlink_field_target_allowed(li, sig("FACT")));
  EXPECT_TRUE(fo4::schema::formlink_field_target_allowed(ln, sig("LVLN")));
  EXPECT_TRUE(fo4::schema::formlink_field_target_allowed(ln, sig("NPC_")));
  EXPECT_FALSE(fo4::schema::formlink_field_target_allowed(ln, sig("REFR")));

  EXPECT_EQ(row_count(plugin, 0x800u, "LVLI\\EntryReference"), 2u);
  EXPECT_EQ(read_row(plugin, 0x800u, "LVLI\\EntryReference", 0), master_key(0xA70));
  EXPECT_EQ(read_row(plugin, 0x800u, "LVLI\\EntryReference", 1), master_key(0xA71));
  EXPECT_EQ(read_row(plugin, 0x800u, "LVLI\\EntryLevel", 0), "1");
  EXPECT_EQ(read_row(plugin, 0x800u, "LVLI\\EntryLevel", 1), "2");
  EXPECT_EQ(read_row(plugin, 0x800u, "LVLI\\EntryCount", 0), "3");
  EXPECT_EQ(read_row(plugin, 0x800u, "LVLI\\EntryChanceNone", 0), "10");
  EXPECT_EQ(read_row(plugin, 0x801u, "LVLN\\EntryReference", 0), master_key(0xA72));
  EXPECT_EQ(read_row(plugin, 0x801u, "LVLN\\EntryLevel", 0), "4");
  EXPECT_EQ(read_row(plugin, 0x801u, "LVLN\\EntryCount", 0), "1");
}

TEST(Rows2GenericRows, AlchEffectsPairBaseEffectAndEffectDataByIndex) {
  const auto efit = [](float magnitude, std::uint32_t area, std::uint32_t duration) {
    ByteWriter out;
    out.f32(magnitude);
    out.u32(area);
    out.u32(duration);
    return out.bytes;
  };
  const auto plugin = build({{"ALCH", 0x800u,
                              {{"EFID", words({0xA80})},
                               {"EFIT", efit(1.5f, 7, 30)},
                               {"CTDA", Bytes(32)},
                               {"EFID", words({0xA81})},
                               {"EFIT", efit(2.0f, 0, 60)}}}});
  const auto base = link_row("ALCH", "ALCH\\EffectBaseEffect");
  EXPECT_EQ(base.subrecord, sig("EFID"));
  EXPECT_FALSE(base.nullable);
  EXPECT_TRUE(fo4::schema::formlink_field_target_allowed(base, sig("MGEF")));
  EXPECT_FALSE(fo4::schema::formlink_field_target_allowed(base, sig("KYWD")));

  EXPECT_EQ(row_count(plugin, 0x800u, "ALCH\\EffectBaseEffect"), 2u);
  EXPECT_EQ(row_count(plugin, 0x800u, "ALCH\\EffectDuration"), 2u);
  EXPECT_EQ(read_row(plugin, 0x800u, "ALCH\\EffectBaseEffect", 0), master_key(0xA80));
  EXPECT_EQ(read_row(plugin, 0x800u, "ALCH\\EffectBaseEffect", 1), master_key(0xA81));
  EXPECT_DOUBLE_EQ(std::stod(read_row(plugin, 0x800u, "ALCH\\EffectMagnitude", 0)), 1.5);
  EXPECT_DOUBLE_EQ(std::stod(read_row(plugin, 0x800u, "ALCH\\EffectMagnitude", 1)), 2.0);
  EXPECT_EQ(read_row(plugin, 0x800u, "ALCH\\EffectArea", 0), "7");
  EXPECT_EQ(read_row(plugin, 0x800u, "ALCH\\EffectArea", 1), "0");
  EXPECT_EQ(read_row(plugin, 0x800u, "ALCH\\EffectDuration", 0), "30");
  EXPECT_EQ(read_row(plugin, 0x800u, "ALCH\\EffectDuration", 1), "60");
  read_row(plugin, 0x800u, "ALCH\\EffectBaseEffect", 2, fo4::FieldStatus::occurrence_missing);
}

TEST(Rows2GenericRows, BodyPartNodeLinksReadByPartIndex) {
  struct Link {
    std::string_view name;
    std::size_t offset;
    std::string_view target;
  };
  constexpr std::array<Link, 11> k_links{{
      {"ExplodableDebris", 4, "DEBR"},
      {"ExplodableExplosion", 8, "EXPL"},
      {"SeverableDebris", 16, "DEBR"},
      {"SeverableExplosion", 20, "EXPL"},
      {"SeverableImpactDataSet", 52, "IPDS"},
      {"ExplodableImpactDataSet", 56, "IPDS"},
      {"ActorValue", 67, "AVIF"},
      {"OnCrippleArtObject", 79, "ARTO"},
      {"OnCrippleDebris", 83, "DEBR"},
      {"OnCrippleExplosion", 87, "EXPL"},
      {"OnCrippleImpactDataSet", 91, "IPDS"},
  }};
  for (const auto& link : k_links) {
    const auto path = "BPTD\\" + std::string(link.name);
    SCOPED_TRACE(path);
    const auto row = link_row("BPTD", path);
    EXPECT_EQ(row.subrecord, sig("BPND"));
    EXPECT_EQ(row.offset, link.offset);
    EXPECT_EQ(row.payload_sizes[0], 101u);
    EXPECT_TRUE(row.nullable);
    EXPECT_TRUE(row.address.repeated);
    EXPECT_TRUE(fo4::schema::formlink_field_target_allowed(row, sig(link.target)));
    EXPECT_FALSE(fo4::schema::formlink_field_target_allowed(row, sig("KYWD")));
  }

  const auto bpnd = [](std::initializer_list<std::pair<std::size_t, std::uint32_t>> at) {
    Bytes out(101);
    for (const auto& [offset, value] : at) {
      for (std::size_t byte = 0; byte < 4; ++byte) {
        out[offset + byte] = std::byte{static_cast<std::uint8_t>(value >> (8 * byte))};
      }
    }
    return out;
  };
  const auto name = [](std::string_view text) { return fo4::test::zstr(text); };
  const auto plugin = build({{"BPTD", 0x800u,
                              {{"BPNN", name("Head")},
                               {"BPND", bpnd({{4, 0xA90}, {67, 0xA91}, {91, 0xA92}})},
                               {"BPNN", name("Arm")},
                               {"BPND", bpnd({{8, 0xA93}})}}}});
  EXPECT_EQ(row_count(plugin, 0x800u, "BPTD\\ExplodableDebris"), 2u);
  EXPECT_EQ(read_row(plugin, 0x800u, "BPTD\\ExplodableDebris", 0), master_key(0xA90));
  EXPECT_EQ(read_row(plugin, 0x800u, "BPTD\\ExplodableDebris", 1), "");
  EXPECT_EQ(read_row(plugin, 0x800u, "BPTD\\ActorValue", 0), master_key(0xA91));
  EXPECT_EQ(read_row(plugin, 0x800u, "BPTD\\OnCrippleImpactDataSet", 0), master_key(0xA92));
  EXPECT_EQ(read_row(plugin, 0x800u, "BPTD\\ExplodableExplosion", 0), "");
  EXPECT_EQ(read_row(plugin, 0x800u, "BPTD\\ExplodableExplosion", 1), master_key(0xA93));
  read_row(plugin, 0x800u, "BPTD\\ExplodableDebris", 2, fo4::FieldStatus::occurrence_missing);
}

}  // namespace
