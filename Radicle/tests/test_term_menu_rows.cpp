#include "fo4/plugin/mutation.hpp"
#include "fo4/plugin/reader.hpp"
#include "fo4/plugin/subrecord_authoring.hpp"
#include "fo4/plugin/writer.hpp"
#include "fo4/schema/formlink_lists.hpp"
#include "fo4/schema/scalar.hpp"
#include "plugin_bytes.hpp"

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace {

using Bytes = std::vector<std::byte>;

constexpr auto k_term = fo4::Signature::from_string("TERM");
constexpr auto k_coct = fo4::Signature::from_string("COCT");
constexpr auto k_cnto = fo4::Signature::from_string("CNTO");

struct Piece {
  std::string_view signature;
  Bytes data;
};

fo4::FormKey record_key() { return fo4::FormKey::parse("000800:Patch.esp"); }
fo4::FormKey note_key() { return fo4::FormKey::parse("000910:Patch.esp"); }
fo4::FormKey other_term_key() { return fo4::FormKey::parse("000930:Patch.esp"); }

fo4::schema::SubrecordGroupDescriptor holotapes_group() {
  return fo4::schema::subrecord_group_descriptor(k_term, "TERM\\Holotapes").value();
}

fo4::schema::ScalarFieldDescriptor scalar(std::string_view path) {
  return fo4::schema::scalar_field_descriptor(path).value();
}

fo4::schema::FormLinkFieldDescriptor link(std::string_view path) {
  return fo4::schema::formlink_field_descriptor(path).value();
}

Bytes zeros(std::size_t size) { return Bytes(size); }

Bytes u32_bytes(std::uint32_t value) {
  fo4::test::ByteWriter out;
  out.u32(value);
  return out.bytes;
}

Bytes cnto_bytes(std::uint32_t item, std::int32_t count) {
  fo4::test::ByteWriter out;
  out.u32(item);
  out.i32(count);
  return out.bytes;
}

Bytes zstring(std::string_view text) {
  fo4::test::ByteWriter out;
  out.raw(text);
  out.u8(0);
  return out.bytes;
}

Bytes anam_bytes(std::uint8_t type) {
  fo4::test::ByteWriter out;
  out.u8(type);
  return out.bytes;
}

Bytes itid_bytes(std::uint16_t id) {
  fo4::test::ByteWriter out;
  out.u16(id);
  return out.bytes;
}

std::vector<Piece> term_base() {
  return {
      {"EDID", zstring("Terminal")},
      {"COCT", u32_bytes(2)},
      {"CNTO", cnto_bytes(0x910, 1)},
      {"CNTO", cnto_bytes(0x911, 2)},
      {"MNAM", u32_bytes(0)},
      {"WBDT", zeros(1)},
      {"XMRK", zstring("Marker.nif")},
      {"BSIZ", u32_bytes(2)},
      {"BTXT", zstring("Body one")},
      {"BTXT", zstring("Body two")},
      {"ISIZ", u32_bytes(2)},
      {"ITXT", zstring("Item one")},
      {"RNAM", zstring("Response one")},
      {"ANAM", anam_bytes(4)},
      {"ITID", itid_bytes(1)},
      {"UNAM", zstring("Display one")},
      {"VNAM", zstring("Image.dds")},
      {"TNAM", u32_bytes(0x920)},
      {"ITXT", zstring("Item two")},
      {"ANAM", anam_bytes(8)},
      {"ITID", itid_bytes(2)},
  };
}

std::vector<std::byte> make_plugin(const std::vector<Piece>& terminal) {
  fo4::test::ByteWriter out;
  const auto tes4 = fo4::test::begin_record(out, "TES4", 0, 0);
  fo4::test::ByteWriter hedr;
  hedr.f32(1.0f);
  hedr.u32(1);
  hedr.u32(0x911);
  fo4::test::write_subrecord(out, "HEDR", hedr.bytes);
  fo4::test::end_record(out, tes4);

  const auto group = fo4::test::begin_group(out, "TERM", 0);
  const auto record = fo4::test::begin_record(out, "TERM", 0, 0x00000800);
  for (const auto& piece : terminal) {
    fo4::test::write_subrecord(out, piece.signature, piece.data);
  }
  fo4::test::end_record(out, record);
  fo4::test::end_group(out, group);

  const auto note_group = fo4::test::begin_group(out, "NOTE", 0);
  const auto note = fo4::test::begin_record(out, "NOTE", 0, 0x00000910);
  fo4::test::end_record(out, note);
  fo4::test::end_group(out, note_group);

  const auto term_group = fo4::test::begin_group(out, "TERM", 0);
  const auto submenu = fo4::test::begin_record(out, "TERM", 0, 0x00000920);
  fo4::test::end_record(out, submenu);
  const auto other = fo4::test::begin_record(out, "TERM", 0, 0x00000930);
  fo4::test::end_record(out, other);
  fo4::test::end_group(out, term_group);
  return out.bytes;
}

fo4::Plugin load(const std::vector<Piece>& terminal = term_base()) {
  return fo4::read_plugin(make_plugin(terminal), "Patch.esp");
}

fo4::Plugin reparse(const fo4::Plugin& plugin) {
  return fo4::read_plugin(fo4::write_plugin(plugin), "Patch.esp");
}

const fo4::Record& record_of(const fo4::Plugin& plugin) {
  return *fo4::find_record(plugin, record_key());
}

std::uint32_t read_u32(const Bytes& data, std::size_t offset) {
  std::uint32_t value = 0;
  for (std::size_t i = 0; i < 4; ++i) {
    value |= static_cast<std::uint32_t>(std::to_integer<std::uint8_t>(data[offset + i])) << (8 * i);
  }
  return value;
}

std::vector<std::string> signatures(const fo4::Plugin& plugin) {
  std::vector<std::string> names;
  for (const auto& subrecord : record_of(plugin).subrecords) {
    names.push_back(subrecord.signature.to_string());
  }
  return names;
}

std::vector<Bytes> payloads(const fo4::Plugin& plugin, std::string_view signature) {
  std::vector<Bytes> out;
  for (const auto& subrecord : record_of(plugin).subrecords) {
    if (subrecord.signature.to_string() == signature) out.push_back(subrecord.data);
  }
  return out;
}

std::string read_member(const fo4::Plugin& plugin, std::string_view path, std::size_t index) {
  const auto field = scalar(path);
  const auto& record = record_of(plugin);
  const auto resolved = fo4::schema::resolve_field_slots(
      record, fo4::schema::scalar_field_shape(field.scalar), field.scalar.address);
  const auto slot = resolved.slots.at(index);
  return fo4::schema::decode_scalar(
      fo4::schema::scalar_slot_bytes(record.subrecords[slot.subrecord].data, slot, field.scalar),
      field.scalar);
}

std::optional<std::uint32_t> cnto_item(const fo4::Plugin& plugin, std::size_t run) {
  std::size_t seen = 0;
  for (const auto& subrecord : record_of(plugin).subrecords) {
    if (subrecord.signature != k_cnto) continue;
    if (seen++ == run) return read_u32(subrecord.data, 0);
  }
  return std::nullopt;
}

std::optional<std::uint32_t> stored_count(const fo4::Plugin& plugin) {
  for (const auto& subrecord : record_of(plugin).subrecords) {
    if (subrecord.signature == k_coct && subrecord.data.size() == 4) return read_u32(subrecord.data, 0);
  }
  return std::nullopt;
}

}  // namespace

TEST(TermMenuRows, RowsDescribeBodyTextAndMenuItemMembers) {
  const auto body = scalar("TERM\\BodyText");
  EXPECT_EQ(body.record, k_term);
  EXPECT_EQ(body.scalar.subrecord, fo4::Signature::from_string("BTXT"));
  EXPECT_FALSE(body.scalar.embedded);
  EXPECT_TRUE(body.scalar.address.repeated);

  const auto item_text = scalar("TERM\\ItemText");
  EXPECT_EQ(item_text.scalar.subrecord, fo4::Signature::from_string("ITXT"));
  EXPECT_TRUE(item_text.scalar.address.repeated);
  EXPECT_EQ(scalar("TERM\\ItemResponseText").scalar.subrecord, fo4::Signature::from_string("RNAM"));
  EXPECT_EQ(scalar("TERM\\ItemDisplayText").scalar.subrecord, fo4::Signature::from_string("UNAM"));
  EXPECT_EQ(scalar("TERM\\ItemImage").scalar.subrecord, fo4::Signature::from_string("VNAM"));

  const auto type = scalar("TERM\\ItemType");
  EXPECT_EQ(type.scalar.subrecord, fo4::Signature::from_string("ANAM"));
  EXPECT_EQ(type.scalar.storage, fo4::schema::ScalarStorage::u8);
  EXPECT_EQ(type.scalar.presentation, fo4::schema::ScalarPresentation::enumeration);
  EXPECT_TRUE(type.scalar.address.repeated);
  ASSERT_EQ(type.scalar.names.size(), 17u);
  EXPECT_EQ(type.scalar.names[4].name, "Submenu - Terminal");
  EXPECT_EQ(type.scalar.names[16].name, "Display Image");

  const auto id = scalar("TERM\\ItemId");
  EXPECT_EQ(id.scalar.subrecord, fo4::Signature::from_string("ITID"));
  EXPECT_EQ(id.scalar.storage, fo4::schema::ScalarStorage::u16);
  EXPECT_TRUE(id.scalar.address.repeated);

  const auto submenu = link("TERM\\ItemSubmenu");
  EXPECT_EQ(submenu.record, k_term);
  EXPECT_EQ(submenu.subrecord, fo4::Signature::from_string("TNAM"));
  EXPECT_EQ(submenu.offset, 0u);
  EXPECT_FALSE(submenu.nullable);
  EXPECT_TRUE(submenu.address.repeated);
  EXPECT_TRUE(fo4::schema::formlink_field_target_allowed(submenu, k_term));
  EXPECT_FALSE(fo4::schema::formlink_field_target_allowed(submenu, fo4::Signature::from_string("NOTE")));
}

TEST(TermMenuRows, HolotapeGroupNamesItsCountAndItsMember) {
  const auto group = holotapes_group();
  EXPECT_EQ(group.record, k_term);
  EXPECT_EQ(group.path, "TERM\\Holotapes");
  EXPECT_EQ(group.count_subrecord, k_coct);
  ASSERT_EQ(group.members.size(), 1u);
  EXPECT_EQ(group.members[0].subrecord, k_cnto);
  EXPECT_TRUE(group.members[0].required);
  EXPECT_FALSE(group.members[0].repeatable);
  EXPECT_EQ(group.members[0].payload_size, 8u);
  EXPECT_EQ(fo4::schema::subrecord_group_for_member(k_term, k_cnto)->path, "TERM\\Holotapes");
  EXPECT_TRUE(fo4::schema::is_subrecord_group_count_subrecord(k_term, k_coct));
  EXPECT_FALSE(fo4::schema::is_subrecord_group_count_subrecord(k_term, k_cnto));
  EXPECT_EQ(scalar("TERM\\HolotapeTotal").scalar.subrecord, k_coct);
  EXPECT_EQ(scalar("TERM\\HolotapeCount").scalar.subrecord, k_cnto);
  EXPECT_EQ(scalar("TERM\\HolotapeCount").scalar.offset, 4u);
}

TEST(TermMenuRows, ReadsBodyTextAndMenuItemsByOccurrence) {
  const auto plugin = load();
  EXPECT_EQ(read_member(plugin, "TERM\\BodyText", 0), "Body one");
  EXPECT_EQ(read_member(plugin, "TERM\\BodyText", 1), "Body two");
  EXPECT_EQ(read_member(plugin, "TERM\\ItemText", 0), "Item one");
  EXPECT_EQ(read_member(plugin, "TERM\\ItemText", 1), "Item two");
  EXPECT_EQ(read_member(plugin, "TERM\\ItemResponseText", 0), "Response one");
  EXPECT_EQ(read_member(plugin, "TERM\\ItemDisplayText", 0), "Display one");
  EXPECT_EQ(read_member(plugin, "TERM\\ItemImage", 0), "Image.dds");
  EXPECT_EQ(read_member(plugin, "TERM\\ItemType", 0), "4");
  EXPECT_EQ(read_member(plugin, "TERM\\ItemType", 1), "8");
  EXPECT_EQ(read_member(plugin, "TERM\\ItemId", 0), "1");
  EXPECT_EQ(read_member(plugin, "TERM\\ItemId", 1), "2");

  EXPECT_EQ(fo4::record_field_count(plugin, record_key(), "TERM\\BodyText"), 2u);
  EXPECT_EQ(fo4::record_field_count(plugin, record_key(), "TERM\\ItemText"), 2u);
  EXPECT_EQ(fo4::record_field_count(plugin, record_key(), "TERM\\ItemId"), 2u);
}

TEST(TermMenuRows, WritesBodyTextAndMenuItemMembers) {
  auto plugin = load();
  EXPECT_EQ(fo4::set_record_string(plugin, record_key(), scalar("TERM\\BodyText"), 1, "Edited body"),
            fo4::ScalarMutationStatus::updated);
  EXPECT_EQ(fo4::set_record_string(plugin, record_key(), scalar("TERM\\ItemText"), 1, "Edited item"),
            fo4::ScalarMutationStatus::updated);
  EXPECT_EQ(fo4::set_record_string(plugin, record_key(), scalar("TERM\\ItemResponseText"), 0, "Edited response"),
            fo4::ScalarMutationStatus::updated);
  EXPECT_EQ(fo4::set_record_scalar(plugin, record_key(), scalar("TERM\\ItemType"), 0, "5"),
            fo4::ScalarMutationStatus::updated);
  EXPECT_EQ(fo4::set_record_scalar(plugin, record_key(), scalar("TERM\\ItemId"), 1, "9"),
            fo4::ScalarMutationStatus::updated);
  EXPECT_EQ(fo4::set_record_formlink_field(plugin, record_key(), link("TERM\\ItemSubmenu"), 0, other_term_key()),
            fo4::FormLinkFieldMutationStatus::updated);

  const auto after = reparse(plugin);
  EXPECT_EQ(payloads(after, "BTXT"), (std::vector<Bytes>{zstring("Body one"), zstring("Edited body")}));
  EXPECT_EQ(payloads(after, "ITXT"), (std::vector<Bytes>{zstring("Item one"), zstring("Edited item")}));
  EXPECT_EQ(payloads(after, "RNAM"), (std::vector<Bytes>{zstring("Edited response")}));
  EXPECT_EQ(payloads(after, "ANAM"), (std::vector<Bytes>{anam_bytes(5), anam_bytes(8)}));
  EXPECT_EQ(payloads(after, "ITID"), (std::vector<Bytes>{itid_bytes(1), itid_bytes(9)}));
  EXPECT_EQ(payloads(after, "TNAM"), (std::vector<Bytes>{u32_bytes(0x930)}));
  EXPECT_EQ(read_member(after, "TERM\\ItemId", 1), "9");
}

TEST(TermMenuRows, RefusesItemValuesOutsideTheRowRange) {
  auto plugin = load();
  const auto before = fo4::write_plugin(plugin);
  EXPECT_THROW(fo4::set_record_scalar(plugin, record_key(), scalar("TERM\\ItemType"), 0, "256"),
               fo4::schema::ScalarError);
  EXPECT_THROW(fo4::set_record_scalar(plugin, record_key(), scalar("TERM\\ItemId"), 0, "65536"),
               fo4::schema::ScalarError);
  EXPECT_EQ(fo4::write_plugin(plugin), before);
}

TEST(TermMenuRows, ReadsHolotapeRunsCountAndLink) {
  const auto plugin = load();
  EXPECT_EQ(read_member(plugin, "TERM\\HolotapeTotal", 0), "2");
  EXPECT_EQ(read_member(plugin, "TERM\\HolotapeCount", 0), "1");
  EXPECT_EQ(read_member(plugin, "TERM\\HolotapeCount", 1), "2");
  EXPECT_EQ(cnto_item(plugin, 0), 0x910u);
  EXPECT_EQ(cnto_item(plugin, 1), 0x911u);
  EXPECT_EQ(fo4::record_field_count(plugin, record_key(), "TERM\\HolotapeCount"), 2u);
}

TEST(TermMenuRows, WritesAHolotapeLinkAndCountWithoutTouchingTheTotal) {
  auto plugin = load();
  EXPECT_EQ(fo4::set_record_formlink_field(plugin, record_key(), link("TERM\\Holotape"), 1, note_key()),
            fo4::FormLinkFieldMutationStatus::updated);
  EXPECT_EQ(fo4::set_record_scalar(plugin, record_key(), scalar("TERM\\HolotapeCount"), 1, "7"),
            fo4::ScalarMutationStatus::updated);

  const auto after = reparse(plugin);
  EXPECT_EQ(cnto_item(after, 0), 0x910u);
  EXPECT_EQ(cnto_item(after, 1), 0x910u);
  EXPECT_EQ(read_member(after, "TERM\\HolotapeCount", 1), "7");
  EXPECT_EQ(stored_count(after), 2u);
  EXPECT_EQ(payloads(after, "CNTO"), (std::vector<Bytes>{cnto_bytes(0x910, 1), cnto_bytes(0x910, 7)}));
}

TEST(TermMenuRows, InsertsAndRemovesHolotapeRunsAndSyncsTheCount) {
  auto inserted = load();
  EXPECT_EQ(fo4::insert_record_subrecord_group(inserted, record_key(), holotapes_group(), 1),
            fo4::SubrecordGroupMutationStatus::updated);
  const auto inserted_after = reparse(inserted);
  EXPECT_EQ(stored_count(inserted_after), 3u);
  EXPECT_EQ(read_member(inserted_after, "TERM\\HolotapeCount", 1), "1");
  EXPECT_EQ(cnto_item(inserted_after, 1), 0u);
  EXPECT_EQ(cnto_item(inserted_after, 2), 0x911u);

  auto removed = load();
  EXPECT_EQ(fo4::remove_record_subrecord_group(removed, record_key(), holotapes_group(), 0),
            fo4::SubrecordGroupMutationStatus::updated);
  const auto removed_after = reparse(removed);
  EXPECT_EQ(stored_count(removed_after), 1u);
  EXPECT_EQ(signatures(removed_after),
            (std::vector<std::string>{"EDID", "COCT", "CNTO", "MNAM", "WBDT", "XMRK", "BSIZ", "BTXT",
                                      "BTXT", "ISIZ", "ITXT", "RNAM", "ANAM", "ITID", "UNAM", "VNAM",
                                      "TNAM", "ITXT", "ANAM", "ITID"}));
}

TEST(TermMenuRows, InsertsHolotapesBeforeTheFirstSuccessorOfATerminalWithoutThem) {
  auto terminal = term_base();
  terminal.erase(terminal.begin() + 1, terminal.begin() + 4);
  auto plugin = load(terminal);
  EXPECT_EQ(fo4::insert_record_subrecord_group(plugin, record_key(), holotapes_group(), 0),
            fo4::SubrecordGroupMutationStatus::updated);
  const auto after = reparse(plugin);
  EXPECT_EQ(signatures(after).front(), "EDID");
  EXPECT_EQ(signatures(after)[1], "COCT");
  EXPECT_EQ(signatures(after)[2], "CNTO");
  EXPECT_EQ(stored_count(after), 1u);
  EXPECT_EQ(read_member(after, "TERM\\HolotapeCount", 0), "1");
  EXPECT_EQ(cnto_item(after, 0), 0u);
}

TEST(TermMenuRows, HolotapeTotalIsRefusedAsAPlainScalarWithoutMutation) {
  auto plugin = load();
  const auto before = fo4::write_plugin(plugin);
  EXPECT_THROW(fo4::set_record_scalar(plugin, record_key(), scalar("TERM\\HolotapeTotal"), "5"),
               fo4::schema::ScalarError);
  EXPECT_EQ(fo4::write_plugin(plugin), before);
}

TEST(TermMenuRows, AuthoringRefusesTheHolotapeMembersAndTheCount) {
  auto plugin = load();
  const auto before = fo4::write_plugin(plugin);
  EXPECT_EQ(fo4::add_record_subrecord(plugin, record_key(), k_cnto), fo4::SubrecordAddStatus::not_authorable);
  EXPECT_EQ(fo4::remove_record_subrecord(plugin, record_key(), k_cnto, 0),
            fo4::SubrecordRemoveStatus::not_authorable);
  EXPECT_EQ(fo4::remove_record_subrecord(plugin, record_key(), k_coct, 0),
            fo4::SubrecordRemoveStatus::not_authorable);
  EXPECT_EQ(fo4::write_plugin(plugin), before);
}
