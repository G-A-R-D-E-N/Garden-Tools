#include "fo4/c_api_nif_mesh.h"

#include "mesh_plain_fixture.hpp"

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace {

using namespace mesh_plain_fixture;

radicle_nif* read_handle(const std::vector<std::byte>& bytes) {
  radicle_nif* handle = nullptr;
  EXPECT_EQ(radicle_nif_read(reinterpret_cast<const uint8_t*>(bytes.data()), bytes.size(), &handle), RADICLE_STATUS_OK)
      << radicle_last_error();
  return handle;
}

radicle_mesh_options options(uint32_t flags = 0) {
  radicle_mesh_options out{};
  out.units_per_metre = k_units;
  out.flags = flags;
  return out;
}

}  // namespace

TEST(CApiNifMesh, RigidPartReadsInEngineSpaceAndOutlivesTheNifHandle) {
  auto* nif = read_handle(rigid_nif(rotated_z_xf()));
  const auto settings = options(RADICLE_MESH_FLIP_WINDING);
  radicle_mesh* mesh = nullptr;
  ASSERT_EQ(radicle_nif_mesh_read(nif, &settings, &mesh), RADICLE_STATUS_OK) << radicle_last_error();
  radicle_nif_destroy(nif);
  ASSERT_NE(mesh, nullptr);
  ASSERT_EQ(mesh->part_count, 1U);
  EXPECT_EQ(mesh->material_count, 0U);
  const auto& part = mesh->parts[0];
  EXPECT_EQ(part.vertex_count, 3U);
  EXPECT_EQ(part.index_count, 3U);
  expect_floats({part.positions, part.positions + 9}, {1.0f, 3.0f, -2.0f, 1.0f, 3.0f, -2.2f, 0.8f, 3.0f, -2.0f});
  EXPECT_EQ(part.indices[0], 0U);
  EXPECT_EQ(part.indices[1], 2U);
  EXPECT_EQ(part.indices[2], 1U);
  EXPECT_EQ(part.normals, nullptr);
  EXPECT_EQ(part.tangents, nullptr);
  EXPECT_EQ(part.skin, nullptr);
  EXPECT_EQ(part.material, -1);
  EXPECT_NEAR(part.bounds_radius, 0.1414214f, 1e-5);
  radicle_nif_mesh_free(mesh);
}

TEST(CApiNifMesh, SkinAndMaterialsSurviveTheBoundary) {
  auto* skinned = read_handle(skinned_nif());
  radicle_mesh* mesh = nullptr;
  const auto settings = options();
  ASSERT_EQ(radicle_nif_mesh_read(skinned, &settings, &mesh), RADICLE_STATUS_OK) << radicle_last_error();
  radicle_nif_destroy(skinned);
  ASSERT_EQ(mesh->part_count, 1U);
  const auto* skin = mesh->parts[0].skin;
  ASSERT_NE(skin, nullptr);
  ASSERT_EQ(skin->bone_count, 5U);
  EXPECT_STREQ(skin->bone_names[0], "Bone");
  expect_floats({skin->inverse_binds[4].origin, skin->inverse_binds[4].origin + 3}, {0.0f, -0.5f, 0.0f});
  EXPECT_EQ(skin->bone_indices[0], 1U);
  EXPECT_NEAR(skin->bone_weights[0], 4.0f / 7.0f, 1e-3);
  radicle_nif_mesh_free(mesh);

  auto* textured = read_handle(material_nif());
  ASSERT_EQ(radicle_nif_mesh_read(textured, nullptr, &mesh), RADICLE_STATUS_OK) << radicle_last_error();
  radicle_nif_destroy(textured);
  ASSERT_EQ(mesh->part_count, 5U);
  ASSERT_EQ(mesh->material_count, 3U);
  EXPECT_EQ(mesh->parts[0].material, 0);
  EXPECT_EQ(mesh->parts[4].material, -1);
  const auto& lit = mesh->materials[0];
  EXPECT_EQ(lit.kind, 1U);
  EXPECT_EQ(lit.alpha_mode, 2U);
  EXPECT_STREQ(lit.key, "materials/test.bgsm");
  EXPECT_STREQ(lit.diffuse, "d.dds");
  EXPECT_STREQ(lit.smooth_spec, "spec.dds");
  EXPECT_EQ(lit.two_sided, 1U);
  EXPECT_EQ(lit.decal, 1U);
  EXPECT_EQ(lit.hair, 0U);
  EXPECT_EQ(mesh->materials[1].kind, 2U);
  EXPECT_EQ(mesh->materials[2].alpha_mode, 1U);
  radicle_nif_mesh_free(mesh);
}

TEST(CApiNifMesh, ShaderValuesBlendFactorsAndEffectValuesSurviveTheBoundary) {
  auto* nif = read_handle(material_nif());
  radicle_mesh* mesh = nullptr;
  ASSERT_EQ(radicle_nif_mesh_read(nif, nullptr, &mesh), RADICLE_STATUS_OK) << radicle_last_error();
  radicle_nif_destroy(nif);
  ASSERT_EQ(mesh->material_count, 3U);

  const auto& lit = mesh->materials[0];
  EXPECT_EQ(lit.shader_type, 1U);
  EXPECT_EQ(lit.shader_flags1, 0x04000009U);
  EXPECT_EQ(lit.shader_flags2, 0x50U);
  EXPECT_EQ(lit.texture_clamp, 3U);
  EXPECT_STREQ(lit.root_material, "materials/wet.bgsm");
  expect_floats({lit.emissive_color, lit.emissive_color + 3}, {0.5f, 0.25f, 0.125f});
  EXPECT_FLOAT_EQ(lit.emissive_multiple, 4.0f);
  EXPECT_EQ(lit.has_env_map, 1U);
  EXPECT_FLOAT_EQ(lit.env_map_scale, 1.5f);
  EXPECT_FLOAT_EQ(lit.glossiness, 0.75f);
  expect_floats({lit.specular_color, lit.specular_color + 3}, {0.1f, 0.2f, 0.3f});
  EXPECT_FLOAT_EQ(lit.specular_strength, 0.875f);
  EXPECT_FLOAT_EQ(lit.alpha, 0.5f);
  EXPECT_FLOAT_EQ(lit.fresnel_power, 7.5f);
  EXPECT_FLOAT_EQ(lit.subsurface_rolloff, 0.125f);
  EXPECT_EQ(lit.has_backlight, 1U);
  EXPECT_FLOAT_EQ(lit.backlight_power, 2.5f);
  EXPECT_EQ(lit.has_grayscale_to_palette_scale, 1U);
  EXPECT_FLOAT_EQ(lit.grayscale_to_palette_scale, 0.625f);
  EXPECT_EQ(lit.has_alpha_property, 1U);
  EXPECT_EQ(lit.alpha_source_blend, 0U);
  EXPECT_EQ(lit.alpha_destination_blend, 0U);

  const auto& effect = mesh->materials[1];
  EXPECT_STREQ(effect.root_material, "");
  EXPECT_EQ(effect.has_alpha_property, 0U);
  EXPECT_EQ(effect.shader_flags1, 0x80000000U);
  EXPECT_EQ(effect.has_env_map, 1U);
  EXPECT_FLOAT_EQ(effect.env_map_scale, 1.75f);
  expect_floats({effect.effect_base_color, effect.effect_base_color + 4}, {0.4f, 0.3f, 0.2f, 0.6f});
  EXPECT_FLOAT_EQ(effect.effect_base_color_scale, 1.5f);
  EXPECT_FLOAT_EQ(effect.effect_falloff_start_angle, 1.0f);
  EXPECT_FLOAT_EQ(effect.effect_falloff_stop_angle, 0.5f);
  EXPECT_FLOAT_EQ(effect.effect_falloff_start_opacity, 0.75f);
  EXPECT_FLOAT_EQ(effect.effect_falloff_stop_opacity, 0.25f);
  EXPECT_FLOAT_EQ(effect.effect_soft_falloff_depth, 100.0f);

  const auto& masked = mesh->materials[2];
  EXPECT_EQ(masked.has_alpha_property, 1U);
  EXPECT_EQ(masked.alpha_threshold, 128U);
  radicle_nif_mesh_free(mesh);
}

TEST(CApiNifMesh, BadArgumentsReportInvalidArgumentAndLeaveNoView) {
  auto* nif = read_handle(rigid_nif(Transform{}));
  radicle_mesh* mesh = reinterpret_cast<radicle_mesh*>(1);
  EXPECT_EQ(radicle_nif_mesh_read(nullptr, nullptr, &mesh), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(mesh, nullptr);
  EXPECT_EQ(radicle_nif_mesh_read(nif, nullptr, nullptr), RADICLE_STATUS_INVALID_ARGUMENT);
  const auto unknown_flag = options(2U);
  EXPECT_EQ(radicle_nif_mesh_read(nif, &unknown_flag, &mesh), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(mesh, nullptr);
  auto negative = options();
  negative.units_per_metre = -1.0f;
  EXPECT_EQ(radicle_nif_mesh_read(nif, &negative, &mesh), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(mesh, nullptr);
  radicle_nif_mesh_free(nullptr);
  radicle_nif_destroy(nif);
}
