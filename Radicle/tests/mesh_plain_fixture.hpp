#pragma once

#include "fo4/nif/nif.hpp"
#include "nif_build.hpp"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace mesh_plain_fixture {

using fo4::nif::Transform;
using namespace nif_build;

inline constexpr float k_units = 10.0f;

inline Transform rotated_z_xf() {
  return Transform{{10.0f, 20.0f, 30.0f}, {0.0f, -1.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f}, 2.0f};
}

inline std::vector<std::byte> rigid_nif(const Transform& xf, std::uint32_t flags = k_full, std::size_t stride = 16,
                                        const std::vector<std::byte>& vertices = triangle_vertices()) {
  return build_nif({"node"},
                   {node_block("NiNode", Transform{}, {1}),
                    shape_block("BSTriShape", -1, xf, flags, stride, vertices, {0, 1, 2})},
                   {0});
}

inline std::vector<std::byte> frame_vertices() {
  Writer w;
  const float positions[9] = {0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f};
  for (std::size_t v = 0; v < 3; ++v) {
    for (std::size_t i = 0; i < 3; ++i) w.f32(positions[v * 3 + i]);
    w.u32(0);
    w.u8(128);
    w.u8(128);
    w.u8(255);
    w.u8(255);
    w.u8(255);
    w.u8(128);
    w.u8(128);
    w.u8(128);
  }
  return w.bytes();
}

inline constexpr std::uint32_t k_frame_flags = k_full | k_has_normal | k_has_tangent;
inline constexpr std::size_t k_frame_stride = 24;

inline std::vector<std::byte> bone_data(std::size_t count, float tz) {
  Writer w;
  w.u32(static_cast<std::uint32_t>(count));
  for (std::size_t i = 0; i < count; ++i) {
    for (int j = 0; j < 4; ++j) w.f32(0.0f);
    for (int j = 0; j < 9; ++j) w.f32(j % 4 == 0 ? 1.0f : 0.0f);
    w.f32(0.0f);
    w.f32(0.0f);
    w.f32(tz);
    w.f32(1.0f);
  }
  return w.bytes();
}

inline std::vector<std::byte> skinned_nif() {
  const Transform bone_xf{{0.0f, 0.0f, 5.0f}, {1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f}, 1.0f};
  return build_nif({"Bone"},
                   {node_block("BSFadeNode", Transform{}, {1, 2}, root_trailer()),
                    named_node_block("NiNode", 0, bone_xf, {}),
                    shape_with_refs("BSTriShape", -1, Transform{}, k_has_skin, 20, 3, -1, -1, skinned_vertices(3),
                                    {0, 1, 2}),
                    skin_instance_block(4, {1, 1, 1, 1, 1}), Block{"BSSkin::BoneData", bone_data(5, -3.0f)}},
                   {0});
}

inline std::vector<std::byte> material_nif() {
  const std::uint32_t flags = k_full;
  const auto verts = triangle_vertices();
  LightingFields fields;
  fields.name = 2;
  fields.texture_set = 7;
  fields.flags1 = 0x04000009U;
  fields.flags2 = 0x50U;
  fields.root_material = 4;
  const std::vector<std::string> slots{"d.dds", "n.dds", "g.dds", "grey.dds", "cube.dds", "env.dds", "x.dds", "spec.dds"};
  const auto shape = [&](std::int32_t shader, std::int32_t alpha) {
    return shape_with_refs("BSTriShape", 1, Transform{}, flags, 16, -1, shader, alpha, verts, {0, 1, 2});
  };
  return build_nif({"Root", "Shape", "materials/test.bgsm", "materials/fx.bgem", "materials/wet.bgsm"},
                   {node_block("NiNode", Transform{}, {1, 2, 3, 4, 10}), shape(5, 6), shape(5, 6), shape(8, -1),
                    shape(5, 9), full_lighting_shader_block(fields), alpha_block(0x1, 0), texture_set_record(slots),
                    effect_shader_block(3, {"src.dds", "grey.dds", "cube.dds", "norm.dds", "envm.dds"}),
                    alpha_block(0x200, 128), shape(-1, -1)},
                   {0});
}

}  // namespace mesh_plain_fixture
