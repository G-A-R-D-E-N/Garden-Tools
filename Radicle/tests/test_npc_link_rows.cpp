#include "fo4/plugin/mutation.hpp"
#include "fo4/plugin/reader.hpp"
#include "fo4/plugin/writer.hpp"
#include "fo4/schema/formlink_lists.hpp"
#include "fo4/schema/scalar.hpp"
#include "plugin_bytes.hpp"

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <string_view>
#include <vector>

namespace {

using Bytes = std::vector<std::byte>;
using fo4::test::ByteWriter;

constexpr auto k_npc = fo4::Signature::from_string("NPC_");
constexpr auto k_pkid = fo4::Signature::from_string("PKID");
constexpr auto k_pnam = fo4::Signature::from_string("PNAM");
constexpr auto k_splo = fo4::Signature::from_string("SPLO");
constexpr auto k_spct = fo4::Signature::from_string("SPCT");

fo4::Signature sig(std::string_view text) { return fo4::Signature::from_string(text); }

fo4::FormKey key(std::uint32_t local) {
  char text[64];
  std::snprintf(text, sizeof(text), "%06X:Patch.esp", local);
  return fo4::FormKey::parse(text);
}

fo4::FormKey npc_key() { return key(0x000800); }

fo4::schema::FormLinkFieldDescriptor row(std::string_view path) {
  return fo4::schema::formlink_field_descriptor(path).value();
}

fo4::schema::SubrecordGroupDescriptor spells_group() {
  return fo4::schema::subrecord_group_descriptor(k_npc, "NPC_\\Spells").value();
}

Bytes u32_bytes(std::uint32_t value) {
  ByteWriter out;
  out.u32(value);
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

std::vector<std::byte> make_plugin(bool padded_package = false,
                                   std::uint32_t stored_spell_count = 2) {
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
  fo4::test::write_subrecord(out, "PKID", u32_bytes(0x00000900u));
  fo4::test::write_subrecord(out, "PKID", padded_package ? Bytes(8, std::byte{0}) : u32_bytes(0x00000901u));
  fo4::test::write_subrecord(out, "PKID", u32_bytes(0x00000902u));
  fo4::test::write_subrecord(out, "PNAM", u32_bytes(0x00000910u));
  fo4::test::write_subrecord(out, "PNAM", u32_bytes(0x00000911u));
  fo4::test::write_subrecord(out, "SPCT", u32_bytes(stored_spell_count));
  fo4::test::write_subrecord(out, "SPLO", u32_bytes(0x00000920u));
  fo4::test::write_subrecord(out, "SPLO", u32_bytes(0x00000921u));
  fo4::test::end_record(out, record);
  fo4::test::end_group(out, group);

  add_group(out, "PACK", {0x00000900u, 0x00000901u, 0x00000902u, 0x00000903u, 0x00000904u});
  add_group(out, "HDPT", {0x00000910u, 0x00000911u, 0x00000912u, 0x00000913u});
  add_group(out, "SPEL", {0x00000920u, 0x00000921u, 0x00000922u});
  return out.bytes;
}

std::vector<std::byte> make_ordered_list_plugin(bool with_predecessor) {
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
  if (with_predecessor) {
    fo4::test::write_subrecord(out, "ACBS", Bytes(20, std::byte{0}));
    fo4::test::write_subrecord(out, "RNAM", u32_bytes(0x00000C00u));
    fo4::test::write_subrecord(out, "AIDT", Bytes(24, std::byte{0}));
    fo4::test::write_subrecord(out, "PNAM", u32_bytes(0x00000C01u));
  } else {
    fo4::test::write_subrecord(out, "PNAM", u32_bytes(0x00000C01u));
  }
  fo4::test::end_record(out, record);
  fo4::test::end_group(out, group);

  add_group(out, "RACE", {0x00000C00u});
  add_group(out, "HDPT", {0x00000C01u});
  add_group(out, "PACK", {0x00000C02u});
  return out.bytes;
}

std::vector<std::byte> make_anchor_plugin(const std::vector<std::string_view>& subrecords) {
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
  for (const auto signature : subrecords) {
    fo4::test::write_subrecord(out, signature, u32_bytes(0));
  }
  fo4::test::end_record(out, record);
  fo4::test::end_group(out, group);

  add_group(out, "PACK", {0x00000D00u});
  add_group(out, "HDPT", {0x00000D01u});
  add_group(out, "KYWD", {0x00000D02u});
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

fo4::schema::FormLinkListDescriptor list(std::string_view path) {
  return fo4::schema::formlink_list_descriptor(k_npc, path).value();
}

std::optional<std::size_t> index_of(const fo4::Plugin& plugin, fo4::Signature signature) {
  const auto* record = fo4::find_record(plugin, npc_key());
  if (record == nullptr) return std::nullopt;
  for (std::size_t index = 0; index < record->subrecords.size(); ++index) {
    if (record->subrecords[index].signature == signature) return index;
  }
  return std::nullopt;
}

std::vector<fo4::Signature> signatures(const fo4::Plugin& plugin) {
  std::vector<fo4::Signature> out;
  const auto* record = fo4::find_record(plugin, npc_key());
  if (record == nullptr) return out;
  for (const auto& subrecord : record->subrecords) out.push_back(subrecord.signature);
  return out;
}

std::vector<std::byte> make_single_link_plugin(bool short_transform = false) {
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
  fo4::test::write_subrecord(out, "PTRN", short_transform ? Bytes(2, std::byte{0}) : u32_bytes(0x00000B00u));
  fo4::test::write_subrecord(out, "STCP", u32_bytes(0x00000B01u));
  fo4::test::write_subrecord(out, "NTRM", u32_bytes(0x00000B02u));
  fo4::test::write_subrecord(out, "FTYP", u32_bytes(0x00000B03u));
  fo4::test::end_record(out, record);
  fo4::test::end_group(out, group);

  add_group(out, "TRNS", {0x00000B00u, 0x00000B10u});
  add_group(out, "STAG", {0x00000B01u, 0x00000B11u});
  add_group(out, "TERM", {0x00000B02u, 0x00000B12u});
  add_group(out, "LCRT", {0x00000B03u, 0x00000B13u});
  return out.bytes;
}

}  // namespace

TEST(NpcLinkRows, SpellRowDeclaresRepeatedTargets) {
  const auto spell = row("NPC_\\Spell");
  EXPECT_EQ(spell.record, k_npc);
  EXPECT_EQ(spell.subrecord, k_splo);
  EXPECT_EQ(spell.offset, 0u);
  EXPECT_FALSE(spell.nullable);
  EXPECT_TRUE(spell.address.repeated);
  EXPECT_TRUE(fo4::schema::formlink_field_target_allowed(spell, sig("SPEL")));
  EXPECT_TRUE(fo4::schema::formlink_field_target_allowed(spell, sig("LVSP")));
  EXPECT_FALSE(fo4::schema::formlink_field_target_allowed(spell, sig("KYWD")));

  const auto spells = spells_group();
  EXPECT_EQ(spells.record, k_npc);
  EXPECT_EQ(spells.count_subrecord, k_spct);
  ASSERT_EQ(spells.members.size(), 1u);
  EXPECT_EQ(spells.members[0].subrecord, k_splo);
  EXPECT_TRUE(spells.members[0].required);
  EXPECT_FALSE(spells.members[0].repeatable);
  EXPECT_EQ(spells.members[0].payload_size, 4u);

  EXPECT_TRUE(fo4::schema::formlink_field_descriptor(k_npc, "NPC_\\Spell").has_value());
  EXPECT_TRUE(fo4::schema::scalar_field_descriptor("NPC_\\SpellCount").has_value());
}

TEST(NpcLinkRows, PackageAndHeadPartArePerSubrecordListsNotFields) {
  const auto packages = list("NPC_\\Packages");
  EXPECT_EQ(packages.record, k_npc);
  EXPECT_EQ(packages.subrecord, k_pkid);
  EXPECT_EQ(packages.storage, fo4::schema::FormLinkListStorage::per_subrecord);
  EXPECT_FALSE(packages.nullable);
  EXPECT_TRUE(packages.can_insert);
  EXPECT_TRUE(fo4::schema::formlink_list_target_allowed(packages, sig("PACK")));
  EXPECT_FALSE(fo4::schema::formlink_list_target_allowed(packages, sig("HDPT")));
  const auto package_targets = fo4::schema::formlink_list_targets(packages);
  ASSERT_EQ(package_targets.size(), 1u);
  EXPECT_EQ(package_targets[0], sig("PACK"));

  const auto head_parts = list("NPC_\\HeadParts");
  EXPECT_EQ(head_parts.record, k_npc);
  EXPECT_EQ(head_parts.subrecord, k_pnam);
  EXPECT_EQ(head_parts.storage, fo4::schema::FormLinkListStorage::per_subrecord);
  EXPECT_FALSE(head_parts.nullable);
  EXPECT_TRUE(head_parts.can_insert);
  EXPECT_TRUE(fo4::schema::formlink_list_target_allowed(head_parts, sig("HDPT")));
  EXPECT_FALSE(fo4::schema::formlink_list_target_allowed(head_parts, sig("PACK")));

  EXPECT_FALSE(fo4::schema::formlink_field_descriptor(k_npc, "NPC_\\Package").has_value());
  EXPECT_FALSE(fo4::schema::formlink_field_descriptor(k_npc, "NPC_\\HeadPart").has_value());
}

TEST(NpcLinkRows, RepeatedWritesReachOnlyTheirOwnSubrecord) {
  auto plugin = fo4::read_plugin(make_plugin(), "Patch.esp");
  EXPECT_EQ(fo4::record_field_count(plugin, npc_key(), "NPC_\\Spell"), 2u);
  EXPECT_EQ(raws(plugin, k_splo), (std::vector<std::uint32_t>{0x00000920u, 0x00000921u}));
  EXPECT_EQ(raws(plugin, k_spct), (std::vector<std::uint32_t>{2u}));

  EXPECT_EQ(fo4::set_record_formlink_field(plugin, npc_key(), row("NPC_\\Spell"), 1, key(0x000922)),
            fo4::FormLinkFieldMutationStatus::updated);

  EXPECT_EQ(fo4::insert_record_subrecord_group(plugin, npc_key(), spells_group(), 1),
            fo4::SubrecordGroupMutationStatus::updated);
  EXPECT_EQ(raws(plugin, k_spct), (std::vector<std::uint32_t>{3u}));

  EXPECT_EQ(fo4::remove_record_subrecord_group(plugin, npc_key(), spells_group(), 0),
            fo4::SubrecordGroupMutationStatus::updated);
  EXPECT_EQ(raws(plugin, k_spct), (std::vector<std::uint32_t>{2u}));

  const auto after = fo4::read_plugin(fo4::write_plugin(plugin), "Patch.esp");
  EXPECT_EQ(raws(after, k_splo), (std::vector<std::uint32_t>{0x00000000u, 0x00000922u}));
  EXPECT_EQ(raws(after, k_pkid), (std::vector<std::uint32_t>{0x00000900u, 0x00000901u, 0x00000902u}));
  EXPECT_EQ(raws(after, k_pnam), (std::vector<std::uint32_t>{0x00000910u, 0x00000911u}));
  EXPECT_EQ(signatures(after), (std::vector<fo4::Signature>{k_pkid, k_pkid, k_pkid, k_pnam, k_pnam,
                                                             k_spct, k_splo, k_splo}));
}

TEST(NpcLinkRows, PackageAndHeadPartListsSetInsertAndRemoveByIndex) {
  auto plugin = fo4::read_plugin(make_plugin(), "Patch.esp");
  const auto packages = list("NPC_\\Packages");
  const auto head_parts = list("NPC_\\HeadParts");
  ASSERT_EQ(fo4::schema::record_formlink_list_size(plugin, npc_key(), packages), 3u);
  ASSERT_EQ(fo4::schema::record_formlink_list_size(plugin, npc_key(), head_parts), 2u);
  EXPECT_EQ(fo4::record_field_count(plugin, npc_key(), "NPC_\\Packages"), 3u);
  EXPECT_EQ(fo4::record_field_count(plugin, npc_key(), "NPC_\\HeadParts"), 2u);
  EXPECT_THROW(fo4::record_field_count(plugin, npc_key(), "NPC_\\Package"), fo4::schema::ScalarError);

  EXPECT_EQ(fo4::schema::set_record_formlink_list_element(plugin, npc_key(), packages, 1, key(0x000904)),
            fo4::schema::FormLinkListMutationStatus::updated);
  EXPECT_EQ(fo4::schema::insert_record_formlink_list_element(plugin, npc_key(), head_parts, 1, key(0x000913)),
            fo4::schema::FormLinkListMutationStatus::updated);
  EXPECT_EQ(fo4::schema::remove_record_formlink_list_element(plugin, npc_key(), packages, 2),
            fo4::schema::FormLinkListMutationStatus::updated);

  const auto after = fo4::read_plugin(fo4::write_plugin(plugin), "Patch.esp");
  EXPECT_EQ(raws(after, k_pkid), (std::vector<std::uint32_t>{0x00000900u, 0x00000904u}));
  EXPECT_EQ(raws(after, k_pnam), (std::vector<std::uint32_t>{0x00000910u, 0x00000913u, 0x00000911u}));
  EXPECT_EQ(raws(after, k_splo), (std::vector<std::uint32_t>{0x00000920u, 0x00000921u}));
}

TEST(NpcLinkRows, OutOfRangeIndexNullAndWrongTargetTypeAreRefused) {
  auto plugin = fo4::read_plugin(make_plugin(), "Patch.esp");
  const auto packages = list("NPC_\\Packages");
  EXPECT_EQ(fo4::schema::set_record_formlink_list_element(plugin, npc_key(), packages, 3, key(0x000903)),
            fo4::schema::FormLinkListMutationStatus::index_out_of_range);
  EXPECT_EQ(fo4::schema::insert_record_formlink_list_element(plugin, npc_key(), packages, 4, key(0x000903)),
            fo4::schema::FormLinkListMutationStatus::index_out_of_range);
  EXPECT_EQ(fo4::schema::remove_record_formlink_list_element(plugin, npc_key(), packages, 3),
            fo4::schema::FormLinkListMutationStatus::index_out_of_range);
  EXPECT_EQ(fo4::schema::set_record_formlink_list_element(plugin, npc_key(), packages, 0, key(0x000910)),
            fo4::schema::FormLinkListMutationStatus::target_type_mismatch);
  EXPECT_THROW(fo4::schema::set_record_formlink_list_element(plugin, npc_key(), packages, 0, fo4::FormKey{}),
               fo4::schema::FormLinkListError);
  EXPECT_THROW(fo4::set_record_formlink_field(plugin, npc_key(), row("NPC_\\Spell"), 0, npc_key()),
               fo4::schema::FormLinkFieldError);
  EXPECT_EQ(fo4::remove_record_subrecord_group(plugin, npc_key(), spells_group(), 2),
            fo4::SubrecordGroupMutationStatus::index_out_of_range);
  EXPECT_EQ(raws(plugin, k_pkid), (std::vector<std::uint32_t>{0x00000900u, 0x00000901u, 0x00000902u}));
  EXPECT_EQ(raws(plugin, k_splo), (std::vector<std::uint32_t>{0x00000920u, 0x00000921u}));
  EXPECT_EQ(raws(plugin, k_spct), (std::vector<std::uint32_t>{2u}));
}

TEST(NpcLinkRows, SpellCountThatDisagreesWithTheRunIsRefusedAtomically) {
  auto plugin = fo4::read_plugin(make_plugin(false, 3), "Patch.esp");
  const auto before = fo4::write_plugin(plugin);
  EXPECT_EQ(raws(plugin, k_splo).size(), 2u);
  EXPECT_EQ(raws(plugin, k_spct), (std::vector<std::uint32_t>{3u}));

  EXPECT_THROW(fo4::insert_record_subrecord_group(plugin, npc_key(), spells_group(), 0),
               fo4::schema::SubrecordGroupError);
  EXPECT_THROW(fo4::remove_record_subrecord_group(plugin, npc_key(), spells_group(), 0),
               fo4::schema::SubrecordGroupError);
  EXPECT_EQ(fo4::write_plugin(plugin), before);
}

TEST(NpcLinkRows, EmptyPackageListAnchorsAfterItsLastXEditOrderPredecessor) {
  auto plugin = fo4::read_plugin(make_ordered_list_plugin(true), "Patch.esp");
  const auto packages = list("NPC_\\Packages");
  ASSERT_EQ(fo4::schema::record_formlink_list_size(plugin, npc_key(), packages), 0u);

  EXPECT_EQ(fo4::schema::insert_record_formlink_list_element(plugin, npc_key(), packages, 0, key(0x000C02)),
            fo4::schema::FormLinkListMutationStatus::updated);

  const auto after = fo4::read_plugin(fo4::write_plugin(plugin), "Patch.esp");
  EXPECT_EQ(raws(after, k_pkid), (std::vector<std::uint32_t>{0x00000C02u}));
  ASSERT_TRUE(index_of(after, k_pkid).has_value());
  ASSERT_TRUE(index_of(after, sig("AIDT")).has_value());
  ASSERT_TRUE(index_of(after, sig("PNAM")).has_value());
  EXPECT_EQ(*index_of(after, k_pkid), *index_of(after, sig("AIDT")) + 1);
  EXPECT_EQ(*index_of(after, sig("PNAM")), *index_of(after, k_pkid) + 1);
}

TEST(NpcLinkRows, EmptyListsAnchorAfterTheWholeAttackRun) {
  struct Row {
    std::string_view path;
    std::string_view subrecord;
    std::uint32_t target;
  };
  const auto bytes = make_anchor_plugin(
      {"ACBS", "RNAM", "DEST", "DSTD", "DSTF", "ATKD", "ATKE", "ATKW", "ATKS", "ATKT", "HCLF"});
  for (const auto& row : {Row{"NPC_\\Packages", "PKID", 0x00000D00u},
                          Row{"NPC_\\HeadParts", "PNAM", 0x00000D01u},
                          Row{"NPC_\\AttachParentSlots", "APPR", 0x00000D02u}}) {
    auto plugin = fo4::read_plugin(bytes, "Patch.esp");
    EXPECT_EQ(fo4::schema::insert_record_formlink_list_element(
                  plugin, npc_key(), list(row.path), 0, key(row.target)),
              fo4::schema::FormLinkListMutationStatus::updated);
    const auto after = fo4::read_plugin(fo4::write_plugin(plugin), "Patch.esp");
    ASSERT_TRUE(index_of(after, sig(row.subrecord)).has_value());
    ASSERT_TRUE(index_of(after, sig("ATKT")).has_value());
    EXPECT_EQ(*index_of(after, sig(row.subrecord)), *index_of(after, sig("ATKT")) + 1);
  }
}

TEST(NpcLinkRows, EmptyAttachParentSlotsAnchorsAfterTheDestructibleRun) {
  auto plugin = fo4::read_plugin(make_anchor_plugin({"ACBS", "RNAM", "DEST", "DSTD", "DSTF", "HCLF"}), "Patch.esp");
  EXPECT_EQ(fo4::schema::insert_record_formlink_list_element(
                plugin, npc_key(), list("NPC_\\AttachParentSlots"), 0, key(0x00000D02u)),
            fo4::schema::FormLinkListMutationStatus::updated);
  const auto after = fo4::read_plugin(fo4::write_plugin(plugin), "Patch.esp");
  ASSERT_TRUE(index_of(after, sig("APPR")).has_value());
  ASSERT_TRUE(index_of(after, sig("DSTF")).has_value());
  EXPECT_EQ(*index_of(after, sig("APPR")), *index_of(after, sig("DSTF")) + 1);
}

TEST(NpcLinkRows, EmptyPackageListRefusesAnInsertWithNoAnchor) {
  auto plugin = fo4::read_plugin(make_ordered_list_plugin(false), "Patch.esp");
  const auto packages = list("NPC_\\Packages");
  EXPECT_EQ(fo4::schema::insert_record_formlink_list_element(plugin, npc_key(), packages, 0, key(0x000C02)),
            fo4::schema::FormLinkListMutationStatus::insertion_boundary_unavailable);
  EXPECT_EQ(raws(plugin, k_pkid).size(), 0u);
}

TEST(NpcLinkRows, OddSizedPackagePayloadIsRefused) {
  auto plugin = fo4::read_plugin(make_plugin(true), "Patch.esp");
  const auto packages = list("NPC_\\Packages");
  EXPECT_THROW(fo4::schema::record_formlink_list_size(plugin, npc_key(), packages),
               fo4::schema::FormLinkListError);
  EXPECT_THROW(fo4::schema::set_record_formlink_list_element(plugin, npc_key(), packages, 0, key(0x000904)),
               fo4::schema::FormLinkListError);
  EXPECT_EQ(raws(plugin, k_pkid).size(), 3u);
}

TEST(NpcLinkRows, SingleLinkRowsCarryTheirOwnTargetType) {
  const auto transform = row("NPC_\\PreviewTransform");
  EXPECT_EQ(transform.record, k_npc);
  EXPECT_EQ(transform.subrecord, sig("PTRN"));
  EXPECT_EQ(transform.offset, 0u);
  EXPECT_FALSE(transform.nullable);
  EXPECT_FALSE(transform.address.repeated);
  EXPECT_EQ(transform.payload_sizes[0], 4u);
  EXPECT_TRUE(fo4::schema::formlink_field_target_allowed(transform, sig("TRNS")));
  EXPECT_FALSE(fo4::schema::formlink_field_target_allowed(transform, sig("LCRT")));

  const auto sound = row("NPC_\\AnimationSound");
  EXPECT_EQ(sound.subrecord, sig("STCP"));
  EXPECT_EQ(sound.offset, 0u);
  EXPECT_FALSE(sound.address.repeated);
  EXPECT_TRUE(fo4::schema::formlink_field_target_allowed(sound, sig("STAG")));
  EXPECT_FALSE(fo4::schema::formlink_field_target_allowed(sound, sig("SNDR")));

  const auto terminal = row("NPC_\\NativeTerminal");
  EXPECT_EQ(terminal.subrecord, sig("NTRM"));
  EXPECT_EQ(terminal.offset, 0u);
  EXPECT_FALSE(terminal.address.repeated);
  EXPECT_TRUE(fo4::schema::formlink_field_target_allowed(terminal, sig("TERM")));
  EXPECT_FALSE(fo4::schema::formlink_field_target_allowed(terminal, sig("TRNS")));

  const auto locref = row("NPC_\\ForcedLocRefType");
  EXPECT_EQ(locref.subrecord, sig("FTYP"));
  EXPECT_EQ(locref.offset, 0u);
  EXPECT_FALSE(locref.address.repeated);
  EXPECT_TRUE(fo4::schema::formlink_field_target_allowed(locref, sig("LCRT")));
  EXPECT_FALSE(fo4::schema::formlink_field_target_allowed(locref, sig("TERM")));

  EXPECT_TRUE(fo4::schema::formlink_field_descriptor(k_npc, "NPC_\\PreviewTransform").has_value());
  EXPECT_TRUE(fo4::schema::formlink_field_descriptor(k_npc, "NPC_\\AnimationSound").has_value());
  EXPECT_TRUE(fo4::schema::formlink_field_descriptor(k_npc, "NPC_\\NativeTerminal").has_value());
  EXPECT_TRUE(fo4::schema::formlink_field_descriptor(k_npc, "NPC_\\ForcedLocRefType").has_value());
}

TEST(NpcLinkRows, SingleLinkRowsReadRetargetAndRefuseForeignTargets) {
  auto plugin = fo4::read_plugin(make_single_link_plugin(), "Patch.esp");
  EXPECT_EQ(raws(plugin, sig("PTRN")), (std::vector<std::uint32_t>{0x00000B00u}));
  EXPECT_EQ(raws(plugin, sig("STCP")), (std::vector<std::uint32_t>{0x00000B01u}));
  EXPECT_EQ(raws(plugin, sig("NTRM")), (std::vector<std::uint32_t>{0x00000B02u}));
  EXPECT_EQ(raws(plugin, sig("FTYP")), (std::vector<std::uint32_t>{0x00000B03u}));

  EXPECT_EQ(fo4::set_record_formlink_field(plugin, npc_key(), row("NPC_\\PreviewTransform"), key(0x000B10)),
            fo4::FormLinkFieldMutationStatus::updated);
  EXPECT_EQ(fo4::set_record_formlink_field(plugin, npc_key(), row("NPC_\\AnimationSound"), key(0x000B11)),
            fo4::FormLinkFieldMutationStatus::updated);
  EXPECT_EQ(fo4::set_record_formlink_field(plugin, npc_key(), row("NPC_\\NativeTerminal"), key(0x000B12)),
            fo4::FormLinkFieldMutationStatus::updated);
  EXPECT_EQ(fo4::set_record_formlink_field(plugin, npc_key(), row("NPC_\\ForcedLocRefType"), key(0x000B13)),
            fo4::FormLinkFieldMutationStatus::updated);

  EXPECT_THROW(fo4::set_record_formlink_field(plugin, npc_key(), row("NPC_\\PreviewTransform"), key(0x000B12)),
               fo4::schema::FormLinkFieldError);
  EXPECT_THROW(fo4::set_record_formlink_field(plugin, npc_key(), row("NPC_\\NativeTerminal"), key(0x000B10)),
               fo4::schema::FormLinkFieldError);
  EXPECT_THROW(fo4::set_record_formlink_field(plugin, npc_key(), row("NPC_\\AnimationSound"), key(0x000B03)),
               fo4::schema::FormLinkFieldError);
  EXPECT_THROW(fo4::set_record_formlink_field(plugin, npc_key(), row("NPC_\\ForcedLocRefType"), fo4::FormKey{}),
               fo4::schema::FormLinkFieldError);

  const auto after = fo4::read_plugin(fo4::write_plugin(plugin), "Patch.esp");
  EXPECT_EQ(raws(after, sig("PTRN")), (std::vector<std::uint32_t>{0x00000B10u}));
  EXPECT_EQ(raws(after, sig("STCP")), (std::vector<std::uint32_t>{0x00000B11u}));
  EXPECT_EQ(raws(after, sig("NTRM")), (std::vector<std::uint32_t>{0x00000B12u}));
  EXPECT_EQ(raws(after, sig("FTYP")), (std::vector<std::uint32_t>{0x00000B13u}));
}

TEST(NpcLinkRows, ShortSingleLinkPayloadIsRefused) {
  auto plugin = fo4::read_plugin(make_single_link_plugin(true), "Patch.esp");
  EXPECT_THROW(fo4::set_record_formlink_field(plugin, npc_key(), row("NPC_\\PreviewTransform"), key(0x000B10)),
               fo4::schema::FormLinkFieldError);
  EXPECT_EQ(raws(plugin, sig("PTRN")), (std::vector<std::uint32_t>{0x00000000u}));

  const auto after = fo4::read_plugin(fo4::write_plugin(plugin), "Patch.esp");
  EXPECT_EQ(raws(after, sig("PTRN")).size(), 1u);
  EXPECT_EQ(raws(after, sig("STCP")), (std::vector<std::uint32_t>{0x00000B01u}));
}
