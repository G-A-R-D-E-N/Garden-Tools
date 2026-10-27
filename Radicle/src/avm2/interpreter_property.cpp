#include <fo4/avm2/interpreter.hpp>

#include "interpreter_internal.hpp"

#include <fo4/avm2/coercion.hpp>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace fo4::avm2 {

using namespace detail;

ExecutionResult Interpreter::get_property(Frame& frame, Object& object,
                                          const Multiname& multiname, std::size_t depth) {
  (void)frame;
  Value out;
  bool handled = false;
  std::string message;
  const InterpreterStatus array_status =
      array_property(object, multiname, false, out, handled, message);
  if (array_status != InterpreterStatus::Ok) {
    return fail(array_status, std::move(message));
  }
  if (handled) {
    ExecutionResult result;
    result.value = std::move(out);
    return result;
  }
  const PropertyLookup lookup = object.lookup(multiname);
  if (!lookup.found()) {
    return ExecutionResult{};
  }
  if (lookup.kind == LookupKind::Dynamic || lookup.kind == LookupKind::Slot) {
    ExecutionResult result;
    result.value = lookup.value;
    return result;
  }
  if (lookup.kind == LookupKind::Method) {
    ExecutionResult result;
    if (lookup.trait != nullptr && lookup.trait->function != nullptr) {
      result.value = Value::object(lookup.trait->function);
    }
    return result;
  }
  if (lookup.kind == LookupKind::Accessor) {
    if (lookup.getter == nullptr) {
      return ExecutionResult{};
    }
    return invoke(*lookup.getter, borrowed_object(object), {}, {}, depth);
  }
  return ExecutionResult{};
}

ExecutionResult Interpreter::set_property(Frame& frame, Object& object,
                                          const Multiname& multiname, const Value& value,
                                          bool define, std::size_t depth) {
  (void)frame;
  bool handled = false;
  Value ignored = value;
  std::string message;
  const InterpreterStatus array_status =
      array_property(object, multiname, true, ignored, handled, message);
  if (array_status != InterpreterStatus::Ok) {
    return fail(array_status, std::move(message));
  }
  if (handled) {
    return ExecutionResult{};
  }
  if (define) {
    if (!multiname.has_fixed_name()) {
      return fail(InterpreterStatus::TypeError, "initproperty needs a fixed property name");
    }
    Property& property = object.define_own_property(primary_key(multiname));
    property.kind = PropertyKind::Value;
    property.value = value;
    return ExecutionResult{};
  }
  const PropertyLookup lookup = object.lookup(multiname);
  if (lookup.kind == LookupKind::Accessor && lookup.setter != nullptr) {
    return invoke(*lookup.setter, borrowed_object(object), {value}, {}, depth);
  }
  if (object.write_value(multiname, value)) {
    return ExecutionResult{};
  }
  return fail(InterpreterStatus::TypeError,
              "cannot write property " + property_text(multiname) + " on a " +
                  object_kind_name(object.kind()));
}

InterpreterStatus Interpreter::find_scope_binding(Frame& frame, const Multiname& multiname,
                                                  bool create_on_global, Object*& owner,
                                                  bool& found, std::string& message) {
  found = false;
  for (auto current = frame.scopes.rbegin(); current != frame.scopes.rend(); ++current) {
    if (*current != nullptr && (*current)->has_property(multiname)) {
      owner = *current;
      found = true;
      return InterpreterStatus::Ok;
    }
  }
  if (create_on_global && !frame.scopes.empty() && multiname.has_fixed_name()) {
    Object* global = frame.scopes.front();
    if (global != nullptr && global->is_dynamic()) {
      Property& property = global->define_own_property(primary_key(multiname));
      property.kind = PropertyKind::Value;
      owner = global;
      found = true;
      return InterpreterStatus::Ok;
    }
  }
  message = "no scope binding for " + property_text(multiname);
  return InterpreterStatus::Ok;
}

ExecutionResult Interpreter::call_property(Frame& frame, Object& receiver,
                                           const Multiname& multiname,
                                           const std::vector<Value>& arguments,
                                           std::size_t depth) {
  (void)frame;
  const PropertyLookup lookup = receiver.lookup(multiname);
  if (!lookup.found()) {
    return fail(InterpreterStatus::CannotCall,
                "no property " + property_text(multiname) + " on a " +
                    object_kind_name(receiver.kind()));
  }
  if (lookup.kind == LookupKind::Method) {
    if (lookup.trait == nullptr || lookup.trait->function == nullptr) {
      return fail(InterpreterStatus::CannotCall,
                  "method trait " + property_text(multiname) + " has no function");
    }
    return invoke(*lookup.trait->function, borrowed_object(receiver), arguments, {}, depth);
  }
  if (lookup.kind == LookupKind::Accessor) {
    if (lookup.getter == nullptr) {
      return fail(InterpreterStatus::CannotCall,
                  "property " + property_text(multiname) + " has no getter to call");
    }
    ExecutionResult getter = invoke(*lookup.getter, borrowed_object(receiver), {}, {}, depth);
    if (!getter.ok()) {
      return getter;
    }
    if (!getter.value.is_object() ||
        getter.value.as_object()->kind() != ObjectKind::Function) {
      return fail(InterpreterStatus::CannotCall,
                  "property " + property_text(multiname) + " is not callable");
    }
    return invoke(static_cast<const FunctionObject&>(*getter.value.as_object()),
                  borrowed_object(receiver), arguments, {}, depth);
  }
  if (!lookup.value.is_object() || lookup.value.as_object()->kind() != ObjectKind::Function) {
    return fail(InterpreterStatus::CannotCall,
                "property " + property_text(multiname) + " is not callable");
  }
  return invoke(static_cast<const FunctionObject&>(*lookup.value.as_object()),
                borrowed_object(receiver), arguments, {}, depth);
}

ExecutionResult Interpreter::construct(Frame& frame, const ClassObject& class_object,
                                       const std::vector<Value>& arguments, std::size_t depth) {
  (void)frame;
  Class* klass = class_object.represented_class();
  if (klass == nullptr) {
    return fail(InterpreterStatus::CannotConstruct, "class object has no class");
  }
  auto instance = std::make_shared<Object>();
  instance->set_instance_class(klass);
  klass->apply_slot_defaults(*instance);
  if (klass->iinit != kNoMethod && klass->file != nullptr) {
    auto initializer = std::make_shared<FunctionObject>(klass->file->method_name(klass->iinit),
                                                        klass->file, klass->iinit);
    initializer->set_owner_class(klass);
    ExecutionResult initialized =
        invoke(*initializer, Value::object(instance), arguments, {}, depth);
    if (!initialized.ok()) {
      return initialized;
    }
  }
  ExecutionResult result;
  result.value = Value::object(std::move(instance));
  return result;
}

bool Interpreter::is_instance_of(const Value& value, const Value& type) const {
  if (!value.is_object() || !type.is_object() || type.as_object()->kind() != ObjectKind::Class) {
    return false;
  }
  const Class* target = static_cast<const ClassObject&>(*type.as_object()).represented_class();
  if (target == nullptr) {
    return false;
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
    value = Value::string(to_string(value));
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
    message = "cannot coerce " + value_kind_name(value.kind()) + " to " + name;
    return InterpreterStatus::TypeError;
  }
  const Class* value_class = value.as_object()->instance_class();
  if (value_class != nullptr &&
      (value_class->is_subclass_of(target) || value_class->implements_interface(target))) {
    return InterpreterStatus::Ok;
  }
  message = "value is not of type " + name;
  return InterpreterStatus::TypeError;
}

InterpreterStatus Interpreter::array_property(Object& object, const Multiname& multiname,
                                              bool write, Value& value, bool& handled,
                                              std::string& message) {
  handled = false;
  if (object.kind() != ObjectKind::Array) {
    return InterpreterStatus::Ok;
  }
  auto& array = static_cast<ArrayObject&>(object);
  if (!multiname.has_fixed_name()) {
    return InterpreterStatus::Ok;
  }
  if (multiname.name == "length") {
    if (write) {
      const double length = to_number(value);
      if (std::isnan(length) || length < 0.0) {
        message = "an array length must be a non-negative number";
        return InterpreterStatus::TypeError;
      }
      array.elements().resize(static_cast<std::size_t>(length));
      handled = true;
      return InterpreterStatus::Ok;
    }
    value = Value::number(static_cast<double>(array.elements().size()));
    handled = true;
    return InterpreterStatus::Ok;
  }
  std::uint32_t index = 0;
  if (!is_array_index(multiname.name, index)) {
    return InterpreterStatus::Ok;
  }
  handled = true;
  if (write) {
    if (array.elements().size() <= index) {
      array.elements().resize(static_cast<std::size_t>(index) + 1);
    }
    array.elements()[index] = value;
    return InterpreterStatus::Ok;
  }
  value = index < array.elements().size() ? array.elements()[index] : Value::undefined();
  return InterpreterStatus::Ok;
}

}  // namespace fo4::avm2
