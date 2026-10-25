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
#include <cstring>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace {

using Bytes = std::vector<std::byte>;

constexpr auto k_term = fo4::Signature::from_string("TERM");
constexpr auto k_coct = fo4::Signature::from_string("COCT");
constexpr auto k_cnto = fo4::Signature::from_string("CNTO");
constexpr auto k_snam = fo4::Signature::from_string("SNAM");
constexpr auto k_kywd = fo4::Signature::from_string("KYWD");

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

// source:wbDefinitionsFO4.pas @93cc0bc wbSNAMMarkerParams: keyword offset 16, entry types offset 20, three bytes from offset 21 in a 24-byte element.
Bytes marker_element(float x, float y, float z, float rotation, std::uint32_t keyword,
                     std::uint8_t entry_types) {
  fo4::test::ByteWriter out;
  out.f32(x);
  out.f32(y);
  out.f32(z);
  out.f32(rotation);
  out.u32(keyword);
  out.u8(entry_types);
  out.u8(0xFF);
  out.u8(0xFF);
  out.u8(0xFF);
  return out.bytes;
}

// source:wbDefinitionsFO4.pas @93cc0bc wbSNAMMarkerParams: below form version 125 the element ends at the keyword offset 16.
Bytes legacy_marker_element(float x, float y, float z, float rotation, std::uint32_t keyword) {
  fo4::test::ByteWriter out;
  out.f32(x);
  out.f32(y);
  out.f32(z);
  out.f32(rotation);
  out.u32(keyword);
  return out.bytes;
}

std::vector<std::byte> make_marker_plugin(const Bytes& marker_params,
                                          std::uint16_t form_version = 131u) {
  fo4::test::ByteWriter out;
  const auto tes4 = fo4::test::begin_record(out, "TES4", 0, 0);
  fo4::test::ByteWriter hedr;
  hedr.f32(1.0f);
  hedr.u32(1);
  hedr.u32(0x911);
  fo4::test::write_subrecord(out, "HEDR", hedr.bytes);
  fo4::test::end_record(out, tes4);

  const auto group = fo4::test::begin_group(out, "TERM", 0);
  const auto record = fo4::test::begin_record(out, "TERM", 0, 0x00000800, form_version);
  fo4::test::write_subrecord(out, "SNAM", marker_params);
  fo4::test::end_record(out, record);
  fo4::test::end_group(out, group);

  const auto keyword_group = fo4::test::begin_group(out, "KYWD", 0);
  const auto keyword = fo4::test::begin_record(out, "KYWD", 0, 0x00000900, form_version);
  fo4::test::end_record(out, keyword);
  fo4::test::end_group(out, keyword_group);
  return out.bytes;
}

Bytes marker_params_two() {
  auto params = marker_element(1.5f, 0.5f, -0.25f, 45.0f, 0xFFFFFFFFu, 0xFFu);
  const auto second = marker_element(-2.5f, 3.0f, 0.0f, 0.0f, 0u, 0xE0u);
  params.insert(params.end(), second.begin(), second.end());
  return params;
}

Bytes legacy_marker_params_two() {
  auto params = legacy_marker_element(1.5f, 0.5f, -0.25f, 45.0f, 0u);
  const auto second = legacy_marker_element(-2.5f, 3.0f, 0.0f, 0.0f, 0x911u);
  params.insert(params.end(), second.begin(), second.end());
  return params;
}

fo4::Plugin load_marker(const Bytes& marker_params, std::uint16_t form_version = 131u) {
  return fo4::read_plugin(make_marker_plugin(marker_params, form_version), "Patch.esp");
}

fo4::FormKey keyword_key() { return fo4::FormKey::parse("000900:Patch.esp"); }

std::size_t changed_bytes(const Bytes& before, const Bytes& after) {
  std::size_t changed = 0;
  for (std::size_t at = 0; at < before.size() && at < after.size(); ++at) {
    if (before[at] != after[at]) ++changed;
  }
  return changed;
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

std::string marker_member(const fo4::Plugin& plugin, std::string_view path, std::size_t index) {
  const auto field = scalar(path);
  const auto& record = record_of(plugin);
  const auto resolved = fo4::schema::resolve_field_slots(
      record, fo4::schema::scalar_field_shape(field.scalar), field.scalar.address);
  if (index >= resolved.slots.size() || !resolved.slots[index].valid) return "absent";
  const auto slot = resolved.slots[index];
  return fo4::schema::decode_scalar(
      fo4::schema::scalar_slot_bytes(record.subrecords[slot.subrecord].data, slot, field.scalar),
      field.scalar);
}

std::optional<std::uint32_t> marker_link(const fo4::Plugin& plugin, std::string_view path,
                                         std::size_t index) {
  const auto field = link(path);
  fo4::schema::FieldShape shape;
  shape.subrecord = field.subrecord;
  shape.offset = field.offset;
  shape.width = sizeof(std::uint32_t);
  shape.min_form_version = field.min_form_version;
  shape.max_form_version = field.max_form_version;
  const auto& record = record_of(plugin);
  const auto resolved = fo4::schema::resolve_field_slots(record, shape, field.address);
  if (index >= resolved.slots.size() || !resolved.slots[index].valid) return std::nullopt;
  const auto slot = resolved.slots[index];
  const auto& data = record.subrecords[slot.subrecord].data;
  std::uint32_t value = 0;
  std::memcpy(&value, data.data() + slot.offset, sizeof(value));
  return value;
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

TEST(TermMarkerParams, RowsDeclareTheTwentyFourByteMarkerElement) {
  struct Row {
    std::string_view path;
    std::size_t offset;
  };
  constexpr Row k_modern[] = {{"TERM\\MarkerOffsetX", 0},
                              {"TERM\\MarkerOffsetY", 4},
                              {"TERM\\MarkerOffsetZ", 8},
                              {"TERM\\MarkerRotationZ", 12}};
  for (const auto& row : k_modern) {
    SCOPED_TRACE(row.path);
    const auto field = scalar(row.path);
    EXPECT_EQ(field.record, k_term);
    EXPECT_EQ(field.scalar.subrecord, k_snam);
    EXPECT_EQ(field.scalar.offset, row.offset);
    EXPECT_TRUE(field.scalar.embedded);
    EXPECT_EQ(field.scalar.address.stride, 24u);
    EXPECT_FALSE(field.scalar.address.repeated);
    EXPECT_EQ(field.scalar.storage, fo4::schema::ScalarStorage::f32);
    EXPECT_EQ(field.scalar.presentation, fo4::schema::ScalarPresentation::identity);
    EXPECT_EQ(field.scalar.min_form_version, 125u);
    EXPECT_EQ(field.scalar.max_form_version, 0u);
  }

  constexpr Row k_legacy[] = {{"TERM\\MarkerOffsetXBelow125", 0},
                              {"TERM\\MarkerOffsetYBelow125", 4},
                              {"TERM\\MarkerOffsetZBelow125", 8},
                              {"TERM\\MarkerRotationZBelow125", 12}};
  for (const auto& row : k_legacy) {
    SCOPED_TRACE(row.path);
    const auto field = scalar(row.path);
    EXPECT_EQ(field.scalar.subrecord, k_snam);
    EXPECT_EQ(field.scalar.offset, row.offset);
    EXPECT_EQ(field.scalar.address.stride, 20u);
    EXPECT_EQ(field.scalar.storage, fo4::schema::ScalarStorage::f32);
    EXPECT_EQ(field.scalar.min_form_version, 0u);
    EXPECT_EQ(field.scalar.max_form_version, 125u);
  }

  const auto entry_types = scalar("TERM\\MarkerEntryTypes");
  EXPECT_EQ(entry_types.scalar.subrecord, k_snam);
  EXPECT_EQ(entry_types.scalar.offset, 20u);
  EXPECT_EQ(entry_types.scalar.address.stride, 24u);
  EXPECT_EQ(entry_types.scalar.storage, fo4::schema::ScalarStorage::u8);
  EXPECT_EQ(entry_types.scalar.presentation, fo4::schema::ScalarPresentation::flags);
  EXPECT_EQ(entry_types.scalar.min_form_version, 125u);
  const auto rear = fo4::schema::scalar_value_name(entry_types.scalar, 1 << 1);
  ASSERT_TRUE(rear.has_value());
  EXPECT_EQ(*rear, "Rear");
  const auto unused_seven = fo4::schema::scalar_value_name(entry_types.scalar, 1 << 7);
  ASSERT_TRUE(unused_seven.has_value());
  EXPECT_EQ(*unused_seven, "Unused 7");
  EXPECT_FALSE(fo4::schema::scalar_value_name(entry_types.scalar, 1 << 8).has_value());

  const auto keyword = link("TERM\\MarkerKeyword");
  EXPECT_EQ(keyword.record, k_term);
  EXPECT_EQ(keyword.subrecord, k_snam);
  EXPECT_EQ(keyword.offset, 16u);
  EXPECT_EQ(keyword.address.stride, 24u);
  EXPECT_FALSE(keyword.address.repeated);
  EXPECT_TRUE(keyword.nullable);
  EXPECT_EQ(keyword.min_form_version, 125u);
  EXPECT_EQ(keyword.max_form_version, 0u);
  EXPECT_TRUE(fo4::schema::formlink_field_target_allowed(keyword, k_kywd));
  EXPECT_FALSE(fo4::schema::formlink_field_target_allowed(keyword, k_snam));

  const auto legacy_keyword = link("TERM\\MarkerKeywordBelow125");
  EXPECT_EQ(legacy_keyword.subrecord, k_snam);
  EXPECT_EQ(legacy_keyword.offset, 16u);
  EXPECT_EQ(legacy_keyword.address.stride, 20u);
  EXPECT_TRUE(legacy_keyword.nullable);
  EXPECT_EQ(legacy_keyword.min_form_version, 0u);
  EXPECT_EQ(legacy_keyword.max_form_version, 125u);
  EXPECT_TRUE(fo4::schema::formlink_field_target_allowed(legacy_keyword, k_kywd));
}

TEST(TermMarkerParams, ElementsReadAndWriteAtTheirOwnOffsets) {
  auto plugin = load_marker(marker_params_two());
  EXPECT_EQ(marker_member(plugin, "TERM\\MarkerOffsetX", 0), "1.5");
  EXPECT_EQ(marker_member(plugin, "TERM\\MarkerOffsetX", 1), "-2.5");
  EXPECT_EQ(marker_member(plugin, "TERM\\MarkerOffsetY", 1), "3");
  EXPECT_EQ(marker_member(plugin, "TERM\\MarkerRotationZ", 0), "45");
  EXPECT_EQ(marker_member(plugin, "TERM\\MarkerEntryTypes", 0), "255");
  EXPECT_EQ(marker_member(plugin, "TERM\\MarkerEntryTypes", 1), "224");
  EXPECT_EQ(marker_member(plugin, "TERM\\MarkerOffsetX", 2), "absent");
  EXPECT_EQ(marker_member(plugin, "TERM\\MarkerOffsetXBelow125", 0), "absent");
  EXPECT_EQ(marker_link(plugin, "TERM\\MarkerKeyword", 0), 0xFFFFFFFFu);
  EXPECT_EQ(marker_link(plugin, "TERM\\MarkerKeyword", 1), 0u);
  EXPECT_FALSE(marker_link(plugin, "TERM\\MarkerKeyword", 2).has_value());

  EXPECT_EQ(fo4::set_record_scalar(plugin, record_key(), scalar("TERM\\MarkerOffsetX"), 1, "2.5"),
            fo4::ScalarMutationStatus::updated);
  EXPECT_EQ(fo4::set_record_scalar(plugin, record_key(), scalar("TERM\\MarkerRotationZ"), 0, "-90"),
            fo4::ScalarMutationStatus::updated);
  EXPECT_EQ(fo4::set_record_scalar(plugin, record_key(), scalar("TERM\\MarkerEntryTypes"), 1, "1"),
            fo4::ScalarMutationStatus::updated);
  EXPECT_EQ(fo4::set_record_formlink_field(plugin, record_key(), link("TERM\\MarkerKeyword"), 1,
                                           keyword_key()),
            fo4::FormLinkFieldMutationStatus::updated);

  const auto after = reparse(plugin);
  const auto params = payloads(after, "SNAM").at(0);
  ASSERT_EQ(params.size(), 48u);
  EXPECT_EQ(read_u32(params, 24), 0x40200000u);
  EXPECT_EQ(read_u32(params, 12), 0xC2B40000u);
  EXPECT_EQ(read_u32(params, 40), 0x00000900u);
  EXPECT_EQ(std::to_integer<std::uint8_t>(params[44]), 1u);
  EXPECT_EQ(read_u32(params, 0), 0x3FC00000u);
  EXPECT_EQ(read_u32(params, 28), 0x40400000u);
  EXPECT_EQ(read_u32(params, 16), 0xFFFFFFFFu);
  EXPECT_EQ(std::to_integer<std::uint8_t>(params[20]), 0xFFu);
  EXPECT_EQ(std::to_integer<std::uint8_t>(params[45]), 0xFFu);
  EXPECT_EQ(std::to_integer<std::uint8_t>(params[46]), 0xFFu);
  EXPECT_EQ(std::to_integer<std::uint8_t>(params[47]), 0xFFu);
  EXPECT_EQ(marker_member(after, "TERM\\MarkerOffsetX", 1), "2.5");
  EXPECT_EQ(marker_member(after, "TERM\\MarkerRotationZ", 0), "-90");
  EXPECT_EQ(marker_member(after, "TERM\\MarkerEntryTypes", 1), "1");
  EXPECT_EQ(marker_link(after, "TERM\\MarkerKeyword", 1), 0x00000900u);
}

TEST(TermMarkerParams, EachScalarWriteTouchesOnlyItsOwnElementBytes) {
  struct Write {
    std::string_view path;
    std::size_t index;
    const char* value;
    std::size_t at;
    std::size_t changed;
    std::uint32_t bits;
  };
  constexpr Write k_writes[] = {{"TERM\\MarkerOffsetX", 1, "2.5", 24, 1, 0x40200000u},
                                {"TERM\\MarkerRotationZ", 0, "-90", 12, 2, 0xC2B40000u},
                                {"TERM\\MarkerOffsetY", 1, "0", 28, 1, 0x00000000u},
                                {"TERM\\MarkerEntryTypes", 1, "1", 44, 1, 0x00000001u}};
  for (const auto& write : k_writes) {
    SCOPED_TRACE(write.path);
    auto plugin = load_marker(marker_params_two());
    const auto before = payloads(plugin, "SNAM").at(0);
    EXPECT_EQ(fo4::set_record_scalar(plugin, record_key(), scalar(write.path), write.index, write.value),
              fo4::ScalarMutationStatus::updated);
    const auto params = payloads(reparse(plugin), "SNAM").at(0);
    ASSERT_EQ(params.size(), before.size());
    EXPECT_EQ(changed_bytes(before, params), write.changed);
    if (write.at == 44) {
      EXPECT_EQ(read_u32(params, 44) & 0xFFu, write.bits);
    } else {
      EXPECT_EQ(read_u32(params, write.at), write.bits);
    }
  }
}

TEST(TermMarkerParams, KeywordWritesFourBytesAndAcceptsNoTarget) {
  auto plugin = load_marker(marker_params_two());
  const auto before = payloads(plugin, "SNAM").at(0);
  EXPECT_EQ(marker_link(plugin, "TERM\\MarkerKeyword", 0), 0xFFFFFFFFu);
  EXPECT_EQ(fo4::set_record_formlink_field(plugin, record_key(), link("TERM\\MarkerKeyword"), 0,
                                           keyword_key()),
            fo4::FormLinkFieldMutationStatus::updated);
  const auto rewritten = payloads(reparse(plugin), "SNAM").at(0);
  EXPECT_EQ(changed_bytes(before, rewritten), 4u);
  EXPECT_EQ(read_u32(rewritten, 16), 0x00000900u);
  EXPECT_EQ(read_u32(rewritten, 12), 0x42340000u);
  EXPECT_EQ(std::to_integer<std::uint8_t>(rewritten[20]), 0xFFu);

  auto cleared = reparse(plugin);
  EXPECT_EQ(fo4::set_record_formlink_field(cleared, record_key(), link("TERM\\MarkerKeyword"), 0,
                                           fo4::FormKey{}),
            fo4::FormLinkFieldMutationStatus::updated);
  const auto emptied = reparse(cleared);
  EXPECT_EQ(read_u32(payloads(emptied, "SNAM").at(0), 16), 0u);
  EXPECT_EQ(std::to_integer<std::uint8_t>(payloads(emptied, "SNAM").at(0)[20]), 0xFFu);

  EXPECT_THROW(fo4::set_record_formlink_field(cleared, record_key(), link("TERM\\MarkerKeyword"), 0,
                                              other_term_key()),
               fo4::schema::FormLinkFieldError);
  EXPECT_EQ(fo4::set_record_formlink_field(cleared, record_key(), link("TERM\\MarkerKeyword"), 2,
                                           keyword_key()),
            fo4::FormLinkFieldMutationStatus::subrecord_missing);
}

TEST(TermMarkerParams, LegacyTwentyByteElementsReadAndWriteBelowFormVersion125) {
  auto plugin = load_marker(legacy_marker_params_two(), 124u);
  EXPECT_EQ(marker_member(plugin, "TERM\\MarkerOffsetXBelow125", 0), "1.5");
  EXPECT_EQ(marker_member(plugin, "TERM\\MarkerOffsetXBelow125", 1), "-2.5");
  EXPECT_EQ(marker_member(plugin, "TERM\\MarkerRotationZBelow125", 0), "45");
  EXPECT_EQ(marker_member(plugin, "TERM\\MarkerOffsetXBelow125", 2), "absent");
  EXPECT_EQ(marker_member(plugin, "TERM\\MarkerOffsetX", 0), "absent");
  EXPECT_EQ(marker_member(plugin, "TERM\\MarkerEntryTypes", 0), "absent");
  EXPECT_EQ(marker_link(plugin, "TERM\\MarkerKeywordBelow125", 1), 0x00000911u);
  EXPECT_FALSE(marker_link(plugin, "TERM\\MarkerKeyword", 0).has_value());
  EXPECT_EQ(fo4::set_record_scalar(plugin, record_key(), scalar("TERM\\MarkerEntryTypes"), 0, "1"),
            fo4::ScalarMutationStatus::subrecord_missing);

  EXPECT_EQ(fo4::set_record_scalar(plugin, record_key(), scalar("TERM\\MarkerOffsetXBelow125"), 1, "2.5"),
            fo4::ScalarMutationStatus::updated);
  EXPECT_EQ(fo4::set_record_scalar(plugin, record_key(), scalar("TERM\\MarkerRotationZBelow125"), 0, "-90"),
            fo4::ScalarMutationStatus::updated);
  EXPECT_EQ(fo4::set_record_formlink_field(plugin, record_key(),
                                           link("TERM\\MarkerKeywordBelow125"), 1, keyword_key()),
            fo4::FormLinkFieldMutationStatus::updated);

  const auto after = reparse(plugin);
  const auto params = payloads(after, "SNAM").at(0);
  ASSERT_EQ(params.size(), 40u);
  EXPECT_EQ(read_u32(params, 20), 0x40200000u);
  EXPECT_EQ(read_u32(params, 12), 0xC2B40000u);
  EXPECT_EQ(read_u32(params, 36), 0x00000900u);
  EXPECT_EQ(read_u32(params, 16), 0x00000000u);
  EXPECT_EQ(read_u32(params, 0), 0x3FC00000u);
  EXPECT_EQ(marker_member(after, "TERM\\MarkerOffsetXBelow125", 1), "2.5");
  EXPECT_EQ(marker_link(after, "TERM\\MarkerKeywordBelow125", 1), 0x00000900u);
}

TEST(TermMarkerParams, RaggedOrMismatchedPayloadsHideEveryMarkerRow) {
  auto twenty_at_modern = load_marker(Bytes(20, std::byte{0}), 131u);
  EXPECT_EQ(marker_member(twenty_at_modern, "TERM\\MarkerOffsetX", 0), "absent");
  EXPECT_EQ(marker_member(twenty_at_modern, "TERM\\MarkerKeyword", 0), "absent");
  EXPECT_EQ(fo4::set_record_scalar(twenty_at_modern, record_key(), scalar("TERM\\MarkerOffsetX"), 0, "1"),
            fo4::ScalarMutationStatus::subrecord_missing);
  EXPECT_EQ(fo4::set_record_formlink_field(twenty_at_modern, record_key(),
                                           link("TERM\\MarkerKeyword"), 0, keyword_key()),
            fo4::FormLinkFieldMutationStatus::subrecord_missing);

  auto twenty_four_at_legacy = load_marker(Bytes(24, std::byte{0}), 124u);
  EXPECT_EQ(marker_member(twenty_four_at_legacy, "TERM\\MarkerOffsetX", 0), "absent");
  EXPECT_EQ(marker_member(twenty_four_at_legacy, "TERM\\MarkerOffsetXBelow125", 0), "absent");
  EXPECT_FALSE(marker_link(twenty_four_at_legacy, "TERM\\MarkerKeywordBelow125", 0).has_value());

  auto ragged = load_marker(Bytes(32, std::byte{0}), 131u);
  EXPECT_EQ(marker_member(ragged, "TERM\\MarkerOffsetX", 0), "absent");
  EXPECT_EQ(fo4::set_record_scalar(ragged, record_key(), scalar("TERM\\MarkerOffsetX"), 0, "1"),
            fo4::ScalarMutationStatus::subrecord_missing);
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
