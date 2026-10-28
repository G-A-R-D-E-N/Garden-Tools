#include <fo4/avm2/display.hpp>

#include <fo4/avm2/coercion.hpp>
#include <fo4/avm2/events.hpp>
#include <fo4/avm2/interpreter.hpp>
#include <fo4/avm2/name.hpp>
#include <fo4/avm2/net.hpp>
#include <fo4/avm2/runtime.hpp>
#include <fo4/avm2/text.hpp>

#include "interpreter_internal.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace fo4::avm2 {

namespace {

DisplayObject* top_ancestor(DisplayObject* object) {
  DisplayObject* current = object;
  while (current != nullptr && current->parent() != nullptr) {
    current = current->parent();
  }
  return current;
}

Multiname multiname_for(std::string_view name) {
  Multiname multiname;
  multiname.kind = MultinameKind::QName;
  multiname.name = std::string{name};
  multiname.namespaces.push_back(Namespace{});
  return multiname;
}

const char* auto_size_name(TextFieldAutoSize value) {
  static const char* const names[] = {"none", "left", "center", "right"};
  return names[static_cast<std::uint8_t>(value)];
}

TextFieldAutoSize auto_size_from_value(const Value& value) {
  if (!value.is_string()) {
    const std::int32_t number = to_int32(value);
    return number >= 0 && number < 4 ? static_cast<TextFieldAutoSize>(number)
                                     : TextFieldAutoSize::none;
  }
  for (std::uint8_t index = 0; index < 4; ++index) {
    if (value.as_string() == auto_size_name(static_cast<TextFieldAutoSize>(index))) {
      return static_cast<TextFieldAutoSize>(index);
    }
  }
  return TextFieldAutoSize::none;
}

constexpr std::uint8_t kBlendModeCount = 15;

const char* blend_mode_name(std::uint8_t value) {
  static const char* const names[kBlendModeCount] = {
      "normal", "normal", "layer", "multiply", "screen", "lighten", "darken", "difference",
      "add", "subtract", "invert", "alpha", "erase", "overlay", "hardlight"};
  return names[value < kBlendModeCount ? value : 0];
}

std::uint8_t blend_mode_from_value(const Value& value) {
  if (value.is_string()) {
    for (std::uint8_t index = 0; index < kBlendModeCount; ++index) {
      if (value.as_string() == blend_mode_name(index)) {
        return index;
      }
    }
  }
  return 0;
}

}  // namespace

std::uint32_t TimelineDefinition::frame_for_label(const std::string& name) const {
  for (const TimelineFrameLabel& label : labels) {
    if (label.name == name) {
      return label.frame;
    }
  }
  return 1;
}

DisplayObject::DisplayObject(Class* display_class)
    : Object(ObjectKind::Display), display_class_(display_class) {
  if (display_class != nullptr) {
    set_instance_class(display_class);
  }
  set_dynamic(true);
}

DisplayObject* DisplayObject::root() const noexcept {
  DisplayObject* top = top_ancestor(const_cast<DisplayObject*>(this));
  return top;
}

DisplayObject* DisplayObject::stage() const noexcept {
  DisplayObject* top = root();
  if (top == nullptr || top->display_class() == nullptr) {
    return nullptr;
  }
  const Class* klass = top->display_class();
  return klass->name.find("::Stage") != std::string::npos ? top : nullptr;
}

DisplayObjectContainer::DisplayObjectContainer(Class* display_class)
    : DisplayObject(display_class) {}

bool DisplayObjectContainer::add_child(const std::shared_ptr<DisplayObject>& child) {
  return add_child_at(child, children_.size());
}

bool DisplayObjectContainer::add_child_at(const std::shared_ptr<DisplayObject>& child,
                                          std::size_t index) {
  if (child == nullptr || child.get() == this) {
    return false;
  }
  for (DisplayObject* ancestor = parent(); ancestor != nullptr; ancestor = ancestor->parent()) {
    if (ancestor == child.get()) {
      return false;
    }
  }
  if (child->parent() != nullptr && child->parent()->is_container()) {
    static_cast<DisplayObjectContainer*>(child->parent())->remove_child(child);
  }
  index = std::min(index, children_.size());
  children_.insert(children_.begin() + static_cast<std::ptrdiff_t>(index), child);
  child->set_parent(this);
  child->set_assigned(true);
  return true;
}

bool DisplayObjectContainer::remove_child(const std::shared_ptr<DisplayObject>& child) {
  if (child == nullptr) {
    return false;
  }
  const auto found = std::find(children_.begin(), children_.end(), child);
  if (found == children_.end()) {
    return false;
  }
  children_.erase(found);
  child->set_parent(nullptr);
  return true;
}

std::shared_ptr<DisplayObject> DisplayObjectContainer::remove_child_at(std::size_t index) {
  if (index >= children_.size()) {
    return nullptr;
  }
  std::shared_ptr<DisplayObject> child = children_[index];
  children_.erase(children_.begin() + static_cast<std::ptrdiff_t>(index));
  if (child != nullptr) {
    child->set_parent(nullptr);
  }
  return child;
}

std::shared_ptr<DisplayObject> DisplayObjectContainer::child_at(std::size_t index) const {
  return index < children_.size() ? children_[index] : nullptr;
}

std::shared_ptr<DisplayObject> DisplayObjectContainer::child_by_name(
    const std::string& name) const {
  for (const auto& child : children_) {
    if (child != nullptr && child->name() == name) {
      return child;
    }
  }
  return nullptr;
}

bool DisplayObjectContainer::contains(const DisplayObject* child) const {
  if (child == nullptr) {
    return false;
  }
  for (const auto* current = child->parent(); current != nullptr; current = current->parent()) {
    if (current == this) {
      return true;
    }
  }
  return false;
}

std::size_t DisplayObjectContainer::child_index(const DisplayObject* child) const {
  for (std::size_t index = 0; index < children_.size(); ++index) {
    if (children_[index].get() == child) {
      return index;
    }
  }
  return children_.size();
}

bool DisplayObjectContainer::set_child_index(const DisplayObject* child, std::size_t index) {
  const std::size_t current = child_index(child);
  if (current >= children_.size() || index >= children_.size() || current == index) {
    return false;
  }
  std::shared_ptr<DisplayObject> moved = children_[current];
  children_.erase(children_.begin() + static_cast<std::ptrdiff_t>(current));
  children_.insert(children_.begin() + static_cast<std::ptrdiff_t>(index), moved);
  return true;
}

bool DisplayObjectContainer::swap_children(const DisplayObject* first,
                                           const DisplayObject* second) {
  const std::size_t first_index = child_index(first);
  const std::size_t second_index = child_index(second);
  if (first_index >= children_.size() || second_index >= children_.size() ||
      first_index == second_index) {
    return false;
  }
  std::swap(children_[first_index], children_[second_index]);
  return true;
}

MovieClip::MovieClip(Class* display_class) : DisplayObjectContainer(display_class) {}

std::uint32_t MovieClip::total_frames() const noexcept {
  return timeline_ == nullptr ? 1u : std::max(1u, timeline_->frame_count);
}

ExecutionResult MovieClip::goto_frame(Interpreter& interpreter, std::uint32_t frame, bool play) {
  const std::uint32_t total = total_frames();
  current_frame_ = frame == 0 ? 1u : std::min(frame, total);
  playing_ = play;
  return run_frame_lifecycle(interpreter);
}

ExecutionResult MovieClip::advance_frame(Interpreter& interpreter) {
  if (!playing_) {
    return ExecutionResult{};
  }
  const std::uint32_t total = total_frames();
  current_frame_ = current_frame_ >= total ? 1u : current_frame_ + 1;
  return run_frame_lifecycle(interpreter);
}

ExecutionResult MovieClip::run_frame_lifecycle(Interpreter& interpreter) {
  ExecutionResult placement = apply_frame_state(interpreter);
  if (!placement.ok()) {
    return placement;
  }
  ExecutionResult script = run_frame_script(interpreter);
  if (!script.ok()) {
    return script;
  }
  const std::shared_ptr<Object> self = std::static_pointer_cast<Object>(shared_from_this());
  ExecutionResult enter = dispatch_event(interpreter, self, "enterFrame", false, 0);
  if (!enter.ok()) {
    return enter;
  }
  ExecutionResult exit = dispatch_event(interpreter, self, "exitFrame", false, 0);
  if (!exit.ok()) {
    return exit;
  }
  return script;
}

ExecutionResult MovieClip::apply_frame_state(Interpreter& interpreter) {
  (void)interpreter;
  return ExecutionResult{};
}

ExecutionResult MovieClip::run_frame_script(Interpreter& interpreter) {
  if (display_class() != nullptr) {
    const std::string frame_name = "frame" + std::to_string(current_frame_);
    const Trait* frame_trait =
        display_class()->resolve_instance_trait(multiname_for(frame_name));
    if (frame_trait != nullptr && frame_trait->function != nullptr) {
      return interpreter.call_function(*frame_trait->function, detail::borrowed_object(*this), {},
                                       0);
    }
  }
  if (timeline_ == nullptr || timeline_->frame_script_file == nullptr) {
    return ExecutionResult{};
  }
  const auto found = timeline_->frame_scripts.find(current_frame_);
  if (found == timeline_->frame_scripts.end()) {
    return ExecutionResult{};
  }
  const abc::AbcFile& file = *timeline_->frame_script_file;
  if (found->second >= file.methods.size()) {
    ExecutionResult failure;
    failure.status = InterpreterStatus::MethodIndexOutOfRange;
    failure.message = "frame script method index is out of range";
    return failure;
  }
  FunctionObject function(file.method_name(found->second), &file, found->second);
  if (display_class() != nullptr) {
    function.set_owner_class(display_class());
  }
  return interpreter.call_function(function, detail::borrowed_object(*this), {}, 0);
}

Class* find_display_class(const Runtime& runtime, std::string_view rendered_name) {
  return const_cast<Class*>(runtime.find_class(rendered_name));
}

std::shared_ptr<DisplayObject> make_display_object(Runtime& runtime, Class& klass) {
  Class* container_class = find_display_class(runtime, "flash.display::DisplayObjectContainer");
  Class* movie_clip = find_display_class(runtime, "flash.display::MovieClip");
  std::shared_ptr<DisplayObject> object;
  Class* loader_class = find_display_class(runtime, "flash.display::Loader");
  Class* text_field_class = find_display_class(runtime, "flash.text::TextField");
  Class* static_text_class = find_display_class(runtime, "flash.text::StaticText");
  if (loader_class != nullptr && klass.is_subclass_of(loader_class)) {
    object = std::make_shared<LoaderObject>(&klass);
  } else if (text_field_class != nullptr && klass.is_subclass_of(text_field_class)) {
    object = std::make_shared<TextFieldObject>(&klass, &runtime.fonts());
  } else if (static_text_class != nullptr && klass.is_subclass_of(static_text_class)) {
    object = std::make_shared<StaticTextObject>(&klass, &runtime.fonts());
  } else if (movie_clip != nullptr && klass.is_subclass_of(movie_clip)) {
    object = std::make_shared<MovieClip>(&klass);
  } else if (container_class != nullptr && klass.is_subclass_of(container_class)) {
    object = std::make_shared<DisplayObjectContainer>(&klass);
  } else {
    object = std::make_shared<DisplayObject>(&klass);
  }
  if (klass.prototype_object != nullptr) {
    object->set_prototype(klass.prototype_object);
  }
  object->set_display_class(&klass);
  return object;
}

bool read_display_property(Interpreter& interpreter, DisplayObject& object,
                           const Multiname& multiname, Value& out) {
  if (!multiname.has_fixed_name()) {
    return false;
  }
  const std::string& name = multiname.name;
  if (name == "x") {
    out = Value::number(object.x());
    return true;
  }
  if (name == "y") {
    out = Value::number(object.y());
    return true;
  }
  if (name == "scaleX") {
    out = Value::number(object.scale_x());
    return true;
  }
  if (name == "scaleY") {
    out = Value::number(object.scale_y());
    return true;
  }
  if (name == "rotation") {
    out = Value::number(object.rotation());
    return true;
  }
  if (name == "alpha") {
    out = Value::number(object.alpha());
    return true;
  }
  if (name == "width") {
    out = Value::number(object.width());
    return true;
  }
  if (name == "height") {
    out = Value::number(object.height());
    return true;
  }
  if (name == "visible") {
    out = Value::boolean(object.visible());
    return true;
  }
  if (name == "blendMode") {
    out = Value::string(blend_mode_name(object.blend_mode()));
    return true;
  }
  if (name == "filters") {
    auto array = std::make_shared<ArrayObject>();
    array->elements() = object.filters();
    out = Value::object(std::move(array));
    return true;
  }
  if (name == "bitmapData") {
    out = object.bitmap_data();
    return true;
  }
  if (name == "stageFocusRect") {
    out = Value::boolean(object.stage_focus_rect());
    return true;
  }
  if (name == "focus") {
    const std::shared_ptr<DisplayObject> focus = object.focus();
    out = focus == nullptr ? Value::null() : Value::object(focus);
    return true;
  }
  if (name == "name") {
    out = Value::string(object.name());
    return true;
  }
  if (name == "parent") {
    DisplayObject* parent = object.parent();
    out = parent == nullptr ? Value::null() : Value::object(parent->shared_from_this());
    return true;
  }
  if (name == "root") {
    DisplayObject* root = object.root();
    out = root == nullptr ? Value::null() : Value::object(root->shared_from_this());
    return true;
  }
  if (name == "stage") {
    DisplayObject* stage = object.stage();
    out = stage == nullptr ? Value::null() : Value::object(stage->shared_from_this());
    return true;
  }
  if (name == "mouseX" || name == "mouseY") {
    out = Value::number(name == "mouseX" ? object.mouse_x() : object.mouse_y());
    return true;
  }
  if (name == "mouseEnabled") {
    out = Value::boolean(object.mouse_enabled());
    return true;
  }
  if (name == "tabEnabled") {
    out = Value::boolean(object.tab_enabled());
    return true;
  }
  if (name == "tabChildren") {
    out = Value::boolean(object.tab_children());
    return true;
  }
  if (name == "mouseChildren") {
    out = Value::boolean(object.mouse_children());
    return true;
  }
  if (name == "buttonMode") {
    out = Value::boolean(object.button_mode());
    return true;
  }
  if (name == "numChildren" && object.is_container()) {
    out = Value::number(
        static_cast<double>(static_cast<DisplayObjectContainer&>(object).children().size()));
    return true;
  }
  if (object.is_static_text() && name == "text") {
    out = Value::string(static_cast<StaticTextObject&>(object).text());
    return true;
  }
  if (object.is_text_field()) {
    auto& text_field = static_cast<TextFieldObject&>(object);
    if (name == "text") {
      out = Value::string(text_field.text());
      return true;
    }
    if (name == "htmlText") {
      out = Value::string(text_field.html_text());
      return true;
    }
    if (name == "textColor") {
      out = Value::uint32(text_field.text_color());
      return true;
    }
    if (name == "textWidth") {
      out = Value::number(text_field.text_width());
      return true;
    }
    if (name == "textHeight") {
      out = Value::number(text_field.text_height());
      return true;
    }
    if (name == "numLines") {
      out = Value::number(static_cast<double>(text_field.line_count()));
      return true;
    }
    if (name == "wordWrap") {
      out = Value::boolean(text_field.word_wrap());
      return true;
    }
    if (name == "multiline") {
      out = Value::boolean(text_field.multiline());
      return true;
    }
    if (name == "autoSize") {
      out = Value::string(auto_size_name(text_field.auto_size()));
      return true;
    }
    if (name == "defaultTextFormat") {
      out = make_text_format_instance(interpreter, text_field.format());
      return true;
    }
    if (name == "length") {
      out = Value::number(static_cast<double>(utf16_from_utf8(text_field.text()).size()));
      return true;
    }
  }
  if ((name == "stageWidth" || name == "stageHeight" || name == "frameRate") &&
      object.stage() == &object) {
    out = Value::number(name == "stageWidth"     ? object.stage_width()
                        : name == "stageHeight"  ? object.stage_height()
                                                 : object.frame_rate());
    return true;
  }
  if (name == "currentFrame" && object.is_movie_clip()) {
    out = Value::number(
        static_cast<double>(static_cast<MovieClip&>(object).current_frame()));
    return true;
  }
  if (name == "totalFrames" && object.is_movie_clip()) {
    out = Value::number(static_cast<double>(static_cast<MovieClip&>(object).total_frames()));
    return true;
  }
  if (name == "framesLoaded" && object.is_movie_clip()) {
    out = Value::number(static_cast<double>(static_cast<MovieClip&>(object).total_frames()));
    return true;
  }
  if (name == "currentLabel" && object.is_movie_clip()) {
    auto& clip = static_cast<MovieClip&>(object);
    std::string label;
    if (clip.timeline() != nullptr) {
      for (const TimelineFrameLabel& candidate : clip.timeline()->labels) {
        if (candidate.frame == clip.current_frame()) {
          label = candidate.name;
        }
      }
    }
    out = Value::string(label);
    return true;
  }
  if (name == "currentLabels" && object.is_movie_clip()) {
    auto& clip = static_cast<MovieClip&>(object);
    std::vector<Value> labels;
    if (clip.timeline() != nullptr) {
      const Class* frame_label_class =
          interpreter.runtime().find_class("flash.display::FrameLabel");
      for (const TimelineFrameLabel& candidate : clip.timeline()->labels) {
        std::shared_ptr<Object> label;
        if (frame_label_class != nullptr) {
          label = interpreter.make_instance(*const_cast<Class*>(frame_label_class));
        } else {
          label = std::make_shared<Object>();
          interpreter.prepare_plain_object(*label);
        }
        Property& name_property =
            label->define_own_property(primary_key(multiname_for("name")));
        name_property.kind = PropertyKind::Value;
        name_property.value = Value::string(candidate.name);
        Property& frame_property =
            label->define_own_property(primary_key(multiname_for("frame")));
        frame_property.kind = PropertyKind::Value;
        frame_property.value = Value::number(static_cast<double>(candidate.frame));
        labels.push_back(Value::object(std::move(label)));
      }
    }
    out = Value::object(interpreter.make_array(std::move(labels)));
    return true;
  }
  return false;
}

bool write_display_property(DisplayObject& object, const Multiname& multiname,
                            const Value& value) {
  if (!multiname.has_fixed_name()) {
    return false;
  }
  const std::string& name = multiname.name;
  if (name == "x") {
    object.set_x(to_number(value));
    return true;
  }
  if (name == "y") {
    object.set_y(to_number(value));
    return true;
  }
  if (name == "scaleX") {
    object.set_scale_x(to_number(value));
    return true;
  }
  if (name == "scaleY") {
    object.set_scale_y(to_number(value));
    return true;
  }
  if (name == "rotation") {
    object.set_rotation(to_number(value));
    return true;
  }
  if (name == "alpha") {
    object.set_alpha(to_number(value));
    return true;
  }
  if (name == "width") {
    object.set_width(to_number(value));
    if (object.is_text_field()) {
      static_cast<TextFieldObject&>(object).relayout();
    }
    return true;
  }
  if (name == "height") {
    object.set_height(to_number(value));
    return true;
  }
  if (name == "visible") {
    object.set_visible(to_boolean(value));
    return true;
  }
  if (name == "blendMode") {
    object.set_blend_mode(blend_mode_from_value(value));
    return true;
  }
  if (name == "filters") {
    object.filters().clear();
    if (value.is_object() && value.as_object()->kind() == ObjectKind::Array) {
      const auto& array = static_cast<const ArrayObject&>(*value.as_object());
      object.filters() = array.elements();
    }
    return true;
  }
  if (name == "bitmapData") {
    object.set_bitmap_data(value);
    return true;
  }
  if (name == "stageFocusRect") {
    object.set_stage_focus_rect(to_boolean(value));
    return true;
  }
  if (name == "name") {
    object.set_name(to_string(value));
    return true;
  }
  if (object.is_text_field()) {
    auto& text_field = static_cast<TextFieldObject&>(object);
    if (name == "text") {
      text_field.set_text(to_string(value));
      return true;
    }
    if (name == "htmlText") {
      text_field.set_html_text(to_string(value));
      return true;
    }
    if (name == "textColor") {
      text_field.set_text_color(static_cast<std::uint32_t>(to_number(value)));
      return true;
    }
    if (name == "wordWrap") {
      text_field.set_word_wrap(to_boolean(value));
      return true;
    }
    if (name == "multiline") {
      text_field.set_multiline(to_boolean(value));
      return true;
    }
    if (name == "autoSize") {
      text_field.set_auto_size(auto_size_from_value(value));
      return true;
    }
    if (name == "defaultTextFormat") {
      text_field.set_format(read_text_format_instance(value));
      return true;
    }
    if (name == "textWidth" || name == "textHeight" || name == "numLines") {
      return true;
    }
  }
  if (name == "mouseEnabled") {
    object.set_mouse_enabled(to_boolean(value));
    return true;
  }
  if (name == "tabEnabled") {
    object.set_tab_enabled(to_boolean(value));
    return true;
  }
  if (name == "tabChildren") {
    object.set_tab_children(to_boolean(value));
    return true;
  }
  if (name == "mouseChildren") {
    object.set_mouse_children(to_boolean(value));
    return true;
  }
  if (name == "buttonMode") {
    object.set_button_mode(to_boolean(value));
    return true;
  }
  return false;
}

}  // namespace fo4::avm2
