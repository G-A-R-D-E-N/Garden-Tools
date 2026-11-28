#include <fo4/avm2/coercion.hpp>
#include <fo4/avm2/interpreter.hpp>
#include <fo4/avm2/name.hpp>
#include <fo4/avm2/text.hpp>
#include <fo4/avm2/value.hpp>

#include "native_internal.hpp"

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace fo4::avm2 {

namespace {

std::string string_self(const Value& self) {
  const Value primitive = native_self(self);
  return to_string(primitive);
}

std::int32_t int_argument(const std::vector<Value>& arguments, std::size_t index,
                          std::int32_t fallback) {
  const Value value = argument_at(arguments, index);
  return value.is_undefined() ? fallback : to_int32(value);
}

std::size_t clamp_index(std::int32_t value, std::size_t low, std::size_t high) {
  if (value < static_cast<std::int32_t>(low)) {
    return low;
  }
  if (value > static_cast<std::int32_t>(high)) {
    return high;
  }
  return static_cast<std::size_t>(value);
}

ExecutionResult units_result(std::u16string_view units) {
  return native_value(Value::string(utf8_from_utf16(units)));
}

ExecutionResult string_char_at(const Value& self, const std::vector<Value>& arguments) {
  const std::u16string units = utf16_from_utf8(string_self(self));
  const std::int32_t index = int_argument(arguments, 0, 0);
  if (index < 0 || static_cast<std::size_t>(index) >= units.size()) {
    return native_value(Value::string(std::string{}));
  }
  return units_result(std::u16string_view(units).substr(static_cast<std::size_t>(index), 1));
}

ExecutionResult string_char_code_at(const Value& self, const std::vector<Value>& arguments) {
  const std::u16string units = utf16_from_utf8(string_self(self));
  const std::int32_t index = int_argument(arguments, 0, 0);
  if (index < 0 || static_cast<std::size_t>(index) >= units.size()) {
    return native_value(Value::number(std::numeric_limits<double>::quiet_NaN()));
  }
  return native_value(Value::number(static_cast<double>(units[static_cast<std::size_t>(index)])));
}

ExecutionResult string_index_of(const Value& self, const std::vector<Value>& arguments) {
  const std::u16string units = utf16_from_utf8(string_self(self));
  const std::u16string needle = utf16_from_utf8(to_string(argument_at(arguments, 0)));
  const std::size_t start = clamp_index(int_argument(arguments, 1, 0), 0, units.size());
  const std::size_t found = units.find(needle, start);
  return native_value(Value::int32(found == std::u16string::npos
                                       ? -1
                                       : static_cast<std::int32_t>(found)));
}

ExecutionResult string_last_index_of(const Value& self, const std::vector<Value>& arguments) {
  const std::u16string units = utf16_from_utf8(string_self(self));
  const std::u16string needle = utf16_from_utf8(to_string(argument_at(arguments, 0)));
  const Value from_value = argument_at(arguments, 1);
  const std::size_t from = from_value.is_undefined()
                               ? units.size()
                               : clamp_index(to_int32(from_value), 0, units.size());
  const std::size_t found = units.rfind(needle, from);
  return native_value(Value::int32(found == std::u16string::npos
                                       ? -1
                                       : static_cast<std::int32_t>(found)));
}

ExecutionResult string_substr(const Value& self, const std::vector<Value>& arguments) {
  const std::u16string units = utf16_from_utf8(string_self(self));
  std::int32_t start = int_argument(arguments, 0, 0);
  if (start < 0) {
    start = static_cast<std::int32_t>(units.size()) + start;
    if (start < 0) {
      start = 0;
    }
  }
  const std::int32_t length = int_argument(arguments, 1,
                                           static_cast<std::int32_t>(units.size()));
  if (length <= 0 || static_cast<std::size_t>(start) >= units.size()) {
    return native_value(Value::string(std::string{}));
  }
  const std::size_t begin = clamp_index(start, 0, units.size());
  const std::size_t end = std::min(units.size(), begin + static_cast<std::size_t>(length));
  return units_result(units.substr(begin, end - begin));
}

ExecutionResult string_substring(const Value& self, const std::vector<Value>& arguments) {
  const std::u16string units = utf16_from_utf8(string_self(self));
  std::size_t begin = clamp_index(int_argument(arguments, 0, 0), 0, units.size());
  std::size_t end = argument_at(arguments, 1).is_undefined()
                        ? units.size()
                        : clamp_index(to_int32(argument_at(arguments, 1)), 0, units.size());
  if (begin > end) {
    std::swap(begin, end);
  }
  return units_result(units.substr(begin, end - begin));
}

ExecutionResult string_slice(const Value& self, const std::vector<Value>& arguments) {
  const std::u16string units = utf16_from_utf8(string_self(self));
  const auto resolve = [size = units.size()](std::int32_t value) {
    if (value < 0) {
      const std::int64_t adjusted = static_cast<std::int64_t>(size) + value;
      return adjusted < 0 ? std::size_t{0} : static_cast<std::size_t>(adjusted);
    }
    return std::min(static_cast<std::size_t>(value), size);
  };
  const std::size_t begin = resolve(int_argument(arguments, 0, 0));
  const std::size_t end = argument_at(arguments, 1).is_undefined()
                              ? units.size()
                              : resolve(to_int32(argument_at(arguments, 1)));
  if (begin >= end) {
    return native_value(Value::string(std::string{}));
  }
  return units_result(units.substr(begin, end - begin));
}

ExecutionResult string_split(Interpreter& interpreter, const Value& self,
                             const std::vector<Value>& arguments) {
  const std::string text = string_self(self);
  const std::u16string units = utf16_from_utf8(text);
  const Value delimiter = argument_at(arguments, 0);
  const std::int32_t limit = int_argument(arguments, 1, 0);
  std::vector<Value> parts;
  const auto push_part = [&parts, limit](std::u16string_view part) {
    if (limit > 0 && parts.size() >= static_cast<std::size_t>(limit)) {
      return;
    }
    parts.push_back(Value::string(utf8_from_utf16(part)));
  };
  if (delimiter.is_undefined() || to_string(delimiter) == text) {
    push_part(units);
  } else {
    const std::u16string separator = utf16_from_utf8(to_string(delimiter));
    if (separator.empty()) {
      for (std::size_t index = 0; index < units.size(); ++index) {
        push_part(std::u16string_view(units).substr(index, 1));
      }
    } else {
      std::size_t start = 0;
      while (start <= units.size()) {
        const std::size_t found = units.find(separator, start);
        if (found == std::u16string::npos) {
          push_part(std::u16string_view(units).substr(start));
          break;
        }
        push_part(std::u16string_view(units).substr(start, found - start));
        start = found + separator.size();
      }
    }
  }
  return native_value(Value::object(interpreter.make_array(std::move(parts))));
}

ExecutionResult string_to_case(const Value& self, bool upper) {
  std::string text = string_self(self);
  for (char& character : text) {
    const auto raw = static_cast<unsigned char>(character);
    character = static_cast<char>(upper ? std::toupper(raw) : std::tolower(raw));
  }
  return native_value(Value::string(std::move(text)));
}

ExecutionResult string_replace(Interpreter& interpreter, const Value& self,
                               const std::vector<Value>& arguments) {
  const std::string text = string_self(self);
  const std::string pattern = to_string(argument_at(arguments, 0));
  const Value replacement = argument_at(arguments, 1);
  const std::size_t found = text.find(pattern);
  if (found == std::string::npos) {
    return native_value(Value::string(text));
  }
  std::string substitute;
  if (replacement.is_object() && replacement.as_object()->kind() == ObjectKind::Function) {
    std::vector<Value> call_arguments{Value::string(pattern)};
    ExecutionResult called = interpreter.call_function(
        static_cast<const FunctionObject&>(*replacement.as_object()), Value::undefined(),
        std::move(call_arguments), 0);
    if (!called.ok()) {
      return called;
    }
    substitute = to_string(called.value);
  } else {
    substitute = to_string(replacement);
  }
  std::string result = text;
  result.replace(found, pattern.size(), substitute);
  return native_value(Value::string(std::move(result)));
}

ExecutionResult string_concat(const Value& self, const std::vector<Value>& arguments) {
  std::string text = string_self(self);
  for (const Value& argument : arguments) {
    text += to_string(argument);
  }
  return native_value(Value::string(std::move(text)));
}

ExecutionResult string_trim(const Value& self) {
  const std::string text = string_self(self);
  std::size_t begin = 0;
  while (begin < text.size() && std::isspace(static_cast<unsigned char>(text[begin])) != 0) {
    ++begin;
  }
  std::size_t end = text.size();
  while (end > begin && std::isspace(static_cast<unsigned char>(text[end - 1])) != 0) {
    end -= 1;
  }
  return native_value(Value::string(text.substr(begin, end - begin)));
}

ExecutionResult string_locale_compare(const Value& self, const std::vector<Value>& arguments) {
  const std::string text = string_self(self);
  const std::string other = to_string(argument_at(arguments, 0));
  if (text < other) {
    return native_value(Value::int32(-1));
  }
  return native_value(Value::int32(text > other ? 1 : 0));
}

ExecutionResult string_from_char_code(const std::vector<Value>& arguments) {
  std::u16string units;
  for (const Value& argument : arguments) {
    units.push_back(static_cast<char16_t>(static_cast<std::uint16_t>(to_int32(argument))));
  }
  return units_result(units);
}

}  // namespace

bool try_string_native(Interpreter& interpreter, native::Id id, const Value& self,
                       const std::vector<Value>& arguments, std::size_t depth,
                       ExecutionResult& out) {
  (void)depth;
  switch (id) {
    case native::Id::string_char_at:
      out = string_char_at(self, arguments);
      return true;
    case native::Id::string_char_code_at:
      out = string_char_code_at(self, arguments);
      return true;
    case native::Id::string_index_of:
      out = string_index_of(self, arguments);
      return true;
    case native::Id::string_last_index_of:
      out = string_last_index_of(self, arguments);
      return true;
    case native::Id::string_substr:
      out = string_substr(self, arguments);
      return true;
    case native::Id::string_substring:
      out = string_substring(self, arguments);
      return true;
    case native::Id::string_slice:
      out = string_slice(self, arguments);
      return true;
    case native::Id::string_split:
      out = string_split(interpreter, self, arguments);
      return true;
    case native::Id::string_to_upper:
      out = string_to_case(self, true);
      return true;
    case native::Id::string_to_lower:
      out = string_to_case(self, false);
      return true;
    case native::Id::string_replace:
      out = string_replace(interpreter, self, arguments);
      return true;
    case native::Id::string_concat:
      out = string_concat(self, arguments);
      return true;
    case native::Id::string_trim:
      out = string_trim(self);
      return true;
    case native::Id::string_to_string:
    case native::Id::string_value_of:
      out = native_value(Value::string(string_self(self)));
      return true;
    case native::Id::string_locale_compare:
      out = string_locale_compare(self, arguments);
      return true;
    case native::Id::string_from_char_code:
      out = string_from_char_code(arguments);
      return true;
    default:
      return false;
  }
}

}  // namespace fo4::avm2
