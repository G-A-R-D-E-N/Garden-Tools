#include "fo4/nif/mesh_plain.hpp"

#include "fo4/nif/nif.hpp"
#include "mesh_plain_fixture.hpp"

#include <gtest/gtest.h>

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <vector>

namespace {

using namespace mesh_plain_fixture;
using fo4::nif::PlainAlphaMode;
using fo4::nif::PlainMaterialKind;
using fo4::nif::PlainMeshOptions;

fo4::nif::PlainMesh read(const std::vector<std::byte>& bytes, bool flip = false) {
  const auto nif = fo4::nif::read_nif(bytes);
  PlainMeshOptions options;
  options.units_per_metre = k_units;
  options.flip_winding = flip;
  return fo4::nif::read_plain_mesh(nif, options);
}

std::array<float, 3> engine_direction(const std::array<float, 9>& r, const float* v) {
  const float x = r[0] * v[0] + r[1] * v[1] + r[2] * v[2];
  const float y = r[3] * v[0] + r[4] * v[1] + r[5] * v[2];
  const float z = r[6] * v[0] + r[7] * v[1] + r[8] * v[2];
  const float length = std::sqrt(x * x + y * y + z * z);
  return {x / length, z / length, -y / length};
}

std::vector<float> triple(const float* values) { return {values, values + 3}; }

}  // namespace

TEST(NifMeshPlain, RigidShapeLandsInEngineAxesAndMetres) {
  const auto mesh = read(rigid_nif(rotated_z_xf()));
  ASSERT_EQ(mesh.parts.size(), 1U);
  const auto& part = mesh.parts[0];
  expect_floats(part.positions, {1.0f, 3.0f, -2.0f, 1.0f, 3.0f, -2.2f, 0.8f, 3.0f, -2.0f});
  EXPECT_EQ(part.indices, (std::vector<std::uint32_t>{0, 1, 2}));
  expect_floats(triple(part.bounds_min), {0.8f, 3.0f, -2.2f});
  expect_floats(triple(part.bounds_max), {1.0f, 3.0f, -2.0f});
  expect_floats(triple(part.bounds_center), {0.9f, 3.0f, -2.1f});
  EXPECT_NEAR(part.bounds_radius, 0.1414214f, 1e-5);
  EXPECT_TRUE(part.normals.empty());
  EXPECT_TRUE(part.tangents.empty());
  EXPECT_TRUE(part.uv0.empty());
  EXPECT_FALSE(part.skin.has_value());
  EXPECT_EQ(part.material, -1);
  EXPECT_TRUE(mesh.materials.empty());
}

TEST(NifMeshPlain, DefaultUnitsUseTheHavokGameUnitsPerMetre) {
  const auto nif = fo4::nif::read_nif(rigid_nif(rotated_z_xf()));
  const auto mesh = fo4::nif::read_plain_mesh(nif);
  ASSERT_EQ(mesh.parts.size(), 1U);
  EXPECT_NEAR(mesh.parts[0].positions[0], 10.0f / 69.99125f, 1e-6);
  PlainMeshOptions bad;
  bad.units_per_metre = -1.0f;
  EXPECT_THROW(fo4::nif::read_plain_mesh(nif, bad), std::invalid_argument);
}

TEST(NifMeshPlain, WindingFollowsTheFlipOptionAndMirroredTransforms) {
  const Transform mirrored{{}, {-1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f}, 1.0f};
  const std::vector<std::uint32_t> kept{0, 1, 2};
  const std::vector<std::uint32_t> flipped{0, 2, 1};
  EXPECT_EQ(read(rigid_nif(Transform{}), false).parts.at(0).indices, kept);
  EXPECT_EQ(read(rigid_nif(Transform{}), true).parts.at(0).indices, flipped);
  EXPECT_EQ(read(rigid_nif(mirrored), false).parts.at(0).indices, flipped);
  EXPECT_EQ(read(rigid_nif(mirrored), true).parts.at(0).indices, kept);
}

TEST(NifMeshPlain, NormalsAndTangentsFollowTheShapeTransformAndMirrorSign) {
  const auto xf = rotated_z_xf();
  const auto bytes = rigid_nif(xf, k_frame_flags, k_frame_stride, frame_vertices());
  const auto stored = fo4::nif::read_shapes(fo4::nif::read_nif(bytes)).at(0);
  const auto mesh = read(bytes);
  const auto& part = mesh.parts.at(0);
  ASSERT_EQ(part.normals.size(), 9U);
  ASSERT_EQ(part.tangents.size(), 12U);
  const std::array<float, 9> r{0.0f, -1.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f};
  for (std::size_t v = 0; v < 3; ++v) {
    const auto n = engine_direction(r, &stored.normals[v * 3]);
    const auto b = engine_direction(r, &stored.bitangents[v * 3]);
    for (std::size_t i = 0; i < 3; ++i) {
      EXPECT_NEAR(part.normals[v * 3 + i], n[i], 1e-5);
      EXPECT_NEAR(part.tangents[v * 4 + i], b[i], 1e-5);
    }
    EXPECT_FLOAT_EQ(std::fabs(part.tangents[v * 4 + 3]), 1.0f);
  }
  const Transform mirrored{{}, {-1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f}, 1.0f};
  const auto left = read(rigid_nif(Transform{}, k_frame_flags, k_frame_stride, frame_vertices())).parts.at(0);
  const auto right = read(rigid_nif(mirrored, k_frame_flags, k_frame_stride, frame_vertices())).parts.at(0);
  EXPECT_FLOAT_EQ(left.tangents[3], -right.tangents[3]);
}

TEST(NifMeshPlain, SkinnedShapeIsInSkinSpaceWithInverseBindsRelativeToIt) {
  const auto mesh = read(skinned_nif());
  ASSERT_EQ(mesh.parts.size(), 1U);
  const auto& part = mesh.parts[0];
  expect_floats(part.positions, {0.1f, 0.5f, -0.2f, 0.4f, 0.8f, -0.5f, 0.4f, 0.8f, -0.5f});
  ASSERT_TRUE(part.skin.has_value());
  const auto& skin = *part.skin;
  ASSERT_EQ(skin.bone_names.size(), 5U);
  EXPECT_EQ(skin.bone_names[0], "Bone");
  ASSERT_EQ(skin.inverse_binds.size(), 5U);
  for (const auto& bind : skin.inverse_binds) {
    expect_floats({bind.basis, bind.basis + 9}, {1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f});
    expect_floats(triple(bind.origin), {0.0f, -0.5f, 0.0f});
  }
  ASSERT_EQ(skin.bone_indices.size(), 12U);
  EXPECT_EQ(skin.bone_indices[0], 1U);
  EXPECT_EQ(skin.bone_indices[1], 2U);
  ASSERT_EQ(skin.bone_weights.size(), 12U);
  EXPECT_NEAR(skin.bone_weights[0], 4.0f / 7.0f, 1e-3);
  EXPECT_NEAR(skin.bone_weights[1], 2.0f / 7.0f, 1e-3);
  EXPECT_NEAR(skin.bone_weights[2], 1.0f / 7.0f, 1e-3);
  EXPECT_FLOAT_EQ(skin.bone_weights[3], 0.0f);
  EXPECT_EQ(skin.bone_indices[3], 0U);
}

TEST(NifMeshPlain, MaterialsCarryTexturesAlphaAndNamedFlagsAndAreShared) {
  const auto mesh = read(material_nif());
  ASSERT_EQ(mesh.parts.size(), 5U);
  ASSERT_EQ(mesh.materials.size(), 3U);
  EXPECT_EQ(mesh.parts[0].material, 0);
  EXPECT_EQ(mesh.parts[1].material, 0);
  EXPECT_EQ(mesh.parts[2].material, 1);
  EXPECT_EQ(mesh.parts[3].material, 2);
  EXPECT_EQ(mesh.parts[4].material, -1);

  const auto& lit = mesh.materials[0];
  EXPECT_EQ(lit.kind, PlainMaterialKind::lighting);
  EXPECT_EQ(lit.key, "materials/test.bgsm");
  EXPECT_EQ(lit.diffuse, "d.dds");
  EXPECT_EQ(lit.normal, "n.dds");
  EXPECT_EQ(lit.glow, "g.dds");
  EXPECT_EQ(lit.greyscale, "grey.dds");
  EXPECT_EQ(lit.cubemap, "cube.dds");
  EXPECT_EQ(lit.env_mask, "env.dds");
  EXPECT_EQ(lit.smooth_spec, "spec.dds");
  EXPECT_EQ(lit.alpha_mode, PlainAlphaMode::blend);
  EXPECT_TRUE(lit.flags.specular);
  EXPECT_TRUE(lit.flags.vertex_alpha);
  EXPECT_TRUE(lit.flags.decal);
  EXPECT_TRUE(lit.flags.two_sided);
  EXPECT_TRUE(lit.flags.glow_map);
  EXPECT_FALSE(lit.flags.z_write);
  EXPECT_FALSE(lit.flags.vertex_colors);
  EXPECT_FALSE(lit.flags.cast_shadows);
  EXPECT_FALSE(lit.flags.own_emit);
  EXPECT_FALSE(lit.flags.hair);

  const auto& effect = mesh.materials[1];
  EXPECT_EQ(effect.kind, PlainMaterialKind::effect);
  EXPECT_EQ(effect.key, "materials/fx.bgem");
  EXPECT_EQ(effect.shader_kind, "Effect");
  EXPECT_EQ(effect.diffuse, "src.dds");
  EXPECT_EQ(effect.greyscale, "grey.dds");
  EXPECT_EQ(effect.cubemap, "cube.dds");
  EXPECT_EQ(effect.normal, "norm.dds");
  EXPECT_EQ(effect.env_mask, "envm.dds");
  EXPECT_EQ(effect.alpha_mode, PlainAlphaMode::opaque);
  EXPECT_TRUE(effect.flags.vertex_colors);

  const auto& masked = mesh.materials[2];
  EXPECT_EQ(masked.key, "materials/test.bgsm");
  EXPECT_EQ(masked.alpha_mode, PlainAlphaMode::mask);
  EXPECT_NEAR(masked.alpha_cutoff, 128.0f / 255.0f, 1e-5);
}

TEST(NifMeshPlain, LightingMaterialCarriesTheRawShaderBlockValues) {
  const auto mesh = read(material_nif());
  const auto& lit = mesh.materials.at(0);
  EXPECT_EQ(lit.shader_type, 1U);
  EXPECT_EQ(lit.shader_flags1, 0x04000009U);
  EXPECT_EQ(lit.shader_flags2, 0x50U);
  EXPECT_EQ(lit.texture_clamp, 3U);
  EXPECT_EQ(lit.root_material, "materials/wet.bgsm");
  expect_floats({lit.emissive_color, lit.emissive_color + 3}, {0.5f, 0.25f, 0.125f});
  EXPECT_FLOAT_EQ(lit.emissive_multiple, 4.0f);
  EXPECT_TRUE(lit.has_env_map);
  EXPECT_FLOAT_EQ(lit.env_map_scale, 1.5f);
  const auto& values = lit.lighting;
  EXPECT_FLOAT_EQ(values.glossiness, 0.75f);
  expect_floats({values.specular_color, values.specular_color + 3}, {0.1f, 0.2f, 0.3f});
  EXPECT_FLOAT_EQ(values.specular_strength, 0.875f);
  EXPECT_FLOAT_EQ(values.alpha, 0.5f);
  EXPECT_FLOAT_EQ(values.fresnel_power, 7.5f);
  EXPECT_FLOAT_EQ(values.subsurface_rolloff, 0.125f);
  EXPECT_EQ(values.rimlight_power, std::numeric_limits<float>::max());
  EXPECT_TRUE(values.has_backlight);
  EXPECT_FLOAT_EQ(values.backlight_power, 2.5f);
  EXPECT_TRUE(values.has_grayscale_to_palette_scale);
  EXPECT_FLOAT_EQ(values.grayscale_to_palette_scale, 0.625f);
  EXPECT_FLOAT_EQ(lit.effect.base_color_scale, 0.0f);
}

TEST(NifMeshPlain, AlphaPropertyIsReportedWhenPresentAndAbsentOtherwise) {
  const auto mesh = read(material_nif());
  const auto& blended = mesh.materials.at(0);
  EXPECT_TRUE(blended.alpha_property.present);
  EXPECT_EQ(blended.alpha_property.source_blend, 0U);
  EXPECT_EQ(blended.alpha_property.destination_blend, 0U);
  EXPECT_EQ(blended.alpha_property.threshold, 0U);

  const auto& effect = mesh.materials.at(1);
  EXPECT_FALSE(effect.alpha_property.present);

  const auto& masked = mesh.materials.at(2);
  EXPECT_TRUE(masked.alpha_property.present);
  EXPECT_EQ(masked.alpha_property.threshold, 128U);
}

TEST(NifMeshPlain, AdditiveBlendFactorsAreDecodedFromTheAlphaFlags) {
  const auto nif_bytes = build_nif({"Root", "Shape", "m.bgsm"},
                                   {node_block("NiNode", Transform{}, {1}),
                                    shape_with_refs("BSTriShape", 1, Transform{}, k_full, 16, -1, 2, 3, triangle_vertices(),
                                                    {0, 1, 2}),
                                    full_lighting_shader_block(LightingFields{}), alpha_block(0x20D, 77)},
                                   {0});
  const auto mesh = read(nif_bytes);
  ASSERT_EQ(mesh.materials.size(), 1U);
  const auto& material = mesh.materials[0];
  EXPECT_EQ(material.alpha_mode, PlainAlphaMode::blend);
  EXPECT_EQ(material.alpha_property.source_blend, 6U);
  EXPECT_EQ(material.alpha_property.destination_blend, 0U);
  EXPECT_EQ(material.alpha_property.threshold, 77U);
}

TEST(NifMeshPlain, EffectMaterialCarriesBaseColourFalloffSoftDepthAndEnvMapScale) {
  const auto mesh = read(material_nif());
  const auto& effect = mesh.materials.at(1);
  EXPECT_EQ(effect.shader_type, 0U);
  EXPECT_EQ(effect.shader_flags1, 0x80000000U);
  EXPECT_EQ(effect.shader_flags2, 0x20U);
  EXPECT_EQ(effect.texture_clamp, 3U);
  EXPECT_TRUE(effect.has_env_map);
  EXPECT_FLOAT_EQ(effect.env_map_scale, 1.75f);
  const auto& values = effect.effect;
  expect_floats({values.base_color, values.base_color + 4}, {0.4f, 0.3f, 0.2f, 0.6f});
  EXPECT_FLOAT_EQ(values.base_color_scale, 1.5f);
  EXPECT_FLOAT_EQ(values.falloff_start_angle, 1.0f);
  EXPECT_FLOAT_EQ(values.falloff_stop_angle, 0.5f);
  EXPECT_FLOAT_EQ(values.falloff_start_opacity, 0.75f);
  EXPECT_FLOAT_EQ(values.falloff_stop_opacity, 0.25f);
  EXPECT_FLOAT_EQ(values.soft_falloff_depth, 100.0f);
  expect_floats({effect.emissive_color, effect.emissive_color + 3}, {0.4f, 0.3f, 0.2f});
  EXPECT_FLOAT_EQ(effect.emissive_multiple, 1.5f);
  EXPECT_FLOAT_EQ(effect.lighting.glossiness, 0.0f);
  EXPECT_TRUE(effect.root_material.empty());
}
