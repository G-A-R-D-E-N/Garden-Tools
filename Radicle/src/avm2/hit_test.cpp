#include <fo4/avm2/hit_test.hpp>

#include <fo4/avm2/shape_geometry.hpp>
#include <fo4/avm2/snapshot.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <memory>
#include <vector>

namespace fo4::avm2 {

namespace {

bool inverse_map(const SnapshotMatrix& matrix, double x, double y, double& out_x, double& out_y) {
  const double determinant = matrix.a * matrix.d - matrix.b * matrix.c;
  if (determinant == 0.0) {
    return false;
  }
  const double dx = x - matrix.tx;
  const double dy = y - matrix.ty;
  out_x = (matrix.d * dx - matrix.c * dy) / determinant;
  out_y = (-matrix.b * dx + matrix.a * dy) / determinant;
  return true;
}

bool has_size(const DisplayObject& object) {
  return object.width() > 0.0 && object.height() > 0.0;
}

bool inside_bounds(const DisplayObject& object, double x, double y) {
  return has_size(object) && x >= object.origin_x() && x <= object.origin_x() + object.width() &&
         y >= object.origin_y() && y <= object.origin_y() + object.height();
}

bool hits_shape_or_bounds(const DisplayObject& object, double x, double y, bool shape_flag) {
  if (!shape_flag) {
    return inside_bounds(object, x, y);
  }
  // The flag must test the drawing and the children, so no empty box claims and no child fill misses.
  const ShapeGeometry geometry = effective_shape_geometry(object);
  if (shape_contains(geometry, x, y)) {
    return true;
  }
  if (object.is_container()) {
    const auto& children = static_cast<const DisplayObjectContainer&>(object).children();
    for (const auto& child : children) {
      if (child == nullptr || !child->visible()) {
        continue;
      }
      double child_x = 0.0;
      double child_y = 0.0;
      if (!inverse_map(local_matrix(*child), x, y, child_x, child_y)) {
        continue;
      }
      if (hits_shape_or_bounds(*child, child_x, child_y, true)) {
        return true;
      }
    }
    return false;
  }
  // A leaf without geometry must keep its box, since text and empty shapes still hit it.
  return geometry.edges.empty() && inside_bounds(object, x, y);
}

bool chain_visible(const DisplayObject& object) {
  for (const DisplayObject* node = &object; node != nullptr; node = node->parent()) {
    if (!node->visible()) {
      return false;
    }
  }
  return true;
}

std::vector<const DisplayObject*> ancestry(const DisplayObject& object) {
  std::vector<const DisplayObject*> chain;
  for (const DisplayObject* node = &object; node != nullptr; node = node->parent()) {
    chain.push_back(node);
  }
  return chain;
}

struct StageBounds {
  double min_x{0.0};
  double min_y{0.0};
  double max_x{0.0};
  double max_y{0.0};
};

StageBounds stage_bounds(const DisplayObject& object) {
  const double left = object.origin_x();
  const double top = object.origin_y();
  const double right = left + object.width();
  const double bottom = top + object.height();
  std::array<double, 4> xs{left, right, left, right};
  std::array<double, 4> ys{top, top, bottom, bottom};
  for (const DisplayObject* node = &object; node != nullptr; node = node->parent()) {
    const SnapshotMatrix matrix = local_matrix(*node);
    for (std::size_t index = 0; index < xs.size(); ++index) {
      const double px = xs[index];
      const double py = ys[index];
      xs[index] = matrix.a * px + matrix.c * py + matrix.tx;
      ys[index] = matrix.b * px + matrix.d * py + matrix.ty;
    }
  }
  StageBounds bounds;
  bounds.min_x = *std::min_element(xs.begin(), xs.end());
  bounds.max_x = *std::max_element(xs.begin(), xs.end());
  bounds.min_y = *std::min_element(ys.begin(), ys.end());
  bounds.max_y = *std::max_element(ys.begin(), ys.end());
  return bounds;
}

std::shared_ptr<DisplayObject> deepest_at(const std::shared_ptr<DisplayObject>& node, double x,
                                          double y) {
  if (!node->visible()) {
    return nullptr;
  }
  double local_x = 0.0;
  double local_y = 0.0;
  if (!inverse_map(local_matrix(*node), x, y, local_x, local_y)) {
    return nullptr;
  }
  if (node->is_container() && node->mouse_children()) {
    const auto& children = static_cast<const DisplayObjectContainer&>(*node).children();
    for (auto child = children.rbegin(); child != children.rend(); ++child) {
      std::shared_ptr<DisplayObject> hit = deepest_at(*child, local_x, local_y);
      if (hit != nullptr) {
        return hit;
      }
    }
  }
  // A mouse pick must use the fill shape and the children, falling back to bounds only for a leaf.
  if (node->mouse_enabled() && hits_shape_or_bounds(*node, local_x, local_y, true)) {
    return node;
  }
  return nullptr;
}

}  // namespace

bool stage_to_local(const DisplayObject& object, double x, double y, double& local_x,
                    double& local_y) {
  const std::vector<const DisplayObject*> chain = ancestry(object);
  double px = x;
  double py = y;
  for (auto node = chain.rbegin(); node != chain.rend(); ++node) {
    double next_x = 0.0;
    double next_y = 0.0;
    if (!inverse_map(local_matrix(**node), px, py, next_x, next_y)) {
      return false;
    }
    px = next_x;
    py = next_y;
  }
  local_x = px;
  local_y = py;
  return true;
}

bool hit_test_point(const DisplayObject& object, double x, double y) {
  return hit_test_point(object, x, y, false);
}

bool hit_test_point(const DisplayObject& object, double x, double y, bool shape_flag) {
  double local_x = 0.0;
  double local_y = 0.0;
  return chain_visible(object) && stage_to_local(object, x, y, local_x, local_y) &&
         hits_shape_or_bounds(object, local_x, local_y, shape_flag);
}

bool hit_test_object(const DisplayObject& first, const DisplayObject& second) {
  if (!chain_visible(first) || !chain_visible(second) || !has_size(first) || !has_size(second)) {
    return false;
  }
  const StageBounds a = stage_bounds(first);
  const StageBounds b = stage_bounds(second);
  return a.min_x <= b.max_x && b.min_x <= a.max_x && a.min_y <= b.max_y && b.min_y <= a.max_y;
}

std::shared_ptr<DisplayObject> mouse_target_at(const std::shared_ptr<DisplayObject>& root, double x,
                                               double y) {
  if (root == nullptr) {
    return nullptr;
  }
  return deepest_at(root, x, y);
}

}  // namespace fo4::avm2
