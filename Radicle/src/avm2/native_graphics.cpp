#include "native_graphics.hpp"

#include <fo4/avm2/coercion.hpp>
#include <fo4/avm2/display.hpp>
#include <fo4/avm2/name.hpp>
#include <fo4/avm2/shape_geometry.hpp>
#include <fo4/constants.hpp>
#include <fo4/swf/swf_art.hpp>

#include "native_internal.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace fo4::avm2 {

namespace {

double number_property(const Object& object, const char* name, double fallback) {
  Multiname multiname = builtin_name(name);
  Value value;
  if (!object.read_value(multiname, value) || !value.is_number_like()) {
    return fallback;
  }
  return value.as_number();
}

// NaN must map to zero, and infinite or huge coordinates must clamp on every platform.
std::int32_t twip_coord(double pixels) {
  if (std::isnan(pixels)) {
    return 0;
  }
  const double twips = pixels * k_twips_per_pixel;
  if (twips >= 2147483647.0) {
    return INT32_MAX;
  }
  if (twips <= -2147483648.0) {
    return INT32_MIN;
  }
  return static_cast<std::int32_t>(std::lround(twips));
}

std::uint32_t argb_of(std::uint32_t rgb, double alpha) {
  const double clamped = std::isnan(alpha) ? 0.0 : std::clamp(alpha, 0.0, 1.0);
  return (static_cast<std::uint32_t>(std::lround(clamped * 255.0)) << 24) | (rgb & 0xFFFFFFu);
}

std::int32_t fixed16_of(double value) {
  if (std::isnan(value)) {
    return 0;
  }
  return static_cast<std::int32_t>(
      std::lround(std::clamp(value * 65536.0, -2147483648.0, 2147483647.0)));
}

std::uint8_t byte_of(double value) {
  if (std::isnan(value)) {
    return 0;
  }
  return static_cast<std::uint8_t>(std::lround(std::clamp(value, 0.0, 255.0)));
}

double number_argument(const std::vector<Value>& arguments, std::size_t index, double fallback) {
  return index < arguments.size() ? to_number(arguments[index]) : fallback;
}

std::string string_argument(const std::vector<Value>& arguments, std::size_t index) {
  const Value value = argument_at(arguments, index);
  return value.is_string() ? value.as_string() : std::string{};
}

std::vector<Value> array_argument(const std::vector<Value>& arguments, std::size_t index) {
  const Value value = argument_at(arguments, index);
  if (value.is_object() && value.as_object()->kind() == ObjectKind::Array) {
    return static_cast<const ArrayObject&>(*value.as_object()).elements();
  }
  return {};
}

swf::SwfFillStyle solid_fill_of(const std::vector<Value>& arguments) {
  swf::SwfFillStyle fill;
  fill.type = 0x00;
  fill.color = argb_of(to_uint32(argument_at(arguments, 0)), number_argument(arguments, 1, 1.0));
  return fill;
}

swf::SwfFillStyle gradient_fill_of(const std::vector<Value>& arguments) {
  swf::SwfFillStyle fill;
  fill.type = string_argument(arguments, 0) == "radial" ? std::uint8_t{0x12} : std::uint8_t{0x10};
  const double focal = number_argument(arguments, 7, 0.0);
  if (fill.type == 0x12 && !std::isnan(focal) && focal != 0.0) {
    fill.type = 0x13;
    fill.focal_point =
        static_cast<std::int16_t>(std::lround(std::clamp(focal, -1.0, 1.0) * 256.0));
  }
  const std::vector<Value> colors = array_argument(arguments, 1);
  const std::vector<Value> alphas = array_argument(arguments, 2);
  const std::vector<Value> ratios = array_argument(arguments, 3);
  for (std::size_t index = 0; index < colors.size(); ++index) {
    swf::SwfGradientStop stop;
    stop.ratio = index < ratios.size() ? byte_of(to_number(ratios[index])) : std::uint8_t{0};
    stop.color = argb_of(to_uint32(colors[index]),
                         index < alphas.size() ? to_number(alphas[index]) : 1.0);
    fill.stops.push_back(stop);
  }
  const Value matrix_value = argument_at(arguments, 4);
  if (matrix_value.is_object()) {
    const Object& matrix = *matrix_value.as_object();
    swf::SwfMatrix transform;
    transform.scale_x = fixed16_of(number_property(matrix, "a", 1.0));
    transform.rotate_skew0 = fixed16_of(number_property(matrix, "b", 0.0));
    transform.rotate_skew1 = fixed16_of(number_property(matrix, "c", 0.0));
    transform.scale_y = fixed16_of(number_property(matrix, "d", 1.0));
    transform.translate_x = twip_coord(number_property(matrix, "tx", 0.0));
    transform.translate_y = twip_coord(number_property(matrix, "ty", 0.0));
    fill.matrix = transform;
  }
  const std::string spread = string_argument(arguments, 5);
  if (spread == "reflect") {
    fill.spread = 1;
  } else if (spread == "repeat") {
    fill.spread = 2;
  }
  if (string_argument(arguments, 6) == "linearRGB") {
    fill.interpolation = 1;
  }
  return fill;
}

std::optional<swf::SwfLineStyle> line_style_of(const std::vector<Value>& arguments) {
  const double thickness =
      number_argument(arguments, 0, std::numeric_limits<double>::quiet_NaN());
  if (std::isnan(thickness)) {
    return std::nullopt;
  }
  swf::SwfLineStyle line;
  line.width = static_cast<std::uint16_t>(std::lround(std::clamp(thickness * k_twips_per_pixel, 0.0, 65535.0)));
  line.color = argb_of(to_uint32(argument_at(arguments, 1)), number_argument(arguments, 2, 1.0));
  const std::string caps = string_argument(arguments, 5);
  const std::string joints = string_argument(arguments, 6);
  unsigned cap_style = 0;
  if (caps == "none") {
    cap_style = 1;
  } else if (caps == "square") {
    cap_style = 2;
  }
  unsigned join_style = 0;
  if (joints == "bevel") {
    join_style = 1;
  } else if (joints == "miter") {
    join_style = 2;
  }
  line.flags = static_cast<std::uint16_t>((cap_style << 14) | (join_style << 12));
  if (join_style == 2) {
    const double limit = number_argument(arguments, 7, 3.0);
    const double scaled = std::isnan(limit) ? 3.0 * 256.0 : limit * 256.0;
    line.miter_limit = static_cast<std::uint16_t>(std::lround(std::clamp(scaled, 0.0, 65535.0)));
  }
  return line;
}

struct GraphicsPen {
  std::int32_t x{0};
  std::int32_t y{0};
};

void emit_move(std::vector<swf::SwfShapeRecord>& records, GraphicsPen& pen, double x, double y) {
  swf::SwfShapeRecord record;
  record.kind = swf::SwfShapeRecordKind::move;
  record.x = twip_coord(x);
  record.y = twip_coord(y);
  pen.x = record.x;
  pen.y = record.y;
  records.push_back(record);
}

std::int32_t twip_delta(std::int32_t from, std::int32_t to) {
  // A clamped pen and a clamped end must still subtract without wrapping.
  const std::int64_t delta = static_cast<std::int64_t>(to) - static_cast<std::int64_t>(from);
  if (delta >= 2147483647LL) {
    return INT32_MAX;
  }
  if (delta <= -2147483648LL) {
    return INT32_MIN;
  }
  return static_cast<std::int32_t>(delta);
}

void emit_edge(std::vector<swf::SwfShapeRecord>& records, GraphicsPen& pen,
               swf::SwfShapeRecordKind kind, double control_x, double control_y, double x,
               double y) {
  // Deltas must run from the quantized pen, so the reader rebuilds the emitted positions exactly.
  const std::int32_t control_twip_x = twip_coord(control_x);
  const std::int32_t control_twip_y = twip_coord(control_y);
  const std::int32_t end_twip_x = twip_coord(x);
  const std::int32_t end_twip_y = twip_coord(y);
  const bool curved = kind == swf::SwfShapeRecordKind::curved_edge;
  swf::SwfShapeRecord record;
  record.kind = kind;
  record.control_x = twip_delta(pen.x, control_twip_x);
  record.control_y = twip_delta(pen.y, control_twip_y);
  record.x = twip_delta(curved ? control_twip_x : pen.x, end_twip_x);
  record.y = twip_delta(curved ? control_twip_y : pen.y, end_twip_y);
  pen.x = end_twip_x;
  pen.y = end_twip_y;
  // A stroke-only edge must still be recorded, since the reader's pen follows every edge record.
  records.push_back(record);
}

void emit_fill_break(std::vector<swf::SwfShapeRecord>& records, std::uint16_t fill1) {
  // Fill state must ride a style record, matching what the SWF parser emits for real shapes.
  swf::SwfShapeRecord record;
  record.kind = swf::SwfShapeRecordKind::style_change;
  record.has_fill0 = true;
  record.has_fill1 = true;
  record.fill0 = 0;
  record.fill1 = fill1;
  records.push_back(record);
}

void emit_line_break(std::vector<swf::SwfShapeRecord>& records, std::uint16_t line) {
  swf::SwfShapeRecord record;
  record.kind = swf::SwfShapeRecordKind::style_change;
  record.has_line = true;
  record.line = line;
  records.push_back(record);
}

// A recorded path must become edge records, because the snapshot and hit test read records.
swf::SwfShape graphics_shape(const GraphicsObject& graphics) {
  swf::SwfShape shape;
  std::vector<swf::SwfShapeRecord>& records = shape.records;
  bool filling = false;
  std::uint16_t fill_index = 0;
  std::uint16_t line_index = 0;
  std::uint16_t active_line = 0;
  GraphicsPen pen;
  GraphicsPen subpath;
  auto edge_to = [&](double x, double y) {
    emit_edge(records, pen, swf::SwfShapeRecordKind::straight_edge, 0.0, 0.0, x, y);
  };
  // An open fill must close back to its subpath start, unstroked, before it ends or restarts.
  auto close_subpath = [&]() {
    if (filling && (pen.x != subpath.x || pen.y != subpath.y)) {
      if (active_line != 0) emit_line_break(records, 0);
      edge_to(subpath.x / k_twips_per_pixel, subpath.y / k_twips_per_pixel);
      if (active_line != 0) emit_line_break(records, active_line);
    }
  };
  auto begin_subpath = [&](double x, double y) {
    close_subpath();
    emit_move(records, pen, x, y);
    subpath = pen;
  };
  for (const GraphicsCommand& command : graphics.commands) {
    switch (command.op) {
      case 0:
      case 1:
        close_subpath();
        filling = true;
        subpath = pen;
        // Each fill must take its own style, so overlapping fills union instead of cancelling.
        if (fill_index < 0xFFFFu) {
          ++fill_index;
          shape.fill_styles.push_back(command.fill.value_or(swf::SwfFillStyle{}));
        }
        emit_fill_break(records, fill_index);
        break;
      case 2:
        close_subpath();
        filling = false;
        emit_fill_break(records, 0);
        break;
      case 3: {
        std::uint16_t next_line = 0;
        if (command.line && line_index < 0xFFFFu) {
          ++line_index;
          shape.line_styles.push_back(*command.line);
          next_line = line_index;
        }
        active_line = next_line;
        emit_line_break(records, next_line);
        break;
      }
      case 4:
        begin_subpath(command.values[0], command.values[1]);
        break;
      case 5:
        edge_to(command.values[0], command.values[1]);
        break;
      case 6:
        emit_edge(records, pen, swf::SwfShapeRecordKind::curved_edge, command.values[0],
                  command.values[1], command.values[2], command.values[3]);
        break;
      case 7: {
        const double x = command.values[0];
        const double y = command.values[1];
        const double right = x + command.values[2];
        const double bottom = y + command.values[3];
        begin_subpath(x, y);
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
        begin_subpath(x + radius, y);
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
        begin_subpath(x + cut_x, y);
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
  close_subpath();
  return shape;
}

const GraphicsObject* drawn_graphics_of(const DisplayObject& object) {
  if (!object.graphics().is_object()) {
    return nullptr;
  }
  const Object& graphics_value = *object.graphics().as_object();
  const Class* klass = graphics_value.instance_class();
  if (klass == nullptr || klass->name != "flash.display::Graphics") {
    return nullptr;
  }
  return static_cast<const GraphicsObject*>(&graphics_value);
}

void rebuild_geometry(const GraphicsObject& graphics) {
  // Graphics records must stay twips, so sub-pixel drawing survives the pixel conversion.
  graphics.geometry = std::make_shared<const ShapeGeometry>(
      geometry_from_shape_records(graphics_shape(graphics).records, k_twips_per_pixel));
}

void build_art(const GraphicsObject& graphics) {
  graphics.art = std::make_shared<const swf::ArtShape>(
      swf::shape_art(graphics_shape(graphics), 1.0 / k_twips_per_pixel));
}

}  // namespace

GraphicsObject::GraphicsObject(Class* graphics_class, std::uint64_t graphics_key)
    : Object(ObjectKind::Object), key(graphics_key) {
  if (graphics_class != nullptr) {
    set_instance_class(graphics_class);
    if (graphics_class->prototype_object != nullptr) {
      set_prototype(graphics_class->prototype_object);
    }
  }
  set_dynamic(true);
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

void record_graphics(const Value& self, std::uint8_t op, const std::vector<Value>& arguments) {
  GraphicsObject* graphics = graphics_self(self);
  if (graphics == nullptr) {
    return;
  }
  GraphicsCommand command;
  command.op = op;
  std::size_t number_index = 0;
  for (const Value& argument : arguments) {
    if (argument.is_number_like() && number_index < 6) {
      command.values[number_index++] = argument.as_number();
    }
  }
  if (op == 0) {
    command.fill = solid_fill_of(arguments);
  } else if (op == 1) {
    command.fill = gradient_fill_of(arguments);
  } else if (op == 3) {
    command.line = line_style_of(arguments);
  }
  graphics->commands.push_back(command);
  graphics->revision += 1;
  graphics->geometry.reset();
  graphics->art.reset();
}

void clear_graphics(const Value& self) {
  if (GraphicsObject* graphics = graphics_self(self)) {
    graphics->commands.clear();
    graphics->geometry.reset();
    graphics->art.reset();
    graphics->revision += 1;
  }
}

std::shared_ptr<const ShapeGeometry> drawn_geometry_of(const DisplayObject& object) {
  static const std::shared_ptr<const ShapeGeometry> kNone =
      std::make_shared<const ShapeGeometry>();
  const GraphicsObject* graphics = drawn_graphics_of(object);
  if (graphics == nullptr) {
    return kNone;
  }
  if (graphics->geometry == nullptr) {
    rebuild_geometry(*graphics);
  }
  return graphics->geometry;
}

DrawnArt drawn_art_of(const DisplayObject& object) {
  DrawnArt drawn;
  const GraphicsObject* graphics = drawn_graphics_of(object);
  if (graphics == nullptr) {
    return drawn;
  }
  if (graphics->art == nullptr) {
    build_art(*graphics);
  }
  drawn.key = graphics->key;
  drawn.revision = graphics->revision;
  drawn.shape = graphics->art;
  return drawn;
}

}  // namespace fo4::avm2
