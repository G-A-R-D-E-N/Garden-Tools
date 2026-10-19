#include "fo4/plugin/mutation.hpp"
#include "fo4/plugin/reader.hpp"
#include "fo4/plugin/writer.hpp"
#include "fo4/schema/formlink_lists.hpp"
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

std::vector<std::byte> make_plugin(bool padded_package = false) {
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
  fo4::test::write_subrecord(out, "SPLO", u32_bytes(0x00000920u));
  fo4::test::write_subrecord(out, "SPLO", u32_bytes(0x00000921u));
  fo4::test::end_record(out, record);
  fo4::test::end_group(out, group);

  add_group(out, "PACK", {0x00000900u, 0x00000901u, 0x00000902u, 0x00000903u});
  add_group(out, "HDPT", {0x00000910u, 0x00000911u, 0x00000912u});
  add_group(out, "SPEL", {0x00000920u, 0x00000921u, 0x00000922u});
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

TEST(NpcLinkRows, PackageHeadPartAndSpellRowsDeclareRepeatedTargets) {
  const auto package = row("NPC_\\Package");
  EXPECT_EQ(package.record, k_npc);
  EXPECT_EQ(package.subrecord, k_pkid);
  EXPECT_EQ(package.offset, 0u);
  EXPECT_FALSE(package.nullable);
  EXPECT_TRUE(package.address.repeated);
  EXPECT_EQ(package.payload_sizes[0], 4u);
  EXPECT_TRUE(fo4::schema::formlink_field_target_allowed(package, sig("PACK")));
  EXPECT_FALSE(fo4::schema::formlink_field_target_allowed(package, sig("HDPT")));
  const auto package_targets = fo4::schema::formlink_field_targets(package);
  ASSERT_EQ(package_targets.size(), 1u);
  EXPECT_EQ(package_targets[0], sig("PACK"));

  const auto head_part = row("NPC_\\HeadPart");
  EXPECT_EQ(head_part.record, k_npc);
  EXPECT_EQ(head_part.subrecord, k_pnam);
  EXPECT_FALSE(head_part.nullable);
  EXPECT_TRUE(head_part.address.repeated);
  EXPECT_EQ(head_part.payload_sizes[0], 4u);
  EXPECT_TRUE(fo4::schema::formlink_field_target_allowed(head_part, sig("HDPT")));
  EXPECT_FALSE(fo4::schema::formlink_field_target_allowed(head_part, sig("PACK")));

  const auto spell = row("NPC_\\Spell");
  EXPECT_EQ(spell.record, k_npc);
  EXPECT_EQ(spell.subrecord, k_splo);
  EXPECT_FALSE(spell.nullable);
  EXPECT_TRUE(spell.address.repeated);
  EXPECT_EQ(spell.payload_sizes[0], 4u);
  EXPECT_TRUE(fo4::schema::formlink_field_target_allowed(spell, sig("SPEL")));
  EXPECT_TRUE(fo4::schema::formlink_field_target_allowed(spell, sig("LVSP")));
  EXPECT_FALSE(fo4::schema::formlink_field_target_allowed(spell, sig("KYWD")));

  EXPECT_TRUE(fo4::schema::formlink_field_descriptor(k_npc, "NPC_\\Package").has_value());
  EXPECT_TRUE(fo4::schema::formlink_field_descriptor(k_npc, "NPC_\\HeadPart").has_value());
  EXPECT_TRUE(fo4::schema::formlink_field_descriptor(k_npc, "NPC_\\Spell").has_value());
}

TEST(NpcLinkRows, RepeatedWritesReachOnlyTheirOwnSubrecord) {
  auto plugin = fo4::read_plugin(make_plugin(), "Patch.esp");
  EXPECT_EQ(fo4::record_field_count(plugin, npc_key(), "NPC_\\Package"), 3u);
  EXPECT_EQ(fo4::record_field_count(plugin, npc_key(), "NPC_\\HeadPart"), 2u);
  EXPECT_EQ(fo4::record_field_count(plugin, npc_key(), "NPC_\\Spell"), 2u);

  EXPECT_EQ(fo4::set_record_formlink_field(plugin, npc_key(), row("NPC_\\Package"), 1, key(0x000903)),
            fo4::FormLinkFieldMutationStatus::updated);
  EXPECT_EQ(fo4::set_record_formlink_field(plugin, npc_key(), row("NPC_\\HeadPart"), 0, key(0x000912)),
            fo4::FormLinkFieldMutationStatus::updated);
  EXPECT_EQ(fo4::set_record_formlink_field(plugin, npc_key(), row("NPC_\\Spell"), 1, key(0x000922)),
            fo4::FormLinkFieldMutationStatus::updated);

  const auto after = fo4::read_plugin(fo4::write_plugin(plugin), "Patch.esp");
  EXPECT_EQ(raws(after, k_pkid), (std::vector<std::uint32_t>{0x00000900u, 0x00000903u, 0x00000902u}));
  EXPECT_EQ(raws(after, k_pnam), (std::vector<std::uint32_t>{0x00000912u, 0x00000911u}));
  EXPECT_EQ(raws(after, k_splo), (std::vector<std::uint32_t>{0x00000920u, 0x00000922u}));
}

TEST(NpcLinkRows, OutOfRangeIndexNullAndWrongTargetTypeAreRefused) {
  auto plugin = fo4::read_plugin(make_plugin(), "Patch.esp");
  EXPECT_EQ(fo4::set_record_formlink_field(plugin, npc_key(), row("NPC_\\Package"), 3, key(0x000903)),
            fo4::FormLinkFieldMutationStatus::subrecord_missing);
  EXPECT_THROW(fo4::set_record_formlink_field(plugin, npc_key(), row("NPC_\\Package"), 0, fo4::FormKey{}),
               fo4::schema::FormLinkFieldError);
  EXPECT_THROW(fo4::set_record_formlink_field(plugin, npc_key(), row("NPC_\\Package"), 0, key(0x000910)),
               fo4::schema::FormLinkFieldError);
  EXPECT_THROW(fo4::set_record_formlink_field(plugin, npc_key(), row("NPC_\\Spell"), 0, npc_key()),
               fo4::schema::FormLinkFieldError);
  EXPECT_EQ(raws(plugin, k_pkid), (std::vector<std::uint32_t>{0x00000900u, 0x00000901u, 0x00000902u}));
}

TEST(NpcLinkRows, OddSizedPackagePayloadIsRefused) {
  auto plugin = fo4::read_plugin(make_plugin(true), "Patch.esp");
  EXPECT_THROW(fo4::set_record_formlink_field(plugin, npc_key(), row("NPC_\\Package"), 0, key(0x000903)),
               fo4::schema::FormLinkFieldError);
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
