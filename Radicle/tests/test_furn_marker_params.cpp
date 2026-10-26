#include "fo4/plugin/mutation.hpp"
#include "fo4/plugin/reader.hpp"
#include "fo4/plugin/writer.hpp"
#include "fo4/schema/formlink_lists.hpp"
#include "fo4/schema/scalar.hpp"
#include "plugin_bytes.hpp"

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace {

using Bytes = std::vector<std::byte>;
using fo4::test::ByteWriter;

constexpr auto k_snam = fo4::Signature::from_string("SNAM");
constexpr auto k_kywd = fo4::Signature::from_string("KYWD");

fo4::FormKey furn_key() { return fo4::FormKey::parse("000800:Patch.esp"); }
fo4::FormKey kywd_key() { return fo4::FormKey::parse("000900:Patch.esp"); }

fo4::schema::ScalarFieldDescriptor scalar(std::string_view path) {
  return fo4::schema::scalar_field_descriptor(path).value();
}

fo4::schema::FormLinkFieldDescriptor link(std::string_view path) {
  return fo4::schema::formlink_field_descriptor(path).value();
}

std::uint32_t read_u32(const Bytes& data, std::size_t offset) {
  std::uint32_t value = 0;
  for (std::size_t i = 0; i < 4; ++i) {
    value |= static_cast<std::uint32_t>(std::to_integer<std::uint8_t>(data[offset + i])) << (8 * i);
  }
  return value;
}

Bytes snam(std::size_t elements) {
  return Bytes(elements * 24, std::byte{0});
}

Bytes snam_legacy(std::size_t elements) {
  return Bytes(elements * 20, std::byte{0});
}

std::vector<std::byte> make_plugin(const Bytes& marker_params, std::uint16_t form_version = 131) {
  ByteWriter out;
  const auto tes4 = fo4::test::begin_record(out, "TES4", 0, 0);
  ByteWriter hedr;
  hedr.f32(1.0f);
  hedr.u32(1);
  hedr.u32(0x911);
  fo4::test::write_subrecord(out, "HEDR", hedr.bytes);
  fo4::test::end_record(out, tes4);

  const auto group = fo4::test::begin_group(out, "FURN", 0);
  const auto record = fo4::test::begin_record(out, "FURN", 0, 0x000800, form_version);
  fo4::test::write_subrecord(out, "SNAM", marker_params);
  fo4::test::end_record(out, record);
  fo4::test::end_group(out, group);

  const auto keyword_group = fo4::test::begin_group(out, "KYWD", 0);
  const auto keyword = fo4::test::begin_record(out, "KYWD", 0, 0x000900, form_version);
  fo4::test::end_record(out, keyword);
  fo4::test::end_group(out, keyword_group);
  return out.bytes;
}

fo4::Plugin reparse(const fo4::Plugin& plugin) {
  return fo4::read_plugin(fo4::write_plugin(plugin), "Patch.esp");
}

const Bytes* payload_of(const fo4::Plugin& plugin, fo4::Signature signature) {
  const auto* record = fo4::find_record(plugin, furn_key());
  if (record == nullptr) return nullptr;
  for (const auto& subrecord : record->subrecords) {
    if (subrecord.signature == signature) return &subrecord.data;
  }
  return nullptr;
}

std::string read_member(const fo4::Plugin& plugin, std::string_view path, std::size_t index) {
  const auto field = scalar(path);
  const auto* record = fo4::find_record(plugin, furn_key());
  if (record == nullptr) return "absent";
  const auto resolved = fo4::schema::resolve_field_slots(
      *record, fo4::schema::scalar_field_shape(field.scalar), field.scalar.address);
  if (index >= resolved.slots.size() || !resolved.slots[index].valid) return "absent";
  const auto slot = resolved.slots[index];
  return fo4::schema::decode_scalar(
      fo4::schema::scalar_slot_bytes(record->subrecords[slot.subrecord].data, slot, field.scalar),
      field.scalar);
}

}  // namespace

TEST(FurnMarkerParams, RowsDeclareStridedElements) {
  const auto offset_x = scalar("FURN\\MarkerOffsetX");
  EXPECT_EQ(offset_x.scalar.subrecord, k_snam);
  EXPECT_EQ(offset_x.scalar.offset, 0u);
  EXPECT_EQ(offset_x.scalar.address.stride, 24u);
  EXPECT_FALSE(offset_x.scalar.address.repeated);
  EXPECT_EQ(offset_x.scalar.min_form_version, 125u);
  EXPECT_EQ(offset_x.scalar.storage, fo4::schema::ScalarStorage::f32);

  EXPECT_EQ(scalar("FURN\\MarkerOffsetY").scalar.offset, 4u);
  EXPECT_EQ(scalar("FURN\\MarkerOffsetZ").scalar.offset, 8u);
  EXPECT_EQ(scalar("FURN\\MarkerRotationZ").scalar.offset, 12u);
  EXPECT_EQ(scalar("FURN\\MarkerRotationZ").scalar.address.stride, 24u);

  const auto entry_types = scalar("FURN\\MarkerEntryTypes");
  EXPECT_EQ(entry_types.scalar.offset, 20u);
  EXPECT_EQ(entry_types.scalar.storage, fo4::schema::ScalarStorage::u8);
  EXPECT_EQ(entry_types.scalar.presentation, fo4::schema::ScalarPresentation::flags);
  EXPECT_EQ(entry_types.scalar.address.stride, 24u);
  EXPECT_EQ(entry_types.scalar.min_form_version, 125u);
  const auto rear = fo4::schema::scalar_value_name(entry_types.scalar, 1 << 1);
  ASSERT_TRUE(rear.has_value());
  EXPECT_EQ(*rear, "Rear");
  EXPECT_FALSE(fo4::schema::scalar_value_name(entry_types.scalar, 1 << 8).has_value());

  const auto keyword = link("FURN\\MarkerKeyword");
  EXPECT_EQ(keyword.subrecord, k_snam);
  EXPECT_EQ(keyword.offset, 16u);
  EXPECT_EQ(keyword.address.stride, 24u);
  EXPECT_FALSE(keyword.address.repeated);
  EXPECT_TRUE(keyword.nullable);
  EXPECT_TRUE(fo4::schema::formlink_field_target_allowed(keyword, k_kywd));
  EXPECT_FALSE(fo4::schema::formlink_field_target_allowed(keyword, k_snam));
}

TEST(FurnMarkerParams, LegacyRowsDeclareATwentyByteStrideBelowFormVersion125) {
  constexpr std::string_view paths[] = {"FURN\\MarkerOffsetXBelow125", "FURN\\MarkerOffsetYBelow125",
                                       "FURN\\MarkerOffsetZBelow125",
                                       "FURN\\MarkerRotationZBelow125"};
  constexpr std::size_t offsets[] = {0, 4, 8, 12};
  for (std::size_t i = 0; i < 4; ++i) {
    SCOPED_TRACE(paths[i]);
    const auto field = scalar(paths[i]);
    EXPECT_EQ(field.scalar.subrecord, k_snam);
    EXPECT_EQ(field.scalar.offset, offsets[i]);
    EXPECT_EQ(field.scalar.address.stride, 20u);
    EXPECT_FALSE(field.scalar.address.repeated);
    EXPECT_EQ(field.scalar.min_form_version, 0u);
    EXPECT_EQ(field.scalar.max_form_version, 125u);
    EXPECT_EQ(field.scalar.storage, fo4::schema::ScalarStorage::f32);
  }

  const auto legacy_keyword = link("FURN\\MarkerKeywordBelow125");
  EXPECT_EQ(legacy_keyword.subrecord, k_snam);
  EXPECT_EQ(legacy_keyword.offset, 16u);
  EXPECT_EQ(legacy_keyword.address.stride, 20u);
  EXPECT_FALSE(legacy_keyword.address.repeated);
  EXPECT_TRUE(legacy_keyword.nullable);
  EXPECT_EQ(legacy_keyword.min_form_version, 0u);
  EXPECT_EQ(legacy_keyword.max_form_version, 125u);
  EXPECT_TRUE(fo4::schema::formlink_field_target_allowed(legacy_keyword, k_kywd));
  EXPECT_FALSE(fo4::schema::formlink_field_target_allowed(legacy_keyword, k_snam));
}

TEST(FurnMarkerParams, ElementsReadAndWriteAtTheirOwnOffsets) {
  auto plugin = fo4::read_plugin(make_plugin(snam(2)), "Patch.esp");
  EXPECT_EQ(read_member(plugin, "FURN\\MarkerOffsetX", 0), "0");
  EXPECT_EQ(read_member(plugin, "FURN\\MarkerOffsetX", 1), "0");
  EXPECT_EQ(read_member(plugin, "FURN\\MarkerEntryTypes", 1), "0");
  EXPECT_EQ(read_member(plugin, "FURN\\MarkerEntryTypes", 2), "absent");

  EXPECT_EQ(fo4::set_record_scalar(plugin, furn_key(), scalar("FURN\\MarkerOffsetX"), 1, "2.5"),
            fo4::ScalarMutationStatus::updated);
  EXPECT_EQ(fo4::set_record_scalar(plugin, furn_key(), scalar("FURN\\MarkerRotationZ"), 0, "-90"),
            fo4::ScalarMutationStatus::updated);
  EXPECT_EQ(fo4::set_record_scalar(plugin, furn_key(), scalar("FURN\\MarkerEntryTypes"), 1, "11"),
            fo4::ScalarMutationStatus::updated);
  EXPECT_EQ(fo4::set_record_formlink_field(plugin, furn_key(), link("FURN\\MarkerKeyword"), 1, kywd_key()),
            fo4::FormLinkFieldMutationStatus::updated);

  const auto after = reparse(plugin);
  const auto* params = payload_of(after, k_snam);
  ASSERT_NE(params, nullptr);
  ASSERT_EQ(params->size(), 48u);
  EXPECT_EQ(read_u32(*params, 24), 0x40200000u);
  EXPECT_EQ(read_u32(*params, 12), 0xC2B40000u);
  EXPECT_EQ(std::to_integer<std::uint8_t>((*params)[44]), 11);
  EXPECT_EQ(read_u32(*params, 40), 0x00000900u);
  EXPECT_EQ(read_u32(*params, 0), 0x00000000u);
  EXPECT_EQ(read_u32(*params, 32), 0x00000000u);
  EXPECT_EQ(read_member(after, "FURN\\MarkerOffsetX", 1), "2.5");
  EXPECT_EQ(read_member(after, "FURN\\MarkerRotationZ", 0), "-90");
  EXPECT_EQ(read_member(after, "FURN\\MarkerEntryTypes", 1), "11");
}

TEST(FurnMarkerParams, RaggedAndMismatchedPayloadsHideEveryMarkerParam) {
  auto mismatched = fo4::read_plugin(make_plugin(snam(2), 124), "Patch.esp");
  EXPECT_EQ(read_member(mismatched, "FURN\\MarkerOffsetX", 0), "absent");
  EXPECT_EQ(fo4::set_record_scalar(mismatched, furn_key(), scalar("FURN\\MarkerOffsetX"), 0, "1"),
            fo4::ScalarMutationStatus::subrecord_missing);
  EXPECT_EQ(read_member(mismatched, "FURN\\MarkerOffsetXBelow125", 0), "absent");
  EXPECT_EQ(fo4::set_record_scalar(mismatched, furn_key(), scalar("FURN\\MarkerOffsetXBelow125"), 0, "1"),
            fo4::ScalarMutationStatus::subrecord_missing);

  auto short_element = fo4::read_plugin(make_plugin(snam(1), 125), "Patch.esp");
  EXPECT_EQ(read_member(short_element, "FURN\\MarkerOffsetX", 0), "0");
  EXPECT_EQ(read_member(short_element, "FURN\\MarkerEntryTypes", 0), "0");
  EXPECT_EQ(fo4::set_record_scalar(short_element, furn_key(), scalar("FURN\\MarkerEntryTypes"), 1, "1"),
            fo4::ScalarMutationStatus::subrecord_missing);

  auto ragged = fo4::read_plugin(make_plugin(Bytes(32, std::byte{0})), "Patch.esp");
  EXPECT_EQ(read_member(ragged, "FURN\\MarkerOffsetX", 0), "absent");
  EXPECT_EQ(fo4::set_record_scalar(ragged, furn_key(), scalar("FURN\\MarkerOffsetX"), 0, "1"),
            fo4::ScalarMutationStatus::subrecord_missing);
  EXPECT_EQ(fo4::set_record_formlink_field(ragged, furn_key(), link("FURN\\MarkerKeyword"), 0, kywd_key()),
            fo4::FormLinkFieldMutationStatus::subrecord_missing);
}

TEST(FurnMarkerParams, LegacyTwentyByteElementsReadAndWriteBelowFormVersion125) {
  auto plugin = fo4::read_plugin(make_plugin(snam_legacy(2), 124), "Patch.esp");
  EXPECT_EQ(read_member(plugin, "FURN\\MarkerOffsetXBelow125", 0), "0");
  EXPECT_EQ(read_member(plugin, "FURN\\MarkerOffsetXBelow125", 1), "0");
  EXPECT_EQ(read_member(plugin, "FURN\\MarkerRotationZBelow125", 1), "0");
  EXPECT_EQ(read_member(plugin, "FURN\\MarkerOffsetXBelow125", 2), "absent");
  EXPECT_EQ(read_member(plugin, "FURN\\MarkerOffsetX", 0), "absent");
  EXPECT_EQ(read_member(plugin, "FURN\\MarkerEntryTypes", 0), "absent");
  EXPECT_EQ(fo4::set_record_scalar(plugin, furn_key(), scalar("FURN\\MarkerEntryTypes"), 0, "1"),
            fo4::ScalarMutationStatus::subrecord_missing);

  EXPECT_EQ(fo4::set_record_scalar(plugin, furn_key(), scalar("FURN\\MarkerOffsetXBelow125"), 1, "2.5"),
            fo4::ScalarMutationStatus::updated);
  EXPECT_EQ(fo4::set_record_scalar(plugin, furn_key(), scalar("FURN\\MarkerRotationZBelow125"), 0, "-90"),
            fo4::ScalarMutationStatus::updated);
  EXPECT_EQ(fo4::set_record_formlink_field(plugin, furn_key(),
                                           link("FURN\\MarkerKeywordBelow125"), 1, kywd_key()),
            fo4::FormLinkFieldMutationStatus::updated);

  const auto after = reparse(plugin);
  const auto* params = payload_of(after, k_snam);
  ASSERT_NE(params, nullptr);
  ASSERT_EQ(params->size(), 40u);
  EXPECT_EQ(read_u32(*params, 20), 0x40200000u);
  EXPECT_EQ(read_u32(*params, 12), 0xC2B40000u);
  EXPECT_EQ(read_u32(*params, 36), 0x00000900u);
  EXPECT_EQ(read_u32(*params, 16), 0x00000000u);
  EXPECT_EQ(read_u32(*params, 0), 0x00000000u);
  EXPECT_EQ(read_member(after, "FURN\\MarkerOffsetXBelow125", 1), "2.5");
  EXPECT_EQ(read_member(after, "FURN\\MarkerRotationZBelow125", 0), "-90");
}

TEST(FurnMarkerParams, EachMarkerLayoutHidesOutsideItsFormVersionWindow) {
  auto modern = fo4::read_plugin(make_plugin(snam(1), 125), "Patch.esp");
  EXPECT_EQ(read_member(modern, "FURN\\MarkerOffsetX", 0), "0");
  EXPECT_EQ(read_member(modern, "FURN\\MarkerOffsetXBelow125", 0), "absent");
  EXPECT_EQ(fo4::set_record_scalar(modern, furn_key(), scalar("FURN\\MarkerOffsetXBelow125"), 0, "1"),
            fo4::ScalarMutationStatus::subrecord_missing);

  auto legacy = fo4::read_plugin(make_plugin(snam_legacy(1), 124), "Patch.esp");
  EXPECT_EQ(read_member(legacy, "FURN\\MarkerOffsetXBelow125", 0), "0");
  EXPECT_EQ(read_member(legacy, "FURN\\MarkerOffsetX", 0), "absent");
  EXPECT_EQ(fo4::set_record_scalar(legacy, furn_key(), scalar("FURN\\MarkerOffsetX"), 0, "1"),
            fo4::ScalarMutationStatus::subrecord_missing);
  EXPECT_EQ(fo4::set_record_formlink_field(legacy, furn_key(), link("FURN\\MarkerKeyword"), 0, kywd_key()),
            fo4::FormLinkFieldMutationStatus::subrecord_missing);
}

TEST(FurnMarkerParams, WrongKeywordTargetAndOutOfRangeIndexAreRefused) {
  auto plugin = fo4::read_plugin(make_plugin(snam(1)), "Patch.esp");
  EXPECT_THROW(fo4::set_record_formlink_field(plugin, furn_key(), link("FURN\\MarkerKeyword"), 0, furn_key()),
               fo4::schema::FormLinkFieldError);
  EXPECT_EQ(fo4::set_record_formlink_field(plugin, furn_key(), link("FURN\\MarkerKeyword"), 1, kywd_key()),
            fo4::FormLinkFieldMutationStatus::subrecord_missing);
  EXPECT_THROW(fo4::set_record_scalar(plugin, furn_key(), scalar("FURN\\MarkerEntryTypes"), 0, "256"),
               fo4::schema::ScalarError);
}
