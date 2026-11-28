#include "fo4/plugin/mutation.hpp"
#include "fo4/plugin/reader.hpp"
#include "fo4/plugin/subrecord_authoring.hpp"
#include "fo4/plugin/writer.hpp"
#include "fo4/schema/field_address.hpp"
#include "fo4/schema/record_constraints.hpp"
#include "fo4/schema/scalar.hpp"
#include "plugin_bytes.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace {

constexpr auto k_refr = fo4::Signature::from_string("REFR");
constexpr auto k_cell = fo4::Signature::from_string("CELL");
constexpr auto k_xcri = fo4::Signature::from_string("XCRI");
constexpr auto k_xwcn = fo4::Signature::from_string("XWCN");
constexpr auto k_xwcu = fo4::Signature::from_string("XWCU");
constexpr auto k_xrmr = fo4::Signature::from_string("XRMR");
constexpr auto k_xlrm = fo4::Signature::from_string("XLRM");

fo4::FormKey record_key() { return fo4::FormKey::parse("000800:Patch.esp"); }

fo4::schema::CountedArrayDescriptor velocities() {
  return fo4::schema::counted_array_descriptor(k_refr, "REFR\\WaterCurrentVelocities").value();
}

std::vector<std::byte> make_plugin(std::uint32_t declared_count, std::size_t payload_size,
                                   bool include_count = true, bool include_array = true,
                                   std::size_t count_size = 4, bool include_scale = false) {
  fo4::test::ByteWriter out;
  const auto tes4 = fo4::test::begin_record(out, "TES4", 0, 0);
  fo4::test::ByteWriter hedr;
  hedr.f32(1.0f);
  hedr.u32(1);
  hedr.u32(0x801);
  fo4::test::write_subrecord(out, "HEDR", hedr.bytes);
  fo4::test::end_record(out, tes4);

  const auto group = fo4::test::begin_group(out, "REFR", 0);
  const auto record = fo4::test::begin_record(out, "REFR", 0, 0x00000800);
  if (include_count) {
    fo4::test::ByteWriter count;
    count.u32(declared_count);
    count.bytes.resize(count_size);
    fo4::test::write_subrecord(out, "XWCN", count.bytes);
  }
  if (include_array) {
    fo4::test::ByteWriter floats;
    for (std::size_t k = 0; k < payload_size / 4; ++k) floats.f32(static_cast<float>(k + 1));
    fo4::test::write_subrecord(out, "XWCU", floats.bytes);
  }
  if (include_scale) {
    fo4::test::ByteWriter scale;
    scale.f32(1.0f);
    fo4::test::write_subrecord(out, "XSCL", scale.bytes);
  }
  fo4::test::end_record(out, record);
  fo4::test::end_group(out, group);
  return out.bytes;
}

const fo4::Subrecord* find_sub(const fo4::Record& record, fo4::Signature signature) {
  for (const auto& subrecord : record.subrecords) {
    if (subrecord.signature == signature) return &subrecord;
  }
  return nullptr;
}

std::vector<std::byte> make_linked_room_plugin() {
  fo4::test::ByteWriter out;
  const auto tes4 = fo4::test::begin_record(out, "TES4", 0, 0);
  fo4::test::ByteWriter hedr;
  hedr.f32(1.0f);
  hedr.u32(1);
  hedr.u32(0x801);
  fo4::test::write_subrecord(out, "HEDR", hedr.bytes);
  fo4::test::end_record(out, tes4);

  const auto group = fo4::test::begin_group(out, "REFR", 0);
  const auto record = fo4::test::begin_record(out, "REFR", 0, 0x00000800);
  fo4::test::ByteWriter header;
  header.u8(3);
  header.u8(0);
  header.u16(0);
  fo4::test::write_subrecord(out, "XRMR", header.bytes);
  for (const auto room : {0x801u, 0x802u, 0x803u}) {
    fo4::test::ByteWriter link;
    link.u32(room);
    fo4::test::write_subrecord(out, "XLRM", link.bytes);
  }
  fo4::test::ByteWriter scale;
  scale.f32(1.0f);
  fo4::test::write_subrecord(out, "XSCL", scale.bytes);
  fo4::test::end_record(out, record);
  fo4::test::end_group(out, group);
  return out.bytes;
}

std::uint32_t read_u32(const std::vector<std::byte>& data) {
  std::uint32_t value = 0;
  for (std::size_t i = 0; i < 4; ++i) {
    value |= static_cast<std::uint32_t>(std::to_integer<std::uint8_t>(data[i])) << (8 * i);
  }
  return value;
}

std::uint32_t read_u32_at(const std::vector<std::byte>& data, std::size_t offset) {
  std::uint32_t value = 0;
  for (std::size_t i = 0; i < 4; ++i) {
    value |= static_cast<std::uint32_t>(std::to_integer<std::uint8_t>(data[offset + i])) << (8 * i);
  }
  return value;
}

std::vector<std::byte> make_cell_xcri_plugin(const std::vector<std::uint32_t>& meshes,
                                             const std::vector<std::uint32_t>& references,
                                             std::optional<std::size_t> declared_meshes = {},
                                             std::optional<std::size_t> declared_references = {}) {
  fo4::test::ByteWriter out;
  const auto tes4 = fo4::test::begin_record(out, "TES4", 0, 0);
  fo4::test::ByteWriter hedr;
  hedr.f32(1.0f);
  hedr.u32(1);
  hedr.u32(0x801);
  fo4::test::write_subrecord(out, "HEDR", hedr.bytes);
  fo4::test::end_record(out, tes4);

  const auto group = fo4::test::begin_group(out, "CELL", 0);
  const auto record = fo4::test::begin_record(out, "CELL", 0, 0x00000800);
  fo4::test::ByteWriter payload;
  payload.u32(static_cast<std::uint32_t>(declared_meshes.value_or(meshes.size())));
// Constraint: XCRI stores the reference count in 4-byte words, so each 8-byte element contributes two to the second header word (the descriptor's divisor).
  payload.u32(static_cast<std::uint32_t>(declared_references.value_or(references.size())) * 2);
  for (const auto mesh : meshes) payload.u32(mesh);
  for (const auto reference : references) {
    payload.u32(reference);
    payload.u32(0);
  }
  fo4::test::write_subrecord(out, "XCRI", payload.bytes);
  fo4::test::end_record(out, record);
  fo4::test::end_group(out, group);
  return out.bytes;
}

std::vector<std::byte> xcri_payload(const fo4::Plugin& plugin) {
  const auto* subrecord = find_sub(*fo4::find_record(plugin, fo4::FormKey::parse("000800:Patch.esp")), k_xcri);
  return subrecord == nullptr ? std::vector<std::byte>{} : subrecord->data;
}

std::string read_member(const fo4::Plugin& plugin, std::string_view path, std::size_t index) {
  const auto field = fo4::schema::scalar_field_descriptor(path).value();
  const auto& record = *fo4::find_record(plugin, record_key());
  const auto resolved = fo4::schema::resolve_field_slots(
      record, fo4::schema::scalar_field_shape(field.scalar), field.scalar.address);
  const auto slot = resolved.slots.at(index);
  return fo4::schema::decode_scalar(
      fo4::schema::scalar_slot_bytes(record.subrecords[slot.subrecord].data, slot, field.scalar),
      field.scalar);
}

std::optional<std::uint32_t> stored_count(const fo4::Plugin& plugin) {
  const auto* subrecord = find_sub(*fo4::find_record(plugin, record_key()), k_xwcn);
  if (subrecord == nullptr || subrecord->data.size() != 4) return std::nullopt;
  return read_u32(subrecord->data);
}

std::optional<std::size_t> stored_payload_size(const fo4::Plugin& plugin) {
  const auto* subrecord = find_sub(*fo4::find_record(plugin, record_key()), k_xwcu);
  if (subrecord == nullptr) return std::nullopt;
  return subrecord->data.size();
}

}  // namespace

TEST(CountedArray, DescriptorNamesItsRecordPathAndCountSubrecord) {
  const auto array = velocities();
  EXPECT_EQ(array.record, k_refr);
  EXPECT_EQ(array.subrecord, k_xwcu);
  EXPECT_EQ(array.count_subrecord, k_xwcn);
  EXPECT_EQ(array.stride, 16u);
  EXPECT_TRUE(fo4::schema::is_counted_array_count_subrecord(k_refr, k_xwcn));
  EXPECT_FALSE(fo4::schema::is_counted_array_count_subrecord(k_refr, k_xwcu));
  EXPECT_FALSE(fo4::schema::counted_array_descriptor(fo4::Signature::from_string("CELL"),
                                                     "REFR\\WaterCurrentVelocities")
                   .has_value());
  EXPECT_FALSE(fo4::schema::counted_array_descriptor(k_refr, "REFR\\WaterCurrentVelocityX").has_value());
}

TEST(CountedArray, ReadsEveryMemberOfEachElement) {
  auto plugin = fo4::read_plugin(make_plugin(3, 48), "Patch.esp");
  EXPECT_EQ(fo4::record_field_count(plugin, record_key(), "REFR\\WaterCurrentVelocityX"), 3u);
  EXPECT_EQ(read_member(plugin, "REFR\\WaterCurrentVelocityX", 0), "1");
  EXPECT_EQ(read_member(plugin, "REFR\\WaterCurrentVelocityY", 1), "6");
  EXPECT_EQ(read_member(plugin, "REFR\\WaterCurrentVelocityZ", 1), "7");
  EXPECT_EQ(read_member(plugin, "REFR\\WaterCurrentUnnamed", 2), "12");
}

TEST(CountedArray, WritesOneMemberThenSavesAndReparses) {
  auto plugin = fo4::read_plugin(make_plugin(3, 48), "Patch.esp");
  const auto member = fo4::schema::scalar_field_descriptor("REFR\\WaterCurrentVelocityY").value();
  EXPECT_EQ(fo4::set_record_scalar(plugin, record_key(), member, 1, "2.5"),
            fo4::ScalarMutationStatus::updated);

  const auto reread = fo4::read_plugin(fo4::write_plugin(plugin), "Patch.esp");
  EXPECT_EQ(read_member(reread, "REFR\\WaterCurrentVelocityY", 1), "2.5");
  EXPECT_EQ(read_member(reread, "REFR\\WaterCurrentVelocityY", 0), "2");
  EXPECT_EQ(read_member(reread, "REFR\\WaterCurrentVelocityX", 1), "5");
  EXPECT_EQ(stored_count(reread), 3u);
  EXPECT_EQ(stored_payload_size(reread), 48u);
}

TEST(CountedArray, GenericScalarEditKeepsTheCounterAndPayload) {
  // Regression: a normalized edit on an unrelated field (REFR Scale) must not
// Constraint: drop the XWCN counter or shrink the XWCU payload. A writer that rebuilt the record from known scalar rows alone would drop both.
  auto plugin = fo4::read_plugin(make_plugin(2, 32, true, true, 4, true), "Patch.esp");
  const auto scale = fo4::schema::scalar_field_descriptor("REFR\\Scale").value();
  EXPECT_EQ(fo4::set_record_scalar(plugin, record_key(), scale, "2.5"),
            fo4::ScalarMutationStatus::updated);

  const auto reread = fo4::read_plugin(fo4::write_plugin(plugin), "Patch.esp");
  EXPECT_EQ(stored_count(reread), 2u);
  EXPECT_EQ(stored_payload_size(reread), 32u);
  EXPECT_EQ(read_member(reread, "REFR\\WaterCurrentVelocityX", 1), "5");
}

TEST(CountedArray, InsertZeroFillsTheNewElementAndIncrementsTheCount) {
  auto plugin = fo4::read_plugin(make_plugin(3, 48), "Patch.esp");
  EXPECT_EQ(fo4::insert_record_counted_element(plugin, record_key(), velocities(), 1),
            fo4::CountedArrayMutationStatus::updated);

  const auto reread = fo4::read_plugin(fo4::write_plugin(plugin), "Patch.esp");
  EXPECT_EQ(stored_count(reread), 4u);
  EXPECT_EQ(stored_payload_size(reread), 64u);
  EXPECT_EQ(read_member(reread, "REFR\\WaterCurrentVelocityX", 0), "1");
  EXPECT_EQ(read_member(reread, "REFR\\WaterCurrentVelocityX", 1), "0");
  EXPECT_EQ(read_member(reread, "REFR\\WaterCurrentVelocityX", 2), "5");
  EXPECT_EQ(read_member(reread, "REFR\\WaterCurrentVelocityX", 3), "9");
}

TEST(CountedArray, RemoveShiftsTheRemainingElementsAndDecrementsTheCount) {
  auto plugin = fo4::read_plugin(make_plugin(3, 48), "Patch.esp");
  EXPECT_EQ(fo4::remove_record_counted_element(plugin, record_key(), velocities(), 0),
            fo4::CountedArrayMutationStatus::updated);

  const auto reread = fo4::read_plugin(fo4::write_plugin(plugin), "Patch.esp");
  EXPECT_EQ(stored_count(reread), 2u);
  EXPECT_EQ(stored_payload_size(reread), 32u);
  EXPECT_EQ(read_member(reread, "REFR\\WaterCurrentVelocityX", 0), "5");
  EXPECT_EQ(read_member(reread, "REFR\\WaterCurrentVelocityX", 1), "9");
}

TEST(CountedArray, RemovingEveryElementKeepsBothSubrecordsAtZero) {
  auto plugin = fo4::read_plugin(make_plugin(3, 48), "Patch.esp");
  for (int i = 0; i < 3; ++i) {
    EXPECT_EQ(fo4::remove_record_counted_element(plugin, record_key(), velocities(), 0),
              fo4::CountedArrayMutationStatus::updated);
  }
  EXPECT_EQ(fo4::remove_record_counted_element(plugin, record_key(), velocities(), 0),
            fo4::CountedArrayMutationStatus::index_out_of_range);

  const auto emptied = fo4::read_plugin(fo4::write_plugin(plugin), "Patch.esp");
  EXPECT_EQ(stored_count(emptied), 0u);
  EXPECT_EQ(stored_payload_size(emptied), 0u);

  EXPECT_EQ(fo4::insert_record_counted_element(plugin, record_key(), velocities(), 0),
            fo4::CountedArrayMutationStatus::updated);
  const auto refilled = fo4::read_plugin(fo4::write_plugin(plugin), "Patch.esp");
  EXPECT_EQ(stored_count(refilled), 1u);
  EXPECT_EQ(stored_payload_size(refilled), 16u);
}

TEST(CountedArray, RefusesOutOfRangeIndexesWithoutMutation) {
  auto plugin = fo4::read_plugin(make_plugin(3, 48), "Patch.esp");
  const auto original = fo4::test::original_bytes(plugin);
  const auto member = fo4::schema::scalar_field_descriptor("REFR\\WaterCurrentVelocityX").value();

  EXPECT_EQ(fo4::set_record_scalar(plugin, record_key(), member, 3, "1"),
            fo4::ScalarMutationStatus::subrecord_missing);
  EXPECT_EQ(fo4::insert_record_counted_element(plugin, record_key(), velocities(), 4),
            fo4::CountedArrayMutationStatus::index_out_of_range);
  EXPECT_EQ(fo4::remove_record_counted_element(plugin, record_key(), velocities(), 3),
            fo4::CountedArrayMutationStatus::index_out_of_range);
  EXPECT_EQ(fo4::insert_record_counted_element(plugin, fo4::FormKey::parse("000999:Patch.esp"),
                                               velocities(), 0),
            fo4::CountedArrayMutationStatus::record_missing);
  EXPECT_EQ(fo4::test::original_bytes(plugin), original);
}

TEST(CountedArray, CountSubrecordCannotBeWrittenAsAPlainScalar) {
  auto plugin = fo4::read_plugin(make_plugin(3, 48), "Patch.esp");
  const auto original = fo4::test::original_bytes(plugin);
  const auto count = fo4::schema::scalar_field_descriptor("REFR\\WaterCurrentVelocityCount").value();
  EXPECT_THROW(fo4::set_record_scalar(plugin, record_key(), count, "5"), fo4::schema::ScalarError);
  EXPECT_EQ(fo4::test::original_bytes(plugin), original);
}

TEST(CountedArray, CountSubrecordCannotBeRemovedAloneFromItsArray) {
  auto plugin = fo4::read_plugin(make_plugin(3, 48), "Patch.esp");
  const auto before = fo4::write_plugin(plugin);
  EXPECT_EQ(fo4::remove_record_subrecord(plugin, record_key(), k_xwcn, 0),
            fo4::SubrecordRemoveStatus::not_authorable);
  EXPECT_EQ(fo4::write_plugin(plugin), before);
}

TEST(CountedArray, MissingCountOrPayloadSubrecordIsRefusedWithoutMutation) {
  auto no_count = fo4::read_plugin(make_plugin(3, 48, false, true), "Patch.esp");
  auto no_payload = fo4::read_plugin(make_plugin(0, 0, true, false), "Patch.esp");
  const auto original = fo4::test::original_bytes(no_count);
  EXPECT_EQ(fo4::insert_record_counted_element(no_count, record_key(), velocities(), 0),
            fo4::CountedArrayMutationStatus::subrecord_missing);
  EXPECT_EQ(fo4::remove_record_counted_element(no_count, record_key(), velocities(), 0),
            fo4::CountedArrayMutationStatus::subrecord_missing);
  EXPECT_EQ(fo4::test::original_bytes(no_count), original);
  EXPECT_EQ(fo4::insert_record_counted_element(no_payload, record_key(), velocities(), 0),
            fo4::CountedArrayMutationStatus::subrecord_missing);
}

TEST(CountedArray, CountThatDisagreesWithThePayloadFailsClosed) {
  auto plugin = fo4::read_plugin(make_plugin(2, 48), "Patch.esp");
  const auto original = fo4::test::original_bytes(plugin);
  EXPECT_THROW(fo4::insert_record_counted_element(plugin, record_key(), velocities(), 0),
               fo4::schema::CountedArrayError);
  EXPECT_THROW(fo4::remove_record_counted_element(plugin, record_key(), velocities(), 0),
               fo4::schema::CountedArrayError);
  EXPECT_EQ(fo4::test::original_bytes(plugin), original);
}

TEST(CountedArray, CountSubrecordThatIsNotFourBytesFailsClosed) {
  auto plugin = fo4::read_plugin(make_plugin(3, 48, true, true, 2), "Patch.esp");
  const auto original = fo4::test::original_bytes(plugin);
  EXPECT_THROW(fo4::insert_record_counted_element(plugin, record_key(), velocities(), 0),
               fo4::schema::CountedArrayError);
  EXPECT_THROW(fo4::remove_record_counted_element(plugin, record_key(), velocities(), 0),
               fo4::schema::CountedArrayError);
  EXPECT_EQ(fo4::test::original_bytes(plugin), original);
}

TEST(CountedArray, PayloadThatIsNotWholeElementsFailsClosed) {
  auto plugin = fo4::read_plugin(make_plugin(1, 20), "Patch.esp");
  const auto original = fo4::test::original_bytes(plugin);
  EXPECT_THROW(fo4::insert_record_counted_element(plugin, record_key(), velocities(), 0),
               fo4::schema::CountedArrayError);
  EXPECT_EQ(fo4::test::original_bytes(plugin), original);
}

TEST(CountedArray, HeaderCountedCellSegmentsKeepTheirHeaderCounters) {
// Constraint: Regression: CELL XCRI stores two segments after a two-word header, with the reference segment using an 8-byte element and a double-word divisor. An insert
  // or removal that ignored the header or the segment offset would corrupt every
// Constraint: following combined reference.
  const auto meshes = fo4::schema::counted_array_descriptor(k_cell, "CELL\\CombinedMeshIds").value();
  const auto references =
      fo4::schema::counted_array_descriptor(k_cell, "CELL\\CombinedReferences").value();
  EXPECT_EQ(meshes.segment_count, 2u);
  EXPECT_EQ(meshes.segment_index, 0u);
  EXPECT_EQ(meshes.count_word, 0u);
  EXPECT_EQ(meshes.stride, 4u);
  EXPECT_EQ(references.segment_index, 1u);
  EXPECT_EQ(references.count_word, 1u);
  EXPECT_EQ(references.stride, 8u);
  EXPECT_EQ(references.count_divisor, 2u);

  auto plugin = fo4::read_plugin(
      make_cell_xcri_plugin({0x801, 0x802}, {0x803, 0x804, 0x805}), "Patch.esp");
  const auto cell_key = fo4::FormKey::parse("000800:Patch.esp");
  EXPECT_EQ(fo4::insert_record_counted_element(plugin, cell_key, meshes, 1),
            fo4::CountedArrayMutationStatus::updated);

  auto reread = fo4::read_plugin(fo4::write_plugin(plugin), "Patch.esp");
  auto bytes = xcri_payload(reread);
  ASSERT_EQ(bytes.size(), 44u);
  EXPECT_EQ(read_u32_at(bytes, 0), 3u);
  EXPECT_EQ(read_u32_at(bytes, 4), 6u);
  EXPECT_EQ(read_u32_at(bytes, 8), 0x801u);
  EXPECT_EQ(read_u32_at(bytes, 12), 0u);
  EXPECT_EQ(read_u32_at(bytes, 16), 0x802u);
  EXPECT_EQ(read_u32_at(bytes, 20), 0x803u);
  EXPECT_EQ(read_u32_at(bytes, 28), 0x804u);
  EXPECT_EQ(read_u32_at(bytes, 36), 0x805u);

  EXPECT_EQ(fo4::remove_record_counted_element(plugin, cell_key, references, 0),
            fo4::CountedArrayMutationStatus::updated);
  reread = fo4::read_plugin(fo4::write_plugin(plugin), "Patch.esp");
  bytes = xcri_payload(reread);
  ASSERT_EQ(bytes.size(), 36u);
  EXPECT_EQ(read_u32_at(bytes, 0), 3u);
  EXPECT_EQ(read_u32_at(bytes, 4), 4u);
  EXPECT_EQ(read_u32_at(bytes, 20), 0x804u);
  EXPECT_EQ(read_u32_at(bytes, 28), 0x805u);

  EXPECT_EQ(fo4::remove_record_counted_element(plugin, cell_key, meshes, 0),
            fo4::CountedArrayMutationStatus::updated);
  reread = fo4::read_plugin(fo4::write_plugin(plugin), "Patch.esp");
  bytes = xcri_payload(reread);
  ASSERT_EQ(bytes.size(), 32u);
  EXPECT_EQ(read_u32_at(bytes, 0), 2u);
  EXPECT_EQ(read_u32_at(bytes, 4), 4u);
  EXPECT_EQ(read_u32_at(bytes, 8), 0u);
  EXPECT_EQ(read_u32_at(bytes, 12), 0x802u);
  EXPECT_EQ(read_u32_at(bytes, 16), 0x804u);
  EXPECT_EQ(read_u32_at(bytes, 24), 0x805u);
}

TEST(CountedArray, HeaderCountedSegmentThatDisagreesWithItsHeaderFailsClosed) {
// Constraint: Regression: a segment whose stored header count is smaller than its payload
  // must be refused rather than silently dropping the trailing elements.
  auto plugin = fo4::read_plugin(
      make_cell_xcri_plugin({0x801, 0x802}, {0x803}, 1, 1), "Patch.esp");
  const auto original = fo4::test::original_bytes(plugin);
  const auto meshes = fo4::schema::counted_array_descriptor(k_cell, "CELL\\CombinedMeshIds").value();
  const auto references =
      fo4::schema::counted_array_descriptor(k_cell, "CELL\\CombinedReferences").value();
  EXPECT_THROW(fo4::insert_record_counted_element(plugin, fo4::FormKey::parse("000800:Patch.esp"),
                                                  meshes, 0),
               fo4::schema::CountedArrayError);
  EXPECT_THROW(fo4::remove_record_counted_element(plugin, fo4::FormKey::parse("000800:Patch.esp"),
                                                  references, 0),
               fo4::schema::CountedArrayError);
  EXPECT_EQ(fo4::test::original_bytes(plugin), original);
}

TEST(CountedArray, CoupledLinkedRoomRunKeepsItsCountThroughAGenericEdit) {
// Constraint: Regression: XRMR stores a one-byte count inside a four-byte header and owns the XLRM run. A generic edit that rebuilt the record from writable rows alone would drop the header or the linked-room list.
  const auto constraint = fo4::schema::record_authoring_constraint(k_refr);
  ASSERT_TRUE(constraint.has_value());
  const auto rules = fo4::schema::authoring_subrecords(*constraint);
  const auto rule = std::find_if(rules.begin(), rules.end(), [](const auto& entry) {
    return entry.signature == k_xrmr;
  });
  ASSERT_NE(rule, rules.end());
  EXPECT_TRUE(rule->coupled);
  EXPECT_FALSE(fo4::schema::scalar_field_descriptor("REFR\\LinkedRoomsCount").has_value());

  auto plugin = fo4::read_plugin(make_linked_room_plugin(), "Patch.esp");
  EXPECT_EQ(fo4::remove_record_subrecord(plugin, record_key(), k_xrmr, 0),
            fo4::SubrecordRemoveStatus::not_authorable);
  EXPECT_EQ(fo4::add_record_subrecord(plugin, record_key(), k_xrmr),
            fo4::SubrecordAddStatus::not_authorable);
  const auto scale = fo4::schema::scalar_field_descriptor("REFR\\Scale").value();
  EXPECT_EQ(fo4::set_record_scalar(plugin, record_key(), scale, "2.5"),
            fo4::ScalarMutationStatus::updated);

  const auto reread = fo4::read_plugin(fo4::write_plugin(plugin), "Patch.esp");
  const auto* record = fo4::find_record(reread, record_key());
  ASSERT_NE(record, nullptr);
  const auto* header = find_sub(*record, k_xrmr);
  ASSERT_NE(header, nullptr);
  ASSERT_EQ(header->data.size(), 4u);
  EXPECT_EQ(std::to_integer<std::uint8_t>(header->data[0]), 3u);
  std::size_t links = 0;
  for (const auto& subrecord : record->subrecords) {
    if (subrecord.signature == k_xlrm) ++links;
  }
  EXPECT_EQ(links, 3u);
}
