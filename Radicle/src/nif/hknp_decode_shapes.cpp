#include "hknp_decode_internal.hpp"

#include "fo4/error.hpp"

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace fo4::nif::hknp_detail {
namespace {

constexpr std::size_t k_instance_stride = 0x80;
constexpr std::size_t k_instance_child = 0x50;
// Constraint: the capsule radius is the stored convex radius divided by 0.9801.
constexpr float k_capsule_core_factor = 0.9801f;
constexpr float k_uniform_point_tolerance = 1.0e-5f;
constexpr int k_max_shape_depth = 32;

HknpShape base_shape(const HknpPackObject& object, std::size_t index, const std::string& class_name,
                     HknpShapeKind kind) {
  HknpShape shape;
  shape.kind = kind;
  shape.class_name = class_name;
  shape.object = index;
  shape.shape_flags = u32_at(object, 0x10);
  shape.convex_radius = f32_at(object, 0x14);
  shape.material_crc = u32_at(object, 0x18);
  return shape;
}

std::vector<HknpVec3> read_points(const HknpPackObject& object, const ArrayRef& array) {
  std::vector<HknpVec3> points;
  points.reserve(array.count);
  for (std::uint32_t i = 0; i < array.count; ++i) {
    points.push_back(vec3_at(object, array.offset + std::size_t{16} * i));
  }
  return points;
}

bool is_single_point(const std::vector<HknpVec3>& points) {
  if (points.empty()) {
    return false;
  }
  for (const auto& point : points) {
    for (std::size_t k = 0; k < 3; ++k) {
      if (std::fabs(point[k] - points.front()[k]) > k_uniform_point_tolerance) {
        return false;
      }
    }
  }
  return true;
}

void read_polytope(const HknpPackObject& object, HknpShape& shape) {
  const ArrayRef planes = rel_array_at(object, 0x40);
  for (std::uint32_t i = 0; i < planes.count; ++i) {
    shape.planes.push_back(vec4_at(object, planes.offset + std::size_t{16} * i));
  }
  const ArrayRef faces = rel_array_at(object, 0x44);
  const ArrayRef indices = rel_array_at(object, 0x48);
  for (std::uint32_t f = 0; f < faces.count; ++f) {
    const std::size_t entry = faces.offset + std::size_t{4} * f;
    const std::size_t first = u16_at(object, entry);
    const std::size_t num = u8_at(object, entry + 2);
    std::vector<std::uint8_t> loop;
    for (std::size_t k = 0; k < num; ++k) {
      const std::size_t slot = first + k;
      if (slot >= indices.count) {
        break;
      }
      loop.push_back(u8_at(object, indices.offset + slot));
    }
    if (loop.size() >= 3) {
      shape.faces.push_back(std::move(loop));
    }
  }
}

void attach_mass_properties(const HknpPackfile& pack, const HknpPackObject& object, HknpShape& shape) {
  const auto reference = global_target(pack, object, 0x20);
  if (!reference) {
    return;
  }
  const auto props = global_target(pack, pack.objects[*reference], 0x10);
  if (!props || pack.objects[*props].class_name != "hknpShapeMassProperties") {
    return;
  }
  const HknpPackObject& mass = pack.objects[*props];
  shape.has_mass_properties = true;
  shape.mass_properties.center = packed_vec3_at(mass, 0x10);
  shape.mass_properties.inertia_raw = packed_vec3_at(mass, 0x18);
  shape.mass_properties.volume = f32_at(mass, 0x28);
  shape.mass_properties.mass = f32_at(mass, 0x2c);
}

HknpShape decode_convex_like(const HknpPackfile& pack, std::size_t index) {
  const HknpPackObject& object = pack.objects[index];
  const std::string& class_name = object.class_name;
  HknpShape shape = base_shape(object, index, class_name, HknpShapeKind::convex);
  if (class_name == "hknpCapsuleShape") {
    shape.kind = HknpShapeKind::capsule;
    shape.point_a = vec3_at(object, 0x50);
    shape.point_b = vec3_at(object, 0x60);
    shape.radius = shape.convex_radius / k_capsule_core_factor;
  } else {
    shape.vertices = read_points(object, rel_array_at(object, 0x30));
    if (class_name == "hknpSphereShape" && is_single_point(shape.vertices)) {
      shape.kind = HknpShapeKind::sphere;
      shape.point_a = shape.vertices.front();
      shape.radius = shape.convex_radius;
      shape.vertices.clear();
    } else if (class_name != "hknpConvexShape") {
      read_polytope(object, shape);
    }
  }
  attach_mass_properties(pack, object, shape);
  return shape;
}

int add_shape(DecodeContext& ctx, HknpShape shape) {
  ctx.sys.shapes.push_back(std::move(shape));
  return static_cast<int>(ctx.sys.shapes.size()) - 1;
}

HknpShape decode_compound(DecodeContext& ctx, std::size_t index) {
  const HknpPackObject& source = ctx.pack.objects[index];
  HknpShape shape = base_shape(source, index, source.class_name, HknpShapeKind::compound);
  const auto instances = array_at(source, 0x60);
  if (!instances) {
    return shape;
  }
  for (std::uint32_t i = 0; i < instances->count; ++i) {
    const std::size_t at = instances->offset + k_instance_stride * i;
    HknpInstance instance;
    for (std::size_t row = 0; row < 3; ++row) {
      instance.rotation_rows[row] = vec3_at(source, at + 16 * row);
    }
    instance.translation = vec3_at(source, at + 0x30);
    instance.scale = vec3_at(source, at + 0x40);
    if (const auto child = global_target(ctx.pack, source, at + k_instance_child)) {
      instance.child_object = *child;
      instance.shape = decode_shape(ctx, *child);
    }
    shape.instances.push_back(instance);
  }
  return shape;
}

}  // namespace

int decode_shape(DecodeContext& ctx, std::size_t object) {
  int& memo = ctx.shape_of_object.at(object);
  if (memo == k_shape_in_progress) {
    throw fo4::parse_error(0, "shape object references itself");
  }
  if (memo != k_shape_unvisited) {
    return memo;
  }
  if (ctx.depth >= k_max_shape_depth) {
    throw fo4::parse_error(0, "shape nesting is deeper than the decoder allows");
  }
  ++ctx.depth;
  memo = k_shape_in_progress;
  const HknpPackObject& source = ctx.pack.objects[object];
  const std::string& cls = source.class_name;
  int result = k_shape_unsupported;
  if (cls == "hknpConvexPolytopeShape" || cls == "hknpConvexShape" || cls == "hknpSphereShape" ||
      cls == "hknpCapsuleShape") {
    result = add_shape(ctx, decode_convex_like(ctx.pack, object));
  } else if (cls == "hknpScaledConvexShape") {
    HknpShape shape = base_shape(source, object, cls, HknpShapeKind::scaled_convex);
    shape.scale = vec3_at(source, 0x40);
    if (const auto inner = global_target(ctx.pack, source, 0x30)) {
      shape.child = decode_shape(ctx, *inner);
    }
    result = add_shape(ctx, std::move(shape));
  } else if (cls == "hknpDynamicCompoundShape" || cls == "hknpStaticCompoundShape") {
    result = add_shape(ctx, decode_compound(ctx, object));
  } else {
    ctx.sys.unsupported.push_back(HknpUnsupportedShape{cls, object});
  }
  memo = result;
  ctx.depth -= 1;
  return result;
}

}  // namespace fo4::nif::hknp_detail
