#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <variant>

namespace fo4::avm2 {

class Object;

enum class ValueKind : std::uint8_t {
  Undefined = 0,
  Null,
  Boolean,
  Int32,
  UInt32,
  Number,
  String,
  Object,
};

struct UndefinedValue final {
  bool operator==(const UndefinedValue&) const = default;
};
struct NullValue final {
  bool operator==(const NullValue&) const = default;
};

class Value {
 public:
  using Storage = std::variant<UndefinedValue, NullValue, bool, std::int32_t, std::uint32_t,
                               double, std::string, std::shared_ptr<Object>>;

  Value();

  static Value undefined();
  static Value null();
  static Value boolean(bool value);
  static Value int32(std::int32_t value);
  static Value uint32(std::uint32_t value);
  static Value number(double value);
  static Value string(std::string value);
  static Value object(std::shared_ptr<Object> value);

  ValueKind kind() const noexcept;
  bool is_undefined() const noexcept;
  bool is_null() const noexcept;

  bool is_nullish() const noexcept;
  bool is_boolean() const noexcept;
  bool is_int32() const noexcept;
  bool is_uint32() const noexcept;
  bool is_number_like() const noexcept;
  bool is_string() const noexcept;
  bool is_object() const noexcept;
  bool is_primitive() const noexcept;

  bool as_boolean() const;
  std::int32_t as_int32() const;
  std::uint32_t as_uint32() const;
  double as_number() const;
  const std::string& as_string() const;
  std::string& as_string();
  const std::shared_ptr<Object>& as_object() const;

  const Storage& storage() const noexcept { return storage_; }

  bool operator==(const Value& other) const = default;

 private:
  Storage storage_;
};

const char* value_kind_name(ValueKind kind) noexcept;

Value undefined_value();
Value null_value();
Value boolean_value(bool value);
Value int32_value(std::int32_t value);
Value uint32_value(std::uint32_t value);
Value number_value(double value);
Value string_value(std::string value);
Value object_value(std::shared_ptr<Object> value);

}  // namespace fo4::avm2
