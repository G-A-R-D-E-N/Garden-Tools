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
#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
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
    chain.push_back(current->shared_from_this());
  }
  return chain;
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

std::shared_ptr<TimelineDefinition> timeline_from_rows(const SwfTimelineRows& rows) {
  auto timeline = std::make_shared<TimelineDefinition>();
  timeline->frame_count = std::max<std::uint32_t>(1, rows.frame_count);
  timeline->labels = rows.labels;
  return timeline;
}

Class* resolve_character_class(Runtime& runtime, const SwfMovieData& data,
                               std::uint16_t character_id) {
  const auto symbol = data.symbol_classes.find(character_id);
  if (symbol != data.symbol_classes.end() && !symbol->second.empty()) {
    if (Class* klass = const_cast<Class*>(runtime.find_class(symbol->second))) {
      return klass;
    }
  }
  if (data.sprites.find(character_id) != data.sprites.end()) {
    return const_cast<Class*>(runtime.find_class("flash.display::MovieClip"));
  }
  if (data.text_fields.find(character_id) != data.text_fields.end()) {
    return const_cast<Class*>(runtime.find_class("flash.text::TextField"));
  }
  if (data.static_texts.find(character_id) != data.static_texts.end()) {
    return const_cast<Class*>(runtime.find_class("flash.text::StaticText"));
  }
  if (Class* shape = const_cast<Class*>(runtime.find_class("flash.display::Shape"))) {
    return shape;
  }
  return const_cast<Class*>(runtime.find_class("flash.display::MovieClip"));
}

void apply_character_definition(DisplayObject& object, const SwfMovieData& data,
                                std::uint16_t character_id) {
  const auto bounds = data.character_bounds.find(character_id);
  if (bounds != data.character_bounds.end()) {
    object.set_origin(bounds->second.origin_x, bounds->second.origin_y);
    object.set_width(bounds->second.width);
    object.set_height(bounds->second.height);
  }
  const auto static_text = data.static_texts.find(character_id);
  if (static_text != data.static_texts.end() && object.is_static_text()) {
    auto& static_object = static_cast<StaticTextObject&>(object);
    static_object.set_font_owner(data.font_owner);
    static_object.set_records(static_text->second);
  }
  const auto text = data.text_fields.find(character_id);
  if (text == data.text_fields.end() || !object.is_text_field()) {
    return;
  }
  auto& field = static_cast<TextFieldObject&>(object);
  field.set_font_owner(data.font_owner);
  field.set_format(text->second.format);
  field.set_word_wrap(text->second.word_wrap);
  field.set_multiline(text->second.multiline);
  field.set_auto_size(text->second.auto_size);
  if (text->second.html) {
    field.set_html_text(text->second.text);
  } else {
    field.set_text(text->second.text);
  }
}

class SwfTimelineClip : public MovieClip {
 public:
  SwfTimelineClip(Class* display_class, std::shared_ptr<SwfMovieData> data,
                  const SwfTimelineRows* rows, std::uint16_t character_id)
      : MovieClip(display_class),
        data_(std::move(data)),
        rows_(rows),
        character_id_(character_id) {
    if (display_class != nullptr && display_class->prototype_object != nullptr) {
      set_prototype(display_class->prototype_object);
    }
    if (rows_ != nullptr) {
      set_timeline(timeline_from_rows(*rows_));
    }
    set_dynamic(true);
  }

  std::uint16_t character_id() const noexcept { return character_id_; }

 protected:
  ExecutionResult apply_frame_state(Interpreter& interpreter) override {
    if (rows_ == nullptr) {
      return ExecutionResult{};
    }
    const std::uint32_t frame = current_frame();
    if (frame == 0 || frame > rows_->frames.size()) {
      return ExecutionResult{};
    }
    Runtime& runtime = interpreter.runtime();
    std::vector<std::uint16_t> just_created;
    for (const SwfPlacementRow& row : rows_->frames[frame - 1]) {
      if (row.kind == SwfPlacementRow::Kind::Remove) {
        const auto found = objects_.find(row.depth);
        if (found != objects_.end()) {
          remove_child(found->second);
          sprite_clips_.erase(row.depth);
          objects_.erase(found);
        }
        continue;
      }
      Class* klass = resolve_character_class(runtime, *data_, row.character_id);
      if (klass == nullptr) {
        continue;
      }
      std::shared_ptr<DisplayObject> object;
      const auto found = objects_.find(row.depth);
      if (found != objects_.end() && found->second->display_class() == klass &&
          found->second->character_id() == row.character_id) {
        object = found->second;
      } else {
        if (found != objects_.end()) {
          remove_child(found->second);
          sprite_clips_.erase(row.depth);
          objects_.erase(found);
        }
        const auto sprite = data_->sprites.find(row.character_id);
        if (sprite != data_->sprites.end()) {
          auto clip = std::make_shared<SwfTimelineClip>(klass, data_, &sprite->second,
                                                        row.character_id);
          object = clip;
          sprite_clips_[row.depth] = clip.get();
        } else {
          object = make_display_object(runtime, *klass);
        }
        if (object == nullptr) {
          continue;
        }
        apply_character_definition(*object, *data_, row.character_id);
        add_child(object);
        objects_[row.depth] = object;
        just_created.push_back(row.depth);
      }
      object->set_x(row.translate_x);
      object->set_y(row.translate_y);
      object->set_scale_x(row.scale_x);
      object->set_scale_y(row.scale_y);
      object->set_character_id(row.character_id);
      if (row.has_color_transform) {
        object->set_color_multiply(row.color_multiply);
        object->set_color_add(row.color_add);
      }
      object->set_clip_depth(row.has_clip_depth ? static_cast<std::int32_t>(row.clip_depth) : 0);
      if (!row.name.empty()) {
        object->set_name(row.name);
      }
    }
    order_children_by_depth();
    for (const auto& [depth, clip] : sprite_clips_) {
      if (clip == nullptr) {
        continue;
      }
      const bool created = std::find(just_created.begin(), just_created.end(), depth) !=
                           just_created.end();
      ExecutionResult result =
          created ? clip->goto_frame(interpreter, 1, true) : clip->advance_frame(interpreter);
      if (!result.ok()) {
        return result;
      }
    }
    return ExecutionResult{};
  }

 private:
  void order_children_by_depth() {
    std::vector<std::shared_ptr<DisplayObject>> expected;
    expected.reserve(objects_.size());
    for (const auto& entry : objects_) {
      expected.push_back(entry.second);
    }
    for (std::size_t index = 0; index < expected.size(); ++index) {
      if (index >= children().size() || children()[index] != expected[index]) {
        const auto found = std::find(children().begin(), children().end(), expected[index]);
        if (found != children().end()) {
          set_child_index(expected[index].get(), index);
        }
      }
    }
  }

  std::shared_ptr<SwfMovieData> data_;
  const SwfTimelineRows* rows_{nullptr};
  std::uint16_t character_id_{0};
  std::map<std::uint16_t, std::shared_ptr<DisplayObject>> objects_;
  std::map<std::uint16_t, SwfTimelineClip*> sprite_clips_;
};

}  // namespace

std::shared_ptr<MovieClip> build_movie_root(Runtime& runtime,
                                            const std::shared_ptr<SwfMovieData>& data,
                                            Class** out_root_class) {
  if (data == nullptr) {
    return nullptr;
  }
  Class* root_class = const_cast<Class*>(runtime.find_class("flash.display::MovieClip"));
  if (!data->root_class_name.empty()) {
    Class* document_class = const_cast<Class*>(runtime.find_class(data->root_class_name));
    Class* movie_clip_class = const_cast<Class*>(runtime.find_class("flash.display::MovieClip"));
    if (document_class != nullptr && movie_clip_class != nullptr &&
        document_class->is_subclass_of(movie_clip_class)) {
      root_class = document_class;
    }
  }
  if (root_class == nullptr) {
    return nullptr;
  }
  if (out_root_class != nullptr) {
    out_root_class[0] = root_class;
  }
  auto root = std::make_shared<SwfTimelineClip>(root_class, data, &data->root, std::uint16_t{0});
  root->set_playing(true);
  return root;
}

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
    while (runtime_movie->next_script_ < runtime.scripts().size()) {
      const ExecutionResult script_result =
          interpreter.run_script(static_cast<std::uint32_t>(runtime_movie->next_script_));
      ++runtime_movie->next_script_;
      if (!script_result.ok()) {
        return fail("a movie script initializer failed: " + script_result.message);
      }
    }
  }
  runtime_movie->stage_ = stage;
  runtime_movie->root_ =
      build_movie_root(runtime, runtime_movie->data_, &runtime_movie->root_class_);
  if (runtime_movie->root_ == nullptr) {
    return fail("the root clip could not be built");
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
  stage->add_child(runtime_movie->root_);
  const ExecutionResult first_frame =
      runtime_movie->root_->goto_frame(interpreter, 1, runtime_movie->playing_);
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
    const ExecutionResult result = root_->advance_frame(interpreter);
    if (!result.ok()) {
      playing_ = false;
      break;
    }
    if (runtime_ != nullptr) {
      const std::vector<std::shared_ptr<MovieClip>> clips = runtime_->movie_clips();
      for (const auto& clip : clips) {
        if (clip != nullptr) {
          const ExecutionResult clip_result = clip->advance_frame(interpreter);
          if (!clip_result.ok()) {
            playing_ = false;
            break;
          }
        }
      }
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
