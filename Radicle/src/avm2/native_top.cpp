#include <fo4/avm2/class.hpp>
#include <fo4/avm2/coercion.hpp>
#include <fo4/avm2/interpreter.hpp>
#include <fo4/avm2/object.hpp>
#include <fo4/avm2/runtime.hpp>
#include <fo4/avm2/value.hpp>

#include "native_internal.hpp"

#include <cctype>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <limits>
#include <string>
#include <string_view>
#include <vector>

namespace fo4::avm2 {

namespace {

int digit_value(char character) {
  if (character >= '0' && character <= '9') {
    return character - '0';
  }
  if (character >= 'a' && character <= 'z') {
    return character - 'a' + 10;
  }
  if (character >= 'A' && character <= 'Z') {
    return character - 'A' + 10;
  }
  return 99;
}

double parse_int_text(std::string_view text, int radix) {
  std::size_t index = 0;
  while (index < text.size() && std::isspace(static_cast<unsigned char>(text[index])) != 0) {
    ++index;
  }
  bool negative = false;
  if (index < text.size() && (text[index] == '+' || text[index] == '-')) {
    negative = text[index] == '-';
    ++index;
  }
  if ((radix == 0 || radix == 16) && index + 1 < text.size() && text[index] == '0' &&
      (text[index + 1] == 'x' || text[index + 1] == 'X')) {
    index += 2;
    radix = 16;
  }
  if (radix == 0) {
    radix = 10;
  }
  if (radix < 2 || radix > 36) {
    return std::numeric_limits<double>::quiet_NaN();
  }
  double result = 0.0;
  bool any = false;
  for (; index < text.size(); ++index) {
    const int digit = digit_value(text[index]);
    if (digit >= radix) {
      break;
    }
    result = result * static_cast<double>(radix) + static_cast<double>(digit);
    any = true;
  }
  if (!any) {
    return std::numeric_limits<double>::quiet_NaN();
  }
  return negative ? -result : result;
}

double parse_float_text(std::string_view text) {
  std::size_t index = 0;
  while (index < text.size() && std::isspace(static_cast<unsigned char>(text[index])) != 0) {
    ++index;
  }
  const std::size_t start = index;
  if (index < text.size() && (text[index] == '+' || text[index] == '-')) {
    ++index;
  }
  bool digits = false;
  while (index < text.size() && text[index] >= '0' && text[index] <= '9') {
    ++index;
    digits = true;
  }
  if (index < text.size() && text[index] == '.') {
    ++index;
    while (index < text.size() && text[index] >= '0' && text[index] <= '9') {
      ++index;
      digits = true;
    }
  }
  if (!digits) {
    return std::numeric_limits<double>::quiet_NaN();
  }
  if (index < text.size() && (text[index] == 'e' || text[index] == 'E')) {
    std::size_t exponent = index + 1;
    if (exponent < text.size() && (text[exponent] == '+' || text[exponent] == '-')) {
      ++exponent;
    }
    if (exponent < text.size() && text[exponent] >= '0' && text[exponent] <= '9') {
      index = exponent;
      while (index < text.size() && text[index] >= '0' && text[index] <= '9') {
        ++index;
      }
    }
  }
  const std::string prefix{text.substr(start, index - start)};
  return std::strtod(prefix.c_str(), nullptr);
}

ExecutionResult to_object(Interpreter& interpreter, const Value& argument) {
  if (argument.is_object()) {
    return native_value(argument);
  }
  if (argument.is_nullish()) {
    const Class* object_class = interpreter.runtime().find_class("Object");
    if (object_class == nullptr) {
      return native_value(Value::object(std::make_shared<Object>()));
    }
    return native_value(
        Value::object(interpreter.make_instance(*const_cast<Class*>(object_class))));
  }
  return native_value(interpreter.box(argument));
}

}  // namespace

bool try_top_native(Interpreter& interpreter, native::Id id, const Value& self,
                    const std::vector<Value>& arguments, std::size_t depth,
                    ExecutionResult& out) {
  (void)self;
  (void)depth;
  switch (id) {
    case native::Id::trace:
      out = native_value(Value::undefined());
      return true;
    case native::Id::is_nan:
      out = native_value(Value::boolean(std::isnan(to_number(argument_at(arguments, 0)))));
      return true;
    case native::Id::is_finite:
      out = native_value(Value::boolean(std::isfinite(to_number(argument_at(arguments, 0)))));
      return true;
    case native::Id::parse_int: {
      const std::string text = to_string(argument_at(arguments, 0));
      const int radix = static_cast<int>(to_int32(argument_at(arguments, 1)));
      out = native_value(Value::number(parse_int_text(text, radix)));
      return true;
    }
    case native::Id::parse_float: {
      const std::string text = to_string(argument_at(arguments, 0));
      out = native_value(Value::number(parse_float_text(text)));
      return true;
    }
    case native::Id::to_string_value:
      out = native_value(Value::string(to_string(argument_at(arguments, 0))));
      return true;
    case native::Id::to_number_value:
      out = native_value(Value::number(to_number(argument_at(arguments, 0))));
      return true;
    case native::Id::to_boolean_value:
      out = native_value(Value::boolean(to_boolean(argument_at(arguments, 0))));
      return true;
    case native::Id::to_int_value:
      out = native_value(Value::int32(to_int32(argument_at(arguments, 0))));
      return true;
    case native::Id::to_uint_value:
      out = native_value(Value::uint32(to_uint32(argument_at(arguments, 0))));
      return true;
    case native::Id::to_object_value:
      out = to_object(interpreter, argument_at(arguments, 0));
      return true;
    case native::Id::get_timer:
      out = native_value(Value::number(0.0));
      return true;
    default:
      return false;
  }
}

}  // namespace fo4::avm2
