#include <fo4/avm2/swf_runtime.hpp>

#include <fo4/avm2/display.hpp>
#include <fo4/avm2/events.hpp>
#include <fo4/avm2/hit_test.hpp>
#include <fo4/avm2/interpreter.hpp>
#include <fo4/avm2/name.hpp>
#include <fo4/avm2/object.hpp>
#include <fo4/avm2/runtime.hpp>
#include <fo4/avm2/text.hpp>

#include "native_internal.hpp"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <numbers>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace fo4::avm2 {

namespace {

thread_local std::string g_create_error;

std::uint64_t next_font_owner() {
  static std::atomic<std::uint64_t> last{0};
  return ++last;
}

void set_event_value(EventObject& event, const char* name, Value value) {
  Property& property = event.define_own_property(primary_key(builtin_name(name)));
  property.kind = PropertyKind::Value;
  property.value = std::move(value);
}

std::shared_ptr<DisplayObject> pointer_target(const std::shared_ptr<MovieClip>& root, double x,
                                              double y) {
  std::shared_ptr<DisplayObject> target = mouse_target_at(root, x, y);
  if (target != nullptr) {
    return target;
  }
  return root;
}

std::shared_ptr<EventObject> pointer_event(Interpreter& interpreter, const char* type,
                                           const DisplayObject& target, double x, double y) {
  const bool bubbles = std::string_view{type} != "rollOver" && std::string_view{type} != "rollOut";
  std::shared_ptr<EventObject> event =
      make_event(interpreter.runtime(), type, bubbles, false, "MouseEvent");
  if (event == nullptr) {
    return event;
  }
  double local_x = 0.0;
  double local_y = 0.0;
  stage_to_local(target, x, y, local_x, local_y);
  set_event_value(*event, "stageX", Value::number(x));
  set_event_value(*event, "stageY", Value::number(y));
  set_event_value(*event, "localX", Value::number(local_x));
  set_event_value(*event, "localY", Value::number(local_y));
  return event;
}

std::vector<std::shared_ptr<DisplayObject>> display_chain(
    const std::shared_ptr<DisplayObject>& node) {
  std::vector<std::shared_ptr<DisplayObject>> chain;
  for (DisplayObject* current = node.get(); current != nullptr; current = current->parent()) {
    chain.push_back(std::static_pointer_cast<DisplayObject>(current->shared_from_this()));
  }
  return chain;
}

void collect_frame_targets(const std::shared_ptr<DisplayObjectContainer>& container,
                           std::vector<std::shared_ptr<DisplayObject>>& out) {
  out.push_back(container);
  for (const auto& child : container->children()) {
    if (auto nested = std::dynamic_pointer_cast<DisplayObjectContainer>(child)) {
      collect_frame_targets(nested, out);
    } else {
      out.push_back(child);
    }
  }
}

ExecutionResult broadcast_frame_event(Interpreter& interpreter,
                                      const std::shared_ptr<DisplayObjectContainer>& stage,
                                      const char* type) {
  if (stage == nullptr) {
    return ExecutionResult{};
  }
  std::vector<std::shared_ptr<DisplayObject>> targets;
  collect_frame_targets(stage, targets);
  for (const auto& target : targets) {
    ExecutionResult result = dispatch_event(interpreter, target, type, false, 0);
    if (!result.ok()) {
      return result;
    }
  }
  return ExecutionResult{};
}

void dispatch_pointer(Interpreter& interpreter, const char* type,
                      const std::shared_ptr<DisplayObject>& node,
                      const std::shared_ptr<DisplayObject>& related, double x, double y) {
  std::shared_ptr<EventObject> event = pointer_event(interpreter, type, *node, x, y);
  if (event != nullptr) {
    set_event_value(*event, "relatedObject",
                    related == nullptr ? Value::null() : Value::object(related));
    dispatch_event(interpreter, node, event, 0);
  }
}

bool contains_node(const std::vector<std::shared_ptr<DisplayObject>>& nodes,
                   const std::shared_ptr<DisplayObject>& node) {
  return std::find(nodes.begin(), nodes.end(), node) != nodes.end();
}

void update_roll(Interpreter& interpreter, std::vector<std::weak_ptr<DisplayObject>>& rolled,
                 const std::shared_ptr<DisplayObject>& target, double x, double y) {
  std::shared_ptr<DisplayObject> previous;
  if (!rolled.empty()) {
    previous = rolled.front().lock();
  }
  std::vector<std::shared_ptr<DisplayObject>> before;
  for (const auto& weak : rolled) {
    if (std::shared_ptr<DisplayObject> node = weak.lock()) {
      before.push_back(node);
    }
  }
  const std::vector<std::shared_ptr<DisplayObject>> after = display_chain(target);
  rolled.assign(after.begin(), after.end());
  if (previous != nullptr && previous != target) {
    dispatch_pointer(interpreter, "mouseOut", previous, target, x, y);
  }
  for (const auto& node : before) {
    if (!contains_node(after, node)) {
      dispatch_pointer(interpreter, "rollOut", node, target, x, y);
    }
  }
  if (previous != target) {
    dispatch_pointer(interpreter, "mouseOver", target, previous, x, y);
  }
  for (auto node = after.rbegin(); node != after.rend(); ++node) {
    if (!contains_node(before, *node)) {
      dispatch_pointer(interpreter, "rollOver", *node, previous, x, y);
    }
  }
}

}  // namespace

const std::string& SwfRuntime::last_error() { return g_create_error; }

void SwfRuntime::release_fonts() {
  if (fonts_ != nullptr && data_ != nullptr) {
    fonts_->remove_owner(data_->font_owner);
  }
}

std::shared_ptr<SwfRuntime> SwfRuntime::create(Runtime& runtime, Interpreter& interpreter,
                                               const swf::SwfFile& file,
                                               const SwfRuntimeOptions& options,
                                               std::shared_ptr<ApplicationDomainObject> domain) {
  g_create_error.clear();
  const auto fail = [](std::string message) {
    g_create_error = std::move(message);
    return nullptr;
  };
  auto data = read_swf_movie_data(file, options);
  if (data == nullptr) {
    return fail("the movie data could not be read");
  }
  Class* stage_class = const_cast<Class*>(runtime.find_class("flash.display::Stage"));
  if (stage_class == nullptr) {
    return fail("the Stage class is missing");
  }
  data->font_owner = next_font_owner();
  for (FontResource font : data->fonts) {
    font.owner = data->font_owner;
    runtime.fonts().add(std::move(font));
  }
  auto stage_object = make_display_object(runtime, *stage_class);
  auto stage = std::dynamic_pointer_cast<DisplayObjectContainer>(stage_object);
  if (stage == nullptr) {
    return fail("the stage object is not a container");
  }
  stage->set_stage_width(data->stage_width);
  stage->set_stage_height(data->stage_height);
  stage->set_frame_rate(data->frame_rate);

  auto runtime_movie = std::shared_ptr<SwfRuntime>(new SwfRuntime());
  runtime_movie->runtime_ = &runtime;
  runtime_movie->fonts_ = runtime.font_registry();
  runtime_movie->data_ = std::move(data);
  /* The movie must run only its own scripts, never ones the host already ran or is running. */
  runtime_movie->next_script_ = runtime.scripts().size();
  for (const SoundResource& resource : runtime_movie->data_->sounds) {
    runtime.register_sound(resource);
  }
  const auto abc_blocks = swf::abc_blocks(file);
  for (const swf::SwfAbcBlock& block : abc_blocks) {
    runtime_movie->abc_files_.emplace_back();
    abc::AbcFile& abc_file = runtime_movie->abc_files_.back();
    if (abc::AbcFile::parse(reinterpret_cast<const std::uint8_t*>(block.payload.data()),
                            block.payload.size(), abc_file) != abc::AbcStatus::Ok) {
      return fail("an ABC block did not parse");
    }
    if (!runtime.add_file(abc_file, domain).ok()) {
      return fail("the runtime refused the movie ABC file");
    }
    if (!runtime.link_added_file(runtime.file_count() - 1).ok()) {
      return fail("the movie ABC file did not link");
    }
  }
  while (runtime_movie->next_script_ < runtime.scripts().size()) {
    const ExecutionResult script_result =
        interpreter.run_script(static_cast<std::uint32_t>(runtime_movie->next_script_));
    ++runtime_movie->next_script_;
    if (!script_result.ok()) {
      return fail("a movie script initializer failed: " + script_result.message);
    }
  }
  runtime_movie->stage_ = stage;
  runtime_movie->root_ =
      build_movie_root(runtime, runtime_movie->data_, &runtime_movie->root_class_);
  if (runtime_movie->root_ == nullptr) {
    return fail("the root clip could not be built");
  }
  // The document constructor reads stage and frame 1's children, so both exist before it runs.
  stage->add_child(runtime_movie->root_);
  const ExecutionResult placed =
      runtime_movie->root_->place_frame(interpreter, 1, options.start_playing);
  if (!placed.ok()) {
    return fail("frame 1 failed: " + placed.message);
  }
  if (runtime_movie->root_class_ != nullptr) {
    runtime_movie->root_class_->apply_slot_defaults(*runtime_movie->root_);
    const ExecutionResult prepare_result =
        interpreter.prepare_class(*runtime_movie->root_class_, 0);
    if (!prepare_result.ok()) {
      return fail("the document class initializer failed");
    }
    if (runtime_movie->root_class_->iinit != kNoMethod &&
        runtime_movie->root_class_->file != nullptr) {
      FunctionObject constructor(runtime_movie->root_class_->name, runtime_movie->root_class_->file,
                                 runtime_movie->root_class_->iinit);
      constructor.set_owner_class(runtime_movie->root_class_);
      const ExecutionResult constructor_result = interpreter.call_function(
          constructor, Value::object(runtime_movie->root_), {}, 0);
      if (!constructor_result.ok()) {
        return fail("the document class constructor failed: " + constructor_result.message);
      }
    }
  }
  if (options.bgs_host) {
    install_bgs_code_object(runtime, *runtime_movie->root_);
  }
  runtime_movie->playing_ = options.start_playing;
  runtime_movie->root_->set_playing(runtime_movie->playing_);
  const ExecutionResult first_frame = runtime_movie->root_->run_frame_actions(interpreter);
  if (!first_frame.ok()) {
    return fail("frame 1 failed: " + first_frame.message);
  }
  runtime_movie->started_ = true;
  return runtime_movie;
}

ExecutionResult SwfRuntime::call_root(Interpreter& interpreter, std::string_view name,
                                      const std::vector<Value>& args) {
  ExecutionResult failure;
  if (root_ == nullptr) {
    failure.status = InterpreterStatus::NotLinked;
    failure.message = "the movie has no root clip";
    return failure;
  }
  const PropertyLookup lookup = root_->lookup(builtin_name(name));
  if (!lookup.found()) {
    failure.status = InterpreterStatus::NoSuchProperty;
    failure.message = "the movie root has no member " + std::string{name};
    return failure;
  }
  if (lookup.kind != LookupKind::Method || lookup.trait == nullptr ||
      lookup.trait->function == nullptr) {
    failure.status = InterpreterStatus::CannotCall;
    failure.message = "the movie root member " + std::string{name} + " is not a method";
    return failure;
  }
  return interpreter.call_function(*lookup.trait->function, Value::object(root_), args, 0);
}

void SwfRuntime::advance(Interpreter& interpreter, double dt_seconds) {
  interpreter.advance_host_time(dt_seconds * 1000.0);
  if (!playing_ || root_ == nullptr || !started_) {
    return;
  }
  const double fps = data_ != nullptr && data_->frame_rate > 0.0 ? data_->frame_rate : 24.0;
  accumulator_ += dt_seconds * fps;
  while (accumulator_ >= 1.0) {
    const ExecutionResult entered = broadcast_frame_event(interpreter, stage_, "enterFrame");
    if (!entered.ok()) {
      playing_ = false;
      break;
    }
    const ExecutionResult result = root_->advance_timeline(interpreter);
    if (!result.ok()) {
      playing_ = false;
      break;
    }
    if (runtime_ != nullptr) {
      const std::vector<std::shared_ptr<MovieClip>> clips = runtime_->movie_clips();
      for (const auto& clip : clips) {
        if (clip != nullptr) {
          const ExecutionResult clip_result = clip->advance_timeline(interpreter);
          if (!clip_result.ok()) {
            playing_ = false;
            break;
          }
        }
      }
    }
    const ExecutionResult exited = broadcast_frame_event(interpreter, stage_, "exitFrame");
    if (!exited.ok()) {
      playing_ = false;
      break;
    }
    accumulator_ -= 1.0;
  }
}

void SwfRuntime::key_event(Interpreter& interpreter, std::uint32_t key_code, bool down) {
  if (root_ == nullptr) {
    return;
  }
  if (runtime_ != nullptr) {
    runtime_->set_key_pressed(key_code, down);
  }
  std::shared_ptr<EventObject> event =
      make_event(interpreter.runtime(), down ? "keyDown" : "keyUp", true, false, "KeyboardEvent");
  if (event == nullptr) {
    return;
  }
  Property& key_property = event->define_own_property(primary_key(builtin_name("keyCode")));
  key_property.kind = PropertyKind::Value;
  key_property.value = Value::uint32(key_code);
  Property& char_property = event->define_own_property(primary_key(builtin_name("charCode")));
  char_property.kind = PropertyKind::Value;
  char_property.value = Value::uint32(0);
  std::shared_ptr<DisplayObject> target;
  if (stage_ != nullptr) {
    target = stage_->focus();
  }
  if (target == nullptr) {
    target = root_;
  }
  dispatch_event(interpreter, target, event, 0);
}

void SwfRuntime::mouse_event(Interpreter& interpreter, double x, double y, std::int32_t button,
                             bool down) {
  if (root_ == nullptr) {
    return;
  }
  root_->set_mouse_position(x, y);
  if (stage_ != nullptr) stage_->set_mouse_position(x, y);
  const std::shared_ptr<DisplayObject> target = pointer_target(root_, x, y);
  update_roll(interpreter, roll_chain_, target, x, y);
  std::shared_ptr<EventObject> event =
      pointer_event(interpreter, down ? "mouseDown" : "mouseUp", *target, x, y);
  if (event == nullptr) {
    return;
  }
  set_event_value(*event, "button", Value::int32(button));
  if (down) {
    mouse_down_ = target;
    if (stage_ != nullptr && (target->is_text_field() || target->tab_enabled())) {
      set_display_focus(interpreter, *stage_, target, 0);
    }
  }
  dispatch_event(interpreter, target, event, 0);
  if (down) {
    return;
  }
  const bool clicked = mouse_down_.lock() == target;
  mouse_down_.reset();
  if (!clicked) {
    return;
  }
  std::shared_ptr<EventObject> click = pointer_event(interpreter, "click", *target, x, y);
  if (click != nullptr) {
    dispatch_event(interpreter, target, click, 0);
  }
}

SoundEventQueue& SwfRuntime::sound_events() {
  return runtime_ == nullptr ? sounds_fallback_ : runtime_->sound_events();
}

FrameSnapshot SwfRuntime::take_snapshot() {
  FrameSnapshot snapshot = root_ == nullptr ? FrameSnapshot{} : build_frame_snapshot(*root_);
  if (runtime_ != nullptr) {
    runtime_->sound_events().append_to(snapshot.sounds);
  }
  return snapshot;
}

FrameSnapshot SwfRuntime::snapshot() const {
  return root_ == nullptr ? FrameSnapshot{} : build_frame_snapshot(*root_);
}

void SwfRuntime::mouse_move(Interpreter& interpreter, double x, double y) {
  if (root_ == nullptr) {
    return;
  }
  root_->set_mouse_position(x, y);
  if (stage_ != nullptr) stage_->set_mouse_position(x, y);
  const std::shared_ptr<DisplayObject> target = pointer_target(root_, x, y);
  update_roll(interpreter, roll_chain_, target, x, y);
  std::shared_ptr<EventObject> event = pointer_event(interpreter, "mouseMove", *target, x, y);
  if (event != nullptr) {
    dispatch_event(interpreter, target, event, 0);
  }
}

void SwfRuntime::mouse_wheel(Interpreter& interpreter, double x, double y, std::int32_t delta) {
  if (root_ == nullptr) {
    return;
  }
  const std::shared_ptr<DisplayObject> target = pointer_target(root_, x, y);
  std::shared_ptr<EventObject> event = pointer_event(interpreter, "mouseWheel", *target, x, y);
  if (event == nullptr) {
    return;
  }
  set_event_value(*event, "delta", Value::int32(delta));
  dispatch_event(interpreter, target, event, 0);
}

void SwfRuntime::char_event(Interpreter& interpreter, std::uint32_t char_code) {
  if (root_ == nullptr) {
    return;
  }
  std::shared_ptr<EventObject> event =
      make_event(interpreter.runtime(), "textInput", true, false, "TextEvent");
  if (event == nullptr) {
    return;
  }
  Property& char_property = event->define_own_property(primary_key(builtin_name("charCode")));
  char_property.kind = PropertyKind::Value;
  char_property.value = Value::uint32(char_code);
  dispatch_event(interpreter, root_, event, 0);
}

void SwfRuntime::set_playing(bool playing) {
  playing_ = playing;
  if (root_ != nullptr) {
    root_->set_playing(playing);
  }
}

bool SwfRuntime::playing() const { return playing_; }

}  // namespace fo4::avm2
