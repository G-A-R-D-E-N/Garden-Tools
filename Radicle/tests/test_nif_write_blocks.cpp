#include "fo4/error.hpp"
#include "fo4/nif/collision.hpp"
#include "fo4/nif/nif.hpp"
#include "fo4/nif/nif_write.hpp"
#include "fo4/nif/shader.hpp"
#include "nif_build.hpp"

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
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

std::vector<std::byte> lighting_nif(const nif_build::LightingFields& fields) {
  return nif_build::build_nif({}, {nif_build::node_block("NiNode", {}, {1}), shape_on_shader(2, -1),
                                   nif_build::full_lighting_shader_block(fields)},
                              {k_root});
}

}  // namespace

TEST(NifWriteBlocks, LightingFlagsAreSetFromReadSideNames) {
  // Invariant: a flag rename on either side would set a different bit than the reader reports
  const auto nif = read_nif(lighting_nif(nif_build::LightingFields{}));
  const std::vector<Edit> edits{
      Edit{k_shape, "shaderFlags", names({"Specular", "TwoSided", "Refraction_Writes_Depth"})}};
  const auto out = read_nif(apply_edits(nif, edits));
  const auto flags = fo4::nif::read_lighting_shader(out, 2).flags;
  EXPECT_EQ(flags.word1, 0x1U);
  EXPECT_EQ(flags.word2, 0x80000010U);
  EXPECT_EQ(fo4::nif::shader_flag_names(flags),
            (std::vector<std::string>{"Specular", "TwoSided", "Refraction_Writes_Depth"}));
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
      {}, {nif_build::node_block("NiNode", {}, {1}), shape_on_shader(2, -1),
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
  EXPECT_EQ(block_bytes(out_bytes, out, 2).size(), block_bytes(bytes, nif, 2).size() + 21U);
  expect_rejected(nif, Edit{k_shape, "flags", as_number(1.0f)});
}

TEST(NifWriteBlocks, AlphaPropertyDecodesAfterItsEdits) {
  // Invariant: flags bits must be toggled in place, so blend, test and their neighbours stay independent
  const auto bytes = nif_build::build_nif(
      {}, {nif_build::node_block("NiNode", {}, {1}), shape_on_shader(-1, 2), nif_build::alpha_block(0x12EDU, 128)},
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
  const auto nif = read_nif(nif_build::build_nif({}, {nif_build::node_block("NiNode", {}, {}), bsx_block(0x480U)},
                                                 {k_root}));
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
      {}, {nif_build::node_block("NiNode", {}, {}), collision_object_block(2, 0x0800U, 7U), physics_system_block()},
      {k_root});
  const auto nif = read_nif(bytes);
  const std::vector<Edit> edits{
      Edit{1, "flags", as_number(2052.0f)},
      Edit{1, "bodyId", as_number(4294967295.0f)},
  };
  const auto out_bytes = apply_edits(nif, edits);
  const auto out = read_nif(out_bytes);
  const auto object = fo4::nif::read_collision_object(out, 1);
  EXPECT_EQ(object.flags, 0x0804U);
  EXPECT_EQ(object.body_id, 4294967295U);
  EXPECT_EQ(object.system, 2);
  EXPECT_EQ(object.target, -1);
  EXPECT_EQ(block_bytes(out_bytes, out, 2), block_bytes(bytes, nif, 2));
  const auto blobs = fo4::nif::read_collision_blobs(out);
  ASSERT_EQ(blobs.size(), 1U);
  EXPECT_EQ(blobs[0].flags, 0x0804U);
  EXPECT_EQ(blobs[0].body_id, 4294967295U);
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
  expect_rejected(nif, Edit{1, "system", as_number(1.0f)});
}

TEST(NifWriteBlocks, JsonEditsCarryFlagNamesAndRejectUnusableValues) {
  // Invariant: the editor's JSON path must reach the same flag-name edit the C++ API accepts
  const auto nif = read_nif(lighting_nif(nif_build::LightingFields{}));
  const auto out =
      fo4::nif::apply_edits_json(nif, R"([{"id":1,"key":"shaderFlags","value":["Specular","TwoSided"]}])");
  const auto flags = fo4::nif::read_lighting_shader(read_nif(out), 2).flags;
  EXPECT_EQ(flags.word1, 0x1U);
  EXPECT_EQ(flags.word2, 0x10U);
  EXPECT_THROW(fo4::nif::read_edits_json("[{\"id\":1,\"key\":\"shaderFlags\",\"value\":[{\"a\":1}]}]"),
               fo4::parse_error);
  EXPECT_THROW(fo4::nif::read_edits_json("{\"id\":1}"), fo4::parse_error);
  EXPECT_THROW(fo4::nif::read_edits_json("not json"), fo4::parse_error);
  EXPECT_THROW(fo4::nif::read_edits_json("[{\"id\":-1,\"key\":\"name\",\"value\":\"x\"}]"), fo4::parse_error);
}
