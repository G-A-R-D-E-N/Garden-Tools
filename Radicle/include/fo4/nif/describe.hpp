#pragma once

#include "fo4/nif/nif.hpp"

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace fo4::nif {

struct TextureSlot {
  std::size_t slot{0};
  std::string path;
};

// Invariant: inspect, geometry and the tree read the same shapes through read_shape, so a shape that parses there parses here.
struct InspectShape {
  std::string name;
  std::size_t verts{0};
  std::size_t tris{0};
  bool skinned{false};
  std::string material;
  std::vector<TextureSlot> textures;
  bool tangents{false};
};

struct InspectReport {
  bool fo4{false};
  bool has_bsx{false};
  bool has_collision{false};
  std::vector<InspectShape> shapes;
};

struct GeometryShape {
  std::string name;
  std::vector<float> verts;
  std::vector<std::uint16_t> tris;
  std::vector<float> normals;
  std::vector<float> uvs;
  std::vector<TextureSlot> textures;
  bool skinned{false};
  bool billboard{false};
  std::int32_t billboard_mode{0};
  std::string material;
};

struct GeometryReport {
  bool fo4{false};
  std::vector<GeometryShape> shapes;
};

// Invariant: a check with an empty failure passed; the name is the report key a caller must not rename.
struct VerifyCheck {
  std::string name;
  std::string failure;
};

struct VerifyReport {
  std::vector<VerifyCheck> checks;

  std::size_t failed() const noexcept;
};

// Invariant: type is the editor widget tag (string, tex, readonly, float, int, vec2, vec3, bool, flags) and value's alternative must match it.
struct TreeFlag {
  std::string key;
  std::string label;
  bool on{false};
};

using TreeValue = std::variant<std::string, float, std::vector<float>, std::vector<TreeFlag>, bool>;

struct TreeField {
  std::string key;
  std::string label;
  std::string type;
  TreeValue value;
};

struct TreeBlock {
  std::uint32_t id{0};
  std::string type;
  std::string name;
  // Constraint: group must be one of Nodes, Shapes, Collision or Extra; the editor renders no other group.
  std::string group;
  std::string cat;
  std::vector<TreeField> fields;
};

struct NifTree {
  bool fo4{false};
  std::vector<TreeBlock> blocks;
};

InspectReport inspect_nif(const NifFile& nif);
GeometryReport geometry_nif(const NifFile& nif);
VerifyReport verify_nif(const NifFile& nif);
// Requires every block to appear exactly once, ordered by block index.
NifTree describe_nif(const NifFile& nif);

// Invariant: a material path is a .bgsm reference, compared without regard to case.
bool is_bgsm_path(std::string_view path);
// Invariant: the name is the BSShaderMaterial::Feature label for the id, and "Unknown" past the last named type.
std::string_view lighting_type_name(std::uint32_t shader_type) noexcept;

}  // namespace fo4::nif
