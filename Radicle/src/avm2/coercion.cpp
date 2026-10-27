#include <fo4/avm2/coercion.hpp>

#include <fo4/avm2/class.hpp>
#include <fo4/avm2/object.hpp>

#include <array>
#include <charconv>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <string>
#include <string_view>
#include <system_error>

namespace fo4::avm2 {

namespace {

constexpr double k_two_to_32 = 4294967296.0;

bool is_ascii_space(char ch) noexcept {
  switch (ch) {
    case ' ':
    case '\t':
    case '\n':
    case '\r':
    case '\v':
    case '\f':
      return true;
    default:
      return false;
  }
}

std::string_view trim_ascii(std::string_view text) noexcept {
  std::size_t begin = 0;
  std::size_t end = text.size();
  while (begin < end && is_ascii_space(text[begin])) {
    ++begin;
  }
  while (end > begin && is_ascii_space(text[end - 1])) {
    end -= 1;
  }
  return text.substr(begin, end - begin);
}

double string_to_number(std::string_view text) noexcept {
  text = trim_ascii(text);
  if (text.empty()) {
    return 0.0;
  }
  bool negative = false;
  std::size_t at = 0;
  if (text[at] == '+' || text[at] == '-') {
    negative = text[at] == '-';
    ++at;
  }
  const std::string_view rest = text.substr(at);
  if (rest == "Infinity") {
    return negative ? -std::numeric_limits<double>::infinity()
                    : std::numeric_limits<double>::infinity();
  }
  if (rest.size() > 2 && rest[0] == '0' && (rest[1] == 'x' || rest[1] == 'X')) {
    if (negative) {
      return std::numeric_limits<double>::quiet_NaN();
    }
    double value = 0.0;
    for (const char digit : rest.substr(2)) {
      int nibble = -1;
      if (digit >= '0' && digit <= '9') {
        nibble = digit - '0';
      } else if (digit >= 'a' && digit <= 'f') {
        nibble = digit - 'a' + 10;
      } else if (digit >= 'A' && digit <= 'F') {
        nibble = digit - 'A' + 10;
      }
      if (nibble < 0) {
        return std::numeric_limits<double>::quiet_NaN();
      }
      value = value * 16.0 + static_cast<double>(nibble);
    }
    return value;
  }

  if (rest.empty()) {
    return std::numeric_limits<double>::quiet_NaN();
  }
  double value = 0.0;
  const auto parsed = std::from_chars(rest.data(), rest.data() + rest.size(), value);
  if (parsed.ec != std::errc{} || parsed.ptr != rest.data() + rest.size()) {
    return std::numeric_limits<double>::quiet_NaN();
  }
  return negative ? -value : value;
}

std::int32_t number_to_int32(double value) noexcept {
  if (std::isnan(value) || std::isinf(value)) {
    return 0;
  }
  const double truncated = std::trunc(value);
  if (truncated == 0.0) {
    return 0;
  }
  const double shifted = std::fmod(truncated, k_two_to_32);
  const double positive = shifted < 0.0 ? shifted + k_two_to_32 : shifted;
  if (positive >= 2147483648.0) {
    return static_cast<std::int32_t>(positive - k_two_to_32);
  }
  return static_cast<std::int32_t>(positive);
}

std::uint32_t number_to_uint32(double value) noexcept {
  if (std::isnan(value) || std::isinf(value)) {
    return 0;
  }
  const double truncated = std::trunc(value);
  if (truncated == 0.0) {
    return 0;
  }
  const double shifted = std::fmod(truncated, k_two_to_32);
  const double positive = shifted < 0.0 ? shifted + k_two_to_32 : shifted;
  return static_cast<std::uint32_t>(positive);
}

std::string number_to_string(double value) {
  if (std::isnan(value)) {
    return "NaN";
  }
  if (std::isinf(value)) {
    return value < 0.0 ? "-Infinity" : "Infinity";
  }
  if (value == 0.0) {
    return "0";
  }
  std::array<char, 400> buffer{};
  const bool integral = std::trunc(value) == value;
  if (integral && std::fabs(value) < 1e21) {
    const auto written = std::to_chars(buffer.data(), buffer.data() + buffer.size(), value,
                                       std::chars_format::fixed, 0);
    if (written.ec == std::errc{}) {
      return std::string{buffer.data(), written.ptr};
    }
  }
  const auto written = std::to_chars(buffer.data(), buffer.data() + buffer.size(), value);
  if (written.ec == std::errc{}) {
    return std::string{buffer.data(), written.ptr};
  }
  return "NaN";
}

std::string array_to_string(const ArrayObject& array) {
  std::string text;
  for (std::size_t index = 0; index < array.elements().size(); ++index) {
    if (index != 0) {
      text += ",";
    }
    const Value& element = array.elements()[index];
    if (!element.is_nullish()) {
      text += to_string(element);
    }
  }
  return text;
}

std::string object_default_to_string(const Object& object) {
  switch (object.kind()) {
    case ObjectKind::Array:
      return array_to_string(static_cast<const ArrayObject&>(object));
    case ObjectKind::Error: {
      const auto& error = static_cast<const ErrorObject&>(object);
      if (error.message().empty()) {
        return error.error_name();
      }
      return error.error_name() + ": " + error.message();
    }
    case ObjectKind::Function: {
      const auto& function = static_cast<const FunctionObject&>(object);
      return "function " + function.name() + "() {}";
    }
    case ObjectKind::Class: {
      const auto& class_object = static_cast<const ClassObject&>(object);
      return "class " + (class_object.represented_class() == nullptr
                             ? std::string{"<unnamed>"}
                             : class_object.represented_class()->name);
    }
    case ObjectKind::Object:
    case ObjectKind::Namespace:
    case ObjectKind::Display:
    case ObjectKind::Event:
      return "[object Object]";
  }
  return "[object Object]";
}

}  // namespace

bool to_boolean(const Value& value) noexcept {
  switch (value.kind()) {
    case ValueKind::Undefined:
    case ValueKind::Null:
      return false;
    case ValueKind::Boolean:
      return value.as_boolean();
    case ValueKind::Int32:
      return value.as_int32() != 0;
    case ValueKind::UInt32:
      return value.as_uint32() != 0;
    case ValueKind::Number:
      return !(std::isnan(value.as_number()) || value.as_number() == 0.0);
    case ValueKind::String:
      return !value.as_string().empty();
    case ValueKind::Object:
      return true;
  }
  return false;
}

std::int32_t to_int32(const Value& value) noexcept {
  if (value.is_int32()) {
    return value.as_int32();
  }
  return number_to_int32(to_number(value));
}

std::uint32_t to_uint32(const Value& value) noexcept {
  if (value.is_uint32()) {
    return value.as_uint32();
  }
  if (value.is_int32()) {
    return static_cast<std::uint32_t>(value.as_int32());
  }
  return number_to_uint32(to_number(value));
}

double to_number(const Value& value) noexcept {
  switch (value.kind()) {
    case ValueKind::Undefined:
      return std::numeric_limits<double>::quiet_NaN();
    case ValueKind::Null:
      return 0.0;
    case ValueKind::Boolean:
      return value.as_boolean() ? 1.0 : 0.0;
    case ValueKind::Int32:
      return static_cast<double>(value.as_int32());
    case ValueKind::UInt32:
      return static_cast<double>(value.as_uint32());
    case ValueKind::Number:
      return value.as_number();
    case ValueKind::String:
      return string_to_number(value.as_string());
    case ValueKind::Object:

      return std::numeric_limits<double>::quiet_NaN();
  }
  return std::numeric_limits<double>::quiet_NaN();
}

std::string to_string(const Value& value) {
  switch (value.kind()) {
    case ValueKind::Undefined:
      return "undefined";
    case ValueKind::Null:
      return "null";
    case ValueKind::Boolean:
      return value.as_boolean() ? "true" : "false";
    case ValueKind::Int32:
      return std::to_string(value.as_int32());
    case ValueKind::UInt32:
      return std::to_string(value.as_uint32());
    case ValueKind::Number:
      return number_to_string(value.as_number());
    case ValueKind::String:
      return value.as_string();
    case ValueKind::Object:
      return object_default_to_string(*value.as_object());
  }
  return {};
}

std::string typeof_name(const Value& value) {
  switch (value.kind()) {
    case ValueKind::Undefined:
      return "undefined";
    case ValueKind::Null:
      return "object";
    case ValueKind::Boolean:
      return "boolean";
    case ValueKind::Int32:
    case ValueKind::UInt32:
    case ValueKind::Number:
      return "number";
    case ValueKind::String:
      return "string";
    case ValueKind::Object:
      return value.as_object()->kind() == ObjectKind::Function ? "function" : "object";
  }
  return "object";
}

Value to_primitive(const Value& value, PrimitiveHint hint) {
  if (!value.is_object()) {
    return value;
  }

  Value text = string_value(object_default_to_string(*value.as_object()));
  if (hint == PrimitiveHint::Number) {
    return number_value(to_number(text));
  }
  return text;
}

bool strict_equals(const Value& left, const Value& right) noexcept {
  const bool left_number = left.is_number_like();
  const bool right_number = right.is_number_like();
  if (left_number && right_number) {
    const double l = left.as_number();
    const double r = right.as_number();
    if (std::isnan(l) || std::isnan(r)) {
      return false;
    }
    return l == r;
  }
  if (left.kind() != right.kind()) {
    return false;
  }
  switch (left.kind()) {
    case ValueKind::Undefined:
    case ValueKind::Null:
      return true;
    case ValueKind::Boolean:
      return left.as_boolean() == right.as_boolean();
    case ValueKind::String:
      return left.as_string() == right.as_string();
    case ValueKind::Object:
      return left.as_object() == right.as_object();
    case ValueKind::Int32:
    case ValueKind::UInt32:
    case ValueKind::Number:
      return false;
  }
  return false;
}

bool loose_equals(const Value& left, const Value& right) {
  if (left.is_number_like() && right.is_number_like()) {
    return strict_equals(left, right);
  }
  if (left.kind() == right.kind()) {
    return strict_equals(left, right);
  }
  if (left.is_nullish() && right.is_nullish()) {
    return true;
  }
  if (left.is_nullish() || right.is_nullish()) {
    return false;
  }
  const auto object_primitive = [](const Value& value, Value& out) {
    if (!value.is_object()) {
      return;
    }
    out = to_primitive(value, PrimitiveHint::Default);
  };
  Value left_value = left;
  Value right_value = right;
  object_primitive(left_value, left_value);
  object_primitive(right_value, right_value);
  if (left_value.is_object() || right_value.is_object()) {
    return strict_equals(left_value, right_value);
  }
  if (left_value.is_boolean() || right_value.is_boolean()) {
    return to_number(left_value) == to_number(right_value);
  }
  if (left_value.is_string() != right_value.is_string()) {
    return to_number(left_value) == to_number(right_value);
  }
  return strict_equals(left_value, right_value);
}

namespace {

bool relational(const Value& left, const Value& right, bool want_less) {
  const Value left_primitive = to_primitive(left, PrimitiveHint::Number);
  const Value right_primitive = to_primitive(right, PrimitiveHint::Number);
  if (left_primitive.is_string() && right_primitive.is_string()) {
    return want_less ? left_primitive.as_string() < right_primitive.as_string()
                     : right_primitive.as_string() < left_primitive.as_string();
  }
  const double l = to_number(left_primitive);
  const double r = to_number(right_primitive);
  if (std::isnan(l) || std::isnan(r)) {
    return false;
  }
  return want_less ? l < r : r < l;
}

}  // namespace

bool less_than(const Value& left, const Value& right) { return relational(left, right, true); }

bool less_equals(const Value& left, const Value& right) {
  const Value left_primitive = to_primitive(left, PrimitiveHint::Number);
  const Value right_primitive = to_primitive(right, PrimitiveHint::Number);
  if (left_primitive.is_string() && right_primitive.is_string()) {
    return !(right_primitive.as_string() < left_primitive.as_string());
  }
  const double l = to_number(left_primitive);
  const double r = to_number(right_primitive);
  if (std::isnan(l) || std::isnan(r)) {
    return false;
  }
  return l <= r;
}

bool greater_than(const Value& left, const Value& right) { return relational(right, left, true); }

bool greater_equals(const Value& left, const Value& right) {
  return less_equals(right, left);
}

}  // namespace fo4::avm2
