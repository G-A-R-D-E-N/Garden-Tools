#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace fo4::nif::import_detail {

using Vec2 = std::array<float, 2>;
using Vec3 = std::array<float, 3>;
using Vec4 = std::array<float, 4>;
using Influence = std::array<std::uint8_t, 4>;

struct Mesh {
  std::string name;
  std::vector<Vec3> positions;
  std::vector<Vec3> normals;
  std::vector<Vec2> uvs;
  std::vector<Vec3> tangents;
  std::vector<Vec3> bitangents;
  std::vector<std::uint32_t> tris;
  std::vector<Vec4> colours;
  std::vector<Vec4> weights;
  std::vector<Influence> bones;
};

Mesh read_obj(std::string_view text, bool from_blender);
void add_tangent_frame(Mesh& mesh);
std::array<float, 4> bounding_sphere(const std::vector<Vec3>& positions);

}
