#include <fo4/avm2/interpreter.hpp>

#include "interpreter_internal.hpp"

#include <fo4/avm2/class.hpp>
#include <fo4/avm2/coercion.hpp>

#include <cmath>
#include <string>

namespace fo4::avm2 {

using namespace detail;

bool Interpreter::is_instance_of(const Value& value, const Value& type) const {
  if (!type.is_object() || type.as_object()->kind() != ObjectKind::Class) {
    return false;
  }
  const Class* target = static_cast<const ClassObject&>(*type.as_object()).represented_class();
  if (target == nullptr || value.is_nullish()) {
    return false;
  }
  const std::string& name = target->name;
  if (name == "Object") {
    return true;
  }
  if (value.is_string()) {
    return name == "String";
  }
  if (value.is_boolean()) {
    return name == "Boolean";
  }
  if (value.is_number_like()) {
    if (name == "Number") {
      return true;
    }
    const double number = to_number(value);
    const bool integral = std::floor(number) == number;
    if (name == "int") {
      return integral && number >= -2147483648.0 && number <= 2147483647.0;
    }
    return name == "uint" && integral && number >= 0.0 && number <= 4294967295.0;
  }
  if (!value.is_object()) {
    return false;
  }
  const ObjectKind kind = value.as_object()->kind();
  if ((kind == ObjectKind::Class && name == "Class") ||
      (kind == ObjectKind::Function && name == "Function")) {
    return true;
  }
  const Class* value_class = value.as_object()->instance_class();
  if (value_class == nullptr) {
    return false;
  }
  return value_class->is_subclass_of(target) || value_class->implements_interface(target);
}

InterpreterStatus Interpreter::coerce_value(const Multiname& multiname, Value& value,
                                            std::string& message) {
  const std::string name = property_text(multiname);
  if (name == "*" || name == "Object") {
    return InterpreterStatus::Ok;
  }
  if (name == "int") {
    value = Value::int32(to_int32(value));
    return InterpreterStatus::Ok;
  }
  if (name == "uint") {
    value = Value::uint32(to_uint32(value));
    return InterpreterStatus::Ok;
  }
  if (name == "Number" || name == "double") {
    value = Value::number(to_number(value));
    return InterpreterStatus::Ok;
  }
  if (name == "Boolean") {
    value = Value::boolean(to_boolean(value));
    return InterpreterStatus::Ok;
  }
  if (name == "String") {
    value = value.is_nullish() ? Value::null() : Value::string(to_string(value));
    return InterpreterStatus::Ok;
  }
  if (name.find("Vector.<") == 0 || name.find("__AS3__.vec::Vector") == 0) {
    return InterpreterStatus::Ok;
  }
  const Class* target = runtime_.find_class(multiname);
  if (target == nullptr) {
    message = "cannot resolve the type " + name;
    return InterpreterStatus::TypeError;
  }
  if (value.is_nullish()) {
    value = Value::null();
    return InterpreterStatus::Ok;
  }
  if (!value.is_object()) {
    message = std::string{"cannot coerce "} + value_kind_name(value.kind()) + " to " + name;
    return InterpreterStatus::TypeError;
  }
  const Class* value_class = value.as_object()->instance_class();
  if (value_class != nullptr &&
      (value_class->is_subclass_of(target) || value_class->implements_interface(target))) {
    return InterpreterStatus::Ok;
  }
  const ObjectKind kind = value.as_object()->kind();
  if ((kind == ObjectKind::Class && name == "Class") ||
      (kind == ObjectKind::Function && name == "Function")) {
    return InterpreterStatus::Ok;
  }
  message = "value is not of type " + name;
  return InterpreterStatus::TypeError;
}

}  // namespace fo4::avm2
