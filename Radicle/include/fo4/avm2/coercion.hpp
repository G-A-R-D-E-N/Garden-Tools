#pragma once

#include <fo4/avm2/value.hpp>

#include <cstdint>
#include <string>

namespace fo4::avm2 {

enum class PrimitiveHint : std::uint8_t {
  Default = 0,
  Number,
  String,
};

bool to_boolean(const Value& value) noexcept;
std::int32_t to_int32(const Value& value) noexcept;
std::uint32_t to_uint32(const Value& value) noexcept;
double to_number(const Value& value) noexcept;
std::string to_string(const Value& value);
std::string typeof_name(const Value& value);

Value to_primitive(const Value& value, PrimitiveHint hint);

bool strict_equals(const Value& left, const Value& right) noexcept;
bool loose_equals(const Value& left, const Value& right);

bool less_than(const Value& left, const Value& right);
bool less_equals(const Value& left, const Value& right);
bool greater_than(const Value& left, const Value& right);
bool greater_equals(const Value& left, const Value& right);

}  // namespace fo4::avm2
