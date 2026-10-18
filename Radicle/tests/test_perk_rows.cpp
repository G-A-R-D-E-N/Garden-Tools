#include "fo4/plugin/mutation.hpp"
#include "fo4/plugin/reader.hpp"
#include "fo4/plugin/subrecord_authoring.hpp"
#include "fo4/plugin/writer.hpp"
#include "fo4/schema/formlink_lists.hpp"
#include "fo4/schema/preflight.hpp"
#include "fo4/schema/record_constraints.hpp"
#include "fo4/schema/scalar.hpp"
#include "plugin_bytes.hpp"

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>
#include <vector>

namespace {

using Bytes = std::vector<std::byte>;
using fo4::test::ByteWriter;

constexpr auto k_perk = "PERK";
constexpr auto k_key = "000D01:Patch.esp";
constexpr auto k_sound_key = "000D02:Patch.esp";
constexpr auto k_next_key = "000D03:Patch.esp";

constexpr std::uint32_t raw_id(std::uint32_t local_id) { return 0x01000000u | local_id; }

Bytes zstr(std::string_view text) {
  ByteWriter out;
  out.raw(text);
  out.u8(0);
  return out.bytes;
}

Bytes u32_bytes(std::uint32_t value) {
  ByteWriter out;
  out.u32(value);
  return out.bytes;
}

std::uint32_t raw_at(const Bytes& data, std::size_t offset) {
  std::uint32_t value = 0;
  for (std::size_t index = 0; index < 4; ++index) {
    value |= static_cast<std::uint32_t>(std::to_integer<std::uint8_t>(data[offset + index]))
             << (8 * index);
  }
  return value;
}

void append_record(ByteWriter& out, std::string_view signature, std::uint32_t form_id,
                   std::string_view editor_id) {
  const auto record = fo4::test::begin_record(out, signature, 0, form_id);
  fo4::test::write_subrecord(out, "EDID", zstr(editor_id));
  fo4::test::end_record(out, record);
}

Bytes perk_data() {
  return Bytes{std::byte{0x01}, std::byte{0x02}, std::byte{0x04}, std::byte{0x01},
               std::byte{0x00}};
}

Bytes effect_data() {
  return Bytes{std::byte{0x00}, std::byte{0x00}, std::byte{0x00}};
}

Bytes header_data() {
  return Bytes{std::byte{0x00}, std::byte{0x00}, std::byte{0x01}, std::byte{0x01},
               std::byte{0x00}};
}

Bytes make_plugin(Bytes data, bool with_data = true) {
  ByteWriter out;
  const auto tes4 = fo4::test::begin_record(out, "TES4", 0, 0);
  ByteWriter hedr;
  hedr.f32(1.0f);
  hedr.u32(4);
  hedr.u32(0x900);
  fo4::test::write_subrecord(out, "HEDR", hedr.bytes);
  fo4::test::write_subrecord(out, "MAST", std::string_view{"Fallout4.esm\0", 13});
  fo4::test::write_subrecord(out, "DATA", Bytes(8));
  fo4::test::end_record(out, tes4);

  append_record(out, "SNDR", raw_id(0x0D02), "RadicleSound");
  append_record(out, "PERK", raw_id(0x0D03), "RadicleNextPerk");

  const auto group = fo4::test::begin_group(out, "PERK", 0);
  const auto record = fo4::test::begin_record(out, "PERK", 0, raw_id(0x0D01));
  fo4::test::write_subrecord(out, "EDID", zstr("RadiclePerk"));
  fo4::test::write_subrecord(out, "ICON", zstr("Interface/perk.swf"));
  if (with_data) {
    fo4::test::write_subrecord(out, "DATA", data);
  }
  fo4::test::write_subrecord(out, "SNAM", u32_bytes(0x00000A20));
  fo4::test::write_subrecord(out, "NNAM", u32_bytes(0x00000A21));
  fo4::test::write_subrecord(out, "FNAM", zstr("PerkMenu"));
  if (with_data) {
    fo4::test::write_subrecord(out, "DATA", effect_data());
  }
  fo4::test::end_record(out, record);
  fo4::test::end_group(out, group);
  return out.bytes;
}

Bytes sub_bytes(const fo4::Plugin& plugin, std::string_view subrecord) {
  const auto* record = fo4::find_record(plugin, fo4::FormKey::parse(k_key));
  if (record == nullptr) {
    ADD_FAILURE() << k_key;
    return {};
  }
  for (const auto& sub : record->subrecords) {
    if (sub.signature.to_string() == subrecord) return sub.data;
  }
  ADD_FAILURE() << subrecord;
  return {};
}

std::optional<fo4::schema::ScalarFieldDescriptor> scalar_field(std::string_view path) {
  return fo4::schema::scalar_field_descriptor(fo4::Signature::from_string(k_perk), path);
}

std::optional<fo4::schema::FormLinkFieldDescriptor> formlink_field(std::string_view path) {
  return fo4::schema::formlink_field_descriptor(fo4::Signature::from_string(k_perk), path);
}

}  // namespace

TEST(PerkRows, HeaderDataStructHasNoScalarRows) {
  EXPECT_FALSE(scalar_field("PERK\\Trait").has_value());
  EXPECT_FALSE(scalar_field("PERK\\Level").has_value());
  EXPECT_FALSE(scalar_field("PERK\\NumRanks").has_value());
  EXPECT_FALSE(scalar_field("PERK\\Playable").has_value());
  EXPECT_FALSE(scalar_field("PERK\\Hidden").has_value());
}

TEST(PerkRows, HeaderStringsAndLinksRoundTrip) {
  auto plugin = fo4::read_plugin(make_plugin(perk_data()), "Patch.esp");
  const auto key = fo4::FormKey::parse(k_key);
  const auto image = scalar_field("PERK\\Image");
  const auto swf = scalar_field("PERK\\SWF");
  const auto sound = formlink_field("PERK\\Sound");
  const auto next = formlink_field("PERK\\NextPerk");
  ASSERT_TRUE(image.has_value());
  ASSERT_TRUE(swf.has_value());
  ASSERT_TRUE(sound.has_value());
  ASSERT_TRUE(next.has_value());
  EXPECT_EQ(image->scalar.subrecord, fo4::Signature::from_string("ICON"));
  EXPECT_EQ(swf->scalar.subrecord, fo4::Signature::from_string("FNAM"));
  EXPECT_EQ(image->scalar.storage, fo4::schema::ScalarStorage::zstring);
  EXPECT_EQ(sound->subrecord, fo4::Signature::from_string("SNAM"));
  EXPECT_EQ(next->subrecord, fo4::Signature::from_string("NNAM"));
  EXPECT_FALSE(sound->nullable);
  EXPECT_TRUE(next->nullable);
  EXPECT_TRUE(fo4::schema::formlink_field_target_allowed(
      *next, fo4::Signature::from_string("PERK")));
  EXPECT_FALSE(fo4::schema::formlink_field_target_allowed(
      *sound, fo4::Signature::from_string("PERK")));

  EXPECT_EQ(fo4::set_record_formlink_field(plugin, key, *sound, 0,
                                           fo4::FormKey::parse(k_sound_key)),
            fo4::FormLinkFieldMutationStatus::updated);
  EXPECT_THROW(fo4::set_record_formlink_field(plugin, key, *sound, 0,
                                              fo4::FormKey::parse(k_next_key)),
               fo4::schema::FormLinkFieldError);
  EXPECT_EQ(fo4::set_record_formlink_field(plugin, key, *next, 0,
                                           fo4::FormKey::parse(k_next_key)),
            fo4::FormLinkFieldMutationStatus::updated);

  const auto reread = fo4::read_plugin(fo4::write_plugin(plugin), "Patch.esp");
  EXPECT_EQ(raw_at(sub_bytes(reread, "SNAM"), 0), 0x01000D02u);
  EXPECT_EQ(raw_at(sub_bytes(reread, "NNAM"), 0), 0x01000D03u);
  EXPECT_EQ(std::string(reinterpret_cast<const char*>(sub_bytes(reread, "ICON").data())),
            "Interface/perk.swf");
  EXPECT_EQ(std::string(reinterpret_cast<const char*>(sub_bytes(reread, "FNAM").data())),
            "PerkMenu");
}

TEST(PerkRows, AuthoringListsTheHeaderRulesAndKeepsTheRequiredData) {
  const auto constraint =
      fo4::schema::record_authoring_constraint(fo4::Signature::from_string(k_perk));
  ASSERT_TRUE(constraint.has_value());
  EXPECT_EQ(constraint->placement, fo4::schema::RecordPlacement::top_level);
  constexpr std::string_view k_order[] = {"EDID", "VMAD", "FULL", "DESC", "ICON", "DATA"};
  const auto rules = fo4::schema::authoring_subrecords(*constraint);
  ASSERT_EQ(rules.size(), 6u);
  for (std::size_t index = 0; index < rules.size(); ++index) {
    SCOPED_TRACE(index);
    EXPECT_EQ(rules[index].signature, fo4::Signature::from_string(k_order[index]));
  }
  EXPECT_EQ(rules[5].minimum, 1);
  EXPECT_EQ(rules[5].maximum, 65535);
  EXPECT_TRUE(rules[5].uncounted);
  EXPECT_TRUE(rules[5].has_default());
  EXPECT_EQ(rules[5].default_size, 5);

  auto plugin = fo4::read_plugin(make_plugin(perk_data()), "Patch.esp");
  const auto created =
      fo4::create_record(plugin, fo4::Signature::from_string(k_perk), "RadicleNewPerk");
  EXPECT_EQ(created.status, fo4::RecordCreateStatus::created);
  const auto* record = fo4::find_record(plugin, created.key);
  ASSERT_NE(record, nullptr);
  ASSERT_EQ(record->subrecords.size(), 2u);
  EXPECT_EQ(record->subrecords[0].signature, fo4::Signature::from_string("EDID"));
  EXPECT_EQ(record->subrecords[1].signature, fo4::Signature::from_string("DATA"));
  EXPECT_EQ(record->subrecords[1].data, header_data());
}

TEST(PerkRows, PreflightAcceptsAnEffectDataSubrecordAfterSwf) {
  const auto plugin = fo4::read_plugin(make_plugin(perk_data()), "Patch.esp");
  const auto issues =
      fo4::schema::preflight_authoring_record(plugin, fo4::FormKey::parse(k_key));
  for (const auto& issue : issues) {
    ADD_FAILURE() << issue.path << ' ' << issue.code;
  }
}

TEST(PerkRows, PreflightRefusesAPerkWithoutAnyDataSubrecord) {
  const auto plugin = fo4::read_plugin(make_plugin(perk_data(), false), "Patch.esp");
  const auto issues =
      fo4::schema::preflight_authoring_record(plugin, fo4::FormKey::parse(k_key));
  ASSERT_EQ(issues.size(), 1u);
  EXPECT_EQ(issues[0].code, "required_subrecord_missing");
  EXPECT_EQ(issues[0].path, "PERK\\DATA");
}
