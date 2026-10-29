#include <fo4/avm2/bytearray.hpp>
#include <fo4/avm2/class.hpp>
#include <fo4/avm2/coercion.hpp>
#include <fo4/avm2/interpreter.hpp>
#include <fo4/avm2/object.hpp>
#include <fo4/avm2/runtime.hpp>
#include <fo4/avm2/value.hpp>

#include "native_internal.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace fo4::avm2 {

ByteArrayObject::ByteArrayObject(Class* byte_array_class) : Object(ObjectKind::ByteArray) {
  if (byte_array_class != nullptr) {
    set_instance_class(byte_array_class);
    if (byte_array_class->prototype_object != nullptr) {
      set_prototype(byte_array_class->prototype_object);
    }
  }
}

namespace {

void add_constant(Class& klass, const char* name, Value value, std::uint32_t& next_slot) {
  Trait trait;
  trait.multiname = builtin_name(name);
  trait.kind = TraitKind::Const;
  trait.slot_id = next_slot;
  next_slot += 1;
  trait.has_default = true;
  trait.default_value = std::move(value);
  klass.class_traits.push_back(std::move(trait));
}

void add_instance_trait(Class& klass, const char* name, TraitKind kind, native::Id id) {
  auto function = std::make_shared<FunctionObject>(name, nullptr, kNoMethod);
  function->set_native_id(static_cast<std::uint32_t>(id));
  function->set_owner_class(&klass);
  Trait trait;
  trait.multiname = builtin_name(name);
  trait.kind = kind;
  trait.function = std::move(function);
  klass.instance_traits.push_back(std::move(trait));
}

ByteArrayObject* byte_array_self(const Value& self) {
  if (!self.is_object() || self.as_object()->kind() != ObjectKind::ByteArray) {
    return nullptr;
  }
  return static_cast<ByteArrayObject*>(self.as_object().get());
}

bool to_size(const Value& value, std::size_t& out) {
  const double number = to_number(value);
  if (!std::isfinite(number) || number < 0.0 ||
      number > static_cast<double>(kMaxByteArraySize)) {
    return false;
  }
  out = static_cast<std::size_t>(number);
  return true;
}

ExecutionResult type_error(const char* message) {
  ExecutionResult failure;
  failure.status = InterpreterStatus::TypeError;
  failure.message = message;
  return failure;
}

std::size_t bytes_available(const ByteArrayObject& bytes) {
  return bytes.position() < bytes.length() ? bytes.length() - bytes.position() : std::size_t{0};
}

ExecutionResult bytearray_construct(const Value& self) {
  Class* klass = nullptr;
  if (self.is_object() && self.as_object()->kind() == ObjectKind::Class) {
    klass = static_cast<const ClassObject&>(*self.as_object()).represented_class();
  }
  if (klass == nullptr) {
    return native_value(Value::undefined());
  }
  return native_value(Value::object(std::make_shared<ByteArrayObject>(klass)));
}

ExecutionResult length_set(ByteArrayObject& bytes, const Value& value) {
  std::size_t count = 0;
  if (!to_size(value, count)) {
    return type_error("a ByteArray length must be a non-negative number");
  }
  bytes.resize(count);
  return native_value(Value::undefined());
}

ExecutionResult position_set(ByteArrayObject& bytes, const Value& value) {
  std::size_t offset = 0;
  if (!to_size(value, offset)) {
    return type_error("a ByteArray position must be a non-negative number");
  }
  bytes.set_position(offset);
  return native_value(Value::undefined());
}

ExecutionResult endian_set(ByteArrayObject& bytes, const Value& value) {
  const std::string name = to_string(value);
  if (name != "bigEndian" && name != "littleEndian") {
    return type_error("a ByteArray endian must be bigEndian or littleEndian");
  }
  bytes.set_little_endian(name == "littleEndian");
  return native_value(Value::undefined());
}

ExecutionResult write_byte(ByteArrayObject& bytes, const Value& value) {
  bytes.set_byte_at(bytes.position(), static_cast<std::uint8_t>(to_uint32(value)));
  bytes.set_position(bytes.position() + 1);
  return native_value(Value::undefined());
}

ExecutionResult read_byte(ByteArrayObject& bytes) {
  const std::uint8_t value = bytes.byte_at(bytes.position());
  bytes.set_position(bytes.position() + 1);
  return native_value(Value::int32(static_cast<std::int8_t>(value)));
}

ExecutionResult read_unsigned_byte(ByteArrayObject& bytes) {
  const std::uint8_t value = bytes.byte_at(bytes.position());
  bytes.set_position(bytes.position() + 1);
  return native_value(Value::uint32(value));
}

ExecutionResult write_utf_bytes(ByteArrayObject& bytes, const Value& value) {
  for (const char character : to_string(value)) {
    bytes.set_byte_at(bytes.position(), static_cast<std::uint8_t>(character));
    bytes.set_position(bytes.position() + 1);
  }
  return native_value(Value::undefined());
}

ExecutionResult read_utf_bytes(ByteArrayObject& bytes, const Value& value) {
  std::size_t count = 0;
  if (!to_size(value, count)) {
    return type_error("a ByteArray read length must be a non-negative number");
  }
  count = std::min(count, bytes_available(bytes));
  std::string text;
  for (std::size_t index = 0; index < count; ++index) {
    text.push_back(static_cast<char>(bytes.byte_at(bytes.position() + index)));
  }
  bytes.set_position(bytes.position() + count);
  return native_value(Value::string(std::move(text)));
}

ExecutionResult clear_bytes(ByteArrayObject& bytes) {
  bytes.resize(0);
  bytes.set_position(0);
  return native_value(Value::undefined());
}

ExecutionResult bytearray_member(ByteArrayObject& bytes, native::Id id, const Value& first) {
  switch (id) {
    case native::Id::bytearray_length_get:
      return native_value(Value::number(static_cast<double>(bytes.length())));
    case native::Id::bytearray_length_set:
      return length_set(bytes, first);
    case native::Id::bytearray_position_get:
      return native_value(Value::number(static_cast<double>(bytes.position())));
    case native::Id::bytearray_position_set:
      return position_set(bytes, first);
    case native::Id::bytearray_endian_get:
      return native_value(Value::string(bytes.little_endian() ? "littleEndian" : "bigEndian"));
    case native::Id::bytearray_endian_set:
      return endian_set(bytes, first);
    case native::Id::bytearray_bytes_available_get:
      return native_value(Value::number(static_cast<double>(bytes_available(bytes))));
    case native::Id::bytearray_write_byte:
      return write_byte(bytes, first);
    case native::Id::bytearray_read_byte:
      return read_byte(bytes);
    case native::Id::bytearray_read_unsigned_byte:
      return read_unsigned_byte(bytes);
    case native::Id::bytearray_write_utf_bytes:
      return write_utf_bytes(bytes, first);
    case native::Id::bytearray_read_utf_bytes:
      return read_utf_bytes(bytes, first);
    case native::Id::bytearray_clear:
      return clear_bytes(bytes);
    default:
      return native_value(Value::undefined());
  }
}

}  // namespace

RuntimeStatus Runtime::install_bytearray_builtins() {
  Class* object_class = const_cast<Class*>(find_class("Object"));
  Class* byte_array = add_builtin_class("ByteArray", "flash.utils", object_class);
  Class* endian = add_builtin_class("Endian", "flash.utils", object_class);
  if (object_class == nullptr || byte_array == nullptr || endian == nullptr) {
    return RuntimeStatus::InvalidTrait;
  }
  byte_array->native_constructor = static_cast<std::uint32_t>(native::Id::bytearray_construct);
  add_instance_trait(*byte_array, "length", TraitKind::Getter, native::Id::bytearray_length_get);
  add_instance_trait(*byte_array, "length", TraitKind::Setter, native::Id::bytearray_length_set);
  add_instance_trait(*byte_array, "position", TraitKind::Getter,
                     native::Id::bytearray_position_get);
  add_instance_trait(*byte_array, "position", TraitKind::Setter,
                     native::Id::bytearray_position_set);
  add_instance_trait(*byte_array, "endian", TraitKind::Getter, native::Id::bytearray_endian_get);
  add_instance_trait(*byte_array, "endian", TraitKind::Setter, native::Id::bytearray_endian_set);
  add_instance_trait(*byte_array, "bytesAvailable", TraitKind::Getter,
                     native::Id::bytearray_bytes_available_get);
  add_instance_trait(*byte_array, "writeByte", TraitKind::Method,
                     native::Id::bytearray_write_byte);
  add_instance_trait(*byte_array, "readByte", TraitKind::Method, native::Id::bytearray_read_byte);
  add_instance_trait(*byte_array, "readUnsignedByte", TraitKind::Method,
                     native::Id::bytearray_read_unsigned_byte);
  add_instance_trait(*byte_array, "writeUTFBytes", TraitKind::Method,
                     native::Id::bytearray_write_utf_bytes);
  add_instance_trait(*byte_array, "readUTFBytes", TraitKind::Method,
                     native::Id::bytearray_read_utf_bytes);
  add_instance_trait(*byte_array, "clear", TraitKind::Method, native::Id::bytearray_clear);

  std::uint32_t endian_slot = 0;
  add_constant(*endian, "BIG_ENDIAN", Value::string("bigEndian"), endian_slot);
  add_constant(*endian, "LITTLE_ENDIAN", Value::string("littleEndian"), endian_slot);
  return RuntimeStatus::Ok;
}

bool try_bytearray_native(Interpreter& interpreter, native::Id id, const Value& self,
                          const std::vector<Value>& arguments, std::size_t depth,
                          ExecutionResult& out) {
  (void)interpreter;
  (void)depth;
  if (id == native::Id::bytearray_construct) {
    out = bytearray_construct(self);
    return true;
  }
  if (id < native::Id::bytearray_length_get || id > native::Id::bytearray_clear) {
    return false;
  }
  ByteArrayObject* bytes = byte_array_self(self);
  out = bytes == nullptr ? native_value(Value::undefined())
                         : bytearray_member(*bytes, id, argument_at(arguments, 0));
  return true;
}

}  // namespace fo4::avm2
