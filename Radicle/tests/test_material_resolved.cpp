#include "fo4/material/resolved.hpp"

#include "resolved_material_fixture.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

using fo4::env::MaterialSubstitution;
using fo4::material::AlphaMode;
using fo4::material::apply_material_swap;
using fo4::material::Color3;
using fo4::material::resolve_material;
using fo4::material::ResolvedMaterial;
using fo4::nif::AlphaBlendMode;
using namespace resolved_fixture;

namespace {

constexpr const char* k_box = "Materials\\Props\\Box.bgsm";
constexpr const char* k_box_file = "materials/props/box.bgsm";

bool has_warning(const ResolvedMaterial& rm, std::string_view needle) {
  return std::ranges::any_of(rm.warnings, [&](const std::string& w) { return w.find(needle) != std::string::npos; });
}

Scene boxed() {
  Scene scene;
  scene.material = k_box;
  scene.slots = {"textures\\a_d.dds"};
  return scene;
}

}  // namespace

// Detects: a shape with no material file that must report the NIF shader, texture set and alpha property as they stand, not an empty material.
TEST(MaterialResolved, NifOnlyShapeMustCarryTheShaderTexturesAndAlphaProperty) {
  fo4::test::TempDataDir dir;
  const auto assets = open_dir(dir);
  Scene scene;
  scene.slots = {"textures\\a_d.dds", "a_n.dds"};
  scene.alpha = true;
  const auto loaded = lighting(scene);

  const auto rm = resolve_material(loaded.file, loaded.shape, assets);

  EXPECT_EQ(rm.source, ResolvedMaterial::Source::nif_only);
  EXPECT_TRUE(rm.material_path.empty());
  EXPECT_EQ(rm.textures[0], "textures/a_d.dds");
  EXPECT_EQ(rm.textures[1], "textures/a_n.dds");
  EXPECT_TRUE(rm.textures[2].empty());
  EXPECT_EQ(rm.alpha_mode, AlphaMode::blend);
  EXPECT_EQ(rm.blend_src, AlphaBlendMode::SourceAlpha);
  EXPECT_EQ(rm.blend_dst, AlphaBlendMode::InverseSourceAlpha);
  EXPECT_FLOAT_EQ(rm.alpha, 0.5F);
  EXPECT_FLOAT_EQ(rm.uv_offset[0], 0.25F);
  EXPECT_FLOAT_EQ(rm.uv_offset[1], 0.5F);
  EXPECT_FLOAT_EQ(rm.uv_scale[0], 2.0F);
  EXPECT_FLOAT_EQ(rm.uv_scale[1], 3.0F);
  EXPECT_FALSE(rm.swapped);
  EXPECT_FALSE(rm.color_remapping_index.has_value());
  EXPECT_TRUE(rm.warnings.empty());
}

// Detects: a NIF alpha property with the test bit that must give an alpha-tested material with the NIF threshold, not an opaque one.
TEST(MaterialResolved, NifAlphaTestPropertyMustGiveTestModeWithItsThreshold) {
  fo4::test::TempDataDir dir;
  const auto assets = open_dir(dir);
  Scene scene;
  scene.alpha = true;
  scene.alpha_flags = 0x1200;
  scene.threshold = 77;
  const auto loaded = lighting(scene);

  const auto rm = resolve_material(loaded.file, loaded.shape, assets);

  EXPECT_EQ(rm.alpha_mode, AlphaMode::test);
  EXPECT_EQ(rm.alpha_threshold, 77);
}

// Detects: a shape that names its own BGSM that must take textures, UVs, blend factors and emissive from the file, not the NIF.
TEST(MaterialResolved, OwnBgsmMustSupplyTexturesUvAlphaAndEmissive) {
  fo4::test::TempDataDir dir;
  put(dir, k_box_file, bgsm(true));
  const auto assets = open_dir(dir);
  const auto loaded = lighting(boxed());

  const auto rm = resolve_material(loaded.file, loaded.shape, assets);

  EXPECT_EQ(rm.source, ResolvedMaterial::Source::bgsm);
  EXPECT_EQ(rm.nif_material_path, k_box_file);
  EXPECT_EQ(rm.material_path, k_box_file);
  EXPECT_EQ(rm.textures[0], "textures/d.dds");
  EXPECT_EQ(rm.textures[1], "textures/nonul");
  EXPECT_EQ(rm.textures[2], "textures/glow.dds");
  EXPECT_EQ(rm.textures[3], "textures/g.dds");
  EXPECT_EQ(rm.textures[4], "textures/env.dds");
  EXPECT_EQ(rm.textures[7], "textures/s.dds");
  EXPECT_FLOAT_EQ(rm.uv_offset[0], 0.5F);
  EXPECT_FLOAT_EQ(rm.uv_offset[1], 0.25F);
  EXPECT_FLOAT_EQ(rm.uv_scale[0], 1.5F);
  EXPECT_FLOAT_EQ(rm.uv_scale[1], 2.0F);
  EXPECT_EQ(rm.alpha_mode, AlphaMode::blend);
  EXPECT_EQ(rm.blend_src, AlphaBlendMode::SourceAlpha);
  EXPECT_EQ(rm.blend_dst, AlphaBlendMode::InverseSourceAlpha);
  EXPECT_TRUE(rm.two_sided);
  EXPECT_FLOAT_EQ(rm.emissive.r, 0.25F);
  EXPECT_FLOAT_EQ(rm.emissive.g, 0.5F);
  EXPECT_FLOAT_EQ(rm.emissive.b, 0.75F);
  EXPECT_FLOAT_EQ(rm.emissive_mult, 0.9F);
}

// Detects: a BGSM texture that is missing from disk while the NIF texture exists, which must keep the NIF texture and not point at a file that is not there.
TEST(MaterialResolved, TextureMissingOnDiskMustKeepTheNifTexture) {
  fo4::test::TempDataDir dir;
  put(dir, k_box_file, bgsm());
  touch(dir, "textures/a_d.dds");
  const auto assets = open_dir(dir);
  const auto loaded = lighting(boxed());

  const auto rm = resolve_material(loaded.file, loaded.shape, assets);

  EXPECT_EQ(rm.textures[0], "textures/a_d.dds");
  EXPECT_EQ(rm.textures[1], "textures/nonul");
}

// Detects: a BGSM texture that exists next to a NIF texture that also exists, which must win because the material file is authoritative.
TEST(MaterialResolved, ExistingBgsmTextureMustBeatTheNifTexture) {
  fo4::test::TempDataDir dir;
  put(dir, k_box_file, bgsm());
  touch(dir, "textures/a_d.dds");
  touch(dir, "textures/d.dds");
  const auto assets = open_dir(dir);
  const auto loaded = lighting(boxed());

  EXPECT_EQ(resolve_material(loaded.file, loaded.shape, assets).textures[0], "textures/d.dds");
}

// Detects: a BGSM with the test flag and no blend that must beat the NIF blend property and give alpha test with the BGSM reference, not blend.
TEST(MaterialResolved, BgsmAlphaTestMustWinOverTheNifBlendProperty) {
  fo4::test::TempDataDir dir;
  auto file = bgsm();
  ASSERT_TRUE(fo4::material::set_field<std::uint8_t>(file, "AlphaBlendMode0", 0));
  ASSERT_TRUE(fo4::material::set_field<std::uint8_t>(file, "AlphaTestRef", 200));
  put(dir, k_box_file, file);
  const auto assets = open_dir(dir);
  auto scene = boxed();
  scene.alpha = true;
  const auto loaded = lighting(scene);

  const auto rm = resolve_material(loaded.file, loaded.shape, assets);

  EXPECT_EQ(rm.alpha_mode, AlphaMode::test);
  EXPECT_EQ(rm.alpha_threshold, 200);
}

// Detects: a BGSM with neither blend nor test that must make the material opaque, not keep the NIF blend it overrides.
TEST(MaterialResolved, BgsmWithoutBlendOrTestMustBeOpaque) {
  fo4::test::TempDataDir dir;
  auto file = bgsm();
  ASSERT_TRUE(fo4::material::set_field<std::uint8_t>(file, "AlphaBlendMode0", 0));
  ASSERT_TRUE(fo4::material::set_field<bool>(file, "AlphaTest", false));
  put(dir, k_box_file, file);
  const auto assets = open_dir(dir);
  auto scene = boxed();
  scene.alpha = true;
  const auto loaded = lighting(scene);

  EXPECT_EQ(resolve_material(loaded.file, loaded.shape, assets).alpha_mode, AlphaMode::opaque);
}

// Detects: a BGEM on an effect shader that must fill only the empty slots and report the BGEM base colour as emissive, not overwrite the NIF base texture.
TEST(MaterialResolved, BgemOnAnEffectShaderMustFillEmptySlotsAndSupplyTheBaseColour) {
  fo4::test::TempDataDir dir;
  const auto file = bgem();
  put(dir, "materials/fx/glow.bgem", file);
  const auto assets = open_dir(dir);
  const auto loaded = effect("Materials\\Fx\\Glow.bgem", {"textures\\n_base.dds"});

  const auto rm = resolve_material(loaded.file, loaded.shape, assets);

  EXPECT_EQ(rm.source, ResolvedMaterial::Source::bgem);
  EXPECT_EQ(rm.material_path, "materials/fx/glow.bgem");
  EXPECT_EQ(rm.textures[0], "textures/n_base.dds");
  EXPECT_EQ(rm.textures[1], "textures/nonul");
  EXPECT_EQ(rm.textures[3], "textures/grey.dds");
  EXPECT_EQ(rm.textures[4], "textures/env.dds");
  EXPECT_TRUE(rm.textures[2].empty());
  EXPECT_TRUE(rm.textures[7].empty());
  const auto base = fo4::material::get_field<Color3>(file, "BaseColor");
  ASSERT_TRUE(base.has_value());
  EXPECT_EQ(rm.emissive, *base);
  EXPECT_EQ(rm.alpha_mode, AlphaMode::blend);
}

// Detects: a missing material file that must leave the NIF values in place and say so in warnings, not throw or return an empty material.
TEST(MaterialResolved, MissingMaterialMustKeepTheNifValuesWithAWarning) {
  fo4::test::TempDataDir dir;
  const auto assets = open_dir(dir);
  const auto loaded = lighting(boxed());

  const auto rm = resolve_material(loaded.file, loaded.shape, assets);

  EXPECT_EQ(rm.source, ResolvedMaterial::Source::nif_only);
  EXPECT_TRUE(rm.material_path.empty());
  EXPECT_EQ(rm.textures[0], "textures/a_d.dds");
  EXPECT_TRUE(has_warning(rm, "material not found")) << (rm.warnings.empty() ? "" : rm.warnings.front());
}

// Detects: a corrupt material file that must keep the NIF values with a warning, not abort the whole resolve.
TEST(MaterialResolved, CorruptMaterialMustKeepTheNifValuesWithAWarning) {
  fo4::test::TempDataDir dir;
  dir.write(k_box_file, {std::byte{1}, std::byte{2}, std::byte{3}}, 0);
  const auto assets = open_dir(dir);
  const auto loaded = lighting(boxed());

  const auto rm = resolve_material(loaded.file, loaded.shape, assets);

  EXPECT_EQ(rm.source, ResolvedMaterial::Source::nif_only);
  EXPECT_EQ(rm.textures[0], "textures/a_d.dds");
  EXPECT_TRUE(has_warning(rm, "material not read"));
}

// Detects: a root material on a NIF without an own material that must only fill empty slots and give the blend the NIF lacks, not replace the NIF texture.
TEST(MaterialResolved, RootMaterialTemplateMustOnlyFillEmptySlots) {
  fo4::test::TempDataDir dir;
  put(dir, "materials/props/tpl.bgsm", bgsm());
  const auto assets = open_dir(dir);
  Scene scene;
  scene.slots = {"textures\\a_d.dds"};
  scene.root = "Materials\\Props\\Tpl.bgsm";
  const auto loaded = lighting(scene);

  const auto rm = resolve_material(loaded.file, loaded.shape, assets);

  EXPECT_EQ(rm.source, ResolvedMaterial::Source::bgsm);
  EXPECT_EQ(rm.material_path, "materials/props/tpl.bgsm");
  EXPECT_EQ(rm.textures[0], "textures/a_d.dds");
  EXPECT_EQ(rm.textures[1], "textures/nonul");
  EXPECT_EQ(rm.textures[3], "textures/g.dds");
  EXPECT_EQ(rm.alpha_mode, AlphaMode::blend);
}

// Detects: a root material ending _wet.bgsm that must set has_wet and not be loaded, as the wet variant is a runtime overlay.
TEST(MaterialResolved, WetRootMaterialMustSetHasWetWithoutLoadingIt) {
  fo4::test::TempDataDir dir;
  put(dir, "materials/props/x_wet.bgsm", bgsm());
  const auto assets = open_dir(dir);
  Scene scene;
  scene.slots = {"textures\\a_d.dds"};
  scene.root = "Materials\\Props\\X_wet.bgsm";
  const auto loaded = lighting(scene);

  const auto rm = resolve_material(loaded.file, loaded.shape, assets);

  EXPECT_TRUE(rm.has_wet);
  EXPECT_TRUE(rm.material_path.empty());
  EXPECT_TRUE(rm.textures[1].empty());
  EXPECT_TRUE(rm.warnings.empty());
}

// Detects: glow that must need emit, the glowmap flag and a glow texture together, not any one of them.
TEST(MaterialResolved, GlowMustNeedEmitGlowmapAndAGlowTexture) {
  const auto glow_of = [](bool emit, bool glowmap) {
    fo4::test::TempDataDir dir;
    auto file = bgsm(emit);
    EXPECT_TRUE(fo4::material::set_field<bool>(file, "Glowmap", glowmap));
    put(dir, k_box_file, file);
    const auto assets = open_dir(dir);
    const auto loaded = lighting(boxed());
    return resolve_material(loaded.file, loaded.shape, assets).has_glow;
  };

  EXPECT_TRUE(glow_of(true, true));
  EXPECT_FALSE(glow_of(true, false));
  EXPECT_FALSE(glow_of(false, true));
}

// Detects: a matching swap that must re-resolve from the replacement BGSM and carry the remapping index, not just rename the path.
TEST(MaterialResolved, SwapMustReResolveFromTheReplacementMaterial) {
  fo4::test::TempDataDir dir;
  auto box = bgsm(true);
  ASSERT_TRUE(fo4::material::set_field<bool>(box, "Glowmap", true));
  put(dir, k_box_file, box);
  auto rust = bgsm();
  ASSERT_TRUE(fo4::material::set_text(rust, "DiffuseTexture", "Textures/rust_d.dds"));
  put(dir, "materials/props/rust.bgsm", rust);
  const auto assets = open_dir(dir);
  const auto loaded = lighting(boxed());
  auto rm = resolve_material(loaded.file, loaded.shape, assets);
  ASSERT_TRUE(rm.has_glow);

  const MaterialSubstitution swap{"Materials\\Props\\Box.bgsm", "Materials\\Props\\Rust.bgsm", 0.5F};
  ASSERT_TRUE(apply_material_swap(rm, loaded.file, loaded.shape, assets, swap));

  EXPECT_TRUE(rm.swapped);
  EXPECT_EQ(rm.material_path, "materials/props/rust.bgsm");
  EXPECT_EQ(rm.nif_material_path, k_box_file);
  EXPECT_EQ(rm.textures[0], "textures/rust_d.dds");
  EXPECT_FALSE(rm.has_glow);
  ASSERT_TRUE(rm.color_remapping_index.has_value());
  EXPECT_FLOAT_EQ(*rm.color_remapping_index, 0.5F);
}

// Detects: a substitution whose original is a different material that must return false and leave the material untouched.
TEST(MaterialResolved, SwapForAnotherOriginalMustChangeNothing) {
  fo4::test::TempDataDir dir;
  put(dir, k_box_file, bgsm());
  put(dir, "materials/props/rust.bgsm", bgsm());
  const auto assets = open_dir(dir);
  const auto loaded = lighting(boxed());
  auto rm = resolve_material(loaded.file, loaded.shape, assets);

  const MaterialSubstitution swap{"Materials\\Props\\Other.bgsm", "Materials\\Props\\Rust.bgsm", 0.5F};
  EXPECT_FALSE(apply_material_swap(rm, loaded.file, loaded.shape, assets, swap));

  EXPECT_FALSE(rm.swapped);
  EXPECT_EQ(rm.material_path, k_box_file);
  EXPECT_FALSE(rm.color_remapping_index.has_value());
}

// Detects: a substitution that only carries a remapping index that must keep the material and set the index.
TEST(MaterialResolved, IndexOnlySwapMustKeepTheMaterial) {
  fo4::test::TempDataDir dir;
  put(dir, k_box_file, bgsm());
  const auto assets = open_dir(dir);
  const auto loaded = lighting(boxed());
  auto rm = resolve_material(loaded.file, loaded.shape, assets);

  const MaterialSubstitution swap{"Materials\\Props\\Box.bgsm", std::nullopt, 0.25F};
  ASSERT_TRUE(apply_material_swap(rm, loaded.file, loaded.shape, assets, swap));

  EXPECT_TRUE(rm.swapped);
  EXPECT_EQ(rm.material_path, k_box_file);
  EXPECT_EQ(rm.textures[0], "textures/d.dds");
  ASSERT_TRUE(rm.color_remapping_index.has_value());
  EXPECT_FLOAT_EQ(*rm.color_remapping_index, 0.25F);
}

// Detects: a swap original written without the Materials prefix that must still match, as the records store both forms.
TEST(MaterialResolved, SwapOriginalWithoutTheMaterialsPrefixMustMatch) {
  fo4::test::TempDataDir dir;
  put(dir, k_box_file, bgsm());
  put(dir, "materials/props/rust.bgsm", bgsm());
  const auto assets = open_dir(dir);
  const auto loaded = lighting(boxed());
  auto rm = resolve_material(loaded.file, loaded.shape, assets);

  const MaterialSubstitution swap{"Props\\Box.bgsm", "Props\\Rust.bgsm", std::nullopt};

  EXPECT_TRUE(apply_material_swap(rm, loaded.file, loaded.shape, assets, swap));
  EXPECT_EQ(rm.material_path, "materials/props/rust.bgsm");
}

// Detects: a substitution list where only a later entry matches that must still apply, not stop at the first miss.
TEST(MaterialResolved, SwapListMustApplyTheFirstMatchingEntry) {
  fo4::test::TempDataDir dir;
  put(dir, k_box_file, bgsm());
  put(dir, "materials/props/rust.bgsm", bgsm());
  const auto assets = open_dir(dir);
  const auto loaded = lighting(boxed());
  auto rm = resolve_material(loaded.file, loaded.shape, assets);

  const std::vector<MaterialSubstitution> swaps{{"Materials\\Props\\Other.bgsm", std::nullopt, 0.1F},
                                                {"Materials\\Props\\Box.bgsm", "Materials\\Props\\Rust.bgsm", 0.75F}};

  EXPECT_TRUE(apply_material_swap(rm, loaded.file, loaded.shape, assets, std::span<const MaterialSubstitution>(swaps)));
  EXPECT_EQ(rm.material_path, "materials/props/rust.bgsm");
  ASSERT_TRUE(rm.color_remapping_index.has_value());
  EXPECT_FLOAT_EQ(*rm.color_remapping_index, 0.75F);
}
