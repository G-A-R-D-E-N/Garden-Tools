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
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

using Bytes = std::vector<std::byte>;
using fo4::test::ByteWriter;

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
  hedr.u32(1);
  hedr.u32(0x911);
  fo4::test::write_subrecord(out, "HEDR", hedr.bytes);
  fo4::test::write_subrecord(out, "MAST", std::string_view{"Fallout4.esm\0", 13});
  fo4::test::write_subrecord(out, "DATA", Bytes(8));
  fo4::test::write_subrecord(out, "MAST", std::string_view{"Objects.esp\0", 12});
  fo4::test::write_subrecord(out, "DATA", Bytes(8));
  fo4::test::end_record(out, tes4);

  add_record(out, "KYWD", 0xA30, {});
  add_record(out, "KYWD", 0xA31, {});
  add_record(out, "SNDR", 0xA40, {});
  add_record(out, "SNDR", 0xA41, {});
  add_record(out, "NPC_", 0x800, {{"KWDA", u32_words({0x00000A30u})}, {"DATA", Bytes(4)}});
  add_record(out, "NPC_", 0x801, {{"APPR", u32_words({0x02000A30u, 0x02000A31u})}});
  add_record(out, "NPC_", 0x802,
             {{"CS2K", u32_bytes(0x00000A30)},
              {"CS2D", u32_bytes(0x00000A40)},
              {"CS2K", u32_bytes(0x00000A31)},
              {"CS2D", u32_bytes(0x00000A41)},
              {"CS2F", Bytes{std::byte{0}}}});
  add_record(out, "FURN", 0x900, {{"XMRK", Bytes(7)}});
  add_record(out, "FURN", 0x901,
             {{"COCT", u32_bytes(1)}, {"CNTO", u32_words({0x02000A30u, 1u})}});
  return out;
}

fo4::schema::FormLinkListDescriptor list_row(std::string_view record, std::string_view path) {
  const auto field = fo4::schema::formlink_list_descriptor(fo4::Signature::from_string(record), path);
  EXPECT_TRUE(field.has_value()) << path;
  return field.value();
}

fo4::schema::FormLinkFieldDescriptor formlink_row(std::string_view path) {
  const auto field = fo4::schema::formlink_field_descriptor(path);
  EXPECT_TRUE(field.has_value()) << path;
  return field.value();
}

fo4::schema::ScalarFieldDescriptor scalar_row(std::string_view path) {
  const auto field = fo4::schema::scalar_field_descriptor(path);
  EXPECT_TRUE(field.has_value()) << path;
  return field.value();
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

Bytes payloads_of(const fo4::Plugin& plugin, std::string_view key, std::string_view signature,
                  std::size_t index = 0) {
  const auto* record = fo4::find_record(plugin, fo4::FormKey::parse(key));
  if (record == nullptr) {
    ADD_FAILURE() << key;
    return {};
  }
  const auto wanted = fo4::Signature::from_string(signature);
  std::size_t seen = 0;
  for (const auto& sub : record->subrecords) {
    if (sub.signature != wanted) continue;
    if (seen == index) return sub.data;
    ++seen;
  }
  ADD_FAILURE() << key << ' ' << signature << ' ' << index;
  return {};
}

}  // namespace

TEST(NpcFurnAttachSlots, RowsDeclareAttachParentSlotsAndActorSounds) {
  for (const auto& entry : {std::pair<std::string_view, std::string_view>{"NPC_", "NPC_\\AttachParentSlots"},
                            {"FURN", "FURN\\AttachParentSlots"}}) {
    const auto appr = list_row(entry.first, entry.second);
    EXPECT_EQ(appr.storage, fo4::schema::FormLinkListStorage::packed_array) << entry.second;
    EXPECT_EQ(appr.subrecord, fo4::Signature::from_string("APPR")) << entry.second;
    EXPECT_FALSE(appr.nullable) << entry.second;
    EXPECT_TRUE(appr.can_insert) << entry.second;
    EXPECT_TRUE(fo4::schema::formlink_list_target_allowed(appr, fo4::Signature::from_string("KYWD")))
        << entry.second;
    EXPECT_FALSE(fo4::schema::formlink_list_target_allowed(appr, fo4::Signature::from_string("SNDR")))
        << entry.second;
  }

  const auto keyword = formlink_row("NPC_\\ActorSoundKeyword");
  EXPECT_EQ(keyword.subrecord, fo4::Signature::from_string("CS2K"));
  EXPECT_EQ(keyword.offset, 0u);
  EXPECT_FALSE(keyword.nullable);
  EXPECT_TRUE(keyword.address.repeated);
  EXPECT_EQ(keyword.payload_sizes[0], 4u);
  EXPECT_TRUE(fo4::schema::formlink_field_target_allowed(keyword, fo4::Signature::from_string("KYWD")));

  const auto sound = formlink_row("NPC_\\ActorSoundSound");
  EXPECT_EQ(sound.subrecord, fo4::Signature::from_string("CS2D"));
  EXPECT_FALSE(sound.nullable);
  EXPECT_TRUE(sound.address.repeated);
  EXPECT_EQ(sound.payload_sizes[0], 4u);
  EXPECT_TRUE(fo4::schema::formlink_field_target_allowed(sound, fo4::Signature::from_string("SNDR")));

  const auto finalize = scalar_row("NPC_\\ActorSoundsFinalize");
  EXPECT_EQ(finalize.scalar.subrecord, fo4::Signature::from_string("CS2F"));
  EXPECT_EQ(finalize.scalar.storage, fo4::schema::ScalarStorage::u8);
  EXPECT_FALSE(finalize.scalar.embedded);
  EXPECT_TRUE(finalize.scalar.address.repeated);
}

TEST(NpcFurnAttachSlots, EmptyAttachParentSlotsAnchorsAfterItsPredecessor) {
  auto plugin = fo4::read_plugin(make_plugin().bytes, "Patch.esp");
  const auto updated = fo4::schema::FormLinkListMutationStatus::updated;
  const auto npc = fo4::FormKey::parse("000800:Patch.esp");
  const auto furn = fo4::FormKey::parse("000900:Patch.esp");
  const auto keyword = fo4::FormKey::parse("000A30:Patch.esp");
  const auto keyword2 = fo4::FormKey::parse("000A31:Patch.esp");

  const auto npc_appr = list_row("NPC_", "NPC_\\AttachParentSlots");
  const auto furn_appr = list_row("FURN", "FURN\\AttachParentSlots");
  EXPECT_EQ(fo4::schema::record_formlink_list_size(plugin, npc, npc_appr), 0u);
  EXPECT_EQ(fo4::schema::record_formlink_list_size(plugin, furn, furn_appr), 0u);

  EXPECT_EQ(fo4::schema::insert_record_formlink_list_element(plugin, npc, npc_appr, 0, keyword), updated);
  EXPECT_EQ(signature_list(plugin, "000800:Patch.esp"), "EDID,KWDA,APPR,DATA");
  EXPECT_EQ(payloads_of(plugin, "000800:Patch.esp", "APPR"), u32_bytes(0x02000A30u));

  EXPECT_EQ(fo4::schema::insert_record_formlink_list_element(plugin, furn, furn_appr, 0, keyword2),
            updated);
  EXPECT_EQ(signature_list(plugin, "000900:Patch.esp"), "EDID,XMRK,APPR");

  const auto furn_items = fo4::FormKey::parse("000901:Patch.esp");
  EXPECT_EQ(fo4::schema::insert_record_formlink_list_element(plugin, furn_items, furn_appr, 0,
                                                             keyword2),
            updated);
  EXPECT_EQ(signature_list(plugin, "000901:Patch.esp"), "EDID,COCT,CNTO,APPR");

  EXPECT_EQ(fo4::schema::insert_record_formlink_list_element(plugin, npc, npc_appr, 1, keyword2), updated);
  EXPECT_EQ(fo4::schema::record_formlink_list_size(plugin, npc, npc_appr), 2u);
  EXPECT_EQ(fo4::schema::insert_record_formlink_list_element(plugin, npc, npc_appr, 3, keyword2),
            fo4::schema::FormLinkListMutationStatus::index_out_of_range);
  EXPECT_EQ(fo4::schema::insert_record_formlink_list_element(plugin, npc, npc_appr, 0,
                                                             fo4::FormKey::parse("000A40:Patch.esp")),
            fo4::schema::FormLinkListMutationStatus::target_type_mismatch);

  const auto reread = fo4::read_plugin(fo4::write_plugin(plugin), "Patch.esp");
  EXPECT_EQ(payloads_of(reread, "000800:Patch.esp", "APPR"), u32_words({0x02000A30u, 0x02000A31u}));
  EXPECT_EQ(payloads_of(reread, "000900:Patch.esp", "APPR"), u32_bytes(0x02000A31u));
  EXPECT_EQ(fo4::schema::record_formlink_list_size(reread, npc, npc_appr), 2u);

  EXPECT_EQ(fo4::schema::remove_record_formlink_list_element(plugin, npc, npc_appr, 0), updated);
  EXPECT_EQ(payloads_of(plugin, "000800:Patch.esp", "APPR"), u32_bytes(0x02000A31u));
}

TEST(NpcFurnAttachSlots, ExistingAttachParentSlotsIsReadAndRetargeted) {
  auto plugin = fo4::read_plugin(make_plugin().bytes, "Patch.esp");
  const auto npc = fo4::FormKey::parse("000801:Patch.esp");
  const auto appr = list_row("NPC_", "NPC_\\AttachParentSlots");
  const auto values = fo4::schema::record_formlink_list_values(plugin, npc, appr);
  ASSERT_TRUE(values.has_value());
  ASSERT_EQ(values->size(), 2u);
  EXPECT_EQ((*values)[0], fo4::FormKey::parse("000A30:Patch.esp"));
  EXPECT_EQ((*values)[1], fo4::FormKey::parse("000A31:Patch.esp"));

  EXPECT_EQ(fo4::schema::set_record_formlink_list_element(plugin, npc, appr, 1, fo4::FormKey::parse(
                "000A30:Patch.esp")),
            fo4::schema::FormLinkListMutationStatus::updated);
  const auto reread = fo4::read_plugin(fo4::write_plugin(plugin), "Patch.esp");
  const auto after = fo4::schema::record_formlink_list_values(reread, npc, appr);
  ASSERT_TRUE(after.has_value());
  ASSERT_EQ(after->size(), 2u);
  EXPECT_EQ((*after)[1], fo4::FormKey::parse("000A30:Patch.esp"));
  EXPECT_EQ((*after)[0], fo4::FormKey::parse("000A30:Patch.esp"));
}

TEST(NpcFurnAttachSlots, ActorSoundPairsAreReadAndRetargetedIndependently) {
  auto plugin = fo4::read_plugin(make_plugin().bytes, "Patch.esp");
  const auto npc = fo4::FormKey::parse("000802:Patch.esp");
  const auto keyword = formlink_row("NPC_\\ActorSoundKeyword");
  const auto sound = formlink_row("NPC_\\ActorSoundSound");
  const auto target = fo4::FormKey::parse("000A40:Patch.esp");

  EXPECT_EQ(fo4::set_record_formlink_field(plugin, npc, keyword, 1, fo4::FormKey::parse(
                "000A31:Patch.esp")),
            fo4::FormLinkFieldMutationStatus::updated);
  EXPECT_EQ(fo4::set_record_formlink_field(plugin, npc, sound, 0, target),
            fo4::FormLinkFieldMutationStatus::updated);
  EXPECT_EQ(fo4::set_record_formlink_field(plugin, npc, sound, 2, target),
            fo4::FormLinkFieldMutationStatus::subrecord_missing);
  EXPECT_THROW(fo4::set_record_formlink_field(plugin, npc, sound, 1, fo4::FormKey::parse(
                   "000A30:Patch.esp")),
               fo4::schema::FormLinkFieldError);

  EXPECT_EQ(fo4::set_record_scalar(plugin, npc, scalar_row("NPC_\\ActorSoundsFinalize"), 0, "1"),
            fo4::ScalarMutationStatus::updated);

  const auto reread = fo4::read_plugin(fo4::write_plugin(plugin), "Patch.esp");
  EXPECT_EQ(signature_list(reread, "000802:Patch.esp"), "EDID,CS2K,CS2D,CS2K,CS2D,CS2F");
  EXPECT_EQ(payloads_of(reread, "000802:Patch.esp", "CS2K", 0), u32_bytes(0x00000A30u));
  EXPECT_EQ(payloads_of(reread, "000802:Patch.esp", "CS2K", 1), u32_bytes(0x02000A31u));
  EXPECT_EQ(payloads_of(reread, "000802:Patch.esp", "CS2D", 0), u32_bytes(0x02000A40u));
  EXPECT_EQ(payloads_of(reread, "000802:Patch.esp", "CS2D", 1), u32_bytes(0x00000A41u));
  EXPECT_EQ(payloads_of(reread, "000802:Patch.esp", "CS2F"), Bytes{std::byte{1}});
}
