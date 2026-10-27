#include <fo4/avm2/class.hpp>
#include <fo4/avm2/coercion.hpp>
#include <fo4/avm2/interpreter.hpp>
#include <fo4/avm2/object.hpp>
#include <fo4/avm2/runtime.hpp>

#include "interpreter_internal.hpp"
#include "native_internal.hpp"

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace fo4::avm2 {

namespace {

const Class* find_builtin(const Runtime& runtime, std::string_view name) {
  return runtime.find_class(name);
}

}  // namespace

Multiname builtin_name(std::string_view name) {
  Multiname multiname;
  multiname.kind = MultinameKind::QName;
  multiname.name = std::string{name};
  multiname.namespaces.push_back(Namespace{});
  return multiname;
}

Value native_self(const Value& self) {
  if (self.is_object() && self.as_object()->has_primitive()) {
    return self.as_object()->primitive();
  }
  return self;
}

Value argument_at(const std::vector<Value>& arguments, std::size_t index) {
  return index < arguments.size() ? arguments[index] : Value::undefined();
}

ExecutionResult native_value(Value value) {
  ExecutionResult result;
  result.value = std::move(value);
  return result;
}

ExecutionResult Interpreter::call_function(const FunctionObject& function, Value this_value,
                                           std::vector<Value> arguments, std::size_t depth) {
  return invoke(function, std::move(this_value), std::move(arguments), {}, depth);
}

ExecutionResult Interpreter::prepare_class(Class& klass, std::size_t depth) {
  return ensure_class_initialized(klass, depth);
}

ExecutionResult Interpreter::read_property(Object& object, const Multiname& multiname,
                                           std::size_t depth) {
  Frame frame;
  if (Object* global = runtime_.global_object()) {
    frame.scopes.push_back(global);
  }
  return get_property(frame, object, multiname, depth);
}

ExecutionResult Interpreter::write_property(Object& object, const Multiname& multiname,
                                            const Value& value, std::size_t depth) {
  Frame frame;
  if (Object* global = runtime_.global_object()) {
    frame.scopes.push_back(global);
  }
  return set_property(frame, object, multiname, value, false, depth);
}

std::shared_ptr<Object> Interpreter::make_instance(Class& klass) {
  auto instance = std::make_shared<Object>();
  instance->set_instance_class(&klass);
  klass.apply_slot_defaults(*instance);
  if (klass.prototype_object != nullptr) {
    instance->set_prototype(klass.prototype_object);
  }
  if (const Class* object_class = find_builtin(runtime_, "Object")) {
    if (&klass != object_class && instance->prototype() == nullptr &&
        object_class->prototype_object != nullptr) {
      instance->set_prototype(object_class->prototype_object);
    }
  }
  return instance;
}

std::shared_ptr<ArrayObject> Interpreter::make_array(std::vector<Value> elements) {
  auto array = std::make_shared<ArrayObject>();
  array->elements() = std::move(elements);
  if (const Class* array_class = find_builtin(runtime_, "Array")) {
    array->set_instance_class(const_cast<Class*>(array_class));
  }
  return array;
}

ExecutionResult Interpreter::coerce_to_class(const Class& target, Value& value,
                                              std::string& message) {
  const InterpreterStatus status = coerce_value(target.name_multiname, value, message);
  if (status != InterpreterStatus::Ok) {
    ExecutionResult failure;
    failure.status = status;
    failure.message = std::move(message);
    return failure;
  }
  return ExecutionResult{};
}

std::shared_ptr<ArrayObject> make_array_value(Interpreter& interpreter,
                                              std::vector<Value> elements) {
  return interpreter.make_array(std::move(elements));
}

Value Interpreter::box(const Value& value) {
  const char* class_name = nullptr;
  switch (value.kind()) {
    case ValueKind::String:
      class_name = "String";
      break;
    case ValueKind::Boolean:
      class_name = "Boolean";
      break;
    case ValueKind::Int32:
      class_name = "int";
      break;
    case ValueKind::UInt32:
      class_name = "uint";
      break;
    case ValueKind::Number:
      class_name = "Number";
      break;
    default:
      return value;
  }
  const Class* klass = find_builtin(runtime_, class_name);
  if (klass == nullptr) {
    return value;
  }
  auto instance = std::make_shared<Object>();
  instance->set_instance_class(const_cast<Class*>(klass));
  instance->set_primitive(value);
  if (klass->prototype_object != nullptr) {
    instance->set_prototype(klass->prototype_object);
  }
  return Value::object(std::move(instance));
}

Value Interpreter::unbox(const Value& value) {
  if (value.is_object() && value.as_object()->has_primitive()) {
    return value.as_object()->primitive();
  }
  return value;
}

void Interpreter::prepare_plain_object(Object& object) {
  if (object.instance_class() != nullptr || object.class_scope() != nullptr) {
    return;
  }
  const Class* object_class = find_builtin(runtime_, "Object");
  if (object_class == nullptr) {
    return;
  }
  object.set_instance_class(const_cast<Class*>(object_class));
  if (object_class->prototype_object != nullptr) {
    object.set_prototype(object_class->prototype_object);
  }
}

double Interpreter::next_random() {
  random_state_ ^= random_state_ >> 12;
  random_state_ ^= random_state_ << 25;
  random_state_ ^= random_state_ >> 27;
  const std::uint64_t value = random_state_ * 0x2545F4914F6CDD1DULL;
  return static_cast<double>(value >> 11) / 9007199254740992.0;
}

Value Interpreter::global_this() {
  Object* global = runtime_.global_object();
  return global == nullptr ? Value::undefined() : detail::borrowed_object(*global);
}

ExecutionResult Interpreter::call_native_id(std::uint32_t native_id, const Value& self,
                                            const std::vector<Value>& arguments,
                                            std::size_t depth) {
  const auto id = static_cast<native::Id>(native_id);
  ExecutionResult result;
  if (try_object_native(*this, id, self, arguments, depth, result)) {
    return result;
  }
  if (try_function_native(*this, id, self, arguments, depth, result)) {
    return result;
  }
  if (try_primitive_native(*this, id, self, arguments, depth, result)) {
    return result;
  }
  if (try_math_native(*this, id, self, arguments, depth, result)) {
    return result;
  }
  if (try_top_native(*this, id, self, arguments, depth, result)) {
    return result;
  }
  if (try_string_native(*this, id, self, arguments, depth, result)) {
    return result;
  }
  if (try_array_native(*this, id, self, arguments, depth, result)) {
    return result;
  }
  if (try_error_native(*this, id, self, arguments, depth, result)) {
    return result;
  }
  if (try_display_native(*this, id, self, arguments, depth, result)) {
    return result;
  }
  if (try_event_native(*this, id, self, arguments, depth, result)) {
    return result;
  }
  if (try_text_native(*this, id, self, arguments, depth, result)) {
    return result;
  }
  if (try_net_native(*this, id, self, arguments, depth, result)) {
    return result;
  }
  if (try_shared_native(*this, id, self, arguments, depth, result)) {
    return result;
  }
  if (try_system_native(*this, id, self, arguments, depth, result)) {
    return result;
  }
  if (try_compat_native(*this, id, self, arguments, depth, result)) {
    return result;
  }
  if (try_bgs_native(*this, id, arguments, result)) {
    return result;
  }
  ExecutionResult failure;
  failure.status = InterpreterStatus::NotImplemented;
  failure.message = "native " + std::to_string(native_id) + " is not bound";
  return failure;
}

}  // namespace fo4::avm2
