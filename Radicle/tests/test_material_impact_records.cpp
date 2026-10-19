#include "fo4/plugin/mutation.hpp"
#include "fo4/plugin/reader.hpp"
#include "fo4/plugin/writer.hpp"
#include "fo4/schema/behavior_layout.hpp"
#include "fo4/schema/formlink_lists.hpp"
#include "fo4/schema/record_constraints.hpp"
#include "fo4/schema/scalar.hpp"
#include "plugin_bytes.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace {

using Bytes = std::vector<std::byte>;
using fo4::test::ByteWriter;

struct Sub {
  std::string_view signature;
  Bytes data;
};

struct Entry {
  std::uint32_t local_id;
  std::vector<Sub> subs;
};

struct Item {
  std::string_view signature;
  std::string_view key;
  std::string_view scalar_path;
  std::string_view scalar_value;
  std::string_view text_path;
  std::string_view text_value;
  std::string_view link_path;
  std::string_view link_target;
};

constexpr Item k_items[] = {
    {"MATO", "000804:Patch.esp", "MATO\\FalloffBias", "0.75", "", "", "", ""},
    {"MATT", "000801:Patch.esp", "MATT\\Buoyancy", "2.5", "MATT\\MaterialName", "Edited",
     "MATT\\MaterialParent", "000803:Patch.esp"},
    {"IPCT", "000805:Patch.esp", "IPCT\\EffectOrientation", "1", "", "", "IPCT\\Sound1", "00080A:Patch.esp"},
    {"LENS", "000806:Patch.esp", "LENS\\ColorInfluence", "0.25", "", "", "", ""},
    {"RFCT", "000807:Patch.esp", "RFCT\\Flags", "1", "", "", "RFCT\\EffectArt", "00080C:Patch.esp"},
};

constexpr std::uint32_t raw_id(std::uint32_t local_id) { return 0x02000000u | local_id; }

Bytes zstr(std::string_view text) {
  ByteWriter out;
  out.raw(text);
  out.u8(0);
  return out.bytes;
}

Bytes zeros(std::size_t size) { return Bytes(size); }

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

Bytes lfsd_bytes(float opacity, std::uint32_t flags) {
  ByteWriter out;
  out.f32(1.0f);
  out.f32(0.5f);
  out.f32(0.25f);
  out.f32(4.0f);
  out.f32(5.0f);
  out.f32(6.0f);
  out.f32(7.0f);
  out.f32(opacity);
  out.u32(flags);
  return out.bytes;
}

Bytes ipct_data() {
  ByteWriter out;
  out.f32(0.25f);
  out.u32(0);
  out.f32(15.0f);
  out.f32(16.0f);
  out.u32(1);
  out.u8(0);
  out.u8(0);
  out.u16(0);
  return out.bytes;
}

Bytes rfct_data() {
  ByteWriter out;
  out.u32(raw_id(0x80B));
  out.u32(0);
  out.u32(0);
  return out.bytes;
}

void write_group(ByteWriter& out, std::string_view label, const std::vector<Entry>& entries) {
  const auto group = fo4::test::begin_group(out, label, 0);
  for (const auto& entry : entries) {
    const auto record = fo4::test::begin_record(out, label, 0, raw_id(entry.local_id));
    for (const auto& sub : entry.subs) {
      fo4::test::write_subrecord(out, sub.signature, sub.data);
    }
    fo4::test::end_record(out, record);
  }
  fo4::test::end_group(out, group);
}

std::vector<std::byte> make_plugin() {
  ByteWriter out;
  const auto tes4 = fo4::test::begin_record(out, "TES4", 0, 0);
  ByteWriter hedr;
  hedr.f32(1.0f);
  hedr.u32(2);
  hedr.u32(0x900);
  fo4::test::write_subrecord(out, "HEDR", hedr.bytes);
  fo4::test::write_subrecord(out, "MAST", std::string_view{"Fallout4.esm\0", 13});
  fo4::test::write_subrecord(out, "DATA", zeros(8));
  fo4::test::write_subrecord(out, "MAST", std::string_view{"MagicMaster.esp\0", 16});
  fo4::test::write_subrecord(out, "DATA", zeros(8));
  fo4::test::end_record(out, tes4);

  write_group(out, "MATT", {{0x801, {{"EDID", zstr("RadicleMaterial")},
                                     {"PNAM", u32_bytes(raw_id(0x802))},
                                     {"MNAM", zstr("Radicle")},
                                     {"CNAM", zeros(12)},
                                     {"BNAM", f32_bytes(1.0f)},
                                     {"FNAM", u32_bytes(0x2)},
                                     {"HNAM", u32_bytes(raw_id(0x808))}}},
                            {0x802, {{"EDID", zstr("RadicleMaterialB")},
                                     {"CNAM", zeros(12)},
                                     {"BNAM", f32_bytes(1.0f)},
                                     {"FNAM", u32_bytes(0)}}},
                            {0x803, {{"EDID", zstr("RadicleMaterialC")},
                                     {"CNAM", zeros(12)},
                                     {"BNAM", f32_bytes(1.0f)},
                                     {"FNAM", u32_bytes(0)}}}});
  write_group(out, "MATO", {{0x804, {{"EDID", zstr("RadicleMaterialObject")},
                                     {"DATA", zeros(48)}}}});
  write_group(out, "IPCT", {{0x805, {{"EDID", zstr("RadicleImpact")},
                                     {"DATA", ipct_data()},
                                     {"SNAM", u32_bytes(raw_id(0x809))}}}});
  write_group(out, "LENS", {{0x806, {{"EDID", zstr("RadicleFlare")},
                                     {"CNAM", f32_bytes(0.5f)},
                                     {"DNAM", f32_bytes(0.7f)},
                                     {"LFSP", u32_bytes(2)},
                                     {"DNAM", zstr("SpriteA")},
                                     {"FNAM", zstr("flare_a.dds")},
                                     {"LFSD", lfsd_bytes(0.75f, 1)},
                                     {"DNAM", zstr("SpriteB")},
                                     {"FNAM", zstr("flare_b.dds")},
                                     {"LFSD", lfsd_bytes(0.5f, 0)}}},
                            {0x80D, {{"EDID", zstr("RadicleSpritesOnly")},
                                     {"LFSP", u32_bytes(1)},
                                     {"DNAM", zstr("SpriteOnly")},
                                     {"FNAM", zstr("flare_c.dds")},
                                     {"LFSD", lfsd_bytes(0.25f, 0)}}},
                            {0x80F, {{"EDID", zstr("RadicleThreeSprites")},
                                     {"DNAM", f32_bytes(3.0f)},
                                     {"LFSP", u32_bytes(3)},
                                     {"DNAM", zstr("SpriteA")},
                                     {"FNAM", zstr("flare_a.dds")},
                                     {"LFSD", lfsd_bytes(0.75f, 1)},
                                     {"DNAM", zstr("SpriteB")},
                                     {"FNAM", zstr("flare_b.dds")},
                                     {"LFSD", lfsd_bytes(0.5f, 0)},
                                     {"DNAM", zstr("SpriteC")},
                                     {"FNAM", zstr("flare_c.dds")},
                                     {"LFSD", lfsd_bytes(0.25f, 2)}}}});
  write_group(out, "RFCT", {{0x807, {{"EDID", zstr("RadicleVisual")},
                                     {"DATA", rfct_data()}}}});
  write_group(out, "IPDS", {{0x808, {{"EDID", zstr("RadicleImpactSet")}}}});
  write_group(out, "SNDR", {{0x809, {{"EDID", zstr("RadicleSoundA")}}},
                            {0x80A, {{"EDID", zstr("RadicleSoundB")}}}});
  write_group(out, "ARTO", {{0x80B, {{"EDID", zstr("RadicleArtA")}}},
                            {0x80C, {{"EDID", zstr("RadicleArtB")}}}});
  return out.bytes;
}

const fo4::Subrecord* find_subrecord(const fo4::Record& record, fo4::Signature signature) {
  for (const auto& sub : record.subrecords) {
    if (sub.signature == signature) return &sub;
  }
  return nullptr;
}

Bytes subrecord_at(const fo4::Record& record, std::string_view signature, std::size_t ordinal) {
  std::size_t seen = 0;
  for (const auto& sub : record.subrecords) {
    if (sub.signature.to_string() != signature) continue;
    if (seen++ == ordinal) return sub.data;
  }
  ADD_FAILURE() << signature << ' ' << ordinal;
  return {};
}

fo4::schema::SubrecordGroupDescriptor sprites_group() {
  const auto group = fo4::schema::subrecord_group_descriptor(fo4::Signature::from_string("LENS"),
                                                             "LENS\\Sprites");
  EXPECT_TRUE(group.has_value());
  return group.value_or(fo4::schema::SubrecordGroupDescriptor{});
}

fo4::schema::ScalarFieldDescriptor sprite_row(std::string_view path) {
  const auto field = fo4::schema::scalar_field_descriptor(fo4::Signature::from_string("LENS"), path);
  EXPECT_TRUE(field.has_value()) << path;
  return field.value_or(fo4::schema::ScalarFieldDescriptor{});
}

std::vector<std::byte> make_short_lens_plugin() {
  ByteWriter out;
  out.bytes = fo4::test::make_tes4_plugin(0, 1.0f, {"Fallout4.esm"});
  const auto group = fo4::test::begin_group(out, "LENS", 0);
  const auto record = fo4::test::begin_record(out, "LENS", 0, 0x0100080Eu);
  fo4::test::write_subrecord(out, "EDID", zstr("RadicleShort"));
  fo4::test::write_subrecord(out, "LFSP", u32_bytes(2));
  fo4::test::write_subrecord(out, "DNAM", zstr("SpriteA"));
  fo4::test::write_subrecord(out, "FNAM", zstr("flare_a.dds"));
  fo4::test::write_subrecord(out, "LFSD", lfsd_bytes(0.5f, 0));
  fo4::test::end_record(out, record);
  fo4::test::end_group(out, group);
  return out.bytes;
}

std::vector<std::byte> make_unterminated_sprite_plugin() {
  ByteWriter out;
  out.bytes = fo4::test::make_tes4_plugin(0, 1.0f, {"Fallout4.esm"});
  const auto group = fo4::test::begin_group(out, "LENS", 0);
  const auto record = fo4::test::begin_record(out, "LENS", 0, 0x01000810u);
  fo4::test::write_subrecord(out, "EDID", zstr("RadicleUnterminated"));
  fo4::test::write_subrecord(out, "LFSP", u32_bytes(1));
  ByteWriter id;
  id.raw("xy");
  fo4::test::write_subrecord(out, "DNAM", id.bytes);
  fo4::test::write_subrecord(out, "FNAM", zstr("flare_a.dds"));
  fo4::test::write_subrecord(out, "LFSD", lfsd_bytes(0.5f, 0));
  fo4::test::end_record(out, record);
  fo4::test::end_group(out, group);
  return out.bytes;
}

std::string row_string(const fo4::Plugin& plugin,
                       const fo4::FormKey& key,
                       const fo4::schema::ScalarFieldDescriptor& field,
                       std::size_t index) {
  const auto* record = fo4::find_record(plugin, key);
  if (record == nullptr) {
    ADD_FAILURE() << "missing record " << key.to_string();
    return {};
  }
  const auto resolved = fo4::schema::resolve_field_slots(
      *record, fo4::schema::scalar_field_shape(field.scalar), field.scalar.address);
  if (index >= resolved.slots.size() || !resolved.slots[index].valid) {
    ADD_FAILURE() << field.path << ' ' << index;
    return {};
  }
  const auto slot = resolved.slots[index];
  const auto bytes = fo4::schema::scalar_slot_bytes(record->subrecords[slot.subrecord].data, slot,
                                                    field.scalar);
  return fo4::schema::decode_scalar(bytes, field.scalar);
}

}  // namespace

TEST(MaterialImpactSynthetic, EveryScalarRowDecodesOnTheFixture) {
  const auto plugin = fo4::read_plugin(make_plugin(), "Patch.esp");
  for (const auto& item : k_items) {
    const auto signature = fo4::Signature::from_string(item.signature);
    const auto* record = fo4::find_record(plugin, fo4::FormKey::parse(item.key));
    ASSERT_NE(record, nullptr) << item.signature;
    std::size_t decoded = 0;
    for (const auto& field : fo4::schema::scalar_fields(signature)) {
      const auto resolved = fo4::schema::resolve_field_slots(
          *record, fo4::schema::scalar_field_shape(field.scalar), field.scalar.address);
      for (const auto& slot : resolved.slots) {
        if (!slot.valid) continue;
        EXPECT_NO_THROW({
          const auto bytes = fo4::schema::scalar_slot_bytes(
              record->subrecords[slot.subrecord].data, slot, field.scalar);
          if (!bytes.empty()) static_cast<void>(fo4::schema::decode_scalar(bytes, field.scalar));
        }) << field.path;
        ++decoded;
      }
    }
    EXPECT_GT(decoded, 0u) << item.signature;
  }
}

TEST(MaterialImpactSynthetic, FlagsRowNamesComeFromXEdit) {
  const auto ipct = fo4::schema::scalar_field_descriptor(fo4::Signature::from_string("IPCT"),
                                                         "IPCT\\Flags");
  ASSERT_TRUE(ipct.has_value());
  const auto ipct_name = fo4::schema::scalar_value_name(ipct->scalar, 0x1);
  ASSERT_TRUE(ipct_name.has_value());
  EXPECT_EQ(*ipct_name, "No Decal Data");

  const auto matt = fo4::schema::scalar_field_descriptor(fo4::Signature::from_string("MATT"),
                                                         "MATT\\Flags");
  ASSERT_TRUE(matt.has_value());
  const auto matt_name = fo4::schema::scalar_value_name(matt->scalar, 0x4);
  ASSERT_TRUE(matt_name.has_value());
  EXPECT_EQ(*matt_name, "Can Tunnel");
}

TEST(MaterialImpactSynthetic, LensFadeScaleUsesTheDnamBeforeTheSpriteList) {
  auto plugin = fo4::read_plugin(make_plugin(), "Patch.esp");
  const auto key = fo4::FormKey::parse("000806:Patch.esp");
  const auto* record = fo4::find_record(plugin, key);
  ASSERT_NE(record, nullptr);
  const auto field = fo4::schema::scalar_field_descriptor(fo4::Signature::from_string("LENS"),
                                                          "LENS\\FadeDistanceRadiusScale");
  ASSERT_TRUE(field.has_value());

  std::vector<const fo4::Subrecord*> sprites;
  for (const auto& sub : record->subrecords) {
    if (sub.signature == fo4::Signature::from_string("DNAM")) sprites.push_back(&sub);
  }
  ASSERT_EQ(sprites.size(), std::size_t{3});
  EXPECT_EQ(sprites[0]->data, f32_bytes(0.7f));

  EXPECT_EQ(fo4::set_record_scalar(plugin, key, *field, "2.5"),
            fo4::ScalarMutationStatus::updated);

  const auto reread = fo4::read_plugin(fo4::write_plugin(plugin), "Patch.esp");
  const auto* edited = fo4::find_record(reread, key);
  ASSERT_NE(edited, nullptr);
  std::vector<const fo4::Subrecord*> after;
  for (const auto& sub : edited->subrecords) {
    if (sub.signature == fo4::Signature::from_string("DNAM")) after.push_back(&sub);
  }
  ASSERT_EQ(after.size(), std::size_t{3});
  EXPECT_EQ(after[0]->data, f32_bytes(2.5f));
  EXPECT_EQ(after[1]->data, zstr("SpriteA"));
  EXPECT_EQ(after[2]->data, zstr("SpriteB"));
}

TEST(MaterialImpactSynthetic, LensFadeScaleIsMissingWithoutARecordLevelDnam) {
  auto plugin = fo4::read_plugin(make_plugin(), "Patch.esp");
  const auto key = fo4::FormKey::parse("00080D:Patch.esp");
  const auto field = fo4::schema::scalar_field_descriptor(fo4::Signature::from_string("LENS"),
                                                          "LENS\\FadeDistanceRadiusScale");
  ASSERT_TRUE(field.has_value());
  EXPECT_EQ(fo4::set_record_scalar(plugin, key, *field, "2.5"),
            fo4::ScalarMutationStatus::subrecord_missing);

  const auto* record = fo4::find_record(plugin, key);
  ASSERT_NE(record, nullptr);
  std::size_t sprites = 0;
  for (const auto& sub : record->subrecords) {
    if (sub.signature != fo4::Signature::from_string("DNAM")) continue;
    ++sprites;
    EXPECT_EQ(sub.data, zstr("SpriteOnly"));
  }
  EXPECT_EQ(sprites, std::size_t{1});
}

TEST(MaterialImpactSynthetic, EditsOneRowPerRecordAndRereads) {
  auto plugin = fo4::read_plugin(make_plugin(), "Patch.esp");
  for (const auto& item : k_items) {
    const auto signature = fo4::Signature::from_string(item.signature);
    const auto record = fo4::FormKey::parse(item.key);
    const auto scalar = fo4::schema::scalar_field_descriptor(signature, item.scalar_path);
    ASSERT_TRUE(scalar.has_value()) << item.scalar_path;
    EXPECT_EQ(fo4::set_record_scalar(plugin, record, *scalar, item.scalar_value),
              fo4::ScalarMutationStatus::updated)
        << item.scalar_path;
    if (!item.text_path.empty()) {
      EXPECT_EQ(fo4::set_record_string(plugin, record, item.text_path, 0, item.text_value),
                fo4::ScalarMutationStatus::updated)
          << item.text_path;
    }
    if (!item.link_path.empty()) {
      const auto link = fo4::schema::formlink_field_descriptor(signature, item.link_path);
      ASSERT_TRUE(link.has_value()) << item.link_path;
      EXPECT_EQ(fo4::set_record_formlink_field(plugin, record, *link,
                                               fo4::FormKey::parse(item.link_target)),
                fo4::FormLinkFieldMutationStatus::updated)
          << item.link_path;
    }
  }

  const auto reread = fo4::read_plugin(fo4::write_plugin(plugin), "Patch.esp");
  for (const auto& item : k_items) {
    const auto signature = fo4::Signature::from_string(item.signature);
    const auto record_key = fo4::FormKey::parse(item.key);
    const auto* record = fo4::find_record(reread, record_key);
    ASSERT_NE(record, nullptr) << item.signature;

    const auto scalar = fo4::schema::scalar_field_descriptor(signature, item.scalar_path);
    ASSERT_TRUE(scalar.has_value());
    const auto* scalar_sub = find_subrecord(*record, scalar->scalar.subrecord);
    ASSERT_NE(scalar_sub, nullptr);
    const auto expected = fo4::schema::encode_scalar(item.scalar_value, scalar->scalar);
    const auto actual = fo4::schema::scalar_field_bytes(scalar_sub->data, scalar->scalar);
    EXPECT_EQ(Bytes(actual.begin(), actual.end()), expected) << item.scalar_path;

    if (!item.text_path.empty()) {
      const auto text = fo4::schema::scalar_field_descriptor(signature, item.text_path);
      ASSERT_TRUE(text.has_value());
      const auto* text_sub = find_subrecord(*record, text->scalar.subrecord);
      ASSERT_NE(text_sub, nullptr);
      EXPECT_EQ(text_sub->data, zstr(item.text_value)) << item.text_path;
    }

    if (!item.link_path.empty()) {
      const auto link = fo4::schema::formlink_field_descriptor(signature, item.link_path);
      ASSERT_TRUE(link.has_value());
      const auto* link_sub = find_subrecord(*record, link->subrecord);
      ASSERT_NE(link_sub, nullptr);
      ASSERT_GE(link_sub->data.size(), link->offset + sizeof(std::uint32_t));
      const auto* first = link_sub->data.data() + link->offset;
      const Bytes stored(first, first + sizeof(std::uint32_t));
      const auto expected_link = raw_id(fo4::FormKey::parse(item.link_target).local().value());
      EXPECT_EQ(stored, u32_bytes(expected_link)) << item.link_path;
    }
  }
}

TEST(MaterialImpactSynthetic, FixtureRecordsMeetTheirAuthoringMinimums) {
  const auto plugin = fo4::read_plugin(make_plugin(), "Patch.esp");
  for (const auto& item : k_items) {
    const auto signature = fo4::Signature::from_string(item.signature);
    const auto constraint = fo4::schema::record_authoring_constraint(signature);
    ASSERT_TRUE(constraint.has_value()) << item.signature;
    const auto* record = fo4::find_record(plugin, fo4::FormKey::parse(item.key));
    ASSERT_NE(record, nullptr);
    for (const auto& rule : fo4::schema::authoring_subrecords(*constraint)) {
      const auto count = static_cast<std::size_t>(std::count_if(
          record->subrecords.begin(), record->subrecords.end(),
          [&](const fo4::Subrecord& sub) { return sub.signature == rule.signature; }));
      EXPECT_GE(count, static_cast<std::size_t>(rule.minimum)) << item.signature << ' ' << rule.signature.to_string();
      EXPECT_LE(count, static_cast<std::size_t>(rule.maximum)) << item.signature << ' ' << rule.signature.to_string();
    }
  }
}

TEST(MaterialImpactSynthetic, LensSpriteGroupRowsFollowTheCountSubrecord) {
  const auto group = sprites_group();
  EXPECT_EQ(group.record, fo4::Signature::from_string("LENS"));
  EXPECT_EQ(group.count_subrecord, fo4::Signature::from_string("LFSP"));
  ASSERT_EQ(group.members.size(), 3u);
  EXPECT_EQ(group.members[0].subrecord, fo4::Signature::from_string("DNAM"));
  EXPECT_TRUE(group.members[0].required);
  ASSERT_EQ(group.members[0].default_payload.size(), 1u);
  EXPECT_EQ(group.members[0].default_payload[0], std::uint8_t{0});
  EXPECT_EQ(group.members[1].subrecord, fo4::Signature::from_string("FNAM"));
  ASSERT_EQ(group.members[1].default_payload.size(), 1u);
  EXPECT_EQ(group.members[2].subrecord, fo4::Signature::from_string("LFSD"));
  EXPECT_EQ(group.members[2].payload_size, 36u);

  const auto member = fo4::schema::subrecord_group_for_member(fo4::Signature::from_string("LENS"),
                                                             fo4::Signature::from_string("LFSD"));
  ASSERT_TRUE(member.has_value());
  EXPECT_EQ(member->path, group.path);

  const auto plugin = fo4::read_plugin(make_plugin(), "Patch.esp");
  const auto spans =
      fo4::subrecord_group_spans(plugin, fo4::FormKey::parse("000806:Patch.esp"), group);
  ASSERT_TRUE(spans.has_value());
  ASSERT_EQ(spans->size(), 2u);
  EXPECT_EQ((*spans)[0].first, 4u);
  EXPECT_EQ((*spans)[0].end, 7u);
  EXPECT_EQ((*spans)[1].first, 7u);
  EXPECT_EQ((*spans)[1].end, 10u);

  const auto only =
      fo4::subrecord_group_spans(plugin, fo4::FormKey::parse("00080D:Patch.esp"), group);
  ASSERT_TRUE(only.has_value());
  ASSERT_EQ(only->size(), 1u);
  EXPECT_EQ((*only)[0].first, 2u);
  EXPECT_EQ((*only)[0].end, 5u);
}

TEST(MaterialImpactSynthetic, LensSpriteRowsAddressOneSpriteAtATime) {
  auto plugin = fo4::read_plugin(make_plugin(), "Patch.esp");
  const auto key = fo4::FormKey::parse("000806:Patch.esp");
  const auto opacity = sprite_row("LENS\\SpriteOpacity");
  const auto flags = sprite_row("LENS\\SpriteFlags");
  EXPECT_EQ(opacity.scalar.subrecord, fo4::Signature::from_string("LFSD"));
  EXPECT_TRUE(opacity.scalar.address.repeated);
  EXPECT_EQ(opacity.scalar.offset, 28u);
  EXPECT_EQ(fo4::record_field_count(plugin, key, opacity), std::optional<std::size_t>{2});
  const auto flag_name = fo4::schema::scalar_value_name(flags.scalar, 0x2);
  ASSERT_TRUE(flag_name.has_value());
  EXPECT_EQ(*flag_name, "Shrinks When Occluded");

  EXPECT_EQ(fo4::set_record_scalar(plugin, key, opacity, 1, "0.25"),
            fo4::ScalarMutationStatus::updated);
  EXPECT_EQ(fo4::set_record_scalar(plugin, key, flags, 0, "2"), fo4::ScalarMutationStatus::updated);
  EXPECT_EQ(fo4::set_record_scalar(plugin, key, opacity, 2, "0.5"),
            fo4::ScalarMutationStatus::subrecord_missing);

  const auto reread = fo4::read_plugin(fo4::write_plugin(plugin), "Patch.esp");
  const auto* record = fo4::find_record(reread, key);
  ASSERT_NE(record, nullptr);
  EXPECT_EQ(subrecord_at(*record, "LFSD", 0), lfsd_bytes(0.75f, 2));
  EXPECT_EQ(subrecord_at(*record, "LFSD", 1), lfsd_bytes(0.25f, 0));
  EXPECT_EQ(subrecord_at(*record, "DNAM", 1), zstr("SpriteA"));
  EXPECT_EQ(subrecord_at(*record, "DNAM", 2), zstr("SpriteB"));
}

TEST(MaterialImpactSynthetic, LensSpriteInsertAndRemoveKeepTheSpriteTotal) {
  const auto key = fo4::FormKey::parse("000806:Patch.esp");
  const auto group = sprites_group();

  auto removed_plugin = fo4::read_plugin(make_plugin(), "Patch.esp");
  EXPECT_EQ(fo4::remove_record_subrecord_group(removed_plugin, key, group, 0),
            fo4::SubrecordGroupMutationStatus::updated);
  EXPECT_EQ(fo4::remove_record_subrecord_group(removed_plugin, key, group, 1),
            fo4::SubrecordGroupMutationStatus::index_out_of_range);
  const auto after_remove = fo4::read_plugin(fo4::write_plugin(removed_plugin), "Patch.esp");
  const auto* removed = fo4::find_record(after_remove, key);
  ASSERT_NE(removed, nullptr);
  EXPECT_EQ(subrecord_at(*removed, "LFSP", 0), u32_bytes(1));
  EXPECT_EQ(subrecord_at(*removed, "DNAM", 1), zstr("SpriteB"));
  EXPECT_EQ(subrecord_at(*removed, "LFSD", 0), lfsd_bytes(0.5f, 0));
  EXPECT_EQ(std::count_if(removed->subrecords.begin(), removed->subrecords.end(),
                          [](const fo4::Subrecord& sub) {
                            return sub.signature == fo4::Signature::from_string("LFSD");
                          }),
            1);

  auto inserted_plugin = fo4::read_plugin(make_plugin(), "Patch.esp");
  EXPECT_EQ(fo4::insert_record_subrecord_group(inserted_plugin, key, group, 1),
            fo4::SubrecordGroupMutationStatus::updated);
  EXPECT_EQ(fo4::insert_record_subrecord_group(inserted_plugin, key, group, 4),
            fo4::SubrecordGroupMutationStatus::index_out_of_range);
  const auto after_insert = fo4::read_plugin(fo4::write_plugin(inserted_plugin), "Patch.esp");
  const auto* inserted = fo4::find_record(after_insert, key);
  ASSERT_NE(inserted, nullptr);
  EXPECT_EQ(subrecord_at(*inserted, "LFSP", 0), u32_bytes(3));
  EXPECT_EQ(subrecord_at(*inserted, "DNAM", 2), zstr(""));
  EXPECT_EQ(subrecord_at(*inserted, "FNAM", 1), zstr(""));
  EXPECT_EQ(subrecord_at(*inserted, "LFSD", 1), Bytes(36));
  EXPECT_EQ(subrecord_at(*inserted, "LFSD", 2), lfsd_bytes(0.5f, 0));
}

TEST(MaterialImpactSynthetic, LensSpriteCountMismatchFailsClosed) {
  auto plugin = fo4::read_plugin(make_short_lens_plugin(), "Patch.esp");
  const auto key = fo4::FormKey::parse("00080E:Patch.esp");
  const auto group = sprites_group();
  const auto spans = fo4::subrecord_group_spans(plugin, key, group);
  ASSERT_TRUE(spans.has_value());
  EXPECT_EQ(spans->size(), 1u);

  const auto before = fo4::write_plugin(plugin);
  EXPECT_THROW(fo4::insert_record_subrecord_group(plugin, key, group, 1),
               fo4::schema::SubrecordGroupError);
  EXPECT_THROW(fo4::remove_record_subrecord_group(plugin, key, group, 0),
               fo4::schema::SubrecordGroupError);
  EXPECT_EQ(fo4::write_plugin(plugin), before);
}

TEST(MaterialImpactSynthetic, LensSpriteStringsAddressOneSpriteAtATime) {
  auto plugin = fo4::read_plugin(make_plugin(), "Patch.esp");
  const auto key = fo4::FormKey::parse("00080F:Patch.esp");
  const auto id = sprite_row("LENS\\SpriteId");
  const auto texture = sprite_row("LENS\\SpriteTexture");
  EXPECT_EQ(id.scalar.subrecord, fo4::Signature::from_string("DNAM"));
  EXPECT_EQ(id.scalar.storage, fo4::schema::ScalarStorage::zstring);
  EXPECT_TRUE(id.scalar.embedded);
  EXPECT_EQ(id.scalar.offset, 0u);
  EXPECT_TRUE(id.scalar.address.repeated);
  EXPECT_EQ(texture.scalar.subrecord, fo4::Signature::from_string("FNAM"));
  EXPECT_EQ(fo4::record_field_count(plugin, key, id), std::optional<std::size_t>{3});
  EXPECT_EQ(fo4::record_field_count(plugin, key, texture), std::optional<std::size_t>{3});

  constexpr const char* ids[] = {"SpriteA", "SpriteB", "SpriteC"};
  constexpr const char* textures[] = {"flare_a.dds", "flare_b.dds", "flare_c.dds"};
  for (std::size_t index = 0; index < 3; ++index) {
    EXPECT_EQ(row_string(plugin, key, id, index), ids[index]) << index;
    EXPECT_EQ(row_string(plugin, key, texture, index), textures[index]) << index;
  }

  EXPECT_EQ(fo4::set_record_string(plugin, key, id, 1, "SpriteX"),
            fo4::ScalarMutationStatus::updated);
  EXPECT_EQ(fo4::set_record_string(plugin, key, texture, 1, "flare_x.dds"),
            fo4::ScalarMutationStatus::updated);
  EXPECT_EQ(fo4::set_record_string(plugin, key, id, 3, "SpriteD"),
            fo4::ScalarMutationStatus::subrecord_missing);
  EXPECT_EQ(fo4::set_record_string(plugin, key, texture, 3, "flare_d.dds"),
            fo4::ScalarMutationStatus::subrecord_missing);

  const auto reread = fo4::read_plugin(fo4::write_plugin(plugin), "Patch.esp");
  const auto* record = fo4::find_record(reread, key);
  ASSERT_NE(record, nullptr);
  EXPECT_EQ(subrecord_at(*record, "DNAM", 0), f32_bytes(3.0f));
  EXPECT_EQ(subrecord_at(*record, "DNAM", 1), zstr(ids[0]));
  EXPECT_EQ(subrecord_at(*record, "DNAM", 2), zstr("SpriteX"));
  EXPECT_EQ(subrecord_at(*record, "DNAM", 3), zstr(ids[2]));
  EXPECT_EQ(subrecord_at(*record, "FNAM", 0), zstr(textures[0]));
  EXPECT_EQ(subrecord_at(*record, "FNAM", 1), zstr("flare_x.dds"));
  EXPECT_EQ(subrecord_at(*record, "FNAM", 2), zstr(textures[2]));
  EXPECT_EQ(subrecord_at(*record, "LFSD", 0), lfsd_bytes(0.75f, 1));
  EXPECT_EQ(subrecord_at(*record, "LFSD", 1), lfsd_bytes(0.5f, 0));
  EXPECT_EQ(subrecord_at(*record, "LFSD", 2), lfsd_bytes(0.25f, 2));
  EXPECT_EQ(row_string(reread, key, id, 0), ids[0]);
  EXPECT_EQ(row_string(reread, key, id, 2), ids[2]);
  EXPECT_EQ(row_string(reread, key, texture, 0), textures[0]);
  EXPECT_EQ(row_string(reread, key, texture, 2), textures[2]);
}

TEST(MaterialImpactSynthetic, SpriteStringRowsSkipTheRecordLevelDnam) {
  const auto plugin = fo4::read_plugin(make_plugin(), "Patch.esp");
  const auto key = fo4::FormKey::parse("000806:Patch.esp");
  const auto* record = fo4::find_record(plugin, key);
  ASSERT_NE(record, nullptr);
  ASSERT_GT(record->subrecords.size(), std::size_t{2});
  const auto dnam = fo4::Signature::from_string("DNAM");
  const auto id = sprite_row("LENS\\SpriteId");
  const auto texture = sprite_row("LENS\\SpriteTexture");

  EXPECT_EQ(record->subrecords[2].signature, dnam);
  EXPECT_EQ(record->subrecords[2].data, f32_bytes(0.7f));

  const auto sprites = fo4::behavior_field_candidates(*record, dnam, true);
  ASSERT_TRUE(sprites.has_value());
  EXPECT_EQ(*sprites, (std::vector<std::size_t>{4, 7}));
  const auto leader = fo4::behavior_field_candidates(*record, dnam, false);
  ASSERT_TRUE(leader.has_value());
  EXPECT_EQ(*leader, (std::vector<std::size_t>{2}));

  EXPECT_EQ(fo4::record_field_count(plugin, key, id), std::optional<std::size_t>{2});
  EXPECT_EQ(fo4::record_field_count(plugin, key, texture), std::optional<std::size_t>{2});
  EXPECT_EQ(row_string(plugin, key, id, 0), "SpriteA");
  EXPECT_EQ(row_string(plugin, key, id, 1), "SpriteB");
  EXPECT_EQ(row_string(plugin, key, texture, 0), "flare_a.dds");
  EXPECT_EQ(row_string(plugin, key, texture, 1), "flare_b.dds");
}

TEST(MaterialImpactSynthetic, LensSpriteStringWritesResizeTheirMemberPayload) {
  auto plugin = fo4::read_plugin(make_plugin(), "Patch.esp");
  const auto key = fo4::FormKey::parse("00080F:Patch.esp");
  const auto id = sprite_row("LENS\\SpriteId");
  const auto texture = sprite_row("LENS\\SpriteTexture");

  EXPECT_EQ(fo4::set_record_string(plugin, key, id, 1, "SpriteBB"),
            fo4::ScalarMutationStatus::updated);
  EXPECT_EQ(fo4::set_record_string(plugin, key, texture, 2, "flare_c_long.dds"),
            fo4::ScalarMutationStatus::updated);

  const auto grown = fo4::read_plugin(fo4::write_plugin(plugin), "Patch.esp");
  const auto* wide = fo4::find_record(grown, key);
  ASSERT_NE(wide, nullptr);
  EXPECT_EQ(subrecord_at(*wide, "DNAM", 2), zstr("SpriteBB"));
  EXPECT_EQ(subrecord_at(*wide, "FNAM", 2), zstr("flare_c_long.dds"));
  EXPECT_EQ(row_string(grown, key, id, 1), "SpriteBB");
  EXPECT_EQ(row_string(grown, key, texture, 2), "flare_c_long.dds");

  EXPECT_EQ(fo4::set_record_string(plugin, key, id, 1, "S"), fo4::ScalarMutationStatus::updated);
  EXPECT_EQ(fo4::set_record_string(plugin, key, id, 1, "S"), fo4::ScalarMutationStatus::unchanged);
  EXPECT_EQ(fo4::set_record_string(plugin, key, texture, 2, "f.dds"),
            fo4::ScalarMutationStatus::updated);

  const auto reread = fo4::read_plugin(fo4::write_plugin(plugin), "Patch.esp");
  const auto* record = fo4::find_record(reread, key);
  ASSERT_NE(record, nullptr);
  EXPECT_EQ(subrecord_at(*record, "DNAM", 2), zstr("S"));
  EXPECT_EQ(subrecord_at(*record, "FNAM", 2), zstr("f.dds"));
  EXPECT_EQ(subrecord_at(*record, "DNAM", 1), zstr("SpriteA"));
  EXPECT_EQ(subrecord_at(*record, "DNAM", 3), zstr("SpriteC"));
  EXPECT_EQ(row_string(reread, key, id, 1), "S");
  EXPECT_EQ(row_string(reread, key, texture, 2), "f.dds");
}

TEST(MaterialImpactSynthetic, LensSpriteStringWithoutATerminatorFailsClosed) {
  auto plugin = fo4::read_plugin(make_unterminated_sprite_plugin(), "Patch.esp");
  const auto key = fo4::FormKey::parse("000810:Patch.esp");
  const auto id = sprite_row("LENS\\SpriteId");
  const auto texture = sprite_row("LENS\\SpriteTexture");
  const auto before = fo4::write_plugin(plugin);

  EXPECT_EQ(fo4::record_field_count(plugin, key, id), std::optional<std::size_t>{1});
  EXPECT_THROW(static_cast<void>(row_string(plugin, key, id, 0)), fo4::schema::ScalarError);
  EXPECT_EQ(row_string(plugin, key, texture, 0), "flare_a.dds");
  EXPECT_THROW(fo4::set_record_string(plugin, key, id, 0, "SpriteA"), fo4::schema::ScalarError);
  EXPECT_EQ(fo4::write_plugin(plugin), before);
}

TEST(MaterialImpactSynthetic, InsertedSpriteAcceptsAnIdAndTexture) {
  auto plugin = fo4::read_plugin(make_plugin(), "Patch.esp");
  const auto key = fo4::FormKey::parse("000806:Patch.esp");
  const auto group = sprites_group();
  const auto id = sprite_row("LENS\\SpriteId");
  const auto texture = sprite_row("LENS\\SpriteTexture");

  EXPECT_EQ(fo4::insert_record_subrecord_group(plugin, key, group, 1),
            fo4::SubrecordGroupMutationStatus::updated);
  EXPECT_EQ(fo4::record_field_count(plugin, key, id), std::optional<std::size_t>{3});
  EXPECT_EQ(fo4::record_field_count(plugin, key, texture), std::optional<std::size_t>{3});
  EXPECT_EQ(fo4::set_record_string(plugin, key, id, 1, "SpriteNew"),
            fo4::ScalarMutationStatus::updated);
  EXPECT_EQ(fo4::set_record_string(plugin, key, texture, 1, "flare_new.dds"),
            fo4::ScalarMutationStatus::updated);

  const auto reread = fo4::read_plugin(fo4::write_plugin(plugin), "Patch.esp");
  const auto* record = fo4::find_record(reread, key);
  ASSERT_NE(record, nullptr);
  EXPECT_EQ(subrecord_at(*record, "LFSP", 0), u32_bytes(3));
  EXPECT_EQ(subrecord_at(*record, "DNAM", 2), zstr("SpriteNew"));
  EXPECT_EQ(subrecord_at(*record, "FNAM", 1), zstr("flare_new.dds"));
  EXPECT_EQ(subrecord_at(*record, "DNAM", 1), zstr("SpriteA"));
  EXPECT_EQ(subrecord_at(*record, "DNAM", 3), zstr("SpriteB"));
  EXPECT_EQ(subrecord_at(*record, "FNAM", 2), zstr("flare_b.dds"));
  EXPECT_EQ(subrecord_at(*record, "LFSD", 1), Bytes(36));
  EXPECT_EQ(row_string(reread, key, id, 1), "SpriteNew");
  EXPECT_EQ(row_string(reread, key, texture, 1), "flare_new.dds");
}
