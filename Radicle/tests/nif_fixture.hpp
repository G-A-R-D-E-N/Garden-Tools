#pragma once

#include "fo4/error.hpp"
#include "fo4/nif/nif.hpp"

#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace nif_fixture {

constexpr std::uint32_t k_has_uv = 1U << 1;
constexpr std::uint32_t k_has_uv2 = 1U << 2;
constexpr std::uint32_t k_has_normal = 1U << 3;
constexpr std::uint32_t k_has_tangent = 1U << 4;
constexpr std::uint32_t k_has_colour = 1U << 5;
constexpr std::uint32_t k_has_skin = 1U << 6;
constexpr std::uint32_t k_full = 1U << 10;

class Writer {
public:
  void u8(std::uint8_t value) { bytes_.push_back(std::byte{value}); }
  void u16(std::uint16_t value) {
    u8(static_cast<std::uint8_t>(value));
    u8(static_cast<std::uint8_t>(value >> 8U));
  }
  void u32(std::uint32_t value) {
    u16(static_cast<std::uint16_t>(value));
    u16(static_cast<std::uint16_t>(value >> 16U));
  }
  void u64(std::uint64_t value) {
    u32(static_cast<std::uint32_t>(value));
    u32(static_cast<std::uint32_t>(value >> 32U));
  }
  void i32(std::int32_t value) { u32(static_cast<std::uint32_t>(value)); }
  void f32(float value) { u32(std::bit_cast<std::uint32_t>(value)); }
  void text(std::string_view value) {
    for (const char ch : value) u8(static_cast<std::uint8_t>(ch));
  }
  void sized(std::string_view value) {
    u32(static_cast<std::uint32_t>(value.size()));
    text(value);
  }
  void short_string(std::string_view value) {
    u8(static_cast<std::uint8_t>(value.size()));
    text(value);
  }
  void append(const std::vector<std::byte>& bytes) { bytes_.insert(bytes_.end(), bytes.begin(), bytes.end()); }
  void transform(const fo4::nif::Transform& xf) {
    for (const float value : xf.t) f32(value);
    for (const float value : xf.r) f32(value);
    f32(xf.s);
  }
  const std::vector<std::byte>& bytes() const { return bytes_; }

private:
  std::vector<std::byte> bytes_;
};

struct Block {
  std::string type;
  std::vector<std::byte> data;
};

inline std::vector<std::byte> build_nif(const std::vector<std::string>& strings, const std::vector<Block>& blocks,
                                        const std::vector<std::int32_t>& roots,
                                        const std::array<std::string, 4>& exports = {},
                                        std::uint32_t max_string_length = 0,
                                        const std::vector<std::uint32_t>& groups = {}) {
  std::vector<std::string> types;
  std::vector<std::uint16_t> indices;
  for (const auto& block : blocks) {
    std::size_t found = 0;
    while (found < types.size() && types[found] != block.type) ++found;
    if (found == types.size()) types.push_back(block.type);
    indices.push_back(static_cast<std::uint16_t>(found));
  }
  Writer w;
  w.text("Gamebryo File Format, Version 20.2.0.7\n");
  w.u32(0x14020007U);
  w.u8(1);
  w.u32(12);
  w.u32(static_cast<std::uint32_t>(blocks.size()));
  w.u32(130);
  for (const auto& text : exports) w.short_string(text);
  w.u16(static_cast<std::uint16_t>(types.size()));
  for (const auto& type : types) w.sized(type);
  for (const auto index : indices) w.u16(index);
  for (const auto& block : blocks) w.u32(static_cast<std::uint32_t>(block.data.size()));
  w.u32(static_cast<std::uint32_t>(strings.size()));
  w.u32(max_string_length);
  for (const auto& value : strings) w.sized(value);
  w.u32(static_cast<std::uint32_t>(groups.size()));
  for (const auto group : groups) w.u32(group);
  for (const auto& block : blocks) w.append(block.data);
  w.u32(static_cast<std::uint32_t>(roots.size()));
  for (const auto root : roots) w.i32(root);
  return w.bytes();
}

inline Block node_block(std::string type, std::int32_t name, const fo4::nif::Transform& xf,
                        const std::vector<std::int32_t>& children) {
  Writer w;
  w.i32(name);
  w.u32(0);
  w.i32(-1);
  w.u32(0);
  w.transform(xf);
  w.i32(-1);
  w.u32(static_cast<std::uint32_t>(children.size()));
  for (const auto child : children) w.i32(child);
  return Block{std::move(type), w.bytes()};
}

struct Vertex {
  float position[3];
  float bitangent_x;
  std::uint16_t uv[2];
  std::uint8_t normal[4];
  std::uint8_t tangent[4];
  std::uint8_t colour[4];
  std::uint16_t weights[4];
  std::uint8_t bones[4];
};

inline std::vector<std::byte> encode_vertex(const Vertex& vertex) {
  Writer w;
  for (const float value : vertex.position) w.f32(value);
  w.f32(vertex.bitangent_x);
  w.u16(vertex.uv[0]);
  w.u16(vertex.uv[1]);
  for (const std::uint8_t value : vertex.normal) w.u8(value);
  for (const std::uint8_t value : vertex.tangent) w.u8(value);
  for (const std::uint8_t value : vertex.colour) w.u8(value);
  for (const std::uint16_t value : vertex.weights) w.u16(value);
  for (const std::uint8_t value : vertex.bones) w.u8(value);
  return w.bytes();
}

constexpr std::size_t k_descriptor_fixed = 100;

inline Block shape_block(std::string type, std::int32_t name, const fo4::nif::Transform& xf, std::uint32_t flags,
                         std::size_t stride, std::int32_t skin, std::int32_t shader, std::int32_t alpha,
                         const std::vector<std::byte>& vertices, const std::vector<std::uint16_t>& tris,
                         std::uint32_t extras = 0, std::uint64_t attribute_offsets = 0) {
  Writer w;
  w.i32(name);
  w.u32(extras);
  for (std::uint32_t i = 0; i < extras; ++i) w.i32(-1);
  w.i32(-1);
  w.u32(0);
  w.transform(xf);
  w.i32(-1);
  for (int i = 0; i < 4; ++i) w.f32(0.0f);
  w.i32(skin);
  w.i32(shader);
  w.i32(alpha);
  w.u64((static_cast<std::uint64_t>(flags) << 44U) | static_cast<std::uint64_t>(stride / 4) | attribute_offsets);
  w.u32(static_cast<std::uint32_t>(tris.size() / 3));
  w.u16(static_cast<std::uint16_t>(vertices.size() / stride));
  w.u32(static_cast<std::uint32_t>(vertices.size() + tris.size() * 2));
  w.append(vertices);
  for (const auto index : tris) w.u16(index);
  return Block{std::move(type), w.bytes()};
}

inline std::uint64_t read_le(const std::vector<std::byte>& bytes, std::size_t at, std::size_t count) {
  std::uint64_t value = 0;
  for (std::size_t i = 0; i < count; ++i) {
    value |= std::to_integer<std::uint64_t>(bytes[at + i]) << (8U * i);
  }
  return value;
}

inline std::uint64_t descriptor_of(const std::vector<std::byte>& bytes, const fo4::nif::NifFile& nif,
                                   std::uint32_t block) {
  const auto at = nif.blocks()[block].offset;
  return read_le(bytes, at + k_descriptor_fixed + 4 * read_le(bytes, at + 4, 4), 8);
}

inline bool same_floats(const std::vector<float>& a, const std::vector<float>& b) {
  if (a.size() != b.size()) return false;
  for (std::size_t i = 0; i < a.size(); ++i) {
    if (std::bit_cast<std::uint32_t>(a[i]) != std::bit_cast<std::uint32_t>(b[i])) return false;
  }
  return true;
}

inline Block texture_set_block(const std::vector<std::string>& textures) {
  Writer w;
  w.u32(static_cast<std::uint32_t>(textures.size()));
  for (const auto& texture : textures) w.sized(texture);
  return Block{"BSShaderTextureSet", w.bytes()};
}

inline Block lighting_shader_block(std::uint32_t shader_type, std::int32_t name, std::int32_t texture_set,
                                   std::uint32_t flags1, std::uint32_t flags2, std::uint32_t extras = 0) {
  Writer w;
  w.u32(shader_type);
  w.i32(name);
  w.u32(extras);
  for (std::uint32_t i = 0; i < extras; ++i) w.i32(-1);
  w.i32(-1);
  w.u32(flags1);
  w.u32(flags2);
  w.f32(0.0f);
  w.f32(0.0f);
  w.f32(1.0f);
  w.f32(1.0f);
  w.i32(texture_set);
  for (int i = 0; i < 3; ++i) w.f32(0.0f);
  w.f32(1.0f);
  w.i32(-1);
  w.u32(3);
  w.f32(1.0f);
  w.f32(0.0f);
  w.f32(1.0f);
  for (int i = 0; i < 3; ++i) w.f32(1.0f);
  w.f32(1.0f);
  w.f32(0.0f);
  w.f32(std::bit_cast<float>(0x7F7FFFFFU));
  w.f32(0.0f);
  w.f32(1.0f);
  w.f32(5.0f);
  for (int i = 0; i < 6; ++i) w.f32(-1.0f);
  if (shader_type == 1) {
    w.f32(1.0f);
    w.u8(0);
    w.u8(0);
  }
  return Block{"BSLightingShaderProperty", w.bytes()};
}

struct BoneFixture {
  std::string name;
  float center[3];
  float radius;
  float rotation[9];
  float translation[3];
  float scale;
};

inline Block skin_instance_block(std::int32_t skeleton_root, std::int32_t data,
                                 const std::vector<std::int32_t>& bones) {
  Writer w;
  w.i32(skeleton_root);
  w.i32(data);
  w.u32(static_cast<std::uint32_t>(bones.size()));
  for (const auto bone : bones) w.i32(bone);
  w.u32(0);
  return Block{"BSSkin::Instance", w.bytes()};
}

inline Block bone_data_block(const std::vector<BoneFixture>& bones) {
  Writer w;
  w.u32(static_cast<std::uint32_t>(bones.size()));
  for (const auto& bone : bones) {
    for (const float value : bone.center) w.f32(value);
    w.f32(bone.radius);
    for (const float value : bone.rotation) w.f32(value);
    for (const float value : bone.translation) w.f32(value);
    w.f32(bone.scale);
  }
  return Block{"BSSkin::BoneData", w.bytes()};
}

inline Vertex skinned_vertex(float x, float y, float z, std::uint8_t bone) {
  Vertex vertex{};
  vertex.position[0] = x;
  vertex.position[1] = y;
  vertex.position[2] = z;
  vertex.bitangent_x = 0.5f;
  vertex.uv[0] = 0x3800;
  vertex.uv[1] = 0x3400;
  vertex.normal[0] = 255;
  vertex.normal[1] = 0;
  vertex.normal[2] = 128;
  vertex.normal[3] = 0x11;
  vertex.tangent[0] = 0;
  vertex.tangent[1] = 255;
  vertex.tangent[2] = 128;
  vertex.tangent[3] = 0x22;
  vertex.colour[0] = 255;
  vertex.colour[1] = 0;
  vertex.colour[2] = 51;
  vertex.colour[3] = 102;
  for (int i = 0; i < 4; ++i) vertex.weights[i] = 0x0000;
  vertex.weights[0] = 0x3C00;
  vertex.weights[1] = 0x3800;
  vertex.weights[2] = 0x3400;
  for (int i = 0; i < 4; ++i) vertex.bones[i] = bone;
  return vertex;
}

inline std::vector<std::byte> full_nif() {
  const std::uint32_t flags = k_full | k_has_uv | k_has_normal | k_has_tangent | k_has_colour | k_has_skin;
  const std::size_t stride = 44;
  auto vertices = encode_vertex(skinned_vertex(1.0f, 2.0f, 3.0f, 0));
  const auto second = encode_vertex(skinned_vertex(4.0f, 5.0f, 6.0f, 0));
  vertices.insert(vertices.end(), second.begin(), second.end());

  const fo4::nif::Transform shape_xf{{7.0f, 8.0f, 9.0f},
                                     {1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f}, 1.0f};
  const fo4::nif::Transform root_xf{{10.0f, 0.0f, 0.0f},
                                    {1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f}, 2.0f};

  BoneFixture bone{};
  bone.name = "RootBone";
  bone.center[0] = 1.0f;
  bone.center[1] = 2.0f;
  bone.center[2] = 3.0f;
  bone.radius = 4.0f;
  for (int i = 0; i < 9; ++i) bone.rotation[i] = static_cast<float>(i + 1);
  bone.translation[0] = 5.0f;
  bone.translation[1] = 6.0f;
  bone.translation[2] = 7.0f;
  bone.scale = 2.0f;

  const std::vector<Block> blocks{
      node_block("NiNode", 0, root_xf, {1}),
      shape_block("BSTriShape", 0, shape_xf, flags, stride, 2, 4, -1, vertices, {0, 1, 1}),
      skin_instance_block(0, 3, {6}),
      bone_data_block({bone}),
      lighting_shader_block(1, 2, 5, 0x80400201U, 0x21U),
      texture_set_block({"textures\\body_d.dds", "textures\\body_n.dds"}),
      node_block("NiNode", 1, fo4::nif::Transform{}, {}),
  };
  return build_nif({"shape", "RootBone", "materials\\body.bgsm", "textures\\body_d.dds",
                    "textures\\body_n.dds"},
                   blocks, {0});
}

}
