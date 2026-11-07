#include "fo4/error.hpp"
#include "fo4/material/material_file.hpp"
#include "fo4/nif/material_link.hpp"
#include "fo4/nif/nif.hpp"
#include "fo4/nif/nif_write.hpp"
#include "fo4/nif/shader.hpp"
#include "nif_build.hpp"

#include <gtest/gtest.h>

#include <cstdint>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

using fo4::material::Color3;
using fo4::material::FieldValue;
using fo4::material::MaterialFile;
using fo4::material::MaterialFormat;
using fo4::nif::Transform;
using namespace nif_build;

namespace {

std::string nul(std::string_view text) {
  std::string out(text);
  out.push_back('\0');
  return out;
}

void put(MaterialFile& file, std::string name, FieldValue value) {
  file.fields.push_back(fo4::material::MaterialField{std::move(name), std::move(value)});
}

MaterialFile lighting_material() {
  MaterialFile file;
  file.format = MaterialFormat::bgsm;
  put(file, "UOffset", 0.25f);
  put(file, "VOffset", 0.5f);
  put(file, "UScale", 2.0f);
  put(file, "VScale", 3.0f);
  put(file, "Alpha", 0.5f);
  put(file, "RefractionPower", 0.25f);
  put(file, "Smoothness", 0.75f);
  put(file, "FresnelPower", 7.5f);
  put(file, "SpecularMult", 0.875f);
  put(file, "SpecularColor", Color3{0.1f, 0.2f, 0.3f});
  put(file, "EmittanceMult", 4.0f);
  put(file, "GrayscaleToPaletteScale", 0.625f);
  put(file, "RootMaterialPath", nul(""));
  put(file, "DiffuseTexture", nul("Textures\\a_d.dds"));
  put(file, "NormalTexture", nul("Textures\\a_n.dds"));
  return file;
}

MaterialFile effect_material() {
  MaterialFile file;
  file.format = MaterialFormat::bgem;
  put(file, "UOffset", 0.5f);
  put(file, "VOffset", 0.25f);
  put(file, "UScale", 4.0f);
  put(file, "VScale", 2.0f);
  put(file, "BaseColor", Color3{0.4f, 0.3f, 0.2f});
  put(file, "BaseColorScale", 1.5f);
  put(file, "FalloffStartAngle", 1.0f);
  put(file, "FalloffStopAngle", 0.5f);
  put(file, "FalloffStartOpacity", 0.75f);
  put(file, "FalloffStopOpacity", 0.25f);
  put(file, "EnvmapMinLOD", std::uint8_t{4});
  put(file, "SoftDepth", 100.0f);
  put(file, "EffectEnvironmentMappingMaskScale", 1.75f);
  put(file, "BaseTexture", nul("Textures\\e_d.dds"));
  put(file, "GrayscaleTexture", nul("Textures\\e_g.dds"));
  put(file, "EnvmapTexture", nul("Textures\\e_e.dds"));
  put(file, "NormalTexture", nul("Textures\\e_n.dds"));
  put(file, "EnvmapMaskTexture", nul("Textures\\e_m.dds"));
  return file;
}

Block texture_set(const std::vector<std::string>& paths) {
  Writer w;
  w.u32(static_cast<std::uint32_t>(paths.size()));
  for (const auto& path : paths) w.sized(path);
  return Block{"BSShaderTextureSet", w.bytes()};
}

std::vector<std::byte> lighting_nif(const std::string& material, const std::vector<std::string>& textures) {
  LightingFields fields;
  fields.shader_type = 0;
  fields.name = 1;
  fields.texture_set = 3;
  std::vector<Block> blocks{named_node_block("NiNode", 2, Transform{}, {1}),
                            shape_with_refs("BSTriShape", 0, Transform{}, k_full, 16, -1, 2, -1,
                                            triangle_vertices(), {0, 1, 2}),
                            full_lighting_shader_block(fields), texture_set(textures)};
  return build_nif({"Tri", material, "Root"}, blocks, {0});
}

std::vector<std::byte> lighting_nif_with_root(const std::string& root) {
  LightingFields fields;
  fields.shader_type = 0;
  fields.name = 1;
  fields.texture_set = 3;
  fields.root_material = 3;
  std::vector<Block> blocks{named_node_block("NiNode", 2, Transform{}, {1}),
                            shape_with_refs("BSTriShape", 0, Transform{}, k_full, 16, -1, 2, -1,
                                            triangle_vertices(), {0, 1, 2}),
                            full_lighting_shader_block(fields),
                            texture_set({"Textures\\a_d.dds", "Textures\\a_n.dds"})};
  return build_nif({"Tri", "Materials\\a.bgsm", "Root", root}, blocks, {0});
}

std::vector<std::byte> effect_nif(const std::string& material, const std::vector<std::string>& textures) {
  std::vector<Block> blocks{named_node_block("NiNode", 2, Transform{}, {1}),
                            shape_with_refs("BSTriShape", 0, Transform{}, k_full, 16, -1, 2, -1,
                                            triangle_vertices(), {0, 1, 2}),
                            effect_shader_block(1, textures)};
  return build_nif({"Tri", material, "Root"}, blocks, {0});
}

std::vector<std::string> fields_of(const std::vector<fo4::nif::MaterialMismatch>& mismatches) {
  std::vector<std::string> names;
  for (const auto& mismatch : mismatches) names.push_back(mismatch.field);
  return names;
}

}  // namespace

// regression: a NIF that agrees with its BGSM (EmitEnabled off, so no EmittanceColor) reports false mismatches.
TEST(NifMaterialLink, MatchingLightingShaderHasNoMismatches) {
  const auto nif = fo4::nif::read_nif(lighting_nif("Materials\\a.bgsm", {"Textures\\a_d.dds", "Textures\\a_n.dds"}));
  EXPECT_TRUE(fo4::nif::compare_shape_material(nif, 1, lighting_material()).empty());
  EXPECT_TRUE(fo4::nif::shape_material_texture_edits(nif, 1, lighting_material()).empty());
}

// regression: a differing shader field is dropped, or compared against the wrong BGSM field.
TEST(NifMaterialLink, ReportsEachLightingFieldThatDiffers) {
  const auto nif = fo4::nif::read_nif(lighting_nif("Materials\\a.bgsm", {"Textures\\a_d.dds", "Textures\\a_n.dds"}));
  auto file = lighting_material();
  fo4::material::set_field<float>(file, "UOffset", 0.75f);
  fo4::material::set_field<float>(file, "Alpha", 0.9f);
  fo4::material::set_field<float>(file, "Smoothness", 0.1f);
  const auto mismatches = fo4::nif::compare_shape_material(nif, 1, file);
  EXPECT_EQ(fields_of(mismatches), (std::vector<std::string>{"UOffset", "Alpha", "Smoothness"}));
  ASSERT_EQ(mismatches.size(), 3U);
  EXPECT_EQ(mismatches[1].nif_value, "0.5");
  EXPECT_EQ(mismatches[1].material_value, "0.9");
}

// regression: a BGSM texture path, relative to Textures and slash-separated, is reported as a mismatch against the NIF's rooted path.
TEST(NifMaterialLink, TexturePathsCompareRelativeToTheTexturesRoot) {
  const auto nif = fo4::nif::read_nif(lighting_nif("Materials\\a.bgsm", {"textures\\Weapons\\A_D.dds", "textures\\a_n.dds"}));
  auto file = lighting_material();
  fo4::material::set_text(file, "DiffuseTexture", "Weapons/a_d.DDS");
  EXPECT_TRUE(fo4::nif::compare_shape_material(nif, 1, file).empty());
  fo4::material::set_text(file, "DiffuseTexture", "Weapons\\other.dds");
  EXPECT_EQ(fields_of(fo4::nif::compare_shape_material(nif, 1, file)), (std::vector<std::string>{"DiffuseTexture"}));
}

// regression: SmoothSpec filled into slot 2 overwrites the Glow texture, where vanilla keeps SmoothSpec in slot 7.
TEST(NifMaterialLink, SmoothSpecFillsSlotSevenAndKeepsGlowInSlotTwo) {
  const auto nif = fo4::nif::read_nif(lighting_nif("Materials\\a.bgsm", {"Textures\\a_d.dds", "Textures\\a_n.dds", "Textures\\a_g.dds"}));
  auto file = lighting_material();
  put(file, "GlowTexture", nul("Textures\\a_g.dds"));
  put(file, "SmoothSpecTexture", nul("Textures\\a_s.dds"));
  EXPECT_EQ(fields_of(fo4::nif::compare_shape_material(nif, 1, file)), (std::vector<std::string>{"SmoothSpecTexture"}));
  const auto edits = fo4::nif::shape_material_texture_edits(nif, 1, file);
  ASSERT_EQ(edits.size(), 1U);
  EXPECT_EQ(edits[0].key, "tex7");
  EXPECT_EQ(std::get<std::string>(edits[0].value), "Textures\\a_s.dds");
}

// Detects: a change that skips empty material texture fields, so a fill keeps a NIF texture path the material has cleared.
TEST(NifMaterialLink, EmptyMaterialTextureClearsTheNifSlot) {
  const auto nif = fo4::nif::read_nif(lighting_nif("Materials\\a.bgsm", {"Textures\\a_d.dds", "Textures\\a_n.dds", "Textures\\a_g.dds"}));
  auto file = lighting_material();
  put(file, "GlowTexture", nul(""));
  EXPECT_EQ(fields_of(fo4::nif::compare_shape_material(nif, 1, file)), (std::vector<std::string>{"GlowTexture"}));
  const auto edits = fo4::nif::shape_material_texture_edits(nif, 1, file);
  ASSERT_EQ(edits.size(), 1U);
  EXPECT_EQ(edits[0].key, "tex2");
  EXPECT_EQ(std::get<std::string>(edits[0].value), "");
  const auto filled = fo4::nif::read_nif(fo4::nif::apply_edits(nif, edits));
  const auto textures = fo4::nif::read_lighting_shader(filled, 2).textures;
  ASSERT_EQ(textures.size(), 3U);
  EXPECT_EQ(textures[2], "");
}

// regression: a BGEM on a lighting shader is compared field by field as if it were a BGSM.
TEST(NifMaterialLink, BgemOnLightingShaderIsOnlyAFormatMismatch) {
  const auto nif = fo4::nif::read_nif(lighting_nif("Materials\\a.bgsm", {"Textures\\a_d.dds", "Textures\\a_n.dds"}));
  const auto mismatches = fo4::nif::compare_shape_material(nif, 1, effect_material());
  ASSERT_EQ(mismatches.size(), 1U);
  EXPECT_EQ(mismatches[0].field, "Format");
  EXPECT_EQ(mismatches[0].material_value, "BGEM");
}

// regression: BGEM fields or effect texture slots read through the lighting layout, so a BaseColor or NormalTexture change reports the wrong field or slot.
TEST(NifMaterialLink, EffectMaterialComparesItsFieldsAndFillsItsSlots) {
  const auto nif = fo4::nif::read_nif(effect_nif(
      "Materials\\e.bgem", {"Textures\\e_d.dds", "Textures\\e_g.dds", "Textures\\e_e.dds", "Textures\\e_n.dds",
                            "Textures\\e_m.dds"}));
  auto file = effect_material();
  EXPECT_TRUE(fo4::nif::compare_shape_material(nif, 1, file).empty());
  fo4::material::set_field<float>(file, "BaseColorScale", 2.0f);
  fo4::material::set_text(file, "NormalTexture", "Textures\\e_n2.dds");
  EXPECT_EQ(fields_of(fo4::nif::compare_shape_material(nif, 1, file)),
            (std::vector<std::string>{"BaseColorScale", "NormalTexture"}));
  const auto edits = fo4::nif::shape_material_texture_edits(nif, 1, file);
  ASSERT_EQ(edits.size(), 1U);
  EXPECT_EQ(edits[0].id, 1U);
  EXPECT_EQ(edits[0].key, "tex3");
  EXPECT_EQ(std::get<std::string>(edits[0].value), "Textures\\e_n2.dds");
}

// Detects: the envmap guard on EffectEnvironmentMappingMaskScale also drops the field at version 130, so a changed mask scale goes unreported.
TEST(NifMaterialLink, EffectMaskScaleMismatchIsReportedOnVersion130) {
  const auto nif = fo4::nif::read_nif(effect_nif(
      "Materials\\e.bgem", {"Textures\\e_d.dds", "Textures\\e_g.dds", "Textures\\e_e.dds", "Textures\\e_n.dds",
                            "Textures\\e_m.dds"}));
  auto file = effect_material();
  fo4::material::set_field<float>(file, "EffectEnvironmentMappingMaskScale", 2.0f);
  EXPECT_EQ(fields_of(fo4::nif::compare_shape_material(nif, 1, file)),
            (std::vector<std::string>{"EffectEnvironmentMappingMaskScale"}));
}

// regression: fill rewrites a matching slot or writes the wrong key, so a second fill is not a no-op.
TEST(NifMaterialLink, FillWritesOnlyTheTextureSlotThatDiffers) {
  const auto nif = fo4::nif::read_nif(lighting_nif("Materials\\a.bgsm", {"Textures\\old_d.dds", "Textures\\a_n.dds"}));
  auto file = lighting_material();
  fo4::material::set_text(file, "DiffuseTexture", "weapons/new_d.dds");
  const auto edits = fo4::nif::shape_material_texture_edits(nif, 1, file);
  ASSERT_EQ(edits.size(), 1U);
  EXPECT_EQ(edits[0].id, 1U);
  EXPECT_EQ(edits[0].key, "tex0");
  EXPECT_EQ(std::get<std::string>(edits[0].value), "textures\\weapons\\new_d.dds");
  const auto filled = fo4::nif::read_nif(fo4::nif::apply_edits(nif, edits));
  const auto textures = fo4::nif::read_lighting_shader(filled, 2).textures;
  ASSERT_EQ(textures.size(), 2U);
  EXPECT_EQ(textures[0], "textures\\weapons\\new_d.dds");
  EXPECT_EQ(textures[1], "Textures\\a_n.dds");
  EXPECT_TRUE(fo4::nif::shape_material_texture_edits(filled, 1, file).empty());
  EXPECT_TRUE(fo4::nif::compare_shape_material(filled, 1, file).empty());
}

// regression: a shape whose material differs by case, slash or a developer prefix is left out of the list of shapes using it.
TEST(NifMaterialLink, ShapesUsingMaterialMatchPathsLikeTheEngine) {
  const auto nif = fo4::nif::read_nif(build_nif(
      {"A", "B", "C", "Root", "Materials\\Thing.BGSM", "materials/THING.bgsm", "Materials\\Other.BGSM",
       "C:\\Projects\\Fallout4\\Build\\PC\\Data\\materials\\x.bgsm"},
      {named_node_block("NiNode", 3, Transform{}, {1, 3, 5, 7}),
       shape_with_refs("BSTriShape", 0, Transform{}, k_full, 16, -1, 2, -1, triangle_vertices(), {0, 1, 2}),
       full_lighting_shader_block([] { LightingFields f; f.name = 4; return f; }()),
       shape_with_refs("BSTriShape", 1, Transform{}, k_full, 16, -1, 4, -1, triangle_vertices(), {0, 1, 2}),
       full_lighting_shader_block([] { LightingFields f; f.name = 5; return f; }()),
       shape_with_refs("BSTriShape", 2, Transform{}, k_full, 16, -1, 6, -1, triangle_vertices(), {0, 1, 2}),
       full_lighting_shader_block([] { LightingFields f; f.name = 6; return f; }()),
       shape_with_refs("BSTriShape", 2, Transform{}, k_full, 16, -1, 8, -1, triangle_vertices(), {0, 1, 2}),
       full_lighting_shader_block([] { LightingFields f; f.name = 7; return f; }())},
      {0}));
  EXPECT_EQ(fo4::nif::shapes_using_material(nif, "Materials/thing.bgsm"), (std::vector<std::uint32_t>{1, 3}));
  EXPECT_EQ(fo4::nif::shapes_using_material(nif, "Materials\\Other.BGSM"), (std::vector<std::uint32_t>{5}));
  EXPECT_EQ(fo4::nif::shapes_using_material(nif, "materials\\x.bgsm"), (std::vector<std::uint32_t>{7}));
  EXPECT_TRUE(fo4::nif::shapes_using_material(nif, "").empty());
}

// regression: a shape with no shader property is compared against a default shader instead of being refused.
TEST(NifMaterialLink, ShapeWithoutShaderIsRefused) {
  const auto nif = fo4::nif::read_nif(build_nif(
      {"Tri"}, {shape_with_refs("BSTriShape", 0, Transform{}, k_full, 16, -1, -1, -1, triangle_vertices(), {0, 1, 2})},
      {0}));
  expect_parse_error([&] { fo4::nif::compare_shape_material(nif, 0, lighting_material()); },
                     "shape has no shader property");
}

// regression: an empty NIF root material, which is normal in vanilla, is reported against every BGSM that names a template.
TEST(NifMaterialLink, RootMaterialIsReportedOnlyWhenBothSidesNameOne) {
  auto file = lighting_material();
  fo4::material::set_text(file, "RootMaterialPath", "template/x.bgsm");
  const auto empty = fo4::nif::read_nif(lighting_nif("Materials\\a.bgsm", {"Textures\\a_d.dds", "Textures\\a_n.dds"}));
  EXPECT_TRUE(fo4::nif::compare_shape_material(empty, 1, file).empty());
  const auto named = fo4::nif::read_nif(lighting_nif_with_root("Materials\\a_root.bgsm"));
  EXPECT_EQ(fields_of(fo4::nif::compare_shape_material(named, 1, file)), (std::vector<std::string>{"RootMaterialPath"}));
  fo4::material::set_text(file, "RootMaterialPath", "");
  EXPECT_TRUE(fo4::nif::compare_shape_material(named, 1, file).empty());
}

// regression: an absolute developer material path never resolves, so every shape that names one is reported as missing.
TEST(NifMaterialLink, DeveloperMaterialPathsMapToTheDataRelativeName) {
  EXPECT_EQ(fo4::nif::data_relative_material_path(
                "C:\\Projects\\Fallout4\\Build\\PC\\Data\\materials\\Landscape\\Rocks\\RockRiverStones_Wet.BGSM"),
            "materials\\Landscape\\Rocks\\RockRiverStones_Wet.BGSM");
  EXPECT_EQ(fo4::nif::data_relative_material_path("C:\\projects\\Build\\data\\Materials\\x.bgsm"), "Materials\\x.bgsm");
  EXPECT_EQ(fo4::nif::data_relative_material_path("a/b/materials/c.bgsm"), "materials\\c.bgsm");
  EXPECT_EQ(fo4::nif::data_relative_material_path("Materials\\Armor\\a.bgsm"), "Materials\\Armor\\a.bgsm");
}

// regression: a fill on a shader with no texture set reaches the writer, which fails on every edit instead of refusing up front.
TEST(NifMaterialLink, FillRefusesAShaderWithNoTextureSet) {
  LightingFields fields;
  fields.shader_type = 0;
  fields.name = 1;
  const auto nif = fo4::nif::read_nif(build_nif(
      {"Tri", "Materials\\a.bgsm", "Root"},
      {named_node_block("NiNode", 2, Transform{}, {1}),
       shape_with_refs("BSTriShape", 0, Transform{}, k_full, 16, -1, 2, -1, triangle_vertices(), {0, 1, 2}),
       full_lighting_shader_block(fields)},
      {0}));
  expect_parse_error([&] { fo4::nif::shape_material_texture_edits(nif, 1, lighting_material()); },
                     "no texture set to fill");
}
