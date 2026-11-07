#include "fo4/error.hpp"
#include "fo4/nif/collision.hpp"
#include "fo4/nif/nif.hpp"
#include "fo4/nif/nif_write.hpp"
#include "fo4/nif/shader.hpp"
#include "nif_build.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace {

using fo4::nif::apply_edits;
using fo4::nif::Edit;
using fo4::nif::EditValue;
using fo4::nif::NifFile;
using fo4::nif::read_nif;
using nif_build::Block;
using nif_build::expect_parse_error;
using nif_build::Writer;

constexpr std::uint32_t k_root = 0;
constexpr std::uint32_t k_shape = 1;

EditValue names(std::vector<std::string> values) { return EditValue{std::move(values)}; }
EditValue as_text(const char* value) { return EditValue{std::string{value}}; }
EditValue as_number(float value) { return EditValue{value}; }
EditValue numbers(std::vector<float> values) { return EditValue{std::move(values)}; }

std::span<const std::byte> block_bytes(std::span<const std::byte> file, const NifFile& nif, std::uint32_t block) {
  const auto& info = nif.blocks()[block];
  return file.subspan(info.offset, info.size);
}

bool same_bytes(std::span<const std::byte> left, std::span<const std::byte> right) {
  return left.size() == right.size() && std::equal(left.begin(), left.end(), right.begin());
}

void expect_rejected(const NifFile& nif, const Edit& edit) {
  const std::vector<Edit> edits{edit};
  EXPECT_THROW(apply_edits(nif, edits), fo4::parse_error);
}

Block collision_object_block(std::int32_t system, std::uint16_t flags, std::uint32_t body_id) {
  Writer w;
  w.i32(-1);
  w.u16(flags);
  w.i32(system);
  w.u32(body_id);
  return Block{"bhkNPCollisionObject", w.bytes()};
}

Block physics_system_block() {
  Writer w;
  w.u32(8);
  for (int i = 0; i < 8; ++i) w.u8(static_cast<std::uint8_t>(0xA0 + i));
  return Block{"bhkPhysicsSystem", w.bytes()};
}

Block bsx_block(std::uint32_t flags) {
  Writer w;
  w.i32(-1);
  w.u32(flags);
  return Block{"BSXFlags", w.bytes()};
}

Block shape_on_shader(std::int32_t shader, std::int32_t alpha) {
  return nif_build::shape_with_refs("BSTriShape", 0, {}, 0, 16, -1, shader, alpha, {}, {});
}

// Constraint: every fixture block names string 0, so the header must carry at least one string.
const std::vector<std::string> k_names{"shape", "fx"};

std::vector<std::byte> lighting_nif(const nif_build::LightingFields& fields) {
  return nif_build::build_nif(k_names, {nif_build::node_block("NiNode", {}, {1}), shape_on_shader(2, -1),
                                        nif_build::full_lighting_shader_block(fields)},
                              {k_root});
}

Block texture_set_block(const std::vector<std::string>& slots) {
  Writer w;
  w.u32(static_cast<std::uint32_t>(slots.size()));
  for (const auto& slot : slots) w.sized(slot);
  return Block{"BSShaderTextureSet", w.bytes()};
}

std::vector<std::byte> textured_lighting_nif() {
  auto fields = nif_build::LightingFields{};
  fields.texture_set = 3;
  return nif_build::build_nif(k_names, {nif_build::node_block("NiNode", {}, {1}), shape_on_shader(2, -1),
                                        nif_build::full_lighting_shader_block(fields),
                                        texture_set_block({"textures\\armor\\a_d.dds", "textures\\armor\\a_n.dds"})},
                              {k_root});
}

std::vector<std::byte> effect_nif() {
  return nif_build::build_nif(k_names, {nif_build::node_block("NiNode", {}, {1}), shape_on_shader(2, -1),
                                        nif_build::effect_shader_block(-1, {"textures\\armor\\a_d.dds"})},
                              {k_root});
}

}  // namespace

TEST(NifWriteBlocks, LightingFlagsAreSetFromReadSideNames) {
  // Invariant: a flag rename on either side would set a different bit than the reader reports
  const auto nif = read_nif(lighting_nif(nif_build::LightingFields{}));
  const std::vector<Edit> edits{
      Edit{k_shape, "shaderFlags", names({"Specular", "Double_Sided", "Refraction_Writes_Depth"})}};
  const auto out = read_nif(apply_edits(nif, edits));
  const auto flags = fo4::nif::read_lighting_shader(out, 2).flags;
  EXPECT_EQ(flags.word1, 0x1U);
  EXPECT_EQ(flags.word2, 0x80000010U);
  EXPECT_EQ(fo4::nif::shader_flag_names(flags),
            (std::vector<std::string>{"Specular", "Double_Sided", "Refraction_Writes_Depth"}));
  EXPECT_FLOAT_EQ(fo4::nif::read_lighting_shader(out, 2).smoothness,
                  nif_build::LightingFields{}.smoothness);
  expect_rejected(nif, Edit{k_shape, "shaderFlags", names({"NoSuchFlag"})});
  expect_rejected(nif, Edit{k_shape, "shaderFlags", as_number(1.0f)});
}

TEST(NifWriteBlocks, LightingBaseFieldsRoundTripThroughTheReader) {
  // Invariant: the writer must place every base field where the reader reads it, gated fields included
  const auto nif = read_nif(lighting_nif(nif_build::LightingFields{}));
  const std::vector<Edit> edits{
      Edit{k_shape, "smoothness", as_number(0.125f)},
      Edit{k_shape, "uvScale", numbers({4.0f, 5.0f})},
      Edit{k_shape, "alpha", as_number(0.75f)},
      Edit{k_shape, "specularColor", numbers({0.25f, 0.5f, 0.75f})},
      Edit{k_shape, "fresnelPower", as_number(6.5f)},
      Edit{k_shape, "wetnessMetalness", as_number(0.875f)},
      Edit{k_shape, "emissiveMultiple", as_number(2.0f)},
  };
  const auto shader = fo4::nif::read_lighting_shader(read_nif(apply_edits(nif, edits)), 2);
  EXPECT_FLOAT_EQ(shader.smoothness, 0.125f);
  EXPECT_FLOAT_EQ(shader.uv_scale[0], 4.0f);
  EXPECT_FLOAT_EQ(shader.uv_scale[1], 5.0f);
  EXPECT_FLOAT_EQ(shader.alpha, 0.75f);
  EXPECT_FLOAT_EQ(shader.specular_color[2], 0.75f);
  EXPECT_FLOAT_EQ(shader.fresnel_power, 6.5f);
  EXPECT_FLOAT_EQ(shader.wetness.metalness, 0.875f);
  EXPECT_FLOAT_EQ(shader.emissive_multiple, 2.0f);
  EXPECT_EQ(shader.texture_set_ref, -1);
}

// Detects: a colour or scalar written at another field's offset, and a value the stored float must reject.
TEST(NifWriteBlocks, LightingColoursAndScalarsRoundTripAndRefuseBadWidths) {
  const auto nif = read_nif(lighting_nif(nif_build::LightingFields{}));
  const std::vector<Edit> edits{
      Edit{k_shape, "emissiveColor", numbers({0.125f, 0.25f, 0.375f})},
      Edit{k_shape, "specularStrength", as_number(0.625f)},
      Edit{k_shape, "refractionStrength", as_number(0.5f)},
      Edit{k_shape, "subsurfaceRolloff", as_number(0.25f)},
      Edit{k_shape, "glossiness", as_number(0.5f)},
  };
  const auto shader = fo4::nif::read_lighting_shader(read_nif(apply_edits(nif, edits)), 2);
  EXPECT_FLOAT_EQ(shader.emissive_color[0], 0.125f);
  EXPECT_FLOAT_EQ(shader.emissive_color[2], 0.375f);
  EXPECT_FLOAT_EQ(shader.specular_strength, 0.625f);
  EXPECT_FLOAT_EQ(shader.refraction_strength, 0.5f);
  EXPECT_FLOAT_EQ(shader.subsurface_rolloff, 0.25f);
  EXPECT_FLOAT_EQ(shader.smoothness, 0.5f);
  expect_rejected(nif, Edit{k_shape, "emissiveColor", numbers({1.0f, 2.0f})});
  expect_rejected(nif, Edit{k_shape, "alpha", as_number(std::numeric_limits<float>::infinity())});
}

// Detects: a texture path or slot the writer must refuse, so a shader cannot store an unusable reference.
TEST(NifWriteBlocks, LightingTextureSlotsRoundTripAndValidateThePath) {
  const auto nif = read_nif(textured_lighting_nif());
  EXPECT_EQ(fo4::nif::read_shader(nif, 2).textures.size(), 2U);
  const auto replaced = read_nif(
      apply_edits(nif, std::vector<Edit>{Edit{k_shape, "tex1", as_text("textures\\armor\\a_s.dds")}}));
  EXPECT_EQ(fo4::nif::read_shader(replaced, 2).textures[1], "textures\\armor\\a_s.dds");
  const auto cleared = read_nif(apply_edits(replaced, std::vector<Edit>{Edit{k_shape, "tex0", as_text("")}}));
  EXPECT_EQ(fo4::nif::read_shader(cleared, 2).textures[0], "");
  expect_rejected(nif, Edit{k_shape, "tex0", EditValue{std::string(261, 'a')}});
  expect_rejected(nif, Edit{k_shape, "tex0", EditValue{std::string("textures\\a") + "\x01" + ".dds"}});
  expect_rejected(nif, Edit{k_shape, "tex0", EditValue{std::string("textures\\a") + static_cast<char>(0xFF) + ".dds"}});
  expect_parse_error(
      [&nif] {
        apply_edits(nif, std::vector<Edit>{Edit{k_shape, "tex8", as_text("textures\\armor\\a_d.dds")}});
      },
      "does not store");
  expect_parse_error([&nif] { apply_edits(nif, std::vector<Edit>{Edit{3, "tex8", as_text("")}}); }, "does not store");
  const auto longest =
      read_nif(apply_edits(nif, std::vector<Edit>{Edit{k_shape, "tex0", EditValue{std::string(260, 'a')}}}));
  EXPECT_EQ(fo4::nif::read_shader(longest, 2).textures[0].size(), 260U);
  expect_rejected(read_nif(effect_nif()), Edit{k_shape, "sourceTexture", EditValue{std::string(261, 'a')}});
}

TEST(NifWriteBlocks, EnvMapTailRoundTripsScaleAndBothScreenSpaceBytes) {
  // Invariant: the two env-map bytes follow the scale, so writing the scale must not land on them
  auto fields = nif_build::LightingFields{};
  fields.shader_type = 1;
  const auto nif = read_nif(lighting_nif(fields));
  const std::vector<Edit> edits{
      Edit{k_shape, "envMapScale", as_number(3.5f)},
      Edit{k_shape, "envMapSSR", as_number(1.0f)},
      Edit{k_shape, "envMapWetnessSSR", as_number(0.0f)},
  };
  const auto shader = fo4::nif::read_lighting_shader(read_nif(apply_edits(nif, edits)), 2);
  EXPECT_FLOAT_EQ(shader.env_map_scale, 3.5f);
  EXPECT_EQ(shader.env_map_ssr, 1);
  EXPECT_EQ(shader.env_map_wetness_ssr, 0);
  EXPECT_EQ(shader.tail_bytes, 6U);
  expect_rejected(nif, Edit{k_shape, "skinTintColor", numbers({0.1f, 0.2f, 0.3f})});
}

TEST(NifWriteBlocks, EffectShaderTextureGrowthKeepsLaterSlotsCorrect) {
  // Invariant: a longer source path shifts every later slot, so the greyscale write must use the new offsets
  const auto bytes = nif_build::build_nif(
      k_names, {nif_build::node_block("NiNode", {}, {1}), shape_on_shader(2, -1),
                nif_build::effect_shader_block(1, {"fx\\a.dds", "fx\\grey.dds"})},
      {k_root});
  const auto nif = read_nif(bytes);
  const std::vector<Edit> edits{
      Edit{k_shape, "sourceTexture", as_text("fx\\a_much_longer_source.dds")},
      Edit{k_shape, "greyscaleTexture", as_text("fx\\new_grey.dds")},
      Edit{k_shape, "falloffStopOpacity", as_number(0.9f)},
      Edit{k_shape, "baseColorAlpha", as_number(0.25f)},
  };
  const auto out_bytes = apply_edits(nif, edits);
  const auto out = read_nif(out_bytes);
  const auto shader = fo4::nif::read_effect_shader(out, 2);
  EXPECT_EQ(shader.source_texture, "fx\\a_much_longer_source.dds");
  EXPECT_EQ(shader.textures[0], "fx\\a_much_longer_source.dds");
  EXPECT_EQ(shader.textures[1], "fx\\new_grey.dds");
  EXPECT_EQ(shader.textures[2], "");
  EXPECT_FLOAT_EQ(shader.falloff_stop_opacity, 0.9f);
  EXPECT_FLOAT_EQ(shader.base_color[3], 0.25f);
  EXPECT_EQ(block_bytes(out_bytes, out, 2).size(), block_bytes(bytes, nif, 2).size() + 23U);
  expect_rejected(nif, Edit{k_shape, "flags", as_number(1.0f)});
}

TEST(NifWriteBlocks, AlphaPropertyDecodesAfterItsEdits) {
  // Invariant: flags bits must be toggled in place, so blend, test and their neighbours stay independent
  const auto bytes = nif_build::build_nif(
      k_names, {nif_build::node_block("NiNode", {}, {1}), shape_on_shader(-1, 2),
                nif_build::alpha_block(0x12EDU, 128)},
      {k_root});
  const auto nif = read_nif(bytes);
  const std::vector<Edit> edits{
      Edit{k_shape, "alphaThreshold", as_number(64.0f)},
      Edit{k_shape, "alphaBlend", as_number(0.0f)},
      Edit{k_shape, "alphaTest", as_number(0.0f)},
  };
  const auto alpha = fo4::nif::read_alpha(read_nif(apply_edits(nif, edits)), 2);
  EXPECT_EQ(alpha.flags, 0x10ECU);
  EXPECT_FALSE(alpha.blend_enabled);
  EXPECT_FALSE(alpha.test_enabled);
  EXPECT_EQ(alpha.threshold, 64);
  EXPECT_EQ(alpha.source_blend, fo4::nif::AlphaBlendMode::SourceAlpha);
  EXPECT_EQ(alpha.test_function, fo4::nif::AlphaTestFunction::Greater);
  expect_rejected(nif, Edit{2, "testFunction", as_text("Always")});
  expect_rejected(nif, Edit{k_shape, "alphaThreshold", as_number(300.0f)});
}

TEST(NifWriteBlocks, BsxEditorMarkerTogglesOnlyBitFive) {
  // Invariant: the marker edit must keep the other BSX bits, which the engine reads for BSX behaviour
  const auto nif = read_nif(
      nif_build::build_nif(k_names, {nif_build::node_block("NiNode", {}, {}), bsx_block(0x480U)}, {k_root}));
  const std::vector<Edit> on{Edit{1, "editorMarker", as_number(1.0f)}};
  const auto marked = fo4::nif::read_bsx_flags(read_nif(apply_edits(nif, on)), 1);
  EXPECT_EQ(marked, 0x4A0U);
  EXPECT_TRUE(fo4::nif::is_editor_marker(marked));
  const std::vector<Edit> off{Edit{1, "editorMarker", as_number(0.0f)}};
  EXPECT_EQ(fo4::nif::read_bsx_flags(read_nif(apply_edits(nif, off)), 1), 0x480U);
}

TEST(NifWriteBlocks, CollisionObjectFlagsAndBodyIdRoundTripAndKeepTheBlob) {
  // Invariant: the packfile bytes must pass through untouched, or the collision system stops loading
  const auto bytes = nif_build::build_nif(
      k_names, {nif_build::node_block("NiNode", {}, {}), collision_object_block(2, 0x0800U, 7U),
                physics_system_block()},
      {k_root});
  const auto nif = read_nif(bytes);
  // Constraint: an Edit value is an f32, so 0xFFFF0000 is the largest body id that keeps every bit it sets.
  const std::vector<Edit> edits{
      Edit{1, "flags", as_number(2052.0f)},
      Edit{1, "bodyId", as_number(4294901760.0f)},
  };
  const auto out_bytes = apply_edits(nif, edits);
  const auto out = read_nif(out_bytes);
  const auto object = fo4::nif::read_collision_object(out, 1);
  EXPECT_EQ(object.flags, 0x0804U);
  EXPECT_EQ(object.body_id, 0xFFFF0000U);
  EXPECT_EQ(object.system, 2);
  EXPECT_EQ(object.target, -1);
  EXPECT_TRUE(same_bytes(block_bytes(out_bytes, out, 2), block_bytes(bytes, nif, 2)));
  const auto blobs = fo4::nif::read_collision_blobs(out);
  ASSERT_EQ(blobs.size(), 1U);
  EXPECT_EQ(blobs[0].flags, 0x0804U);
  EXPECT_EQ(blobs[0].body_id, 0xFFFF0000U);
  // Invariant: every named collision flag bit survives a save and reparse as its own bit.
  const std::uint16_t named[] = {0x4U, 0x8U, 0x20U, 0x40U, 0x80U, 0x400U, 0x800U};
  std::uint16_t combined = 0;
  for (const auto bit : named) combined = static_cast<std::uint16_t>(combined | bit);
  const std::vector<Edit> flag_edits{Edit{1, "flags", as_number(combined)}};
  const auto flags_out = read_nif(apply_edits(nif, flag_edits));
  EXPECT_EQ(fo4::nif::read_collision_object(flags_out, 1).flags, combined);
  for (const auto bit : named) {
    EXPECT_EQ(fo4::nif::read_collision_object(flags_out, 1).flags & bit, bit) << bit;
  }
  // Invariant: a single-bit edit must clear every other bit and set only its own.
  for (const auto bit : named) {
    const std::vector<Edit> one{Edit{1, "flags", as_number(static_cast<float>(bit))}};
    EXPECT_EQ(fo4::nif::read_collision_object(read_nif(apply_edits(nif, one)), 1).flags, bit) << bit;
  }
  expect_rejected(nif, Edit{1, "system", as_number(1.0f)});
  // Invariant: an Edit carries an f32, so 2^32 is the first body id a word value cannot take.
  expect_rejected(nif, Edit{1, "bodyId", as_number(4294967296.0f)});
}

TEST(NifWriteBlocks, RimlightRefusesTheBacklightPlaceholderInEitherDirection) {
  // Invariant: Backlight Power is stored only behind a FLT_MAX rimlight, so a rimlight edit must neither add nor remove that field.
  const auto stored = read_nif(lighting_nif(nif_build::LightingFields{}));
  ASSERT_TRUE(fo4::nif::read_lighting_shader(stored, 2).has_backlight);
  expect_rejected(stored, Edit{k_shape, "rimlightPower", as_number(1.0f)});

  auto plain_fields = nif_build::LightingFields{};
  plain_fields.rimlight_power = 0.5f;
  const auto plain = read_nif(lighting_nif(plain_fields));
  ASSERT_FALSE(fo4::nif::read_lighting_shader(plain, 2).has_backlight);
  expect_rejected(plain, Edit{k_shape, "rimlightPower", as_number(std::numeric_limits<float>::max())});

  const std::vector<Edit> edits{Edit{k_shape, "rimlightPower", as_number(0.75f)}};
  const auto shader = fo4::nif::read_lighting_shader(read_nif(apply_edits(plain, edits)), 2);
  EXPECT_FLOAT_EQ(shader.rimlight_power, 0.75f);
  EXPECT_FALSE(shader.has_backlight);
  EXPECT_FLOAT_EQ(shader.grayscale_to_palette_scale, plain_fields.grayscale_to_palette_scale);
  EXPECT_FLOAT_EQ(shader.fresnel_power, plain_fields.fresnel_power);
}

TEST(NifWriteBlocks, TailKeysLandOnlyInTheFieldsOfTheirOwnMaterialType) {
  // Invariant: the tail is one region shared by every material type, so a type's keys must not land on another type's fields.
  auto fields = nif_build::LightingFields{};
  fields.shader_type = 11;
  const auto nif = read_nif(lighting_nif(fields));
  const std::vector<Edit> edits{
      Edit{k_shape, "multiLayerThickness", as_number(6.0f)},
      Edit{k_shape, "multiLayerRefractionScale", as_number(0.5f)},
      Edit{k_shape, "multiLayerInnerScale", numbers({2.5f, 3.5f})},
      Edit{k_shape, "multiLayerEnvMapStrength", as_number(0.75f)},
  };
  const auto shader = fo4::nif::read_lighting_shader(read_nif(apply_edits(nif, edits)), 2);
  ASSERT_TRUE(shader.has_multi_layer_parallax);
  EXPECT_FLOAT_EQ(shader.multi_layer_parallax[0], 6.0f);
  EXPECT_FLOAT_EQ(shader.multi_layer_parallax[1], 0.5f);
  EXPECT_FLOAT_EQ(shader.multi_layer_parallax[2], 2.5f);
  EXPECT_FLOAT_EQ(shader.multi_layer_parallax[3], 3.5f);
  EXPECT_FLOAT_EQ(shader.multi_layer_parallax[4], 0.75f);
  EXPECT_FALSE(shader.has_env_map);
  expect_rejected(nif, Edit{k_shape, "eyeCubemapScale", as_number(1.0f)});
}

TEST(NifWriteBlocks, SharedAlphaPropertyIsCopiedBeforeAShapeEdit) {
  // Invariant: two shapes sharing one NiAlphaProperty must not both change when one of them is edited.
  const auto bytes = nif_build::build_nif(
      k_names, {nif_build::node_block("NiNode", {}, {1, 2}), shape_on_shader(-1, 3), shape_on_shader(-1, 3),
                nif_build::alpha_block(0x0201U, 128)},
      {k_root});
  const auto nif = read_nif(bytes);
  const std::vector<Edit> edits{Edit{1, "alphaBlend", as_number(0.0f)}};
  const auto out_bytes = apply_edits(nif, edits);
  const auto out = read_nif(out_bytes);
  ASSERT_EQ(out.blocks().size(), nif.blocks().size() + 1);
  const auto shapes = fo4::nif::read_shapes(out);
  ASSERT_EQ(shapes.size(), 2U);
  EXPECT_EQ(shapes[0].alpha_ref, 4);
  EXPECT_EQ(shapes[1].alpha_ref, 3);
  // Invariant: the copy takes the edit and the shared original keeps its own bytes.
  EXPECT_TRUE(fo4::nif::read_alpha(out, 3).blend_enabled);
  EXPECT_EQ(fo4::nif::read_alpha(out, 3).flags, 0x0201U);
  EXPECT_FALSE(fo4::nif::read_alpha(out, 4).blend_enabled);
  EXPECT_EQ(fo4::nif::read_alpha(out, 4).flags, 0x0200U);
  EXPECT_TRUE(same_bytes(block_bytes(out_bytes, out, 3), block_bytes(bytes, nif, 3)));
}

TEST(NifWriteBlocks, JsonEditsCarryFlagNamesAndRejectUnusableValues) {
  // Invariant: the editor's JSON path must reach the same flag-name edit the C++ API accepts
  const auto nif = read_nif(lighting_nif(nif_build::LightingFields{}));
  const auto out =
      fo4::nif::apply_edits_json(nif, R"([{"id":1,"key":"shaderFlags","value":["Specular","Double_Sided"]}])");
  const auto flags = fo4::nif::read_lighting_shader(read_nif(out), 2).flags;
  EXPECT_EQ(flags.word1, 0x1U);
  EXPECT_EQ(flags.word2, 0x10U);
  EXPECT_THROW(fo4::nif::read_edits_json("[{\"id\":1,\"key\":\"shaderFlags\",\"value\":[{\"a\":1}]}]"),
               fo4::parse_error);
  EXPECT_THROW(fo4::nif::read_edits_json("{\"id\":1}"), fo4::parse_error);
  EXPECT_THROW(fo4::nif::read_edits_json("not json"), fo4::parse_error);
  EXPECT_THROW(fo4::nif::read_edits_json("[{\"id\":-1,\"key\":\"name\",\"value\":\"x\"}]"), fo4::parse_error);
  // Invariant: a whole number an f32 cannot hold is refused, never rounded into a different id.
  EXPECT_THROW(fo4::nif::read_edits_json("[{\"id\":1,\"key\":\"bodyId\",\"value\":4294967295}]"),
               fo4::parse_error);
  EXPECT_NO_THROW(fo4::nif::read_edits_json("[{\"id\":1,\"key\":\"bodyId\",\"value\":16777216}]"));
}

TEST(NifWriteBlocks, JsonFlagTogglesBooleansAndAnEmptyListReachTheWriter) {
  // Invariant: the editor sends a shader flag list as {key,label,on} objects and every other toggle as a JSON boolean.
  const auto nif = read_nif(lighting_nif(nif_build::LightingFields{}));
  const auto toggles = fo4::nif::apply_edits_json(
      nif,
      R"([{"id":1,"key":"shaderFlags","value":[{"key":"Specular","label":"Specular","on":true},{"key":"Skinned","label":"Skinned","on":false}]}])");
  const auto flags = fo4::nif::read_lighting_shader(read_nif(toggles), 2).flags;
  EXPECT_EQ(flags.word1, 0x1U);
  EXPECT_EQ(flags.word2, 0x0U);

  const auto cleared = fo4::nif::apply_edits_json(nif, R"([{"id":1,"key":"shaderFlags","value":[]}])");
  EXPECT_EQ(fo4::nif::read_lighting_shader(read_nif(cleared), 2).flags.value(), 0U);

  const auto off = fo4::nif::apply_edits_json(nif, R"([{"id":1,"key":"envMapSSR","value":false}])");
  EXPECT_EQ(fo4::nif::read_lighting_shader(read_nif(off), 2).env_map_ssr, 0);
  const auto on = fo4::nif::apply_edits_json(nif, R"([{"id":1,"key":"envMapSSR","value":true}])");
  EXPECT_EQ(fo4::nif::read_lighting_shader(read_nif(on), 2).env_map_ssr, 1);

  EXPECT_THROW(fo4::nif::read_edits_json(R"([{"id":1,"key":"envMapSSR","value":{"on":true}}])"),
               fo4::parse_error);
  EXPECT_THROW(fo4::nif::read_edits_json(R"([{"id":1,"key":"shaderFlags","value":[{"key":"Specular"}]}])"),
               fo4::parse_error);
}

TEST(NifWriteBlocks, JsonRefusesNumbersTheStoredFloatWouldChange) {
  // Invariant: a value typed as 254.99999999 must be refused, not silently stored as 255.
  EXPECT_THROW(fo4::nif::read_edits_json(R"([{"id":1,"key":"smoothness","value":254.99999999}])"),
               fo4::parse_error);
  EXPECT_THROW(fo4::nif::read_edits_json(R"([{"id":1,"key":"uvScale","value":[0.5,16777217.0]}])"),
               fo4::parse_error);
  EXPECT_NO_THROW(fo4::nif::read_edits_json(R"([{"id":1,"key":"smoothness","value":0.1}])"));
}

TEST(NifWriteBlocks, JsonAcceptsTheWidenedDoublesDescribeEmits) {
  // Invariant: describe emits the shortest double for a stored float, so that value must survive the JSON reader.
  EXPECT_NO_THROW(fo4::nif::read_edits_json(R"([{"id":1,"key":"smoothness","value":0.10000000149011612}])"));
  EXPECT_NO_THROW(
      fo4::nif::read_edits_json(R"([{"id":1,"key":"uvOffset","value":[0.10000000149011612,0.5,1.0]}])"));
}

TEST(NifWriteBlocks, JsonRefusesABooleanForANumericKey) {
  // Invariant: only the five on/off keys take a boolean, so scale cannot be set with true.
  EXPECT_THROW(fo4::nif::read_edits_json(R"([{"id":1,"key":"scale","value":true}])"), fo4::parse_error);
  EXPECT_THROW(fo4::nif::read_edits_json(R"([{"id":1,"key":"smoothness","value":false}])"), fo4::parse_error);
  EXPECT_NO_THROW(fo4::nif::read_edits_json(R"([{"id":1,"key":"alphaBlend","value":true}])"));
  EXPECT_NO_THROW(fo4::nif::read_edits_json(R"([{"id":1,"key":"editorMarker","value":false}])"));
}

// Regression: a JSON number for a boolean key was stored as 1.0 or 0.0 and accepted, so editorMarker took 1 silently.
TEST(NifWriteBlocks, JsonRefusesANumberForABooleanKeyByName) {
  expect_parse_error([] { fo4::nif::read_edits_json(R"([{"id":1,"key":"editorMarker","value":1}])"); },
                     "key 'editorMarker' takes true or false, not a number");
  expect_parse_error([] { fo4::nif::read_edits_json(R"([{"id":1,"key":"alphaBlend","value":0}])"); },
                     "key 'alphaBlend' takes true or false, not a number");
}

TEST(NifWriteBlocks, JsonRefusesANumberPastTheFloatRange) {
  // Invariant: a double past FLT_MAX is refused before the narrowing cast, which is undefined for it.
  expect_parse_error([] { fo4::nif::read_edits_json(R"([{"id":1,"key":"smoothness","value":1e300}])"); },
                     "is outside the range of a 32-bit float");
  expect_parse_error([] { fo4::nif::read_edits_json(R"([{"id":1,"key":"uvScale","value":[-1e300,0.5]}])"); },
                     "is outside the range of a 32-bit float");
  EXPECT_NO_THROW(fo4::nif::read_edits_json(R"([{"id":1,"key":"smoothness","value":3.4e38}])"));
}
