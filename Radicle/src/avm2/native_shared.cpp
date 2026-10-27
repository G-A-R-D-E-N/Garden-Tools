#include <fo4/avm2/amf.hpp>
#include <fo4/avm2/class.hpp>
#include <fo4/avm2/coercion.hpp>
#include <fo4/avm2/interpreter.hpp>
#include <fo4/avm2/net.hpp>
#include <fo4/avm2/object.hpp>
#include <fo4/avm2/runtime.hpp>
#include <fo4/avm2/value.hpp>

#include "native_internal.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace fo4::avm2 {

SharedObjectObject::SharedObjectObject(Class* shared_class) : Object(ObjectKind::Object) {
  if (shared_class != nullptr) {
    set_instance_class(shared_class);
    if (shared_class->prototype_object != nullptr) {
      set_prototype(shared_class->prototype_object);
    }
  }
  set_dynamic(true);
  data = Value::object(std::make_shared<Object>(ObjectKind::Object));
}

namespace {

Class* class_from_self(const Value& self) {
  if (!self.is_object() || self.as_object()->kind() != ObjectKind::Class) {
    return nullptr;
  }
  return static_cast<const ClassObject&>(*self.as_object()).represented_class();
}

SharedObjectObject* shared_self(const Value& self) {
  if (!self.is_object()) {
    return nullptr;
  }
  const Class* klass = self.as_object()->instance_class();
  if (klass == nullptr || klass->name != "flash.net::SharedObject") {
    return nullptr;
  }
  return static_cast<SharedObjectObject*>(self.as_object().get());
}

Value empty_object(Interpreter& interpreter) {
  const Class* object_class = interpreter.runtime().find_class("Object");
  if (object_class == nullptr) {
    return Value::object(std::make_shared<Object>(ObjectKind::Object));
  }
  return Value::object(interpreter.make_instance(*object_class));
}

ExecutionResult shared_get_local(Interpreter& interpreter, const Value& self,
                                 const std::vector<Value>& arguments) {
  Class* shared_class = class_from_self(self);
  if (shared_class == nullptr) {
    return native_value(Value::null());
  }
  const std::string name = to_string(argument_at(arguments, 0));
  if (!shared_object_name_ok(name)) {
    return native_value(Value::null());
  }
  auto shared = std::make_shared<SharedObjectObject>(shared_class);
  shared->name = name;
  const std::optional<std::vector<std::uint8_t>> stored =
      interpreter.runtime().host().read_storage(name);
  if (stored.has_value()) {
    Value decoded;
    if (decode_amf3(*stored, decoded) && decoded.is_object()) {
      shared->data = std::move(decoded);
    }
  }
  return native_value(Value::object(std::move(shared)));
}

ExecutionResult shared_data_get(const Value& self) {
  SharedObjectObject* shared = shared_self(self);
  if (shared == nullptr) {
    return native_value(Value::undefined());
  }
  return native_value(shared->data);
}

ExecutionResult shared_data_set(const Value& self, const std::vector<Value>& arguments) {
  SharedObjectObject* shared = shared_self(self);
  if (shared != nullptr) {
    shared->data = argument_at(arguments, 0);
    shared->dirty = true;
  }
  return native_value(Value::undefined());
}

ExecutionResult shared_flush(Interpreter& interpreter, const Value& self) {
  SharedObjectObject* shared = shared_self(self);
  if (shared == nullptr) {
    return native_value(Value::boolean(false));
  }
  const std::vector<std::uint8_t> bytes = encode_amf3(shared->data);
  const bool written = interpreter.runtime().host().write_storage(shared->name, bytes);
  if (written) {
    shared->dirty = false;
  }
  return native_value(Value::boolean(written));
}

ExecutionResult shared_clear(Interpreter& interpreter, const Value& self) {
  SharedObjectObject* shared = shared_self(self);
  if (shared != nullptr) {
    shared->data = empty_object(interpreter);
    shared->dirty = false;
  }
  return native_value(Value::undefined());
}

ExecutionResult shared_close(const Value& self) {
  (void)self;
  return native_value(Value::undefined());
}

}  // namespace

bool try_shared_native(Interpreter& interpreter, native::Id id, const Value& self,
                       const std::vector<Value>& arguments, std::size_t depth,
                       ExecutionResult& out) {
  (void)depth;
  switch (id) {
    case native::Id::shared_get_local:
      out = shared_get_local(interpreter, self, arguments);
      return true;
    case native::Id::shared_data_get:
      out = shared_data_get(self);
      return true;
    case native::Id::shared_data_set:
      out = shared_data_set(self, arguments);
      return true;
    case native::Id::shared_flush:
      out = shared_flush(interpreter, self);
      return true;
    case native::Id::shared_clear:
      out = shared_clear(interpreter, self);
      return true;
    case native::Id::shared_close:
      out = shared_close(self);
      return true;
    default:
      return false;
  }
}

}  // namespace fo4::avm2
