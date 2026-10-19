#include "formlink_segments.hpp"

#include <cstring>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace fo4::schema::detail {
namespace {

std::optional<std::size_t> subrecord_index(const Record& record, Signature signature) {
  for (std::size_t index = 0; index < record.subrecords.size(); ++index) {
    if (record.subrecords[index].signature == signature) return index;
  }
  return std::nullopt;
}

std::uint32_t read_u32(const std::vector<std::byte>& data, std::size_t offset) {
  std::uint32_t value = 0;
  std::memcpy(&value, data.data() + offset, sizeof(value));
  return value;
}

void write_u32(std::vector<std::byte>& data, std::size_t offset, std::uint32_t value) {
  std::memcpy(data.data() + offset, &value, sizeof(value));
}

void write_u16(std::vector<std::byte>& data, std::size_t offset, std::uint16_t value) {
  std::memcpy(data.data() + offset, &value, sizeof(value));
}

std::span<const std::uint32_t> segment_counters(
    const FormLinkListDescriptor& descriptor) noexcept {
  return {k_generated_packed_segment_counters + descriptor.first_segment,
          descriptor.segment_count};
}

std::vector<std::uint32_t> segment_counts(const FormLinkListDescriptor& descriptor,
                                          const Subrecord& counter) {
  if (counter.data.size() != descriptor.counter_payload_size) {
    throw FormLinkListError(std::string(descriptor.path) +
                            " counter subrecord has the wrong size");
  }
  const auto offsets = segment_counters(descriptor);
  std::vector<std::uint32_t> counts;
  counts.reserve(offsets.size());
  for (const auto offset : offsets) {
    counts.push_back(read_u32(counter.data, offset));
  }
  return counts;
}

void insert_counter(Record& record,
                    const FormLinkListDescriptor& descriptor,
                    Subrecord counter) {
  const auto array_index = subrecord_index(record, descriptor.subrecord);
  const std::size_t insertion = array_index.value_or(record.subrecords.size());
  record.subrecords.insert(
      record.subrecords.begin() + static_cast<std::ptrdiff_t>(insertion),
      std::move(counter));
}

}  // namespace

PackedSegment packed_segment(const Record& record,
                             const FormLinkListDescriptor& descriptor) {
  const auto array_index = subrecord_index(record, descriptor.subrecord);
  std::size_t elements = 0;
  if (array_index.has_value()) {
    const auto& data = record.subrecords[*array_index].data;
    if (data.size() % 4 != 0) {
      throw FormLinkListError(descriptor.record.to_string() + " " +
                              std::string(descriptor.path) +
                              " packed array length must be a multiple of four bytes");
    }
    elements = data.size() / 4;
  }
  if (descriptor.segment_count == 0) {
    return PackedSegment{0, elements};
  }

  const auto counter_index = subrecord_index(record, descriptor.counter);
  if (!counter_index.has_value()) {
    if (elements != 0) {
      throw FormLinkListError(std::string(descriptor.path) +
                              " has links without a counter subrecord");
    }
    return PackedSegment{0, 0};
  }
  const auto counts = segment_counts(descriptor, record.subrecords[*counter_index]);
  if (descriptor.segment_index >= counts.size()) {
    throw FormLinkListError(std::string(descriptor.path) +
                            " segment index is out of range");
  }
  std::size_t base = 0;
  std::size_t total = 0;
  for (std::size_t index = 0; index < counts.size(); ++index) {
    if (index < descriptor.segment_index) base += counts[index];
    total += counts[index];
  }
  if (total != elements) {
    throw FormLinkListError(std::string(descriptor.path) +
                            " counters do not match the link array");
  }
  return PackedSegment{base, counts[descriptor.segment_index]};
}

void reconcile_counter(Record& record,
                       const FormLinkListDescriptor& descriptor,
                       std::uint32_t count) {
  if (descriptor.counter == Signature{}) return;
  const auto counter_index = subrecord_index(record, descriptor.counter);
  if (descriptor.counter_offset != 0) {
    if (count > 0xFFFFu) {
      throw FormLinkListError(std::string(descriptor.path) + " count does not fit the counter field");
    }
    if (!counter_index.has_value()) {
      throw FormLinkListError(std::string(descriptor.path) +
                              " counter subrecord is missing from the record");
    }
    auto& data = record.subrecords[*counter_index].data;
    if (data.size() < descriptor.counter_offset + 2) {
      throw FormLinkListError(std::string(descriptor.path) +
                              " counter subrecord is too short for the count");
    }
    // Constraint: an in-subrecord counter is zeroed in place, never erased.
    write_u16(data, descriptor.counter_offset, static_cast<std::uint16_t>(count));
    return;
  }
  if (descriptor.segment_count == 0) {
    if (count == 0) {
      if (counter_index.has_value()) {
        record.subrecords.erase(record.subrecords.begin() +
                                static_cast<std::ptrdiff_t>(*counter_index));
      }
      return;
    }
    if (counter_index.has_value()) {
      auto& data = record.subrecords[*counter_index].data;
      data.resize(4);
      write_u32(data, 0, count);
      return;
    }
    Subrecord counter;
    counter.signature = descriptor.counter;
    counter.data.resize(4);
    write_u32(counter.data, 0, count);
    insert_counter(record, descriptor, std::move(counter));
    return;
  }

  const auto offsets = segment_counters(descriptor);
  const std::size_t counter_offset = offsets[descriptor.segment_index];
  if (counter_index.has_value()) {
    auto& data = record.subrecords[*counter_index].data;
    if (data.size() != descriptor.counter_payload_size) {
      throw FormLinkListError(std::string(descriptor.path) +
                              " counter subrecord has the wrong size");
    }
    // Constraint: a shared segmented counter is zeroed in place, never erased.
    write_u32(data, counter_offset, count);
    return;
  }
  if (count == 0) return;
  Subrecord counter;
  counter.signature = descriptor.counter;
  counter.data.assign(descriptor.counter_payload_size, std::byte{0});
  write_u32(counter.data, counter_offset, count);
  insert_counter(record, descriptor, std::move(counter));
}

}  // namespace fo4::schema::detail
