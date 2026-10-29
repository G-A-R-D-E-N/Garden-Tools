#include <fo4/avm2/class.hpp>
#include <fo4/avm2/coercion.hpp>
#include <fo4/avm2/interpreter.hpp>
#include <fo4/avm2/object.hpp>
#include <fo4/avm2/runtime.hpp>
#include <fo4/avm2/value.hpp>

#include "native_internal.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace fo4::avm2 {

namespace {

ExecutionResult object_construct(Interpreter& interpreter, const Value& self,
                                 const std::vector<Value>& arguments, std::size_t depth) {
  (void)self;
  (void)depth;
  const Value argument = argument_at(arguments, 0);
  if (argument.is_object()) {
    return native_value(argument);
  }
  if (!argument.is_primitive() || argument.is_nullish()) {
    const Class* object_class = interpreter.runtime().find_class("Object");
    if (object_class == nullptr) {
      return native_value(Value::object(std::make_shared<Object>()));
    }
    return native_value(
        Value::object(interpreter.make_instance(*const_cast<Class*>(object_class))));
  }
  return native_value(interpreter.box(argument));
}

ExecutionResult object_to_string(Interpreter& interpreter, const Value& self,
                                 const std::vector<Value>& arguments, std::size_t depth) {
  (void)interpreter;
  (void)arguments;
  (void)depth;
  const Value primitive = native_self(self);
  if (primitive.is_string()) {
    return native_value(Value::string(primitive.as_string()));
  }
  return native_value(Value::string("[object Object]"));
}

ExecutionResult object_value_of(const Value& self) { return native_value(native_self(self)); }

bool has_property(const Value& target, const Value& name) {
  if (!target.is_object()) {
    return false;
  }
  const Multiname multiname = builtin_name(to_string(name));
  return target.as_object()->has_property(multiname);
}

ExecutionResult object_has_own_property(Interpreter& interpreter, const Value& self,
                                        const std::vector<Value>& arguments, std::size_t depth) {
  (void)interpreter;
  (void)depth;
  return native_value(Value::boolean(has_property(self, argument_at(arguments, 0))));
}

ExecutionResult object_is_prototype_of(const Interpreter& interpreter, const Value& self,
                                       const std::vector<Value>& arguments) {
  (void)interpreter;
  const Value target = argument_at(arguments, 0);
  if (!self.is_object() || !target.is_object()) {
    return native_value(Value::boolean(false));
  }
  const Object* wanted = self.as_object().get();
  for (const Object* current = target.as_object()->prototype().get(); current != nullptr;
       current = current->prototype().get()) {
    if (current == wanted) {
      return native_value(Value::boolean(true));
    }
  }
  return native_value(Value::boolean(false));
}

ExecutionResult object_property_is_enumerable(Interpreter& interpreter, const Value& self,
                                              const std::vector<Value>& arguments,
                                              std::size_t depth) {
  (void)interpreter;
  (void)depth;
  return native_value(Value::boolean(has_property(self, argument_at(arguments, 0))));
}

ExecutionResult function_call(Interpreter& interpreter, const Value& self,
                              const std::vector<Value>& arguments, std::size_t depth) {
  if (!self.is_object() || self.as_object()->kind() != ObjectKind::Function) {
    ExecutionResult failure;
    failure.status = InterpreterStatus::CannotCall;
    failure.message = "Function.call without a function receiver";
    return failure;
  }
  const auto& function = static_cast<const FunctionObject&>(*self.as_object());
  std::vector<Value> forwarded;
  for (std::size_t index = 1; index < arguments.size(); ++index) {
    forwarded.push_back(arguments[index]);
  }
  return interpreter.call_function(function, argument_at(arguments, 0), std::move(forwarded),
                                   depth);
}

ExecutionResult function_apply(Interpreter& interpreter, const Value& self,
                               const std::vector<Value>& arguments, std::size_t depth) {
  if (!self.is_object() || self.as_object()->kind() != ObjectKind::Function) {
    ExecutionResult failure;
    failure.status = InterpreterStatus::CannotCall;
    failure.message = "Function.apply without a function receiver";
    return failure;
  }
  const auto& function = static_cast<const FunctionObject&>(*self.as_object());
  std::vector<Value> forwarded;
  const Value array = argument_at(arguments, 1);
  if (array.is_object() && array.as_object()->kind() == ObjectKind::Array) {
    forwarded = static_cast<const ArrayObject&>(*array.as_object()).elements();
  }
  return interpreter.call_function(function, argument_at(arguments, 0), std::move(forwarded),
                                   depth);
}

ExecutionResult function_to_string(const Value& self) {
  std::string name{"anonymous"};
  if (self.is_object() && self.as_object()->kind() == ObjectKind::Function) {
    name = static_cast<const FunctionObject&>(*self.as_object()).name();
  }
  return native_value(Value::string("function " + name + "() {}"));
}

}  // namespace

bool try_object_native(Interpreter& interpreter, native::Id id, const Value& self,
                       const std::vector<Value>& arguments, std::size_t depth,
                       ExecutionResult& out) {
  switch (id) {
    case native::Id::object_construct:
      out = object_construct(interpreter, self, arguments, depth);
      return true;
    case native::Id::object_to_string:
      out = object_to_string(interpreter, self, arguments, depth);
      return true;
    case native::Id::object_value_of:
      out = object_value_of(self);
      return true;
    case native::Id::object_has_own_property:
      out = object_has_own_property(interpreter, self, arguments, depth);
      return true;
    case native::Id::object_is_prototype_of:
      out = object_is_prototype_of(interpreter, self, arguments);
      return true;
    case native::Id::object_property_is_enumerable:
      out = object_property_is_enumerable(interpreter, self, arguments, depth);
      return true;
    default:
      return false;
  }
}

bool try_function_native(Interpreter& interpreter, native::Id id, const Value& self,
                         const std::vector<Value>& arguments, std::size_t depth,
                         ExecutionResult& out) {
  switch (id) {
    case native::Id::function_call:
      out = function_call(interpreter, self, arguments, depth);
      return true;
    case native::Id::function_apply:
      out = function_apply(interpreter, self, arguments, depth);
      return true;
    case native::Id::function_to_string:
      out = function_to_string(self);
      return true;
    default:
      return false;
  }
}

}  // namespace fo4::avm2
