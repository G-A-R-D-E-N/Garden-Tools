#include "gltf_frame.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <stdexcept>
#include <string>
#include <vector>

namespace fo4::nif::gltf_detail {
namespace {

using import_detail::Vec3;

constexpr double k_metres_per_nif_unit = 0.0142875;
constexpr double k_tolerance = 1e-4;
constexpr std::array<double, 9> k_gltf_to_nif_axes{1.0, 0.0, 0.0, 0.0, 0.0, -1.0, 0.0, 1.0, 0.0};

double axis(std::size_t row, std::size_t col) { return k_gltf_to_nif_axes[row * 3 + col]; }

double column_length(const Matrix& m, std::size_t col) {
  return std::sqrt(m[col * 4] * m[col * 4] + m[col * 4 + 1] * m[col * 4 + 1] + m[col * 4 + 2] * m[col * 4 + 2]);
}

double column_dot(const Matrix& m, std::size_t a, std::size_t b) {
  return m[a * 4] * m[b * 4] + m[a * 4 + 1] * m[b * 4 + 1] + m[a * 4 + 2] * m[b * 4 + 2];
}

std::vector<double> numbers(const nlohmann::json& node, const char* key, std::size_t count,
                            std::vector<double> fallback) {
  if (!node.contains(key)) return fallback;
  auto values = node.at(key).get<std::vector<double>>();
  if (values.size() != count) throw std::runtime_error(std::string("glTF ") + key + " must have " + std::to_string(count) + " numbers");
  return values;
}

}

Matrix multiply(const Matrix& a, const Matrix& b) {
  Matrix out{};
  for (std::size_t col = 0; col < 4; ++col) {
    for (std::size_t row = 0; row < 4; ++row) {
      for (std::size_t k = 0; k < 4; ++k) out[col * 4 + row] += a[k * 4 + row] * b[col * 4 + k];
    }
  }
  return out;
}

Vec3 apply(const Matrix& m, const Vec3& p) {
  Vec3 out{};
  for (std::size_t row = 0; row < 3; ++row) {
    out[row] = static_cast<float>(m[row] * p[0] + m[4 + row] * p[1] + m[8 + row] * p[2] + m[12 + row]);
  }
  return out;
}

Matrix node_matrix(const nlohmann::json& node) {
  if (node.contains("matrix")) {
    const auto values = numbers(node, "matrix", 16, {});
    Matrix m{};
    std::copy(values.begin(), values.end(), m.begin());
    return m;
  }
  const auto t = numbers(node, "translation", 3, {0.0, 0.0, 0.0});
  const auto q = numbers(node, "rotation", 4, {0.0, 0.0, 0.0, 1.0});
  const auto s = numbers(node, "scale", 3, {1.0, 1.0, 1.0});
  const double x = q[0];
  const double y = q[1];
  const double z = q[2];
  const double w = q[3];
  const std::array<std::array<double, 3>, 3> rotation{{
      {1.0 - 2.0 * (y * y + z * z), 2.0 * (x * y + w * z), 2.0 * (x * z - w * y)},
      {2.0 * (x * y - w * z), 1.0 - 2.0 * (x * x + z * z), 2.0 * (y * z + w * x)},
      {2.0 * (x * z + w * y), 2.0 * (y * z - w * x), 1.0 - 2.0 * (x * x + y * y)},
  }};
  Matrix m{};
  for (std::size_t col = 0; col < 3; ++col) {
    for (std::size_t row = 0; row < 3; ++row) m[col * 4 + row] = rotation[col][row] * s[col];
    m[12 + col] = t[col];
  }
  m[15] = 1.0;
  return m;
}

Matrix Frame::conjugate(const Matrix& m) const {
  if (nif_units) return m;
  Matrix out = m;
  const double metres = k_metres_per_nif_unit;
  for (std::size_t row = 0; row < 3; ++row) {
    for (std::size_t col = 0; col < 3; ++col) {
      double value = 0.0;
      for (std::size_t i = 0; i < 3; ++i) {
        for (std::size_t j = 0; j < 3; ++j) value += axis(row, i) * m[j * 4 + i] * axis(col, j);
      }
      out[col * 4 + row] = value;
    }
    double translation = 0.0;
    for (std::size_t i = 0; i < 3; ++i) translation += axis(row, i) * m[12 + i];
    out[12 + row] = translation / metres;
  }
  return out;
}

Vec3 Frame::point(const Vec3& p) const {
  if (nif_units) return p;
  Vec3 out{};
  for (std::size_t row = 0; row < 3; ++row) {
    double value = 0.0;
    for (std::size_t i = 0; i < 3; ++i) value += axis(row, i) * p[i];
    out[row] = static_cast<float>(value / k_metres_per_nif_unit);
  }
  return out;
}

Vec3 Frame::direction(const Vec3& d) const {
  if (nif_units) return d;
  Vec3 out{};
  for (std::size_t row = 0; row < 3; ++row) {
    double value = 0.0;
    for (std::size_t i = 0; i < 3; ++i) value += axis(row, i) * d[i];
    out[row] = static_cast<float>(value);
  }
  return out;
}

Transform Frame::transform(const Matrix& m, const std::string& what) const {
  const auto c = conjugate(m);
  for (const double value : c) {
    if (!std::isfinite(value)) throw std::runtime_error("glTF " + what + " has a non-finite transform");
  }
  const double scale = column_length(c, 0);
  if (scale <= 0.0) throw std::runtime_error("glTF " + what + " has a zero scale, which a NIF transform cannot hold");
  for (std::size_t col = 1; col < 3; ++col) {
    if (std::fabs(column_length(c, col) - scale) > k_tolerance * scale) {
      throw std::runtime_error("glTF " + what + " has a non-uniform scale, which a NIF transform cannot hold");
    }
  }
  for (std::size_t a = 0; a < 3; ++a) {
    for (std::size_t b = a; b < 3; ++b) {
      const double expected = a == b ? scale * scale : 0.0;
      if (std::fabs(column_dot(c, a, b) - expected) > k_tolerance * scale * scale) {
        throw std::runtime_error("glTF " + what + " is sheared, which a NIF transform cannot hold");
      }
    }
  }
  const double determinant = c[0] * (c[5] * c[10] - c[9] * c[6]) - c[4] * (c[1] * c[10] - c[9] * c[2]) +
                             c[8] * (c[1] * c[6] - c[5] * c[2]);
  if (determinant <= 0.0) throw std::runtime_error("glTF " + what + " is mirrored, which a NIF transform cannot hold");

  Transform xf;
  for (std::size_t row = 0; row < 3; ++row) {
    xf.t[row] = static_cast<float>(c[12 + row]);
    for (std::size_t col = 0; col < 3; ++col) xf.r[row * 3 + col] = static_cast<float>(c[col * 4 + row] / scale);
  }
  xf.s = static_cast<float>(scale);
  return xf;
}

}
