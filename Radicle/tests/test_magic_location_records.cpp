#include "fo4/plugin/mutation.hpp"
#include "fo4/plugin/reader.hpp"
#include "fo4/plugin/writer.hpp"
#include "fo4/schema/formlink_lists.hpp"
#include "fo4/schema/preflight.hpp"
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
  std::string_view link_path;
  std::string_view link_subrecord;
  std::size_t link_offset;
  std::string_view target;
  std::uint32_t target_raw;
};

constexpr std::uint32_t raw_id(std::uint32_t local_id) { return 0x02000000u | local_id; }

constexpr Item k_items[] = {
    {"MGEF", "000901:Patch.esp", "MGEF\\BaseCost", "2.5", "MGEF\\PerkToApply", "DATA", 136,
     "000911:Patch.esp", raw_id(0x911)},
    {"SPEL", "000902:Patch.esp", "SPEL\\ChargeTime", "1.5", "SPEL\\EquipmentType", "ETYP", 0,
     "000921:Patch.esp", raw_id(0x921)},
    {"LCTN", "000903:Patch.esp", "LCTN\\WorldLocationRadius", "120.5", "LCTN\\ParentLocation", "PNAM", 0,
     "000931:Patch.esp", raw_id(0x931)},
};

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

void put(Bytes& data, std::size_t offset, const Bytes& value) {
  std::copy(value.begin(), value.end(), data.begin() + static_cast<std::ptrdiff_t>(offset));
}

Bytes mgef_data() {
  Bytes data = zeros(152);
  put(data, 4, f32_bytes(1.0f));
  put(data, 136, u32_bytes(raw_id(0x910)));
  return data;
}

Bytes spel_spit() {
  ByteWriter out;
  out.u32(100);
  out.u32(0x20000u);
  out.u32(0);
  out.f32(1.0f);
  out.u32(0);
  out.u32(0);
  out.f32(2.0f);
  out.f32(3.0f);
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

  write_group(out, "PERK", {{0x910, {{"EDID", zstr("RadiclePerkA")}}},
                            {0x911, {{"EDID", zstr("RadiclePerkB")}}}});
  write_group(out, "EQUP", {{0x920, {{"EDID", zstr("RadicleEquipA")}}},
                            {0x921, {{"EDID", zstr("RadicleEquipB")}}}});
  write_group(out, "MGEF", {{0x901, {{"EDID", zstr("RadicleEffect")},
                                     {"DATA", mgef_data()},
                                     {"DNAM", zstr("An effect.")}}}});
  write_group(out, "SPEL", {{0x902, {{"EDID", zstr("RadicleSpell")},
                                     {"OBND", zeros(12)},
                                     {"ETYP", u32_bytes(raw_id(0x920))},
                                     {"DESC", zstr("A spell.")},
                                     {"SPIT", spel_spit()}}}});
  write_group(out, "LCTN", {{0x903, {{"EDID", zstr("RadicleLocation")},
                                     {"PNAM", u32_bytes(raw_id(0x930))},
                                     {"RNAM", f32_bytes(50.0f)},
                                     {"ANAM", f32_bytes(1.0f)}}},
                            {0x930, {{"EDID", zstr("RadicleParentA")}}},
                            {0x931, {{"EDID", zstr("RadicleParentB")}}}});
  return out.bytes;
}

const fo4::Subrecord* find_subrecord(const fo4::Record& record, fo4::Signature signature) {
  for (const auto& sub : record.subrecords) {
    if (sub.signature == signature) return &sub;
  }
  return nullptr;
}

}  // namespace

TEST(MagicLocationSynthetic, EveryScalarRowDecodesOnTheFixture) {
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

TEST(MagicLocationSynthetic, FlagsRowNamesComeFromXEdit) {
  const auto field = fo4::schema::scalar_field_descriptor(fo4::Signature::from_string("SPEL"),
                                                          "SPEL\\Flags");
  ASSERT_TRUE(field.has_value());
  const auto name = fo4::schema::scalar_value_name(field->scalar, 0x20000);
  ASSERT_TRUE(name.has_value());
  EXPECT_EQ(*name, "PC Start Spell");
}

TEST(MagicLocationSynthetic, EditsOneRowPerRecordAndRereads) {
  auto plugin = fo4::read_plugin(make_plugin(), "Patch.esp");
  for (const auto& item : k_items) {
    const auto signature = fo4::Signature::from_string(item.signature);
    const auto record = fo4::FormKey::parse(item.key);
    const auto scalar = fo4::schema::scalar_field_descriptor(signature, item.scalar_path);
    ASSERT_TRUE(scalar.has_value()) << item.scalar_path;
    EXPECT_EQ(fo4::set_record_scalar(plugin, record, *scalar, item.scalar_value),
              fo4::ScalarMutationStatus::updated)
        << item.scalar_path;
    const auto link = fo4::schema::formlink_field_descriptor(signature, item.link_path);
    ASSERT_TRUE(link.has_value()) << item.link_path;
    EXPECT_EQ(fo4::set_record_formlink_field(plugin, record, *link, fo4::FormKey::parse(item.target)),
              fo4::FormLinkFieldMutationStatus::updated)
        << item.link_path;
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

    const auto* link_sub = find_subrecord(*record, fo4::Signature::from_string(item.link_subrecord));
    ASSERT_NE(link_sub, nullptr) << item.link_subrecord;
    const auto offset = static_cast<std::ptrdiff_t>(item.link_offset);
    const Bytes stored(link_sub->data.begin() + offset, link_sub->data.begin() + offset + 4);
    EXPECT_EQ(stored, u32_bytes(item.target_raw)) << item.link_path;
  }
}

TEST(MagicLocationSynthetic, FixtureRecordsPassAuthoringPreflight) {
  const auto plugin = fo4::read_plugin(make_plugin(), "Patch.esp");
  for (const auto& item : k_items) {
    const auto issues =
        fo4::schema::preflight_authoring_record(plugin, fo4::FormKey::parse(item.key));
    EXPECT_TRUE(issues.empty()) << item.signature;
  }
}
