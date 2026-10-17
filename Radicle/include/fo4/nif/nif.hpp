#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace fo4::nif {

struct BlockInfo {
  std::string type;
  std::size_t offset;
  std::size_t size;
};

class NifFile {
public:
  std::uint32_t version() const noexcept { return version_; }
  std::uint32_t user_version() const noexcept { return user_version_; }
  std::uint32_t bs_version() const noexcept { return bs_version_; }
  const std::vector<std::byte>& bytes() const noexcept { return bytes_; }
  const std::vector<std::string>& strings() const noexcept { return strings_; }
  const std::vector<BlockInfo>& blocks() const noexcept { return blocks_; }
  const std::vector<std::uint32_t>& roots() const noexcept { return roots_; }
  const std::vector<std::uint32_t>& children(std::uint32_t block) const { return children_.at(block); }
  std::int32_t parent(std::uint32_t block) const { return parents_.at(block); }

private:
  friend NifFile read_nif(std::span<const std::byte> bytes);

  std::vector<std::byte> bytes_;
  std::uint32_t version_{0};
  std::uint32_t user_version_{0};
  std::uint32_t bs_version_{0};
  std::vector<std::string> strings_;
  std::vector<BlockInfo> blocks_;
  std::vector<std::uint32_t> roots_;
  std::vector<std::vector<std::uint32_t>> children_;
  std::vector<std::int32_t> parents_;
};

NifFile read_nif(std::span<const std::byte> bytes);

struct Transform {
  float t[3]{};
  float r[9]{1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f};
  float s{1.0f};
};

struct Bone {
  std::string name;
  float center[3]{};
  float radius{0.0f};
  float rotation[9]{1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f};
  float translation[3]{};
  float scale{1.0f};
};

struct Shader {
  std::uint32_t block{0};
  std::uint32_t type{0};
  std::string name;
  std::int32_t texture_set_ref{-1};
  std::vector<std::string> textures;
};

struct Shape {
  std::uint32_t block{0};
  std::string name;
  std::vector<float> verts;
  std::vector<float> normals;
  std::vector<float> uvs;
  std::vector<float> tangents;
  std::vector<float> bitangents;
  std::vector<float> colors;
  std::vector<std::uint16_t> tris;
  std::vector<float> weights;
  std::vector<std::uint8_t> bone_indices;
  std::vector<Bone> bones;
  std::int32_t skin_ref{-1};
  std::int32_t shader_ref{-1};
  std::int32_t alpha_ref{-1};
  std::string material;
  std::vector<std::string> textures;
};

std::vector<Shape> read_shapes(const NifFile& nif);
Transform shape_to_global(const NifFile& nif, std::uint32_t block);
std::int32_t billboard_mode(const NifFile& nif, std::uint32_t block);

/* Reads a BSLightingShaderProperty block: its NiObjectNET name (the BGSM or
   BGEM path when the material is a file) and its BSShaderTextureSet slots. */
Shader read_shader(const NifFile& nif, std::uint32_t block);

/* Reads a BSSkin::Instance block and its BSSkin::BoneData: one entry per bone,
   named from the referenced node, with the bind transform when the data block
   is present and its bone count matches. */
std::vector<Bone> read_bones(const NifFile& nif, std::uint32_t instance_block);

}  // namespace fo4::nif
