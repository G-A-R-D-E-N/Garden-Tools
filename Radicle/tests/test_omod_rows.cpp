#include "fo4/plugin/mutation.hpp"
#include "fo4/plugin/reader.hpp"
#include "fo4/plugin/subrecord_authoring.hpp"
#include "fo4/plugin/writer.hpp"
#include "fo4/schema/form_links.hpp"
#include "fo4/schema/field_address.hpp"
#include "fo4/schema/formlink_lists.hpp"
#include "fo4/schema/record_constraints.hpp"
#include "fo4/schema/scalar.hpp"
#include "plugin_bytes.hpp"
#include "byte_helpers.hpp"

#include <algorithm>
#include <gtest/gtest.h>

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <initializer_list>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

using Bytes = std::vector<std::byte>;
using fo4::test::ByteWriter;

constexpr auto k_omod = "OMOD";
constexpr auto k_key = "000D01:Patch.esp";
constexpr auto k_keyword_key = "000D02:Patch.esp";
constexpr auto k_static_key = "000D03:Patch.esp";

constexpr std::uint32_t raw_id(std::uint32_t local_id) { return 0x01000000u | local_id; }

constexpr std::size_t k_data_size = 47;

using fo4::test::zstr;

using fo4::test::u32_bytes;

Bytes words(std::initializer_list<std::uint32_t> values) {
  ByteWriter out;
  for (const auto value : values) out.u32(value);
  return out.bytes;
}

Bytes omod_data() {
  ByteWriter out;
  out.u32(1);
  out.u32(0);
  out.u8(1);
  out.u8(0);
  out.u32(1330467393u);
  out.u8(3);
  out.u8(2);
  out.u32(0x00000A10);
  out.u32(1);
  out.u32(0x00000A11);
  out.u32(1);
  out.u32(0x11223344);
  out.u32(0x55667788);
  out.u32(0x00000A12);
  out.u8(0);
  out.u8(1);
  out.u8(0);
  return out.bytes;
}

Bytes short_omod_data() {
  ByteWriter out;
  out.u32(0);
  out.u32(0);
  out.u8(0);
  out.u8(0);
  out.u32(0);
  out.u8(0);
  out.u8(0);
  return out.bytes;
}

void append_record(ByteWriter& out, std::string_view signature, std::uint32_t form_id,
                   std::string_view editor_id) {
  const auto group = fo4::test::begin_group(out, signature, 0);
  const auto record = fo4::test::begin_record(out, signature, 0, form_id);
  fo4::test::write_subrecord(out, "EDID", zstr(editor_id));
  fo4::test::end_record(out, record);
  fo4::test::end_group(out, group);
}

Bytes make_plugin(Bytes data) {
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

  append_record(out, "KYWD", raw_id(0x0D02), "RadicleKeyword");
  append_record(out, "STAT", raw_id(0x0D03), "RadicleStatic");

  const auto group = fo4::test::begin_group(out, "OMOD", 0);
  const auto record = fo4::test::begin_record(out, "OMOD", 0, raw_id(0x0D01));
  fo4::test::write_subrecord(out, "EDID", zstr("RadicleOmod"));
  fo4::test::write_subrecord(out, "DATA", data);
  fo4::test::write_subrecord(out, "MNAM", words({0x00000A20}));
  fo4::test::write_subrecord(out, "FNAM", words({0x00000A21}));
  fo4::test::write_subrecord(out, "LNAM", u32_bytes(0x00000A22));
  fo4::test::write_subrecord(out, "NAM1", Bytes{std::byte{7}});
  fo4::test::write_subrecord(out, "FLTR", zstr("FilterText"));
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

Bytes scalar_bytes(const fo4::Plugin& plugin, std::string_view path) {
  const auto field = fo4::schema::scalar_field_descriptor(fo4::Signature::from_string(k_omod), path);
  if (!field.has_value()) {
    ADD_FAILURE() << path;
    return {};
  }
  const auto data = sub_bytes(plugin, field->scalar.subrecord.to_string());
  const auto bytes = fo4::schema::scalar_field_bytes(data, field->scalar);
  return Bytes(bytes.begin(), bytes.end());
}

std::optional<fo4::schema::ScalarFieldDescriptor> scalar_field(std::string_view path) {
  return fo4::schema::scalar_field_descriptor(fo4::Signature::from_string(k_omod), path);
}

std::uint32_t raw_at(const Bytes& data, std::size_t offset) {
  std::uint32_t value = 0;
  if (offset + 4 > data.size()) {
    ADD_FAILURE() << offset;
    return 0;
  }
  std::memcpy(&value, data.data() + offset, sizeof(value));
  return value;
}

std::uint32_t link_raw(const fo4::Plugin& plugin, std::string_view subrecord) {
  const auto record = fo4::FormKey::parse(k_key);
  for (const auto& hit : fo4::collect_form_links(plugin)) {
    if (hit.record == record && hit.subrecord.to_string() == subrecord) return hit.raw.value();
  }
  ADD_FAILURE() << subrecord;
  return 0;
}

struct ScalarRow {
  std::string_view path;
  std::string_view subrecord;
  std::size_t offset;
  fo4::schema::ScalarStorage storage;
  std::size_t minimum_payload_size;
};

constexpr auto u8 = fo4::schema::ScalarStorage::u8;
constexpr auto u32 = fo4::schema::ScalarStorage::u32;

constexpr ScalarRow k_scalar_rows[] = {
    {"OMOD\\IncludeCount", "DATA", 0, u32, 20},
    {"OMOD\\PropertyCount", "DATA", 4, u32, 20},
    {"OMOD\\UnknownBool1", "DATA", 8, u8, 20},
    {"OMOD\\UnknownBool2", "DATA", 9, u8, 20},
    {"OMOD\\FormType", "DATA", 10, u32, 20},
    {"OMOD\\MaxRank", "DATA", 14, u8, 20},
    {"OMOD\\LevelTierScaledOffset", "DATA", 15, u8, 20},
};

}  // namespace

TEST(OmodRows, ScalarRowsNameTheirOffsetsStorageAndMinimumPayload) {
  for (const auto& row : k_scalar_rows) {
    SCOPED_TRACE(row.path);
    const auto field = scalar_field(row.path);
    ASSERT_TRUE(field.has_value());
    EXPECT_EQ(field->record, fo4::Signature::from_string(k_omod));
    EXPECT_EQ(field->scalar.subrecord, fo4::Signature::from_string(row.subrecord));
    EXPECT_EQ(field->scalar.offset, row.offset);
    EXPECT_EQ(field->scalar.storage, row.storage);
    EXPECT_EQ(field->scalar.minimum_payload_size, row.minimum_payload_size);
    EXPECT_EQ(field->scalar.payload_size, 0u);
    EXPECT_TRUE(field->scalar.embedded);
  }
  const auto form_type = scalar_field("OMOD\\FormType");
  ASSERT_TRUE(form_type.has_value());
  const auto armor = fo4::schema::scalar_value_name(form_type->scalar, 1330467393);
  const auto npc = fo4::schema::scalar_value_name(form_type->scalar, 1598246990);
  const auto weapon = fo4::schema::scalar_value_name(form_type->scalar, 1346454871);
  const auto none = fo4::schema::scalar_value_name(form_type->scalar, 1162760014);
  ASSERT_TRUE(armor.has_value());
  ASSERT_TRUE(npc.has_value());
  ASSERT_TRUE(weapon.has_value());
  ASSERT_TRUE(none.has_value());
  EXPECT_EQ(*armor, "Armor");
  EXPECT_EQ(*npc, "Non-player character");
  EXPECT_EQ(*weapon, "Weapon");
  EXPECT_EQ(*none, "None");
  EXPECT_FALSE(fo4::schema::scalar_value_name(form_type->scalar, 0).has_value());
}

TEST(OmodRows, DataScalarsReadAndWriteAtTheirOffsets) {
  auto plugin = fo4::read_plugin(make_plugin(omod_data()), "Patch.esp");
  const auto key = fo4::FormKey::parse(k_key);
  EXPECT_EQ(scalar_bytes(plugin, "OMOD\\IncludeCount"), u32_bytes(1));
  EXPECT_EQ(scalar_bytes(plugin, "OMOD\\PropertyCount"), u32_bytes(0));
  EXPECT_EQ(scalar_bytes(plugin, "OMOD\\UnknownBool1"), Bytes{std::byte{1}});
  EXPECT_EQ(scalar_bytes(plugin, "OMOD\\UnknownBool2"), Bytes{std::byte{0}});
  EXPECT_EQ(scalar_bytes(plugin, "OMOD\\FormType"), u32_bytes(1330467393u));
  EXPECT_EQ(scalar_bytes(plugin, "OMOD\\MaxRank"), Bytes{std::byte{3}});
  EXPECT_EQ(scalar_bytes(plugin, "OMOD\\LevelTierScaledOffset"), Bytes{std::byte{2}});

  const auto rank = scalar_field("OMOD\\MaxRank");
  const auto type = scalar_field("OMOD\\FormType");
  const auto tier = scalar_field("OMOD\\LevelTierScaledOffset");
  ASSERT_TRUE(rank.has_value());
  ASSERT_TRUE(type.has_value());
  ASSERT_TRUE(tier.has_value());
  EXPECT_THROW(fo4::set_record_scalar(plugin, key, *rank, 0, "999"), fo4::schema::ScalarError);
  EXPECT_EQ(fo4::set_record_scalar(plugin, key, *rank, 0, "9"),
            fo4::ScalarMutationStatus::updated);
  const auto weapon_value = fo4::schema::scalar_name_value(type->scalar, "Weapon");
  ASSERT_TRUE(weapon_value.has_value());
  const auto weapon = std::to_string(*weapon_value);
  EXPECT_EQ(fo4::set_record_scalar(plugin, key, *type, 0, weapon),
            fo4::ScalarMutationStatus::updated);
  EXPECT_EQ(fo4::set_record_scalar(plugin, key, *tier, 0, "2"),
            fo4::ScalarMutationStatus::unchanged);

  const auto reread = fo4::read_plugin(fo4::write_plugin(plugin), "Patch.esp");
  const auto data = sub_bytes(reread, "DATA");
  ASSERT_EQ(data.size(), k_data_size);
  EXPECT_EQ(data[0], std::byte{1});
  EXPECT_EQ(raw_at(data, 10), 1346454871u);
  EXPECT_EQ(data[14], std::byte{9});
  EXPECT_EQ(data[15], std::byte{2});
  EXPECT_EQ(raw_at(data, 16), 0x00000A10u);
  EXPECT_EQ(raw_at(data, 24), 0x00000A11u);
  EXPECT_EQ(raw_at(data, 40), 0x00000A12u);
}

TEST(OmodRows, IncludeAndPropertyCountRowsRefuseWrites) {
  auto plugin = fo4::read_plugin(make_plugin(omod_data()), "Patch.esp");
  const auto key = fo4::FormKey::parse(k_key);
  const auto before = fo4::test::plugin_state(plugin);
  for (const auto* path : {"OMOD\\IncludeCount", "OMOD\\PropertyCount"}) {
    const auto row = scalar_field(path);
    ASSERT_TRUE(row.has_value()) << path;
    EXPECT_TRUE(fo4::schema::is_object_template_count_row(*row)) << path;
    EXPECT_THROW(fo4::set_record_scalar(plugin, key, *row, 0, "3"), fo4::schema::ScalarError) << path;
  }
  EXPECT_EQ(fo4::test::plugin_state(plugin), before);
  const auto rank = scalar_field("OMOD\\MaxRank");
  ASSERT_TRUE(rank.has_value());
  EXPECT_FALSE(fo4::schema::is_object_template_count_row(*rank));
}

TEST(OmodRows, PriorityAndFilterRowsOwnTheirWholeSubrecord) {
  auto plugin = fo4::read_plugin(make_plugin(omod_data()), "Patch.esp");
  const auto key = fo4::FormKey::parse(k_key);
  const auto priority = scalar_field("OMOD\\Priority");
  const auto filter = scalar_field("OMOD\\Filter");
  ASSERT_TRUE(priority.has_value());
  ASSERT_TRUE(filter.has_value());
  EXPECT_EQ(priority->scalar.subrecord, fo4::Signature::from_string("NAM1"));
  EXPECT_EQ(priority->scalar.storage, fo4::schema::ScalarStorage::u8);
  EXPECT_EQ(priority->scalar.payload_size, 0u);
  EXPECT_FALSE(priority->scalar.embedded);
  EXPECT_EQ(filter->scalar.subrecord, fo4::Signature::from_string("FLTR"));
  EXPECT_EQ(filter->scalar.storage, fo4::schema::ScalarStorage::zstring);
  EXPECT_FALSE(filter->scalar.embedded);
  EXPECT_EQ(scalar_bytes(plugin, "OMOD\\Priority"), Bytes{std::byte{7}});
  EXPECT_EQ(scalar_bytes(plugin, "OMOD\\Filter"), zstr("FilterText"));

  EXPECT_THROW(fo4::set_record_scalar(plugin, key, *priority, 0, "256"),
               fo4::schema::ScalarError);
  EXPECT_THROW(fo4::set_record_scalar(plugin, key, *filter, 0, "Other"),
               fo4::schema::ScalarError);
  EXPECT_EQ(fo4::set_record_scalar(plugin, key, *priority, 1, "1"),
            fo4::ScalarMutationStatus::subrecord_missing);
  EXPECT_EQ(fo4::set_record_scalar(plugin, key, *priority, 0, "9"),
            fo4::ScalarMutationStatus::updated);
  EXPECT_EQ(fo4::set_record_scalar(plugin, key, *priority, 0, "9"),
            fo4::ScalarMutationStatus::unchanged);
  EXPECT_EQ(fo4::set_record_string(plugin, key, *filter, 0, "FilterText"),
            fo4::ScalarMutationStatus::unchanged);
  EXPECT_EQ(fo4::set_record_string(plugin, key, *filter, 0, "NewFilter"),
            fo4::ScalarMutationStatus::updated);

  const auto reread = fo4::read_plugin(fo4::write_plugin(plugin), "Patch.esp");
  EXPECT_EQ(sub_bytes(reread, "NAM1"), Bytes{std::byte{9}});
  EXPECT_EQ(sub_bytes(reread, "FLTR"), zstr("NewFilter"));
  EXPECT_EQ(sub_bytes(reread, "DATA").size(), k_data_size);
}

TEST(OmodRows, ShortDataPayloadRefusesEveryHeaderScalar) {
  auto plugin = fo4::read_plugin(make_plugin(short_omod_data()), "Patch.esp");
  const auto key = fo4::FormKey::parse(k_key);
  const auto data = sub_bytes(plugin, "DATA");
  ASSERT_EQ(data.size(), 16u);
  for (const auto& row : k_scalar_rows) {
    SCOPED_TRACE(row.path);
    const auto field = scalar_field(row.path);
    ASSERT_TRUE(field.has_value());
    EXPECT_TRUE(fo4::schema::scalar_field_bytes(data, field->scalar).empty());
    if (fo4::schema::is_object_template_count_row(*field)) {
      EXPECT_THROW(fo4::set_record_scalar(plugin, key, *field, 0, "1"), fo4::schema::ScalarError);
      continue;
    }
    EXPECT_EQ(fo4::set_record_scalar(plugin, key, *field, 0, "1"),
              fo4::ScalarMutationStatus::subrecord_missing);
  }
  const auto attach =
      fo4::schema::formlink_field_descriptor(fo4::Signature::from_string(k_omod),
                                             "OMOD\\AttachPoint");
  ASSERT_TRUE(attach.has_value());
  EXPECT_THROW(fo4::set_record_formlink_field(plugin, key, *attach, 0,
                                              fo4::FormKey::parse(k_keyword_key)),
               fo4::schema::FormLinkFieldError);
}

TEST(OmodRows, AttachPointAndLooseModWriteTheirFormIds) {
  auto plugin = fo4::read_plugin(make_plugin(omod_data()), "Patch.esp");
  const auto key = fo4::FormKey::parse(k_key);
  const auto attach =
      fo4::schema::formlink_field_descriptor(fo4::Signature::from_string(k_omod),
                                             "OMOD\\AttachPoint");
  const auto loose =
      fo4::schema::formlink_field_descriptor(fo4::Signature::from_string(k_omod),
                                             "OMOD\\LooseMod");
  ASSERT_TRUE(attach.has_value());
  ASSERT_TRUE(loose.has_value());
  EXPECT_EQ(attach->subrecord, fo4::Signature::from_string("DATA"));
  EXPECT_EQ(attach->offset, 16u);
  EXPECT_TRUE(attach->nullable);
  EXPECT_EQ(attach->payload_sizes[0], 0u);
  EXPECT_EQ(loose->subrecord, fo4::Signature::from_string("LNAM"));
  EXPECT_EQ(loose->offset, 0u);
  EXPECT_TRUE(loose->nullable);
  EXPECT_EQ(loose->payload_sizes[0], 4u);
  EXPECT_TRUE(fo4::schema::formlink_field_target_allowed(*attach,
                                                         fo4::Signature::from_string("KYWD")));
  EXPECT_FALSE(fo4::schema::formlink_field_target_allowed(*attach,
                                                          fo4::Signature::from_string("STAT")));
  EXPECT_TRUE(fo4::schema::formlink_field_target_allowed(*loose,
                                                         fo4::Signature::from_string("STAT")));
  EXPECT_FALSE(fo4::schema::formlink_field_target_allowed(*loose,
                                                          fo4::Signature::from_string("KYWD")));

  EXPECT_EQ(fo4::set_record_formlink_field(plugin, key, *attach, 0,
                                           fo4::FormKey::parse(k_keyword_key)),
            fo4::FormLinkFieldMutationStatus::updated);
  EXPECT_THROW(fo4::set_record_formlink_field(plugin, key, *attach, 0,
                                              fo4::FormKey::parse(k_static_key)),
               fo4::schema::FormLinkFieldError);
  EXPECT_EQ(fo4::set_record_formlink_field(plugin, key, *loose, 0,
                                           fo4::FormKey::parse(k_static_key)),
            fo4::FormLinkFieldMutationStatus::updated);
  EXPECT_THROW(fo4::set_record_formlink_field(plugin, key, *loose, 0,
                                              fo4::FormKey::parse(k_keyword_key)),
               fo4::schema::FormLinkFieldError);

  const auto reread = fo4::read_plugin(fo4::write_plugin(plugin), "Patch.esp");
  const auto data = sub_bytes(reread, "DATA");
  ASSERT_EQ(data.size(), k_data_size);
  EXPECT_EQ(raw_at(data, 16), 0x01000D02u);
  EXPECT_EQ(raw_at(data, 24), 0x00000A11u);
  EXPECT_EQ(sub_bytes(reread, "LNAM"), u32_bytes(0x01000D03u));
  EXPECT_EQ(link_raw(reread, "LNAM"), 0x01000D03u);
  EXPECT_EQ(link_raw(reread, "DATA"), 0x01000D02u);
}

TEST(OmodRows, PackedKeywordListsInsertAndRemoveElements) {
  auto plugin = fo4::read_plugin(make_plugin(omod_data()), "Patch.esp");
  const auto key = fo4::FormKey::parse(k_key);
  const auto mnam = fo4::schema::formlink_list_descriptor(fo4::Signature::from_string(k_omod),
                                                          "OMOD\\TargetOMODKeywords");
  const auto fnam = fo4::schema::formlink_list_descriptor(fo4::Signature::from_string(k_omod),
                                                          "OMOD\\FilterKeywords");
  ASSERT_TRUE(mnam.has_value());
  ASSERT_TRUE(fnam.has_value());
  EXPECT_EQ(mnam->subrecord, fo4::Signature::from_string("MNAM"));
  EXPECT_EQ(fnam->subrecord, fo4::Signature::from_string("FNAM"));
  EXPECT_EQ(mnam->storage, fo4::schema::FormLinkListStorage::packed_array);
  EXPECT_EQ(fnam->storage, fo4::schema::FormLinkListStorage::packed_array);
  EXPECT_TRUE(mnam->can_insert);
  EXPECT_TRUE(fnam->can_insert);
  EXPECT_FALSE(mnam->nullable);
  EXPECT_FALSE(fnam->nullable);
  EXPECT_TRUE(
      fo4::schema::formlink_list_target_allowed(*mnam, fo4::Signature::from_string("KYWD")));
  EXPECT_FALSE(
      fo4::schema::formlink_list_target_allowed(*mnam, fo4::Signature::from_string("STAT")));
  EXPECT_EQ(fo4::schema::record_formlink_list_size(plugin, key, *mnam), 1u);
  EXPECT_EQ(fo4::schema::record_formlink_list_size(plugin, key, *fnam), 1u);

  const auto keyword = fo4::FormKey::parse(k_keyword_key);
  EXPECT_EQ(fo4::schema::insert_record_formlink_list_element(plugin, key, *mnam, 1, keyword),
            fo4::schema::FormLinkListMutationStatus::updated);
  EXPECT_EQ(fo4::schema::insert_record_formlink_list_element(
                plugin, key, *mnam, 2, fo4::FormKey::parse(k_static_key)),
            fo4::schema::FormLinkListMutationStatus::target_type_mismatch);
  EXPECT_EQ(fo4::schema::insert_record_formlink_list_element(plugin, key, *mnam, 3, keyword),
            fo4::schema::FormLinkListMutationStatus::index_out_of_range);
  EXPECT_EQ(fo4::schema::record_formlink_list_size(plugin, key, *mnam), 2u);
  EXPECT_EQ(fo4::schema::remove_record_formlink_list_element(plugin, key, *mnam, 0),
            fo4::schema::FormLinkListMutationStatus::updated);

  const auto reread = fo4::read_plugin(fo4::write_plugin(plugin), "Patch.esp");
  EXPECT_EQ(sub_bytes(reread, "MNAM"), u32_bytes(0x01000D02u));
  EXPECT_EQ(sub_bytes(reread, "FNAM"), u32_bytes(0x00000A21u));
  const auto data = sub_bytes(reread, "DATA");
  ASSERT_EQ(data.size(), k_data_size);
  EXPECT_EQ(raw_at(data, 40), 0x00000A12u);
}

TEST(OmodRows, AuthoringMatchesTheXEditSubrecordOrder) {
  const auto constraint =
      fo4::schema::record_authoring_constraint(fo4::Signature::from_string(k_omod));
  ASSERT_TRUE(constraint.has_value());
  EXPECT_EQ(constraint->placement, fo4::schema::RecordPlacement::top_level);
  constexpr std::string_view k_order[] = {"EDID", "FULL", "DESC", "MODL", "MODT", "MODC",
                                          "MODS", "MODF", "DATA", "MNAM", "FNAM", "LNAM",
                                          "NAM1", "FLTR"};
  const auto rules = fo4::schema::authoring_subrecords(*constraint);
  ASSERT_EQ(rules.size(), 14u);
  for (std::size_t index = 0; index < rules.size(); ++index) {
    SCOPED_TRACE(index);
    EXPECT_EQ(rules[index].signature, fo4::Signature::from_string(k_order[index]));
    EXPECT_EQ(rules[index].minimum, 0);
    EXPECT_EQ(rules[index].maximum, 1);
  }
  EXPECT_TRUE(rules[3].coupled);
  EXPECT_FALSE(rules[3].has_default());

  auto plugin = fo4::read_plugin(make_plugin(omod_data()), "Patch.esp");
  const auto created =
      fo4::create_record(plugin, fo4::Signature::from_string(k_omod), "RadicleNewOmod");
  EXPECT_EQ(created.status, fo4::RecordCreateStatus::created);
  const auto* record = fo4::find_record(plugin, created.key);
  ASSERT_NE(record, nullptr);
  ASSERT_EQ(record->subrecords.size(), 1u);
  EXPECT_EQ(record->subrecords[0].signature, fo4::Signature::from_string("EDID"));
}

namespace {

Bytes property_row(std::uint8_t value_type, std::uint8_t function_type, std::uint16_t property,
                   std::uint32_t value1, std::uint32_t value2, float step) {
  ByteWriter out;
  out.u8(value_type);
  out.u8(0);
  out.u8(0);
  out.u8(0);
  out.u8(function_type);
  out.u8(0);
  out.u8(0);
  out.u8(0);
  out.u16(property);
  out.u16(0);
  out.u32(value1);
  out.u32(value2);
  out.f32(step);
  return out.bytes;
}

Bytes omod_data_with_property(std::uint8_t value_type, std::uint32_t value1) {
  ByteWriter out;
  out.u32(1);
  out.u32(1);
  out.u8(1);
  out.u8(0);
  out.u32(1330467393u);
  out.u8(3);
  out.u8(2);
  out.u32(0x00000A10);
  out.u32(1);
  out.u32(0x00000A11);
  out.u32(1);
  out.u32(0x11223344);
  out.u32(0x55667788);
  out.u32(0x00000A12);
  out.u8(0);
  out.u8(1);
  out.u8(0);
  const auto row = property_row(value_type, 1, 0x0142, value1, 0x40000000, 0.5f);
  out.raw(row);
  return out.bytes;
}

const fo4::Record* omod_record(const fo4::Plugin& plugin) {
  return fo4::find_record(plugin, fo4::FormKey::parse(k_key));
}

// Constraint: the OBTS header is 16 bytes, then one keyword, the 2-byte rank trailer, one 7-byte include row
// and one 24-byte property row, so a stored count that disagrees with the payload length must be detectable here.
Bytes obts_payload() {
  ByteWriter out;
  out.u32(1);
  out.u32(1);
  out.u8(1);
  out.u8(0);
  out.u8(2);
  out.u8(0);
  out.u16(3);
  out.u8(4);
  out.u8(1);
  out.u32(0x00000A30);
  out.u8(7);
  out.u8(3);
  out.u32(0x00000A31);
  out.u8(5);
  out.u8(1);
  out.u8(0);
  out.raw(property_row(4, 1, 0x0142, 0x00000A33, 0x40000000, 0.5f));
  return out.bytes;
}

Bytes from_u8(std::initializer_list<std::uint8_t> values) {
  Bytes out;
  for (const auto value : values) out.push_back(static_cast<std::byte>(value));
  return out;
}

// Constraint: these are the stored bytes of one Fallout4.esm ARMO OBTS (0023CAA4, the 53-byte template), copied verbatim.
Bytes vanilla_armo_obts() {
  return from_u8({0x01, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xFF, 0xFF,
                  0x00, 0x01, 0x43, 0xDE, 0x20, 0x00, 0x00, 0x00, 0x3F, 0xDE, 0x20, 0x00, 0x00, 0x00,
                  0x01, 0x04, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00, 0x0D, 0x00, 0x00, 0x00, 0xA3,
                  0xCA, 0x23, 0x00, 0xB9, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00});
}

// Constraint: these are the stored bytes of one Fallout4.esm WEAP OBTS (0024A3A9), copied verbatim; its rank trailer is 17 00.
Bytes vanilla_weap_obts() {
  return from_u8({0x05, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xFF, 0xFF,
                  0x01, 0x00, 0x17, 0x00, 0xED, 0xEC, 0x12, 0x00, 0x00, 0x00, 0x01, 0x3D, 0x98, 0x09,
                  0x00, 0x00, 0x00, 0x01, 0x36, 0x98, 0x09, 0x00, 0x00, 0x00, 0x01, 0xFA, 0x77, 0x18,
                  0x00, 0x00, 0x00, 0x01, 0x1D, 0xF2, 0x04, 0x00, 0x00, 0x00, 0x01});
}

Bytes make_object_template_plugin(Bytes obts) {
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

  const auto group = fo4::test::begin_group(out, "FURN", 0);
  const auto record = fo4::test::begin_record(out, "FURN", 0, raw_id(0x0D01));
  fo4::test::write_subrecord(out, "EDID", zstr("RadicleFurniture"));
  fo4::test::write_subrecord(out, "OBTS", std::move(obts));
  fo4::test::end_record(out, record);
  fo4::test::end_group(out, group);
  return out.bytes;
}

constexpr const char* k_object_template_leaves[] = {
    "LevelMin", "LevelMax", "ParentCombinationIndex", "Keyword", "MinLevelForRanks",
    "AltLevelsPerTier", "IncludeMod", "IncludeAttachPointIndex", "IncludeOptional",
    "IncludeDontUseAll", "PropertyValueType", "PropertyFunctionType", "PropertyProperty",
    "PropertyValue1", "PropertyValue2", "PropertyStep"};

fo4::schema::ScalarFieldDescriptor furn_field(std::string_view leaf) {
  const auto field = fo4::schema::scalar_field_descriptor("FURN\\ObjectTemplate" + std::string(leaf));
  if (!field) throw std::invalid_argument("unknown row FURN\\ObjectTemplate" + std::string(leaf));
  return *field;
}

std::optional<std::string> furn_row(const fo4::Plugin& plugin, std::string_view leaf, std::size_t index = 0) {
  return fo4::record_scalar_text(plugin, *omod_record(plugin), furn_field(leaf), index);
}

std::size_t furn_row_count(const fo4::Plugin& plugin, std::string_view leaf) {
  return fo4::record_field_count(plugin, fo4::FormKey::parse(k_key), furn_field(leaf)).value_or(~std::size_t{0});
}

std::size_t rewrite_object_template_rows(fo4::Plugin& plugin) {
  std::size_t rows = 0;
  for (const auto* leaf : k_object_template_leaves) {
    const auto field = furn_field(leaf);
    const auto count = furn_row_count(plugin, leaf);
    for (std::size_t index = 0; index < count; ++index) {
      const auto text = fo4::record_scalar_text(plugin, *omod_record(plugin), field, index);
      if (!text) {
        ADD_FAILURE() << leaf << " " << index;
        continue;
      }
      const auto status = fo4::set_record_scalar(plugin, fo4::FormKey::parse(k_key), field, index, *text);
      EXPECT_TRUE(status == fo4::ScalarMutationStatus::updated || status == fo4::ScalarMutationStatus::unchanged)
          << leaf;
      ++rows;
    }
  }
  return rows;
}

}  // namespace

// Constraint: detects an object-template row rewrite that zeroes the Unused bytes (1-3, 5-7, 10-11) of a property row.
TEST(ObtsTemplate, PropertyRowUnusedBytesSurviveARewriteOfEveryRow) {
  auto stored = obts_payload();
  const auto row = stored.size() - 24;
  for (const std::size_t at : {1u, 2u, 3u, 5u, 6u, 7u, 10u, 11u}) {
    stored[row + at] = static_cast<std::byte>(0xA0 + at);
  }
  auto plugin = fo4::read_plugin(make_object_template_plugin(stored), "Patch.esp");
  EXPECT_EQ(furn_row(plugin, "PropertyProperty"), "322");
  EXPECT_EQ(rewrite_object_template_rows(plugin), 16u);
  EXPECT_EQ(sub_bytes(plugin, "OBTS"), stored);
}

// Constraint: detects a row address that lands the keyword, include or property words on the wrong bytes of a stored template.
TEST(ObtsTemplate, RowsDecodeKeywordsIncludesAndProperties) {
  const auto plugin = fo4::read_plugin(make_object_template_plugin(obts_payload()), "Patch.esp");
  const auto context = fo4::plugin_form_id_context(plugin);
  EXPECT_EQ(furn_row(plugin, "LevelMin"), "1");
  EXPECT_EQ(furn_row(plugin, "LevelMax"), "2");
  EXPECT_EQ(furn_row(plugin, "ParentCombinationIndex"), "3");
  EXPECT_EQ(furn_row_count(plugin, "Keyword"), 1u);
  EXPECT_EQ(furn_row(plugin, "Keyword"), "2608");
  EXPECT_EQ(fo4::resolve_plugin_form_id(fo4::RawFormId{2608}, context),
            fo4::FormKey::parse("000A30:Fallout4.esm"));
  EXPECT_EQ(furn_row(plugin, "MinLevelForRanks"), "7");
  EXPECT_EQ(furn_row(plugin, "AltLevelsPerTier"), "3");
  EXPECT_EQ(furn_row_count(plugin, "IncludeMod"), 1u);
  EXPECT_EQ(fo4::resolve_plugin_form_id(
                fo4::RawFormId{static_cast<std::uint32_t>(std::stoul(*furn_row(plugin, "IncludeMod")))}, context),
            fo4::FormKey::parse("000A31:Fallout4.esm"));
  EXPECT_EQ(furn_row(plugin, "IncludeAttachPointIndex"), "5");
  EXPECT_EQ(furn_row(plugin, "IncludeOptional"), "1");
  EXPECT_EQ(furn_row(plugin, "IncludeDontUseAll"), "0");
  EXPECT_EQ(furn_row_count(plugin, "PropertyValue1"), 1u);
  EXPECT_EQ(furn_row(plugin, "PropertyValueType"), "4");
  EXPECT_EQ(furn_row(plugin, "PropertyProperty"), "322");
  EXPECT_EQ(furn_row(plugin, "PropertyValue1"), "2611");
  EXPECT_EQ(furn_row(plugin, "PropertyValue2"), "1073741824");
}

// Constraint: detects a row writer that changes a byte of a stored vanilla ARMO or WEAP template when a row is written back unchanged.
TEST(ObtsTemplate, VanillaFallout4EsmTemplatesReadAndRewriteUnchangedThroughRows) {
  {
    auto plugin = fo4::read_plugin(make_object_template_plugin(vanilla_armo_obts()), "Patch.esp");
    EXPECT_EQ(furn_row_count(plugin, "Keyword"), 1u);
    EXPECT_EQ(furn_row(plugin, "Keyword"), "2154051");
    EXPECT_EQ(furn_row(plugin, "MinLevelForRanks"), "0");
    EXPECT_EQ(furn_row_count(plugin, "IncludeMod"), 1u);
    EXPECT_EQ(furn_row(plugin, "IncludeMod"), "2154047");
    EXPECT_EQ(furn_row(plugin, "IncludeDontUseAll"), "1");
    EXPECT_EQ(furn_row_count(plugin, "PropertyValue1"), 1u);
    EXPECT_EQ(furn_row(plugin, "PropertyValueType"), "4");
    EXPECT_EQ(furn_row(plugin, "PropertyFunctionType"), "2");
    EXPECT_EQ(furn_row(plugin, "PropertyProperty"), "13");
    EXPECT_EQ(furn_row(plugin, "PropertyValue1"), "2345635");
    EXPECT_EQ(rewrite_object_template_rows(plugin), 16u);
    EXPECT_EQ(sub_bytes(plugin, "OBTS"), vanilla_armo_obts());
  }
  {
    auto plugin = fo4::read_plugin(make_object_template_plugin(vanilla_weap_obts()), "Patch.esp");
    EXPECT_EQ(furn_row_count(plugin, "Keyword"), 0u);
    EXPECT_EQ(furn_row(plugin, "MinLevelForRanks"), "23");
    EXPECT_EQ(furn_row(plugin, "AltLevelsPerTier"), "0");
    EXPECT_EQ(furn_row_count(plugin, "IncludeMod"), 5u);
    EXPECT_EQ(furn_row_count(plugin, "PropertyValue1"), 0u);
    EXPECT_EQ(rewrite_object_template_rows(plugin), 3u + 2u + 5u * 4u);
    EXPECT_EQ(sub_bytes(plugin, "OBTS"), vanilla_weap_obts());
  }
}

// Constraint: detects rows that accept a payload whose stored counts disagree with its length and read another section's bytes.
TEST(ObtsTemplate, RowsRefuseMalformedTemplates) {
  const auto count_rows = [](Bytes payload) {
    const auto plugin =
        fo4::read_plugin(make_object_template_plugin(std::move(payload)), "Patch.esp");
    return furn_row_count(plugin, "IncludeMod");
  };
  EXPECT_EQ(count_rows(obts_payload()), 1u);
  EXPECT_THROW(count_rows(Bytes(10)), fo4::schema::ScalarError);
  EXPECT_THROW(count_rows(Bytes(0x10)), fo4::schema::ScalarError);
  {
    auto miscounted = obts_payload();
    miscounted[4] = std::byte{2};
    EXPECT_THROW(count_rows(std::move(miscounted)), fo4::schema::ScalarError);
  }
  {
    auto keywords = obts_payload();
    keywords[0x0F] = std::byte{2};
    EXPECT_THROW(count_rows(std::move(keywords)), fo4::schema::ScalarError);
  }
  {
    auto no_trailer = obts_payload();
    no_trailer.erase(no_trailer.begin() + 0x14, no_trailer.begin() + 0x16);
    EXPECT_THROW(count_rows(std::move(no_trailer)), fo4::schema::ScalarError);
  }
}

// Constraint: the managed tests load this emitted Omod.esp from disk, so its bytes must decode through the same reader path the row tests use.
TEST(OmodFixture, WrittenPluginReloadsWithTheSameRows) {
  const auto path = std::filesystem::temp_directory_path() / "Omod.esp";
  const auto bytes = make_plugin(omod_data());
  {
    std::ofstream file(path, std::ios::binary);
    file.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    ASSERT_TRUE(file.good());
  }

  const auto plugin = fo4::read_plugin_file(path);
  const auto key = fo4::FormKey::parse("000D01:Omod.esp");
  const auto* record = fo4::find_record(plugin, key);
  ASSERT_NE(record, nullptr);
  const auto max_rank = scalar_field("OMOD\\MaxRank");
  const auto include = scalar_field("OMOD\\IncludeMod");
  ASSERT_TRUE(max_rank.has_value());
  ASSERT_TRUE(include.has_value());
  EXPECT_EQ(fo4::record_scalar_text(plugin, *record, *max_rank, 0), "3");
  EXPECT_EQ(fo4::record_field_count(plugin, key, *include), 1u);
  EXPECT_EQ(fo4::record_scalar_text(plugin, *record, *include, 0), "2578");
}

namespace {

struct OmodInclude {
  std::uint32_t mod{0x00000A12};
  std::uint8_t minimum_level{5};
  std::uint8_t optional{1};
  std::uint8_t dont_use_all{0};
};

struct OmodPropertyRow {
  std::uint8_t value_type{0};
  std::uint8_t function_type{0};
  std::uint16_t property{0};
  std::uint32_t value1{0};
  std::uint32_t value2{0};
  float step{0.0f};
};

struct OmodSpec {
  std::vector<std::uint32_t> slots{0x00000A11};
  std::vector<std::pair<std::uint32_t, std::uint32_t>> items{{0x11223344, 0x55667788}};
  std::vector<OmodInclude> includes{OmodInclude{}};
  std::vector<OmodPropertyRow> properties{{1, 2, 0x0102, 0x3F800000u, 7, 0.5f},
                                          {4, 0, 0x0031, 0x00000A13, 77, -2.0f}};
};

Bytes omod_payload(const OmodSpec& spec) {
  ByteWriter out;
  out.u32(static_cast<std::uint32_t>(spec.includes.size()));
  out.u32(static_cast<std::uint32_t>(spec.properties.size()));
  out.u8(1);
  out.u8(0);
  out.u32(1330467393u);
  out.u8(3);
  out.u8(2);
  out.u32(0x00000A10);
  out.u32(static_cast<std::uint32_t>(spec.slots.size()));
  for (const auto slot : spec.slots) out.u32(slot);
  out.u32(static_cast<std::uint32_t>(spec.items.size()));
  for (const auto& item : spec.items) {
    out.u32(item.first);
    out.u32(item.second);
  }
  for (const auto& include : spec.includes) {
    out.u32(include.mod);
    out.u8(include.minimum_level);
    out.u8(include.optional);
    out.u8(include.dont_use_all);
  }
  for (const auto& property : spec.properties) {
    out.u8(property.value_type);
    for (int i = 0; i < 3; ++i) out.u8(0xAB);
    out.u8(property.function_type);
    for (int i = 0; i < 3; ++i) out.u8(0xAB);
    out.u16(property.property);
    out.u8(0xAB);
    out.u8(0xAB);
    out.u32(property.value1);
    out.u32(property.value2);
    out.f32(property.step);
  }
  return out.bytes;
}

std::optional<std::string> row_text(const fo4::Plugin& plugin, std::string_view path, std::size_t index) {
  const auto field = scalar_field(path);
  const auto* record = fo4::find_record(plugin, fo4::FormKey::parse(k_key));
  if (!field.has_value() || record == nullptr) {
    ADD_FAILURE() << path;
    return std::nullopt;
  }
  return fo4::record_scalar_text(plugin, *record, *field, index);
}

std::size_t row_count(const fo4::Plugin& plugin, std::string_view path) {
  const auto field = scalar_field(path);
  if (!field.has_value()) {
    ADD_FAILURE() << path;
    return 0;
  }
  return fo4::record_field_count(plugin, fo4::FormKey::parse(k_key), *field).value_or(~std::size_t{0});
}

fo4::ScalarMutationStatus set_row(fo4::Plugin& plugin, std::string_view path, std::size_t index,
                                  std::string_view value) {
  const auto field = scalar_field(path);
  if (!field.has_value()) {
    ADD_FAILURE() << path;
    return fo4::ScalarMutationStatus::subrecord_missing;
  }
  return fo4::set_record_scalar(plugin, fo4::FormKey::parse(k_key), *field, index, value);
}

}  // namespace

// Constraint: detects a run walker that skips the inline-counted slot and item sections wrongly, which moves every include and property row.
TEST(OmodRows, IncludeAndPropertyRowsReadPastTheInlineCountedSlotAndItemSections) {
  const auto plugin = fo4::read_plugin(make_plugin(omod_payload(OmodSpec{})), "Patch.esp");
  EXPECT_EQ(row_count(plugin, "OMOD\\IncludeOptional"), 1u);
  EXPECT_EQ(row_count(plugin, "OMOD\\PropertyStep"), 2u);
  EXPECT_EQ(row_count(plugin, "OMOD\\IncludeMod"), 1u);
  EXPECT_EQ(row_text(plugin, "OMOD\\IncludeMod", 0), "2578");
  EXPECT_EQ(row_text(plugin, "OMOD\\IncludeMinimumLevel", 0), "5");
  EXPECT_EQ(row_text(plugin, "OMOD\\IncludeOptional", 0), "1");
  EXPECT_EQ(row_text(plugin, "OMOD\\IncludeDontUseAll", 0), "0");
  EXPECT_FALSE(row_text(plugin, "OMOD\\IncludeOptional", 1).has_value());
  EXPECT_EQ(row_text(plugin, "OMOD\\PropertyValueType", 0), "1");
  EXPECT_EQ(row_text(plugin, "OMOD\\PropertyValueType", 1), "4");
  EXPECT_EQ(row_text(plugin, "OMOD\\PropertyFunctionType", 0), "2");
  EXPECT_EQ(row_text(plugin, "OMOD\\PropertyProperty", 0), "258");
  EXPECT_EQ(row_text(plugin, "OMOD\\PropertyProperty", 1), "49");
  EXPECT_EQ(row_text(plugin, "OMOD\\PropertyValue1", 0), "1065353216");
  EXPECT_EQ(row_text(plugin, "OMOD\\PropertyValue1", 1), "2579");
  EXPECT_EQ(row_text(plugin, "OMOD\\PropertyValue2", 1), "77");
  const auto step = row_text(plugin, "OMOD\\PropertyStep", 1);
  ASSERT_TRUE(step.has_value());
  EXPECT_FLOAT_EQ(std::stof(*step), -2.0f);

  const auto data = sub_bytes(plugin, "DATA");
  const auto includes = fo4::schema::run_span(data, scalar_field("OMOD\\IncludeOptional")->scalar.address.run);
  const auto properties = fo4::schema::run_span(data, scalar_field("OMOD\\PropertyStep")->scalar.address.run);
  ASSERT_TRUE(includes.has_value());
  ASSERT_TRUE(properties.has_value());
  EXPECT_EQ(includes->base, 40u);
  EXPECT_EQ(includes->count, 1u);
  EXPECT_EQ(properties->base, 47u);
  EXPECT_EQ(properties->count, 2u);

  OmodSpec bare;
  bare.slots.clear();
  bare.items.clear();
  bare.includes.clear();
  bare.properties = {{2, 1, 9, 1, 0, 1.0f}};
  const auto bare_plugin = fo4::read_plugin(make_plugin(omod_payload(bare)), "Patch.esp");
  EXPECT_EQ(row_count(bare_plugin, "OMOD\\IncludeOptional"), 0u);
  EXPECT_EQ(row_count(bare_plugin, "OMOD\\PropertyStep"), 1u);
  EXPECT_EQ(row_text(bare_plugin, "OMOD\\PropertyProperty", 0), "9");
}

// Constraint: detects a row write that rebuilds a property row (zeroing its 0xAB Unused bytes) or moves the slot, item or FormID words.
TEST(OmodRows, IncludeAndPropertyRowWritesLandOnTheirBytesAndNothingElseMoves) {
  auto plugin = fo4::read_plugin(make_plugin(omod_payload(OmodSpec{})), "Patch.esp");
  EXPECT_EQ(set_row(plugin, "OMOD\\IncludeMod", 0, "2579"), fo4::ScalarMutationStatus::updated);
  EXPECT_EQ(set_row(plugin, "OMOD\\IncludeOptional", 0, "0"), fo4::ScalarMutationStatus::updated);
  EXPECT_EQ(set_row(plugin, "OMOD\\IncludeMinimumLevel", 0, "9"), fo4::ScalarMutationStatus::updated);
  EXPECT_EQ(set_row(plugin, "OMOD\\PropertyFunctionType", 1, "2"), fo4::ScalarMutationStatus::updated);
  EXPECT_EQ(set_row(plugin, "OMOD\\PropertyValue2", 1, "100"), fo4::ScalarMutationStatus::updated);
  EXPECT_EQ(set_row(plugin, "OMOD\\PropertyStep", 0, "1.5"), fo4::ScalarMutationStatus::updated);
  EXPECT_EQ(set_row(plugin, "OMOD\\PropertyProperty", 0, "258"), fo4::ScalarMutationStatus::unchanged);

  OmodSpec edited;
  edited.includes[0].mod = 0x00000A13;
  edited.includes[0].optional = 0;
  edited.includes[0].minimum_level = 9;
  edited.properties[1].function_type = 2;
  edited.properties[1].value2 = 100;
  edited.properties[0].step = 1.5f;
  const auto reread = fo4::read_plugin(fo4::write_plugin(plugin), "Patch.esp");
  EXPECT_EQ(sub_bytes(reread, "DATA"), omod_payload(edited));
}

// Constraint: detects an out-of-range or invalid include or property row write that mutates the record or reports success instead of refusing.
TEST(OmodRows, IncludeAndPropertyOutOfRangeIndexAndBadValuesDoNotTouchTheRecord) {
  auto plugin = fo4::read_plugin(make_plugin(omod_payload(OmodSpec{})), "Patch.esp");
  const auto before = fo4::test::plugin_state(plugin);
  EXPECT_EQ(set_row(plugin, "OMOD\\IncludeOptional", 1, "1"), fo4::ScalarMutationStatus::subrecord_missing);
  EXPECT_EQ(set_row(plugin, "OMOD\\PropertyStep", 2, "1.0"), fo4::ScalarMutationStatus::subrecord_missing);
  EXPECT_THROW(set_row(plugin, "OMOD\\IncludeOptional", 0, "2"), fo4::schema::ScalarError);
  EXPECT_THROW(set_row(plugin, "OMOD\\PropertyFunctionType", 0, "3"), fo4::schema::ScalarError);
  EXPECT_EQ(fo4::test::plugin_state(plugin), before);
}

// Constraint: detects a run walker that accepts sections not ending at the payload end (trailing byte, inflated slot or property count, cut row).
TEST(OmodRows, IncludeAndPropertyRowsFailClosedWhenTheSectionsDoNotEndAtThePayloadEnd) {
  const auto good = omod_payload(OmodSpec{});
  std::vector<Bytes> bad;
  bad.push_back(good);
  bad.back().push_back(std::byte{0});
  bad.push_back(good);
  std::memcpy(bad.back().data() + 20, u32_bytes(9).data(), 4);
  bad.push_back(good);
  std::memcpy(bad.back().data() + 4, u32_bytes(3).data(), 4);
  bad.push_back(Bytes(good.begin(), good.end() - 1));
  for (const auto& payload : bad) {
    auto plugin = fo4::read_plugin(make_plugin(payload), "Patch.esp");
    EXPECT_THROW(row_count(plugin, "OMOD\\PropertyStep"), fo4::schema::ScalarError);
    EXPECT_THROW(row_text(plugin, "OMOD\\IncludeOptional", 0), fo4::schema::ScalarError);
    EXPECT_THROW(row_count(plugin, "OMOD\\IncludeMod"), fo4::schema::ScalarError);
    const auto before = fo4::test::plugin_state(plugin);
    EXPECT_THROW(set_row(plugin, "OMOD\\PropertyStep", 0, "9"), fo4::schema::ScalarError);
    EXPECT_EQ(fo4::test::plugin_state(plugin), before);
    EXPECT_EQ(row_text(plugin, "OMOD\\MaxRank", 0), "3");
  }
}

// Constraint: detects a run row that reads a DATA stub shorter than the 20-byte header as holding no rows.
TEST(OmodRows, IncludeWordRowsRefuseADataPayloadShorterThanItsHeader) {
  const auto plugin = fo4::read_plugin(make_plugin(short_omod_data()), "Patch.esp");
  EXPECT_THROW(row_count(plugin, "OMOD\\IncludeMod"), fo4::schema::ScalarError);
  EXPECT_THROW(row_count(plugin, "OMOD\\PropertyValueType"), fo4::schema::ScalarError);
}

// Constraint: detects an own-count inline run that starts the slot rows at the wrong byte or moves the include and property rows when the slot count changes.
TEST(OmodRows, AttachParentSlotRowsReadAndWriteTheInlineCountedSlots) {
  const auto field = scalar_field("OMOD\\AttachParentSlot");
  ASSERT_TRUE(field.has_value());
  EXPECT_EQ(field->scalar.subrecord, fo4::Signature::from_string("DATA"));
  EXPECT_EQ(field->scalar.address.run.start, 20u);
  EXPECT_EQ(static_cast<unsigned>(field->scalar.address.run.count), 4u);
  EXPECT_EQ(static_cast<unsigned>(field->scalar.address.run.index), 0u);

  for (const auto& slots : {std::vector<std::uint32_t>{}, std::vector<std::uint32_t>{0x00000A11},
                            std::vector<std::uint32_t>{0x00000A11, 0x00000A22, 0x00000A33}}) {
    SCOPED_TRACE(slots.size());
    OmodSpec spec;
    spec.slots = slots;
    const auto plugin = fo4::read_plugin(make_plugin(omod_payload(spec)), "Patch.esp");
    EXPECT_EQ(row_count(plugin, "OMOD\\AttachParentSlot"), slots.size());
    for (std::size_t i = 0; i < slots.size(); ++i) {
      EXPECT_EQ(row_text(plugin, "OMOD\\AttachParentSlot", i), std::to_string(slots[i]));
    }
    EXPECT_FALSE(row_text(plugin, "OMOD\\AttachParentSlot", slots.size()).has_value());
    EXPECT_EQ(row_text(plugin, "OMOD\\IncludeMod", 0), "2578");
    EXPECT_EQ(row_text(plugin, "OMOD\\PropertyValue1", 1), "2579");
  }

  OmodSpec three;
  three.slots = {0x00000A11, 0x00000A22, 0x00000A33};
  auto plugin = fo4::read_plugin(make_plugin(omod_payload(three)), "Patch.esp");
  const auto before = fo4::test::plugin_state(plugin);
  EXPECT_EQ(set_row(plugin, "OMOD\\AttachParentSlot", 3, "1"), fo4::ScalarMutationStatus::subrecord_missing);
  EXPECT_EQ(fo4::test::plugin_state(plugin), before);
  EXPECT_EQ(set_row(plugin, "OMOD\\AttachParentSlot", 1, "4660"), fo4::ScalarMutationStatus::updated);
  EXPECT_EQ(set_row(plugin, "OMOD\\AttachParentSlot", 2, "2611"), fo4::ScalarMutationStatus::unchanged);
  three.slots[1] = 4660;
  const auto reread = fo4::read_plugin(fo4::write_plugin(plugin), "Patch.esp");
  EXPECT_EQ(sub_bytes(reread, "DATA"), omod_payload(three));
}

// Constraint: detects an own-count inline run that accepts a miscounted or short DATA payload as holding slot rows.
TEST(OmodRows, AttachParentSlotRowsFailClosedWhenTheSectionsDoNotEndAtThePayloadEnd) {
  const auto good = omod_payload(OmodSpec{});
  std::vector<Bytes> bad;
  bad.push_back(good);
  bad.back().push_back(std::byte{0});
  bad.push_back(good);
  std::memcpy(bad.back().data() + 20, u32_bytes(9).data(), 4);
  bad.push_back(Bytes(good.begin(), good.end() - 1));
  for (const auto& payload : bad) {
    auto plugin = fo4::read_plugin(make_plugin(payload), "Patch.esp");
    EXPECT_THROW(row_count(plugin, "OMOD\\AttachParentSlot"), fo4::schema::ScalarError);
    EXPECT_THROW(row_text(plugin, "OMOD\\AttachParentSlot", 0), fo4::schema::ScalarError);
    const auto before = fo4::test::plugin_state(plugin);
    EXPECT_THROW(set_row(plugin, "OMOD\\AttachParentSlot", 0, "9"), fo4::schema::ScalarError);
    EXPECT_EQ(fo4::test::plugin_state(plugin), before);
  }
  const auto stub = fo4::read_plugin(make_plugin(short_omod_data()), "Patch.esp");
  EXPECT_THROW(row_count(stub, "OMOD\\AttachParentSlot"), fo4::schema::ScalarError);
}

// Constraint: detects a property reader that goes through text and so rejects or rewrites a stored non-finite step.
TEST(OmodRows, RawByteReaderReturnsTheStoredPropertyWordsAndANonFiniteStep) {
  auto stored = omod_data_with_property(6, 0x00000A13u);
  const auto nan = u32_bytes(0x7FC00001u);
  std::memcpy(stored.data() + stored.size() - 4, nan.data(), 4);
  const auto plugin = fo4::read_plugin(make_plugin(stored), "Patch.esp");
  const auto* record = omod_record(plugin);
  ASSERT_NE(record, nullptr);
  const auto step = scalar_field("OMOD\\PropertyStep");
  const auto value2 = scalar_field("OMOD\\PropertyValue2");
  ASSERT_TRUE(step.has_value());
  ASSERT_TRUE(value2.has_value());
  EXPECT_THROW(fo4::record_scalar_text(plugin, *record, *step, 0), fo4::schema::ScalarError);
  EXPECT_EQ(fo4::record_scalar_bytes(*record, *step, 0), (std::optional<Bytes>{nan}));
  EXPECT_EQ(fo4::record_scalar_bytes(*record, *value2, 0), (std::optional<Bytes>{u32_bytes(0x40000000u)}));
  EXPECT_FALSE(fo4::record_scalar_bytes(*record, *step, 1).has_value());
}
