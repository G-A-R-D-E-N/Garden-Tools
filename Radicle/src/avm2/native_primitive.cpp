#include <fo4/avm2/coercion.hpp>
#include <fo4/avm2/interpreter.hpp>
#include <fo4/avm2/value.hpp>

#include "native_internal.hpp"

#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

namespace fo4::avm2 {

namespace {

constexpr std::string_view k_digits = "0123456789abcdefghijklmnopqrstuvwxyz";

std::string radix_string(std::int64_t value, std::uint32_t radix) {
  if (radix < 2 || radix > 36) {
    radix = 10;
  }
  if (value == 0) {
    return "0";
  }
  const bool negative = value < 0;
  std::uint64_t magnitude =
      negative ? static_cast<std::uint64_t>(-(value + 1)) + 1u : static_cast<std::uint64_t>(value);
  std::string text;
  while (magnitude != 0) {
    text.push_back(k_digits[static_cast<std::size_t>(magnitude % radix)]);
    magnitude /= radix;
  }
  if (negative) {
    text.push_back('-');
  }
  std::reverse(text.begin(), text.end());
  return text;
}

ExecutionResult number_construct(const Value& self, const std::vector<Value>& arguments) {
  (void)self;
  const Value argument = argument_at(arguments, 0);
  if (arguments.empty()) {
    return native_value(Value::number(0.0));
  }
  return native_value(Value::number(to_number(argument)));
}

ExecutionResult number_to_string(const Value& self, const std::vector<Value>& arguments) {
  const double value = to_number(native_self(self));
  const Value radix_value = argument_at(arguments, 0);
  if (!radix_value.is_undefined()) {
    const double radix = to_number(radix_value);
    if (!std::isnan(radix) && radix >= 2.0 && radix <= 36.0 &&
        std::floor(radix) == radix) {
      if (std::isnan(value) || std::isinf(value)) {
        return native_value(Value::string(to_string(Value::number(value))));
      }
      const auto integral = static_cast<std::int64_t>(value);
      if (static_cast<double>(integral) == value) {
        return native_value(
            Value::string(radix_string(integral, static_cast<std::uint32_t>(radix))));
      }
      return native_value(Value::string(to_string(Value::number(value))));
    }
  }
  return native_value(Value::string(to_string(Value::number(value))));
}

ExecutionResult number_to_fixed(const Value& self, const std::vector<Value>& arguments) {
  const double value = to_number(native_self(self));
  std::int32_t digits = to_int32(argument_at(arguments, 0));
  if (digits < 0 || digits > 20) {
    digits = 0;
  }
  if (std::isnan(value) || std::isinf(value)) {
    return native_value(Value::string(to_string(Value::number(value))));
  }
  std::array<char, 400> buffer{};
  const auto written = std::to_chars(buffer.data(), buffer.data() + buffer.size(), value,
                                     std::chars_format::fixed, digits);
  if (written.ec != std::errc{}) {
    return native_value(Value::string(to_string(Value::number(value))));
  }
  return native_value(Value::string(std::string{buffer.data(), written.ptr}));
}

ExecutionResult number_to_exponential(const Value& self, const std::vector<Value>& arguments) {
  const double value = to_number(native_self(self));
  std::int32_t digits = to_int32(argument_at(arguments, 0));
  if (digits < 0 || digits > 20) {
    digits = 6;
  }
  std::array<char, 400> buffer{};
  const auto written = std::to_chars(buffer.data(), buffer.data() + buffer.size(), value,
                                     std::chars_format::scientific, digits);
  if (written.ec != std::errc{}) {
    return native_value(Value::string(to_string(Value::number(value))));
  }
  return native_value(Value::string(std::string{buffer.data(), written.ptr}));
}

ExecutionResult number_to_precision(const Value& self, const std::vector<Value>& arguments) {
  const double value = to_number(native_self(self));
  std::int32_t digits = to_int32(argument_at(arguments, 0));
  if (digits < 1 || digits > 21) {
    digits = 21;
  }
  std::array<char, 400> buffer{};
  const auto written = std::to_chars(buffer.data(), buffer.data() + buffer.size(), value,
                                     std::chars_format::general, digits - 1);
  if (written.ec != std::errc{}) {
    return native_value(Value::string(to_string(Value::number(value))));
  }
  return native_value(Value::string(std::string{buffer.data(), written.ptr}));
}

ExecutionResult number_value_of(const Value& self) {
  return native_value(Value::number(to_number(native_self(self))));
}

ExecutionResult boolean_construct(const Value& self, const std::vector<Value>& arguments) {
  (void)self;
  if (arguments.empty()) {
    return native_value(Value::boolean(false));
  }
  return native_value(Value::boolean(to_boolean(arguments[0])));
}

ExecutionResult boolean_to_string(const Value& self) {
  return native_value(Value::string(to_boolean(native_self(self)) ? "true" : "false"));
}

ExecutionResult boolean_value_of(const Value& self) {
  return native_value(Value::boolean(to_boolean(native_self(self))));
}

ExecutionResult int_construct(const Value& self, const std::vector<Value>& arguments) {
  (void)self;
  if (arguments.empty()) {
    return native_value(Value::int32(0));
  }
  return native_value(Value::int32(to_int32(arguments[0])));
}

ExecutionResult int_to_string(const Value& self, const std::vector<Value>& arguments) {
  const std::int32_t value = to_int32(native_self(self));
  const Value radix_value = argument_at(arguments, 0);
  if (!radix_value.is_undefined()) {
    const double radix = to_number(radix_value);
    if (!std::isnan(radix) && radix >= 2.0 && radix <= 36.0 && std::floor(radix) == radix) {
      return native_value(
          Value::string(radix_string(value, static_cast<std::uint32_t>(radix))));
    }
  }
  return native_value(Value::string(std::to_string(value)));
}

ExecutionResult int_value_of(const Value& self) {
  return native_value(Value::int32(to_int32(native_self(self))));
}

ExecutionResult uint_construct(const Value& self, const std::vector<Value>& arguments) {
  (void)self;
  if (arguments.empty()) {
    return native_value(Value::uint32(0));
  }
  return native_value(Value::uint32(to_uint32(arguments[0])));
}

ExecutionResult uint_to_string(const Value& self, const std::vector<Value>& arguments) {
  const std::uint32_t value = to_uint32(native_self(self));
  const Value radix_value = argument_at(arguments, 0);
  if (!radix_value.is_undefined()) {
    const double radix = to_number(radix_value);
    if (!std::isnan(radix) && radix >= 2.0 && radix <= 36.0 && std::floor(radix) == radix) {
      return native_value(Value::string(
          radix_string(static_cast<std::int64_t>(value), static_cast<std::uint32_t>(radix))));
    }
  }
  return native_value(Value::string(std::to_string(value)));
}

ExecutionResult uint_value_of(const Value& self) {
  return native_value(Value::uint32(to_uint32(native_self(self))));
}

}  // namespace

bool try_primitive_native(Interpreter& interpreter, native::Id id, const Value& self,
                          const std::vector<Value>& arguments, std::size_t depth,
                          ExecutionResult& out) {
  (void)interpreter;
  (void)depth;
  switch (id) {
    case native::Id::number_construct:
      out = number_construct(self, arguments);
      return true;
    case native::Id::number_to_string:
      out = number_to_string(self, arguments);
      return true;
    case native::Id::number_to_fixed:
      out = number_to_fixed(self, arguments);
      return true;
    case native::Id::number_to_exponential:
      out = number_to_exponential(self, arguments);
      return true;
    case native::Id::number_to_precision:
      out = number_to_precision(self, arguments);
      return true;
    case native::Id::number_value_of:
      out = number_value_of(self);
      return true;
    case native::Id::boolean_construct:
      out = boolean_construct(self, arguments);
      return true;
    case native::Id::boolean_to_string:
      out = boolean_to_string(self);
      return true;
    case native::Id::boolean_value_of:
      out = boolean_value_of(self);
      return true;
    case native::Id::int_construct:
      out = int_construct(self, arguments);
      return true;
    case native::Id::int_to_string:
      out = int_to_string(self, arguments);
      return true;
    case native::Id::int_value_of:
      out = int_value_of(self);
      return true;
    case native::Id::uint_construct:
      out = uint_construct(self, arguments);
      return true;
    case native::Id::uint_to_string:
      out = uint_to_string(self, arguments);
      return true;
    case native::Id::uint_value_of:
      out = uint_value_of(self);
      return true;
    default:
      return false;
  }
}

}  // namespace fo4::avm2
