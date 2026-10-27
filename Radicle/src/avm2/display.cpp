#include <fo4/avm2/display.hpp>

#include <fo4/avm2/coercion.hpp>
#include <fo4/avm2/interpreter.hpp>
#include <fo4/avm2/name.hpp>
#include <fo4/avm2/runtime.hpp>

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
  ExecutionResult script = run_frame_script(interpreter);
  if (!script.ok()) {
    return script;
  }
  (void)interpreter;
  return script;
}

ExecutionResult MovieClip::run_frame_script(Interpreter& interpreter) {
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
  if (movie_clip != nullptr && klass.is_subclass_of(movie_clip)) {
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
    out = Value::number(0.0);
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
      for (const TimelineFrameLabel& candidate : clip.timeline()->labels) {
        auto label = std::make_shared<Object>();
        interpreter.prepare_plain_object(*label);
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
  if (name == "name") {
    object.set_name(to_string(value));
    return true;
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
