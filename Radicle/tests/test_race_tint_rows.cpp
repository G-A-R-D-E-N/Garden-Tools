#include "fo4/plugin/mutation.hpp"
#include "fo4/plugin/reader.hpp"
#include "fo4/plugin/writer.hpp"
#include "fo4/schema/localized_fields.hpp"
#include "fo4/schema/scalar.hpp"
#include "plugin_bytes.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

using Bytes = std::vector<std::byte>;
using fo4::test::ByteWriter;

constexpr std::string_view k_group_path = "RACE\\TintLayerOption";
constexpr std::string_view k_race_key = "000800:Patch.esp";

const fo4::Signature k_race = fo4::Signature::from_string("RACE");
const fo4::Signature k_teti = fo4::Signature::from_string("TETI");
const fo4::Signature k_ttgp = fo4::Signature::from_string("TTGP");
const fo4::Signature k_ttet = fo4::Signature::from_string("TTET");
const fo4::Signature k_ttec = fo4::Signature::from_string("TTEC");
const fo4::Signature k_ttge = fo4::Signature::from_string("TTGE");
const fo4::Signature k_clfm = fo4::Signature::from_string("CLFM");
const fo4::Signature k_spel = fo4::Signature::from_string("SPEL");

Bytes zstr(std::string_view text) {
  ByteWriter out;
  out.raw(text);
  out.u8(0);
  return out.bytes;
}

Bytes u16_bytes(std::uint16_t value) {
  ByteWriter out;
  out.u16(value);
  return out.bytes;
}

Bytes u32_bytes(std::uint32_t value) {
  ByteWriter out;
  out.u32(value);
  return out.bytes;
}

Bytes f32_bytes(float value) {
  ByteWriter out;
  out.f32(value);
  return out.bytes;
}

Bytes concat(std::initializer_list<Bytes> parts) {
  Bytes out;
  for (const auto& part : parts) out.insert(out.end(), part.begin(), part.end());
  return out;
}

// source:wbDefinitionsFO4.pas @93cc0bc TETI offset map: Slot u16 at 0, Index u16 at 2 in a 4-byte struct.
Bytes teti(std::uint16_t slot, std::uint16_t index) {
  return concat({u16_bytes(slot), u16_bytes(index)});
}

// source:wbDefinitionsFO4.pas @93cc0bc TTEC offset map: Color at 0, Alpha f32 at 4, Template Index u16 at 8, Blend Operation u32 at 10 in a 14-byte element.
Bytes ttec_element(std::uint32_t color, float alpha, std::uint16_t template_index,
                   std::uint32_t blend) {
  ByteWriter out;
  out.u32(color);
  out.f32(alpha);
  out.u16(template_index);
  out.u32(blend);
  return out.bytes;
}

using Subs = std::vector<std::pair<std::string_view, Bytes>>;

std::vector<std::byte> make_plugin(const Subs& subs, std::uint32_t tes4_flags = 0) {
  ByteWriter out;
  const auto tes4 = fo4::test::begin_record(out, "TES4", tes4_flags, 0);
  ByteWriter hedr;
  hedr.f32(1.0f);
  hedr.u32(2);
  hedr.u32(0x0C00);
  fo4::test::write_subrecord(out, "HEDR", hedr.bytes);
  fo4::test::end_record(out, tes4);

  const auto colors = fo4::test::begin_group(out, "CLFM", 0);
  for (const auto local : {0x0B01u, 0x0B02u}) {
    const auto color = fo4::test::begin_record(out, "CLFM", 0, local);
    fo4::test::write_subrecord(out, "EDID", zstr("RadicleColor"));
    fo4::test::end_record(out, color);
  }
  fo4::test::end_group(out, colors);

  const auto group = fo4::test::begin_group(out, "RACE", 0);
  const auto record = fo4::test::begin_record(out, "RACE", 0, 0x0800);
  fo4::test::write_subrecord(out, "EDID", zstr("RadicleRace"));
  for (const auto& [signature, payload] : subs) fo4::test::write_subrecord(out, signature, payload);
  fo4::test::end_record(out, record);
  fo4::test::end_group(out, group);
  return out.bytes;
}

fo4::Plugin load(const Subs& subs, std::uint32_t tes4_flags = 0) {
  return fo4::read_plugin(make_plugin(subs, tes4_flags), "Patch.esp");
}

const fo4::schema::SubrecordGroupDescriptor& tint_group() {
  static const auto group = fo4::schema::subrecord_group_descriptor(k_race, k_group_path);
  EXPECT_TRUE(group.has_value());
  return *group;
}

fo4::schema::ScalarFieldDescriptor row(std::string_view path) {
  const auto descriptor = fo4::schema::scalar_field_descriptor(k_race, path);
  EXPECT_TRUE(descriptor.has_value()) << path;
  return descriptor.value_or(fo4::schema::ScalarFieldDescriptor{});
}

fo4::schema::FormLinkFieldDescriptor link(std::string_view path) {
  const auto descriptor = fo4::schema::formlink_field_descriptor(k_race, path);
  EXPECT_TRUE(descriptor.has_value()) << path;
  return descriptor.value_or(fo4::schema::FormLinkFieldDescriptor{});
}

fo4::FormKey key() { return fo4::FormKey::parse(k_race_key); }

const fo4::Record& race_record(const fo4::Plugin& plugin) {
  const auto* record = fo4::find_record(plugin, key());
  EXPECT_NE(record, nullptr);
  static const fo4::Record empty{};
  return record == nullptr ? empty : *record;
}

std::vector<Bytes> payloads(const fo4::Plugin& plugin, fo4::Signature signature) {
  std::vector<Bytes> out;
  for (const auto& sub : race_record(plugin).subrecords) {
    if (sub.signature == signature) out.push_back(sub.data);
  }
  return out;
}

std::vector<std::string> order(const fo4::Plugin& plugin) {
  std::vector<std::string> out;
  for (const auto& sub : race_record(plugin).subrecords) out.push_back(sub.signature.to_string());
  return out;
}

std::string read_index(const fo4::Plugin& plugin, std::string_view path, std::size_t index) {
  const auto field = row(path);
  const auto resolved = fo4::schema::resolve_field_slots(
      race_record(plugin), fo4::schema::scalar_field_shape(field.scalar), field.scalar.address);
  if (index >= resolved.slots.size() || !resolved.slots[index].valid) return "absent";
  const auto slot = resolved.slots[index];
  return fo4::schema::decode_scalar(
      fo4::schema::scalar_slot_bytes(race_record(plugin).subrecords[slot.subrecord].data, slot,
                                     field.scalar),
      field.scalar);
}

std::int64_t value_of(const fo4::schema::ScalarDescriptor& scalar, std::string_view name) {
  const auto found = std::find_if(scalar.names.begin(), scalar.names.end(),
                                  [name](const auto& entry) { return entry.name == name; });
  EXPECT_NE(found, scalar.names.end()) << name;
  return found == scalar.names.end() ? 0 : found->value;
}

Subs two_groups() {
  return {
      {"TTGP", zstr("Body")},
      {"TETI", teti(7, 3)},
      {"TTGP", zstr("Lip Color")},
      {"TTEF", u16_bytes(5)},
      {"TTET", zstr("Lip_n.dds")},
      {"TTEB", u32_bytes(1)},
      {"TETI", teti(12, 4)},
      {"TTGP", zstr("Skin Tone")},
      {"TTEF", u16_bytes(4)},
      {"CTDA", Bytes(32, std::byte{0})},
      {"TTET", zstr("Skin_n.dds")},
      {"TTEC", concat({ttec_element(0x00000B01u, 0.5f, 2, 3)})},
      {"TTED", f32_bytes(0.75f)},
      {"TTGE", u32_bytes(2)},
      {"TTGP", zstr("Face")},
      {"TETI", teti(9, 5)},
      {"TTGP", zstr("Eyeliner")},
      {"TTET", zstr("Liner_n.dds")},
      {"TTGE", u32_bytes(1)},
  };
}

}  // namespace

// Constraint: a group-name TTGP opens a span and TTGE closes it, so options cannot fold across groups.
TEST(RaceTintRows, GroupSplitsOptionsAtGroupNamesAndCategoryIndexes) {
  const auto& group = tint_group();
  EXPECT_EQ(group.record, k_race);
  EXPECT_EQ(group.count_subrecord, fo4::Signature{});
  ASSERT_EQ(group.members.size(), 8u);
  EXPECT_EQ(group.members[0].subrecord, k_teti);
  EXPECT_TRUE(group.members[0].required);
  EXPECT_FALSE(group.members[0].repeatable);
  EXPECT_EQ(group.members[0].payload_size, 4u);
  EXPECT_EQ(group.members[1].subrecord, k_ttgp);
  EXPECT_TRUE(group.members[1].required);
  EXPECT_TRUE(group.members[1].localized);
  EXPECT_TRUE(group.members[1].skippable);
  EXPECT_EQ(group.members[1].payload_size, 0u);
  EXPECT_EQ(group.members[2].subrecord, fo4::Signature::from_string("TTEF"));
  EXPECT_EQ(group.members[2].payload_size, 2u);
  EXPECT_EQ(group.members[3].subrecord, fo4::Signature::from_string("CTDA"));
  EXPECT_TRUE(group.members[3].repeatable);
  EXPECT_EQ(group.members[3].payload_size, 32u);
  EXPECT_EQ(group.members[4].subrecord, k_ttet);
  EXPECT_TRUE(group.members[4].repeatable);
  EXPECT_EQ(group.members[5].subrecord, fo4::Signature::from_string("TTEB"));
  EXPECT_EQ(group.members[5].payload_size, 4u);
  EXPECT_EQ(group.members[6].subrecord, k_ttec);
  EXPECT_TRUE(group.members[6].repeatable);
  EXPECT_EQ(group.members[7].subrecord, fo4::Signature::from_string("TTED"));
  EXPECT_EQ(group.members[7].payload_size, 4u);
  ASSERT_EQ(group.successors.size(), 1u);
  EXPECT_EQ(group.successors[0], k_ttge);
  ASSERT_TRUE(fo4::schema::subrecord_group_for_member(k_race, k_ttgp).has_value());
  EXPECT_EQ(fo4::schema::subrecord_group_for_member(k_race, k_ttgp)->path, k_group_path);

  auto plugin = load(two_groups());
  const auto spans = fo4::subrecord_group_spans(plugin, key(), group);
  ASSERT_TRUE(spans.has_value());
  ASSERT_EQ(spans->size(), 3u);
  EXPECT_EQ((*spans)[0].first, 2u);
  EXPECT_EQ((*spans)[0].end, 7u);
  EXPECT_EQ((*spans)[1].first, 7u);
  EXPECT_EQ((*spans)[1].end, 14u);
  EXPECT_EQ((*spans)[2].first, 16u);
  EXPECT_EQ((*spans)[2].end, 19u);

  const auto traced = order(plugin);
  ASSERT_EQ(traced.size(), 20u);
  EXPECT_EQ(traced[1], "TTGP");
  EXPECT_EQ(traced[14], "TTGE");
  EXPECT_EQ(traced[15], "TTGP");
  EXPECT_EQ(traced[19], "TTGE");
}

// Constraint: TETI holds Slot u16 at 0 and Index u16 at 2, and TTEF is the u16 flags word.
TEST(RaceTintRows, OptionRowsCarryXEditOffsetsStorageAndNames) {
  const auto slot = row("RACE\\TintLayerSlot");
  EXPECT_EQ(slot.scalar.subrecord, k_teti);
  EXPECT_EQ(slot.scalar.storage, fo4::schema::ScalarStorage::u16);
  EXPECT_EQ(slot.scalar.presentation, fo4::schema::ScalarPresentation::enumeration);
  EXPECT_TRUE(slot.scalar.embedded);
  EXPECT_EQ(slot.scalar.offset, 0u);
  EXPECT_EQ(slot.scalar.names.size(), 26u);
  EXPECT_EQ(value_of(slot.scalar, "Lip Color"), 7);
  EXPECT_EQ(value_of(slot.scalar, "Beards"), 25);
  EXPECT_EQ(slot.scalar.maximum, 25.0);
  EXPECT_TRUE(slot.scalar.address.repeated);

  const auto index = row("RACE\\TintLayerIndex");
  EXPECT_EQ(index.scalar.subrecord, k_teti);
  EXPECT_EQ(index.scalar.storage, fo4::schema::ScalarStorage::u16);
  EXPECT_EQ(index.scalar.offset, 2u);
  EXPECT_TRUE(index.scalar.address.repeated);

  const auto flags = row("RACE\\TintLayerFlags");
  EXPECT_EQ(flags.scalar.subrecord, fo4::Signature::from_string("TTEF"));
  EXPECT_EQ(flags.scalar.storage, fo4::schema::ScalarStorage::u16);
  EXPECT_EQ(flags.scalar.presentation, fo4::schema::ScalarPresentation::flags);
  EXPECT_EQ(flags.scalar.payload_size, 2u);
  EXPECT_EQ(flags.scalar.names.size(), 3u);
  EXPECT_EQ(value_of(flags.scalar, "On/Off only"), 1);
  EXPECT_EQ(value_of(flags.scalar, "Chargen Detail"), 2);
  EXPECT_EQ(value_of(flags.scalar, "Takes Skin Tone"), 4);

  const auto texture = row("RACE\\TintLayerTexture");
  EXPECT_EQ(texture.scalar.subrecord, k_ttet);
  EXPECT_EQ(texture.scalar.storage, fo4::schema::ScalarStorage::zstring);
  EXPECT_FALSE(texture.scalar.embedded);
  EXPECT_TRUE(texture.scalar.address.repeated);

  const auto blend = row("RACE\\TintLayerBlendOperation");
  EXPECT_EQ(blend.scalar.subrecord, fo4::Signature::from_string("TTEB"));
  EXPECT_EQ(blend.scalar.storage, fo4::schema::ScalarStorage::u32);
  EXPECT_EQ(blend.scalar.presentation, fo4::schema::ScalarPresentation::enumeration);
  EXPECT_EQ(blend.scalar.payload_size, 4u);
  EXPECT_EQ(value_of(blend.scalar, "Hard Light"), 4);

  const auto name = row("RACE\\TintLayerName");
  EXPECT_EQ(name.scalar.subrecord, k_ttgp);
  EXPECT_EQ(name.scalar.storage, fo4::schema::ScalarStorage::zstring);
  EXPECT_TRUE(name.scalar.localized);
  EXPECT_TRUE(name.scalar.address.repeated);
  EXPECT_TRUE(fo4::schema::is_localized_field(k_race, k_ttgp));

  const auto alpha = row("RACE\\TintTemplateColorAlpha");
  EXPECT_EQ(alpha.scalar.subrecord, k_ttec);
  EXPECT_EQ(alpha.scalar.storage, fo4::schema::ScalarStorage::f32);
  EXPECT_EQ(alpha.scalar.offset, 4u);
  EXPECT_EQ(alpha.scalar.address.stride, 14u);
  EXPECT_TRUE(alpha.scalar.address.repeated);

  const auto template_index = row("RACE\\TintTemplateColorTemplateIndex");
  EXPECT_EQ(template_index.scalar.storage, fo4::schema::ScalarStorage::u16);
  EXPECT_EQ(template_index.scalar.offset, 8u);
  EXPECT_EQ(template_index.scalar.address.stride, 14u);

  const auto color_blend = row("RACE\\TintTemplateColorBlendOperation");
  EXPECT_EQ(color_blend.scalar.storage, fo4::schema::ScalarStorage::u32);
  EXPECT_EQ(color_blend.scalar.offset, 10u);
  EXPECT_EQ(color_blend.scalar.address.stride, 14u);

  const auto fallback = row("RACE\\TintLayerDefault");
  EXPECT_EQ(fallback.scalar.subrecord, fo4::Signature::from_string("TTED"));
  EXPECT_EQ(fallback.scalar.storage, fo4::schema::ScalarStorage::f32);
  EXPECT_EQ(fallback.scalar.payload_size, 4u);
  EXPECT_TRUE(fallback.scalar.address.repeated);

  const auto category = row("RACE\\TintLayerCategoryIndex");
  EXPECT_EQ(category.scalar.subrecord, k_ttge);
  EXPECT_EQ(category.scalar.storage, fo4::schema::ScalarStorage::u32);
  EXPECT_FALSE(category.scalar.embedded);
  EXPECT_TRUE(category.scalar.address.repeated);

  const auto color = link("RACE\\TintTemplateColor");
  EXPECT_EQ(color.subrecord, k_ttec);
  EXPECT_EQ(color.offset, 0u);
  EXPECT_EQ(color.address.stride, 14u);
  EXPECT_TRUE(color.address.repeated);
  EXPECT_TRUE(fo4::schema::formlink_field_target_allowed(color, k_clfm));
  EXPECT_FALSE(fo4::schema::formlink_field_target_allowed(color, k_spel));
}

// Constraint: TTEC elements are 14 bytes and a payload that is not a whole number of them is ragged.
TEST(RaceTintRows, TemplateColorsStrideFourteenBytesAndRejectRaggedPayloads) {
  auto plugin = load({{"TTGP", zstr("Skin")},
                            {"TETI", teti(12, 0)},
                            {"TTGP", zstr("Skin Tone")},
                            {"TTEC", concat({ttec_element(0x00000B01u, 0.25f, 3, 1),
                                             ttec_element(0x00000B02u, 0.75f, 4, 2)})}});
  EXPECT_EQ(read_index(plugin, "RACE\\TintTemplateColorAlpha", 0), "0.25");
  EXPECT_EQ(read_index(plugin, "RACE\\TintTemplateColorAlpha", 1), "0.75");
  EXPECT_EQ(read_index(plugin, "RACE\\TintTemplateColorAlpha", 2), "absent");
  EXPECT_EQ(read_index(plugin, "RACE\\TintTemplateColorTemplateIndex", 0), "3");
  EXPECT_EQ(read_index(plugin, "RACE\\TintTemplateColorTemplateIndex", 1), "4");
  EXPECT_EQ(read_index(plugin, "RACE\\TintTemplateColorBlendOperation", 1), "2");
  EXPECT_EQ(fo4::record_field_count(plugin, key(), row("RACE\\TintTemplateColorAlpha")),
            std::optional<std::size_t>{2});

  auto ragged = load({{"TTGP", zstr("Skin")},
                      {"TETI", teti(12, 0)},
                      {"TTGP", zstr("Skin Tone")},
                      {"TTEC", Bytes(20, std::byte{0})}});
  EXPECT_EQ(fo4::record_field_count(ragged, key(), row("RACE\\TintTemplateColorAlpha")),
            std::optional<std::size_t>{0});
  EXPECT_EQ(fo4::set_record_scalar(ragged, key(), row("RACE\\TintTemplateColorAlpha"), 0, "0.5"),
            fo4::ScalarMutationStatus::subrecord_missing);
  const auto kept = fo4::read_plugin(fo4::write_plugin(ragged), "Patch.esp");
  EXPECT_EQ(payloads(kept, k_ttec), (std::vector<Bytes>{Bytes(20, std::byte{0})}));
}

// Constraint: an indexed write touches only the addressed TTEC/TTED/TETI/TTEF element or subrecord.
TEST(RaceTintRows, WritesTouchOnlyTheAddressedOption) {
  auto plugin = load({{"TTGP", zstr("Body")},
                      {"TETI", teti(7, 3)},
                      {"TTGP", zstr("Lip Color")},
                      {"TTEF", u16_bytes(5)},
                      {"TTET", zstr("Lip_n.dds")},
                      {"TTEC", concat({ttec_element(0x00000B01u, 0.25f, 3, 1),
                                       ttec_element(0x00000B02u, 0.75f, 4, 2)})},
                      {"TTED", f32_bytes(0.5f)},
                      {"TETI", teti(12, 4)},
                      {"TTGP", zstr("Skin Tone")},
                      {"TTEF", u16_bytes(4)},
                      {"TTET", zstr("Skin_n.dds")},
                      {"TTEC", concat({ttec_element(0x00000B01u, 0.125f, 5, 3)})},
                      {"TTED", f32_bytes(0.75f)}});

  EXPECT_EQ(fo4::set_record_scalar(plugin, key(), row("RACE\\TintLayerSlot"), 1, "9"),
            fo4::ScalarMutationStatus::updated);
  EXPECT_EQ(fo4::set_record_scalar(plugin, key(), row("RACE\\TintLayerIndex"), 0, "2"),
            fo4::ScalarMutationStatus::updated);
  EXPECT_EQ(fo4::set_record_scalar(plugin, key(), row("RACE\\TintLayerFlags"), 1, "1"),
            fo4::ScalarMutationStatus::updated);
  EXPECT_EQ(fo4::set_record_scalar(plugin, key(), row("RACE\\TintTemplateColorAlpha"), 1, "0.5"),
            fo4::ScalarMutationStatus::updated);
  EXPECT_EQ(fo4::set_record_scalar(plugin, key(), row("RACE\\TintLayerDefault"), 1, "0.25"),
            fo4::ScalarMutationStatus::updated);
  EXPECT_EQ(fo4::set_record_formlink_field(plugin, key(), link("RACE\\TintTemplateColor"), 0,
                                           fo4::FormKey::parse("000B02:Patch.esp")),
            fo4::FormLinkFieldMutationStatus::updated);
  EXPECT_EQ(fo4::set_record_string(plugin, key(), row("RACE\\TintLayerTexture"), 1, "Skin_b.dds"),
            fo4::ScalarMutationStatus::updated);

  const auto reread = fo4::read_plugin(fo4::write_plugin(plugin), "Patch.esp");
  EXPECT_EQ(read_index(reread, "RACE\\TintLayerSlot", 0), "2");
  EXPECT_EQ(read_index(reread, "RACE\\TintLayerSlot", 1), "9");
  EXPECT_EQ(read_index(reread, "RACE\\TintLayerIndex", 0), "2");
  EXPECT_EQ(read_index(reread, "RACE\\TintLayerIndex", 1), "4");
  EXPECT_EQ(read_index(reread, "RACE\\TintLayerFlags", 0), "5");
  EXPECT_EQ(read_index(reread, "RACE\\TintLayerFlags", 1), "1");
  EXPECT_EQ(read_index(reread, "RACE\\TintTemplateColorAlpha", 0), "0.25");
  EXPECT_EQ(read_index(reread, "RACE\\TintTemplateColorAlpha", 1), "0.5");
  EXPECT_EQ(read_index(reread, "RACE\\TintTemplateColorTemplateIndex", 1), "5");
  EXPECT_EQ(read_index(reread, "RACE\\TintLayerDefault", 0), "0.5");
  EXPECT_EQ(read_index(reread, "RACE\\TintLayerDefault", 1), "0.25");

  const auto colors = payloads(reread, k_ttec);
  ASSERT_EQ(colors.size(), 2u);
  EXPECT_EQ(colors[0], ttec_element(0x00000B02u, 0.25f, 3, 1));
  EXPECT_EQ(colors[1], ttec_element(0x00000B01u, 0.5f, 5, 3));
  EXPECT_EQ(payloads(reread, k_ttet), (std::vector<Bytes>{zstr("Lip_n.dds"), zstr("Skin_b.dds")}));
}

// Constraint: a localized TTGP carries a four-byte string id, so inline writes are refused.
TEST(RaceTintRows, LocalizedNamesAreFourByteStringIds) {
  auto plugin = load({{"TTGP", u32_bytes(0x00035763u)},
                            {"TETI", teti(7, 3)},
                            {"TTGP", u32_bytes(0x00035764u)}},
                           0x80);
  const auto name = row("RACE\\TintLayerName");
  EXPECT_EQ(fo4::record_field_count(plugin, key(), name), std::optional<std::size_t>{2});
  EXPECT_THROW(fo4::set_record_string(plugin, key(), name, 1, "Inline"), fo4::schema::ScalarError);

  const auto spans = fo4::subrecord_group_spans(plugin, key(), tint_group());
  ASSERT_TRUE(spans.has_value());
  EXPECT_EQ(spans->size(), 1u);

  const auto& record = race_record(plugin);
  const auto found = std::find_if(record.subrecords.begin(), record.subrecords.end(),
                                  [](const auto& sub) { return sub.signature == k_ttgp; });
  ASSERT_NE(found, record.subrecords.end());
  const auto text = fo4::schema::read_localized_string(plugin, k_race, k_ttgp, *found);
  ASSERT_TRUE(text.id.has_value());
  EXPECT_EQ(*text.id, 0x00035763u);
}

// Constraint: TTGE is a successor, so it ends the group and a later TTGP starts a new span.
TEST(RaceTintRows, SecondGroupOptionsStayReachableAfterTheCategoryIndex) {
  auto plugin = load(two_groups());
  const auto spans = fo4::subrecord_group_spans(plugin, key(), tint_group());
  ASSERT_TRUE(spans.has_value());
  ASSERT_EQ(spans->size(), 3u);
  EXPECT_EQ(read_index(plugin, "RACE\\TintLayerSlot", 2), "9");
  EXPECT_EQ(read_index(plugin, "RACE\\TintLayerIndex", 2), "5");
  EXPECT_EQ(read_index(plugin, "RACE\\TintLayerCategoryIndex", 0), "2");
  EXPECT_EQ(read_index(plugin, "RACE\\TintLayerCategoryIndex", 1), "1");
  EXPECT_EQ(read_index(plugin, "RACE\\TintTemplateColorAlpha", 0), "0.5");
  EXPECT_EQ(read_index(plugin, "RACE\\TintLayerDefault", 0), "0.75");
  EXPECT_EQ(fo4::record_field_count(plugin, key(), row("RACE\\TintLayerName")),
            std::optional<std::size_t>{4});
}
