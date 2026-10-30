#include "fo4/c_api_nif.h"

#include "nif_build.hpp"
#include "nif_fixture.hpp"

#include <gtest/gtest.h>

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

namespace {

using nif_fixture::Writer;

class Nif {
public:
  explicit Nif(const std::vector<std::byte>& bytes) {
    const auto status = radicle_nif_read(reinterpret_cast<const uint8_t*>(bytes.data()), bytes.size(), &handle_);
    EXPECT_EQ(status, RADICLE_STATUS_OK) << radicle_last_error();
  }
  ~Nif() { radicle_nif_destroy(handle_); }
  Nif(const Nif&) = delete;
  Nif& operator=(const Nif&) = delete;

  radicle_nif* get() const { return handle_; }

private:
  radicle_nif* handle_{nullptr};
};

std::vector<float> take_floats(float* values, size_t count) {
  std::vector<float> result(values, values + count);
  radicle_free_floats(values);
  return result;
}

std::vector<float> floats_of(radicle_nif* nif, size_t index, radicle_status (*getter)(const radicle_nif*, size_t,
                                                                                      float**, size_t*)) {
  float* values = nullptr;
  size_t count = 0;
  const auto status = getter(nif, index, &values, &count);
  EXPECT_EQ(status, RADICLE_STATUS_OK) << radicle_last_error();
  if (count == 0) {
    EXPECT_EQ(values, nullptr);
    return {};
  }
  return take_floats(values, count);
}

// Requires a block index rather than a shape index, which text_of cannot pass through.
std::string shader_text(const radicle_nif* nif,
                        uint32_t block,
                        radicle_status (*getter)(const radicle_nif*, uint32_t, uint8_t**, size_t*)) {
  uint8_t* bytes = nullptr;
  size_t size = 0;
  EXPECT_EQ(getter(nif, block, &bytes, &size), RADICLE_STATUS_OK) << radicle_last_error();
  std::string result(reinterpret_cast<const char*>(bytes), size);
  radicle_free_bytes(bytes);
  return result;
}

std::string text_of(radicle_nif* nif, size_t index, radicle_status (*getter)(const radicle_nif*, size_t, uint8_t**,
                                                                            size_t*)) {
  uint8_t* bytes = nullptr;
  size_t size = 0;
  EXPECT_EQ(getter(nif, index, &bytes, &size), RADICLE_STATUS_OK) << radicle_last_error();
  std::string result(reinterpret_cast<const char*>(bytes), size);
  radicle_free_bytes(bytes);
  return result;
}

void expect_floats(const std::vector<float>& actual, const std::vector<float>& expected) {
  ASSERT_EQ(actual.size(), expected.size());
  for (std::size_t i = 0; i < expected.size(); ++i) EXPECT_NEAR(actual[i], expected[i], 1e-5) << "index " << i;
}

const float k_signed_0x11 = 0x11 / 255.0f * 2.0f - 1.0f;
const float k_signed_0x22 = 0x22 / 255.0f * 2.0f - 1.0f;

}

TEST(CapiNif, ReadsEveryShapeField) {
  const auto bytes = nif_fixture::full_nif();
  Nif nif(bytes);
  ASSERT_NE(nif.get(), nullptr);

  size_t shapes = 0;
  ASSERT_EQ(radicle_nif_shape_count(nif.get(), &shapes), RADICLE_STATUS_OK);
  ASSERT_EQ(shapes, 1U);

  size_t vertices = 0;
  ASSERT_EQ(radicle_nif_shape_vertex_count(nif.get(), 0, &vertices), RADICLE_STATUS_OK);
  ASSERT_EQ(vertices, 2U);

  EXPECT_EQ(text_of(nif.get(), 0, radicle_nif_shape_name), "shape");

  int32_t skin = 0;
  int32_t shader = 0;
  int32_t alpha = 0;
  ASSERT_EQ(radicle_nif_shape_refs(nif.get(), 0, &skin, &shader, &alpha), RADICLE_STATUS_OK);
  EXPECT_EQ(skin, 2);
  EXPECT_EQ(shader, 4);
  EXPECT_EQ(alpha, -1);

  expect_floats(floats_of(nif.get(), 0, radicle_nif_shape_positions), {1.0f, 2.0f, 3.0f, 4.0f, 5.0f, 6.0f});
  expect_floats(floats_of(nif.get(), 0, radicle_nif_shape_uvs), {0.5f, 0.25f, 0.5f, 0.25f});
  expect_floats(floats_of(nif.get(), 0, radicle_nif_shape_normals), {1.0f, -1.0f, 128.0f / 255.0f * 2.0f - 1.0f,
                                                                    1.0f, -1.0f, 128.0f / 255.0f * 2.0f - 1.0f});
  expect_floats(floats_of(nif.get(), 0, radicle_nif_shape_tangents), {-1.0f, 1.0f, 128.0f / 255.0f * 2.0f - 1.0f,
                                                                     -1.0f, 1.0f, 128.0f / 255.0f * 2.0f - 1.0f});
  expect_floats(floats_of(nif.get(), 0, radicle_nif_shape_bitangents),
                {0.5f, k_signed_0x11, k_signed_0x22, 0.5f, k_signed_0x11, k_signed_0x22});
  expect_floats(floats_of(nif.get(), 0, radicle_nif_shape_colors), {1.0f, 0.0f, 0.2f, 0.4f, 1.0f, 0.0f, 0.2f, 0.4f});

  uint16_t* triangles = nullptr;
  size_t triangle_count = 0;
  ASSERT_EQ(radicle_nif_shape_triangles(nif.get(), 0, &triangles, &triangle_count), RADICLE_STATUS_OK);
  ASSERT_EQ(triangle_count, 3U);
  EXPECT_EQ(triangles[0], 0);
  EXPECT_EQ(triangles[1], 1);
  EXPECT_EQ(triangles[2], 1);
  radicle_free_triangles(triangles);
}

TEST(CapiNif, ReadsShaderMaterialAndTextureSlots) {
  const auto bytes = nif_fixture::full_nif();
  Nif nif(bytes);

  EXPECT_EQ(text_of(nif.get(), 0, radicle_nif_shape_material), "materials\\body.bgsm");

  size_t slots = 0;
  ASSERT_EQ(radicle_nif_shape_texture_count(nif.get(), 0, &slots), RADICLE_STATUS_OK);
  ASSERT_EQ(slots, 2U);

  auto texture = [&](size_t slot) {
    uint8_t* text = nullptr;
    size_t size = 0;
    EXPECT_EQ(radicle_nif_shape_texture(nif.get(), 0, slot, &text, &size), RADICLE_STATUS_OK);
    std::string result(reinterpret_cast<const char*>(text), size);
    radicle_free_bytes(text);
    return result;
  };
  EXPECT_EQ(texture(0), "textures\\body_d.dds");
  EXPECT_EQ(texture(1), "textures\\body_n.dds");

  EXPECT_EQ(radicle_nif_shape_texture(nif.get(), 0, 2, nullptr, nullptr), RADICLE_STATUS_INVALID_ARGUMENT);
}

TEST(CapiNif, ReadsSkinWeightsBonesAndBindTransforms) {
  const auto bytes = nif_fixture::full_nif();
  Nif nif(bytes);

  int32_t skinned = 0;
  ASSERT_EQ(radicle_nif_shape_skinned(nif.get(), 0, &skinned), RADICLE_STATUS_OK);
  EXPECT_EQ(skinned, 1);

  expect_floats(floats_of(nif.get(), 0, radicle_nif_shape_weights),
                {1.0f, 0.5f, 0.25f, 0.0f, 1.0f, 0.5f, 0.25f, 0.0f});

  uint8_t* indices = nullptr;
  size_t index_count = 0;
  ASSERT_EQ(radicle_nif_shape_bone_indices(nif.get(), 0, &indices, &index_count), RADICLE_STATUS_OK);
  ASSERT_EQ(index_count, 8U);
  const std::vector<uint8_t> expected_indices{0, 0, 0, 0, 0, 0, 0, 0};
  for (std::size_t i = 0; i < expected_indices.size(); ++i) EXPECT_EQ(indices[i], expected_indices[i]);
  radicle_free_bytes(indices);

  size_t bones = 0;
  ASSERT_EQ(radicle_nif_shape_bone_count(nif.get(), 0, &bones), RADICLE_STATUS_OK);
  ASSERT_EQ(bones, 1U);

  uint8_t* name = nullptr;
  size_t name_size = 0;
  ASSERT_EQ(radicle_nif_shape_bone_name(nif.get(), 0, 0, &name, &name_size), RADICLE_STATUS_OK);
  EXPECT_EQ(std::string(reinterpret_cast<const char*>(name), name_size), "RootBone");
  radicle_free_bytes(name);

  float center[3]{};
  float radius = 0;
  float rotation[9]{};
  float translation[3]{};
  float scale = 0;
  ASSERT_EQ(radicle_nif_shape_bone_transform(nif.get(), 0, 0, center, &radius, rotation, translation, &scale),
            RADICLE_STATUS_OK);
  const std::vector<float> center_values{center[0], center[1], center[2]};
  const std::vector<float> rotation_values(rotation, rotation + 9);
  const std::vector<float> translation_values{translation[0], translation[1], translation[2]};
  expect_floats(center_values, {1.0f, 2.0f, 3.0f});
  EXPECT_FLOAT_EQ(radius, 4.0f);
  expect_floats(rotation_values, {1.0f, 2.0f, 3.0f, 4.0f, 5.0f, 6.0f, 7.0f, 8.0f, 9.0f});
  expect_floats(translation_values, {5.0f, 6.0f, 7.0f});
  EXPECT_FLOAT_EQ(scale, 2.0f);
}

TEST(CapiNif, ComposesTheShapeToGlobalTransform) {
  const auto bytes = nif_fixture::full_nif();
  Nif nif(bytes);
  float translation[3]{};
  float rotation[9]{};
  float scale = 0;
  ASSERT_EQ(radicle_nif_shape_transform(nif.get(), 0, translation, rotation, &scale), RADICLE_STATUS_OK);
  expect_floats({translation[0], translation[1], translation[2]}, {7.0f, 8.0f, 9.0f});
  expect_floats(std::vector<float>(rotation, rotation + 9),
                {1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f});
  EXPECT_FLOAT_EQ(scale, 1.0f);
}

TEST(CapiNif, ReportsEmptyArraysAsNullAndZero) {
  const std::vector<std::string> types{"BSTriShape", "BSSubIndexTriShape", "BSMeshLODTriShape", "BSDynamicTriShape"};
  std::vector<nif_fixture::Block> blocks;
  for (const auto& type : types) {
    blocks.push_back(nif_fixture::shape_block(type, 0, fo4::nif::Transform{}, nif_fixture::k_full, 16, -1, -1, -1,
                                              std::vector<std::byte>(16 * 3, std::byte{0}),
                                              {0, 1, 2}));
  }
  const auto bytes = nif_fixture::build_nif({"shape"}, blocks, {});
  Nif nif(bytes);

  size_t shapes = 0;
  ASSERT_EQ(radicle_nif_shape_count(nif.get(), &shapes), RADICLE_STATUS_OK);
  ASSERT_EQ(shapes, types.size());

  float* values = reinterpret_cast<float*>(0x1);
  size_t count = 99;
  ASSERT_EQ(radicle_nif_shape_normals(nif.get(), 0, &values, &count), RADICLE_STATUS_OK);
  EXPECT_EQ(values, nullptr);
  EXPECT_EQ(count, 0U);

  uint8_t* text = reinterpret_cast<uint8_t*>(0x1);
  size_t size = 99;
  ASSERT_EQ(radicle_nif_shape_material(nif.get(), 0, &text, &size), RADICLE_STATUS_OK);
  EXPECT_EQ(text, nullptr);
  EXPECT_EQ(size, 0U);

  int32_t skinned = 1;
  ASSERT_EQ(radicle_nif_shape_skinned(nif.get(), 0, &skinned), RADICLE_STATUS_OK);
  EXPECT_EQ(skinned, 0);

  size_t bones = 99;
  ASSERT_EQ(radicle_nif_shape_bone_count(nif.get(), 0, &bones), RADICLE_STATUS_OK);
  EXPECT_EQ(bones, 0U);
}

TEST(CapiNif, RejectsBadInput) {
  EXPECT_EQ(radicle_nif_read(nullptr, 0, nullptr), RADICLE_STATUS_INVALID_ARGUMENT);

  radicle_nif* handle = nullptr;
  const std::vector<std::byte> junk(64, std::byte{0});
  EXPECT_EQ(radicle_nif_read(reinterpret_cast<const uint8_t*>(junk.data()), junk.size(), &handle),
            RADICLE_STATUS_PARSE_ERROR);
  EXPECT_EQ(handle, nullptr);

  const auto bytes = nif_fixture::full_nif();
  Nif nif(bytes);
  size_t count = 0;
  EXPECT_EQ(radicle_nif_shape_count(nif.get(), nullptr), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_nif_shape_vertex_count(nif.get(), 4, &count), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_nif_shape_name(nif.get(), 4, nullptr, nullptr), RADICLE_STATUS_INVALID_ARGUMENT);
  float translation[3]{};
  float rotation[9]{};
  float scale = 0;
  EXPECT_EQ(radicle_nif_shape_transform(nullptr, 0, translation, rotation, &scale), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_nif_shape_transform(nif.get(), 0, nullptr, rotation, &scale), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_nif_shape_bone_transform(nif.get(), 0, 5, nullptr, nullptr, nullptr, nullptr, nullptr),
            RADICLE_STATUS_INVALID_ARGUMENT);
}

TEST(CapiNif, ResetsTheHandleOnInvalidArgument) {
  int sentinel = 0;
  auto* handle = reinterpret_cast<radicle_nif*>(&sentinel);
  EXPECT_EQ(radicle_nif_read(nullptr, 0, &handle), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(handle, nullptr);
}

namespace {

std::vector<nif_fixture::Block> bad_shader_blocks() {
  const std::vector<std::byte> vertices(16, std::byte{0});
  return {nif_fixture::node_block("NiNode", 0, fo4::nif::Transform{}, {1}),
          nif_fixture::shape_block("BSTriShape", 0, fo4::nif::Transform{}, nif_fixture::k_full, 16, -1, 2, -1,
                                   vertices, {0, 0, 0}),
          nif_fixture::lighting_shader_block(1, -1, 99, 0, 0)};
}

}

TEST(CapiNif, ExposesTheNodeTreeAndTheShapesParentNode) {
  const auto bytes = nif_fixture::full_nif();
  Nif nif(bytes);
  ASSERT_NE(nif.get(), nullptr);

  size_t nodes = 0;
  ASSERT_EQ(radicle_nif_node_count(nif.get(), &nodes), RADICLE_STATUS_OK);
  ASSERT_EQ(nodes, 2U);

  uint32_t block = 99;
  ASSERT_EQ(radicle_nif_node_block(nif.get(), 0, &block), RADICLE_STATUS_OK);
  EXPECT_EQ(block, 0U);
  EXPECT_EQ(text_of(nif.get(), 0, radicle_nif_node_type), "NiNode");
  EXPECT_EQ(text_of(nif.get(), 0, radicle_nif_node_name), "shape");
  EXPECT_EQ(text_of(nif.get(), 1, radicle_nif_node_name), "RootBone");

  int32_t parent = 99;
  ASSERT_EQ(radicle_nif_node_parent(nif.get(), 0, &parent), RADICLE_STATUS_OK);
  EXPECT_EQ(parent, -1);
  ASSERT_EQ(radicle_nif_node_parent(nif.get(), 1, &parent), RADICLE_STATUS_OK);
  EXPECT_EQ(parent, -1);

  uint32_t* refs = nullptr;
  size_t ref_count = 0;
  ASSERT_EQ(radicle_nif_node_children(nif.get(), 0, &refs, &ref_count), RADICLE_STATUS_OK);
  ASSERT_EQ(ref_count, 1U);
  EXPECT_EQ(refs[0], 1U);
  radicle_free_node_refs(refs);

  ASSERT_EQ(radicle_nif_node_children(nif.get(), 1, &refs, &ref_count), RADICLE_STATUS_OK);
  EXPECT_EQ(refs, nullptr);
  EXPECT_EQ(ref_count, 0U);

  float translation[3]{};
  float rotation[9]{};
  float scale = 0;
  ASSERT_EQ(radicle_nif_node_transform(nif.get(), 0, translation, rotation, &scale), RADICLE_STATUS_OK);
  expect_floats({translation[0], translation[1], translation[2]}, {10.0f, 0.0f, 0.0f});
  expect_floats(std::vector<float>(rotation, rotation + 9),
                {1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f});
  EXPECT_FLOAT_EQ(scale, 2.0f);

  int32_t node = 99;
  ASSERT_EQ(radicle_nif_node_for_block(nif.get(), 0, &node), RADICLE_STATUS_OK);
  EXPECT_EQ(node, 0);
  ASSERT_EQ(radicle_nif_node_for_block(nif.get(), 1, &node), RADICLE_STATUS_OK);
  EXPECT_EQ(node, -1);
  ASSERT_EQ(radicle_nif_node_for_block(nif.get(), 6, &node), RADICLE_STATUS_OK);
  EXPECT_EQ(node, 1);
  ASSERT_EQ(radicle_nif_node_for_block(nif.get(), 9999, &node), RADICLE_STATUS_OK);
  EXPECT_EQ(node, -1);

  ASSERT_EQ(radicle_nif_shape_parent_node(nif.get(), 0, &node), RADICLE_STATUS_OK);
  EXPECT_EQ(node, 0);

  EXPECT_EQ(radicle_nif_node_count(nullptr, &nodes), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_nif_node_block(nif.get(), 5, &block), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_nif_node_name(nif.get(), 5, nullptr, nullptr), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_nif_node_parent(nif.get(), 5, &parent), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_nif_node_children(nif.get(), 5, &refs, &ref_count), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_nif_node_transform(nif.get(), 5, translation, rotation, &scale),
            RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_nif_shape_parent_node(nif.get(), 5, &node), RADICLE_STATUS_INVALID_ARGUMENT);
}

TEST(CapiNif, ReportsABadShaderBlockAsAShapeWarning) {
  const auto bytes = nif_fixture::build_nif({"shape", "Root"}, bad_shader_blocks(), {0});
  Nif nif(bytes);
  ASSERT_NE(nif.get(), nullptr);

  EXPECT_EQ(text_of(nif.get(), 0, radicle_nif_shape_material), "");

  size_t slots = 99;
  ASSERT_EQ(radicle_nif_shape_texture_count(nif.get(), 0, &slots), RADICLE_STATUS_OK);
  EXPECT_EQ(slots, 0U);

  const auto warning = text_of(nif.get(), 0, radicle_nif_shape_warning);
  EXPECT_NE(warning.find("texture set reference is out of range"), std::string::npos) << warning;

  size_t vertices = 0;
  ASSERT_EQ(radicle_nif_shape_vertex_count(nif.get(), 0, &vertices), RADICLE_STATUS_OK);
  EXPECT_EQ(vertices, 1U);
  uint16_t* triangles = nullptr;
  size_t triangle_count = 0;
  ASSERT_EQ(radicle_nif_shape_triangles(nif.get(), 0, &triangles, &triangle_count), RADICLE_STATUS_OK);
  EXPECT_EQ(triangle_count, 3U);
  radicle_free_triangles(triangles);

  int32_t node = 99;
  ASSERT_EQ(radicle_nif_shape_parent_node(nif.get(), 0, &node), RADICLE_STATUS_OK);
  EXPECT_EQ(node, 0);
}

TEST(CapiNif, ReportsNoWarningForAHealthyShape) {
  const auto bytes = nif_fixture::full_nif();
  Nif nif(bytes);
  EXPECT_EQ(text_of(nif.get(), 0, radicle_nif_shape_warning), "");
}

// Invariant: every shader field the editor shows must arrive through these structs, one member per field.
TEST(CapiNif, ExposesTheShaderAndAlphaFieldsItReads) {
  nif_build::LightingFields fields;
  fields.shader_type = 11;
  const std::vector<nif_build::Block> blocks{
      nif_build::full_lighting_shader_block(fields),
      nif_build::effect_shader_block(0, {"fx_source.dds", "fx_grey.dds", "fx_env.dds", "fx_normal.dds", "fx_mask.dds"}),
      nif_build::alpha_block(0x12EDU, 128),
  };
  const auto bytes = nif_build::build_nif({"fx"}, blocks, {0});
  Nif nif(bytes);
  ASSERT_NE(nif.get(), nullptr);

  radicle_nif_lighting_shader lighting{};
  ASSERT_EQ(radicle_nif_lighting_shader(nif.get(), 0, &lighting), RADICLE_STATUS_OK) << radicle_last_error();
  EXPECT_EQ(lighting.shader_type, 11U);
  EXPECT_EQ(lighting.flags1, 0x80400201U);
  EXPECT_EQ(lighting.flags2, 0x21U);
  EXPECT_FLOAT_EQ(lighting.alpha, 0.5f);
  EXPECT_FLOAT_EQ(lighting.smoothness, 0.75f);
  EXPECT_EQ(lighting.has_backlight, 1);
  EXPECT_FLOAT_EQ(lighting.backlight_power, 2.5f);
  EXPECT_EQ(lighting.has_wetness, 1);
  EXPECT_FLOAT_EQ(lighting.wetness[5], 6.0f);
  EXPECT_EQ(lighting.has_multi_layer_parallax, 1);
  EXPECT_FLOAT_EQ(lighting.multi_layer_parallax[4], 0.5f);
  EXPECT_EQ(lighting.tail_bytes, 20U);
  EXPECT_EQ(shader_text(nif.get(), 0, radicle_nif_lighting_shader_name), "");
  EXPECT_EQ(shader_text(nif.get(), 0, radicle_nif_lighting_shader_root_material), "");

  radicle_nif_effect_shader effect{};
  ASSERT_EQ(radicle_nif_effect_shader(nif.get(), 1, &effect), RADICLE_STATUS_OK) << radicle_last_error();
  EXPECT_EQ(effect.texture_count, 5U);
  EXPECT_EQ(effect.texture_clamp, 3);
  EXPECT_FLOAT_EQ(effect.base_color[0], 0.4f);
  EXPECT_FLOAT_EQ(effect.env_map_scale, 1.75f);
  EXPECT_EQ(shader_text(nif.get(), 1, radicle_nif_effect_shader_name), "fx");

  uint8_t* texture = nullptr;
  size_t texture_size = 0;
  ASSERT_EQ(radicle_nif_effect_shader_texture(nif.get(), 1, 2, &texture, &texture_size), RADICLE_STATUS_OK);
  EXPECT_EQ(std::string(reinterpret_cast<const char*>(texture), texture_size), "fx_env.dds");
  radicle_free_bytes(texture);
  EXPECT_EQ(radicle_nif_effect_shader_texture(nif.get(), 1, 5, &texture, &texture_size),
            RADICLE_STATUS_INVALID_ARGUMENT);

  radicle_nif_alpha_property alpha{};
  ASSERT_EQ(radicle_nif_alpha_property(nif.get(), 2, &alpha), RADICLE_STATUS_OK) << radicle_last_error();
  EXPECT_EQ(alpha.flags, 0x12EDU);
  EXPECT_EQ(alpha.threshold, 128);
  EXPECT_EQ(alpha.blend_enabled, 1);
  EXPECT_EQ(alpha.source_blend, 6);
  EXPECT_EQ(alpha.destination_blend, 7);
  EXPECT_EQ(alpha.test_enabled, 1);
  EXPECT_EQ(alpha.test_function, 4);

  EXPECT_EQ(radicle_nif_lighting_shader(nif.get(), 2, &lighting), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_nif_effect_shader(nif.get(), 0, &effect), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_nif_alpha_property(nif.get(), 0, &alpha), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_nif_lighting_shader(nif.get(), 9, &lighting), RADICLE_STATUS_INVALID_ARGUMENT);
}

// Invariant: the C ABI flag table holds the same 64 names in the same bit order as the reader.
TEST(CapiNif, ExposesTheShaderFlagNameTable) {
  EXPECT_EQ(radicle_nif_shader_flag_count(), 64U);
  uint8_t* name = nullptr;
  size_t size = 0;
  ASSERT_EQ(radicle_nif_shader_flag_name(0, &name, &size), RADICLE_STATUS_OK);
  EXPECT_EQ(std::string(reinterpret_cast<const char*>(name), size), "Specular");
  radicle_free_bytes(name);
  ASSERT_EQ(radicle_nif_shader_flag_name(63, &name, &size), RADICLE_STATUS_OK);
  EXPECT_EQ(std::string(reinterpret_cast<const char*>(name), size), "Refraction_Writes_Depth");
  radicle_free_bytes(name);
  EXPECT_EQ(radicle_nif_shader_flag_name(64, &name, &size), RADICLE_STATUS_NOT_FOUND);
  EXPECT_EQ(radicle_nif_shader_flag_name(0, nullptr, nullptr), RADICLE_STATUS_INVALID_ARGUMENT);
}
