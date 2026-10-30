#include "fo4/plugin/mutation.hpp"
#include "fo4/plugin/reader.hpp"
#include "fo4/plugin/writer.hpp"
#include "fo4/schema/formlink_lists.hpp"
#include "fo4/schema/scalar.hpp"
#include "plugin_bytes.hpp"

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {

using Bytes = std::vector<std::byte>;
using fo4::test::ByteWriter;

const fo4::Signature k_achr = fo4::Signature::from_string("ACHR");

std::string label_of(std::uint32_t value) {
  std::string out;
  for (int shift = 0; shift < 32; shift += 8) out.push_back(static_cast<char>(value >> shift));
  return out;
}

void write_actor(ByteWriter& out, std::uint32_t local, std::size_t parent_bytes,
                 std::size_t link_bytes = 8) {
  const auto at = fo4::test::begin_record(out, "ACHR", 0, 0x01000000u | local);
  fo4::test::write_subrecord(out, "EDID", std::string_view{"Actor\0", 6});
  fo4::test::write_subrecord(out, "XAPD", Bytes{std::byte{1}});
  ByteWriter parent;
  parent.u32(0x00000A40u);
  parent.f32(1.5f);
  if (parent_bytes < parent.bytes.size()) parent.bytes.resize(parent_bytes);
  fo4::test::write_subrecord(out, "XAPR", parent.bytes);
  ByteWriter second_parent;
  second_parent.u32(0x00000A41u);
  second_parent.f32(-0.25f);
  fo4::test::write_subrecord(out, "XAPR", second_parent.bytes);
  ByteWriter link;
  link.u32(0x00000A50u);
  link.u32(0x00000A51u);
  if (link_bytes < link.bytes.size()) link.bytes.resize(link_bytes);
  fo4::test::write_subrecord(out, "XLKR", link.bytes);
  ByteWriter second_link;
  second_link.u32(0x00000A52u);
  second_link.u32(0x00000A53u);
  if (link_bytes < second_link.bytes.size()) second_link.bytes.resize(link_bytes);
  fo4::test::write_subrecord(out, "XLKR", second_link.bytes);
  ByteWriter ref_types;
  ref_types.u32(0x00000A60u);
  ref_types.u32(0x00000A61u);
  fo4::test::write_subrecord(out, "XLRT", ref_types.bytes);
  fo4::test::write_subrecord(out, "MNAM", std::string_view{"Garden comment\0", 15});
  fo4::test::end_record(out, at);
}

std::vector<std::byte> make_plugin(std::size_t parent_bytes = 8, std::size_t link_bytes = 8) {
  ByteWriter out;
  out.bytes = fo4::test::make_tes4_plugin(0, 1.0f, {"Fallout4.esm"});
  const auto cell_group = fo4::test::begin_group(out, "CELL", 0);
  const auto cell = fo4::test::begin_record(out, "CELL", 0, 0x01000000u | 0x800u);
  fo4::test::write_subrecord(out, "EDID", std::string_view{"Garden\0", 7});
  fo4::test::end_record(out, cell);
  const auto label = label_of(0x01000000u | 0x800u);
  const auto children = fo4::test::begin_group(out, label, 6);
  const auto persistent = fo4::test::begin_group(out, label, 8);
  write_actor(out, 0x810u, parent_bytes, link_bytes);
  fo4::test::end_group(out, persistent);
  fo4::test::end_group(out, children);
  fo4::test::end_group(out, cell_group);
  return out.bytes;
}

fo4::FormKey patch_key(std::uint32_t local) {
  return fo4::FormKey{fo4::PluginKey::from_filename("Patch.esp"), fo4::LocalFormId{local}};
}

const fo4::Record& actor(const fo4::Plugin& plugin, std::uint32_t local) {
  const auto* record = fo4::find_record(plugin, patch_key(local));
  if (record == nullptr) throw std::runtime_error("missing ACHR");
  return *record;
}

std::vector<std::span<const std::byte>> payloads(const fo4::Record& record,
                                                 std::string_view signature) {
  std::vector<std::span<const std::byte>> out;
  for (const auto& sub : record.subrecords) {
    if (sub.signature == fo4::Signature::from_string(signature)) out.push_back(sub.data);
  }
  return out;
}

std::optional<std::span<const std::byte>> scalar_bytes(const fo4::Record& record,
                                                       std::string_view path) {
  const auto field = fo4::schema::scalar_field_descriptor(k_achr, path);
  if (!field) return std::nullopt;
  for (const auto& sub : record.subrecords) {
    if (sub.signature != field->scalar.subrecord) continue;
    return fo4::schema::scalar_field_bytes(sub.data, field->scalar);
  }
  return std::nullopt;
}

std::optional<std::uint32_t> link_at(const fo4::Record& record, std::string_view path,
                                     std::size_t index) {
  const auto field = fo4::schema::formlink_field_descriptor(k_achr, path);
  if (!field) return std::nullopt;
  std::size_t seen = 0;
  for (const auto& sub : record.subrecords) {
    if (sub.signature != field->subrecord) continue;
    if (sub.data.size() != field->payload_sizes[0] &&
        sub.data.size() != field->payload_sizes[1]) {
      continue;
    }
    if (seen++ != index) continue;
    std::uint32_t value = 0;
    std::memcpy(&value, sub.data.data() + field->offset, sizeof(value));
    return value;
  }
  return std::nullopt;
}

float f32_of(std::span<const std::byte> bytes) {
  float value = 0.0f;
  std::memcpy(&value, bytes.data(), sizeof(value));
  return value;
}

}  // namespace

TEST(AchrParityRows, ParentActivateOnlyAndCommentsDecodeFromTheirOwnSubrecords) {
  auto plugin = fo4::read_plugin(make_plugin(), "Patch.esp");
  const auto& full = actor(plugin, 0x810u);

  const auto parent_flag = fo4::schema::scalar_field_descriptor(k_achr, "ACHR\\ParentActivateOnly");
  ASSERT_TRUE(parent_flag.has_value());
  EXPECT_EQ(parent_flag->scalar.subrecord, fo4::Signature::from_string("XAPD"));
  EXPECT_EQ(parent_flag->scalar.storage, fo4::schema::ScalarStorage::u8);
  EXPECT_EQ(parent_flag->scalar.presentation, fo4::schema::ScalarPresentation::enumeration);
  ASSERT_EQ(parent_flag->scalar.names.size(), 2u);
  EXPECT_EQ(parent_flag->scalar.names[0].value, 0);
  EXPECT_EQ(parent_flag->scalar.names[0].name, "False");
  EXPECT_EQ(parent_flag->scalar.names[1].value, 1);
  EXPECT_EQ(parent_flag->scalar.names[1].name, "True");
  EXPECT_FALSE(parent_flag->scalar.address.repeated);

  const auto flag_bytes = scalar_bytes(full, "ACHR\\ParentActivateOnly");
  ASSERT_TRUE(flag_bytes.has_value());
  ASSERT_EQ(flag_bytes->size(), 1u);
  EXPECT_EQ(std::to_integer<std::uint8_t>((*flag_bytes)[0]), 1);

  const auto comments = fo4::schema::scalar_field_descriptor(k_achr, "ACHR\\Comments");
  ASSERT_TRUE(comments.has_value());
  EXPECT_EQ(comments->scalar.subrecord, fo4::Signature::from_string("MNAM"));
  EXPECT_EQ(comments->scalar.storage, fo4::schema::ScalarStorage::zstring);
  const auto comment_bytes = scalar_bytes(full, "ACHR\\Comments");
  ASSERT_TRUE(comment_bytes.has_value());
  ASSERT_EQ(comment_bytes->size(), 15u);
  EXPECT_EQ(std::string(reinterpret_cast<const char*>(comment_bytes->data())), "Garden comment");
}

TEST(AchrParityRows, ActivateParentLinkAndDelayShareEachEightByteEntry) {
  auto plugin = fo4::read_plugin(make_plugin(), "Patch.esp");
  const auto& full = actor(plugin, 0x810u);

  const auto link = fo4::schema::formlink_field_descriptor(k_achr, "ACHR\\ActivateParent");
  ASSERT_TRUE(link.has_value());
  EXPECT_EQ(link->subrecord, fo4::Signature::from_string("XAPR"));
  EXPECT_EQ(link->offset, 0u);
  EXPECT_EQ(link->payload_sizes[0], 8u);
  EXPECT_TRUE(link->address.repeated);
  EXPECT_TRUE(fo4::schema::formlink_field_target_allowed(*link, fo4::Signature::from_string("REFR")));

  const auto delay = fo4::schema::scalar_field_descriptor(k_achr, "ACHR\\ActivateParentDelay");
  ASSERT_TRUE(delay.has_value());
  EXPECT_EQ(delay->scalar.subrecord, fo4::Signature::from_string("XAPR"));
  EXPECT_EQ(delay->scalar.offset, 4u);
  EXPECT_EQ(delay->scalar.storage, fo4::schema::ScalarStorage::f32);
  EXPECT_TRUE(delay->scalar.address.repeated);

  EXPECT_EQ(link_at(full, "ACHR\\ActivateParent", 0), 0x00000A40u);
  EXPECT_EQ(link_at(full, "ACHR\\ActivateParent", 1), 0x00000A41u);
  EXPECT_EQ(f32_of(*scalar_bytes(full, "ACHR\\ActivateParentDelay")), 1.5f);

  const auto truncated = fo4::read_plugin(make_plugin(4), "Patch.esp");
  const auto& short_actor = actor(truncated, 0x810u);
  const auto short_delay = scalar_bytes(short_actor, "ACHR\\ActivateParentDelay");
  ASSERT_TRUE(short_delay.has_value());
  EXPECT_TRUE(short_delay->empty());
  EXPECT_EQ(payloads(short_actor, "XAPR").size(), 2u);
}

TEST(AchrParityRows, LinkedReferenceKeywordAndReferenceShareEachEightByteEntry) {
  auto plugin = fo4::read_plugin(make_plugin(), "Patch.esp");
  const auto& full = actor(plugin, 0x810u);

  const auto keyword = fo4::schema::formlink_field_descriptor(k_achr, "ACHR\\LinkedReferenceKeyword");
  ASSERT_TRUE(keyword.has_value());
  EXPECT_EQ(keyword->subrecord, fo4::Signature::from_string("XLKR"));
  EXPECT_EQ(keyword->offset, 0u);
  EXPECT_EQ(keyword->payload_sizes[0], 4u);
  EXPECT_EQ(keyword->payload_sizes[1], 8u);
  EXPECT_TRUE(keyword->address.repeated);
  EXPECT_TRUE(
      fo4::schema::formlink_field_target_allowed(*keyword, fo4::Signature::from_string("KYWD")));

  const auto reference = fo4::schema::formlink_field_descriptor(k_achr, "ACHR\\LinkedReference");
  ASSERT_TRUE(reference.has_value());
  EXPECT_EQ(reference->offset, 4u);
  EXPECT_EQ(reference->payload_sizes[0], 8u);
  EXPECT_TRUE(reference->address.repeated);
  EXPECT_TRUE(fo4::schema::formlink_field_target_allowed(*reference,
                                                         fo4::Signature::from_string("REFR")));

  EXPECT_EQ(link_at(full, "ACHR\\LinkedReferenceKeyword", 0), 0x00000A50u);
  EXPECT_EQ(link_at(full, "ACHR\\LinkedReference", 0), 0x00000A51u);
  EXPECT_EQ(link_at(full, "ACHR\\LinkedReferenceKeyword", 1), 0x00000A52u);
  EXPECT_EQ(link_at(full, "ACHR\\LinkedReference", 1), 0x00000A53u);
}

TEST(AchrParityRows, FourByteLinkedReferenceKeepsTheKeywordSlot) {
  auto plugin = fo4::read_plugin(make_plugin(8, 4), "Patch.esp");
  const auto& full = actor(plugin, 0x810u);

  EXPECT_EQ(link_at(full, "ACHR\\LinkedReferenceKeyword", 0), 0x00000A50u);
  EXPECT_EQ(link_at(full, "ACHR\\LinkedReferenceKeyword", 1), 0x00000A52u);
  EXPECT_FALSE(link_at(full, "ACHR\\LinkedReference", 0).has_value());
  EXPECT_EQ(fo4::record_field_count(plugin, patch_key(0x810u), "ACHR\\LinkedReferenceKeyword"),
            2u);
}

TEST(AchrParityRows, LocationRefTypesAreAPackedArrayOfFourByteRefs) {
  auto plugin = fo4::read_plugin(make_plugin(), "Patch.esp");
  const auto list = fo4::schema::formlink_list_descriptor(k_achr, "ACHR\\LocationRefTypes");
  ASSERT_TRUE(list.has_value());
  EXPECT_EQ(list->subrecord, fo4::Signature::from_string("XLRT"));
  EXPECT_EQ(list->storage, fo4::schema::FormLinkListStorage::packed_array);
  EXPECT_TRUE(fo4::schema::formlink_list_target_allowed(*list, fo4::Signature::from_string("LCRT")));

  const auto size = fo4::schema::record_formlink_list_size(plugin, patch_key(0x810u), *list);
  ASSERT_TRUE(size.has_value());
  EXPECT_EQ(*size, 2u);
  const auto values = fo4::schema::record_formlink_list_values(plugin, patch_key(0x810u), *list);
  ASSERT_TRUE(values.has_value());
  ASSERT_EQ(values->size(), 2u);
  EXPECT_EQ(values->at(0).local().value(), 0x00000A60u);
  EXPECT_EQ(values->at(1).local().value(), 0x00000A61u);

  EXPECT_EQ(fo4::schema::set_record_formlink_list_element(
                plugin, patch_key(0x810u), *list, 1,
                fo4::FormKey::parse("00000A70:Fallout4.esm")),
            fo4::schema::FormLinkListMutationStatus::updated);
  EXPECT_EQ(fo4::schema::set_record_formlink_list_element(
                plugin, patch_key(0x810u), *list, 2,
                fo4::FormKey::parse("00000A71:Fallout4.esm")),
            fo4::schema::FormLinkListMutationStatus::index_out_of_range);

  const auto reread = fo4::read_plugin(fo4::write_plugin(plugin), "Patch.esp");
  const auto written = fo4::schema::record_formlink_list_values(reread, patch_key(0x810u), *list);
  ASSERT_TRUE(written.has_value());
  ASSERT_EQ(written->size(), 2u);
  EXPECT_EQ(written->at(0).local().value(), 0x00000A60u);
  EXPECT_EQ(written->at(1).local().value(), 0x00000A70u);
  const auto payload = payloads(actor(reread, 0x810u), "XLRT");
  ASSERT_EQ(payload.size(), 1u);
  EXPECT_EQ(payload[0].size(), 8u);
}

TEST(AchrParityRows, WritesTheParentFlagAndDelayThenRereads) {
  auto plugin = fo4::read_plugin(make_plugin(), "Patch.esp");
  const auto flag = fo4::schema::scalar_field_descriptor(k_achr, "ACHR\\ParentActivateOnly");
  const auto delay = fo4::schema::scalar_field_descriptor(k_achr, "ACHR\\ActivateParentDelay");
  const auto link = fo4::schema::formlink_field_descriptor(k_achr, "ACHR\\ActivateParent");
  ASSERT_TRUE(flag.has_value());
  ASSERT_TRUE(delay.has_value());
  ASSERT_TRUE(link.has_value());

  EXPECT_EQ(fo4::set_record_scalar(plugin, patch_key(0x810u), *flag, "0"),
            fo4::ScalarMutationStatus::updated);
  EXPECT_EQ(fo4::set_record_scalar(plugin, patch_key(0x810u), *delay, "2.5"),
            fo4::ScalarMutationStatus::updated);
  EXPECT_THROW(fo4::set_record_scalar(plugin, patch_key(0x810u), *flag, "256"),
               fo4::schema::ScalarError);
  EXPECT_EQ(fo4::set_record_formlink_field(plugin, patch_key(0x810u), *link,
                                           fo4::FormKey::parse("00000A90:Fallout4.esm")),
            fo4::FormLinkFieldMutationStatus::updated);
  EXPECT_THROW(fo4::set_record_formlink_field(plugin, patch_key(0x810u), *link,
                                              fo4::FormKey::parse("00000800:Patch.esp")),
               fo4::schema::FormLinkFieldError);

  const auto reread = fo4::read_plugin(fo4::write_plugin(plugin), "Patch.esp");
  const auto& written = actor(reread, 0x810u);
  const auto flag_bytes = scalar_bytes(written, "ACHR\\ParentActivateOnly");
  ASSERT_TRUE(flag_bytes.has_value());
  ASSERT_EQ(flag_bytes->size(), 1u);
  EXPECT_EQ(std::to_integer<std::uint8_t>((*flag_bytes)[0]), 0);
  EXPECT_EQ(f32_of(*scalar_bytes(written, "ACHR\\ActivateParentDelay")), 2.5f);
  EXPECT_EQ(link_at(written, "ACHR\\ActivateParent", 0), 0x00000A90u);
  EXPECT_EQ(link_at(written, "ACHR\\ActivateParent", 1), 0x00000A41u);
  EXPECT_EQ(payloads(written, "XAPD").size(), 1u);
  EXPECT_EQ(payloads(written, "XAPR").size(), 2u);
}
