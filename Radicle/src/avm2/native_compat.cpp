#include <fo4/avm2/class.hpp>
#include <fo4/avm2/coercion.hpp>
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

Class* class_from_self(const Value& self) {
  if (!self.is_object() || self.as_object()->kind() != ObjectKind::Class) {
    return nullptr;
  }
  return static_cast<const ClassObject&>(*self.as_object()).represented_class();
}

Value property_of(const Object& object, const char* name) {
  Multiname multiname = builtin_name(name);
  Value value;
  if (!object.read_value(multiname, value)) {
    return Value::undefined();
  }
  return value;
}

void set_property(Object& object, const char* name, Value value) {
  Multiname multiname = builtin_name(name);
  object.write_value(multiname, value);
}

/* The constructor field lists must match the AS3 signatures because scripts read them by name. */
const std::vector<const char*>* constructor_fields(const std::string& class_name) {
  static const std::vector<const char*> bitmap_data{"width", "height", "transparent", "fillColor"};
  static const std::vector<const char*> bitmap{"bitmapData", "pixelSnapping", "smoothing"};
  static const std::vector<const char*> frame_label{"name", "frame"};
  static const std::vector<const char*> blur{"blurX", "blurY", "quality"};
  static const std::vector<const char*> color_matrix{"matrix"};
  static const std::vector<const char*> drop_shadow{"distance", "angle", "color", "alpha", "blurX",
                                                    "blurY", "strength", "quality", "inner",
                                                    "knockout", "hideObject"};
  static const std::vector<const char*> glow{"color", "alpha", "blurX", "blurY", "strength",
                                             "quality", "inner", "knockout"};
  if (class_name.find("::BitmapData") != std::string::npos) return &bitmap_data;
  if (class_name.find("::Bitmap") != std::string::npos) return &bitmap;
  if (class_name.find("::FrameLabel") != std::string::npos) return &frame_label;
  if (class_name.find("::BlurFilter") != std::string::npos) return &blur;
  if (class_name.find("::ColorMatrixFilter") != std::string::npos) return &color_matrix;
  if (class_name.find("::DropShadowFilter") != std::string::npos) return &drop_shadow;
  if (class_name.find("::GlowFilter") != std::string::npos) return &glow;
  static const std::vector<const char*> none;
  return &none;
}

ExecutionResult compat_construct(Interpreter& interpreter, const Value& self,
                                 const std::vector<Value>& arguments) {
  Class* klass = class_from_self(self);
  if (klass == nullptr) {
    return native_value(Value::null());
  }
  auto instance = interpreter.make_instance(*klass);
  const std::vector<const char*>* fields = constructor_fields(klass->name);
  for (std::size_t index = 0; index < fields->size() && index < arguments.size(); ++index) {
    set_property(*instance, (*fields)[index], arguments[index]);
  }
  return native_value(Value::object(std::move(instance)));
}

ExecutionResult compat_rect(Interpreter& interpreter, const Value& self) {
  Class* rectangle = const_cast<Class*>(interpreter.runtime().find_class("flash.geom::Rectangle"));
  if (rectangle == nullptr || !self.is_object()) {
    return native_value(Value::null());
  }
  auto instance = interpreter.make_instance(*rectangle);
  set_property(*instance, "x", Value::number(0.0));
  set_property(*instance, "y", Value::number(0.0));
  set_property(*instance, "width", property_of(*self.as_object(), "width"));
  set_property(*instance, "height", property_of(*self.as_object(), "height"));
  return native_value(Value::object(std::move(instance)));
}

ExecutionResult compat_number(const Value& self) {
  (void)self;
  return native_value(Value::int32(0));
}

ExecutionResult compat_false(const Value& self) {
  (void)self;
  return native_value(Value::boolean(false));
}

ExecutionResult compat_undefined(const Value& self) {
  (void)self;
  return native_value(Value::undefined());
}

ExecutionResult compat_null(const Value& self) {
  (void)self;
  return native_value(Value::null());
}

ExecutionResult compat_static_get(const Value& self, const char* name) {
  if (!self.is_object()) {
    return native_value(Value::undefined());
  }
  return native_value(property_of(*self.as_object(), name));
}

ExecutionResult compat_static_set(const Value& self, const std::vector<Value>& arguments,
                                  const char* name) {
  if (self.is_object()) {
    set_property(*self.as_object(), name, argument_at(arguments, 0));
  }
  return native_value(Value::undefined());
}

}  // namespace

bool try_compat_native(Interpreter& interpreter, native::Id id, const Value& self,
                       const std::vector<Value>& arguments, std::size_t depth,
                       ExecutionResult& out) {
  (void)depth;
  switch (id) {
    case native::Id::compat_construct:
      out = compat_construct(interpreter, self, arguments);
      return true;
    case native::Id::compat_rect:
      out = compat_rect(interpreter, self);
      return true;
    case native::Id::compat_number:
      out = compat_number(self);
      return true;
    case native::Id::compat_false:
      out = compat_false(self);
      return true;
    case native::Id::compat_undefined:
      out = compat_undefined(self);
      return true;
    case native::Id::compat_null:
      out = compat_null(self);
      return true;
    case native::Id::compat_static_get:
      out = compat_static_get(self, "soundTransform");
      return true;
    case native::Id::compat_static_set:
      out = compat_static_set(self, arguments, "soundTransform");
      return true;
    default:
      return false;
  }
}

}  // namespace fo4::avm2
