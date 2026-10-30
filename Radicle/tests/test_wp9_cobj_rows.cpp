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
#include <initializer_list>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

using Bytes = std::vector<std::byte>;
using fo4::test::ByteWriter;

struct LinkRow {
  std::string_view path;
  std::string_view subrecord;
  bool nullable;
  std::string_view target;
  std::string_view rejected;
};

constexpr LinkRow k_link_rows[] = {
    {"COBJ\\CreatedObject", "CNAM", true, "ARMO", "KYWD"},
    {"COBJ\\WorkbenchKeyword", "BNAM", false, "KYWD", "ARMO"},
    {"COBJ\\MenuArtObject", "ANAM", false, "ARTO", "KYWD"},
    {"COBJ\\PickUpSound", "YNAM", false, "SNDR", "SOUN"},
    {"COBJ\\PutDownSound", "ZNAM", false, "SNDR", "SOUN"},
};

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

Bytes u32_words(std::initializer_list<std::uint32_t> values) {
  ByteWriter out;
  for (const auto value : values) out.u32(value);
  return out.bytes;
}

Bytes intv(std::uint16_t count, std::uint16_t priority) {
  ByteWriter out;
  out.u16(count);
  out.u16(priority);
  return out.bytes;
}

Bytes zstr(std::string_view text) {
  ByteWriter out;
  out.raw(text);
  out.u8(0);
  return out.bytes;
}

void add_cobj(ByteWriter& out, std::uint32_t local_id,
              std::initializer_list<std::pair<std::string_view, Bytes>> subrecords) {
  const auto group = fo4::test::begin_group(out, "COBJ", 0);
  const auto record = fo4::test::begin_record(out, "COBJ", 0, 0x02000000u | local_id);
  for (const auto& [subrecord, data] : subrecords) {
    fo4::test::write_subrecord(out, subrecord, data);
  }
  fo4::test::end_record(out, record);
  fo4::test::end_group(out, group);
}

ByteWriter make_plugin() {
  ByteWriter out;
  const auto tes4 = fo4::test::begin_record(out, "TES4", 0, 0);
  ByteWriter hedr;
  hedr.f32(1.0f);
  hedr.u32(6);
  hedr.u32(0x900);
  fo4::test::write_subrecord(out, "HEDR", hedr.bytes);
  fo4::test::write_subrecord(out, "MAST", std::string_view{"Fallout4.esm\0", 13});
  fo4::test::write_subrecord(out, "DATA", Bytes(8));
  fo4::test::write_subrecord(out, "MAST", std::string_view{"Objects.esp\0", 12});
  fo4::test::write_subrecord(out, "DATA", Bytes(8));
  fo4::test::end_record(out, tes4);
  add_cobj(out, 0xA01, {{"EDID", zstr("RadicleRecipe")},
                        {"CNAM", u32_bytes(0x01000A20)},
                        {"BNAM", u32_bytes(0x01000A21)},
                        {"FNAM", u32_words({0x01000A22})},
                        {"INTV", intv(5, 0x0102)}});
  add_cobj(out, 0xA02, {{"EDID", zstr("RadicleCount")},
                        {"INTV", Bytes{std::byte{0x03}, std::byte{0x00}}}});
  add_cobj(out, 0xA03, {{"EDID", zstr("RadicleBare")}, {"INTV", intv(1, 0)}});
  add_cobj(out, 0xA04, {{"EDID", zstr("RadicleCnam")},
                        {"CNAM", u32_bytes(0x01000A20)},
                        {"INTV", intv(1, 0)}});
  add_cobj(out, 0xA05, {{"EDID", zstr("RadicleSounds")},
                        {"YNAM", u32_bytes(0x01000A30)},
                        {"ZNAM", u32_bytes(0x01000A31)},
                        {"ANAM", u32_bytes(0x01000A40)},
                        {"INTV", intv(1, 0)}});
  return out;
}

Bytes sub_bytes(const fo4::Plugin& plugin, std::string_view key, std::string_view subrecord) {
  const auto* record = fo4::find_record(plugin, fo4::FormKey::parse(key));
  if (record == nullptr) {
    ADD_FAILURE() << key;
    return {};
  }
  for (const auto& sub : record->subrecords) {
    if (sub.signature.to_string() == subrecord) return sub.data;
  }
  ADD_FAILURE() << subrecord;
  return {};
}

Bytes scalar_bytes(const fo4::Plugin& plugin, std::string_view key, std::string_view path) {
  const auto field = fo4::schema::scalar_field_descriptor(fo4::Signature::from_string("COBJ"), path);
  if (!field.has_value()) {
    ADD_FAILURE() << path;
    return {};
  }
  const auto data = sub_bytes(plugin, key, field->scalar.subrecord.to_string());
  const auto bytes = fo4::schema::scalar_field_bytes(data, field->scalar);
  return Bytes(bytes.begin(), bytes.end());
}

std::string signature_list(const fo4::Plugin& plugin, std::string_view key) {
  std::string out;
  const auto* record = fo4::find_record(plugin, fo4::FormKey::parse(key));
  if (record == nullptr) {
    ADD_FAILURE() << key;
    return out;
  }
  for (std::size_t index = 0; index < record->subrecords.size(); ++index) {
    if (index != 0) out += ',';
    out += record->subrecords[index].signature.to_string();
  }
  return out;
}

fo4::schema::FormLinkListDescriptor category_list() {
  const auto descriptor = fo4::schema::formlink_list_descriptor(
      fo4::Signature::from_string("COBJ"), "COBJ\\Category");
  if (!descriptor) throw std::invalid_argument("COBJ\\Category row is missing");
  return *descriptor;
}

fo4::schema::FormLinkFieldDescriptor link_field(std::string_view path) {
  const auto field = fo4::schema::formlink_field_descriptor(fo4::Signature::from_string("COBJ"), path);
  if (!field) throw std::invalid_argument("COBJ link row is missing");
  return *field;
}

}  // namespace

TEST(WP9CobjRows, LinkRowsCarryXEditSubrecordTargetsAndNullability) {
  for (const auto& row : k_link_rows) {
    SCOPED_TRACE(row.path);
    const auto field =
        fo4::schema::formlink_field_descriptor(fo4::Signature::from_string("COBJ"), row.path);
    ASSERT_TRUE(field.has_value());
    EXPECT_EQ(field->subrecord, fo4::Signature::from_string(row.subrecord));
    EXPECT_EQ(field->offset, 0u);
    EXPECT_EQ(field->nullable, row.nullable);
    ASSERT_EQ(field->payload_sizes[0], 4u);
    EXPECT_TRUE(fo4::schema::formlink_field_target_allowed(*field, fo4::Signature::from_string(row.target)));
    EXPECT_FALSE(fo4::schema::formlink_field_target_allowed(*field, fo4::Signature::from_string(row.rejected)));
  }
}

TEST(WP9CobjRows, CategoryListIsPackedKeywordsWithoutCounter) {
  const auto descriptor = category_list();
  EXPECT_EQ(descriptor.subrecord, fo4::Signature::from_string("FNAM"));
  EXPECT_EQ(descriptor.storage, fo4::schema::FormLinkListStorage::packed_array);
  EXPECT_EQ(descriptor.counter, fo4::Signature{});
  EXPECT_FALSE(descriptor.nullable);
  EXPECT_TRUE(fo4::schema::formlink_list_target_allowed(descriptor, fo4::Signature::from_string("KYWD")));
  EXPECT_FALSE(fo4::schema::formlink_list_target_allowed(descriptor, fo4::Signature::from_string("ARMO")));
}

TEST(WP9CobjRows, IntvScalarsReadTheCountAndGatePriorityOnPayloadSize) {
  auto plugin = fo4::read_plugin(make_plugin().bytes, "Patch.esp");
  EXPECT_EQ(scalar_bytes(plugin, "000A01:Patch.esp", "COBJ\\CreatedObjectCount"), u16_bytes(5));
  EXPECT_EQ(scalar_bytes(plugin, "000A01:Patch.esp", "COBJ\\Priority"), u16_bytes(0x0102));
  EXPECT_EQ(scalar_bytes(plugin, "000A02:Patch.esp", "COBJ\\CreatedObjectCount"), u16_bytes(3));
  EXPECT_TRUE(scalar_bytes(plugin, "000A02:Patch.esp", "COBJ\\Priority").empty());
}

TEST(WP9CobjRows, IntvScalarsWriteAndRereadThroughTheirRows) {
  auto plugin = fo4::read_plugin(make_plugin().bytes, "Patch.esp");
  const auto key = fo4::FormKey::parse("000A01:Patch.esp");
  const auto priority = fo4::schema::scalar_field_descriptor(fo4::Signature::from_string("COBJ"), "COBJ\\Priority");
  ASSERT_TRUE(priority.has_value());
  EXPECT_EQ(fo4::set_record_scalar(plugin, key, *priority, "9"), fo4::ScalarMutationStatus::updated);
  const auto reread = fo4::read_plugin(fo4::write_plugin(plugin), "Patch.esp");
  EXPECT_EQ(sub_bytes(reread, "000A01:Patch.esp", "INTV"), (intv(5, 9)));
}

TEST(WP9CobjRows, CreatedObjectAndWorkbenchKeywordRetargetAndNullRules) {
  auto plugin = fo4::read_plugin(make_plugin().bytes, "Patch.esp");
  const auto key = fo4::FormKey::parse("000A01:Patch.esp");
  const auto none = fo4::FormKey{};
  const auto created = link_field("COBJ\\CreatedObject");
  const auto workbench = link_field("COBJ\\WorkbenchKeyword");

  EXPECT_EQ(fo4::set_record_formlink_field(plugin, key, created,
                                           fo4::FormKey::parse("000A30:Objects.esp")),
            fo4::FormLinkFieldMutationStatus::updated);
  EXPECT_EQ(fo4::set_record_formlink_field(plugin, key, created, none),
            fo4::FormLinkFieldMutationStatus::updated);
  EXPECT_THROW(fo4::set_record_formlink_field(plugin, key, workbench, none),
               fo4::schema::FormLinkFieldError);
  EXPECT_EQ(fo4::set_record_formlink_field(plugin, key, workbench,
                                           fo4::FormKey::parse("000A31:Objects.esp")),
            fo4::FormLinkFieldMutationStatus::updated);

  const auto reread = fo4::read_plugin(fo4::write_plugin(plugin), "Patch.esp");
  EXPECT_EQ(sub_bytes(reread, "000A01:Patch.esp", "CNAM"), u32_bytes(0));
  EXPECT_EQ(sub_bytes(reread, "000A01:Patch.esp", "BNAM"), u32_bytes(0x01000A31));
}

TEST(WP9CobjRows, SoundAndArtLinksRetargetAndRejectNull) {
  auto plugin = fo4::read_plugin(make_plugin().bytes, "Patch.esp");
  const auto key = fo4::FormKey::parse("000A05:Patch.esp");
  const auto none = fo4::FormKey{};
  const auto pick_up = link_field("COBJ\\PickUpSound");
  EXPECT_EQ(fo4::set_record_formlink_field(plugin, key, pick_up,
                                           fo4::FormKey::parse("000A32:Objects.esp")),
            fo4::FormLinkFieldMutationStatus::updated);
  EXPECT_THROW(fo4::set_record_formlink_field(plugin, key, link_field("COBJ\\MenuArtObject"), none),
               fo4::schema::FormLinkFieldError);
  const auto reread = fo4::read_plugin(fo4::write_plugin(plugin), "Patch.esp");
  EXPECT_EQ(sub_bytes(reread, "000A05:Patch.esp", "YNAM"), u32_bytes(0x01000A32));
  EXPECT_EQ(sub_bytes(reread, "000A05:Patch.esp", "ANAM"), u32_bytes(0x01000A40));
}

TEST(WP9CobjRows, CategoryKeywordsInsertAndRemoveKeepPackedPayload) {
  auto plugin = fo4::read_plugin(make_plugin().bytes, "Patch.esp");
  const auto descriptor = category_list();
  const auto key = fo4::FormKey::parse("000A01:Patch.esp");
  const auto updated = fo4::schema::FormLinkListMutationStatus::updated;

  EXPECT_EQ(fo4::schema::insert_record_formlink_list_element(plugin, key, descriptor, 1,
                                                             fo4::FormKey::parse("000A23:Objects.esp")),
            updated);
  EXPECT_EQ(fo4::schema::insert_record_formlink_list_element(plugin, key, descriptor, 0,
                                                             fo4::FormKey::parse("000A21:Objects.esp")),
            updated);
  EXPECT_EQ(sub_bytes(plugin, "000A01:Patch.esp", "FNAM"), (u32_words({0x01000A21, 0x01000A22, 0x01000A23})));

  EXPECT_EQ(fo4::schema::remove_record_formlink_list_element(plugin, key, descriptor, 0), updated);
  EXPECT_EQ(fo4::schema::remove_record_formlink_list_element(plugin, key, descriptor, 1), updated);
  EXPECT_EQ(sub_bytes(plugin, "000A01:Patch.esp", "FNAM"), u32_bytes(0x01000A22));
  EXPECT_EQ(signature_list(plugin, "000A01:Patch.esp"), "EDID,CNAM,BNAM,FNAM,INTV");

  const auto reread = fo4::read_plugin(fo4::write_plugin(plugin), "Patch.esp");
  EXPECT_EQ(fo4::schema::record_formlink_list_size(reread, key, descriptor), 1u);
}

TEST(WP9CobjRows, CategoryKeywordAnchorsAfterTheNearestPresentPredecessor) {
  auto plugin = fo4::read_plugin(make_plugin().bytes, "Patch.esp");
  const auto descriptor = category_list();
  const auto updated = fo4::schema::FormLinkListMutationStatus::updated;
  const auto bare = fo4::FormKey::parse("000A03:Patch.esp");
  const auto cnam = fo4::FormKey::parse("000A04:Patch.esp");

  EXPECT_EQ(fo4::schema::insert_record_formlink_list_element(plugin, bare, descriptor, 0,
                                                             fo4::FormKey::parse("000A24:Objects.esp")),
            updated);
  EXPECT_EQ(signature_list(plugin, "000A03:Patch.esp"), "EDID,FNAM,INTV");
  EXPECT_EQ(fo4::schema::insert_record_formlink_list_element(plugin, cnam, descriptor, 0,
                                                             fo4::FormKey::parse("000A24:Objects.esp")),
            updated);
  EXPECT_EQ(signature_list(plugin, "000A04:Patch.esp"), "EDID,CNAM,FNAM,INTV");

  EXPECT_EQ(fo4::schema::remove_record_formlink_list_element(plugin, bare, descriptor, 0), updated);
  EXPECT_EQ(signature_list(plugin, "000A03:Patch.esp"), "EDID,INTV");
}

TEST(WP9CobjRows, CreateThroughGenericPathWritesEdidThenIntvInAuthoringOrder) {
  auto plugin = fo4::read_plugin(make_plugin().bytes, "Patch.esp");
  const auto created = fo4::create_record(plugin, fo4::Signature::from_string("COBJ"), "RadicleNew");
  ASSERT_EQ(created.status, fo4::RecordCreateStatus::created);
  EXPECT_EQ(created.key, fo4::FormKey::parse("000900:Patch.esp"));
  EXPECT_EQ(signature_list(plugin, "000900:Patch.esp"), "EDID,INTV");
  EXPECT_EQ(sub_bytes(plugin, "000900:Patch.esp", "INTV"), intv(1, 0));

  EXPECT_EQ(fo4::add_record_subrecord(plugin, created.key, fo4::Signature::from_string("FVPA")),
            fo4::SubrecordAddStatus::not_authorable);
  EXPECT_EQ(fo4::add_record_subrecord(plugin, created.key, fo4::Signature::from_string("CNAM")),
            fo4::SubrecordAddStatus::no_default);
  EXPECT_EQ(fo4::remove_record_subrecord(plugin, created.key, fo4::Signature::from_string("INTV"), 0),
            fo4::SubrecordRemoveStatus::below_minimum);

  EXPECT_EQ(fo4::schema::insert_record_formlink_list_element(plugin, created.key, category_list(), 0,
                                                             fo4::FormKey::parse("000A25:Objects.esp")),
            fo4::schema::FormLinkListMutationStatus::updated);
  EXPECT_EQ(signature_list(plugin, "000900:Patch.esp"), "EDID,FNAM,INTV");

  const auto reread = fo4::read_plugin(fo4::write_plugin(plugin), "Patch.esp");
  EXPECT_EQ(signature_list(reread, "000900:Patch.esp"), "EDID,FNAM,INTV");
  EXPECT_EQ(sub_bytes(reread, "000900:Patch.esp", "INTV"), intv(1, 0));
}
