#pragma once

#include <array>
#include <cmath>

namespace fo4::hkx::gltf_math {

using Vec3 = std::array<double, 3>;
using Quat = std::array<double, 4>;  // x, y, z, w
// Invariant: row-major with row vectors (v * M), translation in the last row, as System.Numerics lays it out.
using Mat = std::array<double, 16>;

inline Mat identity() { return {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1}; }

inline Mat multiply(const Mat& a, const Mat& b) {
  Mat out{};
  for (int row = 0; row < 4; ++row) {
    for (int col = 0; col < 4; ++col) {
      double sum = 0.0;
      for (int k = 0; k < 4; ++k) sum += a[row * 4 + k] * b[k * 4 + col];
      out[row * 4 + col] = sum;
    }
  }
  return out;
}

inline Mat scale_matrix(const Vec3& s) { return {s[0], 0, 0, 0, 0, s[1], 0, 0, 0, 0, s[2], 0, 0, 0, 0, 1}; }

inline Mat translation_matrix(const Vec3& t) { return {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, t[0], t[1], t[2], 1}; }

// Constraint: +90 degrees about X in the row-vector convention, so glTF (x, y, z) becomes Havok (x, -z, y).
inline Mat rotation_x_quarter_turn() { return {1, 0, 0, 0, 0, 0, 1, 0, 0, -1, 0, 0, 0, 0, 0, 1}; }

inline Mat rotation_matrix(const Quat& q) {
  const double x = q[0], y = q[1], z = q[2], w = q[3];
  const double xx = x * x, yy = y * y, zz = z * z;
  const double xy = x * y, wz = w * z, xz = x * z, wy = w * y, yz = y * z, wx = w * x;
  return {1 - 2 * (yy + zz), 2 * (xy + wz),     2 * (xz - wy),     0,
          2 * (xy - wz),     1 - 2 * (zz + xx), 2 * (yz + wx),     0,
          2 * (xz + wy),     2 * (yz - wx),     1 - 2 * (yy + xx), 0,
          0,                 0,                 0,                 1};
}

inline Quat multiply(const Quat& a, const Quat& b) {
  const double cx = a[1] * b[2] - a[2] * b[1];
  const double cy = a[2] * b[0] - a[0] * b[2];
  const double cz = a[0] * b[1] - a[1] * b[0];
  const double dot = a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
  return {a[0] * b[3] + b[0] * a[3] + cx, a[1] * b[3] + b[1] * a[3] + cy, a[2] * b[3] + b[2] * a[3] + cz,
          a[3] * b[3] - dot};
}

inline Quat conjugate(const Quat& q) { return {-q[0], -q[1], -q[2], q[3]}; }

inline double dot(const Quat& a, const Quat& b) { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2] + a[3] * b[3]; }

inline Quat negate(const Quat& q) { return {-q[0], -q[1], -q[2], -q[3]}; }

inline bool usable_rotation(const Quat& q) {
  const double squared = dot(q, q);
  return std::isfinite(squared) && squared > 1e-12;
}

// Requires usable_rotation(q).
inline Quat normalised(const Quat& q) {
  const double length = std::sqrt(dot(q, q));
  return {q[0] / length, q[1] / length, q[2] / length, q[3] / length};
}

inline Quat axis_z(double angle) { return {0.0, 0.0, std::sin(angle * 0.5), std::cos(angle * 0.5)}; }

inline Quat slerp(const Quat& a, const Quat& b, double t) {
  double cosine = dot(a, b);
  bool flip = false;
  if (cosine < 0.0) {
    flip = true;
    cosine = -cosine;
  }
  double s1 = 0.0, s2 = 0.0;
  if (cosine > 1.0 - 1e-6) {
    s1 = 1.0 - t;
    s2 = flip ? -t : t;
  } else {
    const double omega = std::acos(cosine);
    const double inverse = 1.0 / std::sin(omega);
    s1 = std::sin((1.0 - t) * omega) * inverse;
    s2 = flip ? -std::sin(t * omega) * inverse : std::sin(t * omega) * inverse;
  }
  return {s1 * a[0] + s2 * b[0], s1 * a[1] + s2 * b[1], s1 * a[2] + s2 * b[2], s1 * a[3] + s2 * b[3]};
}

// Constraint: scale is the row length, rotation the normalised rows, no shear removal; false for zero, non-finite or mirrored input.
inline bool decompose(const Mat& m, Vec3& scale, Quat& rotation, Vec3& translation) {
  Mat r = m;
  for (int row = 0; row < 3; ++row) {
    const double length = std::sqrt(m[row * 4] * m[row * 4] + m[row * 4 + 1] * m[row * 4 + 1] +
                                    m[row * 4 + 2] * m[row * 4 + 2]);
    if (!std::isfinite(length) || length < 1e-9) return false;
    scale[row] = length;
    for (int col = 0; col < 3; ++col) r[row * 4 + col] = m[row * 4 + col] / length;
  }
  const double determinant = r[0] * (r[5] * r[10] - r[6] * r[9]) - r[1] * (r[4] * r[10] - r[6] * r[8]) +
                             r[2] * (r[4] * r[9] - r[5] * r[8]);
  if (!(determinant > 0.0)) return false;

  const double m11 = r[0], m12 = r[1], m13 = r[2], m21 = r[4], m22 = r[5], m23 = r[6], m31 = r[8], m32 = r[9],
               m33 = r[10];
  const double trace = m11 + m22 + m33;
  double x, y, z, w;
  if (trace > 0.0) {
    double s = std::sqrt(trace + 1.0);
    w = s * 0.5;
    s = 0.5 / s;
    x = (m23 - m32) * s;
    y = (m31 - m13) * s;
    z = (m12 - m21) * s;
  } else if (m11 >= m22 && m11 >= m33) {
    const double s = std::sqrt(1.0 + m11 - m22 - m33);
    const double inverse = 0.5 / s;
    x = 0.5 * s;
    y = (m12 + m21) * inverse;
    z = (m13 + m31) * inverse;
    w = (m23 - m32) * inverse;
  } else if (m22 > m33) {
    const double s = std::sqrt(1.0 + m22 - m11 - m33);
    const double inverse = 0.5 / s;
    x = (m21 + m12) * inverse;
    y = 0.5 * s;
    z = (m32 + m23) * inverse;
    w = (m31 - m13) * inverse;
  } else {
    const double s = std::sqrt(1.0 + m33 - m11 - m22);
    const double inverse = 0.5 / s;
    x = (m31 + m13) * inverse;
    y = (m32 + m23) * inverse;
    z = 0.5 * s;
    w = (m12 - m21) * inverse;
  }
  rotation = {x, y, z, w};
  if (!usable_rotation(rotation)) return false;
  rotation = normalised(rotation);
  translation = {m[12], m[13], m[14]};
  return std::isfinite(translation[0]) && std::isfinite(translation[1]) && std::isfinite(translation[2]);
}

}  // namespace fo4::hkx::gltf_math
