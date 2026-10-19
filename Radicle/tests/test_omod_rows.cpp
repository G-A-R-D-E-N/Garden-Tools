#include "fo4/forms/omod.hpp"
#include "fo4/plugin/mutation.hpp"
#include "fo4/plugin/reader.hpp"
#include "fo4/plugin/subrecord_authoring.hpp"
#include "fo4/plugin/writer.hpp"
#include "fo4/schema/form_links.hpp"
#include "fo4/schema/formlink_lists.hpp"
#include "fo4/schema/record_constraints.hpp"
#include "fo4/schema/scalar.hpp"
#include "plugin_bytes.hpp"

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <initializer_list>
#include <optional>
#include <string>
#include <string_view>
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

}  // namespace

TEST(OmodData, DecodesHeaderSlotsItemsAndIncludes) {
  const auto plugin = fo4::read_plugin(make_plugin(omod_data()), "Patch.esp");
  const auto* record = omod_record(plugin);
  ASSERT_NE(record, nullptr);
  const auto data = fo4::forms::read_omod_data(plugin, *record);
  EXPECT_EQ(data.unknown_bool1, 1);
  EXPECT_EQ(data.unknown_bool2, 0);
  EXPECT_EQ(data.form_type, 1330467393u);
  EXPECT_EQ(data.max_rank, 3);
  EXPECT_EQ(data.level_tier_scaled_offset, 2);
  EXPECT_EQ(data.attach_point, fo4::FormKey::parse("000A10:Fallout4.esm"));
  ASSERT_EQ(data.attach_parent_slots.size(), 1u);
  EXPECT_EQ(data.attach_parent_slots[0], fo4::FormKey::parse("000A11:Fallout4.esm"));
  ASSERT_EQ(data.items.size(), 1u);
  EXPECT_EQ(data.items[0].value1, (std::array<std::byte, 4>{std::byte{0x44}, std::byte{0x33},
                                                            std::byte{0x22}, std::byte{0x11}}));
  EXPECT_EQ(data.items[0].value2, (std::array<std::byte, 4>{std::byte{0x88}, std::byte{0x77},
                                                            std::byte{0x66}, std::byte{0x55}}));
  ASSERT_EQ(data.includes.size(), 1u);
  EXPECT_EQ(data.includes[0].mod, fo4::FormKey::parse("000A12:Fallout4.esm"));
  EXPECT_EQ(data.includes[0].minimum_level, 0);
  EXPECT_EQ(data.includes[0].optional, 1);
  EXPECT_EQ(data.includes[0].use_all, 0);
  EXPECT_TRUE(data.properties.empty());
}

TEST(OmodData, DecodesPropertyValueArms) {
  const auto form_plugin =
      fo4::read_plugin(make_plugin(omod_data_with_property(4, 0x00000A13u)), "Patch.esp");
  const auto* form_record = omod_record(form_plugin);
  ASSERT_NE(form_record, nullptr);
  auto data = fo4::forms::read_omod_data(form_plugin, *form_record);
  ASSERT_EQ(data.properties.size(), 1u);
  EXPECT_EQ(data.properties[0].value_type, 4);
  EXPECT_EQ(data.properties[0].function_type, 1);
  EXPECT_EQ(data.properties[0].property, 0x0142);
  EXPECT_FLOAT_EQ(data.properties[0].step, 0.5f);
  const auto linked = fo4::forms::omod_property_value1_form(form_plugin, data.properties[0]);
  ASSERT_TRUE(linked.has_value());
  EXPECT_EQ(*linked, fo4::FormKey::parse("000A13:Fallout4.esm"));
  EXPECT_FALSE(fo4::forms::omod_property_value1_float(data.properties[0]).has_value());
  EXPECT_EQ(fo4::forms::omod_property_value1_int(data.properties[0]),
            (std::optional<std::uint32_t>{0x00000A13u}));
  EXPECT_EQ(fo4::forms::omod_property_value2_int(data.properties[0]),
            (std::optional<std::uint32_t>{0x40000000u}));
  EXPECT_FALSE(fo4::forms::omod_property_value2_float(data.properties[0]).has_value());

  const auto float_plugin =
      fo4::read_plugin(make_plugin(omod_data_with_property(1, 0x3F800000u)), "Patch.esp");
  const auto* float_record = omod_record(float_plugin);
  ASSERT_NE(float_record, nullptr);
  data = fo4::forms::read_omod_data(float_plugin, *float_record);
  ASSERT_EQ(data.properties.size(), 1u);
  const auto first = fo4::forms::omod_property_value1_float(data.properties[0]);
  ASSERT_TRUE(first.has_value());
  EXPECT_FLOAT_EQ(*first, 1.0f);
  const auto second = fo4::forms::omod_property_value2_float(data.properties[0]);
  ASSERT_TRUE(second.has_value());
  EXPECT_FLOAT_EQ(*second, 2.0f);
  EXPECT_FALSE(fo4::forms::omod_property_value1_form(float_plugin, data.properties[0]).has_value());
  EXPECT_FALSE(fo4::forms::omod_property_value1_int(data.properties[0]).has_value());

  const auto bool_plugin =
      fo4::read_plugin(make_plugin(omod_data_with_property(2, 1u)), "Patch.esp");
  const auto* bool_record = omod_record(bool_plugin);
  ASSERT_NE(bool_record, nullptr);
  data = fo4::forms::read_omod_data(bool_plugin, *bool_record);
  ASSERT_EQ(data.properties.size(), 1u);
  EXPECT_EQ(fo4::forms::omod_property_value1_int(data.properties[0]),
            (std::optional<std::uint32_t>{1u}));
  EXPECT_EQ(fo4::forms::omod_property_value2_int(data.properties[0]),
            (std::optional<std::uint32_t>{0x40000000u}));
  EXPECT_FALSE(fo4::forms::omod_property_value2_float(data.properties[0]).has_value());
}

TEST(OmodData, RoundTripsThePayloadByteForByte) {
  auto plugin = fo4::read_plugin(make_plugin(omod_data()), "Patch.esp");
  const auto* record = omod_record(plugin);
  ASSERT_NE(record, nullptr);
  const auto data = fo4::forms::read_omod_data(plugin, *record);
  EXPECT_EQ(fo4::forms::write_omod_data(plugin, fo4::FormKey::parse(k_key), data),
            fo4::forms::OmodDataWrite::written);
  const auto reread = fo4::read_plugin(fo4::write_plugin(plugin), "Patch.esp");
  EXPECT_EQ(sub_bytes(reread, "DATA"), omod_data());
  EXPECT_EQ(sub_bytes(reread, "MNAM"), words({0x00000A20}));
}

TEST(OmodData, EditsArraysAndRewritesTheirCounts) {
  auto plugin = fo4::read_plugin(make_plugin(omod_data()), "Patch.esp");
  const auto key = fo4::FormKey::parse(k_key);
  const auto* record = omod_record(plugin);
  ASSERT_NE(record, nullptr);
  auto data = fo4::forms::read_omod_data(plugin, *record);

  data.attach_parent_slots.push_back(fo4::FormKey::parse("000A1F:Fallout4.esm"));
  data.items.clear();
  data.includes.push_back(data.includes[0]);
  fo4::forms::OmodProperty property;
  property.value_type = 6;
  property.function_type = 2;
  property.property = 0x0031;
  property.value1 = std::array<std::byte, 4>{std::byte{0x20}, std::byte{0x0A}, std::byte{0x00},
                                            std::byte{0x00}};
  property.value2 = std::array<std::byte, 4>{std::byte{0x00}, std::byte{0x00}, std::byte{0x80},
                                            std::byte{0x3F}};
  property.step = 1.0f;
  data.properties.push_back(property);
  EXPECT_EQ(fo4::forms::write_omod_data(plugin, key, data), fo4::forms::OmodDataWrite::written);

  EXPECT_EQ(sub_bytes(plugin, "DATA").size(), 20u + 4 + 8 + 4 + 14 + 24);
  const auto reread = fo4::read_plugin(fo4::write_plugin(plugin), "Patch.esp");
  const auto* written = omod_record(reread);
  ASSERT_NE(written, nullptr);
  const auto payload = sub_bytes(reread, "DATA");
  std::uint32_t include_count = 0;
  std::uint32_t property_count = 0;
  std::memcpy(&include_count, payload.data(), 4);
  std::memcpy(&property_count, payload.data() + 4, 4);
  EXPECT_EQ(include_count, 2u);
  EXPECT_EQ(property_count, 1u);
  const auto decoded = fo4::forms::read_omod_data(reread, *written);
  ASSERT_EQ(decoded.attach_parent_slots.size(), 2u);
  EXPECT_EQ(decoded.attach_parent_slots[1], fo4::FormKey::parse("000A1F:Fallout4.esm"));
  EXPECT_TRUE(decoded.items.empty());
  ASSERT_EQ(decoded.includes.size(), 2u);
  EXPECT_EQ(decoded.includes[1].optional, 1);
  ASSERT_EQ(decoded.properties.size(), 1u);
  EXPECT_EQ(decoded.properties[0].property, 0x0031);
  const auto linked = fo4::forms::omod_property_value1_form(reread, decoded.properties[0]);
  ASSERT_TRUE(linked.has_value());
  EXPECT_EQ(linked->local().value(), 0x000A20u);
  EXPECT_FALSE(fo4::forms::omod_property_value1_float(decoded.properties[0]).has_value());
  const auto factor = fo4::forms::omod_property_value2_float(decoded.properties[0]);
  ASSERT_TRUE(factor.has_value());
  EXPECT_FLOAT_EQ(*factor, 1.0f);
}

TEST(OmodData, RefusesMalformedPayloadsAndForeignRecords) {
  const auto decode = [](Bytes payload) {
    const auto plugin = fo4::read_plugin(make_plugin(std::move(payload)), "Patch.esp");
    const auto* record = omod_record(plugin);
    EXPECT_NE(record, nullptr);
    if (record == nullptr) return fo4::forms::OmodData{};
    return fo4::forms::read_omod_data(plugin, *record);
  };

  EXPECT_THROW(decode(short_omod_data()), fo4::forms::OmodError);
  {
    auto trailing = omod_data();
    trailing.insert(trailing.end(), 4, std::byte{0});
    EXPECT_THROW(decode(std::move(trailing)), fo4::forms::OmodError);
  }
  {
    auto truncated = omod_data();
    truncated.resize(24);
    EXPECT_THROW(decode(std::move(truncated)), fo4::forms::OmodError);
  }
  {
    auto miscounted = omod_data();
    std::memcpy(miscounted.data() + 4, u32_bytes(2).data(), 4);
    EXPECT_THROW(decode(std::move(miscounted)), fo4::forms::OmodError);
  }
  {
    auto unsized = omod_data();
    std::memcpy(unsized.data() + 28, u32_bytes(9).data(), 4);
    EXPECT_THROW(decode(std::move(unsized)), fo4::forms::OmodError);
  }
}

TEST(OmodData, WriteStatusesCoverForeignAndDatalessRecords) {
  auto plugin = fo4::read_plugin(make_plugin(omod_data()), "Patch.esp");
  const auto key = fo4::FormKey::parse(k_key);
  const fo4::forms::OmodData data;
  EXPECT_EQ(fo4::forms::write_omod_data(plugin, fo4::FormKey::parse("000D09:Patch.esp"), data),
            fo4::forms::OmodDataWrite::record_missing);
  EXPECT_EQ(fo4::forms::write_omod_data(plugin, fo4::FormKey::parse(k_keyword_key), data),
            fo4::forms::OmodDataWrite::unsupported_record);
  EXPECT_EQ(fo4::forms::write_omod_data(plugin, key, data), fo4::forms::OmodDataWrite::written);
  const auto reread = fo4::read_plugin(fo4::write_plugin(plugin), "Patch.esp");
  EXPECT_EQ(sub_bytes(reread, "DATA").size(), 28u);

  const auto created =
      fo4::create_record(plugin, fo4::Signature::from_string(k_omod), "RadicleNewOmod");
  ASSERT_EQ(created.status, fo4::RecordCreateStatus::created);
  EXPECT_EQ(fo4::forms::write_omod_data(plugin, created.key, data),
            fo4::forms::OmodDataWrite::missing_data_subrecord);
}
