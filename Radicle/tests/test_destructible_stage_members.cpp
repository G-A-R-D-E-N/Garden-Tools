#include "fo4/plugin/destructible_stage.hpp"
#include "fo4/plugin/mutation.hpp"
#include "fo4/plugin/reader.hpp"
#include "fo4/plugin/writer.hpp"
#include "fo4/schema/formlink_lists.hpp"
#include "plugin_bytes.hpp"

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace {

using Bytes = std::vector<std::byte>;
using fo4::test::ByteWriter;

const std::string k_staged_order =
    "EDID OBND MODL MODT DEST DSTD DMDL DSTF DSTD DSTF DATA DNAM NAM1 VNAM";

fo4::FormKey staged_key() { return fo4::FormKey::parse("000D01:Patch.esp"); }

Bytes dest_payload(std::int32_t health, std::uint8_t stages) {
  ByteWriter out;
  out.i32(health);
  out.u8(stages);
  out.u8(0);
  out.u16(0);
  return out.bytes;
}

Bytes dstd_payload(std::uint8_t health_percent, std::uint8_t index) {
  ByteWriter out;
  out.u8(health_percent);
  out.u8(index);
  out.u8(0);
  out.u8(0);
  out.i32(0);
  out.u32(0);
  out.u32(0);
  out.i32(0);
  return out.bytes;
}

void write_tes4(ByteWriter& out) {
  const auto tes4 = fo4::test::begin_record(out, "TES4", 0, 0);
  ByteWriter hedr;
  hedr.f32(1.0f);
  hedr.u32(4);
  hedr.u32(0x900);
  fo4::test::write_subrecord(out, "HEDR", hedr.bytes);
  fo4::test::write_subrecord(out, "MAST", std::string_view{"Fallout4.esm\0", 13});
  fo4::test::write_subrecord(out, "DATA", Bytes(8));
  fo4::test::end_record(out, tes4);
}

Bytes make_plugin() {
  ByteWriter out;
  write_tes4(out);

  const auto group = fo4::test::begin_group(out, "PROJ", 0);
  const auto staged = fo4::test::begin_record(out, "PROJ", 0, 0x01000D01);
  fo4::test::write_subrecord(out, "EDID", std::string_view{"Staged\0", 7});
  fo4::test::write_subrecord(out, "OBND", Bytes(12));
  fo4::test::write_subrecord(out, "MODL", std::string_view{"p.nif\0", 6});
  fo4::test::write_subrecord(out, "MODT", Bytes(12));
  fo4::test::write_subrecord(out, "DEST", dest_payload(300, 2));
  fo4::test::write_subrecord(out, "DSTD", dstd_payload(20, 0));
  fo4::test::write_subrecord(out, "DMDL", std::string_view{"a.nif\0", 6});
  fo4::test::write_subrecord(out, "DSTF", Bytes{});
  fo4::test::write_subrecord(out, "DSTD", dstd_payload(60, 1));
  fo4::test::write_subrecord(out, "DSTF", Bytes{});
  fo4::test::write_subrecord(out, "DATA", Bytes(92));
  fo4::test::write_subrecord(out, "DNAM", Bytes(4));
  fo4::test::write_subrecord(out, "NAM1", std::string_view{"m.nif\0", 6});
  fo4::test::write_subrecord(out, "VNAM", Bytes(4));
  fo4::test::end_record(out, staged);

  const auto headerless = fo4::test::begin_record(out, "PROJ", 0, 0x01000D02);
  fo4::test::write_subrecord(out, "EDID", std::string_view{"NoHeader\0", 9});
  fo4::test::write_subrecord(out, "OBND", Bytes(12));
  fo4::test::write_subrecord(out, "MODL", std::string_view{"p.nif\0", 6});
  fo4::test::write_subrecord(out, "MODT", Bytes(12));
  fo4::test::write_subrecord(out, "DSTD", dstd_payload(20, 0));
  fo4::test::write_subrecord(out, "DSTF", Bytes{});
  fo4::test::write_subrecord(out, "DATA", Bytes(92));
  fo4::test::write_subrecord(out, "DNAM", Bytes(4));
  fo4::test::write_subrecord(out, "NAM1", std::string_view{"m.nif\0", 6});
  fo4::test::write_subrecord(out, "VNAM", Bytes(4));
  fo4::test::end_record(out, headerless);
  fo4::test::end_group(out, group);

  const auto stat_group = fo4::test::begin_group(out, "STAT", 0);
  const auto stat = fo4::test::begin_record(out, "STAT", 0, 0x01000D03);
  fo4::test::write_subrecord(out, "EDID", std::string_view{"Prop\0", 5});
  fo4::test::write_subrecord(out, "DSTD", dstd_payload(20, 0));
  fo4::test::write_subrecord(out, "DSTF", Bytes{});
  fo4::test::end_record(out, stat);
  fo4::test::end_group(out, stat_group);
  return out.bytes;
}

std::string signature_order(const fo4::Plugin& plugin, const fo4::FormKey& key) {
  const auto* record = fo4::find_record(plugin, key);
  if (record == nullptr) return {};
  std::string order;
  for (const auto& subrecord : record->subrecords) {
    if (!order.empty()) order.push_back(' ');
    order += subrecord.signature.to_string();
  }
  return order;
}

const fo4::Subrecord* subrecord_at(const fo4::Plugin& plugin, const fo4::FormKey& key,
                                   std::string_view signature, std::size_t ordinal) {
  const auto* record = fo4::find_record(plugin, key);
  if (record == nullptr) return nullptr;
  std::size_t seen = 0;
  for (const auto& subrecord : record->subrecords) {
    if (subrecord.signature.to_string() != signature) continue;
    if (seen == ordinal) return &subrecord;
    ++seen;
  }
  return nullptr;
}

Bytes payload_at(const fo4::Plugin& plugin, const fo4::FormKey& key, std::string_view signature,
                 std::size_t ordinal) {
  const auto* subrecord = subrecord_at(plugin, key, signature, ordinal);
  if (subrecord == nullptr) {
    ADD_FAILURE() << signature;
    return {};
  }
  return subrecord->data;
}

std::uint8_t byte_at(const Bytes& data, std::size_t offset) {
  return std::to_integer<std::uint8_t>(data[offset]);
}

fo4::Signature sig(std::string_view text) { return fo4::Signature::from_string(text); }

}  // namespace

TEST(DestructibleStageMembers, InsertPlacesTheMemberInsideTheAddressedStage) {
  auto plugin = fo4::read_plugin(make_plugin(), "Patch.esp");
  ASSERT_EQ(signature_order(plugin, staged_key()), k_staged_order);

  EXPECT_EQ(fo4::insert_destructible_stage_member(plugin, staged_key(), 0, sig("DSTA")),
            fo4::DestructibleStageMemberStatus::updated);
  EXPECT_EQ(signature_order(plugin, staged_key()),
            "EDID OBND MODL MODT DEST DSTD DSTA DMDL DSTF DSTD DSTF DATA DNAM NAM1 VNAM");

  EXPECT_EQ(fo4::insert_destructible_stage_member(plugin, staged_key(), 0, sig("DMDC")),
            fo4::DestructibleStageMemberStatus::updated);
  EXPECT_EQ(signature_order(plugin, staged_key()),
            "EDID OBND MODL MODT DEST DSTD DSTA DMDL DMDC DSTF DSTD DSTF DATA DNAM NAM1 VNAM");

  EXPECT_EQ(fo4::insert_destructible_stage_member(plugin, staged_key(), 0, sig("DMDS")),
            fo4::DestructibleStageMemberStatus::updated);
  EXPECT_EQ(signature_order(plugin, staged_key()),
            "EDID OBND MODL MODT DEST DSTD DSTA DMDL DMDC DMDS DSTF DSTD DSTF DATA DNAM NAM1 VNAM");
  const auto swap_row = fo4::schema::formlink_field_descriptor(
                            fo4::Signature::from_string("PROJ"), "PROJ\\DestructionStageMaterialSwap")
                            .value();
  EXPECT_EQ(fo4::set_record_formlink_field(plugin, staged_key(), swap_row, 0,
                                           fo4::FormKey::parse("000A30:Fallout4.esm")),
            fo4::FormLinkFieldMutationStatus::updated);

  EXPECT_EQ(fo4::insert_destructible_stage_member(plugin, staged_key(), 1, sig("DSTA")),
            fo4::DestructibleStageMemberStatus::updated);
  EXPECT_EQ(signature_order(plugin, staged_key()),
            "EDID OBND MODL MODT DEST DSTD DSTA DMDL DMDC DMDS DSTF DSTD DSTA DSTF DATA DNAM NAM1 VNAM");

  EXPECT_TRUE(fo4::test::original_bytes(plugin).empty());
  const auto reread = fo4::read_plugin(fo4::write_plugin(plugin), "Patch.esp");
  EXPECT_EQ(signature_order(reread, staged_key()),
            "EDID OBND MODL MODT DEST DSTD DSTA DMDL DMDC DMDS DSTF DSTD DSTA DSTF DATA DNAM NAM1 VNAM");

  const auto dest = payload_at(reread, staged_key(), "DEST", 0);
  ASSERT_EQ(dest.size(), 8u);
  EXPECT_EQ(byte_at(dest, 4), 2u);

  const auto first_sequence = payload_at(reread, staged_key(), "DSTA", 0);
  ASSERT_EQ(first_sequence.size(), 1u);
  EXPECT_EQ(byte_at(first_sequence, 0), 0u);
  const auto second_sequence = payload_at(reread, staged_key(), "DSTA", 1);
  ASSERT_EQ(second_sequence.size(), 1u);
  EXPECT_EQ(byte_at(second_sequence, 0), 0u);

  const auto color = payload_at(reread, staged_key(), "DMDC", 0);
  ASSERT_EQ(color.size(), 4u);
  EXPECT_EQ(byte_at(color, 0), 0u);
  EXPECT_EQ(byte_at(color, 3), 0u);

  const auto swap_bytes = payload_at(reread, staged_key(), "DMDS", 0);
  ASSERT_EQ(swap_bytes.size(), 4u);
  EXPECT_EQ(byte_at(swap_bytes, 0), 0x30u);
  EXPECT_EQ(byte_at(swap_bytes, 1), 0x0Au);
  EXPECT_EQ(byte_at(swap_bytes, 2), 0u);
  EXPECT_EQ(byte_at(swap_bytes, 3), 0u);

  const auto model = payload_at(reread, staged_key(), "DMDL", 0);
  ASSERT_EQ(model.size(), 6u);
  EXPECT_EQ(byte_at(model, 0), static_cast<std::uint8_t>('a'));
}

TEST(DestructibleStageMembers, InsertRefusesMembersWithoutADefaultOrASlot) {
  auto plugin = fo4::read_plugin(make_plugin(), "Patch.esp");

  EXPECT_EQ(fo4::insert_destructible_stage_member(plugin, staged_key(), 0, sig("DMDT")),
            fo4::DestructibleStageMemberStatus::no_default);
  for (const char* name : {"DSTD", "DSTF", "DEST", "DAMC", "EDID", "DATA"}) {
    EXPECT_EQ(fo4::insert_destructible_stage_member(plugin, staged_key(), 0, sig(name)),
              fo4::DestructibleStageMemberStatus::not_optional_member)
        << name;
  }
  EXPECT_EQ(fo4::insert_destructible_stage_member(plugin, staged_key(), 0, sig("DMDL")),
            fo4::DestructibleStageMemberStatus::member_present);
  EXPECT_EQ(fo4::insert_destructible_stage_member(plugin, staged_key(), 2, sig("DSTA")),
            fo4::DestructibleStageMemberStatus::stage_index_out_of_range);
  EXPECT_EQ(fo4::insert_destructible_stage_member(
                plugin, fo4::FormKey::parse("000D02:Patch.esp"), 0, sig("DSTA")),
            fo4::DestructibleStageMemberStatus::header_missing);
  EXPECT_EQ(fo4::insert_destructible_stage_member(
                plugin, fo4::FormKey::parse("000D03:Patch.esp"), 0, sig("DSTA")),
            fo4::DestructibleStageMemberStatus::record_mismatch);
  EXPECT_EQ(fo4::insert_destructible_stage_member(
                plugin, fo4::FormKey::parse("000D09:Patch.esp"), 0, sig("DSTA")),
            fo4::DestructibleStageMemberStatus::record_missing);

  EXPECT_FALSE(fo4::test::original_bytes(plugin).empty());
  EXPECT_EQ(signature_order(plugin, staged_key()), k_staged_order);
}

TEST(DestructibleStageMembers, RemoveDropsOnlyTheAddressedStageMember) {
  auto plugin = fo4::read_plugin(make_plugin(), "Patch.esp");
  ASSERT_EQ(fo4::insert_destructible_stage_member(plugin, staged_key(), 1, sig("DSTA")),
            fo4::DestructibleStageMemberStatus::updated);
  EXPECT_TRUE(fo4::test::original_bytes(plugin).empty());
  ASSERT_EQ(signature_order(plugin, staged_key()),
            "EDID OBND MODL MODT DEST DSTD DMDL DSTF DSTD DSTA DSTF DATA DNAM NAM1 VNAM");

  EXPECT_EQ(fo4::remove_destructible_stage_member(plugin, staged_key(), 0, sig("DMDL")),
            fo4::DestructibleStageMemberStatus::updated);
  EXPECT_EQ(signature_order(plugin, staged_key()),
            "EDID OBND MODL MODT DEST DSTD DSTF DSTD DSTA DSTF DATA DNAM NAM1 VNAM");

  EXPECT_EQ(fo4::remove_destructible_stage_member(plugin, staged_key(), 0, sig("DMDL")),
            fo4::DestructibleStageMemberStatus::member_absent);
  EXPECT_EQ(fo4::remove_destructible_stage_member(plugin, staged_key(), 0, sig("DMDT")),
            fo4::DestructibleStageMemberStatus::member_absent);
  for (const char* name : {"DSTD", "DSTF", "DEST", "DAMC"}) {
    EXPECT_EQ(fo4::remove_destructible_stage_member(plugin, staged_key(), 0, sig(name)),
              fo4::DestructibleStageMemberStatus::not_optional_member)
        << name;
  }
  EXPECT_EQ(fo4::remove_destructible_stage_member(plugin, staged_key(), 4, sig("DSTA")),
            fo4::DestructibleStageMemberStatus::stage_index_out_of_range);
  EXPECT_EQ(fo4::remove_destructible_stage_member(
                plugin, fo4::FormKey::parse("000D02:Patch.esp"), 0, sig("DSTA")),
            fo4::DestructibleStageMemberStatus::header_missing);
  EXPECT_EQ(fo4::remove_destructible_stage_member(
                plugin, fo4::FormKey::parse("000D03:Patch.esp"), 0, sig("DSTA")),
            fo4::DestructibleStageMemberStatus::record_mismatch);

  EXPECT_EQ(fo4::remove_destructible_stage_member(plugin, staged_key(), 1, sig("DSTA")),
            fo4::DestructibleStageMemberStatus::updated);
  EXPECT_EQ(signature_order(plugin, staged_key()),
            "EDID OBND MODL MODT DEST DSTD DSTF DSTD DSTF DATA DNAM NAM1 VNAM");

  const auto reread = fo4::read_plugin(fo4::write_plugin(plugin), "Patch.esp");
  EXPECT_EQ(signature_order(reread, staged_key()),
            "EDID OBND MODL MODT DEST DSTD DSTF DSTD DSTF DATA DNAM NAM1 VNAM");
  const auto dest = payload_at(reread, staged_key(), "DEST", 0);
  ASSERT_EQ(dest.size(), 8u);
  EXPECT_EQ(byte_at(dest, 4), 2u);
}

TEST(DestructibleStageMembers, InsertRefusesModelFollowersWithoutDMDL) {
  auto plugin = fo4::read_plugin(make_plugin(), "Patch.esp");
  const auto before = fo4::write_plugin(plugin);

  EXPECT_EQ(fo4::insert_destructible_stage_member(plugin, staged_key(), 1, sig("DMDC")),
            fo4::DestructibleStageMemberStatus::model_required);
  EXPECT_EQ(fo4::insert_destructible_stage_member(plugin, staged_key(), 1, sig("DMDS")),
            fo4::DestructibleStageMemberStatus::model_required);
  EXPECT_EQ(fo4::write_plugin(plugin), before);
  EXPECT_EQ(signature_order(plugin, staged_key()), k_staged_order);

  EXPECT_EQ(fo4::insert_destructible_stage_member(plugin, staged_key(), 1, sig("DMDL")),
            fo4::DestructibleStageMemberStatus::updated);
  EXPECT_EQ(fo4::insert_destructible_stage_member(plugin, staged_key(), 1, sig("DMDC")),
            fo4::DestructibleStageMemberStatus::updated);
  EXPECT_EQ(signature_order(plugin, staged_key()),
            "EDID OBND MODL MODT DEST DSTD DMDL DSTF DSTD DMDL DMDC DSTF DATA DNAM NAM1 VNAM");
}

TEST(DestructibleStageMembers, RemoveRefusesDMDLWhileAModelFollowerRemains) {
  auto plugin = fo4::read_plugin(make_plugin(), "Patch.esp");
  ASSERT_EQ(fo4::insert_destructible_stage_member(plugin, staged_key(), 0, sig("DMDC")),
            fo4::DestructibleStageMemberStatus::updated);
  ASSERT_EQ(fo4::insert_destructible_stage_member(plugin, staged_key(), 0, sig("DMDS")),
            fo4::DestructibleStageMemberStatus::updated);
  ASSERT_EQ(signature_order(plugin, staged_key()),
            "EDID OBND MODL MODT DEST DSTD DMDL DMDC DMDS DSTF DSTD DSTF DATA DNAM NAM1 VNAM");
  const auto before = fo4::write_plugin(plugin);

  EXPECT_EQ(fo4::remove_destructible_stage_member(plugin, staged_key(), 0, sig("DMDL")),
            fo4::DestructibleStageMemberStatus::model_required);
  EXPECT_EQ(fo4::write_plugin(plugin), before);

  EXPECT_EQ(fo4::remove_destructible_stage_member(plugin, staged_key(), 0, sig("DMDC")),
            fo4::DestructibleStageMemberStatus::updated);
  EXPECT_EQ(fo4::remove_destructible_stage_member(plugin, staged_key(), 0, sig("DMDL")),
            fo4::DestructibleStageMemberStatus::model_required);
  EXPECT_EQ(fo4::remove_destructible_stage_member(plugin, staged_key(), 0, sig("DMDS")),
            fo4::DestructibleStageMemberStatus::updated);
  EXPECT_EQ(fo4::remove_destructible_stage_member(plugin, staged_key(), 0, sig("DMDL")),
            fo4::DestructibleStageMemberStatus::updated);
  EXPECT_EQ(signature_order(plugin, staged_key()),
            "EDID OBND MODL MODT DEST DSTD DSTF DSTD DSTF DATA DNAM NAM1 VNAM");
}
