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

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <initializer_list>
#include <optional>
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
  EXPECT_EQ(fo4::set_record_scalar(plugin, key, *type, 0, "Weapon"),
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
