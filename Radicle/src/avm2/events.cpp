#include <fo4/avm2/events.hpp>

#include <fo4/avm2/class.hpp>
#include <fo4/avm2/coercion.hpp>
#include <fo4/avm2/display.hpp>
#include <fo4/avm2/interpreter.hpp>
#include <fo4/avm2/name.hpp>
#include <fo4/avm2/object.hpp>
#include <fo4/avm2/runtime.hpp>
#include <fo4/avm2/value.hpp>

#include "native_internal.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace fo4::avm2 {

namespace {

std::vector<std::shared_ptr<Object>> propagation_path(const std::shared_ptr<Object>& target) {
  std::vector<std::shared_ptr<Object>> path;
  Object* current = target.get();
  while (current != nullptr) {
    auto shared = current->kind() == ObjectKind::Display
                      ? static_cast<DisplayObject*>(current)->shared_from_this()
                      : std::shared_ptr<Object>{};
    path.push_back(shared != nullptr ? shared : target);
    if (current->kind() != ObjectKind::Display) {
      break;
    }
    current = static_cast<DisplayObject*>(current)->parent();
  }
  std::reverse(path.begin(), path.end());
  return path;
}

ExecutionResult invoke_listeners(Interpreter& interpreter, Object& node,
                                 const std::shared_ptr<Object>& node_value,
                                 const std::shared_ptr<EventObject>& event, bool capture_only,
                                 std::size_t depth) {
  std::vector<EventListener> listeners = node.event_listeners();
  ExecutionResult first;
  for (const EventListener& listener : listeners) {
    if (listener.type != event->event_type() || listener.function == nullptr) {
      continue;
    }
    if (capture_only && !listener.use_capture) {
      continue;
    }
    if (!capture_only && event->event_phase() != 2 && listener.use_capture) {
      continue;
    }
    event->set_current_target(node_value);
    ExecutionResult result = interpreter.call_function(*listener.function, Value::object(node_value),
                                                       {Value::object(event)}, depth + 1);
    interpreter.record_handler_failure(result);
    if (first.ok()) {
      first = std::move(result);
    }
    if (event->immediate_stopped()) {
      break;
    }
  }
  return first;
}

ExecutionResult dispatch_focus(Interpreter& interpreter, const std::shared_ptr<DisplayObject>& node,
                               const char* type, const std::shared_ptr<DisplayObject>& related,
                               std::size_t depth) {
  std::shared_ptr<EventObject> event =
      make_event(interpreter.runtime(), type, true, false, "FocusEvent");
  if (event == nullptr) {
    return ExecutionResult{};
  }
  Property& related_property =
      event->define_own_property(primary_key(builtin_name("relatedObject")));
  related_property.kind = PropertyKind::Value;
  related_property.value = related == nullptr ? Value::null() : Value::object(related);
  return dispatch_event(interpreter, node, event, depth);
}

bool same_listener_function(const FunctionObject* registered, const FunctionObject* given) {
  if (registered == given) {
    return true;
  }
  return registered != nullptr && given != nullptr && registered->bound_receiver() != nullptr &&
         registered->bound_receiver() == given->bound_receiver() &&
         registered->file() == given->file() &&
         registered->method_index() == given->method_index() &&
         registered->native_id() == given->native_id();
}

ExecutionResult dispatch_stage_tree(Interpreter& interpreter,
                                    const std::shared_ptr<DisplayObject>& node, const char* type,
                                    std::size_t depth) {
  ExecutionResult result = dispatch_event(interpreter, node, type, false, depth);
  if (!node->is_container()) {
    return result;
  }
  const auto children = static_cast<DisplayObjectContainer&>(*node).children();
  for (const auto& child : children) {
    ExecutionResult child_result = dispatch_stage_tree(interpreter, child, type, depth);
    if (result.ok()) {
      result = std::move(child_result);
    }
  }
  return result;
}

}  // namespace

EventObject::EventObject(Class* event_class, std::string type, bool bubbles, bool cancelable)
    : Object(ObjectKind::Event),
      type_(std::move(type)),
      bubbles_(bubbles),
      cancelable_(cancelable) {
  if (event_class != nullptr) {
    set_instance_class(event_class);
    if (event_class->prototype_object != nullptr) {
      set_prototype(event_class->prototype_object);
    }
  }
  set_dynamic(true);
}

ExecutionResult dispatch_event(Interpreter& interpreter, const std::shared_ptr<Object>& target,
                               const std::shared_ptr<EventObject>& event, std::size_t depth) {
  if (target == nullptr || event == nullptr) {
    return ExecutionResult{};
  }
  const std::vector<std::shared_ptr<Object>> path = propagation_path(target);
  event->set_target(target);
  if (event->propagation_stopped()) {
    return ExecutionResult{};
  }

  ExecutionResult first;
  const auto keep = [&first](ExecutionResult result) {
    if (first.ok()) {
      first = std::move(result);
    }
  };
  event->set_event_phase(1);
  for (std::size_t index = 0; index + 1 < path.size(); ++index) {
    keep(invoke_listeners(interpreter, *path[index], path[index], event, true, depth));
    if (event->propagation_stopped()) {
      return first;
    }
  }

  event->set_event_phase(2);
  keep(invoke_listeners(interpreter, *path.back(), path.back(), event, false, depth));
  if (event->propagation_stopped() || !event->bubbles()) {
    event->set_current_target(nullptr);
    return first;
  }

  event->set_event_phase(3);
  for (std::size_t index = path.size() - 1; index > 0; --index) {
    keep(invoke_listeners(interpreter, *path[index - 1], path[index - 1], event, false, depth));
    if (event->propagation_stopped()) {
      break;
    }
  }
  event->set_current_target(nullptr);
  return first;
}

ExecutionResult dispatch_event(Interpreter& interpreter, const std::shared_ptr<Object>& target,
                               std::string type, bool bubbles, std::size_t depth) {
  if (target == nullptr) {
    return ExecutionResult{};
  }
  std::shared_ptr<EventObject> event = make_event(interpreter.runtime(), type, bubbles, false);
  if (event == nullptr) {
    return ExecutionResult{};
  }
  return dispatch_event(interpreter, target, event, depth);
}

ExecutionResult dispatch_added(Interpreter& interpreter,
                               const std::shared_ptr<DisplayObject>& child, std::size_t depth) {
  ExecutionResult added = dispatch_event(interpreter, child, "added", true, depth);
  if (child->stage() == nullptr) {
    return added;
  }
  ExecutionResult tree = dispatch_stage_tree(interpreter, child, "addedToStage", depth);
  return added.ok() ? tree : added;
}

ExecutionResult dispatch_removed(Interpreter& interpreter,
                                 const std::shared_ptr<DisplayObject>& child, std::size_t depth) {
  const bool on_stage = child->stage() != nullptr;
  ExecutionResult removed = dispatch_event(interpreter, child, "removed", true, depth);
  if (!on_stage) {
    return removed;
  }
  ExecutionResult tree = dispatch_stage_tree(interpreter, child, "removedFromStage", depth);
  return removed.ok() ? tree : removed;
}

bool add_event_listener(Object& target, std::string_view type, const Value& listener,
                        bool use_capture, std::int32_t priority) {
  if (!listener.is_object() || listener.as_object()->kind() != ObjectKind::Function) {
    return false;
  }
  const auto* function = static_cast<const FunctionObject*>(listener.as_object().get());
  for (const EventListener& existing : target.event_listeners()) {
    if (existing.type == type && existing.use_capture == use_capture &&
        same_listener_function(existing.function.get(), function)) {
      return true;
    }
  }
  EventListener entry;
  entry.type = std::string{type};
  entry.function = std::static_pointer_cast<FunctionObject>(listener.as_object());
  entry.use_capture = use_capture;
  entry.priority = priority;
  target.event_listeners().push_back(std::move(entry));
  std::stable_sort(target.event_listeners().begin(), target.event_listeners().end(),
                   [](const EventListener& left, const EventListener& right) {
                     return left.priority > right.priority;
                   });
  return true;
}

bool remove_event_listener(Object& target, std::string_view type, const Value& listener,
                           bool use_capture) {
  if (!listener.is_object() || listener.as_object()->kind() != ObjectKind::Function) {
    return false;
  }
  const auto* function = static_cast<const FunctionObject*>(listener.as_object().get());
  auto& listeners = target.event_listeners();
  const auto found = std::find_if(listeners.begin(), listeners.end(),
                                  [&](const EventListener& entry) {
                                    return entry.type == type && entry.use_capture == use_capture &&
                                           same_listener_function(entry.function.get(), function);
                                  });
  if (found == listeners.end()) {
    return false;
  }
  listeners.erase(found);
  return true;
}

bool has_event_listener(const Object& target, std::string_view type) {
  for (const EventListener& entry : target.event_listeners()) {
    if (entry.type == type) {
      return true;
    }
  }
  return false;
}

bool will_trigger(const std::shared_ptr<Object>& target, std::string_view type) {
  if (target == nullptr) {
    return false;
  }
  for (const auto& node : propagation_path(target)) {
    if (has_event_listener(*node, type)) {
      return true;
    }
  }
  return false;
}

void sync_frame_listener(Runtime& runtime, const std::shared_ptr<Object>& target) {
  if (target == nullptr || target->kind() != ObjectKind::Display) {
    return;
  }
  const auto display = std::static_pointer_cast<DisplayObject>(target);
  if (has_event_listener(*target, "enterFrame") || has_event_listener(*target, "exitFrame") ||
      has_event_listener(*target, "render")) {
    runtime.track_frame_listener(display);
  } else {
    runtime.untrack_frame_listener(display.get());
  }
}

ExecutionResult set_display_focus(Interpreter& interpreter, DisplayObject& owner,
                                  const std::shared_ptr<DisplayObject>& target, std::size_t depth) {
  const std::shared_ptr<DisplayObject> previous = owner.focus();
  if (previous == target) {
    return ExecutionResult{};
  }
  owner.set_focus(target);
  ExecutionResult unfocused;
  if (previous != nullptr) {
    unfocused = dispatch_focus(interpreter, previous, "focusOut", target, depth);
  }
  if (target == nullptr) {
    return unfocused;
  }
  ExecutionResult focused = dispatch_focus(interpreter, target, "focusIn", previous, depth);
  return unfocused.ok() ? focused : unfocused;
}

TimerObject::TimerObject(Class* timer_class, double delay, std::int32_t repeat_count)
    : Object(ObjectKind::Object), delay_(delay), repeat_count_(repeat_count) {
  if (timer_class != nullptr) {
    set_instance_class(timer_class);
    if (timer_class->prototype_object != nullptr) {
      set_prototype(timer_class->prototype_object);
    }
  }
  set_dynamic(true);
}

void HostClock::register_timer(const std::shared_ptr<TimerObject>& timer) {
  if (timer == nullptr) {
    return;
  }
  for (const auto& existing : timers_) {
    if (existing.get() == timer.get()) {
      return;
    }
  }
  timers_.push_back(timer);
}

void HostClock::unregister_timer(const TimerObject* timer) {
  timers_.erase(std::remove_if(timers_.begin(), timers_.end(),
                               [timer](const std::shared_ptr<TimerObject>& entry) {
                                 return entry.get() == timer;
                               }),
                timers_.end());
}

void HostClock::advance(Interpreter& interpreter, double dt_ms) {
  if (dt_ms <= 0.0) {
    return;
  }
  time_ms_ += dt_ms;
  const std::vector<std::shared_ptr<TimerObject>> timers = timers_;
  for (const auto& timer : timers) {
    if (timer == nullptr || !timer->running()) {
      continue;
    }
    const double delay = std::max(1.0, timer->delay());
    timer->add_accumulator(dt_ms);
    std::size_t fired = 0;
    while (timer->running() && timer->accumulator() >= delay && fired < 1000) {
      timer->add_accumulator(-delay);
      timer->set_current_count(timer->current_count() + 1);
      ++fired;
      std::shared_ptr<EventObject> event =
          make_event(interpreter.runtime(), "timer", false, false, "TimerEvent");
      if (event != nullptr) {
        dispatch_event(interpreter, std::static_pointer_cast<Object>(timer), event, 0);
      }
      if (timer->repeat_count() != 0 && timer->current_count() >= timer->repeat_count()) {
        timer->set_running(false);
        std::shared_ptr<EventObject> complete =
            make_event(interpreter.runtime(), "timerComplete", false, false, "TimerEvent");
        if (complete != nullptr) {
          dispatch_event(interpreter, std::static_pointer_cast<Object>(timer), complete, 0);
        }
        break;
      }
    }
  }
}

bool read_event_property(Interpreter& interpreter, EventObject& event, const Multiname& multiname,
                         Value& out) {
  if (!multiname.has_fixed_name()) {
    return false;
  }
  const std::string& name = multiname.name;
  if (name == "type") {
    out = Value::string(event.event_type());
    return true;
  }
  if (name == "bubbles") {
    out = Value::boolean(event.bubbles());
    return true;
  }
  if (name == "cancelable") {
    out = Value::boolean(event.cancelable());
    return true;
  }
  if (name == "eventPhase") {
    out = Value::uint32(event.event_phase());
    return true;
  }
  if (name == "target") {
    const auto& target = event.target();
    out = target == nullptr ? Value::null() : Value::object(target);
    return true;
  }
  if (name == "currentTarget") {
    const auto& current = event.current_target();
    out = current == nullptr ? Value::null() : Value::object(current);
    return true;
  }
  (void)interpreter;
  return false;
}

void Interpreter::advance_host_time(double dt_ms) { host_clock_.advance(*this, dt_ms); }

std::shared_ptr<EventObject> make_event(Runtime& runtime, std::string_view type, bool bubbles,
                                        bool cancelable, std::string_view class_name) {
  const std::string rendered = "flash.events::" + std::string{class_name};
  Class* event_class = const_cast<Class*>(runtime.find_class(rendered));
  if (event_class == nullptr) {
    event_class = const_cast<Class*>(runtime.find_class("flash.events::Event"));
  }
  if (event_class == nullptr) {
    return nullptr;
  }
  return std::make_shared<EventObject>(event_class, std::string{type}, bubbles, cancelable);
}

}  // namespace fo4::avm2
