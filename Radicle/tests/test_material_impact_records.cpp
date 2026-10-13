#include "fo4/plugin/mutation.hpp"
#include "fo4/plugin/reader.hpp"
#include "fo4/plugin/writer.hpp"
#include "fo4/schema/form_links.hpp"
#include "fo4/schema/formlink_lists.hpp"
#include "fo4/schema/record_constraints.hpp"
#include "fo4/schema/scalar.hpp"
#include "plugin_bytes.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
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
                                     {"CNAM", f32_bytes(0.5f)}}}});
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

}  // namespace

TEST(MaterialImpactSynthetic, EveryScalarRowDecodesOnTheFixture) {
  const auto plugin = fo4::read_plugin(make_plugin(), "Patch.esp");
  for (const auto& item : k_items) {
    const auto signature = fo4::Signature::from_string(item.signature);
    const auto* record = fo4::find_record(plugin, fo4::FormKey::parse(item.key));
    ASSERT_NE(record, nullptr) << item.signature;
    std::size_t decoded = 0;
    for (const auto& field : fo4::schema::scalar_fields(signature)) {
      const auto* sub = find_subrecord(*record, field.scalar.subrecord);
      if (sub == nullptr) continue;
      const auto bytes = fo4::schema::scalar_field_bytes(sub->data, field.scalar);
      if (bytes.empty()) continue;
      EXPECT_NO_THROW(static_cast<void>(fo4::schema::decode_scalar(bytes, field.scalar)))
          << field.path;
      ++decoded;
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
  const auto hits = fo4::collect_form_links(reread);
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
      const auto target = fo4::FormKey::parse(item.link_target);
      const bool found = std::any_of(hits.begin(), hits.end(), [&](const fo4::FormLinkHit& hit) {
        return hit.record == record_key && hit.subrecord == link->subrecord && hit.key == target;
      });
      EXPECT_TRUE(found) << item.link_path;
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
