#include <fo4/avm2/class.hpp>
#include <fo4/avm2/coercion.hpp>
#include <fo4/avm2/display.hpp>
#include <fo4/avm2/events.hpp>
#include <fo4/avm2/hit_test.hpp>
#include <fo4/avm2/interpreter.hpp>
#include <fo4/avm2/name.hpp>
#include <fo4/avm2/net.hpp>
#include <fo4/avm2/object.hpp>
#include <fo4/avm2/runtime.hpp>
#include <fo4/avm2/shape_geometry.hpp>
#include <fo4/avm2/value.hpp>

#include "native_internal.hpp"

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace fo4::avm2 {

namespace {

/* A draw command must be recorded, not executed, because rendering stays with GardenEngine. */
struct GraphicsCommand {
  std::uint8_t op{0};
  double values[6]{0.0, 0.0, 0.0, 0.0, 0.0, 0.0};
  std::uint32_t color{0};
  double alpha{1.0};
};

class GraphicsObject : public Object {
 public:
  explicit GraphicsObject(Class* graphics_class) : Object(ObjectKind::Object) {
    if (graphics_class != nullptr) {
      set_instance_class(graphics_class);
      if (graphics_class->prototype_object != nullptr) {
        set_prototype(graphics_class->prototype_object);
      }
    }
    set_dynamic(true);
  }

  std::vector<GraphicsCommand> commands;
};

Class* class_from_self(const Value& self) {
  if (!self.is_object() || self.as_object()->kind() != ObjectKind::Class) {
    return nullptr;
  }
  return static_cast<const ClassObject&>(*self.as_object()).represented_class();
}

Class* find_class(Interpreter& interpreter, const char* name) {
  return const_cast<Class*>(interpreter.runtime().find_class(name));
}

DisplayObject* display_self(const Value& self) {
  if (!self.is_object() || self.as_object()->kind() != ObjectKind::Display) {
    return nullptr;
  }
  return static_cast<DisplayObject*>(self.as_object().get());
}

GraphicsObject* graphics_self(const Value& self) {
  if (!self.is_object()) {
    return nullptr;
  }
  const Class* klass = self.as_object()->instance_class();
  if (klass == nullptr || klass->name != "flash.display::Graphics") {
    return nullptr;
  }
  return static_cast<GraphicsObject*>(self.as_object().get());
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
  object.write_value(multiname, std::move(value));
}

double number_property(const Object& object, const char* name, double fallback) {
  const Value value = property_of(object, name);
  return value.is_number_like() ? value.as_number() : fallback;
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
  if (klass->name == "flash.display::Graphics") {
    return native_value(Value::object(std::make_shared<GraphicsObject>(klass)));
  }
  auto instance = interpreter.make_instance(*klass);
  const std::vector<const char*>* fields = constructor_fields(klass->name);
  for (std::size_t index = 0; index < fields->size() && index < arguments.size(); ++index) {
    set_property(*instance, (*fields)[index], arguments[index]);
  }
  return native_value(Value::object(std::move(instance)));
}

ExecutionResult compat_rect(Interpreter& interpreter, const Value& self) {
  Class* rectangle = find_class(interpreter, "flash.geom::Rectangle");
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

/* A draw call must keep every argument it receives so a future renderer can replay the path. */
ExecutionResult graphics_command(const Value& self, std::uint8_t op,
                                 const std::vector<Value>& arguments) {
  GraphicsObject* graphics = graphics_self(self);
  if (graphics == nullptr) {
    return native_value(Value::undefined());
  }
  GraphicsCommand command;
  command.op = op;
  std::size_t number_index = 0;
  for (const Value& argument : arguments) {
    if (argument.is_number_like() && number_index < 6) {
      command.values[number_index++] = argument.as_number();
    } else if (argument.is_int32() || argument.is_uint32()) {
      command.color = argument.is_uint32() ? argument.as_uint32()
                                           : static_cast<std::uint32_t>(argument.as_int32());
    }
  }
  graphics->commands.push_back(command);
  return native_value(Value::undefined());
}

std::int32_t pixel_coord(double value) { return static_cast<std::int32_t>(std::lround(value)); }

void emit_move(std::vector<swf::SwfShapeRecord>& records, double x, double y) {
  swf::SwfShapeRecord record;
  record.kind = swf::SwfShapeRecordKind::move;
  record.x = pixel_coord(x);
  record.y = pixel_coord(y);
  records.push_back(record);
}

void emit_edge(std::vector<swf::SwfShapeRecord>& records, swf::SwfShapeRecordKind kind,
               double control_x, double control_y, double x, double y, bool fill, bool line) {
  swf::SwfShapeRecord record;
  record.kind = kind;
  record.control_x = pixel_coord(control_x);
  record.control_y = pixel_coord(control_y);
  record.x = pixel_coord(x);
  record.y = pixel_coord(y);
  record.has_fill1 = fill;
  record.has_line = line;
  records.push_back(record);
}

void emit_break(std::vector<swf::SwfShapeRecord>& records) {
  swf::SwfShapeRecord record;
  record.kind = swf::SwfShapeRecordKind::style_change;
  records.push_back(record);
}

// A recorded path must become edge records, because the snapshot and hit test read records.
std::vector<swf::SwfShapeRecord> graphics_shape_records(const GraphicsObject& graphics) {
  std::vector<swf::SwfShapeRecord> records;
  bool filling = false;
  bool line = false;
  double pen_x = 0.0;
  double pen_y = 0.0;
  auto edge_to = [&](double x, double y) {
    emit_edge(records, swf::SwfShapeRecordKind::straight_edge, 0.0, 0.0, x, y, filling, line);
    pen_x = x;
    pen_y = y;
  };
  for (const GraphicsCommand& command : graphics.commands) {
    switch (command.op) {
      case 0:
      case 1:
        // A gradient fill must read as a flat fill, since the runtime keeps no raster state.
        filling = true;
        emit_break(records);
        break;
      case 2:
        filling = false;
        emit_break(records);
        break;
      case 3:
        line = command.values[0] > 0.0;
        break;
      case 4:
        pen_x = command.values[0];
        pen_y = command.values[1];
        emit_move(records, pen_x, pen_y);
        break;
      case 5:
        edge_to(command.values[0], command.values[1]);
        break;
      case 6:
        emit_edge(records, swf::SwfShapeRecordKind::curved_edge, command.values[0],
                  command.values[1], command.values[2], command.values[3], filling, line);
        pen_x = command.values[2];
        pen_y = command.values[3];
        break;
      case 7: {
        const double x = command.values[0];
        const double y = command.values[1];
        const double right = x + command.values[2];
        const double bottom = y + command.values[3];
        emit_move(records, x, y);
        edge_to(right, y);
        edge_to(right, bottom);
        edge_to(x, bottom);
        edge_to(x, y);
        break;
      }
      case 8: {
        // A circle must become an octagon, since records carry no arc primitive.
        const double x = command.values[0];
        const double y = command.values[1];
        const double radius = command.values[2];
        emit_move(records, x + radius, y);
        for (int step = 1; step <= 8; ++step) {
          const double angle = step * 3.141592653589793 / 4.0;
          edge_to(x + radius * std::cos(angle), y + radius * std::sin(angle));
        }
        break;
      }
      case 9: {
        // A round rect must become a chamfered polygon, for the same reason as the circle.
        const double x = command.values[0];
        const double y = command.values[1];
        const double right = x + command.values[2];
        const double bottom = y + command.values[3];
        const double cut_x = command.values[4] / 2.0;
        const double cut_y = command.values[5] / 2.0;
        emit_move(records, x + cut_x, y);
        edge_to(right - cut_x, y);
        edge_to(right, y + cut_y);
        edge_to(right, bottom - cut_y);
        edge_to(right - cut_x, bottom);
        edge_to(x + cut_x, bottom);
        edge_to(x, bottom - cut_y);
        edge_to(x, y + cut_y);
        edge_to(x + cut_x, y);
        break;
      }
      default:
        break;
    }
  }
  return records;
}

Value rectangle_value(Interpreter& interpreter, double x, double y, double width, double height) {
  Class* rectangle = find_class(interpreter, "flash.geom::Rectangle");
  if (rectangle == nullptr) return Value::null();
  auto instance = interpreter.make_instance(*rectangle);
  set_property(*instance, "x", Value::number(x));
  set_property(*instance, "y", Value::number(y));
  set_property(*instance, "width", Value::number(width));
  set_property(*instance, "height", Value::number(height));
  return Value::object(std::move(instance));
}

ExecutionResult display_graphics(Interpreter& interpreter, const Value& self) {
  DisplayObject* object = display_self(self);
  if (object == nullptr) {
    return native_value(Value::null());
  }
  if (object->graphics().is_object()) {
    return native_value(object->graphics());
  }
  Class* graphics_class = find_class(interpreter, "flash.display::Graphics");
  if (graphics_class == nullptr) {
    return native_value(Value::null());
  }
  Value graphics = Value::object(std::make_shared<GraphicsObject>(graphics_class));
  object->set_graphics(graphics);
  return native_value(std::move(graphics));
}

ExecutionResult display_get_bounds(Interpreter& interpreter, const Value& self) {
  DisplayObject* object = display_self(self);
  if (object == nullptr) {
    return native_value(Value::null());
  }
  return native_value(rectangle_value(interpreter, object->x() + object->origin_x(),
                                      object->y() + object->origin_y(), object->width(),
                                      object->height()));
}

const DisplayObject* display_argument(const std::vector<Value>& arguments) {
  const Value value = argument_at(arguments, 0);
  if (!value.is_object() || value.as_object()->kind() != ObjectKind::Display) {
    return nullptr;
  }
  return static_cast<const DisplayObject*>(value.as_object().get());
}

ExecutionResult display_hit_test(const Value& self, const std::vector<Value>& arguments) {
  DisplayObject* object = display_self(self);
  if (object == nullptr) {
    return native_value(Value::boolean(false));
  }
  if (const DisplayObject* other = display_argument(arguments)) {
    return native_value(Value::boolean(hit_test_object(*object, *other)));
  }
  // A point hit must read the third argument as shapeFlag, matching the AS3 signature.
  const bool shape_flag = arguments.size() > 2 && to_boolean(arguments[2]);
  return native_value(Value::boolean(hit_test_point(*object, to_number(argument_at(arguments, 0)),
                                                    to_number(argument_at(arguments, 1)),
                                                    shape_flag)));
}

ExecutionResult display_hit_test_point(const Value& self, const std::vector<Value>& arguments) {
  DisplayObject* object = display_self(self);
  if (object == nullptr) {
    return native_value(Value::boolean(false));
  }
  const bool shape_flag = arguments.size() > 2 && to_boolean(arguments[2]);
  return native_value(Value::boolean(hit_test_point(*object, to_number(argument_at(arguments, 0)),
                                                    to_number(argument_at(arguments, 1)),
                                                    shape_flag)));
}

ExecutionResult display_hit_test_object(const Value& self, const std::vector<Value>& arguments) {
  DisplayObject* object = display_self(self);
  const DisplayObject* other = display_argument(arguments);
  return native_value(Value::boolean(object != nullptr && other != nullptr &&
                                     hit_test_object(*object, *other)));
}

ExecutionResult display_transform_get(Interpreter& interpreter, const Value& self) {
  DisplayObject* object = display_self(self);
  if (object == nullptr) {
    return native_value(Value::null());
  }
  Class* object_class = find_class(interpreter, "Object");
  Class* color_class = find_class(interpreter, "flash.geom::ColorTransform");
  Class* matrix_class = find_class(interpreter, "flash.geom::Matrix");
  if (object_class == nullptr || color_class == nullptr || matrix_class == nullptr) {
    return native_value(Value::null());
  }
  auto transform = interpreter.make_instance(*object_class);
  auto color = interpreter.make_instance(*color_class);
  set_property(*color, "redMultiplier", Value::number(object->color_multiply()[0]));
  set_property(*color, "greenMultiplier", Value::number(object->color_multiply()[1]));
  set_property(*color, "blueMultiplier", Value::number(object->color_multiply()[2]));
  set_property(*color, "alphaMultiplier", Value::number(object->color_multiply()[3]));
  set_property(*color, "redOffset", Value::number(object->color_add()[0]));
  set_property(*color, "greenOffset", Value::number(object->color_add()[1]));
  set_property(*color, "blueOffset", Value::number(object->color_add()[2]));
  set_property(*color, "alphaOffset", Value::number(object->color_add()[3]));
  auto matrix = interpreter.make_instance(*matrix_class);
  set_property(*matrix, "a", Value::number(object->scale_x()));
  set_property(*matrix, "b", Value::number(0.0));
  set_property(*matrix, "c", Value::number(0.0));
  set_property(*matrix, "d", Value::number(object->scale_y()));
  set_property(*matrix, "tx", Value::number(object->x()));
  set_property(*matrix, "ty", Value::number(object->y()));
  set_property(*transform, "colorTransform", Value::object(std::move(color)));
  set_property(*transform, "matrix", Value::object(std::move(matrix)));
  return native_value(Value::object(std::move(transform)));
}

ExecutionResult display_transform_set(Interpreter& interpreter, const Value& self,
                                      const std::vector<Value>& arguments) {
  (void)interpreter;
  DisplayObject* object = display_self(self);
  if (object == nullptr || !argument_at(arguments, 0).is_object()) {
    return native_value(Value::undefined());
  }
  const Object& transform = *argument_at(arguments, 0).as_object();
  const Value color_value = property_of(transform, "colorTransform");
  if (color_value.is_object()) {
    const Object& color = *color_value.as_object();
    const double multiply[4] = {number_property(color, "redMultiplier", 1.0),
                                number_property(color, "greenMultiplier", 1.0),
                                number_property(color, "blueMultiplier", 1.0),
                                number_property(color, "alphaMultiplier", 1.0)};
    const double add[4] = {number_property(color, "redOffset", 0.0),
                           number_property(color, "greenOffset", 0.0),
                           number_property(color, "blueOffset", 0.0),
                           number_property(color, "alphaOffset", 0.0)};
    object->set_color_multiply(multiply);
    object->set_color_add(add);
  }
  return native_value(Value::undefined());
}

void set_info_domain(Interpreter& interpreter, Object& info,
                     const std::shared_ptr<ApplicationDomainObject>& domain) {
  set_property(info, "applicationDomain",
               Value::object(domain == nullptr ? interpreter.runtime().current_domain() : domain));
}

Value make_loader_info(Interpreter& interpreter,
                       const std::shared_ptr<ApplicationDomainObject>& domain) {
  Class* info_class = find_class(interpreter, "flash.display::LoaderInfo");
  Class* object_class = find_class(interpreter, "Object");
  if (info_class == nullptr || object_class == nullptr) {
    return Value::null();
  }
  auto info = interpreter.make_instance(*info_class);
  set_info_domain(interpreter, *info, domain);
  set_property(*info, "parameters",
               Value::object(interpreter.make_instance(*object_class)));
  return Value::object(std::move(info));
}

Value content_loader_info(Interpreter& interpreter, LoaderObject& loader) {
  if (loader.content_loader_info.is_object()) return loader.content_loader_info;
  Value value = make_loader_info(interpreter, loader.domain);
  if (value.is_object()) loader.content_loader_info = value;
  return value;
}

ExecutionResult loader_content_loader_info(Interpreter& interpreter, const Value& self) {
  auto* loader = self.is_object() ? dynamic_cast<LoaderObject*>(self.as_object().get()) : nullptr;
  if (loader == nullptr) return native_value(Value::null());
  return native_value(content_loader_info(interpreter, *loader));
}

ExecutionResult display_loader_info(Interpreter& interpreter, const Value& self) {
  auto* content = self.is_object() ? dynamic_cast<DisplayObject*>(self.as_object().get()) : nullptr;
  auto* loader = content == nullptr ? nullptr : dynamic_cast<LoaderObject*>(content->parent());
  if (loader != nullptr && loader->content.get() == content) {
    return native_value(content_loader_info(interpreter, *loader));
  }
  return native_value(make_loader_info(interpreter, nullptr));
}

ExecutionResult keyboard_is_down(Interpreter& interpreter, const std::vector<Value>& arguments) {
  const std::uint32_t key_code = static_cast<std::uint32_t>(to_number(argument_at(arguments, 0)));
  return native_value(Value::boolean(interpreter.runtime().key_pressed(key_code)));
}

ExecutionResult event_format_to_string(const Value& self, const std::vector<Value>& arguments) {
  if (!self.is_object() || self.as_object()->kind() != ObjectKind::Event) {
    return native_value(Value::string(""));
  }
  const auto& event = static_cast<const EventObject&>(*self.as_object());
  const std::string class_name = arguments.empty() ? "Event" : to_string(arguments[0]);
  const std::string text = "[" + class_name + " type=\"" + event.event_type() + "\" bubbles=" +
                           (event.bubbles() ? "true" : "false") + " cancelable=" +
                           (event.cancelable() ? "true" : "false") + "]";
  return native_value(Value::string(text));
}

}  // namespace

ShapeGeometry drawn_geometry_of(const DisplayObject& object) {
  if (!object.graphics().is_object()) {
    return {};
  }
  const Object& graphics_value = *object.graphics().as_object();
  const Class* klass = graphics_value.instance_class();
  if (klass == nullptr || klass->name != "flash.display::Graphics") {
    return {};
  }
  const auto& graphics = static_cast<const GraphicsObject&>(graphics_value);
  // Graphics coordinates must stay pixels, so the record scale factor is one.
  return geometry_from_shape_records(graphics_shape_records(graphics), 1.0);
}

ShapeGeometry effective_shape_geometry(const DisplayObject& object) {
  if (const auto& placed = object.shape_geometry()) {
    return *placed;
  }
  return drawn_geometry_of(object);
}

void refresh_content_loader_info(Interpreter& interpreter, LoaderObject& loader) {
  if (loader.content_loader_info.is_object()) {
    set_info_domain(interpreter, *loader.content_loader_info.as_object(), loader.domain);
  }
}

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
    case native::Id::graphics_construct:
      out = compat_construct(interpreter, self, arguments);
      return true;
    case native::Id::graphics_begin_fill:
      out = graphics_command(self, 0, arguments);
      return true;
    case native::Id::graphics_begin_gradient_fill:
      out = graphics_command(self, 1, arguments);
      return true;
    case native::Id::graphics_end_fill:
      out = graphics_command(self, 2, arguments);
      return true;
    case native::Id::graphics_line_style:
      out = graphics_command(self, 3, arguments);
      return true;
    case native::Id::graphics_move_to:
      out = graphics_command(self, 4, arguments);
      return true;
    case native::Id::graphics_line_to:
      out = graphics_command(self, 5, arguments);
      return true;
    case native::Id::graphics_curve_to:
      out = graphics_command(self, 6, arguments);
      return true;
    case native::Id::graphics_draw_rect:
      out = graphics_command(self, 7, arguments);
      return true;
    case native::Id::graphics_draw_circle:
      out = graphics_command(self, 8, arguments);
      return true;
    case native::Id::graphics_draw_round_rect:
      out = graphics_command(self, 9, arguments);
      return true;
    case native::Id::graphics_clear:
      if (GraphicsObject* graphics = graphics_self(self)) graphics->commands.clear();
      out = native_value(Value::undefined());
      return true;
    case native::Id::graphics_copy_from:
      out = compat_undefined(self);
      return true;
    case native::Id::display_graphics:
      out = display_graphics(interpreter, self);
      return true;
    case native::Id::display_get_bounds:
      out = display_get_bounds(interpreter, self);
      return true;
    case native::Id::display_hit_test:
      out = display_hit_test(self, arguments);
      return true;
    case native::Id::display_hit_test_point:
      out = display_hit_test_point(self, arguments);
      return true;
    case native::Id::display_hit_test_object:
      out = display_hit_test_object(self, arguments);
      return true;
    case native::Id::display_transform_get:
      out = display_transform_get(interpreter, self);
      return true;
    case native::Id::display_transform_set:
      out = display_transform_set(interpreter, self, arguments);
      return true;
    case native::Id::loader_content_loader_info:
      out = loader_content_loader_info(interpreter, self);
      return true;
    case native::Id::loader_unload_and_stop:
      out = compat_undefined(self);
      return true;
    case native::Id::event_format_to_string:
      out = event_format_to_string(self, arguments);
      return true;
    case native::Id::keyboard_is_down:
      out = keyboard_is_down(interpreter, arguments);
      return true;
    case native::Id::display_loader_info:
      out = display_loader_info(interpreter, self);
      return true;
    default:
      return false;
  }
}

}  // namespace fo4::avm2
