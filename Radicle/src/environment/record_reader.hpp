#pragma once

#include "fo4/environment/world.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <optional>
#include <string>

namespace fo4::env::detail {

using Vec3f = std::array<float, 3>;

class Reader {
public:
  Reader(const Environment& env, const RecordRef& ref, Signature expected)
      : env_(env), ref_(ref) {
    if (ref.record == nullptr || ref.record->header.signature != expected) {
      throw WorldDecodeError("expected a " + expected.to_string() + " record");
    }
  }

  // Caller has already validated the record signature.
  Reader(const Environment& env, const RecordRef& ref) : env_(env), ref_(ref) {}

  FormKey link_at(const Subrecord& subrecord, std::size_t offset) const {
    return env_.resolve_link(ref_, RawFormId{u32(subrecord, offset)});
  }

  Vec3f vec3(const Subrecord& subrecord, std::size_t offset) const {
    return {f32(subrecord, offset), f32(subrecord, offset + 4), f32(subrecord, offset + 8)};
  }

  const Subrecord* unique(Signature signature) const {
    return unique(*ref_.record, signature);
  }

  static const Subrecord* unique(const Record& record, Signature signature) {
    const Subrecord* found = nullptr;
    for (const auto& subrecord : record.subrecords) {
      if (subrecord.signature != signature) continue;
      if (found != nullptr) fail(record, signature, "appears more than once");
      found = &subrecord;
    }
    return found;
  }

  const Subrecord* sized(Signature signature, std::size_t size) const {
    return sized(*ref_.record, signature, size);
  }

  static const Subrecord* sized(const Record& record, Signature signature, std::size_t size) {
    const auto* subrecord = unique(record, signature);
    if (subrecord != nullptr && subrecord->data.size() != size) {
      fail(record, signature, "must be " + std::to_string(size) + " bytes");
    }
    return subrecord;
  }

  std::optional<FormKey> link(Signature signature) const {
    const auto* subrecord = sized(signature, 4);
    if (subrecord == nullptr) return std::nullopt;
    return env_.resolve_link(ref_, RawFormId{u32(*subrecord, 0)});
  }

  std::optional<float> f32(Signature signature) const {
    const auto* subrecord = sized(signature, 4);
    if (subrecord == nullptr) return std::nullopt;
    return f32(*subrecord, 0);
  }

  [[noreturn]] void fail(Signature signature, const std::string& message) const {
    fail(*ref_.record, signature, message);
  }

  [[noreturn]] static void fail(const Record& record, Signature signature, const std::string& message) {
    throw WorldDecodeError(record.header.signature.to_string() + " " +
                           record.form_key.to_string() + " " + signature.to_string() +
                           " " + message);
  }

  static std::uint32_t u32(const Subrecord& subrecord, std::size_t offset) {
    std::uint32_t value = 0;
    std::memcpy(&value, subrecord.data.data() + offset, 4);
    return value;
  }

  static std::int32_t i32(const Subrecord& subrecord, std::size_t offset) {
    std::int32_t value = 0;
    std::memcpy(&value, subrecord.data.data() + offset, 4);
    return value;
  }

  static std::uint16_t u16(const Subrecord& subrecord, std::size_t offset) {
    std::uint16_t value = 0;
    std::memcpy(&value, subrecord.data.data() + offset, 2);
    return value;
  }

  static float f32(const Subrecord& subrecord, std::size_t offset) {
    float value = 0.0f;
    std::memcpy(&value, subrecord.data.data() + offset, 4);
    return value;
  }

  static ByteColor color(const Subrecord& subrecord, std::size_t offset) {
    const auto* bytes = subrecord.data.data() + offset;
    return {std::to_integer<std::uint8_t>(bytes[0]), std::to_integer<std::uint8_t>(bytes[1]),
            std::to_integer<std::uint8_t>(bytes[2])};
  }

  const RecordRef& ref() const noexcept { return ref_; }

private:
  const Environment& env_;
  const RecordRef& ref_;
};

}  // namespace fo4::env::detail
