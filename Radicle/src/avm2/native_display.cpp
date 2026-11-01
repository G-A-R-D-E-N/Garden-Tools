#include <fo4/avm2/coercion.hpp>
#include <fo4/avm2/display.hpp>
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

DisplayObject* display_self(const Value& self) {
  if (!self.is_object() || self.as_object()->kind() != ObjectKind::Display) {
    return nullptr;
  }
  return static_cast<DisplayObject*>(self.as_object().get());
}

DisplayObjectContainer* container_self(const Value& self) {
  DisplayObject* object = display_self(self);
  return object != nullptr && object->is_container() ? static_cast<DisplayObjectContainer*>(object)
                                                     : nullptr;
}

MovieClip* movie_clip_self(const Value& self) {
  DisplayObject* object = display_self(self);
  return object != nullptr && object->is_movie_clip() ? static_cast<MovieClip*>(object) : nullptr;
}

std::shared_ptr<DisplayObject> display_argument(const std::vector<Value>& arguments,
                                                std::size_t index) {
  const Value value = argument_at(arguments, index);
  if (!value.is_object() || value.as_object()->kind() != ObjectKind::Display) {
    return nullptr;
  }
  return std::static_pointer_cast<DisplayObject>(value.as_object());
}

Value point_value(Interpreter& interpreter, double x, double y) {
  Class* point_class = find_display_class(interpreter.runtime(), "flash.geom::Point");
  if (point_class == nullptr) {
    auto plain = std::make_shared<Object>();
    interpreter.prepare_plain_object(*plain);
    Multiname x_name = builtin_name("x");
    Multiname y_name = builtin_name("y");
    Property& x_property = plain->define_own_property(primary_key(x_name));
    x_property.kind = PropertyKind::Value;
    x_property.value = Value::number(x);
    Property& y_property = plain->define_own_property(primary_key(y_name));
    y_property.kind = PropertyKind::Value;
    y_property.value = Value::number(y);
    return Value::object(std::move(plain));
  }
  auto instance = interpreter.make_instance(*point_class);
  Multiname x_name = builtin_name("x");
  Multiname y_name = builtin_name("y");
  Property& x_property = instance->define_own_property(primary_key(x_name));
  x_property.kind = PropertyKind::Value;
  x_property.value = Value::number(x);
  Property& y_property = instance->define_own_property(primary_key(y_name));
  y_property.kind = PropertyKind::Value;
  y_property.value = Value::number(y);
  return Value::object(std::move(instance));
}

void point_coordinates(const Value& point, double& x, double& y) {
  x = 0.0;
  y = 0.0;
  if (!point.is_object()) {
    return;
  }
  Object& object = *point.as_object();
  Value value;
  if (object.read_value(builtin_name("x"), value) && value.is_number_like()) {
    x = value.as_number();
  }
  value = Value::undefined();
  if (object.read_value(builtin_name("y"), value) && value.is_number_like()) {
    y = value.as_number();
  }
}

void accumulate_translation(const DisplayObject& object, double& x, double& y) {
  for (const DisplayObject* current = &object; current != nullptr; current = current->parent()) {
    x += current->x();
    y += current->y();
  }
}

ExecutionResult display_construct(Interpreter& interpreter, const Value& self) {
  if (!self.is_object() || self.as_object()->kind() != ObjectKind::Class) {
    return native_value(Value::undefined());
  }
  Class* klass = static_cast<const ClassObject&>(*self.as_object()).represented_class();
  if (klass == nullptr) {
    return native_value(Value::undefined());
  }
  auto object = make_display_object(interpreter.runtime(), *klass);
  return native_value(Value::object(std::move(object)));
}

ExecutionResult display_add_child(Interpreter& interpreter, const Value& self,
                                  const std::vector<Value>& arguments) {
  DisplayObjectContainer* container = container_self(self);
  std::shared_ptr<DisplayObject> child = display_argument(arguments, 0);
  if (container == nullptr || child == nullptr) {
    return native_value(Value::undefined());
  }
  (void)interpreter;
  container->add_child(child);
  return native_value(Value::object(std::move(child)));
}

ExecutionResult display_add_child_at(Interpreter& interpreter, const Value& self,
                                     const std::vector<Value>& arguments) {
  DisplayObjectContainer* container = container_self(self);
  std::shared_ptr<DisplayObject> child = display_argument(arguments, 0);
  if (container == nullptr || child == nullptr) {
    return native_value(Value::undefined());
  }
  (void)interpreter;
  const std::int32_t index = to_int32(argument_at(arguments, 1));
  container->add_child_at(child, index < 0 ? 0u : static_cast<std::size_t>(index));
  return native_value(Value::object(std::move(child)));
}

ExecutionResult display_remove_child(Interpreter& interpreter, const Value& self,
                                     const std::vector<Value>& arguments) {
  (void)interpreter;
  DisplayObjectContainer* container = container_self(self);
  std::shared_ptr<DisplayObject> child = display_argument(arguments, 0);
  if (container == nullptr || child == nullptr) {
    return native_value(Value::undefined());
  }
  container->remove_child(child);
  return native_value(Value::object(std::move(child)));
}

ExecutionResult display_remove_child_at(Interpreter& interpreter, const Value& self,
                                        const std::vector<Value>& arguments) {
  (void)interpreter;
  DisplayObjectContainer* container = container_self(self);
  if (container == nullptr) {
    return native_value(Value::undefined());
  }
  const std::int32_t index = to_int32(argument_at(arguments, 0));
  if (index < 0) {
    return native_value(Value::undefined());
  }
  std::shared_ptr<DisplayObject> child =
      container->remove_child_at(static_cast<std::size_t>(index));
  return child == nullptr ? native_value(Value::undefined())
                          : native_value(Value::object(std::move(child)));
}

ExecutionResult display_get_child_at(Interpreter& interpreter, const Value& self,
                                     const std::vector<Value>& arguments) {
  (void)interpreter;
  DisplayObjectContainer* container = container_self(self);
  if (container == nullptr) {
    return native_value(Value::undefined());
  }
  const std::int32_t index = to_int32(argument_at(arguments, 0));
  if (index < 0) {
    return native_value(Value::undefined());
  }
  std::shared_ptr<DisplayObject> child = container->child_at(static_cast<std::size_t>(index));
  return child == nullptr ? native_value(Value::undefined())
                          : native_value(Value::object(std::move(child)));
}

ExecutionResult display_get_child_by_name(Interpreter& interpreter, const Value& self,
                                          const std::vector<Value>& arguments) {
  (void)interpreter;
  DisplayObjectContainer* container = container_self(self);
  if (container == nullptr) {
    return native_value(Value::null());
  }
  std::shared_ptr<DisplayObject> child = container->child_by_name(to_string(argument_at(arguments, 0)));
  return child == nullptr ? native_value(Value::null())
                          : native_value(Value::object(std::move(child)));
}

ExecutionResult display_contains(Interpreter& interpreter, const Value& self,
                                 const std::vector<Value>& arguments) {
  (void)interpreter;
  DisplayObjectContainer* container = container_self(self);
  std::shared_ptr<DisplayObject> child = display_argument(arguments, 0);
  return native_value(
      Value::boolean(container != nullptr && child != nullptr && container->contains(child.get())));
}

ExecutionResult display_set_child_index(Interpreter& interpreter, const Value& self,
                                        const std::vector<Value>& arguments) {
  (void)interpreter;
  DisplayObjectContainer* container = container_self(self);
  std::shared_ptr<DisplayObject> child = display_argument(arguments, 0);
  if (container == nullptr || child == nullptr) {
    return native_value(Value::undefined());
  }
  const std::int32_t index = to_int32(argument_at(arguments, 1));
  if (index >= 0) {
    container->set_child_index(child.get(), static_cast<std::size_t>(index));
  }
  return native_value(Value::undefined());
}

ExecutionResult display_swap_children(Interpreter& interpreter, const Value& self,
                                      const std::vector<Value>& arguments) {
  (void)interpreter;
  DisplayObjectContainer* container = container_self(self);
  std::shared_ptr<DisplayObject> first = display_argument(arguments, 0);
  std::shared_ptr<DisplayObject> second = display_argument(arguments, 1);
  if (container != nullptr && first != nullptr && second != nullptr) {
    container->swap_children(first.get(), second.get());
  }
  return native_value(Value::undefined());
}

std::uint32_t requested_frame(const Multiname& multiname, MovieClip& clip, const Value& value) {
  (void)multiname;
  if (value.is_string() && clip.timeline() != nullptr) {
    return clip.timeline()->frame_for_label(value.as_string());
  }
  const double frame = to_number(value);
  return frame <= 0.0 ? 1u : static_cast<std::uint32_t>(frame);
}

ExecutionResult display_goto(Interpreter& interpreter, const Value& self,
                             const std::vector<Value>& arguments, bool play) {
  MovieClip* clip = movie_clip_self(self);
  if (clip == nullptr) {
    return native_value(Value::undefined());
  }
  const std::uint32_t frame = requested_frame(builtin_name("frame"), *clip,
                                              argument_at(arguments, 0));
  ExecutionResult result = clip->goto_frame(interpreter, frame, play);
  if (!result.ok()) {
    return result;
  }
  return native_value(Value::undefined());
}

ExecutionResult display_stop_play(const Value& self, bool play) {
  MovieClip* clip = movie_clip_self(self);
  if (clip != nullptr) {
    clip->set_playing(play);
  }
  return native_value(Value::undefined());
}

ExecutionResult display_add_frame_script(const Value& self, const std::vector<Value>& arguments) {
  MovieClip* clip = movie_clip_self(self);
  if (clip == nullptr) {
    return native_value(Value::undefined());
  }
  for (std::size_t at = 0; at + 1 < arguments.size(); at += 2) {
    const std::int32_t frame = to_int32(arguments[at]);
    if (frame < 0) {
      continue;
    }
    const Value& script = arguments[at + 1];
    clip->set_frame_script(static_cast<std::uint32_t>(frame) + 1u,
                           script.is_object() && script.as_object()->kind() == ObjectKind::Function
                               ? std::static_pointer_cast<FunctionObject>(script.as_object())
                               : nullptr);
  }
  return native_value(Value::undefined());
}

ExecutionResult display_step(Interpreter& interpreter, const Value& self, int direction) {
  MovieClip* clip = movie_clip_self(self);
  if (clip == nullptr) {
    return native_value(Value::undefined());
  }
  const std::uint32_t total = clip->total_frames();
  std::uint32_t frame = 1;
  if (direction > 0) {
    frame = clip->current_frame() >= total ? 1u : clip->current_frame() + 1;
  } else {
    frame = clip->current_frame() <= 1 ? total : clip->current_frame() - 1;
  }
  ExecutionResult result = clip->goto_frame(interpreter, frame, false);
  if (!result.ok()) {
    return result;
  }
  return native_value(Value::undefined());
}

ExecutionResult display_to_global(Interpreter& interpreter, const Value& self,
                                  const std::vector<Value>& arguments, bool to_global) {
  DisplayObject* object = display_self(self);
  const Value point = argument_at(arguments, 0);
  if (object == nullptr) {
    return native_value(point);
  }
  double x = 0.0;
  double y = 0.0;
  point_coordinates(point, x, y);
  double offset_x = 0.0;
  double offset_y = 0.0;
  accumulate_translation(*object, offset_x, offset_y);
  if (to_global) {
    return native_value(point_value(interpreter, x + offset_x, y + offset_y));
  }
  return native_value(point_value(interpreter, x - offset_x, y - offset_y));
}

ExecutionResult geom_construct(Interpreter& interpreter, const Value& self,
                               const std::vector<Value>& arguments) {
  if (!self.is_object() || self.as_object()->kind() != ObjectKind::Class) {
    return native_value(Value::undefined());
  }
  Class* klass = static_cast<const ClassObject&>(*self.as_object()).represented_class();
  if (klass == nullptr) {
    return native_value(Value::undefined());
  }
  auto instance = interpreter.make_instance(*klass);
  const std::string& class_name = klass->name;
  const std::vector<std::string> point_fields{"x", "y"};
  const std::vector<std::string> rectangle_fields{"x", "y", "width", "height"};
  const std::vector<std::string> matrix_fields{"a", "b", "c", "d", "tx", "ty"};
  const std::vector<std::string> color_fields{"redMultiplier", "greenMultiplier",
                                              "blueMultiplier", "alphaMultiplier",
                                              "redOffset",     "greenOffset",
                                              "blueOffset",    "alphaOffset"};
  const std::vector<std::string>* fields = nullptr;
  if (class_name.find("::Point") != std::string::npos) {
    fields = &point_fields;
  } else if (class_name.find("::Rectangle") != std::string::npos) {
    fields = &rectangle_fields;
  } else if (class_name.find("::Matrix") != std::string::npos) {
    fields = &matrix_fields;
  } else if (class_name.find("::ColorTransform") != std::string::npos) {
    fields = &color_fields;
  }
  if (fields != nullptr) {
    for (std::size_t index = 0; index < fields->size() && index < arguments.size(); ++index) {
      Multiname field = builtin_name((*fields)[index]);
      Property& property = instance->define_own_property(primary_key(field));
      property.kind = PropertyKind::Value;
      property.value = Value::number(to_number(arguments[index]));
    }
  }
  return native_value(Value::object(std::move(instance)));
}

ExecutionResult dictionary_construct(Interpreter& interpreter, const Value& self,
                                     const std::vector<Value>& arguments) {
  (void)arguments;
  if (!self.is_object() || self.as_object()->kind() != ObjectKind::Class) {
    return native_value(Value::undefined());
  }
  Class* klass = static_cast<const ClassObject&>(*self.as_object()).represented_class();
  if (klass == nullptr) {
    return native_value(Value::undefined());
  }
  return native_value(Value::object(interpreter.make_instance(*klass)));
}

}  // namespace

bool try_display_native(Interpreter& interpreter, native::Id id, const Value& self,
                        const std::vector<Value>& arguments, std::size_t depth,
                        ExecutionResult& out) {
  (void)depth;
  switch (id) {
    case native::Id::display_construct:
      out = display_construct(interpreter, self);
      return true;
    case native::Id::display_add_child:
      out = display_add_child(interpreter, self, arguments);
      return true;
    case native::Id::display_add_child_at:
      out = display_add_child_at(interpreter, self, arguments);
      return true;
    case native::Id::display_remove_child:
      out = display_remove_child(interpreter, self, arguments);
      return true;
    case native::Id::display_remove_child_at:
      out = display_remove_child_at(interpreter, self, arguments);
      return true;
    case native::Id::display_get_child_at:
      out = display_get_child_at(interpreter, self, arguments);
      return true;
    case native::Id::display_get_child_by_name:
      out = display_get_child_by_name(interpreter, self, arguments);
      return true;
    case native::Id::display_contains:
      out = display_contains(interpreter, self, arguments);
      return true;
    case native::Id::display_set_child_index:
      out = display_set_child_index(interpreter, self, arguments);
      return true;
    case native::Id::display_swap_children:
      out = display_swap_children(interpreter, self, arguments);
      return true;
    case native::Id::display_goto_and_play:
      out = display_goto(interpreter, self, arguments, true);
      return true;
    case native::Id::display_goto_and_stop:
      out = display_goto(interpreter, self, arguments, false);
      return true;
    case native::Id::display_stop:
      out = display_stop_play(self, false);
      return true;
    case native::Id::display_add_frame_script:
      out = display_add_frame_script(self, arguments);
      return true;
    case native::Id::display_play:
      out = display_stop_play(self, true);
      return true;
    case native::Id::display_next_frame:
      out = display_step(interpreter, self, 1);
      return true;
    case native::Id::display_prev_frame:
      out = display_step(interpreter, self, -1);
      return true;
    case native::Id::display_local_to_global:
      out = display_to_global(interpreter, self, arguments, true);
      return true;
    case native::Id::display_global_to_local:
      out = display_to_global(interpreter, self, arguments, false);
      return true;
    case native::Id::geom_construct:
      out = geom_construct(interpreter, self, arguments);
      return true;
    case native::Id::dictionary_construct:
      out = dictionary_construct(interpreter, self, arguments);
      return true;
    default:
      return false;
  }
}

}  // namespace fo4::avm2
