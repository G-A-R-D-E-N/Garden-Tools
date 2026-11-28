#pragma once

#include <fo4/avm2/class.hpp>
#include <fo4/avm2/object.hpp>
#include <fo4/avm2/shape_geometry.hpp>
#include <fo4/avm2/value.hpp>
#include <fo4/swf/swf.hpp>
#include <fo4/swf/swf_art.hpp>

#include <cstdint>
#include <memory>
#include <optional>
#include <vector>

namespace fo4::avm2 {

/* A draw command must be recorded, not executed, because rendering stays with GardenEngine. */
struct GraphicsCommand {
  std::uint8_t op{0};
  double values[6]{0.0, 0.0, 0.0, 0.0, 0.0, 0.0};
  std::optional<swf::SwfFillStyle> fill;
  std::optional<swf::SwfLineStyle> line;
};

class GraphicsObject : public Object {
 public:
  GraphicsObject(Class* graphics_class, std::uint64_t graphics_key);

  std::vector<GraphicsCommand> commands;
  mutable std::shared_ptr<const ShapeGeometry> geometry;
  mutable std::shared_ptr<const swf::ArtShape> art;
  std::uint64_t key{0};
  std::uint64_t revision{0};
};

/* Requires any value; returns the Graphics object it holds, or null for anything else. */
GraphicsObject* graphics_self(const Value& self);

/* Requires a draw op from the command table; records the command, advances the revision and drops the cached shapes. */
void record_graphics(const Value& self, std::uint8_t op, const std::vector<Value>& arguments);

/* Requires a Graphics value; drops every recorded command and advances the revision. */
void clear_graphics(const Value& self);

}  // namespace fo4::avm2
