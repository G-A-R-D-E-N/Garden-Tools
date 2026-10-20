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

std::uint32_t raw_id(std::uint32_t local_id) { return 0x01000000u | local_id; }

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

std::string label_of(std::uint32_t value) {
  std::string out;
  for (int shift = 0; shift < 32; shift += 8) out.push_back(static_cast<char>(value >> shift));
  return out;
}

void write_actor(ByteWriter& out, std::uint32_t local, bool linked_pair) {
  const auto at = fo4::test::begin_record(out, "ACHR", 0, raw_id(local));
  fo4::test::write_subrecord(out, "NAME", u32_bytes(0x00000A00u));
  ByteWriter transform;
  for (const float value : {1.0f, 2.0f, 3.0f, 0.0f, 0.0f, 0.5f}) transform.f32(value);
  fo4::test::write_subrecord(out, "DATA", transform.bytes);
  ByteWriter linked_keyword;
  linked_keyword.u32(0x00000A90u);
  fo4::test::write_subrecord(out, "XLKR", linked_keyword.bytes);
  if (linked_pair) {
    ByteWriter linked_reference;
    linked_reference.u32(0x00000A91u);
    linked_reference.u32(0x00000A92u);
    fo4::test::write_subrecord(out, "XLKR", linked_reference.bytes);
  }
  fo4::test::write_subrecord(out, "XAPD", Bytes{std::byte{0x01}});
  ByteWriter activate_parent;
  activate_parent.u32(0x00000A93u);
  activate_parent.f32(1.25f);
  fo4::test::write_subrecord(out, "XAPR", activate_parent.bytes);
  ByteWriter spline;
  spline.u32(0x00000A94u);
  spline.u32(0);
  fo4::test::write_subrecord(out, "XPLK", spline.bytes);
  fo4::test::write_subrecord(out, "XLRT", u32_bytes(0x00000A95u));
  fo4::test::write_subrecord(out, "XATR", u32_bytes(0x00000A63u));
  fo4::test::write_subrecord(out, "XWCN", u32_bytes(1));
  ByteWriter velocities;
  for (const float value : {1.0f, 2.0f, 3.0f, 0.5f}) velocities.f32(value);
  fo4::test::write_subrecord(out, "XWCU", velocities.bytes);
  fo4::test::end_record(out, at);
}

std::vector<std::byte> make_plugin() {
  ByteWriter out;
  const auto tes4 = fo4::test::begin_record(out, "TES4", 0, 0);
  ByteWriter hedr;
  hedr.f32(1.0f);
  hedr.u32(0);
  hedr.u32(0x900);
  fo4::test::write_subrecord(out, "HEDR", hedr.bytes);
  fo4::test::write_subrecord(out, "MAST", std::string_view{"Fallout4.esm\0", 13});
  ByteWriter master_data;
  master_data.u32(0);
  master_data.u32(0);
  fo4::test::write_subrecord(out, "DATA", master_data.bytes);
  fo4::test::end_record(out, tes4);

  const auto cell_group = fo4::test::begin_group(out, "CELL", 0);
  const auto cell = fo4::test::begin_record(out, "CELL", 0, raw_id(0x800u));
  fo4::test::write_subrecord(out, "EDID", std::string_view{"Garden\0", 7});
  fo4::test::end_record(out, cell);
  const auto label = label_of(raw_id(0x800u));
  const auto children = fo4::test::begin_group(out, label, 6);
  const auto persistent = fo4::test::begin_group(out, label, 8);
  write_actor(out, 0x810u, true);
  fo4::test::end_group(out, persistent);
  const auto temporary = fo4::test::begin_group(out, label, 9);
  write_actor(out, 0x811u, false);
  fo4::test::end_group(out, temporary);
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

Bytes sub_bytes(const fo4::Plugin& plugin, std::uint32_t local, std::string_view signature) {
  for (const auto& sub : actor(plugin, local).subrecords) {
    if (sub.signature == fo4::Signature::from_string(signature)) {
      return Bytes(sub.data.begin(), sub.data.end());
    }
  }
  throw std::runtime_error("missing subrecord");
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

std::optional<std::uint32_t> link_raw(const fo4::Record& record, std::string_view path) {
  const auto field = fo4::schema::formlink_field_descriptor(k_achr, path);
  if (!field) return std::nullopt;
  for (const auto& sub : record.subrecords) {
    if (sub.signature != field->subrecord) continue;
    if (sub.data.size() != field->payload_sizes[0]) return std::nullopt;
    std::uint32_t value = 0;
    std::memcpy(&value, sub.data.data() + field->offset, sizeof(value));
    return value;
  }
  return std::nullopt;
}

std::optional<std::uint32_t> link_at(const fo4::Record& record,
                                     const fo4::schema::FormLinkFieldDescriptor& field,
                                     std::size_t index) {
  std::size_t seen = 0;
  for (const auto& sub : record.subrecords) {
    if (sub.signature != field.subrecord) continue;
    if (seen++ != index) continue;
    if (sub.data.size() < field.offset + 4) return std::nullopt;
    std::uint32_t value = 0;
    std::memcpy(&value, sub.data.data() + field.offset, sizeof(value));
    return value;
  }
  return std::nullopt;
}

}  // namespace

TEST(AchrExtraRows, WaterCurrentAndLocationRefTypeRowsAreAddressedAsXEditDoes) {
  const auto velocities =
      fo4::schema::counted_array_descriptor(k_achr, "ACHR\\WaterCurrentVelocities");
  ASSERT_TRUE(velocities.has_value());
  EXPECT_EQ(velocities->subrecord, fo4::Signature::from_string("XWCU"));
  EXPECT_EQ(velocities->count_subrecord, fo4::Signature::from_string("XWCN"));
  EXPECT_EQ(velocities->stride, 16u);
  const auto velocity_y = fo4::schema::scalar_field_descriptor(k_achr, "ACHR\\WaterCurrentVelocityY");
  ASSERT_TRUE(velocity_y.has_value());
  EXPECT_EQ(velocity_y->scalar.offset, 4u);
  EXPECT_EQ(velocity_y->scalar.address.stride, 16u);

  const auto ref_types = fo4::schema::formlink_list_descriptor(k_achr, "ACHR\\LocationRefTypes");
  ASSERT_TRUE(ref_types.has_value());
  EXPECT_EQ(ref_types->subrecord, fo4::Signature::from_string("XLRT"));
  EXPECT_EQ(ref_types->storage, fo4::schema::FormLinkListStorage::packed_array);
  EXPECT_TRUE(ref_types->nullable);
  EXPECT_TRUE(ref_types->can_insert);
  EXPECT_TRUE(fo4::schema::formlink_list_target_allowed(
      *ref_types, fo4::Signature::from_string("LCRT")));
  EXPECT_FALSE(fo4::schema::formlink_list_target_allowed(
      *ref_types, fo4::Signature::from_string("LCTN")));

  const auto spline = fo4::schema::formlink_field_descriptor(k_achr, "ACHR\\SplineConnection");
  ASSERT_TRUE(spline.has_value());
  EXPECT_EQ(spline->payload_sizes[0], 8u);
  EXPECT_EQ(spline->payload_sizes[1], 4u);
}

TEST(AchrExtraRows, LinkedActivateParentSplineAndVelocityRowsReadFromThePlugin) {
  const auto plugin = fo4::read_plugin(make_plugin(), "Patch.esp");
  const auto& full = actor(plugin, 0x810u);

  EXPECT_EQ(link_raw(full, "ACHR\\LinkedReferenceKeyword"), 0x00000A90u);
  const auto reference = fo4::schema::formlink_field_descriptor(k_achr, "ACHR\\LinkedReference");
  ASSERT_TRUE(reference.has_value());
  EXPECT_EQ(link_at(full, *reference, 1), 0x00000A92u);
  EXPECT_FALSE(link_at(full, *reference, 0).has_value());
  EXPECT_EQ(link_raw(full, "ACHR\\ActivateParent"), 0x00000A93u);
  EXPECT_EQ(link_raw(full, "ACHR\\SplineConnection"), 0x00000A94u);

  const auto only = scalar_bytes(full, "ACHR\\ParentActivateOnly");
  ASSERT_TRUE(only.has_value());
  ASSERT_EQ(only->size(), 1u);
  EXPECT_EQ(std::to_integer<std::uint8_t>((*only)[0]), 0x01);
  const auto delay = scalar_bytes(full, "ACHR\\ActivateParentDelay");
  ASSERT_TRUE(delay.has_value());
  EXPECT_EQ(Bytes(delay->begin(), delay->end()), f32_bytes(1.25f));

  const auto velocity_y = scalar_bytes(full, "ACHR\\WaterCurrentVelocityY");
  ASSERT_TRUE(velocity_y.has_value());
  EXPECT_EQ(Bytes(velocity_y->begin(), velocity_y->end()), f32_bytes(2.0f));
  const auto unnamed = scalar_bytes(full, "ACHR\\WaterCurrentUnnamed");
  ASSERT_TRUE(unnamed.has_value());
  EXPECT_EQ(Bytes(unnamed->begin(), unnamed->end()), f32_bytes(0.5f));
  const auto velocity_count = scalar_bytes(full, "ACHR\\WaterCurrentVelocityCount");
  ASSERT_TRUE(velocity_count.has_value());
  EXPECT_EQ(Bytes(velocity_count->begin(), velocity_count->end()), u32_bytes(1));

  const auto ref_types = fo4::schema::formlink_list_descriptor(k_achr, "ACHR\\LocationRefTypes");
  ASSERT_TRUE(ref_types.has_value());
  const auto values =
      fo4::schema::record_formlink_list_values(plugin, patch_key(0x810u), *ref_types);
  ASSERT_TRUE(values.has_value());
  ASSERT_EQ(values->size(), 1u);
  EXPECT_EQ((*values)[0], fo4::FormKey::parse("00000A95:Fallout4.esm"));
}

TEST(AchrExtraRows, ShortLinkedKeywordPayloadRefusesTheReferenceRowAndWritesLandInPlace) {
  auto plugin = fo4::read_plugin(make_plugin(), "Patch.esp");
  const auto keyword = fo4::schema::formlink_field_descriptor(k_achr, "ACHR\\LinkedReferenceKeyword");
  const auto reference = fo4::schema::formlink_field_descriptor(k_achr, "ACHR\\LinkedReference");
  const auto activate_only = fo4::schema::scalar_field_descriptor(k_achr, "ACHR\\ParentActivateOnly");
  const auto delay = fo4::schema::scalar_field_descriptor(k_achr, "ACHR\\ActivateParentDelay");
  ASSERT_TRUE(keyword.has_value());
  ASSERT_TRUE(reference.has_value());
  ASSERT_TRUE(activate_only.has_value());
  ASSERT_TRUE(delay.has_value());
  EXPECT_EQ(keyword->payload_sizes[0], 4u);
  EXPECT_EQ(keyword->payload_sizes[1], 8u);
  ASSERT_EQ(sub_bytes(plugin, 0x811u, "XLKR").size(), 4u);

  EXPECT_THROW(fo4::set_record_formlink_field(plugin, patch_key(0x811u), *reference,
                                              fo4::FormKey::parse("00000A96:Fallout4.esm")),
               fo4::schema::FormLinkFieldError);
  EXPECT_EQ(fo4::set_record_formlink_field(plugin, patch_key(0x811u), *keyword,
                                           fo4::FormKey::parse("00000A96:Fallout4.esm")),
            fo4::FormLinkFieldMutationStatus::updated);
  EXPECT_EQ(fo4::set_record_scalar(plugin, patch_key(0x811u), *activate_only, "0"),
            fo4::ScalarMutationStatus::updated);
  EXPECT_EQ(fo4::set_record_scalar(plugin, patch_key(0x811u), *delay, "2.5"),
            fo4::ScalarMutationStatus::updated);

  const auto reread = fo4::read_plugin(fo4::write_plugin(plugin), "Patch.esp");
  const auto& full = actor(reread, 0x811u);
  const auto activate_parent = sub_bytes(reread, 0x811u, "XAPR");
  ASSERT_EQ(activate_parent.size(), 8u);
  EXPECT_EQ(std::to_integer<std::uint8_t>(activate_parent[0]), 0x93);
  EXPECT_EQ(Bytes(activate_parent.begin() + 4, activate_parent.end()), f32_bytes(2.5f));
  EXPECT_EQ(link_raw(full, "ACHR\\ActivateParent"), 0x00000A93u);
  const auto activate_only_bytes = scalar_bytes(full, "ACHR\\ParentActivateOnly");
  ASSERT_TRUE(activate_only_bytes.has_value());
  ASSERT_EQ(activate_only_bytes->size(), 1u);
  EXPECT_EQ(std::to_integer<std::uint8_t>((*activate_only_bytes)[0]), 0x00);
  const auto linked = sub_bytes(reread, 0x811u, "XLKR");
  ASSERT_EQ(linked.size(), 4u);
  EXPECT_EQ(Bytes(linked.begin(), linked.end()), u32_bytes(0x00000A96u));
  EXPECT_EQ(link_raw(full, "ACHR\\LinkedReferenceKeyword"), 0x00000A96u);
  EXPECT_EQ(link_raw(full, "ACHR\\SplineConnection"), 0x00000A94u);
  EXPECT_EQ(link_raw(full, "ACHR\\AttachRef"), 0x00000A63u);
  EXPECT_EQ(sub_bytes(reread, 0x811u, "XWCN"), u32_bytes(1));
}
