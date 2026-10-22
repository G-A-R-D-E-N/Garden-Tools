#include "fo4/schema/scalar.hpp"

#include <array>
#include <charconv>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <iterator>
#include <limits>
#include <string>
#include <system_error>
#include <type_traits>

namespace fo4::schema {
namespace {

#include "fo4/schema/generated_scalar_fields.inc"

void append_u16(std::vector<std::byte>& out, std::uint16_t value) {
  out.push_back(std::byte{static_cast<std::uint8_t>(value)});
  out.push_back(std::byte{static_cast<std::uint8_t>(value >> 8)});
}

void append_u32(std::vector<std::byte>& out, std::uint32_t value) {
  append_u16(out, static_cast<std::uint16_t>(value));
  append_u16(out, static_cast<std::uint16_t>(value >> 16));
}

std::uint16_t read_u16(std::span<const std::byte> data) {
  return static_cast<std::uint16_t>(std::to_integer<std::uint8_t>(data[0])) |
         static_cast<std::uint16_t>(
             static_cast<std::uint16_t>(std::to_integer<std::uint8_t>(data[1])) << 8);
}

std::uint32_t read_u32(std::span<const std::byte> data) {
  return static_cast<std::uint32_t>(read_u16(data)) |
         (static_cast<std::uint32_t>(read_u16(data.subspan(2))) << 16);
}

template <class Integer>
Integer parse_integer(std::string_view text) {
  static_assert(std::is_integral_v<Integer>);
  if (text.empty()) throw ScalarError("scalar value is empty");
  Integer value{};
  const auto* begin = text.data();
  const auto* end = begin + text.size();
  const auto [ptr, error] = std::from_chars(begin, end, value, 10);
  if (error != std::errc{} || ptr != end) {
    throw ScalarError("scalar value must be a base-10 integer");
  }
  return value;
}

double parse_float(std::string_view text) {
  if (text.empty()) throw ScalarError("scalar value is empty");
  double value = 0.0;
  const auto* begin = text.data();
  const auto* end = begin + text.size();
  const auto [ptr, error] = std::from_chars(begin, end, value, std::chars_format::general);
  if (error != std::errc{} || ptr != end || !std::isfinite(value)) {
    throw ScalarError("scalar value must be a finite number");
  }
  return value;
}

void validate_range(double value, const ScalarDescriptor& descriptor) {
  if (value < descriptor.minimum || value > descriptor.maximum) {
    throw ScalarError(
        std::string(descriptor.field_name) + " must be between " +
        std::to_string(descriptor.minimum) + " and " +
        std::to_string(descriptor.maximum));
  }
}

std::string format_float(float value) {
  char buffer[64]{};
  const auto [ptr, error] = std::to_chars(
      buffer, buffer + sizeof(buffer), value, std::chars_format::general,
      std::numeric_limits<float>::max_digits10);
  if (error != std::errc{}) throw ScalarError("could not format scalar float");
  return std::string(buffer, ptr);
}

std::span<const std::byte> fixed_string_bytes(std::span<const std::byte> payload,
                                              const ScalarDescriptor& descriptor) {
  if (payload.size() != descriptor.payload_size) {
    throw ScalarError(std::string(descriptor.field_name) + " has unexpected binary width");
  }
  return payload;
}

std::span<const std::byte> slot_string_bytes(std::span<const std::byte> payload,
                                             std::size_t offset,
                                             const ScalarDescriptor& descriptor) {
  const auto tail = payload.subspan(offset);
  const auto limit = descriptor.minimum_payload_size > offset
                         ? descriptor.minimum_payload_size - offset
                         : tail.size();
  const auto slot = tail.first(std::min(limit, tail.size()));
  const auto terminator = std::find(slot.begin(), slot.end(), std::byte{0});
  if (terminator == slot.end()) {
    throw ScalarError(std::string(descriptor.field_name) + " has no NUL terminator in its slot");
  }
  return slot.first(static_cast<std::size_t>(terminator - slot.begin()) + 1);
}

struct SegmentLayout {
  std::size_t count_word{0};
  std::size_t stride{0};
  bool operator==(const SegmentLayout&) const = default;
};

using SegmentLayouts = std::array<SegmentLayout, k_max_header_counters>;

bool record_segment_layouts(Signature record, Signature subrecord, std::size_t segment_count,
                            SegmentLayouts& layouts) {
  if (segment_count == 0 || segment_count > k_max_header_counters) return false;
  std::size_t found = 0;
  for (const auto& array : k_counted_arrays) {
    if (array.record != record || array.subrecord != subrecord || array.segment_count == 0) continue;
    if (array.segment_index >= segment_count || array.stride == 0 ||
        layouts[array.segment_index].stride != 0) {
      return false;
    }
    layouts[array.segment_index] = SegmentLayout{array.count_word, array.stride};
    ++found;
  }
  return found == segment_count;
}

// Constraint: one payload subrecord carries one segment layout in every record that declares it.
bool shared_segment_layouts(Signature subrecord, std::size_t segment_count,
                            SegmentLayouts& layouts) {
  bool found = false;
  Signature source{};
  for (const auto& array : k_counted_arrays) {
    if (array.subrecord != subrecord || array.segment_count != segment_count) continue;
    if (found && array.record == source) continue;
    SegmentLayouts candidate{};
    if (!record_segment_layouts(array.record, subrecord, segment_count, candidate)) return false;
    if (!found) {
      layouts = candidate;
      source = array.record;
      found = true;
    } else if (candidate != layouts) {
      return false;
    }
  }
  return found;
}

std::optional<SegmentSpan> segment_span(std::span<const std::byte> payload,
                                        const SegmentLayouts& layouts,
                                        const FieldAddress& address) {
  const auto& segment = address.segment;
  if (segment.count == 0 || segment.count > k_max_header_counters || address.stride == 0 ||
      segment.index >= segment.count || payload.size() < 4 ||
      layouts[segment.index].stride != address.stride ||
      layouts[segment.index].count_word != segment.count_word) {
    return std::nullopt;
  }
  const auto header_words = read_u32(payload);
  if (header_words == 0 || header_words > k_max_header_counters) return std::nullopt;
  const std::size_t header_size = 4 * (1 + header_words);
  if (payload.size() < header_size) return std::nullopt;

  std::size_t base = header_size;
  std::optional<SegmentSpan> ours;
  for (std::size_t index = 0; index < segment.count; ++index) {
    const auto count_word = layouts[index].count_word;
    if (count_word >= header_words) continue;
    const auto count = static_cast<std::size_t>(read_u32(payload.subspan(4 * (count_word + 1))));
    if (count > (payload.size() - base) / layouts[index].stride) return std::nullopt;
    if (index == segment.index) ours = SegmentSpan{base, count};
    base += count * layouts[index].stride;
  }
  if (!ours || base != payload.size()) return std::nullopt;
  return ours;
}

std::optional<SegmentSpan> descriptor_segment(std::span<const std::byte> payload,
                                              const ScalarDescriptor& descriptor) {
  if (descriptor.address.segment.count == 0) return std::nullopt;
  SegmentLayouts layouts{};
  if (!shared_segment_layouts(descriptor.subrecord, descriptor.address.segment.count, layouts)) {
    return std::nullopt;
  }
  return segment_span(payload, layouts, descriptor.address);
}

}  // namespace

std::optional<ScalarDescriptor> scalar_descriptor(Signature subrecord) noexcept {
  const ScalarDescriptor* only = nullptr;
  for (const auto& field : k_scalar_fields) {
    if (field.scalar.subrecord != subrecord) continue;
    if (only) return std::nullopt;
    only = &field.scalar;
  }
  if (!only || only->embedded || only->address.repeated ||
      only->storage == ScalarStorage::zstring ||
      only->storage == ScalarStorage::fixed_string) {
    return std::nullopt;
  }
  return *only;
}

std::span<const std::byte> scalar_field_bytes(std::span<const std::byte> payload,
                                              const ScalarDescriptor& descriptor) {
  if (descriptor.storage == ScalarStorage::fixed_string) {
    return fixed_string_bytes(payload, descriptor);
  }
  if (descriptor.storage == ScalarStorage::zstring) {
    if (!descriptor.embedded) return payload;
    if (descriptor.payload_size != 0 && payload.size() != descriptor.payload_size) return {};
    if (payload.size() < descriptor.minimum_payload_size) return {};
    return slot_string_bytes(payload, descriptor.offset, descriptor);
  }
  const auto width = scalar_storage_size(descriptor.storage);
  if (!descriptor.embedded) {
    if (payload.size() != width) {
      throw ScalarError(std::string(descriptor.field_name) + " has unexpected binary width");
    }
    return payload;
  }
  if (descriptor.payload_size != 0 && payload.size() != descriptor.payload_size) return {};
  if (payload.size() < descriptor.minimum_payload_size) return {};
  std::size_t offset = descriptor.offset;
  if (descriptor.address.segment.count != 0) {
    const auto segment = descriptor_segment(payload, descriptor);
    if (!segment || segment->count == 0) return {};
    offset += segment->base;
  }
  if (payload.size() < offset + width) return {};
  return payload.subspan(offset, width);
}

FieldShape scalar_field_shape(const ScalarDescriptor& descriptor) noexcept {
  FieldShape shape;
  shape.subrecord = descriptor.subrecord;
  shape.payload_size = descriptor.payload_size;
  shape.minimum_payload_size = descriptor.minimum_payload_size;
  shape.min_form_version = descriptor.min_form_version;
  shape.max_form_version = descriptor.max_form_version;
  shape.whole_payload = !descriptor.embedded;
  shape.offset = descriptor.embedded ? descriptor.offset : std::size_t{0};
  shape.width = descriptor.storage == ScalarStorage::fixed_string
                    ? descriptor.payload_size
                    : scalar_storage_size(descriptor.storage);
  return shape;
}

std::span<const std::byte> scalar_slot_bytes(std::span<const std::byte> payload,
                                             const FieldSlot& slot,
                                             const ScalarDescriptor& descriptor) {
  if (descriptor.storage == ScalarStorage::fixed_string) {
    return fixed_string_bytes(payload, descriptor);
  }
  if (descriptor.storage == ScalarStorage::zstring) {
    if (!descriptor.embedded) return payload;
    return slot_string_bytes(payload, slot.offset, descriptor);
  }
  return payload.subspan(slot.offset, scalar_storage_size(descriptor.storage));
}

void patch_scalar_field(std::span<std::byte> payload,
                        const ScalarDescriptor& descriptor,
                        std::span<const std::byte> encoded) {
  const auto width = scalar_storage_size(descriptor.storage);
  if (encoded.size() != width) {
    throw ScalarError(std::string(descriptor.field_name) + " encoded to the wrong width");
  }
  std::size_t offset = descriptor.embedded ? descriptor.offset : std::size_t{0};
  if (descriptor.address.segment.count != 0) {
    const auto segment = descriptor_segment(payload, descriptor);
    if (!segment || segment->count == 0) {
      throw ScalarError(std::string(descriptor.field_name) +
                        " does not fit in the subrecord payload");
    }
    offset += segment->base;
  }
  if (payload.size() < descriptor.minimum_payload_size || payload.size() < offset + width) {
    throw ScalarError(std::string(descriptor.field_name) +
                      " does not fit in the subrecord payload");
  }
  std::copy(encoded.begin(), encoded.end(), payload.begin() + static_cast<std::ptrdiff_t>(offset));
}

std::optional<ScalarFieldDescriptor> scalar_field_descriptor(
    std::string_view path) noexcept {
  for (const auto& field : k_scalar_fields) {
    if (field.path == path) return field;
  }
  return std::nullopt;
}

std::optional<ScalarFieldDescriptor> scalar_field_descriptor(
    Signature record, std::string_view path) noexcept {
  for (const auto& field : k_scalar_fields) {
    if (field.record == record && field.path == path) return field;
  }
  return std::nullopt;
}

std::span<const ScalarFieldDescriptor> scalar_fields(Signature record) noexcept {
  std::size_t first = 0;
  while (first < std::size(k_scalar_fields) && k_scalar_fields[first].record != record) {
    ++first;
  }
  if (first == std::size(k_scalar_fields)) return {};

  std::size_t last = first + 1;
  while (last < std::size(k_scalar_fields) && k_scalar_fields[last].record == record) {
    ++last;
  }
  return {k_scalar_fields + first, last - first};
}

std::optional<CountedArrayDescriptor> counted_array_descriptor(Signature record,
                                                               std::string_view path) noexcept {
  for (const auto& array : k_counted_arrays) {
    if (array.record == record && array.path == path) return array;
  }
  return std::nullopt;
}

std::optional<SegmentSpan> header_segment(std::span<const std::byte> payload,
                                          Signature record,
                                          Signature subrecord,
                                          const FieldAddress& address) {
  SegmentLayouts layouts{};
  if (!record_segment_layouts(record, subrecord, address.segment.count, layouts)) {
    return std::nullopt;
  }
  return segment_span(payload, layouts, address);
}

bool is_counted_array_count_subrecord(Signature record, Signature subrecord) noexcept {
  for (const auto& array : k_counted_arrays) {
    if (array.record == record && array.count_subrecord != Signature{} && array.count_subrecord == subrecord) {
      return true;
    }
  }
  return false;
}

std::optional<SubrecordGroupDescriptor> subrecord_group_descriptor(Signature record,
                                                                   std::string_view path) noexcept {
  for (const auto& group : k_subrecord_groups) {
    if (group.record == record && group.path == path) return group;
  }
  return std::nullopt;
}

std::optional<SubrecordGroupDescriptor> subrecord_group_for_member(Signature record,
                                                                   Signature subrecord) noexcept {
  for (const auto& group : k_subrecord_groups) {
    if (group.record != record) continue;
    for (const auto& member : group.members) {
      if (member.subrecord == subrecord) return group;
    }
    for (const auto& member : group.run.members) {
      if (member.subrecord == subrecord) return group;
    }
  }
  return std::nullopt;
}

bool is_subrecord_group_count_subrecord(Signature record, Signature subrecord) noexcept {
  for (const auto& group : k_subrecord_groups) {
    if (group.record != record) continue;
    if (group.count_subrecord != Signature{} && group.count_subrecord == subrecord) return true;
    if (group.run.count_member != Signature{} && group.run.count_member == subrecord) return true;
  }
  return false;
}

bool is_derived_scalar_row(Signature record, Signature subrecord) noexcept {
  if (is_counted_array_count_subrecord(record, subrecord)) return true;
  if (is_subrecord_group_count_subrecord(record, subrecord)) return true;
  if (record == Signature::from_string("FSTS") && subrecord == Signature::from_string("XCNT")) {
    return true;
  }
  return record == Signature::from_string("INNR") &&
         (subrecord == Signature::from_string("VNAM") ||
          subrecord == Signature::from_string("KSIZ"));
}

std::size_t scalar_storage_size(ScalarStorage storage) noexcept {
  switch (storage) {
    case ScalarStorage::u8:
    case ScalarStorage::i8:
      return 1;
    case ScalarStorage::u16:
    case ScalarStorage::i16:
      return 2;
    case ScalarStorage::u32:
    case ScalarStorage::i32:
    case ScalarStorage::f32:
      return 4;
    case ScalarStorage::zstring:
    case ScalarStorage::fixed_string:
      return 0;
  }
  return 0;
}

std::optional<std::string_view> scalar_value_name(const ScalarDescriptor& descriptor,
                                                  std::int64_t value) noexcept {
  for (const auto& entry : descriptor.names) {
    if (entry.value == value) return entry.name;
  }
  return std::nullopt;
}

std::optional<std::int64_t> scalar_name_value(const ScalarDescriptor& descriptor,
                                              std::string_view name) noexcept {
  for (const auto& entry : descriptor.names) {
    if (entry.name == name) return entry.value;
  }
  return std::nullopt;
}

std::string decode_scalar(std::span<const std::byte> data,
                          const ScalarDescriptor& descriptor) {
  if (descriptor.storage == ScalarStorage::fixed_string) {
    const auto slot = fixed_string_bytes(data, descriptor);
    const auto* begin = reinterpret_cast<const char*>(slot.data());
    return std::string(begin, begin + slot.size());
  }
  if (descriptor.storage == ScalarStorage::zstring) {
    const auto terminated = !data.empty() && data.back() == std::byte{0} &&
                            std::find(data.begin(), data.end() - 1, std::byte{0}) == data.end() - 1;
    if (!terminated) {
      throw ScalarError(std::string(descriptor.field_name) + " is not a single NUL-terminated string");
    }
    const auto* begin = reinterpret_cast<const char*>(data.data());
    return std::string(begin, begin + data.size() - 1);
  }
  if (data.size() != scalar_storage_size(descriptor.storage)) {
    throw ScalarError(
        std::string(descriptor.field_name) + " has unexpected binary width");
  }

  switch (descriptor.storage) {
    case ScalarStorage::u8:
      return std::to_string(std::to_integer<std::uint8_t>(data[0]));
    case ScalarStorage::i8:
      return std::to_string(static_cast<std::int8_t>(std::to_integer<std::uint8_t>(data[0])));
    case ScalarStorage::i16:
      return std::to_string(static_cast<std::int16_t>(read_u16(data)));
    case ScalarStorage::zstring:
    case ScalarStorage::fixed_string:
      break;
    case ScalarStorage::u16:
      return std::to_string(read_u16(data));
    case ScalarStorage::u32:
      return std::to_string(read_u32(data));
    case ScalarStorage::i32:
      return std::to_string(static_cast<std::int32_t>(read_u32(data)));
    case ScalarStorage::f32: {
      const auto bits = read_u32(data);
      float value = 0.0F;
      static_assert(sizeof(value) == sizeof(bits));
      std::memcpy(&value, &bits, sizeof(value));
      if (!std::isfinite(value)) throw ScalarError("stored scalar float is not finite");
      return format_float(value);
    }
  }
  throw ScalarError("unsupported scalar storage type");
}

std::vector<std::byte> encode_scalar(std::string_view text,
                                     const ScalarDescriptor& descriptor) {
  std::vector<std::byte> out;
  out.reserve(scalar_storage_size(descriptor.storage));

  switch (descriptor.storage) {
    case ScalarStorage::u8: {
      const auto value = parse_integer<unsigned int>(text);
      if (value > std::numeric_limits<std::uint8_t>::max()) {
        throw ScalarError("scalar value exceeds u8 storage");
      }
      validate_range(value, descriptor);
      out.push_back(std::byte{static_cast<std::uint8_t>(value)});
      break;
    }
    case ScalarStorage::i8: {
      const auto value = parse_integer<int>(text);
      if (value < std::numeric_limits<std::int8_t>::min() ||
          value > std::numeric_limits<std::int8_t>::max()) {
        throw ScalarError("scalar value exceeds i8 storage");
      }
      validate_range(value, descriptor);
      out.push_back(std::byte{static_cast<std::uint8_t>(static_cast<std::int8_t>(value))});
      break;
    }
    case ScalarStorage::i16: {
      const auto value = parse_integer<int>(text);
      if (value < std::numeric_limits<std::int16_t>::min() ||
          value > std::numeric_limits<std::int16_t>::max()) {
        throw ScalarError("scalar value exceeds i16 storage");
      }
      validate_range(value, descriptor);
      append_u16(out, static_cast<std::uint16_t>(static_cast<std::int16_t>(value)));
      break;
    }
    case ScalarStorage::zstring: {
      if (text.find('\0') != std::string_view::npos) {
        throw ScalarError(std::string(descriptor.field_name) + " value contains an embedded NUL");
      }
      for (const char ch : text) out.push_back(std::byte{static_cast<std::uint8_t>(ch)});
      out.push_back(std::byte{0});
      break;
    }
    case ScalarStorage::fixed_string: {
      if (text.size() != descriptor.payload_size) {
        throw ScalarError(std::string(descriptor.field_name) + " must be exactly " +
                          std::to_string(descriptor.payload_size) + " characters");
      }
      for (const char ch : text) out.push_back(std::byte{static_cast<std::uint8_t>(ch)});
      break;
    }
    case ScalarStorage::u16: {
      const auto value = parse_integer<unsigned int>(text);
      if (value > std::numeric_limits<std::uint16_t>::max()) {
        throw ScalarError("scalar value exceeds u16 storage");
      }
      validate_range(value, descriptor);
      append_u16(out, static_cast<std::uint16_t>(value));
      break;
    }
    case ScalarStorage::u32: {
      const auto value = parse_integer<std::uint64_t>(text);
      if (value > std::numeric_limits<std::uint32_t>::max()) {
        throw ScalarError("scalar value exceeds u32 storage");
      }
      validate_range(static_cast<double>(value), descriptor);
      append_u32(out, static_cast<std::uint32_t>(value));
      break;
    }
    case ScalarStorage::i32: {
      const auto value = parse_integer<std::int64_t>(text);
      if (value < std::numeric_limits<std::int32_t>::min() ||
          value > std::numeric_limits<std::int32_t>::max()) {
        throw ScalarError("scalar value exceeds i32 storage");
      }
      validate_range(static_cast<double>(value), descriptor);
      append_u32(out, static_cast<std::uint32_t>(static_cast<std::int32_t>(value)));
      break;
    }
    case ScalarStorage::f32: {
      const auto parsed = parse_float(text);
      validate_range(parsed, descriptor);
      const auto value = static_cast<float>(parsed);
      if (!std::isfinite(value)) throw ScalarError("scalar value exceeds f32 storage");
      std::uint32_t bits = 0;
      static_assert(sizeof(value) == sizeof(bits));
      std::memcpy(&bits, &value, sizeof(bits));
      append_u32(out, bits);
      break;
    }
  }
  return out;
}

}  // namespace fo4::schema
