#include "fo4/error.hpp"
#include "fo4/nif/collision.hpp"
#include "fo4/nif/describe.hpp"
#include "fo4/nif/nif.hpp"
#include "fo4/nif/shader.hpp"
#include "nif_build.hpp"

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

using fo4::nif::Transform;
using fo4::nif::TreeBlock;
using fo4::nif::TreeField;
using namespace nif_build;

namespace {

const TreeField* find_field(const TreeBlock& block, std::string_view key) {
  for (const auto& field : block.fields) {
    if (field.key == key) return &field;
  }
  return nullptr;
}

std::string text_of(const TreeBlock& block, std::string_view key) {
  const auto* field = find_field(block, key);
  if (!field) return "<missing>";
  if (const auto* text = std::get_if<std::string>(&field->value)) return *text;
  return "<not text>";
}

float number_of(const TreeBlock& block, std::string_view key) {
  const auto* field = find_field(block, key);
  if (!field) return -9999.0f;
  if (const auto* number = std::get_if<float>(&field->value)) return *number;
  return -9998.0f;
}

std::vector<float> numbers_of(const TreeBlock& block, std::string_view key) {
  const auto* field = find_field(block, key);
  if (!field) return {};
  if (const auto* numbers = std::get_if<std::vector<float>>(&field->value)) return *numbers;
  return {};
}

bool bool_of(const TreeBlock& block, std::string_view key) {
  const auto* field = find_field(block, key);
  if (!field) return false;
  if (const auto* flag = std::get_if<bool>(&field->value)) return *flag;
  return false;
}

std::vector<fo4::nif::TreeFlag> shader_flags(const TreeBlock& block) {
  const auto* field = find_field(block, "shaderFlags");
  if (!field) return {};
  const auto* flags = std::get_if<std::vector<fo4::nif::TreeFlag>>(&field->value);
  return flags ? *flags : std::vector<fo4::nif::TreeFlag>{};
}

bool flag_on(const TreeBlock& block, std::string_view key) {
  for (const auto& flag : shader_flags(block)) {
    if (flag.key == key) return flag.on;
  }
  ADD_FAILURE() << "no shader flag named " << key;
  return false;
}

Block bsx_block(std::int32_t name, std::uint32_t flags) {
  Writer w;
  w.i32(name);
  w.u32(flags);
  return Block{"BSXFlags", w.bytes()};
}

Block collision_object_block(std::int32_t target, std::uint16_t flags, std::int32_t system,
                             std::uint32_t body_id) {
  Writer w;
  w.i32(target);
  w.u16(flags);
  w.i32(system);
  w.u32(body_id);
  return Block{"bhkNPCollisionObject", w.bytes()};
}

Block physics_system_block(std::size_t payload) {
  Writer w;
  w.u32(static_cast<std::uint32_t>(payload));
  for (std::size_t i = 0; i < payload; ++i) w.u8(0x5AU);
  return Block{"bhkPhysicsSystem", w.bytes()};
}

Block texture_set(std::vector<std::string> paths) {
  Writer w;
  w.u32(static_cast<std::uint32_t>(paths.size()));
  for (const auto& path : paths) w.sized(path);
  return Block{"BSShaderTextureSet", w.bytes()};
}

Block shape_with_descriptor(std::uint64_t descriptor, std::uint32_t triangles, std::uint16_t vertices,
                            const std::vector<std::byte>& data) {
  Writer w;
  w.i32(0);
  w.u32(0);
  w.i32(-1);
  w.u32(0);
  w.transform(Transform{});
  w.i32(-1);
  for (int i = 0; i < 4; ++i) w.f32(0.0f);
  w.i32(-1);
  w.i32(-1);
  w.i32(-1);
  w.u64(descriptor);
  w.u32(triangles);
  w.u16(vertices);
  w.u32(static_cast<std::uint32_t>(data.size() + triangles * 6));
  w.append(data);
  for (std::uint32_t i = 0; i < triangles * 3; ++i) w.u16(static_cast<std::uint16_t>(i));
  return Block{"BSTriShape", w.bytes()};
}

Block opaque_block(std::string type, std::size_t bytes) {
  Writer w;
  for (std::size_t i = 0; i < bytes; ++i) w.u8(static_cast<std::uint8_t>(i));
  return Block{std::move(type), w.bytes()};
}

std::vector<std::byte> full_file(std::uint32_t bsx_flags) {
  const Transform shape_xf{{7.0f, 8.0f, 9.0f}, {1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f}, 1.0f};
  LightingFields lighting;
  lighting.name = 1;
  lighting.texture_set = 4;
  std::vector<Block> blocks;
  blocks.push_back(named_node_block("NiNode", 2, Transform{}, {1}));
  blocks.push_back(shape_with_refs("BSTriShape", 0, shape_xf, k_full, 16, -1, 2, 3, triangle_vertices(), {0, 1, 2}));
  blocks.push_back(full_lighting_shader_block(lighting));
  blocks.push_back(alpha_block(0x201U, 128));
  blocks.push_back(texture_set({"Textures\\a_d.dds", "Textures\\a_n.dds"}));
  blocks.push_back(bsx_block(3, bsx_flags));
  blocks.push_back(collision_object_block(1, 4, 7, 42U));
  blocks.push_back(physics_system_block(16));
  blocks.push_back(opaque_block("NiStringExtraData", 6));
  return build_nif({"Tri", "shaderName", "Root", "BSX"}, blocks, {0});
}

}  // namespace

TEST(NifTree, ListsEveryBlockInIndexOrderWithItsGroup) {
  const auto nif = fo4::nif::read_nif(full_file(0x21U));
  const auto tree = fo4::nif::describe_nif(nif);
  ASSERT_EQ(tree.blocks.size(), nif.blocks().size());
  EXPECT_TRUE(tree.fo4);
  const std::vector<std::string> groups{"Nodes", "Shapes", "Extra", "Extra", "Extra",
                                        "Extra", "Collision", "Collision", "Extra"};
  const std::vector<std::string> cats{"node", "shape", "shader", "alpha", "texture",
                                      "property", "collision", "collision", "block"};
  for (std::size_t i = 0; i < tree.blocks.size(); ++i) {
    EXPECT_EQ(tree.blocks[i].id, i);
    EXPECT_EQ(tree.blocks[i].type, nif.blocks()[i].type) << "block " << i;
    EXPECT_EQ(tree.blocks[i].group, groups[i]) << "block " << i;
    EXPECT_EQ(tree.blocks[i].cat, cats[i]) << "block " << i;
  }
}

TEST(NifTree, NodeBlockCarriesNameTranslationAndScale) {
  const auto nif = fo4::nif::read_nif(full_file(0x1U));
  const auto tree = fo4::nif::describe_nif(nif);
  const auto& node = tree.blocks.at(0);
  EXPECT_EQ(node.name, "Root");
  EXPECT_EQ(text_of(node, "name"), "Root");
  EXPECT_EQ(numbers_of(node, "translation"), (std::vector<float>{0.0f, 0.0f, 0.0f}));
  EXPECT_EQ(number_of(node, "scale"), 1.0f);
}

TEST(NifTree, ShapeBlockCarriesGeometryMaterialTexturesAndAlpha) {
  const auto nif = fo4::nif::read_nif(full_file(0x1U));
  const auto tree = fo4::nif::describe_nif(nif);
  const auto& shape = tree.blocks.at(1);
  EXPECT_EQ(text_of(shape, "name"), "Tri");
  EXPECT_EQ(numbers_of(shape, "translation"), (std::vector<float>{7.0f, 8.0f, 9.0f}));
  EXPECT_EQ(text_of(shape, "verts"), "3");
  EXPECT_EQ(text_of(shape, "tris"), "1");
  EXPECT_EQ(text_of(shape, "shaderType"), "BSLightingShaderProperty");
  EXPECT_EQ(text_of(shape, "material"), "shaderName");
  EXPECT_EQ(text_of(shape, "tex0"), "Textures\\a_d.dds");
  EXPECT_EQ(text_of(shape, "tex1"), "Textures\\a_n.dds");
  EXPECT_EQ(text_of(shape, "tex2"), "");
  EXPECT_EQ(text_of(shape, "skin"), "none");
  EXPECT_EQ(text_of(shape, "vertexSize"), "16 bytes");
  EXPECT_EQ(text_of(shape, "vertexAttributes"), "Position");
  EXPECT_EQ(text_of(shape, "fullPrecision"), "yes");
  EXPECT_TRUE(bool_of(shape, "alphaBlend"));
  EXPECT_TRUE(bool_of(shape, "alphaTest"));
  EXPECT_EQ(number_of(shape, "alphaThreshold"), 128.0f);
}

TEST(NifTree, ShapeWithoutAlphaPropertyHasNoAlphaFields) {
  const Transform xf{};
  const std::vector<Block> blocks{triangle_block("BSTriShape", xf)};
  const auto nif = fo4::nif::read_nif(build_nif({"shape"}, blocks, {}));
  const auto tree = fo4::nif::describe_nif(nif);
  ASSERT_EQ(tree.blocks.size(), 1U);
  EXPECT_EQ(find_field(tree.blocks[0], "alphaBlend"), nullptr);
  EXPECT_EQ(text_of(tree.blocks[0], "skin"), "none");
}

// Invariant: Skeleton Root Ptr is an i32 block reference, so a root id outside the string table is not a string index.
TEST(NifTree, SkinInstanceSkipsItsSkeletonRootPointerAndReportsTheBoneData) {
  const Transform xf{};
  const std::vector<Block> blocks{
      shape_with_refs("BSTriShape", 0, xf, k_has_skin, 20, 1, -1, -1, skinned_vertices(3), {0, 1, 2}),
      // Requires a root id past the end of the one-string table, the case a name read threw on.
      skin_instance_block(2, {-1, -1, -1, -1, -1}, 999),
      // Invariant: skinned_vertices writes bone indices 1 to 4, so four bones would be an invalid shape.
      bone_data_block(5),
  };
  const auto nif = fo4::nif::read_nif(build_nif({"shape"}, blocks, {}));
  const auto tree = fo4::nif::describe_nif(nif);
  ASSERT_EQ(tree.blocks.size(), 3U);
  const auto& instance = tree.blocks.at(1);
  EXPECT_EQ(instance.cat, "skin");
  EXPECT_EQ(find_field(instance, "name"), nullptr);
  EXPECT_EQ(find_field(instance, "readError"), nullptr);
  EXPECT_EQ(text_of(instance, "skinData"), "block 2");
  EXPECT_EQ(text_of(instance, "bones"), "5");
  EXPECT_EQ(text_of(tree.blocks.at(2), "transforms"), "5");
}

TEST(NifTree, ShapeWhoseAlphaReferenceIsPastTheEndKeepsItsNameAndFault) {
  // Invariant: a corrupt alpha reference must surface as the block's read error, not as std::out_of_range escaping describe_nif.
  const Transform xf{};
  const std::vector<Block> blocks{
      shape_with_refs("BSTriShape", 0, xf, k_full, 16, -1, -1, 9, triangle_vertices(), {0, 1, 2}),
  };
  const auto nif = fo4::nif::read_nif(build_nif({"shape"}, blocks, {}));
  const auto tree = fo4::nif::describe_nif(nif);
  ASSERT_EQ(tree.blocks.size(), 1U);
  EXPECT_EQ(text_of(tree.blocks[0], "name"), "shape");
  EXPECT_NE(text_of(tree.blocks[0], "readError").find("alpha property reference is out of range"), std::string::npos);
  EXPECT_EQ(find_field(tree.blocks[0], "alphaBlend"), nullptr);
}

TEST(NifTree, LightingShaderCarriesFocusFlagsAndTheEnvironmentMapTail) {
  const auto nif = fo4::nif::read_nif(full_file(0x1U));
  const auto tree = fo4::nif::describe_nif(nif);
  const auto& shader = tree.blocks.at(2);
  EXPECT_EQ(shader.name, "shaderName");
  EXPECT_EQ(text_of(shader, "materialClass"), "Envmap");
  EXPECT_EQ(numbers_of(shader, "uvOffset"), (std::vector<float>{0.25f, 0.5f}));
  EXPECT_EQ(numbers_of(shader, "uvScale"), (std::vector<float>{2.0f, 3.0f}));
  EXPECT_EQ(numbers_of(shader, "emissiveColor"), (std::vector<float>{0.5f, 0.25f, 0.125f}));
  EXPECT_EQ(number_of(shader, "emissiveMultiple"), 4.0f);
  EXPECT_EQ(number_of(shader, "backlightPower"), 2.5f);
  EXPECT_EQ(number_of(shader, "envMapScale"), 1.5f);
  EXPECT_TRUE(bool_of(shader, "envMapSSR"));
  EXPECT_FALSE(bool_of(shader, "envMapWetnessSSR"));
  EXPECT_TRUE(flag_on(shader, "Specular"));
  EXPECT_FALSE(flag_on(shader, "Skinned"));
  EXPECT_TRUE(flag_on(shader, "Cast_Shadows"));
  EXPECT_TRUE(flag_on(shader, "ZBuffer_Write"));
  EXPECT_TRUE(flag_on(shader, "Vertex_Colors"));
  EXPECT_EQ(shader_flags(shader).size(), fo4::nif::shader_flag_count());
}

TEST(NifTree, MultiLayerParallaxShaderCarriesItsFiveTailFloats) {
  LightingFields lighting;
  lighting.shader_type = 11;
  const std::vector<Block> blocks{full_lighting_shader_block(lighting)};
  const auto nif = fo4::nif::read_nif(build_nif({"shader"}, blocks, {}));
  const auto tree = fo4::nif::describe_nif(nif);
  const auto& shader = tree.blocks.at(0);
  EXPECT_EQ(text_of(shader, "materialClass"), "Multi-layer parallax");
  EXPECT_EQ(number_of(shader, "multiLayerThickness"), 5.0f);
  EXPECT_EQ(number_of(shader, "multiLayerRefractionScale"), 0.25f);
  EXPECT_EQ(numbers_of(shader, "multiLayerInnerScale"), (std::vector<float>{1.5f, 1.75f}));
  EXPECT_EQ(number_of(shader, "multiLayerEnvMapStrength"), 0.5f);
  EXPECT_EQ(find_field(shader, "envMapScale"), nullptr);
}

TEST(NifTree, EffectShaderCarriesItsFiveTextureSlotsAndBaseColor) {
  const std::vector<Block> blocks{effect_shader_block(0, {"Textures\\e_d.dds", "Textures\\e_g.dds",
                                                          "Textures\\e_e.dds", "Textures\\e_n.dds",
                                                          "Textures\\e_m.dds"})};
  const auto nif = fo4::nif::read_nif(build_nif({"effect"}, blocks, {}));
  const auto tree = fo4::nif::describe_nif(nif);
  const auto& shader = tree.blocks.at(0);
  EXPECT_EQ(text_of(shader, "sourceTexture"), "Textures\\e_d.dds");
  EXPECT_EQ(text_of(shader, "greyscaleTexture"), "Textures\\e_g.dds");
  EXPECT_EQ(text_of(shader, "envMapTexture"), "Textures\\e_e.dds");
  EXPECT_EQ(text_of(shader, "normalTexture"), "Textures\\e_n.dds");
  EXPECT_EQ(text_of(shader, "envMaskTexture"), "Textures\\e_m.dds");
  EXPECT_EQ(number_of(shader, "envMapScale"), 1.75f);
  EXPECT_EQ(numbers_of(shader, "baseColor"), (std::vector<float>{0.4f, 0.3f, 0.2f}));
  EXPECT_EQ(number_of(shader, "baseColorAlpha"), 0.6f);
  EXPECT_EQ(number_of(shader, "lightingInfluence"), 255.0f);
}

TEST(NifTree, AlphaBlockCarriesItsFlagsAndDecodedFields) {
  const auto nif = fo4::nif::read_nif(full_file(0x1U));
  const auto tree = fo4::nif::describe_nif(nif);
  const auto& alpha = tree.blocks.at(3);
  EXPECT_EQ(number_of(alpha, "flags"), 513.0f);
  EXPECT_TRUE(bool_of(alpha, "alphaBlend"));
  EXPECT_TRUE(bool_of(alpha, "alphaTest"));
  EXPECT_EQ(number_of(alpha, "threshold"), 128.0f);
  EXPECT_EQ(text_of(alpha, "sourceBlend"), "One");
  EXPECT_EQ(text_of(alpha, "destinationBlend"), "One");
  EXPECT_EQ(text_of(alpha, "testFunction"), "Always");
}

TEST(NifTree, CollisionAndExtraBlocksKeepTheirOwnFields) {
  const auto nif = fo4::nif::read_nif(full_file(fo4::nif::k_bsx_editor_marker_bit));
  const auto tree = fo4::nif::describe_nif(nif);
  const auto& object = tree.blocks.at(6);
  EXPECT_EQ(text_of(object, "target"), "block 1");
  EXPECT_EQ(number_of(object, "flags"), 4.0f);
  EXPECT_EQ(text_of(object, "system"), "block 7");
  EXPECT_EQ(number_of(object, "bodyId"), 42.0f);
  EXPECT_EQ(text_of(tree.blocks.at(7), "bytes"), "16");
  EXPECT_EQ(text_of(tree.blocks.at(4), "slots"), "2");
  EXPECT_EQ(text_of(tree.blocks.at(4), "tex1"), "Textures\\a_n.dds");
  EXPECT_TRUE(bool_of(tree.blocks.at(5), "editorMarker"));
  EXPECT_EQ(text_of(tree.blocks.at(8), "size"), "6");
}

TEST(NifTree, UnreadableBlockStaysInTheTreeWithItsFault) {
  Writer truncated;
  truncated.u32(1);
  const std::vector<Block> blocks{Block{"BSLightingShaderProperty", truncated.bytes()}};
  const auto nif = fo4::nif::read_nif(build_nif({"shader"}, blocks, {}));
  const auto tree = fo4::nif::describe_nif(nif);
  ASSERT_EQ(tree.blocks.size(), 1U);
  EXPECT_EQ(tree.blocks[0].type, "BSLightingShaderProperty");
  EXPECT_EQ(tree.blocks[0].cat, "shader");
  EXPECT_NE(text_of(tree.blocks[0], "readError"), "<missing>");
  EXPECT_EQ(find_field(tree.blocks[0], "materialClass"), nullptr);
}

TEST(NifVerify, ReportsTheSameCheckingOrderOnACleanFile) {
  const auto nif = fo4::nif::read_nif(full_file(0x1U));
  const auto report = fo4::nif::verify_nif(nif);
  const std::vector<std::string> names{"header parses", "each block is listed under one parent only",
                                       "no editor markers", "shapes parse",
                                       "vertex descriptor matches the data",
                                       "shapes have vertices and triangles", "normals match the vertex count",
                                       "UVs match the vertex count", "shader and skin reads are clean",
                                       "texture paths are relative"};
  ASSERT_EQ(report.checks.size(), names.size());
  for (std::size_t i = 0; i < names.size(); ++i) {
    EXPECT_EQ(report.checks[i].name, names[i]);
    EXPECT_EQ(report.checks[i].failure, "") << names[i];
  }
  EXPECT_EQ(report.failed(), 0U);
}

TEST(NifVerify, EditorMarkerBitFailsTheCheck) {
  const auto nif = fo4::nif::read_nif(full_file(0x21U));
  const auto report = fo4::nif::verify_nif(nif);
  ASSERT_EQ(report.failed(), 1U);
  EXPECT_EQ(report.checks.at(2).failure, "block 5 sets the BSX editor-marker bit");
}

TEST(NifVerify, TexturePathFaultNamesTheShapeAndThePath) {
  const Transform xf{};
  LightingFields lighting;
  lighting.name = 1;
  lighting.texture_set = 2;
  const std::vector<Block> blocks{
      shape_with_refs("BSTriShape", 0, xf, k_full, 16, -1, 1, -1, triangle_vertices(), {0, 1, 2}),
      full_lighting_shader_block(lighting),
      texture_set({"C:\\Art\\a_d.dds"}),
  };
  const auto nif = fo4::nif::read_nif(build_nif({"Tri", "Materials\\a.BGSM"}, blocks, {}));
  const auto report = fo4::nif::verify_nif(nif);
  EXPECT_EQ(report.checks.at(9).failure,
            "shape 'Tri' (block 0) texture 'C:\\Art\\a_d.dds' is a drive-letter path");
}

TEST(NifVerify, ShapeFaultNamesTheBlock) {
  const std::vector<Block> blocks{opaque_block("BSTriShape", 4)};
  const auto nif = fo4::nif::read_nif(build_nif({"shape"}, blocks, {}));
  const auto report = fo4::nif::verify_nif(nif);
  EXPECT_EQ(report.checks.at(3).name, "shapes parse");
  EXPECT_EQ(report.checks.at(3).failure.rfind("block 0: ", 0), 0U);
  EXPECT_EQ(report.failed(), 1U);
}

// Invariant: a descriptor whose offsets disagree with its own sizes is the regression this check exists for.
TEST(NifVerify, DescriptorOffsetsThatDisagreeWithTheAttributeSizesFailTheCheck) {
  Writer vertices;
  for (int vertex = 0; vertex < 3; ++vertex) {
    for (int i = 0; i < 4; ++i) vertices.u32(0);
    vertices.u32(0);
  }
  const std::uint64_t flags = static_cast<std::uint64_t>(k_has_uv | k_full) << 44U;
  const std::uint64_t uv0_at_twelve = std::uint64_t{3} << 8U;
  const std::vector<Block> blocks{shape_with_descriptor(5U | uv0_at_twelve | flags, 1, 3, vertices.bytes())};
  const auto nif = fo4::nif::read_nif(build_nif({"shape"}, blocks, {}));
  const auto report = fo4::nif::verify_nif(nif);
  EXPECT_EQ(report.checks.at(4).name, "vertex descriptor matches the data");
  EXPECT_EQ(report.checks.at(4).failure, "block 0 stores UV0 at 12 but its attributes put it at 16");
}

// Invariant: a land-data flag adds no vertex bytes, so the reader and the descriptor check agree.
// Invariant: verify used to count land data as four bytes and faulted a file the reader accepts.
TEST(NifVerify, LandDataFlagAddsNoVertexBytesAndBothReadersAgree) {
  Writer vertices;
  for (int vertex = 0; vertex < 3; ++vertex) {
    for (int i = 0; i < 4; ++i) vertices.u32(0);
    vertices.u32(0);
  }
  const std::uint64_t flags = static_cast<std::uint64_t>(k_has_uv | k_has_land_data | k_full) << 44U;
  const std::uint64_t uv0_at_sixteen = std::uint64_t{16} << 6U;
  const std::vector<Block> blocks{shape_with_descriptor(5U | uv0_at_sixteen | flags, 1, 3, vertices.bytes())};
  const auto nif = fo4::nif::read_nif(build_nif({"shape"}, blocks, {}));

  const auto shapes = fo4::nif::read_shapes(nif);
  ASSERT_EQ(shapes.size(), 1U);
  EXPECT_EQ(shapes[0].tris.size(), 3U);

  const auto report = fo4::nif::verify_nif(nif);
  EXPECT_EQ(report.checks.at(4).failure, "");
  EXPECT_EQ(report.failed(), 0U);
}

TEST(NifDescribe, InspectAndGeometryAgreeOnShapeCounts) {
  const auto nif = fo4::nif::read_nif(full_file(0x1U));
  const auto inspect = fo4::nif::inspect_nif(nif);
  EXPECT_TRUE(inspect.fo4);
  EXPECT_TRUE(inspect.has_bsx);
  EXPECT_TRUE(inspect.has_collision);
  ASSERT_EQ(inspect.shapes.size(), 1U);
  EXPECT_EQ(inspect.shapes[0].name, "Tri");
  EXPECT_EQ(inspect.shapes[0].verts, 3U);
  EXPECT_EQ(inspect.shapes[0].tris, 1U);
  EXPECT_FALSE(inspect.shapes[0].skinned);
  EXPECT_EQ(inspect.shapes[0].material, "shaderName");
  ASSERT_EQ(inspect.shapes[0].textures.size(), 2U);
  EXPECT_EQ(inspect.shapes[0].textures[0].slot, 0U);
  EXPECT_EQ(inspect.shapes[0].textures[1].path, "Textures\\a_n.dds");
  EXPECT_FALSE(inspect.shapes[0].tangents);

  const auto geometry = fo4::nif::geometry_nif(nif);
  ASSERT_EQ(geometry.shapes.size(), 1U);
  EXPECT_EQ(geometry.shapes[0].tris, (std::vector<std::uint16_t>{0, 1, 2}));
  EXPECT_FALSE(geometry.shapes[0].billboard);
  EXPECT_EQ(geometry.shapes[0].billboard_mode, -1);
}

TEST(NifDescribe, MaterialPathTestIgnoresCase) {
  EXPECT_TRUE(fo4::nif::is_bgsm_path("Materials\\Clutter\\Thing.BGSM"));
  EXPECT_TRUE(fo4::nif::is_bgsm_path("a.bgsm"));
  EXPECT_FALSE(fo4::nif::is_bgsm_path("Materials\\Clutter\\Thing.bgem"));
  EXPECT_FALSE(fo4::nif::is_bgsm_path(""));
}
