#include <fo4/avm2/value.hpp>

#include <fo4/avm2/text.hpp>

#include <cstddef>
#include <stdexcept>
#include <string>
#include <string_view>

namespace fo4::avm2 {

namespace {

[[noreturn]] void wrong_kind(const char* wanted) {
  throw std::logic_error(std::string{"avm2 value is not "} + wanted);
}

void join_surrogate_halves(std::string& text) {
  for (std::size_t index = text.find('\xED'); index != std::string::npos;
       index = text.find('\xED', index + 1)) {
    const std::u16string units = utf16_from_utf8(std::string_view(text).substr(index, 6));
    if (units.size() >= 2 && units[0] >= 0xD800 && units[0] <= 0xDBFF && units[1] >= 0xDC00 &&
        units[1] <= 0xDFFF) {
      text.replace(index, 6, utf8_from_utf16(units.substr(0, 2)));
    }
  }
}

}  // namespace

Value::Value() : storage_{UndefinedValue{}} {}

Value Value::undefined() { return Value{}; }

Value Value::null() {
  Value value;
  value.storage_ = NullValue{};
  return value;
}

Value Value::boolean(bool flag) {
  Value value;
  value.storage_ = flag;
  return value;
}

Value Value::int32(std::int32_t number) {
  Value value;
  value.storage_ = number;
  return value;
}

Value Value::uint32(std::uint32_t number) {
  Value value;
  value.storage_ = number;
  return value;
}

Value Value::number(double number) {
  Value value;
  value.storage_ = number;
  return value;
}

Value Value::string(std::string text) {
  Value value;
  join_surrogate_halves(text);
  value.storage_ = std::move(text);
  return value;
}

Value Value::object(std::shared_ptr<Object> object) {
  Value value;
  value.storage_ = std::move(object);
  return value;
}

ValueKind Value::kind() const noexcept {
  switch (storage_.index()) {
    case 0:
      return ValueKind::Undefined;
    case 1:
      return ValueKind::Null;
    case 2:
      return ValueKind::Boolean;
    case 3:
      return ValueKind::Int32;
    case 4:
      return ValueKind::UInt32;
    case 5:
      return ValueKind::Number;
    case 6:
      return ValueKind::String;
    default:
      return ValueKind::Object;
  }
}

bool Value::is_undefined() const noexcept { return storage_.index() == 0; }
bool Value::is_null() const noexcept { return storage_.index() == 1; }
bool Value::is_nullish() const noexcept { return storage_.index() <= 1; }
bool Value::is_boolean() const noexcept { return storage_.index() == 2; }
bool Value::is_int32() const noexcept { return storage_.index() == 3; }
bool Value::is_uint32() const noexcept { return storage_.index() == 4; }
bool Value::is_number_like() const noexcept { return storage_.index() >= 3 && storage_.index() <= 5; }
bool Value::is_string() const noexcept { return storage_.index() == 6; }
bool Value::is_object() const noexcept { return storage_.index() == 7; }
bool Value::is_primitive() const noexcept { return !is_object(); }

bool Value::as_boolean() const {
  if (!is_boolean()) {
    wrong_kind("a boolean");
  }
  return std::get<2>(storage_);
}

std::int32_t Value::as_int32() const {
  if (!is_int32()) {
    wrong_kind("an int");
  }
  return std::get<3>(storage_);
}

std::uint32_t Value::as_uint32() const {
  if (!is_uint32()) {
    wrong_kind("a uint");
  }
  return std::get<4>(storage_);
}

double Value::as_number() const {
  if (!is_number_like()) {
    wrong_kind("a number");
  }
  if (is_int32()) {
    return static_cast<double>(as_int32());
  }
  if (is_uint32()) {
    return static_cast<double>(as_uint32());
  }
  return std::get<5>(storage_);
}

const std::string& Value::as_string() const {
  if (!is_string()) {
    wrong_kind("a string");
  }
  return std::get<6>(storage_);
}

std::string& Value::as_string() {
  if (!is_string()) {
    wrong_kind("a string");
  }
  return std::get<6>(storage_);
}

const std::shared_ptr<Object>& Value::as_object() const {
  if (!is_object()) {
    wrong_kind("an object");
  }
  return std::get<7>(storage_);
}

const char* value_kind_name(ValueKind kind) noexcept {
  switch (kind) {
    case ValueKind::Undefined:
      return "undefined";
    case ValueKind::Null:
      return "null";
    case ValueKind::Boolean:
      return "boolean";
    case ValueKind::Int32:
      return "int";
    case ValueKind::UInt32:
      return "uint";
    case ValueKind::Number:
      return "number";
    case ValueKind::String:
      return "string";
    case ValueKind::Object:
      return "object";
  }
  return "unknown";
}

Value undefined_value() { return Value::undefined(); }
Value null_value() { return Value::null(); }
Value boolean_value(bool value) { return Value::boolean(value); }
Value int32_value(std::int32_t value) { return Value::int32(value); }
Value uint32_value(std::uint32_t value) { return Value::uint32(value); }
Value number_value(double value) { return Value::number(value); }
Value string_value(std::string value) { return Value::string(std::move(value)); }
Value object_value(std::shared_ptr<Object> value) { return Value::object(std::move(value)); }

}  // namespace fo4::avm2
