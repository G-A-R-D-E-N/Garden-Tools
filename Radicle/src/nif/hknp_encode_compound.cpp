#include "fo4/nif/hknp_encode.hpp"

#include "hknp_encode_internal.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace fo4::nif {
namespace {

using namespace hknp_detail;

// Constraint: a dynamic compound is 0x02020004 at +0x10; the static compound differs in byte +0x12 and its data object.
constexpr std::uint32_t k_dynamic_compound_flags = 0x02020004U;
constexpr std::uint32_t k_half_w = 0x3f000000U;
constexpr std::size_t k_instance_base = 0xd0;
constexpr std::size_t k_instance_stride = 0x80;
constexpr std::size_t k_node_base = 0x40;
constexpr std::size_t k_node_stride = 0x20;
constexpr std::size_t k_max_children = 4096;
constexpr float k_empty_min = 3.402823466e+38f;
constexpr float k_empty_max = -3.402823466e+38f;

struct Box {
  HknpVec3 low{k_empty_min, k_empty_min, k_empty_min};
  HknpVec3 high{k_empty_max, k_empty_max, k_empty_max};
};

void expand(Box& box, const HknpVec3& point) {
  for (std::size_t k = 0; k < 3; ++k) {
    box.low[k] = std::min(box.low[k], point[k]);
    box.high[k] = std::max(box.high[k], point[k]);
  }
}

void merge(Box& into, const Box& other) {
  for (std::size_t k = 0; k < 3; ++k) {
    into.low[k] = std::min(into.low[k], other.low[k]);
    into.high[k] = std::max(into.high[k], other.high[k]);
  }
}

bool box_is_finite(const Box& box) {
  for (std::size_t k = 0; k < 3; ++k) {
    if (!std::isfinite(box.low[k]) || !std::isfinite(box.high[k]) || box.low[k] > box.high[k]) return false;
  }
  return true;
}

Box shape_box(const HknpShape& shape) {
  Box box;
  switch (shape.kind) {
    case HknpShapeKind::sphere:
      for (std::size_t k = 0; k < 3; ++k) {
        box.low[k] = shape.point_a[k] - shape.radius;
        box.high[k] = shape.point_a[k] + shape.radius;
      }
      return box;
    case HknpShapeKind::capsule:
      for (std::size_t k = 0; k < 3; ++k) {
        box.low[k] = std::min(shape.point_a[k], shape.point_b[k]) - shape.radius;
        box.high[k] = std::max(shape.point_a[k], shape.point_b[k]) + shape.radius;
      }
      return box;
    case HknpShapeKind::convex:
      if (shape.vertices.empty()) {
        throw std::invalid_argument("a convex compound child needs vertices to bound");
      }
      for (const auto& vertex : shape.vertices) expand(box, vertex);
      for (std::size_t k = 0; k < 3; ++k) {
        box.low[k] -= shape.convex_radius;
        box.high[k] += shape.convex_radius;
      }
      return box;
    case HknpShapeKind::compressed_mesh:
      if (shape.mesh_triangles.empty()) {
        throw std::invalid_argument("a compressed mesh compound child needs triangles to bound");
      }
      for (const auto& triangle : shape.mesh_triangles) {
        const std::size_t used = triangle.quad ? 4 : 3;
        for (std::size_t k = 0; k < used; ++k) {
          if (triangle.indices[k] >= shape.mesh_vertices.size()) {
            throw std::invalid_argument("a compressed mesh compound child names a vertex outside its list");
          }
          expand(box, shape.mesh_vertices[triangle.indices[k]]);
        }
      }
      return box;
    case HknpShapeKind::scaled_convex:
      throw std::invalid_argument("a scaled convex compound child is not authored; scale the instance instead");
    case HknpShapeKind::compound:
      throw std::invalid_argument("a compound cannot hold another compound");
  }
  throw std::invalid_argument("unknown compound child shape kind");
}

Box placed_box(const Box& box, const HknpInstance& placement) {
  Box out;
  for (int corner = 0; corner < 8; ++corner) {
    HknpVec3 point{(corner & 1) != 0 ? box.high[0] : box.low[0], (corner & 2) != 0 ? box.high[1] : box.low[1],
                   (corner & 4) != 0 ? box.high[2] : box.low[2]};
    for (std::size_t k = 0; k < 3; ++k) point[k] *= placement.scale[k];
    const HknpVec3 rotated{dot(placement.rotation_rows[0], point), dot(placement.rotation_rows[1], point),
                           dot(placement.rotation_rows[2], point)};
    expand(out, added(rotated, placement.translation));
  }
  return out;
}

bool placement_is_finite(const HknpInstance& placement) {
  for (const auto& row : placement.rotation_rows) {
    for (const float value : row) {
      if (!std::isfinite(value)) return false;
    }
  }
  for (std::size_t k = 0; k < 3; ++k) {
    if (!std::isfinite(placement.translation[k]) || !std::isfinite(placement.scale[k])) return false;
    if (placement.scale[k] == 0.0f) return false;
  }
  return true;
}

struct TreeNode {
  Box box;
  std::size_t parent{0};
  std::size_t left{0};
  std::size_t right{0};
  std::size_t instance{0};
  bool leaf{false};
};

// Constraint: node 0 of the record array is padding, so the root is record 1 and a node's left child is always record i + 1.
std::size_t build_tree(std::vector<TreeNode>& nodes, std::vector<std::size_t>& order, const std::vector<Box>& boxes,
                       std::size_t first, std::size_t last, std::size_t parent) {
  const std::size_t self = nodes.size();
  TreeNode node;
  node.parent = parent;
  for (std::size_t k = first; k < last; ++k) merge(node.box, boxes[order[k]]);
  nodes.push_back(node);
  if (last - first == 1) {
    nodes[self].leaf = true;
    nodes[self].instance = order[first];
    return self;
  }
  std::size_t axis = 0;
  for (std::size_t k = 1; k < 3; ++k) {
    if (nodes[self].box.high[k] - nodes[self].box.low[k] >
        nodes[self].box.high[axis] - nodes[self].box.low[axis]) {
      axis = k;
    }
  }
  // Ordering: a stable sort keeps tied centres in input order.
  std::stable_sort(order.begin() + static_cast<std::ptrdiff_t>(first), order.begin() + static_cast<std::ptrdiff_t>(last),
                   [&boxes, axis](std::size_t a, std::size_t b) {
                     return boxes[a].low[axis] + boxes[a].high[axis] < boxes[b].low[axis] + boxes[b].high[axis];
                   });
  const std::size_t mid = first + (last - first) / 2;
  nodes[self].left = build_tree(nodes, order, boxes, first, mid, self);
  nodes[self].right = build_tree(nodes, order, boxes, mid, last, self);
  return self;
}

HknpPackObject make_compound_data(const std::vector<Box>& boxes, std::vector<std::size_t>& leaf_of_instance) {
  const std::size_t count = boxes.size();
  const std::size_t records = 2 * count + 1;
  HknpPackObject object = make_object("hknpDynamicCompoundShapeData", k_node_base + k_node_stride * records);
  put_u32(object, 0x18, static_cast<std::uint32_t>(records));
  put_u32(object, 0x1c, 0x80000000U | static_cast<std::uint32_t>(records));
  put_u32(object, 0x20, static_cast<std::uint32_t>(2 * count));
  put_u32(object, 0x28, static_cast<std::uint32_t>(count));
  put_u32(object, 0x30, 1);
  add_local(object, 0x10, static_cast<std::uint32_t>(k_node_base));

  std::vector<TreeNode> nodes(1);
  std::vector<std::size_t> order(count);
  for (std::size_t i = 0; i < count; ++i) order[i] = i;
  build_tree(nodes, order, boxes, 0, count, 0);

  leaf_of_instance.assign(count, 0);
  for (std::size_t i = 1; i < nodes.size(); ++i) {
    const TreeNode& node = nodes[i];
    const std::size_t at = k_node_base + k_node_stride * i;
    put_vec3(object, at, node.box.low);
    put_u32(object, at + 0x0c, k_half_w | static_cast<std::uint32_t>(node.parent));
    put_vec3(object, at + 0x10, node.box.high);
    if (node.leaf) {
      put_u16(object, at + 0x1c, 0);
      put_u16(object, at + 0x1e, to_u16(node.instance));
      leaf_of_instance[node.instance] = i;
    } else {
      put_u16(object, at + 0x1c, to_u16(i + 1));
      put_u16(object, at + 0x1e, to_u16(node.right));
    }
  }
  return object;
}

void put_instance(HknpPackObject& object, std::size_t at, const HknpInstance& placement, std::size_t leaf) {
  for (std::size_t row = 0; row < 3; ++row) put_vec3(object, at + 0x10 * row, placement.rotation_rows[row]);
  put_f32(object, at + 0x0c, 0.5f);
  put_f32(object, at + 0x1c, 0.0f);
  put_f32(object, at + 0x2c, 0.5f);
  put_vec3(object, at + 0x30, placement.translation);
  put_u32(object, at + 0x3c, k_half_w | static_cast<std::uint32_t>(leaf));
  put_vec3(object, at + 0x40, placement.scale);
  put_f32(object, at + 0x4c, 1.0f);
  put_u32(object, at + 0x58, 0xffffffffU);
}

}  // namespace

namespace hknp_detail {

std::uint32_t append_compound_shape(HknpPackfile& pack, const std::vector<HknpCompoundChild>& children) {
  if (children.empty() || children.size() > k_max_children) {
    throw std::invalid_argument("a compound needs between 1 and 4096 children");
  }
  std::vector<Box> placed_boxes;
  Box compound_box;
  placed_boxes.reserve(children.size());
  for (const auto& child : children) {
    if (!placement_is_finite(child.placement)) {
      throw std::invalid_argument("a compound instance transform must be finite with a non-zero scale");
    }
    const Box local = shape_box(child.shape);
    if (!box_is_finite(local)) {
      throw std::invalid_argument("a compound child has no finite bounds");
    }
    const Box placed = placed_box(local, child.placement);
    placed_boxes.push_back(placed);
    merge(compound_box, placed);
  }

  const std::size_t compound_index = pack.objects.size();
  pack.objects.push_back(make_object("hknpDynamicCompoundShape", 0));

  std::vector<std::size_t> leaf_of_instance;
  HknpPackObject data = make_compound_data(placed_boxes, leaf_of_instance);

  std::vector<std::uint32_t> child_objects;
  child_objects.reserve(children.size());
  for (const auto& child : children) child_objects.push_back(append_shape(pack, child.shape));

  const std::size_t data_index = pack.objects.size();
  pack.objects.push_back(std::move(data));

  HknpPackObject compound =
      make_object("hknpDynamicCompoundShape", k_instance_base + k_instance_stride * children.size());
  put_u32(compound, 0x10, k_dynamic_compound_flags);
  put_f32(compound, 0x14, 0.0f);
  put_u32(compound, 0x18, children.front().shape.material_crc);
  put_u32(compound, 0x30, 0xffffffffU);
  put_u32(compound, 0x44, 0x80000000U);
  put_u32(compound, 0x54, 0x80000000U);
  put_u32(compound, 0x58, 0xffffffffU);
  put_array_descriptor(compound, 0x60, static_cast<std::uint32_t>(children.size()),
                       0x80000000U | static_cast<std::uint32_t>(children.size()));
  add_local(compound, 0x60, static_cast<std::uint32_t>(k_instance_base));
  put_u32(compound, 0x70, 0xffffffffU);
  put_vec3(compound, 0x80, compound_box.low);
  put_f32(compound, 0x8c, 0.5f);
  put_vec3(compound, 0x90, compound_box.high);
  put_u32(compound, 0xa0, 1);
  for (std::size_t i = 0; i < children.size(); ++i) {
    put_instance(compound, k_instance_base + k_instance_stride * i, children[i].placement, leaf_of_instance[i]);
    add_global(compound, static_cast<std::uint32_t>(k_instance_base + k_instance_stride * i + 0x50),
               static_cast<std::uint32_t>(child_objects[i]));
  }
  add_global(compound, 0xc0, static_cast<std::uint32_t>(data_index));
  pack.objects[compound_index] = std::move(compound);
  return static_cast<std::uint32_t>(compound_index);
}

}  // namespace hknp_detail

HknpPackfile author_hknp_compound_body(const std::vector<HknpCompoundChild>& children, const HknpStaticBody& body) {
  HknpSystemBody spec;
  spec.children = children;
  spec.position = body.position;
  spec.orientation = body.orientation;
  spec.packed_filter = body.packed_filter;
  return author_hknp_system({std::move(spec)});
}
}  // namespace fo4::nif
