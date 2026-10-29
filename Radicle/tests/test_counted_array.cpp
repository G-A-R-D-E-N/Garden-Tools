#include "fo4/plugin/mutation.hpp"
#include "fo4/plugin/reader.hpp"
#include "fo4/plugin/subrecord_authoring.hpp"
#include "fo4/plugin/writer.hpp"
#include "fo4/schema/field_address.hpp"
#include "fo4/schema/scalar.hpp"
#include "plugin_bytes.hpp"

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace {

constexpr auto k_refr = fo4::Signature::from_string("REFR");
constexpr auto k_xwcn = fo4::Signature::from_string("XWCN");
constexpr auto k_xwcu = fo4::Signature::from_string("XWCU");

fo4::FormKey record_key() { return fo4::FormKey::parse("000800:Patch.esp"); }

fo4::schema::CountedArrayDescriptor velocities() {
  return fo4::schema::counted_array_descriptor(k_refr, "REFR\\WaterCurrentVelocities").value();
}

std::vector<std::byte> make_plugin(std::uint32_t declared_count, std::size_t payload_size,
                                   bool include_count = true, bool include_array = true,
                                   std::size_t count_size = 4) {
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

std::uint32_t read_u32(const std::vector<std::byte>& data) {
  std::uint32_t value = 0;
  for (std::size_t i = 0; i < 4; ++i) {
    value |= static_cast<std::uint32_t>(std::to_integer<std::uint8_t>(data[i])) << (8 * i);
  }
  return value;
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
