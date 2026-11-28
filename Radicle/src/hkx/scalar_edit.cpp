#include "fo4/hkx/scalar_edit.hpp"

#include "fo4/hkx/layout.hpp"
#include "fo4/hkx/packfile.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <charconv>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

namespace fo4::hkx {
namespace {

struct Scalar {
  HkxScalarKind kind{HkxScalarKind::Bool};
  bool is_enum{false};
  bool is_flags{false};
};

struct Range {
  std::int64_t low;
  std::int64_t high;
};

std::size_t width_of(HkxScalarKind kind) {
  switch (kind) {
    case HkxScalarKind::Bool:
    case HkxScalarKind::Int8:
    case HkxScalarKind::Uint8:
      return 1;
    case HkxScalarKind::Int16:
    case HkxScalarKind::Uint16:
      return 2;
    case HkxScalarKind::Int32:
    case HkxScalarKind::Uint32:
    case HkxScalarKind::Real:
      return 4;
  }
  return 0;
}

Range range_of(HkxScalarKind kind) {
  switch (kind) {
    case HkxScalarKind::Int8: return {std::numeric_limits<std::int8_t>::min(), std::numeric_limits<std::int8_t>::max()};
    case HkxScalarKind::Uint8: return {0, std::numeric_limits<std::uint8_t>::max()};
    case HkxScalarKind::Int16: return {std::numeric_limits<std::int16_t>::min(), std::numeric_limits<std::int16_t>::max()};
    case HkxScalarKind::Uint16: return {0, std::numeric_limits<std::uint16_t>::max()};
    case HkxScalarKind::Int32: return {std::numeric_limits<std::int32_t>::min(), std::numeric_limits<std::int32_t>::max()};
    case HkxScalarKind::Uint32: return {0, std::numeric_limits<std::uint32_t>::max()};
    default: return {0, 0};
  }
}

std::optional<HkxScalarKind> kind_of(std::string_view text) {
  struct Row {
    std::string_view name;
    HkxScalarKind kind;
  };
  static constexpr Row kRows[] = {
      {"bool", HkxScalarKind::Bool},     {"int8", HkxScalarKind::Int8},     {"uint8", HkxScalarKind::Uint8},
      {"int16", HkxScalarKind::Int16},   {"uint16", HkxScalarKind::Uint16}, {"int32", HkxScalarKind::Int32},
      {"uint32", HkxScalarKind::Uint32}, {"real", HkxScalarKind::Real},
  };
  for (const Row& row : kRows) {
    if (row.name == text) return row.kind;
  }
  return std::nullopt;
}

// Constraint: only the exact type texts of the layout table count, so "real x16", "half" and "array of real" stay out.
std::optional<Scalar> scalar_of(std::string_view type) {
  Scalar scalar;
  if (type.starts_with("enum of ")) {
    scalar.is_enum = true;
    type.remove_prefix(8);
  } else if (type.starts_with("flags of ")) {
    scalar.is_flags = true;
    type.remove_prefix(9);
  }
  const auto kind = kind_of(type);
  if (!kind) return std::nullopt;
  if ((scalar.is_enum || scalar.is_flags) && (*kind == HkxScalarKind::Bool || *kind == HkxScalarKind::Real)) {
    return std::nullopt;
  }
  scalar.kind = *kind;
  return scalar;
}

std::int64_t load_integer(std::span<const std::byte> bytes, HkxScalarKind kind) {
  std::uint64_t raw = 0;
  for (std::size_t i = 0; i < width_of(kind); ++i) {
    raw |= static_cast<std::uint64_t>(std::to_integer<std::uint8_t>(bytes[i])) << (8 * i);
  }
  switch (kind) {
    case HkxScalarKind::Int8: return static_cast<std::int8_t>(raw);
    case HkxScalarKind::Int16: return static_cast<std::int16_t>(raw);
    case HkxScalarKind::Int32: return static_cast<std::int32_t>(raw);
    default: return static_cast<std::int64_t>(raw);
  }
}

std::optional<std::string> value_text(HkxScalarKind kind, std::span<const std::byte> bytes) {
  if (kind == HkxScalarKind::Real) {
    char text[32];
    const float value = std::bit_cast<float>(static_cast<std::uint32_t>(load_integer(bytes, HkxScalarKind::Uint32)));
    const auto [end, error] = std::to_chars(text, text + sizeof text, value, std::chars_format::general, 9);
    if (error != std::errc{}) return std::nullopt;
    return std::string(text, end);
  }
  const std::int64_t number = load_integer(bytes, kind);
  if (kind == HkxScalarKind::Bool) {
    if (number > 1) return std::nullopt;
    return std::string(number == 1 ? "true" : "false");
  }
  return std::to_string(number);
}

struct Opened {
  HkxPackfile packfile;
  const HkxObjectView* object{nullptr};
  const HkxClassLayout* layout{nullptr};
  std::vector<HkxMemberLayout> members;
};

HkxScalarStatus open_object(std::span<const std::byte> source, std::size_t object_index, Opened& opened, std::string& why) {
  if (const auto status = read_packfile(source, opened.packfile); status != HkxStatus::Ok) {
    why = "the packfile could not be read, HkxStatus " + std::to_string(static_cast<int>(status));
    return HkxScalarStatus::BadPackfile;
  }
  if (opened.packfile.data_section >= opened.packfile.sections.size()) {
    why = "the packfile has no data section";
    return HkxScalarStatus::BadPackfile;
  }
  if (object_index >= opened.packfile.objects.size()) {
    why = "object index " + std::to_string(object_index) + " is past the " +
          std::to_string(opened.packfile.objects.size()) + " objects of the packfile";
    return HkxScalarStatus::NoObject;
  }
  opened.object = &opened.packfile.objects[object_index];
  opened.layout = find_layout(opened.object->class_name, opened.object->signature);
  if (opened.layout == nullptr) {
    why = "class " + std::string(opened.object->class_name) + " has no layout for this signature";
    return HkxScalarStatus::NoLayout;
  }
  opened.members = all_members(*opened.layout);
  return HkxScalarStatus::Ok;
}

// Invariant: Ok means the member is a written fixed-size scalar that fits its room and the object, and no fixup of the data section overlaps it.
HkxScalarStatus check_member(const Opened& opened, std::size_t index, Scalar& scalar, std::string& why) {
  const HkxMemberLayout& member = opened.members[index];
  const std::string name(member.name);
  const auto found = member.written ? scalar_of(member.type) : std::nullopt;
  if (!found) {
    why = "member " + name + " is not a fixed-size scalar (type \"" + std::string(member.type) + "\")";
    return HkxScalarStatus::NotScalar;
  }
  scalar = *found;
  const std::size_t width = width_of(scalar.kind);
  const std::uint32_t next = index + 1 < opened.members.size() ? opened.members[index + 1].offset : opened.layout->size;
  if (next < member.offset || width > next - member.offset ||
      static_cast<std::size_t>(member.offset) + width > opened.object->data.size()) {
    why = "member " + name + " does not fit the room before the next member or the object";
    return HkxScalarStatus::NotScalar;
  }
  const HkxSection& data = opened.packfile.sections[opened.packfile.data_section];
  const std::uint64_t begin = static_cast<std::uint64_t>(opened.object->offset) + member.offset;
  const auto covers = [&](std::uint32_t source) {
    return source < begin + width && static_cast<std::uint64_t>(source) + opened.packfile.pointer_size > begin;
  };
  const auto hit = [&](const auto& table) {
    return std::any_of(table.begin(), table.end(), [&](const auto& fixup) { return covers(fixup.source); });
  };
  if (hit(data.local_fixups) || hit(data.global_fixups) || hit(data.virtual_fixups)) {
    why = "member " + name + " is covered by a fixup in this file";
    return HkxScalarStatus::NotScalar;
  }
  return HkxScalarStatus::Ok;
}

HkxScalarMember row_of(const Opened& opened, std::size_t index, const Scalar& scalar, std::string text) {
  const HkxMemberLayout& member = opened.members[index];
  HkxScalarMember row;
  row.name = std::string(member.name);
  row.type = std::string(member.type);
  row.value = std::move(text);
  row.kind = scalar.kind;
  row.is_enum = scalar.is_enum;
  row.is_flags = scalar.is_flags;
  row.offset = member.offset;
  row.size = static_cast<std::uint32_t>(width_of(scalar.kind));
  if ((scalar.is_enum || scalar.is_flags) && member.enumeration != nullptr) {
    const auto bytes = opened.object->data.subspan(member.offset);
    row.enum_name = enum_value_name(*member.enumeration, scalar.is_flags, load_integer(bytes, scalar.kind));
  }
  return row;
}

std::vector<HkxScalarMember> list_rows(const Opened& opened) {
  std::vector<HkxScalarMember> rows;
  for (std::size_t index = 0; index < opened.members.size(); ++index) {
    Scalar scalar;
    std::string ignored;
    if (check_member(opened, index, scalar, ignored) != HkxScalarStatus::Ok) continue;
    auto text = value_text(scalar.kind, opened.object->data.subspan(opened.members[index].offset));
    if (text) rows.push_back(row_of(opened, index, scalar, std::move(*text)));
  }
  return rows;
}

HkxScalarStatus integer_of(std::string_view text, std::int64_t& out, std::string& why) {
  if (text.size() > 1 && text.front() == '+' && text[1] >= '0' && text[1] <= '9') text.remove_prefix(1);
  const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), out);
  if (error == std::errc::result_out_of_range) {
    why = "the integer does not fit 64 bits";
    return HkxScalarStatus::OutOfRange;
  }
  if (error != std::errc{} || end != text.data() + text.size()) {
    why = "\"" + std::string(text) + "\" is not a decimal integer";
    return HkxScalarStatus::BadValue;
  }
  return HkxScalarStatus::Ok;
}

// Constraint: std::from_chars rounds to the nearest float and never reads the C locale; a nonzero mantissa that ends as zero is an underflow and is refused.
HkxScalarStatus real_of(std::string_view text, float& out, std::string& why) {
  const bool plain = !text.empty() && std::all_of(text.begin(), text.end(), [](char ch) {
    return (ch >= '0' && ch <= '9') || ch == '.' || ch == '+' || ch == '-' || ch == 'e' || ch == 'E';
  });
  const std::string owned(text);
  if (!plain) {
    why = "\"" + owned + "\" is not a finite decimal number";
    return HkxScalarStatus::BadValue;
  }
  std::string_view digits = text;
  if (digits.size() > 1 && digits.front() == '+' && ((digits[1] >= '0' && digits[1] <= '9') || digits[1] == '.')) {
    digits.remove_prefix(1);
  }
  float value = 0.0f;
  const auto [end, error] = std::from_chars(digits.data(), digits.data() + digits.size(), value);
  if (error == std::errc::result_out_of_range) {
    why = "\"" + owned + "\" does not fit a 32-bit real";
    return HkxScalarStatus::OutOfRange;
  }
  if (error != std::errc{} || end != digits.data() + digits.size()) {
    why = "\"" + owned + "\" is not a finite decimal number";
    return HkxScalarStatus::BadValue;
  }
  if (value == 0.0f) {
    const auto mantissa = digits.substr(0, digits.find_first_of("eE"));
    if (mantissa.find_first_of("123456789") != std::string_view::npos) {
      why = "\"" + owned + "\" is too small for a 32-bit real";
      return HkxScalarStatus::OutOfRange;
    }
  }
  out = value;
  return HkxScalarStatus::Ok;
}

// Invariant: out holds the member's little-endian bytes, width_of(kind) of them; an enum value the table does not name is refused unless it is the one already stored, so a write of the current value stays a no-op.
HkxScalarStatus encode(const Scalar& scalar, const HkxMemberLayout& member, std::string_view text, std::int64_t current,
                       std::array<std::byte, 4>& out, std::string& why) {
  std::uint64_t raw = 0;
  if (scalar.kind == HkxScalarKind::Real) {
    float value = 0.0f;
    if (const auto status = real_of(text, value, why); status != HkxScalarStatus::Ok) return status;
    raw = std::bit_cast<std::uint32_t>(value);
  } else if (scalar.kind == HkxScalarKind::Bool) {
    if (text != "true" && text != "false" && text != "1" && text != "0") {
      why = "a bool takes true, false, 1 or 0, not \"" + std::string(text) + "\"";
      return HkxScalarStatus::BadValue;
    }
    raw = (text == "true" || text == "1") ? 1 : 0;
  } else {
    std::int64_t number = 0;
    const HkxEnumLayout* table = scalar.is_enum ? member.enumeration : nullptr;
    const HkxEnumValue* named = nullptr;
    if (table != nullptr) {
      for (const HkxEnumValue& row : table->values) {
        if (row.name == text) {
          named = &row;
          break;
        }
      }
    }
    if (named != nullptr) {
      number = named->value;
    } else if (const auto status = integer_of(text, number, why); status != HkxScalarStatus::Ok) {
      return status;
    }
    const Range range = range_of(scalar.kind);
    if (number < range.low || number > range.high) {
      why = std::to_string(number) + " is outside " + std::to_string(range.low) + " to " + std::to_string(range.high) +
            " for type \"" + std::string(member.type) + "\"";
      return HkxScalarStatus::OutOfRange;
    }
    if (table != nullptr && !table->values.empty() && number != current &&
        std::none_of(table->values.begin(), table->values.end(),
                     [&](const HkxEnumValue& row) { return row.value == number; })) {
      why = std::to_string(number) + " is not a value of enum " + std::string(table->name);
      return HkxScalarStatus::BadValue;
    }
    raw = static_cast<std::uint64_t>(number);
  }
  for (std::size_t i = 0; i < out.size(); ++i) out[i] = static_cast<std::byte>((raw >> (8 * i)) & 0xFFu);
  return HkxScalarStatus::Ok;
}

}  // namespace

const char* hkx_scalar_status_name(HkxScalarStatus status) {
  switch (status) {
    case HkxScalarStatus::Ok: return "ok";
    case HkxScalarStatus::BadPackfile: return "bad packfile";
    case HkxScalarStatus::NoObject: return "no such object";
    case HkxScalarStatus::NoLayout: return "no layout for the object's class";
    case HkxScalarStatus::NoMember: return "no such member";
    case HkxScalarStatus::NotScalar: return "not a fixed-size scalar member";
    case HkxScalarStatus::BadValue: return "bad value";
    case HkxScalarStatus::OutOfRange: return "value out of range";
    case HkxScalarStatus::VerifyFailed: return "the written file failed verification";
  }
  return "unknown";
}

HkxScalarStatus list_scalar_members(std::span<const std::byte> source, std::size_t object_index,
                                    std::vector<HkxScalarMember>& out, std::string& why) {
  out.clear();
  why.clear();
  Opened opened;
  if (const auto status = open_object(source, object_index, opened, why); status != HkxScalarStatus::Ok) return status;
  out = list_rows(opened);
  return HkxScalarStatus::Ok;
}

HkxScalarStatus set_scalar_member(std::span<const std::byte> source, std::size_t object_index,
                                  std::string_view member, std::string_view value, std::vector<std::byte>& out,
                                  std::string& why) {
  out.clear();
  why.clear();
  Opened opened;
  if (const auto status = open_object(source, object_index, opened, why); status != HkxScalarStatus::Ok) return status;
  const auto found = std::find_if(opened.members.begin(), opened.members.end(),
                                  [&](const HkxMemberLayout& row) { return row.name == member; });
  if (found == opened.members.end()) {
    why = "class " + std::string(opened.object->class_name) + " has no member " + std::string(member);
    return HkxScalarStatus::NoMember;
  }
  const auto index = static_cast<std::size_t>(found - opened.members.begin());
  Scalar scalar;
  if (const auto status = check_member(opened, index, scalar, why); status != HkxScalarStatus::Ok) return status;
  std::array<std::byte, 4> encoded{};
  const std::int64_t current =
      scalar.kind == HkxScalarKind::Real ? 0 : load_integer(opened.object->data.subspan(found->offset), scalar.kind);
  if (const auto status = encode(scalar, *found, value, current, encoded, why); status != HkxScalarStatus::Ok) {
    return status;
  }

  const std::size_t width = width_of(scalar.kind);
  const HkxSection& data = opened.packfile.sections[opened.packfile.data_section];
  const std::size_t at = data.file_offset + static_cast<std::size_t>(opened.object->offset) + found->offset;
  if (at > source.size() || width > source.size() - at) {
    why = "the member lies outside the file";
    return HkxScalarStatus::BadPackfile;
  }
  std::vector<std::byte> produced(source.begin(), source.end());
  std::copy_n(encoded.begin(), width, produced.begin() + static_cast<std::ptrdiff_t>(at));

  for (std::size_t i = 0; i < source.size(); ++i) {
    if (produced[i] != source[i] && (i < at || i >= at + width)) {
      why = "the write changed a byte outside the member";
      return HkxScalarStatus::VerifyFailed;
    }
  }
  Opened again;
  std::string ignored;
  if (open_object(produced, object_index, again, ignored) != HkxScalarStatus::Ok ||
      again.packfile.objects.size() != opened.packfile.objects.size()) {
    why = "the written file does not read back";
    return HkxScalarStatus::VerifyFailed;
  }
  const auto rows = list_rows(again);
  const auto expected = value_text(scalar.kind, std::span<const std::byte>(encoded.data(), width));
  const auto row = std::find_if(rows.begin(), rows.end(), [&](const HkxScalarMember& entry) { return entry.name == member; });
  if (!expected || row == rows.end() || row->value != *expected) {
    why = "the member does not read back as written";
    return HkxScalarStatus::VerifyFailed;
  }
  out = std::move(produced);
  return HkxScalarStatus::Ok;
}

}  // namespace fo4::hkx
