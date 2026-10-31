#include <fo4/avm2/interpreter.hpp>

#include "interpreter_internal.hpp"

#include <fo4/avm2/bytearray.hpp>
#include <fo4/avm2/class.hpp>
#include <fo4/avm2/coercion.hpp>
#include <fo4/avm2/display.hpp>
#include <fo4/avm2/events.hpp>
#include <fo4/avm2/text.hpp>

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

InterpreterStatus Interpreter::pop_runtime_operands(Frame& frame, std::size_t above,
                                                    const Multiname*& multiname,
                                                    std::string& message) {
  const std::size_t name_parts = multiname->has_fixed_name() ? 0u : 1u;
  const std::size_t namespace_parts = multiname->has_fixed_namespace() ? 0u : 1u;
  const std::size_t parts = name_parts + namespace_parts;
  if (frame.stack.size() < above + parts) {
    message = "runtime multiname operands are missing from the operand stack";
    return InterpreterStatus::StackUnderflow;
  }
  const std::size_t base = frame.stack.size() - above - parts;
  Multiname& resolved = frame.runtime_name;
  resolved = *multiname;
  if (name_parts == 1) {
    resolved.name = to_string(frame.stack[frame.stack.size() - above - 1]);
  }
  if (namespace_parts == 1) {
    resolved.kind = MultinameKind::QName;
    resolved.namespaces = {Namespace{NamespaceKind::Package, to_string(frame.stack[base])}};
  } else {
    resolved.kind = MultinameKind::QNameSet;
    const auto is_public = [](const Namespace& space) {
      return is_public_namespace(space.kind) && space.name.empty();
    };
    static_cast<void>(
        std::stable_partition(resolved.namespaces.begin(), resolved.namespaces.end(), is_public));
  }
  frame.stack.erase(frame.stack.begin() + static_cast<std::ptrdiff_t>(base),
                    frame.stack.begin() + static_cast<std::ptrdiff_t>(base + parts));
  multiname = &resolved;
  return InterpreterStatus::Ok;
}

namespace {

std::shared_ptr<FunctionObject> bind_method(const std::shared_ptr<Object>& receiver,
                                            const FunctionObject& method) {
  std::weak_ptr<FunctionObject>& cached = receiver->bound_methods()[&method];
  if (std::shared_ptr<FunctionObject> existing = cached.lock()) {
    return existing;
  }
  auto bound =
      std::make_shared<FunctionObject>(method.name(), method.file(), method.method_index());
  bound->set_native_id(method.native_id());
  bound->set_owner_class(method.owner_class());
  bound->set_instance_class(method.instance_class());
  bound->bind_receiver(receiver);
  cached = bound;
  return bound;
}

}  // namespace

ExecutionResult Interpreter::get_property(Frame& frame, const std::shared_ptr<Object>& owner,
                                          const Multiname& multiname, std::size_t depth) {
  (void)frame;
  Object& object = *owner;
  if (object.has_primitive() && object.primitive().is_string() && multiname.has_fixed_name()) {
    const std::string& text = object.primitive().as_string();
    if (multiname.name == "length") {
      ExecutionResult result;
      result.value = Value::number(static_cast<double>(utf16_from_utf8(text).size()));
      return result;
    }
    bool numeric = !multiname.name.empty();
    for (const char character : multiname.name) {
      numeric = numeric && character >= '0' && character <= '9';
    }
    if (numeric && multiname.name.size() <= 10) {
      const std::size_t index = static_cast<std::size_t>(std::stoul(multiname.name));
      const std::u16string units = utf16_from_utf8(text);
      ExecutionResult result;
      result.value = index < units.size()
                         ? Value::string(utf8_from_utf16(units.substr(index, 1)))
                         : Value::undefined();
      return result;
    }
  }
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
  if (object.instance_class() != nullptr && object.instance_class()->name == "flash.utils::Timer" &&
      multiname.has_fixed_name()) {
    auto& timer = static_cast<TimerObject&>(object);
    if (multiname.name == "delay") {
      ExecutionResult result;
      result.value = Value::number(timer.delay());
      return result;
    }
    if (multiname.name == "repeatCount") {
      ExecutionResult result;
      result.value = Value::int32(timer.repeat_count());
      return result;
    }
    if (multiname.name == "currentCount") {
      ExecutionResult result;
      result.value = Value::int32(timer.current_count());
      return result;
    }
    if (multiname.name == "running") {
      ExecutionResult result;
      result.value = Value::boolean(timer.running());
      return result;
    }
  }
  const PropertyLookup lookup = object.lookup(multiname);
  const bool declared_in_abc =
      lookup.found() && lookup.owner != nullptr && lookup.owner->file != nullptr;
  if (object.kind() == ObjectKind::Event && !declared_in_abc) {
    Value event_value;
    if (read_event_property(*this, static_cast<EventObject&>(object), multiname, event_value)) {
      ExecutionResult result;
      result.value = std::move(event_value);
      return result;
    }
  }
  if (object.kind() == ObjectKind::Display && !declared_in_abc) {
    Value display_value;
    if (read_display_property(*this, static_cast<DisplayObject&>(object), multiname,
                              display_value)) {
      ExecutionResult result;
      result.value = std::move(display_value);
      return result;
    }
  }
  if (object.kind() == ObjectKind::Function && multiname.has_fixed_name()) {
    const auto& function = static_cast<const FunctionObject&>(object);
    if (multiname.name == "length") {
      ExecutionResult result;
      result.value = Value::number(static_cast<double>(function.param_count()));
      return result;
    }
    if (multiname.name == "prototype") {
      const Key key = primary_key(multiname);
      if (const Property* existing = object.find_own_property(key)) {
        ExecutionResult result;
        result.value = existing->value;
        return result;
      }
      auto prototype = std::make_shared<Object>();
      prepare_plain_object(*prototype);
      Property& property = object.define_own_property(key);
      property.kind = PropertyKind::Value;
      property.value = Value::object(std::move(prototype));
      ExecutionResult result;
      result.value = property.value;
      return result;
    }
  }
  if (object.kind() == ObjectKind::Class && multiname.has_fixed_name() &&
      multiname.name == "prototype") {
    const Class* klass = static_cast<const ClassObject&>(object).represented_class();
    ExecutionResult result;
    result.value = klass == nullptr || klass->prototype_object == nullptr
                       ? Value::undefined()
                       : Value::object(klass->prototype_object);
    return result;
  }
  if (object.kind() == ObjectKind::Error && multiname.has_fixed_name()) {
    const auto& error = static_cast<const ErrorObject&>(object);
    if (multiname.name == "name") {
      ExecutionResult result;
      result.value = Value::string(error.error_name());
      return result;
    }
    if (multiname.name == "message") {
      ExecutionResult result;
      result.value = Value::string(error.message());
      return result;
    }
  }
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
      result.value = Value::object(bind_method(owner, *lookup.trait->function));
    }
    return result;
  }
  if (lookup.kind == LookupKind::Accessor) {
    if (lookup.getter == nullptr) {
      return ExecutionResult{};
    }
    return invoke(*lookup.getter, Value::object(owner), {}, {}, depth);
  }
  return ExecutionResult{};
}

ExecutionResult Interpreter::set_property(Frame& frame, const std::shared_ptr<Object>& owner,
                                          const Multiname& multiname, const Value& value,
                                          bool define, std::size_t depth) {
  (void)frame;
  Object& object = *owner;
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
  if (object.instance_class() != nullptr && object.instance_class()->name == "flash.utils::Timer" &&
      multiname.has_fixed_name()) {
    auto& timer = static_cast<TimerObject&>(object);
    if (multiname.name == "delay") {
      ExecutionResult result;
      result.value = Value::number(timer.delay());
      return result;
    }
    if (multiname.name == "repeatCount") {
      ExecutionResult result;
      result.value = Value::int32(timer.repeat_count());
      return result;
    }
    if (multiname.name == "currentCount") {
      ExecutionResult result;
      result.value = Value::int32(timer.current_count());
      return result;
    }
    if (multiname.name == "running") {
      ExecutionResult result;
      result.value = Value::boolean(timer.running());
      return result;
    }
  }
  const PropertyLookup lookup = object.lookup(multiname);
  const bool declared_in_abc =
      lookup.found() && lookup.owner != nullptr && lookup.owner->file != nullptr;
  if (object.kind() == ObjectKind::Event && !declared_in_abc) {
    Value event_value;
    if (read_event_property(*this, static_cast<EventObject&>(object), multiname, event_value)) {
      ExecutionResult result;
      result.value = std::move(event_value);
      return result;
    }
  }
  if (object.kind() == ObjectKind::Display && !declared_in_abc) {
    if (multiname.has_fixed_name() && multiname.name == "focus") {
      std::shared_ptr<DisplayObject> target;
      if (value.is_object() && value.as_object()->kind() == ObjectKind::Display) {
        target = std::static_pointer_cast<DisplayObject>(value.as_object());
      }
      return set_display_focus(*this, static_cast<DisplayObject&>(object), target, depth);
    }
    if (write_display_property(static_cast<DisplayObject&>(object), multiname, value)) {
      return ExecutionResult{};
    }
  }
  if (object.instance_class() != nullptr && object.instance_class()->name == "flash.utils::Timer" &&
      multiname.has_fixed_name()) {
    auto& timer = static_cast<TimerObject&>(object);
    if (multiname.name == "delay") {
      timer.set_delay(to_number(value));
      return ExecutionResult{};
    }
    if (multiname.name == "repeatCount") {
      timer.set_repeat_count(to_int32(value));
      return ExecutionResult{};
    }
    if (multiname.name == "running") {
      timer.set_running(to_boolean(value));
      return ExecutionResult{};
    }
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
  if (lookup.kind == LookupKind::Accessor && lookup.setter != nullptr) {
    return invoke(*lookup.setter, Value::object(owner), {value}, {}, depth);
  }
  if (object.write_value(multiname, value)) {
    return ExecutionResult{};
  }
  return fail(InterpreterStatus::TypeError,
              "cannot write property " + property_text(multiname) + " on a " +
                  object_kind_name(object.kind()));
}

namespace {

bool scope_has_property(Interpreter& interpreter, Object& object, const Multiname& multiname) {
  if (object.has_property(multiname)) {
    return true;
  }
  Value probe;
  return object.kind() == ObjectKind::Display &&
         read_display_property(interpreter, static_cast<DisplayObject&>(object), multiname, probe);
}

}  // namespace

InterpreterStatus Interpreter::find_scope_binding(Frame& frame, const Multiname& multiname,
                                                  bool create_on_global,
                                                  std::shared_ptr<Object>& owner, bool& found,
                                                  std::string& message) {
  found = false;
  for (auto current = frame.scopes.rbegin(); current != frame.scopes.rend(); ++current) {
    if (*current != nullptr && scope_has_property(*this, **current, multiname)) {
      owner = *current;
      found = true;
      return InterpreterStatus::Ok;
    }
  }
  if (multiname.has_fixed_name() && !multiname.attribute) {
    const bool package_named =
        !multiname.namespaces.empty() && !multiname.namespaces.front().name.empty();
    const bool capitalised =
        !multiname.name.empty() && multiname.name.front() >= 'A' && multiname.name.front() <= 'Z';
    if (package_named || capitalised) {
      if (runtime_.resolve_or_create_global_class(multiname) != nullptr) {
        owner = runtime_.global_object_shared();
        found = owner != nullptr;
        return InterpreterStatus::Ok;
      }
    }
  }
  if (create_on_global && !frame.scopes.empty() && multiname.has_fixed_name()) {
    const std::shared_ptr<Object>& global = frame.scopes.front();
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

ExecutionResult Interpreter::call_property(Frame& frame, const std::shared_ptr<Object>& owner,
                                           const Multiname& multiname,
                                           const std::vector<Value>& arguments,
                                           std::size_t depth) {
  (void)frame;
  Object& receiver = *owner;
  if (receiver.kind() == ObjectKind::Class) {
    ExecutionResult initialized = initialize_class_value(Value::object(owner), depth);
    if (!initialized.ok()) {
      return initialized;
    }
  }
  const PropertyLookup lookup = receiver.lookup(multiname);
  if (!lookup.found()) {
    if (limits_.lenient_builtins) {
      return ExecutionResult{};
    }
    return fail(InterpreterStatus::CannotCall,
                "no property " + property_text(multiname) + " on a " +
                    object_kind_name(receiver.kind()) +
                    (receiver.kind() == ObjectKind::Class &&
                             static_cast<const ClassObject&>(receiver).represented_class() != nullptr
                         ? " (" + static_cast<const ClassObject&>(receiver).represented_class()->name + ")"
                         : ""));
  }
  if (lookup.kind == LookupKind::Method) {
    if (lookup.trait == nullptr || lookup.trait->function == nullptr) {
      return fail(InterpreterStatus::CannotCall,
                  "method trait " + property_text(multiname) + " has no function");
    }
    return invoke(*lookup.trait->function, Value::object(owner), arguments, {}, depth);
  }
  if (lookup.kind == LookupKind::Accessor) {
    if (lookup.getter == nullptr) {
      return fail(InterpreterStatus::CannotCall,
                  "property " + property_text(multiname) + " has no getter to call");
    }
    ExecutionResult getter = invoke(*lookup.getter, Value::object(owner), {}, {}, depth);
    if (!getter.ok()) {
      return getter;
    }
    if (!getter.value.is_object() ||
        getter.value.as_object()->kind() != ObjectKind::Function) {
      if (limits_.lenient_builtins) {
        return ExecutionResult{};
      }
      return fail(InterpreterStatus::CannotCall,
                  "property " + property_text(multiname) + " is not callable");
    }
    return invoke(static_cast<const FunctionObject&>(*getter.value.as_object()),
                  Value::object(owner), arguments, {}, depth);
  }
  if (!lookup.value.is_object() || lookup.value.as_object()->kind() != ObjectKind::Function) {
    if (limits_.lenient_builtins) {
      return ExecutionResult{};
    }
    return fail(InterpreterStatus::CannotCall,
                "property " + property_text(multiname) + " is not callable");
  }
  return invoke(static_cast<const FunctionObject&>(*lookup.value.as_object()),
                Value::object(owner), arguments, {}, depth);
}

ExecutionResult Interpreter::call_indexed(Frame& frame, const abc::AbcInstruction& instruction,
                                          std::size_t depth) {
  const std::string name = instruction.mnemonic;
  const std::uint32_t index = operand_at(instruction, 0);
  std::vector<Value> arguments;
  std::string message;
  InterpreterStatus status = pop_arguments(frame, operand_at(instruction, 1), arguments, message);
  Value receiver;
  if (status == InterpreterStatus::Ok) {
    status = pop(frame, receiver, message);
  }
  if (status != InterpreterStatus::Ok) {
    return fail(status, message);
  }
  if (!receiver.is_object()) {
    receiver = box(receiver);
  }
  if (!receiver.is_object()) {
    return fail(InterpreterStatus::NullReference, name + " needs an object receiver");
  }
  if (instruction.opcode == op::k_callstatic) {
    const std::size_t method_count = frame.file->methods.size();
    if (index >= method_count) {
      return fail(InterpreterStatus::MethodIndexOutOfRange,
                  name + " method index " + std::to_string(index) + " is outside the " +
                      std::to_string(method_count) + "-entry method table");
    }
    const auto function =
        std::make_shared<FunctionObject>(frame.file->method_name(index), frame.file, index);
    return invoke(*function, receiver, std::move(arguments), {}, depth);
  }
  const Class* klass = receiver.as_object()->instance_class();
  const Trait* trait = klass == nullptr ? nullptr : klass->resolve_dispatch_trait(index);
  if (trait == nullptr || trait->function == nullptr) {
    return fail(InterpreterStatus::CannotCall,
                name + " has no method " + std::to_string(index) + " on this receiver");
  }
  return invoke(*trait->function, receiver, std::move(arguments), {}, depth);
}

ExecutionResult Interpreter::outer_scope_value(const Frame& frame, std::uint32_t index) const {
  if (frame.outer_scope == nullptr || frame.outer_scope->empty()) {
    return fail(InterpreterStatus::NotImplemented,
                "opcode getouterscope needs a closure's captured outer scope; "
                "this method has none");
  }
  if (index >= frame.outer_scope->size()) {
    return fail(InterpreterStatus::ScopeUnderflow,
                "getouterscope " + std::to_string(index) + " is outside the " +
                    std::to_string(frame.outer_scope->size()) + "-entry captured outer scope");
  }
  if ((*frame.outer_scope)[index] == nullptr) {
    return fail(InterpreterStatus::ScopeUnderflow,
                "getouterscope " + std::to_string(index) + " is a null scope");
  }
  ExecutionResult result;
  result.value = Value::object((*frame.outer_scope)[index]);
  return result;
}

ExecutionResult Interpreter::construct(Frame& frame, const ClassObject& class_object,
                                       const std::vector<Value>& arguments, std::size_t depth) {
  (void)frame;
  Class* klass = class_object.represented_class();
  if (klass == nullptr) {
    return fail(InterpreterStatus::CannotConstruct, "class object has no class");
  }
  if (klass->native_constructor != 0) {
    const Value class_self =
        klass->class_object == nullptr ? Value::undefined() : Value::object(klass->class_object);
    return call_native_id(klass->native_constructor, class_self, arguments, depth);
  }
  ExecutionResult initialized = ensure_class_initialized(*klass, depth);
  if (!initialized.ok()) {
    return initialized;
  }
  if (klass->vector_element != nullptr) {
    auto array = std::make_shared<ArrayObject>();
    array->set_instance_class(klass);
    array->set_element_class(klass->vector_element);
    if (klass->prototype_object != nullptr) {
      array->set_prototype(klass->prototype_object);
    }
    ExecutionResult result;
    result.value = Value::object(std::move(array));
    return result;
  }
  const Class* display_object = find_display_class(runtime_, "flash.display::DisplayObject");
  std::shared_ptr<Object> instance =
      display_object != nullptr && klass->is_subclass_of(display_object)
          ? std::shared_ptr<Object>(make_display_object(runtime_, *klass))
          : std::make_shared<Object>();
  instance->set_instance_class(klass);
  klass->apply_slot_defaults(*instance);
  if (klass->iinit != kNoMethod && klass->file != nullptr) {
    auto initializer = std::make_shared<FunctionObject>(klass->file->method_name(klass->iinit),
                                                        klass->file, klass->iinit);
    initializer->set_owner_class(klass);
    ExecutionResult constructed =
        invoke(*initializer, Value::object(instance), arguments, {}, depth);
    if (!constructed.ok()) {
      return constructed;
    }
  }
  ExecutionResult result;
  result.value = Value::object(std::move(instance));
  return result;
}

std::vector<Key>& Interpreter::enumeration_keys(Frame& frame, Object& object) {
  std::vector<Key>& keys = frame.iterator_keys[&object];
  if (!keys.empty()) {
    return keys;
  }
  if (object.kind() == ObjectKind::Array) {
    const auto& array = static_cast<const ArrayObject&>(object);
    for (std::size_t index = 0; index < array.elements().size(); ++index) {
      Key key;
      key.name = std::to_string(index);
      keys.push_back(std::move(key));
    }
    return keys;
  }
  for (const auto& entry : object.own_properties()) {
    keys.push_back(entry.first);
  }
  for (const Class* klass = object.instance_class(); klass != nullptr; klass = klass->super) {
    for (const Trait& trait : klass->instance_traits) {
      if (!trait.is_slot_like() || !trait.multiname.has_fixed_name()) {
        continue;
      }
      Key key;
      key.name = trait.multiname.name;
      if (!trait.multiname.namespaces.empty()) {
        key.name_space = trait.multiname.namespaces.front();
      }
      keys.push_back(std::move(key));
    }
  }
  return keys;
}

bool Interpreter::is_instance_of(const Value& value, const Value& type) const {
  if (!value.is_object() || !type.is_object() || type.as_object()->kind() != ObjectKind::Class) {
    return false;
  }
  const Class* target = static_cast<const ClassObject&>(*type.as_object()).represented_class();
  if (target == nullptr) {
    return false;
  }
  const ObjectKind kind = value.as_object()->kind();
  if ((kind == ObjectKind::Class || kind == ObjectKind::Function) &&
      (target->name == "Object" || target->name == "Function" ||
       (kind == ObjectKind::Class && target->name == "Class"))) {
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
  if ((kind == ObjectKind::Class && (name == "Class" || name == "Function")) ||
      (kind == ObjectKind::Function && name == "Function")) {
    return InterpreterStatus::Ok;
  }
  message = "value is not of type " + name;
  return InterpreterStatus::TypeError;
}

namespace {

bool byte_array_index_access(ByteArrayObject& bytes, const Multiname& multiname, bool write,
                             Value& value) {
  std::uint32_t index = 0;
  if (!multiname.has_fixed_name() || !is_array_index(multiname.name, index)) {
    return false;
  }
  if (write) {
    bytes.set_byte_at(index, static_cast<std::uint8_t>(to_uint32(value)));
  } else {
    value = Value::uint32(bytes.byte_at(index));
  }
  return true;
}

}  // namespace

InterpreterStatus Interpreter::array_property(Object& object, const Multiname& multiname,
                                              bool write, Value& value, bool& handled,
                                              std::string& message) {
  handled = false;
  if (object.kind() == ObjectKind::ByteArray) {
    handled = byte_array_index_access(static_cast<ByteArrayObject&>(object), multiname, write,
                                      value);
    return InterpreterStatus::Ok;
  }
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
    if (array.element_class() != nullptr) {
      const InterpreterStatus coerced = coerce_value(array.element_class()->name_multiname, value,
                                                     message);
      if (coerced != InterpreterStatus::Ok) {
        return coerced;
      }
    }
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
