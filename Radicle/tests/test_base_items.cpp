#include "fo4/plugin/mutation.hpp"
#include "fo4/plugin/reader.hpp"
#include "fo4/plugin/writer.hpp"
#include "fo4/schema/form_links.hpp"
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
  std::string_view text_path;
  std::string_view link_path;
};

constexpr Item k_items[] = {
    {"ALCH", "000801:Patch.esp", "ALCH\\Value", "7", "ALCH\\Model", "ALCH\\PickUpSound"},
    {"AMMO", "000802:Patch.esp", "AMMO\\Damage", "2.5", "AMMO\\Model", "AMMO\\PickUpSound"},
    {"BOOK", "000803:Patch.esp", "BOOK\\TextOffsetY", "9", "BOOK\\Model", "BOOK\\PickUpSound"},
    {"INGR", "000804:Patch.esp", "INGR\\Weight", "0.5", "INGR\\Model", "INGR\\PickUpSound"},
    {"KEYM", "000805:Patch.esp", "KEYM\\Value", "4","KEYM\\Model", "KEYM\\PickUpSound"},
    {"ARTO", "000806:Patch.esp", "ARTO\\ArtType", "1", "ARTO\\Model", ""},
    {"IDLM", "000807:Patch.esp", "IDLM\\IdleTimer", "2.5", "IDLM\\Model", ""},
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

Bytes alch_enit() {
  ByteWriter out;
  out.i32(5);
  out.u32(0x10001u);
  out.u32(0);
  out.f32(0.5f);
  out.u32(0);
  return out.bytes;
}

Bytes ammo_dnam() {
  ByteWriter out;
  out.u32(0);
  out.u8(2);
  out.u8(0);
  out.u8(0);
  out.u8(0);
  out.f32(4.0f);
  out.u32(0);
  return out.bytes;
}

Bytes book_dnam() {
  ByteWriter out;
  out.u8(0x04);
  out.u32(0);
  out.u32(0);
  out.u32(0);
  return out.bytes;
}

Bytes words(std::uint32_t first, std::uint32_t second) {
  ByteWriter out;
  out.u32(first);
  out.u32(second);
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

  write_group(out, "SNDR", {{0x800, {{"EDID", zstr("RadicleSoundA")}}},
                            {0x808, {{"EDID", zstr("RadicleSoundB")}}}});
  write_group(out, "ALCH", {{0x801, {{"EDID", zstr("RadicleAlch")},
                                     {"OBND", zeros(12)},
                                     {"MODL", zstr("Meat.nif")},
                                     {"YNAM", u32_bytes(raw_id(0x800))},
                                     {"DATA", f32_bytes(1.5f)},
                                     {"ENIT", alch_enit()}}}});
  write_group(out, "AMMO", {{0x802, {{"EDID", zstr("RadicleAmmo")},
                                     {"OBND", zeros(12)},
                                     {"MODL", zstr("Round.nif")},
                                     {"YNAM", u32_bytes(raw_id(0x800))},
                                     {"DATA", words(10, 0x3C23D70Au)},
                                     {"DNAM", ammo_dnam()}}}});
  write_group(out, "BOOK", {{0x803, {{"EDID", zstr("RadicleBook")},
                                     {"OBND", zeros(12)},
                                     {"MODL", zstr("Tome.nif")},
                                     {"DESC", zstr("A tome.")},
                                     {"YNAM", u32_bytes(raw_id(0x800))},
                                     {"DATA", words(25, 0x3F800000u)},
                                     {"DNAM", book_dnam()}}}});
  write_group(out, "INGR", {{0x804, {{"EDID", zstr("RadicleIngr")},
                                     {"OBND", zeros(12)},
                                     {"MODL", zstr("Herb.nif")},
                                     {"YNAM", u32_bytes(raw_id(0x800))},
                                     {"DATA", words(4, 0x3E4CCCCDu)},
                                     {"ENIT", words(4, 0x1u)}}}});
  write_group(out, "KEYM", {{0x805, {{"EDID", zstr("RadicleKey")},
                                     {"OBND", zeros(12)},
                                     {"FULL", zstr("Key")},
                                     {"MODL", zstr("Key.nif")},
                                     {"YNAM", u32_bytes(raw_id(0x800))},
                                     {"DATA", words(3, 0)}}}});
  write_group(out, "ARTO", {{0x806, {{"EDID", zstr("RadicleArto")},
                                     {"OBND", zeros(12)},
                                     {"MODL", zstr("Art.nif")},
                                     {"DNAM", u32_bytes(0)}}}});
  ByteWriter idlf;
  idlf.u8(1);
  ByteWriter idlc;
  idlc.u8(0);
  write_group(out, "IDLM", {{0x807, {{"EDID", zstr("RadicleIdle")},
                                     {"OBND", zeros(12)},
                                     {"IDLF", idlf.bytes},
                                     {"IDLC", idlc.bytes},
                                     {"IDLT", f32_bytes(1.0f)},
                                     {"MODL", zstr("Idle.nif")}}}});
  return out.bytes;
}

const fo4::Subrecord* find_subrecord(const fo4::Record& record, fo4::Signature signature) {
  for (const auto& sub : record.subrecords) {
    if (sub.signature == signature) return &sub;
  }
  return nullptr;
}

}  // namespace

TEST(BaseItemsSynthetic, EveryScalarRowDecodesOnTheFixture) {
  auto plugin = fo4::read_plugin(make_plugin(), "Patch.esp");
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

TEST(BaseItemsSynthetic, FlagsRowNamesComeFromXEdit) {
  const auto field = fo4::schema::scalar_field_descriptor(fo4::Signature::from_string("ALCH"),
                                                          "ALCH\\Flags");
  ASSERT_TRUE(field.has_value());
  const auto name = fo4::schema::scalar_value_name(field->scalar, 0x10000);
  ASSERT_TRUE(name.has_value());
  EXPECT_EQ(*name, "Medicine");
}

TEST(BaseItemsSynthetic, EditsOneRowPerRecordAndRereads) {
  auto plugin = fo4::read_plugin(make_plugin(), "Patch.esp");
  for (const auto& item : k_items) {
    const auto signature = fo4::Signature::from_string(item.signature);
    const auto record = fo4::FormKey::parse(item.key);
    const auto scalar = fo4::schema::scalar_field_descriptor(signature, item.scalar_path);
    ASSERT_TRUE(scalar.has_value()) << item.scalar_path;
    EXPECT_EQ(fo4::set_record_scalar(plugin, record, *scalar, item.scalar_value),
              fo4::ScalarMutationStatus::updated)
        << item.scalar_path;
    EXPECT_EQ(fo4::set_record_string(plugin, record, item.text_path, 0, "Edited.nif"),
              fo4::ScalarMutationStatus::updated)
        << item.text_path;
    if (!item.link_path.empty()) {
      const auto link = fo4::schema::formlink_field_descriptor(signature, item.link_path);
      ASSERT_TRUE(link.has_value()) << item.link_path;
      EXPECT_EQ(fo4::set_record_formlink_field(plugin, record, *link,
                                               fo4::FormKey::parse("000808:Patch.esp")),
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

    const auto text = fo4::schema::scalar_field_descriptor(signature, item.text_path);
    ASSERT_TRUE(text.has_value());
    const auto* text_sub = find_subrecord(*record, text->scalar.subrecord);
    ASSERT_NE(text_sub, nullptr);
    EXPECT_EQ(text_sub->data, zstr("Edited.nif")) << item.text_path;

    if (!item.link_path.empty()) {
      const auto link = fo4::schema::formlink_field_descriptor(signature, item.link_path);
      ASSERT_TRUE(link.has_value());
      const auto target = fo4::FormKey::parse("000808:Patch.esp");
      const bool found = std::any_of(hits.begin(), hits.end(), [&](const fo4::FormLinkHit& hit) {
        return hit.record == record_key && hit.subrecord == link->subrecord && hit.key == target;
      });
      EXPECT_TRUE(found) << item.link_path;
    }
  }
}

TEST(BaseItemsSynthetic, FixtureRecordsMeetTheirAuthoringMinimums) {
  auto plugin = fo4::read_plugin(make_plugin(), "Patch.esp");
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

TEST(BaseItemsSynthetic, FixtureRecordsPassAuthoringPreflight) {
  auto plugin = fo4::read_plugin(make_plugin(), "Patch.esp");
  for (const auto& item : k_items) {
    const auto issues =
        fo4::schema::preflight_authoring_record(plugin, fo4::FormKey::parse(item.key));
    EXPECT_TRUE(issues.empty()) << item.signature;
  }
}
