#include "fo4/archive/assets.hpp"
#include "fo4/nif/mesh_plain.hpp"
#include "fo4/nif/nif.hpp"
#include "real_data_plugins.hpp"
#include "real_data.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdlib>
#include <iostream>
#include <string>
#include <string_view>
#include <vector>

namespace {

constexpr std::size_t k_per_folder = 12;
constexpr float k_slack = 1e-4f;

bool all_finite(const std::vector<float>& values) {
  return std::all_of(values.begin(), values.end(), [](float value) { return std::isfinite(value); });
}

}  // namespace

TEST(NifMeshPlainReal, SampledVanillaNifsGiveConsistentPlainPartsSkinsAndMaterials) {
  FO4_REQUIRE_DATA_DIR(dir, "set FO4_DATA_DIR to a Fallout 4 Data folder with its Fallout4 - *.ba2 archives");
  const auto archives = fo4::archive::archive_load_order(dir, {}, installed_master_and_cc_plugins(dir));
  if (archives.empty()) GTEST_SKIP() << "set FO4_DATA_DIR to a Fallout 4 Data folder with its Fallout4 - *.ba2 archives";
  const auto assets = fo4::archive::AssetResolver::open({}, archives);

  std::size_t nifs = 0;
  std::size_t parts = 0;
  std::size_t skinned = 0;
  std::size_t with_material = 0;
  std::size_t textured = 0;
  std::size_t lit = 0;
  std::size_t effects = 0;
  std::size_t warnings = 0;
  for (const std::string_view folder :
       {"meshes/actors/character/characterassets", "meshes/armor", "meshes/weapons", "meshes/actors"}) {
    const auto listing = assets.list(folder, ".nif", false);
    EXPECT_FALSE(listing.empty()) << folder;
    const auto step = std::max<std::size_t>(1, listing.size() / k_per_folder);
    for (std::size_t i = 0, picked = 0; i < listing.size() && picked < k_per_folder; i += step, ++picked) {
      const auto& entry = listing[i];
      ++nifs;
      try {
        const auto nif = fo4::nif::read_nif(assets.read(entry.location));
        const auto mesh = fo4::nif::read_plain_mesh(nif);
        for (const auto& part : mesh.parts) {
          ++parts;
          const std::size_t count = part.positions.size() / 3;
          EXPECT_GT(count, 0U) << entry.path;
          EXPECT_EQ(part.positions.size(), count * 3) << entry.path;
          EXPECT_TRUE(all_finite(part.positions)) << entry.path;
          EXPECT_EQ(part.indices.size() % 3, 0U) << entry.path;
          for (const auto index : part.indices) {
            ASSERT_LT(index, count) << entry.path;
          }
          if (!part.normals.empty()) {
            EXPECT_EQ(part.normals.size(), count * 3) << entry.path;
          }
          if (!part.tangents.empty()) {
            EXPECT_EQ(part.tangents.size(), count * 4) << entry.path;
          }
          if (!part.uv0.empty()) {
            EXPECT_EQ(part.uv0.size(), count * 2) << entry.path;
          }
          for (std::size_t at = 0; at + 2 < part.positions.size(); at += 3) {
            for (std::size_t axis = 0; axis < 3; ++axis) {
              EXPECT_GE(part.positions[at + axis], part.bounds_min[axis] - k_slack) << entry.path;
              EXPECT_LE(part.positions[at + axis], part.bounds_max[axis] + k_slack) << entry.path;
            }
          }
          EXPECT_LT(part.bounds_radius, 200.0f) << entry.path;
          if (part.material >= 0) {
            ++with_material;
            EXPECT_LT(static_cast<std::size_t>(part.material), mesh.materials.size()) << entry.path;
          }
          if (part.skin) {
            ++skinned;
            const auto& skin = *part.skin;
            EXPECT_EQ(skin.bone_names.size(), skin.inverse_binds.size()) << entry.path;
            EXPECT_EQ(skin.bone_indices.size(), count * 4) << entry.path;
            EXPECT_EQ(skin.bone_weights.size(), count * 4) << entry.path;
            for (std::size_t v = 0; v < count; ++v) {
              float sum = 0.0f;
              for (std::size_t k = 0; k < 4; ++k) {
                sum += skin.bone_weights[v * 4 + k];
                EXPECT_LT(skin.bone_indices[v * 4 + k], skin.bone_names.size()) << entry.path;
              }
              EXPECT_TRUE(std::fabs(sum - 1.0f) < 1e-3f || sum == 0.0f) << entry.path << " vertex " << v << " sum " << sum;
            }
            for (const auto& bind : skin.inverse_binds) {
              EXPECT_TRUE(all_finite({bind.basis, bind.basis + 9})) << entry.path;
              EXPECT_TRUE(all_finite({bind.origin, bind.origin + 3})) << entry.path;
            }
          }
          if (!part.warning.empty()) ++warnings;
        }
        for (const auto& material : mesh.materials) {
          if (!material.key.empty() || !material.diffuse.empty()) ++textured;
          if (material.kind == fo4::nif::PlainMaterialKind::lighting) {
            ++lit;
            EXPECT_TRUE(std::isfinite(material.lighting.glossiness)) << entry.path;
            EXPECT_TRUE(std::isfinite(material.lighting.specular_strength)) << entry.path;
            EXPECT_TRUE(std::isfinite(material.lighting.fresnel_power)) << entry.path;
            EXPECT_TRUE(all_finite({material.emissive_color, material.emissive_color + 3})) << entry.path;
          }
          if (material.kind == fo4::nif::PlainMaterialKind::effect) {
            ++effects;
            EXPECT_TRUE(all_finite({material.effect.base_color, material.effect.base_color + 4})) << entry.path;
            EXPECT_TRUE(std::isfinite(material.effect.soft_falloff_depth)) << entry.path;
          }
          if (material.alpha_property.present) {
            EXPECT_LE(material.alpha_property.source_blend, 15U) << entry.path;
          }
        }
      } catch (const std::exception& error) {
        ADD_FAILURE() << entry.path << ": " << error.what();
      }
    }
  }
  std::cout << "[mesh-plain] nifs=" << nifs << " parts=" << parts << " skinned=" << skinned
            << " with_material=" << with_material << " textured_materials=" << textured << " warnings=" << warnings
            << " lighting=" << lit << " effect=" << effects
            << "\n";
  EXPECT_GT(parts, 0U);
  EXPECT_GT(skinned, 0U);
  EXPECT_GT(with_material, 0U);
  EXPECT_GT(textured, 0U);
  EXPECT_GT(lit, 0U);
}
