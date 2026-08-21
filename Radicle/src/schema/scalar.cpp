#include "fo4/schema/scalar.hpp"

#include <charconv>
#include <cmath>
#include <cstring>
#include <limits>
#include <string>
#include <type_traits>

namespace fo4::schema {
namespace {

constexpr auto k_xhlt = Signature::from_string("XHLT");

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

}  // namespace

std::optional<ScalarDescriptor> scalar_descriptor(Signature subrecord) noexcept {
  if (subrecord == k_xhlt) {
    // xEdit FO4: XHLT is wbInteger(..., 'Health %', itU32).
    // Radicle's UI convention is human percentage points 0..100, stored directly
    // in the u32 field. Integer-backed percentages do not accept fractions.
    return ScalarDescriptor{
        k_xhlt, "Health %", ScalarStorage::u32,
        ScalarPresentation::percent_0_100, 0.0, 100.0};
  }
  return std::nullopt;
}

std::size_t scalar_storage_size(ScalarStorage storage) noexcept {
  switch (storage) {
    case ScalarStorage::u8:
      return 1;
    case ScalarStorage::u16:
      return 2;
    case ScalarStorage::u32:
    case ScalarStorage::i32:
    case ScalarStorage::f32:
      return 4;
  }
  return 0;
}

std::string decode_scalar(std::span<const std::byte> data,
                          const ScalarDescriptor& descriptor) {
  if (data.size() != scalar_storage_size(descriptor.storage)) {
    throw ScalarError(
        std::string(descriptor.field_name) + " has unexpected binary width");
  }

  switch (descriptor.storage) {
    case ScalarStorage::u8: {
      const auto value = std::to_integer<std::uint8_t>(data[0]);
      validate_range(value, descriptor);
      return std::to_string(value);
    }
    case ScalarStorage::u16: {
      const auto value = read_u16(data);
      validate_range(value, descriptor);
      return std::to_string(value);
    }
    case ScalarStorage::u32: {
      const auto value = read_u32(data);
      validate_range(static_cast<double>(value), descriptor);
      return std::to_string(value);
    }
    case ScalarStorage::i32: {
      const auto raw = read_u32(data);
      const auto value = static_cast<std::int32_t>(raw);
      validate_range(static_cast<double>(value), descriptor);
      return std::to_string(value);
    }
    case ScalarStorage::f32: {
      const auto bits = read_u32(data);
      float value = 0.0F;
      static_assert(sizeof(value) == sizeof(bits));
      std::memcpy(&value, &bits, sizeof(value));
      if (!std::isfinite(value)) throw ScalarError("stored scalar float is not finite");
      validate_range(value, descriptor);
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
