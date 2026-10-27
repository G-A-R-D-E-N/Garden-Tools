#include <fo4/avm2/class.hpp>
#include <fo4/avm2/coercion.hpp>
#include <fo4/avm2/events.hpp>
#include <fo4/avm2/interpreter.hpp>
#include <fo4/avm2/name.hpp>
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

const char* class_local_name(const Class* klass) {
  if (klass == nullptr) {
    return "";
  }
  const std::size_t separator = klass->name.rfind("::");
  return separator == std::string::npos ? klass->name.c_str() : klass->name.c_str() + separator + 2;
}

EventObject* event_self(const Value& self) {
  if (!self.is_object() || self.as_object()->kind() != ObjectKind::Event) {
    return nullptr;
  }
  return static_cast<EventObject*>(self.as_object().get());
}

TimerObject* timer_self(const Value& self) {
  if (!self.is_object()) {
    return nullptr;
  }
  const Class* klass = self.as_object()->instance_class();
  if (klass == nullptr || klass->name != "flash.utils::Timer") {
    return nullptr;
  }
  return static_cast<TimerObject*>(self.as_object().get());
}

void set_dynamic_number(Object& object, const char* name, double value) {
  Property& property = object.define_own_property(primary_key(builtin_name(name)));
  property.kind = PropertyKind::Value;
  property.value = Value::number(value);
}

void set_dynamic_boolean(Object& object, const char* name, bool value) {
  Property& property = object.define_own_property(primary_key(builtin_name(name)));
  property.kind = PropertyKind::Value;
  property.value = Value::boolean(value);
}

void set_dynamic_value(Object& object, const char* name, Value value) {
  Property& property = object.define_own_property(primary_key(builtin_name(name)));
  property.kind = PropertyKind::Value;
  property.value = std::move(value);
}

ExecutionResult event_construct(Interpreter& interpreter, const Value& self,
                                const std::vector<Value>& arguments) {
  Class* klass = nullptr;
  if (self.is_object() && self.as_object()->kind() == ObjectKind::Class) {
    klass = static_cast<const ClassObject&>(*self.as_object()).represented_class();
  }
  if (klass == nullptr) {
    return native_value(Value::undefined());
  }
  const std::string type = arguments.empty() ? std::string{} : to_string(arguments[0]);
  const bool bubbles = arguments.size() > 1 && to_boolean(arguments[1]);
  const bool cancelable = arguments.size() > 2 && to_boolean(arguments[2]);
  auto event = std::make_shared<EventObject>(klass, type, bubbles, cancelable);
  const std::string local_name = class_local_name(klass);
  if (local_name == "KeyboardEvent") {
    set_dynamic_number(*event, "charCode", to_number(argument_at(arguments, 3)));
    set_dynamic_number(*event, "keyCode", to_number(argument_at(arguments, 4)));
    set_dynamic_number(*event, "keyLocation", to_number(argument_at(arguments, 5)));
    set_dynamic_boolean(*event, "ctrlKey", to_boolean(argument_at(arguments, 6)));
    set_dynamic_boolean(*event, "altKey", to_boolean(argument_at(arguments, 7)));
    set_dynamic_boolean(*event, "shiftKey", to_boolean(argument_at(arguments, 8)));
  } else if (local_name == "MouseEvent") {
    const double local_x = to_number(argument_at(arguments, 3));
    const double local_y = to_number(argument_at(arguments, 4));
    set_dynamic_number(*event, "localX", local_x);
    set_dynamic_number(*event, "localY", local_y);
    set_dynamic_number(*event, "stageX", local_x);
    set_dynamic_number(*event, "stageY", local_y);
    set_dynamic_value(*event, "relatedObject", argument_at(arguments, 5));
    set_dynamic_boolean(*event, "ctrlKey", to_boolean(argument_at(arguments, 6)));
    set_dynamic_boolean(*event, "altKey", to_boolean(argument_at(arguments, 7)));
    set_dynamic_boolean(*event, "shiftKey", to_boolean(argument_at(arguments, 8)));
    set_dynamic_boolean(*event, "buttonDown", to_boolean(argument_at(arguments, 9)));
    set_dynamic_number(*event, "delta", to_number(argument_at(arguments, 10)));
  } else if (local_name == "FocusEvent") {
    set_dynamic_value(*event, "relatedObject", argument_at(arguments, 3));
    set_dynamic_boolean(*event, "shiftKey", to_boolean(argument_at(arguments, 4)));
  }
  (void)interpreter;
  return native_value(Value::object(std::move(event)));
}

ExecutionResult event_prevent_default(const Value& self) {
  if (EventObject* event = event_self(self)) {
    event->prevent_default();
  }
  return native_value(Value::undefined());
}

ExecutionResult event_stop_propagation(const Value& self) {
  if (EventObject* event = event_self(self)) {
    event->stop_propagation();
  }
  return native_value(Value::undefined());
}

ExecutionResult event_stop_immediate(const Value& self) {
  if (EventObject* event = event_self(self)) {
    event->stop_immediate();
  }
  return native_value(Value::undefined());
}

ExecutionResult event_is_default_prevented(const Value& self) {
  const EventObject* event = event_self(self);
  return native_value(Value::boolean(event != nullptr && event->default_prevented()));
}

ExecutionResult event_clone(const Value& self) {
  EventObject* event = event_self(self);
  if (event == nullptr) {
    return native_value(Value::undefined());
  }
  Class* klass = event->instance_class();
  auto clone = std::make_shared<EventObject>(klass, event->event_type(), event->bubbles(),
                                             event->cancelable());
  for (const auto& entry : event->own_properties()) {
    Property& property = clone->define_own_property(entry.first);
    property.kind = entry.second.kind;
    property.value = entry.second.value;
    property.getter = entry.second.getter;
    property.setter = entry.second.setter;
  }
  return native_value(Value::object(std::move(clone)));
}

ExecutionResult event_to_string(const Value& self) {
  const EventObject* event = event_self(self);
  if (event == nullptr) {
    return native_value(Value::string("[Event]"));
  }
  const Class* klass = event->instance_class();
  const std::string name = klass == nullptr ? std::string{"Event"} : std::string{class_local_name(klass)};
  std::string text = "[" + name + " type=\"" + event->event_type() + "\" bubbles=" +
                     (event->bubbles() ? "true" : "false") + " cancelable=" +
                     (event->cancelable() ? "true" : "false") + "]";
  return native_value(Value::string(std::move(text)));
}

ExecutionResult dispatcher_construct(Interpreter& interpreter, const Value& self) {
  if (!self.is_object() || self.as_object()->kind() != ObjectKind::Class) {
    return native_value(Value::undefined());
  }
  Class* klass = static_cast<const ClassObject&>(*self.as_object()).represented_class();
  if (klass == nullptr) {
    return native_value(Value::undefined());
  }
  return native_value(Value::object(interpreter.make_instance(*klass)));
}

ExecutionResult dispatcher_add_listener(Interpreter& interpreter, const Value& self,
                                        const std::vector<Value>& arguments) {
  (void)interpreter;
  if (!self.is_object()) {
    return native_value(Value::undefined());
  }
  add_event_listener(*self.as_object(), to_string(argument_at(arguments, 0)),
                     argument_at(arguments, 1), to_boolean(argument_at(arguments, 2)),
                     to_int32(argument_at(arguments, 3)));
  return native_value(Value::undefined());
}

ExecutionResult dispatcher_remove_listener(Interpreter& interpreter, const Value& self,
                                           const std::vector<Value>& arguments) {
  (void)interpreter;
  if (!self.is_object()) {
    return native_value(Value::undefined());
  }
  remove_event_listener(*self.as_object(), to_string(argument_at(arguments, 0)),
                        argument_at(arguments, 1), to_boolean(argument_at(arguments, 2)));
  return native_value(Value::undefined());
}

ExecutionResult dispatcher_dispatch_event(Interpreter& interpreter, const Value& self,
                                          const std::vector<Value>& arguments, std::size_t depth) {
  if (!self.is_object()) {
    return native_value(Value::boolean(true));
  }
  const Value event_value = argument_at(arguments, 0);
  EventObject* event = event_self(event_value);
  if (event == nullptr) {
    return native_value(Value::boolean(true));
  }
  std::shared_ptr<EventObject> event_shared =
      std::static_pointer_cast<EventObject>(event_value.as_object());
  ExecutionResult result =
      dispatch_event(interpreter, self.as_object(), event_shared, depth);
  if (!result.ok()) {
    return result;
  }
  return native_value(Value::boolean(!event->default_prevented()));
}

ExecutionResult dispatcher_has_listener(Interpreter& interpreter, const Value& self,
                                        const std::vector<Value>& arguments) {
  (void)interpreter;
  if (!self.is_object()) {
    return native_value(Value::boolean(false));
  }
  return native_value(Value::boolean(
      has_event_listener(*self.as_object(), to_string(argument_at(arguments, 0)))));
}

ExecutionResult timer_construct(Interpreter& interpreter, const Value& self,
                                const std::vector<Value>& arguments) {
  Class* klass = nullptr;
  if (self.is_object() && self.as_object()->kind() == ObjectKind::Class) {
    klass = static_cast<const ClassObject&>(*self.as_object()).represented_class();
  }
  if (klass == nullptr) {
    return native_value(Value::undefined());
  }
  auto timer = std::make_shared<TimerObject>(klass, to_number(argument_at(arguments, 0)),
                                             to_int32(argument_at(arguments, 1)));
  interpreter.host_clock().register_timer(timer);
  return native_value(Value::object(std::move(timer)));
}

ExecutionResult timer_start(const Value& self) {
  if (TimerObject* timer = timer_self(self)) {
    timer->set_running(true);
  }
  return native_value(Value::undefined());
}

ExecutionResult timer_stop(const Value& self) {
  if (TimerObject* timer = timer_self(self)) {
    timer->set_running(false);
  }
  return native_value(Value::undefined());
}

ExecutionResult timer_reset(const Value& self) {
  if (TimerObject* timer = timer_self(self)) {
    timer->set_running(false);
    timer->set_current_count(0);
    timer->reset_accumulator();
  }
  return native_value(Value::undefined());
}

}  // namespace

bool try_event_native(Interpreter& interpreter, native::Id id, const Value& self,
                      const std::vector<Value>& arguments, std::size_t depth,
                      ExecutionResult& out) {
  switch (id) {
    case native::Id::event_construct:
      out = event_construct(interpreter, self, arguments);
      return true;
    case native::Id::event_prevent_default:
      out = event_prevent_default(self);
      return true;
    case native::Id::event_stop_propagation:
      out = event_stop_propagation(self);
      return true;
    case native::Id::event_stop_immediate:
      out = event_stop_immediate(self);
      return true;
    case native::Id::event_is_default_prevented:
      out = event_is_default_prevented(self);
      return true;
    case native::Id::event_clone:
      out = event_clone(self);
      return true;
    case native::Id::event_to_string:
      out = event_to_string(self);
      return true;
    case native::Id::dispatcher_construct:
      out = dispatcher_construct(interpreter, self);
      return true;
    case native::Id::dispatcher_add_listener:
      out = dispatcher_add_listener(interpreter, self, arguments);
      return true;
    case native::Id::dispatcher_remove_listener:
      out = dispatcher_remove_listener(interpreter, self, arguments);
      return true;
    case native::Id::dispatcher_dispatch_event:
      out = dispatcher_dispatch_event(interpreter, self, arguments, depth);
      return true;
    case native::Id::dispatcher_has_listener:
      out = dispatcher_has_listener(interpreter, self, arguments);
      return true;
    case native::Id::timer_construct:
      out = timer_construct(interpreter, self, arguments);
      return true;
    case native::Id::timer_start:
      out = timer_start(self);
      return true;
    case native::Id::timer_stop:
      out = timer_stop(self);
      return true;
    case native::Id::timer_reset:
      out = timer_reset(self);
      return true;
    default:
      return false;
  }
}

}  // namespace fo4::avm2
