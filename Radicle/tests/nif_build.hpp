#pragma once

#include "fo4/error.hpp"
#include "fo4/nif/nif.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace nif_build {

constexpr std::uint32_t k_has_uv = 1U << 1;
constexpr std::uint32_t k_has_uv2 = 1U << 2;
constexpr std::uint32_t k_has_normal = 1U << 3;
constexpr std::uint32_t k_has_tangent = 1U << 4;
constexpr std::uint32_t k_has_colour = 1U << 5;
constexpr std::uint32_t k_has_skin = 1U << 6;
constexpr std::uint32_t k_has_land_data = 1U << 7;
constexpr std::uint32_t k_has_tail_slot = 1U << 8;
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
                                        const std::vector<std::int32_t>& roots, std::uint32_t bs_version = 130,
                                        std::uint32_t size_bias = 0) {
  std::vector<std::string> types;
  std::vector<std::uint16_t> indices;
  for (const auto& block : blocks) {
    auto found = std::find(types.begin(), types.end(), block.type);
    if (found == types.end()) {
      types.push_back(block.type);
      found = types.end() - 1;
    }
    indices.push_back(static_cast<std::uint16_t>(found - types.begin()));
  }
  Writer w;
  w.text("Gamebryo File Format, Version 20.2.0.7\n");
  w.u32(0x14020007U);
  w.u8(1);
  w.u32(12);
  w.u32(static_cast<std::uint32_t>(blocks.size()));
  w.u32(bs_version);
  for (int i = 0; i < 4; ++i) w.short_string("");
  w.u16(static_cast<std::uint16_t>(types.size()));
  for (const auto& type : types) w.sized(type);
  for (const auto index : indices) w.u16(index);
  for (std::size_t i = 0; i < blocks.size(); ++i) {
    w.u32(static_cast<std::uint32_t>(blocks[i].data.size()) + (i == 0 ? size_bias : 0U));
  }
  w.u32(static_cast<std::uint32_t>(strings.size()));
  w.u32(0);
  for (const auto& value : strings) w.sized(value);
  w.u32(0);
  for (const auto& block : blocks) w.append(block.data);
  w.u32(static_cast<std::uint32_t>(roots.size()));
  for (const auto root : roots) w.i32(root);
  return w.bytes();
}

inline std::vector<std::byte> root_trailer() {
  Writer w;
  w.i32(-1);
  w.u32(0);
  return w.bytes();
}

inline std::vector<std::byte> mode_trailer(std::uint16_t mode) {
  Writer w;
  w.u16(mode);
  return w.bytes();
}

inline Block node_block(std::string type, const fo4::nif::Transform& xf, const std::vector<std::int32_t>& children,
                        const std::vector<std::byte>& trailing = {}) {
  Writer w;
  w.i32(0);
  w.u32(0);
  w.i32(-1);
  w.u32(0);
  w.transform(xf);
  w.i32(-1);
  w.u32(static_cast<std::uint32_t>(children.size()));
  for (const auto child : children) w.i32(child);
  w.append(trailing);
  return Block{std::move(type), w.bytes()};
}

inline Block named_node_block(std::string type, std::int32_t name, const fo4::nif::Transform& xf,
                              const std::vector<std::int32_t>& children,
                              const std::vector<std::byte>& trailing = {}) {
  Writer w;
  w.i32(name);
  w.u32(0);
  w.i32(-1);
  w.u32(0);
  w.transform(xf);
  w.i32(-1);
  w.u32(static_cast<std::uint32_t>(children.size()));
  for (const auto child : children) w.i32(child);
  w.append(trailing);
  return Block{std::move(type), w.bytes()};
}

inline Block shape_block(std::string type, std::int32_t name, const fo4::nif::Transform& xf, std::uint32_t flags,
                         std::size_t stride, const std::vector<std::byte>& vertices,
                         const std::vector<std::uint16_t>& tris) {
  Writer w;
  w.i32(name);
  w.u32(0);
  w.i32(-1);
  w.u32(0);
  w.transform(xf);
  w.i32(-1);
  for (int i = 0; i < 4; ++i) w.f32(0.0f);
  w.i32(-1);
  w.i32(-1);
  w.i32(-1);
  w.u64((static_cast<std::uint64_t>(flags) << 44U) | static_cast<std::uint64_t>(stride / 4));
  w.u32(static_cast<std::uint32_t>(tris.size() / 3));
  w.u16(static_cast<std::uint16_t>(vertices.size() / stride));
  w.u32(static_cast<std::uint32_t>(vertices.size() + tris.size() * 2));
  w.append(vertices);
  for (const auto index : tris) w.u16(index);
  return Block{std::move(type), w.bytes()};
}

inline Block shape_with_refs(std::string type, std::int32_t name, const fo4::nif::Transform& xf, std::uint32_t flags,
                             std::size_t stride, std::int32_t skin, std::int32_t shader, std::int32_t alpha,
                             const std::vector<std::byte>& vertices, const std::vector<std::uint16_t>& tris) {
  Writer w;
  w.i32(name);
  w.u32(0);
  w.i32(-1);
  w.u32(0);
  w.transform(xf);
  w.i32(-1);
  for (int i = 0; i < 4; ++i) w.f32(0.0f);
  w.i32(skin);
  w.i32(shader);
  w.i32(alpha);
  w.u64((static_cast<std::uint64_t>(flags) << 44U) | static_cast<std::uint64_t>(stride / 4));
  w.u32(static_cast<std::uint32_t>(tris.size() / 3));
  w.u16(static_cast<std::uint16_t>(vertices.size() / stride));
  w.u32(static_cast<std::uint32_t>(vertices.size() + tris.size() * 2));
  w.append(vertices);
  for (const auto index : tris) w.u16(index);
  return Block{std::move(type), w.bytes()};
}

inline Block lighting_shader_block(std::int32_t name, std::int32_t texture_set) {
  Writer w;
  w.u32(1);
  w.i32(name);
  w.u32(0);
  w.i32(-1);
  w.u32(0);
  w.u32(0);
  w.f32(0.0f);
  w.f32(0.0f);
  w.f32(1.0f);
  w.f32(1.0f);
  w.i32(texture_set);
  return Block{"BSLightingShaderProperty", w.bytes()};
}

// Invariant: every field carries a distinct value, so a field read out of order shows a wrong number.
struct LightingFields {
  std::uint32_t shader_type{1};
  std::int32_t name{-1};
  std::int32_t texture_set{-1};
  std::uint32_t flags1{0x80400201U};
  std::uint32_t flags2{0x21U};
  float uv_offset[2]{0.25f, 0.5f};
  float uv_scale[2]{2.0f, 3.0f};
  float emissive_color[3]{0.5f, 0.25f, 0.125f};
  float emissive_multiple{4.0f};
  std::int32_t root_material{-1};
  std::uint32_t texture_clamp{3};
  float alpha{0.5f};
  float refraction{0.25f};
  float smoothness{0.75f};
  float specular_color[3]{0.1f, 0.2f, 0.3f};
  float specular_strength{0.875f};
  float subsurface_rolloff{0.125f};
  float rimlight_power{std::bit_cast<float>(0x7F7FFFFFU)};
  float backlight_power{2.5f};
  float grayscale_to_palette_scale{0.625f};
  float fresnel_power{7.5f};
  float wetness[6]{1.0f, 2.0f, 3.0f, 4.0f, 5.0f, 6.0f};
};

inline void lighting_tail(Writer& w, std::uint32_t shader_type) {
  switch (shader_type) {
    case 1:
      w.f32(1.5f);
      w.u8(1);
      w.u8(0);
      break;
    case 5:
      for (const float value : {0.1f, 0.2f, 0.3f, 0.4f}) w.f32(value);
      break;
    case 6:
      for (const float value : {0.11f, 0.22f, 0.33f}) w.f32(value);
      break;
    case 7:
      w.f32(4.0f);
      w.f32(1.25f);
      break;
    case 11:
      for (const float value : {5.0f, 0.25f, 1.5f, 1.75f, 0.5f}) w.f32(value);
      break;
    case 14:
      for (const float value : {0.9f, 0.8f, 0.7f, 0.6f}) w.f32(value);
      break;
    case 16:
      w.f32(1.3f);
      for (const float value : {0.01f, 0.02f, 0.03f, 0.04f, 0.05f, 0.06f}) w.f32(value);
      break;
    default:
      break;
  }
}

inline Block full_lighting_shader_block(const LightingFields& fields) {
  Writer w;
  w.u32(fields.shader_type);
  w.i32(fields.name);
  w.u32(0);
  w.i32(-1);
  w.u32(fields.flags1);
  w.u32(fields.flags2);
  for (const float value : fields.uv_offset) w.f32(value);
  for (const float value : fields.uv_scale) w.f32(value);
  w.i32(fields.texture_set);
  for (const float value : fields.emissive_color) w.f32(value);
  w.f32(fields.emissive_multiple);
  w.i32(fields.root_material);
  w.u32(fields.texture_clamp);
  w.f32(fields.alpha);
  w.f32(fields.refraction);
  w.f32(fields.smoothness);
  for (const float value : fields.specular_color) w.f32(value);
  w.f32(fields.specular_strength);
  w.f32(fields.subsurface_rolloff);
  w.f32(fields.rimlight_power);
  // Invariant: the block stores Backlight Power only behind the FLT_MAX rimlight placeholder.
  if (fields.rimlight_power == std::numeric_limits<float>::max()) w.f32(fields.backlight_power);
  w.f32(fields.grayscale_to_palette_scale);
  w.f32(fields.fresnel_power);
  for (const float value : fields.wetness) w.f32(value);
  lighting_tail(w, fields.shader_type);
  return Block{"BSLightingShaderProperty", w.bytes()};
}

inline Block effect_shader_block(std::int32_t name, const std::vector<std::string>& textures) {
  Writer w;
  w.i32(name);
  w.u32(0);
  w.i32(-1);
  w.u32(0x80000000U);
  w.u32(0x20U);
  w.f32(0.5f);
  w.f32(0.25f);
  w.f32(4.0f);
  w.f32(2.0f);
  w.sized(textures.empty() ? std::string_view{} : std::string_view{textures[0]});
  w.u8(3);
  w.u8(255);
  w.u8(4);
  w.u8(0);
  for (const float value : {1.0f, 0.5f, 0.75f, 0.25f}) w.f32(value);
  for (const float value : {0.4f, 0.3f, 0.2f, 0.6f}) w.f32(value);
  w.f32(1.5f);
  w.f32(100.0f);
  for (std::size_t slot = 1; slot < 5; ++slot) {
    w.sized(slot < textures.size() ? std::string_view{textures[slot]} : std::string_view{});
  }
  w.f32(1.75f);
  return Block{"BSEffectShaderProperty", w.bytes()};
}

inline Block alpha_block(std::uint16_t flags, std::uint8_t threshold) {
  Writer w;
  w.i32(-1);
  w.u32(0);
  w.i32(-1);
  w.u16(flags);
  w.u8(threshold);
  return Block{"NiAlphaProperty", w.bytes()};
}

inline Block skin_instance_block(std::int32_t data, const std::vector<std::int32_t>& bones,
                                 std::int32_t skeleton_root = -1) {
  Writer w;
  w.i32(skeleton_root);
  w.i32(data);
  w.u32(static_cast<std::uint32_t>(bones.size()));
  for (const auto bone : bones) w.i32(bone);
  w.u32(0);
  return Block{"BSSkin::Instance", w.bytes()};
}

inline Block bone_data_block(std::size_t count) {
  Writer w;
  w.u32(static_cast<std::uint32_t>(count));
  for (std::size_t i = 0; i < count; ++i) {
    for (int j = 0; j < 4; ++j) w.f32(0.0f);
    for (int j = 0; j < 9; ++j) w.f32(j % 4 == 0 ? 1.0f : 0.0f);
    for (int j = 0; j < 3; ++j) w.f32(0.0f);
    w.f32(1.0f);
  }
  return Block{"BSSkin::BoneData", w.bytes()};
}

inline std::vector<std::byte> triangle_vertices() {
  Writer w;
  const float positions[9] = {0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f};
  for (std::size_t i = 0; i < 9; ++i) {
    w.f32(positions[i]);
    if (i % 3 == 2) w.u32(0);
  }
  return w.bytes();
}

inline Block triangle_block(std::string type, const fo4::nif::Transform& xf) {
  return shape_block(std::move(type), 0, xf, k_full, 16, triangle_vertices(), {0, 1, 2});
}

inline std::vector<std::byte> chain_file() {
  const fo4::nif::Transform root{{100.0f, 0.0f, 0.0f}, {1.0f, 0.0f, 0.0f, 0.0f, 0.0f, -1.0f, 0.0f, 1.0f, 0.0f}, 3.0f};
  const fo4::nif::Transform mid{{1.0f, 2.0f, 3.0f}, {1.0f, 0.0f, 0.0f, 0.0f, 0.0f, -1.0f, 0.0f, 1.0f, 0.0f}, 2.0f};
  const fo4::nif::Transform leaf{{4.0f, 5.0f, 6.0f}, {0.0f, -1.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f}, 1.0f};
  const fo4::nif::Transform shape_xf{{7.0f, 8.0f, 9.0f}, {1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f}, 1.0f};
  return build_nif({"node"},
                   {node_block("BSMultiBoundNode", root, {1}, root_trailer()), node_block("NiNode", mid, {2}),
                    node_block("NiNode", leaf, {3}), triangle_block("BSTriShape", shape_xf)},
                   {0});
}

inline void expect_floats(const std::vector<float>& actual, const std::vector<float>& expected) {
  ASSERT_EQ(actual.size(), expected.size());
  for (std::size_t i = 0; i < expected.size(); ++i) {
    EXPECT_NEAR(actual[i], expected[i], 1e-5) << "index " << i;
  }
}

template <typename Fn>
void expect_parse_error(Fn&& fn, std::string_view message) {
  try {
    fn();
    ADD_FAILURE() << "expected fo4::parse_error containing \"" << message << "\"";
  } catch (const fo4::parse_error& error) {
    EXPECT_NE(error.parse_message().find(message), std::string::npos) << error.parse_message();
  }
}

struct Vert {
  float pos[3];
  std::uint16_t half[3];
};

inline constexpr Vert k_verts[2] = {{{1.0f, 2.0f, 3.0f}, {0x3C00, 0x4000, 0x4200}},
                                    {{4.0f, 5.0f, 6.0f}, {0x4400, 0x4500, 0x4600}}};

inline std::vector<std::byte> encode_vertex(bool full, std::uint32_t flags, const Vert& vert) {
  Writer w;
  for (std::size_t i = 0; i < 3; ++i) {
    if (full) {
      w.f32(vert.pos[i]);
    } else {
      w.u16(vert.half[i]);
    }
  }
  if (full) w.u32(0);
  if (!full) w.u16(0);
  if ((flags & k_has_uv) != 0) {
    w.u16(0x3800);
    w.u16(0x3400);
  }
  if ((flags & k_has_uv2) != 0) {
    for (int i = 0; i < 4; ++i) w.u8(0xAB);
  }
  if ((flags & k_has_normal) != 0) {
    w.u8(255);
    w.u8(0);
    w.u8(128);
    w.u8(0x11);
  }
  if ((flags & k_has_tangent) != 0) {
    w.u8(0);
    w.u8(255);
    w.u8(128);
    w.u8(0x22);
  }
  if ((flags & k_has_colour) != 0) {
    w.u8(255);
    w.u8(0);
    w.u8(51);
    w.u8(102);
  }
  if ((flags & k_has_skin) != 0) {
    w.u16(0x3C00);
    w.u16(0x3800);
    w.u16(0x3400);
    w.u16(0);
    for (std::uint8_t index = 1; index <= 4; ++index) w.u8(index);
  }
  if ((flags & k_has_tail_slot) != 0) w.u32(0xEFU);
  return w.bytes();
}

inline std::vector<std::byte> skinned_vertices(std::size_t count) {
  std::vector<std::byte> vertices;
  for (std::size_t i = 0; i < count; ++i) {
    const auto one = encode_vertex(false, k_has_skin, k_verts[i == 0 ? 0 : 1]);
    vertices.insert(vertices.end(), one.begin(), one.end());
  }
  return vertices;
}

}
