#include "hknp_decode_internal.hpp"

#include "fo4/binary/cursor.hpp"
#include "fo4/error.hpp"

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>

namespace fo4::nif::hknp_detail {
namespace {

fo4::binary::Cursor cursor_at(const HknpPackObject& object, std::size_t offset) {
  fo4::binary::Cursor cursor(object.bytes);
  cursor.seek(offset);
  return cursor;
}

}  // namespace

std::uint8_t u8_at(const HknpPackObject& object, std::size_t offset) {
  return cursor_at(object, offset).read_u8();
}

std::uint16_t u16_at(const HknpPackObject& object, std::size_t offset) {
  return cursor_at(object, offset).read_u16();
}

std::uint32_t u32_at(const HknpPackObject& object, std::size_t offset) {
  return cursor_at(object, offset).read_u32();
}

float f32_at(const HknpPackObject& object, std::size_t offset) {
  return cursor_at(object, offset).read_f32();
}

HknpVec3 vec3_at(const HknpPackObject& object, std::size_t offset) {
  auto cursor = cursor_at(object, offset);
  const float x = cursor.read_f32();
  const float y = cursor.read_f32();
  const float z = cursor.read_f32();
  return {x, y, z};
}

HknpVec4 vec4_at(const HknpPackObject& object, std::size_t offset) {
  auto cursor = cursor_at(object, offset);
  const float x = cursor.read_f32();
  const float y = cursor.read_f32();
  const float z = cursor.read_f32();
  const float w = cursor.read_f32();
  return {x, y, z, w};
}

HknpVec3 packed_vec3_at(const HknpPackObject& object, std::size_t offset) {
  const int exponent = static_cast<int>(u16_at(object, offset + 6) >> 7) - 96;
  const float scale = std::ldexp(1.0f, exponent) / 32768.0f;
  HknpVec3 out{};
  for (std::size_t k = 0; k < 3; ++k) {
    const auto mantissa = static_cast<std::int16_t>(u16_at(object, offset + 2 * k));
    out[k] = static_cast<float>(mantissa) * scale;
  }
  return out;
}

std::optional<std::size_t> local_target(const HknpPackObject& object, std::size_t source) {
  for (const auto& fixup : object.local) {
    if (fixup.source != source) {
      continue;
    }
    if (fixup.target > object.bytes.size()) {
      throw fo4::parse_error(source, "local fixup target lies outside its object");
    }
    return fixup.target;
  }
  return std::nullopt;
}

std::optional<std::size_t> global_target(const HknpPackfile& pack, const HknpPackObject& object, std::size_t source) {
  for (const auto& fixup : object.global) {
    if (fixup.source != source) {
      continue;
    }
    if (fixup.object >= pack.objects.size()) {
      throw fo4::parse_error(source, "global fixup names no object in the packfile");
    }
    return fixup.object;
  }
  return std::nullopt;
}

std::optional<ArrayRef> array_at(const HknpPackObject& object, std::size_t field) {
  const auto target = local_target(object, field);
  if (!target) {
    return std::nullopt;
  }
  return ArrayRef{*target, u32_at(object, field + 8)};
}

ArrayRef rel_array_at(const HknpPackObject& object, std::size_t field) {
  const std::uint16_t count = u16_at(object, field);
  const std::size_t offset = field + u16_at(object, field + 2);
  return ArrayRef{offset, count};
}

}  // namespace fo4::nif::hknp_detail
