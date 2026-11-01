#pragma once

#include <fo4/avm2/class.hpp>
#include <fo4/avm2/name.hpp>
#include <fo4/avm2/object.hpp>
#include <fo4/avm2/value.hpp>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace fo4::avm2 {

class DisplayObject;
class Interpreter;
class Runtime;
struct ExecutionResult;

class EventObject : public Object {
 public:
  EventObject(Class* event_class, std::string type, bool bubbles, bool cancelable);

  const std::string& event_type() const noexcept { return type_; }
  bool bubbles() const noexcept { return bubbles_; }
  bool cancelable() const noexcept { return cancelable_; }
  bool default_prevented() const noexcept { return default_prevented_; }
  bool propagation_stopped() const noexcept { return propagation_stopped_; }
  bool immediate_stopped() const noexcept { return immediate_stopped_; }
  const std::shared_ptr<Object>& target() const noexcept { return target_; }
  const std::shared_ptr<Object>& current_target() const noexcept { return current_target_; }
  std::uint8_t event_phase() const noexcept { return event_phase_; }

  void prevent_default() noexcept { default_prevented_ = true; }
  void stop_propagation() noexcept { propagation_stopped_ = true; }
  void stop_immediate() noexcept {
    immediate_stopped_ = true;
    propagation_stopped_ = true;
  }
  void set_target(const std::shared_ptr<Object>& target) { target_ = target; }
  void set_current_target(const std::shared_ptr<Object>& current) { current_target_ = current; }
  void set_event_phase(std::uint8_t phase) noexcept { event_phase_ = phase; }

 private:
  std::string type_;
  bool bubbles_{false};
  bool cancelable_{false};
  bool default_prevented_{false};
  bool propagation_stopped_{false};
  bool immediate_stopped_{false};
  std::shared_ptr<Object> target_;
  std::shared_ptr<Object> current_target_;
  std::uint8_t event_phase_{0};
};

/* Requires a linked runtime; dispatches through the display ancestor chain in capture, target and
   bubble order. The event object must outlive the call. */
ExecutionResult dispatch_event(Interpreter& interpreter, const std::shared_ptr<Object>& target,
                               const std::shared_ptr<EventObject>& event, std::size_t depth);
ExecutionResult dispatch_event(Interpreter& interpreter, const std::shared_ptr<Object>& target,
                               std::string type, bool bubbles, std::size_t depth);

/* Call after the child is attached; its subtree gets addedToStage when it is on a stage. */
ExecutionResult dispatch_added(Interpreter& interpreter,
                               const std::shared_ptr<DisplayObject>& child, std::size_t depth);
/* Call before the child is detached, so listeners still see its parent and stage. */
ExecutionResult dispatch_removed(Interpreter& interpreter,
                                 const std::shared_ptr<DisplayObject>& child, std::size_t depth);

/* Requires the listener to be a Function value; the signature is the AS3 one. */
bool add_event_listener(Object& target, std::string_view type, const Value& listener,
                        bool use_capture, std::int32_t priority);
bool remove_event_listener(Object& target, std::string_view type, const Value& listener,
                           bool use_capture);
bool has_event_listener(const Object& target, std::string_view type);

/* Requires a target; true when it or a display ancestor listens for the type. */
bool will_trigger(const std::shared_ptr<Object>& target, std::string_view type);

/* Requires a linked runtime. */
void sync_frame_listener(Runtime& runtime, const std::shared_ptr<Object>& target);

/* Requires a linked runtime; moves focus and dispatches focusOut then focusIn, bubbling. */
ExecutionResult set_display_focus(Interpreter& interpreter, DisplayObject& owner,
                                  const std::shared_ptr<DisplayObject>& target, std::size_t depth);

/* Requires a constructed Timer; the host clock drives it and never the wall clock. */
class TimerObject : public Object {
 public:
  TimerObject(Class* timer_class, double delay, std::int32_t repeat_count);

  double delay() const noexcept { return delay_; }
  void set_delay(double delay) noexcept { delay_ = delay; }
  std::int32_t repeat_count() const noexcept { return repeat_count_; }
  void set_repeat_count(std::int32_t count) noexcept { repeat_count_ = count; }
  std::int32_t current_count() const noexcept { return current_count_; }
  bool running() const noexcept { return running_; }
  void set_running(bool running) noexcept { running_ = running; }
  void set_current_count(std::int32_t count) noexcept { current_count_ = count; }
  double accumulator() const noexcept { return accumulator_; }
  void add_accumulator(double value) noexcept { accumulator_ += value; }
  void reset_accumulator() noexcept { accumulator_ = 0.0; }

 private:
  double delay_{0.0};
  std::int32_t repeat_count_{0};
  std::int32_t current_count_{0};
  bool running_{false};
  double accumulator_{0.0};
};

class HostClock {
 public:
  double time_ms() const noexcept { return time_ms_; }
  void advance(Interpreter& interpreter, double dt_ms);
  void register_timer(const std::shared_ptr<TimerObject>& timer);
  void unregister_timer(const TimerObject* timer);

 private:
  double time_ms_{0.0};
  std::vector<std::shared_ptr<TimerObject>> timers_;
};

/* Requires a linked runtime; resolves the flash.events class for a dispatch. */
std::shared_ptr<EventObject> make_event(Runtime& runtime, std::string_view type, bool bubbles,
                                        bool cancelable, std::string_view class_name = "Event");

/* Requires an EventObject; answers the AS3 Event property surface. */
bool read_event_property(Interpreter& interpreter, EventObject& event, const Multiname& multiname,
                         Value& out);

}  // namespace fo4::avm2
