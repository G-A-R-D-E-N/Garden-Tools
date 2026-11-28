#pragma once

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace fo4::hkx {

// Invariant: Other covers every member the game does not write and every type no reader walks (pointers, fixed arrays, ulong, matrix4, variant, simple arrays); enums and flags take their storage kind; Struct, Array of structs and RelArray of structs name their class in type_class.
enum class HkxFieldKind : std::uint8_t {
  Other = 0,
  Bool,
  Int8,
  Uint8,
  Int16,
  Uint16,
  Int32,
  Uint32,
  Int64,
  Uint64,
  Real,
  String,
  Struct,
  Array,
  Vector4,
  Quaternion,
  QsTransform,
  Transform,
  Matrix3,
  Half,
  RelArray,
};

struct HkxEnumValue {
  std::string_view name;
  std::int64_t value;
};

// Invariant: values are ordered by (value, name), so the first name of an aliased value is stable; owner is the class that declares the enum in HavokClassTypes.json.
struct HkxEnumLayout {
  std::string_view name;
  std::string_view owner;
  std::span<const HkxEnumValue> values;
};

// Invariant: element is Other unless kind is Array or RelArray; type_class is empty unless kind is Struct or the array holds structs; enumeration is null unless the member is a written enum or flags member whose table the class types name; type is the member type text of HavokClassLayouts.json ("real", "enum of int8", "array of pointer"), empty when the table has none; written is false for a runtime-only member a file never stores, and kind is then Other.
struct HkxMemberLayout {
  std::string_view name;
  std::uint32_t offset;
  HkxFieldKind kind;
  HkxFieldKind element;
  std::string_view type_class;
  std::string_view type;
  bool written;
  const HkxEnumLayout* enumeration;
};

// Invariant: members are the class's own members at absolute offsets; parent names the base class in this table, empty when it has none here.
struct HkxClassLayout {
  std::string_view name;
  std::uint32_t signature;
  std::uint32_t size;
  std::span<const HkxMemberLayout> members;
  std::string_view parent;
};

const HkxClassLayout* find_layout(std::string_view class_name, std::uint32_t signature);
const HkxClassLayout* find_layout(std::string_view class_name);
std::optional<std::uint32_t> find_member_offset(const HkxClassLayout& layout, std::string_view member_name);

// Invariant: empty when nothing is named; a flags value is named only when every set bit is, joined by "|"; an enum value takes the first name of that value.
std::string enum_value_name(const HkxEnumLayout& table, bool flags, std::int64_t value);

// Invariant: base-class members come first, so the list is in offset order; the walk stops at a parent the table lacks.
std::vector<HkxMemberLayout> all_members(const HkxClassLayout& layout);

}  // namespace fo4::hkx
