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

std::string error_name_from_class(const Value& self) {
  if (!self.is_object() || self.as_object()->kind() != ObjectKind::Class) {
    return "Error";
  }
  const Class* klass = static_cast<const ClassObject&>(*self.as_object()).represented_class();
  if (klass == nullptr) {
    return "Error";
  }
  const std::string& rendered = klass->name;
  const std::size_t separator = rendered.rfind("::");
  return separator == std::string::npos ? rendered : rendered.substr(separator + 2);
}

ExecutionResult error_construct(Interpreter& interpreter, const Value& self,
                                const std::vector<Value>& arguments) {
  (void)interpreter;
  const std::string error_name = error_name_from_class(self);
  std::string message;
  if (!arguments.empty() && !arguments[0].is_undefined()) {
    message = to_string(arguments[0]);
  }
  auto error = std::make_shared<ErrorObject>(error_name, message);
  Class* klass = nullptr;
  if (self.is_object() && self.as_object()->kind() == ObjectKind::Class) {
    klass = static_cast<const ClassObject&>(*self.as_object()).represented_class();
  }
  if (klass != nullptr) {
    error->set_instance_class(klass);
    if (klass->prototype_object != nullptr) {
      error->set_prototype(klass->prototype_object);
    }
  }
  return native_value(Value::object(std::move(error)));
}

ExecutionResult error_to_string(const Value& self) {
  if (self.is_object() && self.as_object()->kind() == ObjectKind::Error) {
    const auto& error = static_cast<const ErrorObject&>(*self.as_object());
    if (error.message().empty()) {
      return native_value(Value::string(error.error_name()));
    }
    return native_value(Value::string(error.error_name() + ": " + error.message()));
  }
  return native_value(Value::string(std::string{"Error"}));
}

}  // namespace

bool try_error_native(Interpreter& interpreter, native::Id id, const Value& self,
                      const std::vector<Value>& arguments, std::size_t depth,
                      ExecutionResult& out) {
  (void)interpreter;
  (void)depth;
  switch (id) {
    case native::Id::error_construct:
      out = error_construct(interpreter, self, arguments);
      return true;
    case native::Id::error_to_string:
      out = error_to_string(self);
      return true;
    default:
      return false;
  }
}

}  // namespace fo4::avm2
