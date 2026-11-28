#pragma once

#include "fo4/nif/nif.hpp"
#include "import_mesh.hpp"

#include <nlohmann/json.hpp>

#include <array>
#include <string>

namespace fo4::nif::gltf_detail {

using Matrix = std::array<double, 16>;

Matrix multiply(const Matrix& a, const Matrix& b);
import_detail::Vec3 apply(const Matrix& m, const import_detail::Vec3& p);
// Requires a matrix or translation, rotation and scale; the node's local transform, not its parent's.
Matrix node_matrix(const nlohmann::json& node);

struct Frame {
  bool nif_units{false};

  Matrix conjugate(const Matrix& m) const;
  import_detail::Vec3 point(const import_detail::Vec3& p) const;
  import_detail::Vec3 direction(const import_detail::Vec3& d) const;
  // Requires a rigid transform with one uniform positive scale; `what` names the node or bone in the refusal.
  Transform transform(const Matrix& m, const std::string& what) const;
};

}
