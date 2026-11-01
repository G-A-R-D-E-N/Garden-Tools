#include "fo4/error.hpp"
#include "fo4/nif/collision.hpp"
#include "fo4/nif/hknp_decode.hpp"
#include "fo4/nif/hknp_encode.hpp"
#include "fo4/nif/hknp_packfile.hpp"
#include "fo4/nif/nif.hpp"
#include "fo4/nif/nif_fix.hpp"
#include "fo4/nif/nif_import.hpp"
#include "fo4/nif/nif_write.hpp"
#include "nif_fixture.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {

using fo4::nif::apply_edits;
using fo4::nif::block_transform;
using fo4::nif::decode_hknp_system;
using fo4::nif::Edit;
using fo4::nif::fix_nif;
using fo4::nif::EditValue;
using fo4::nif::import_obj;
using fo4::nif::ImportOptions;
using fo4::nif::NifFile;
using fo4::nif::read_collision_blobs;
using fo4::nif::read_hknp_packfile;
using fo4::nif::read_nif;
using fo4::nif::read_shader;
using fo4::nif::read_shapes;
using fo4::nif::Transform;
using fo4::nif::write_nif;
using nif_fixture::Block;
using nif_fixture::Writer;

constexpr std::uint32_t k_root = 0;
constexpr std::uint32_t k_shape = 1;
constexpr std::uint32_t k_shader = 4;
constexpr std::uint32_t k_texture_set = 5;

EditValue as_text(const char* value) { return EditValue{std::string{value}}; }
EditValue as_number(float value) { return EditValue{value}; }
EditValue numbers(const std::vector<float>& values) { return EditValue{values}; }

float float_at(std::span<const std::byte> bytes, std::size_t offset) {
  std::uint32_t bits = 0;
  for (std::size_t i = 0; i < 4; ++i) {
    bits |= static_cast<std::uint32_t>(std::to_integer<std::uint8_t>(bytes[offset + i])) << (8 * i);
  }
  return std::bit_cast<float>(bits);
}

std::span<const std::byte> block_bytes(std::span<const std::byte> file, const NifFile& nif, std::uint32_t block) {
  const auto& info = nif.blocks()[block];
  return file.subspan(info.offset, info.size);
}

void expect_rejected(const NifFile& nif, const Edit& edit) {
  const std::vector<Edit> edits{edit};
  EXPECT_THROW(apply_edits(nif, edits), fo4::parse_error);
}

std::vector<std::byte> nif_with_root(std::vector<Block> blocks) {
  blocks.insert(blocks.begin(), nif_fixture::node_block("NiNode", -1, Transform{}, {}));
  return nif_fixture::build_nif({}, blocks, {0});
}

std::vector<std::byte> shared_texture_set_nif() {
  return nif_with_root({
      nif_fixture::lighting_shader_block(1, -1, 3, 0, 0),
      nif_fixture::lighting_shader_block(1, -1, 3, 0, 0),
      nif_fixture::texture_set_block({"a.dds", "b.dds"}),
  });
}

std::vector<std::byte> shared_shader_nif() {
  const fo4::nif::Transform shape_xf{{1.0f, 2.0f, 3.0f}, {1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f}, 1.0f};
  const std::vector<nif_fixture::Block> blocks{
      nif_fixture::node_block("NiNode", -1, Transform{}, {1, 2}),
      nif_fixture::shape_block("BSTriShape", -1, shape_xf, 0, 44, -1, 3, -1, {}, {}),
      nif_fixture::shape_block("BSTriShape", -1, shape_xf, 0, 44, -1, 3, -1, {}, {}),
      nif_fixture::lighting_shader_block(1, 0, 4, 0, 0),
      nif_fixture::texture_set_block({"a.dds", "b.dds"}),
  };
  return nif_fixture::build_nif({"materials\\old.bgsm"}, blocks, {0});
}

std::vector<std::byte> block_copy(std::span<const std::byte> file, const NifFile& nif, std::uint32_t block) {
  const auto bytes = block_bytes(file, nif, block);
  return {bytes.begin(), bytes.end()};
}

void expect_changes_within(std::span<const std::byte> before, std::span<const std::byte> after, std::size_t lo,
                           std::size_t hi) {
  ASSERT_EQ(before.size(), after.size());
  for (std::size_t i = 0; i < before.size(); ++i) {
    EXPECT_TRUE(before[i] == after[i] || (i >= lo && i < hi))
        << "byte " << i << " changed outside [" << lo << ", " << hi << ")";
  }
}

Block node_with_two_extras() {
  Writer w;
  w.i32(-1);
  w.u32(2);
  w.i32(-1);
  w.i32(-1);
  w.i32(-1);
  w.u32(0);
  w.transform(Transform{});
  w.i32(-1);
  w.u32(0);
  return Block{"NiNode", w.bytes()};
}

std::vector<std::byte> import_text(std::string_view obj, bool from_blender = false, bool box = false) {
  ImportOptions options;
  options.material = "materials\\body.bgsm";
  options.tex_diffuse = "textures\\body_d.dds";
  options.tex_normal = "textures\\body_n.dds";
  options.from_blender = from_blender;
  options.box_collision = box;
  return import_obj(obj, options);
}

std::uint32_t uint_at(std::span<const std::byte> bytes, std::size_t offset) {
  std::uint32_t value = 0;
  for (std::size_t i = 0; i < 4; ++i) {
    value |= static_cast<std::uint32_t>(std::to_integer<std::uint8_t>(bytes[offset + i])) << (8 * i);
  }
  return value;
}

std::vector<std::byte> quad_with_wrong_frame_nif() {
  constexpr std::uint32_t flags = nif_fixture::k_full | nif_fixture::k_has_uv | nif_fixture::k_has_normal |
                                  nif_fixture::k_has_tangent | nif_fixture::k_has_colour | nif_fixture::k_has_skin;
  constexpr std::array<std::array<float, 2>, 4> corners{{{0.0f, 0.0f}, {1.0f, 0.0f}, {1.0f, 1.0f}, {0.0f, 1.0f}}};
  constexpr std::array<std::array<std::uint16_t, 2>, 4> uvs{
      {{0x0000, 0x0000}, {0x3C00, 0x0000}, {0x3C00, 0x3C00}, {0x0000, 0x3C00}}};
  std::vector<std::byte> vertices;
  for (std::size_t v = 0; v < 4; ++v) {
    nif_fixture::Vertex vertex{};
    vertex.position[0] = corners[v][0];
    vertex.position[1] = corners[v][1];
    vertex.bitangent_x = 0.7f;
    vertex.uv[0] = uvs[v][0];
    vertex.uv[1] = uvs[v][1];
    vertex.normal[0] = 128;
    vertex.normal[1] = 128;
    vertex.normal[2] = 255;
    vertex.tangent[0] = 128;
    vertex.tangent[1] = 255;
    vertex.tangent[2] = 128;
    const auto encoded = nif_fixture::encode_vertex(vertex);
    vertices.insert(vertices.end(), encoded.begin(), encoded.end());
  }
  const std::vector<nif_fixture::Block> blocks{
      nif_fixture::node_block("NiNode", -1, Transform{}, {1}),
      nif_fixture::shape_block("BSTriShape", -1, Transform{}, flags, 44, -1, 2, -1, vertices, {0, 1, 2, 0, 2, 3}),
      nif_fixture::lighting_shader_block(1, -1, 3, 0x80400201U, 0x21U),
      nif_fixture::texture_set_block({"textures\\body_d.dds", "textures\\body_n.dds"}),
  };
  return nif_fixture::build_nif({}, blocks, {0});
}

std::vector<std::byte> padded_paths_nif() {
  const std::vector<nif_fixture::Block> blocks{
      nif_fixture::node_block("NiNode", -1, Transform{}, {1}),
      nif_fixture::shape_block("BSTriShape", -1, Transform{}, 0, 44, -1, 2, -1, {}, {}),
      nif_fixture::lighting_shader_block(1, 0, 3, 0, 0),
      nif_fixture::texture_set_block({" textures\\body_d.dds ", "textures\\body_n.dds\t"}),
  };
  return nif_fixture::build_nif({" materials\\body.bgsm "}, blocks, {0});
}

}  // namespace

TEST(NifWrite, ZeroEditRoundTripIsByteIdentical) {
  // regression: any header or table field re-encoded differently must show up as a byte diff here
  const auto bytes = nif_fixture::full_nif();
  const auto nif = read_nif(bytes);
  const std::vector<Edit> no_edits;
  EXPECT_EQ(write_nif(nif), bytes);
  EXPECT_EQ(apply_edits(nif, no_edits), bytes);
}

TEST(NifWrite, ZeroEditRoundTripKeepsSharedTextureSet) {
  // regression: a texture set referenced by two shaders must not be duplicated or re-indexed when nothing is edited
  const auto bytes = shared_texture_set_nif();
  EXPECT_EQ(write_nif(read_nif(bytes)), bytes);
}

TEST(NifWrite, NameEditAppendsToStringTable) {
  // regression: a name longer than every existing string must be appended to the table, not written past its end
  const auto bytes = nif_fixture::full_nif();
  const auto nif = read_nif(bytes);
  const std::vector<Edit> edits{Edit{k_shape, "name", as_text("a_much_longer_shape_name")}};
  const auto out = read_nif(apply_edits(nif, edits));
  EXPECT_EQ(out.strings().size(), nif.strings().size() + 1);
  EXPECT_EQ(read_shapes(out).at(0).name, "a_much_longer_shape_name");
  const auto root = static_cast<std::size_t>(out.node_for_block(k_root));
  EXPECT_EQ(out.nodes().at(root).name, "shape");
}

TEST(NifWrite, NameEditReusesAnExistingStringIndex) {
  // regression: a name already in the string table must reuse its index, not append a duplicate entry
  const auto bytes = nif_fixture::full_nif();
  const auto nif = read_nif(bytes);
  const std::vector<Edit> edits{Edit{k_shape, "name", as_text("RootBone")}};
  const auto out = read_nif(apply_edits(nif, edits));
  EXPECT_EQ(out.strings().size(), nif.strings().size());
  EXPECT_EQ(read_shapes(out).at(0).name, "RootBone");
}

TEST(NifWrite, ZeroEditRoundTripKeepsExportStringsGroupsAndMaxLength) {
  // regression: export strings, max string length and the group table must be re-encoded as read, or a zero-edit save changes bytes
  const std::vector<nif_fixture::Block> blocks{
      nif_fixture::node_block("NiNode", 0, Transform{}, {1}),
      nif_fixture::shape_block("BSTriShape", 3, Transform{}, 0, 44, -1, 2, -1, {}, {}),
      nif_fixture::lighting_shader_block(1, 1, 3, 0, 0),
      nif_fixture::texture_set_block({"textures\\a_d.dds", ""}),
  };
  const auto bytes = nif_fixture::build_nif({"Root", "materials\\a.bgsm", "a_longer_shape_name", "Tri"}, blocks, {0},
                                            {"Author", "Process", "", "Export info"}, 19, {7, 9});
  const auto nif = read_nif(bytes);
  EXPECT_EQ(write_nif(nif), bytes);
  EXPECT_EQ(apply_edits(nif, std::vector<Edit>{}), bytes);
}

TEST(NifWrite, NodeEditsFollowTheExtraDataCount) {
  // regression: AV translation and scale must move with the extra-data count, or node neighbours get overwritten
  const auto bytes = nif_fixture::build_nif({}, {node_with_two_extras()}, {0});
  const auto nif = read_nif(bytes);
  const std::vector<Edit> edits{
      Edit{k_root, "translation", numbers({1.0f, 2.0f, 3.0f})},
      Edit{k_root, "scale", as_number(4.0f)},
  };
  const auto out = read_nif(apply_edits(nif, edits));
  const auto& node = out.nodes().at(static_cast<std::size_t>(out.node_for_block(k_root)));
  EXPECT_FLOAT_EQ(node.local.t[0], 1.0f);
  EXPECT_FLOAT_EQ(node.local.t[1], 2.0f);
  EXPECT_FLOAT_EQ(node.local.t[2], 3.0f);
  EXPECT_FLOAT_EQ(node.local.s, 4.0f);
  EXPECT_FLOAT_EQ(node.local.r[0], 1.0f);
}

TEST(NifWrite, ShapeTransformEditsKeepTheShaderRef) {
  // regression: a shape translation or scale must land in the AV transform, not in the shader ref after it
  const auto bytes = nif_fixture::full_nif();
  const auto nif = read_nif(bytes);
  const std::vector<Edit> edits{
      Edit{k_shape, "translation", numbers({4.0f, 5.0f, 6.0f})},
      Edit{k_shape, "scale", as_number(3.0f)},
  };
  const auto out = read_nif(apply_edits(nif, edits));
  const auto xf = block_transform(out, k_shape);
  EXPECT_FLOAT_EQ(xf.t[0], 4.0f);
  EXPECT_FLOAT_EQ(xf.t[1], 5.0f);
  EXPECT_FLOAT_EQ(xf.t[2], 6.0f);
  EXPECT_FLOAT_EQ(xf.s, 3.0f);
  EXPECT_EQ(read_shapes(out).at(0).shader_ref, static_cast<std::int32_t>(k_shader));
}

TEST(NifWrite, MaterialEditOnShapeRenamesShaderPath) {
  // regression: a material edit sent on the shape must rewrite the shader name that Shape.material reads
  const auto bytes = nif_fixture::full_nif();
  const auto nif = read_nif(bytes);
  const std::vector<Edit> edits{Edit{k_shape, "material", as_text("materials\\new.bgsm")}};
  const auto out = read_nif(apply_edits(nif, edits));
  EXPECT_EQ(read_shapes(out).at(0).material, "materials\\new.bgsm");
  EXPECT_EQ(read_shader(out, k_shader).name, "materials\\new.bgsm");
}

TEST(NifWrite, ShaderFloatEditsLandAtNifSpecOffsets) {
  // regression: each shader float must be written at its NIF.md offset, so glossiness cannot hit refraction
  const auto bytes = nif_fixture::full_nif();
  const auto nif = read_nif(bytes);
  const std::vector<Edit> edits{
      Edit{k_shape, "uvOffset", numbers({0.25f, 0.5f})},
      Edit{k_shape, "uvScale", numbers({2.0f, 3.0f})},
      Edit{k_shape, "emissiveColor", numbers({0.5f, 0.25f, 0.125f})},
      Edit{k_shape, "emissiveMultiple", as_number(4.0f)},
      Edit{k_shape, "alpha", as_number(0.5f)},
      Edit{k_shape, "glossiness", as_number(0.75f)},
      Edit{k_shape, "specularColor", numbers({0.25f, 0.5f, 0.75f})},
      Edit{k_shape, "specularStrength", as_number(0.875f)},
  };
  const auto out_bytes = apply_edits(nif, edits);
  const auto out = read_nif(out_bytes);
  const auto shader = block_bytes(out_bytes, out, k_shader);
  EXPECT_FLOAT_EQ(float_at(shader, 24), 0.25f);
  EXPECT_FLOAT_EQ(float_at(shader, 28), 0.5f);
  EXPECT_FLOAT_EQ(float_at(shader, 32), 2.0f);
  EXPECT_FLOAT_EQ(float_at(shader, 36), 3.0f);
  EXPECT_FLOAT_EQ(float_at(shader, 44), 0.5f);
  EXPECT_FLOAT_EQ(float_at(shader, 48), 0.25f);
  EXPECT_FLOAT_EQ(float_at(shader, 52), 0.125f);
  EXPECT_FLOAT_EQ(float_at(shader, 56), 4.0f);
  EXPECT_FLOAT_EQ(float_at(shader, 68), 0.5f);
  EXPECT_FLOAT_EQ(float_at(shader, 72), 0.0f);
  EXPECT_FLOAT_EQ(float_at(shader, 76), 0.75f);
  EXPECT_FLOAT_EQ(float_at(shader, 80), 0.25f);
  EXPECT_FLOAT_EQ(float_at(shader, 84), 0.5f);
  EXPECT_FLOAT_EQ(float_at(shader, 88), 0.75f);
  EXPECT_FLOAT_EQ(float_at(shader, 92), 0.875f);
  EXPECT_EQ(read_shader(out, k_shader).texture_set_ref, 5);
}

TEST(NifWrite, TextureSlotEditExtendsTheSet) {
  // regression: a texture slot past the end of the set must extend the set, not write past the texture set block
  const auto bytes = nif_fixture::full_nif();
  const auto nif = read_nif(bytes);
  const std::vector<Edit> edits{
      Edit{k_shape, "tex0", as_text("textures\\new_d.dds")},
      Edit{k_shape, "tex3", as_text("textures\\extra.dds")},
  };
  const auto out = read_nif(apply_edits(nif, edits));
  const auto textures = read_shader(out, k_shader).textures;
  ASSERT_EQ(textures.size(), 4U);
  EXPECT_EQ(textures[0], "textures\\new_d.dds");
  EXPECT_EQ(textures[1], "textures\\body_n.dds");
  EXPECT_EQ(textures[2], "");
  EXPECT_EQ(textures[3], "textures\\extra.dds");
  EXPECT_EQ(read_shader(out, k_shader).texture_set_ref, static_cast<std::int32_t>(k_texture_set));
}

TEST(NifWrite, SharedTextureSetIsCopiedBeforeEdit) {
  // regression: editing one shader's texture must not change a second shader that shares the same texture set
  const auto nif = read_nif(shared_texture_set_nif());
  const std::vector<Edit> edits{Edit{1, "tex0", as_text("new.dds")}};
  const auto out = read_nif(apply_edits(nif, edits));
  EXPECT_EQ(read_shader(out, 1).textures, (std::vector<std::string>{"new.dds", "b.dds"}));
  EXPECT_EQ(read_shader(out, 2).textures, (std::vector<std::string>{"a.dds", "b.dds"}));
  EXPECT_EQ(read_shader(out, 1).texture_set_ref, 4);
  EXPECT_EQ(read_shader(out, 2).texture_set_ref, 3);
}

TEST(NifWrite, EditsFollowTheExtraDataCountOnShapeAndShader) {
  // regression: shape and shader fields must move with their extra-data count, or the edit lands on neighbouring bytes
  const fo4::nif::Transform shape_xf{{7.0f, 8.0f, 9.0f}, {1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f}, 1.0f};
  const auto bytes = nif_with_root({
      nif_fixture::shape_block("BSTriShape", -1, shape_xf, 0, 44, -1, 2, -1, {}, {}, 2),
      nif_fixture::lighting_shader_block(1, -1, 3, 0, 0, 2),
      nif_fixture::texture_set_block({"a.dds", "b.dds"}),
  });
  const auto nif = read_nif(bytes);
  const std::vector<Edit> edits{
      Edit{1, "translation", numbers({4.0f, 5.0f, 6.0f})},
      Edit{1, "glossiness", as_number(0.5f)},
      Edit{1, "tex0", as_text("new.dds")},
  };
  const auto out_bytes = apply_edits(nif, edits);
  const auto out = read_nif(out_bytes);
  const auto xf = block_transform(out, 1);
  EXPECT_FLOAT_EQ(xf.t[0], 4.0f);
  EXPECT_FLOAT_EQ(xf.t[1], 5.0f);
  EXPECT_FLOAT_EQ(xf.t[2], 6.0f);
  EXPECT_EQ(read_shader(out, 2).textures, (std::vector<std::string>{"new.dds", "b.dds"}));
  EXPECT_FLOAT_EQ(float_at(block_bytes(out_bytes, out, 2), 84), 0.5f);
  expect_changes_within(block_bytes(bytes, nif, 1), block_bytes(out_bytes, out, 1), 24, 36);
  expect_changes_within(block_bytes(bytes, nif, 2), block_bytes(out_bytes, out, 2), 84, 88);
}

TEST(NifWrite, SharedShaderIsCopiedBeforeAShapeEdit) {
  // regression: a translation, float, texture or material edit on one shape must not change the other shape that shares its shader
  const auto bytes = shared_shader_nif();
  const auto nif = read_nif(bytes);
  const std::vector<Edit> edits{
      Edit{1, "translation", numbers({4.0f, 5.0f, 6.0f})},
      Edit{1, "glossiness", as_number(0.5f)},
      Edit{1, "tex0", as_text("new.dds")},
      Edit{1, "material", as_text("materials\\new.bgsm")},
  };
  const auto out_bytes = apply_edits(nif, edits);
  const auto out = read_nif(out_bytes);
  const auto shapes = read_shapes(out);
  ASSERT_EQ(shapes.size(), 2U);
  const auto& edited = shapes.at(0);
  const auto& untouched = shapes.at(1);
  EXPECT_NE(edited.shader_ref, 3);
  EXPECT_EQ(untouched.shader_ref, 3);
  EXPECT_EQ(untouched.material, "materials\\old.bgsm");
  EXPECT_EQ(read_shader(out, 3).textures, (std::vector<std::string>{"a.dds", "b.dds"}));
  EXPECT_EQ(read_shader(out, 3).texture_set_ref, 4);
  EXPECT_FLOAT_EQ(block_transform(out, 2).t[0], 1.0f);
  EXPECT_FLOAT_EQ(block_transform(out, 1).t[0], 4.0f);
  EXPECT_EQ(block_copy(out_bytes, out, 3), block_copy(bytes, nif, 3));
  EXPECT_EQ(block_copy(out_bytes, out, 4), block_copy(bytes, nif, 4));
  const auto edited_shader = static_cast<std::uint32_t>(edited.shader_ref);
  EXPECT_EQ(read_shader(out, edited_shader).name, "materials\\new.bgsm");
  EXPECT_EQ(read_shader(out, edited_shader).textures, (std::vector<std::string>{"new.dds", "b.dds"}));
  EXPECT_FLOAT_EQ(float_at(block_bytes(out_bytes, out, edited_shader), 76), 0.5f);
}

TEST(NifWrite, RejectsUnsupportedAndMalformedEditsWithoutOutput) {
  // regression: an unsupported, read-only or mistyped edit must throw parse_error, never return partially edited bytes
  const auto bytes = nif_fixture::full_nif();
  const auto nif = read_nif(bytes);
  expect_rejected(nif, Edit{k_shape, "shaderFlags", as_number(1.0f)});
  expect_rejected(nif, Edit{k_shape, "alphaBlend", as_number(1.0f)});
  expect_rejected(nif, Edit{k_shape, "verts", as_number(1.0f)});
  expect_rejected(nif, Edit{k_shape, "shaderType", as_text("x")});
  expect_rejected(nif, Edit{k_shape, "translation", numbers({1.0f, 2.0f})});
  expect_rejected(nif, Edit{k_shape, "translation", as_text("x")});
  expect_rejected(nif, Edit{k_shape, "scale", numbers({1.0f, 2.0f, 3.0f})});
  expect_rejected(nif, Edit{k_shape, "scale", as_number(std::numeric_limits<float>::quiet_NaN())});
  expect_rejected(nif, Edit{k_shape, "tex0", as_number(1.0f)});
  expect_rejected(nif, Edit{k_root, "tex0", as_text("x.dds")});
  expect_rejected(nif, Edit{k_root, "material", as_text("x.bgsm")});
  expect_rejected(nif, Edit{k_texture_set, "name", as_text("x")});
  expect_rejected(nif, Edit{99, "translation", numbers({1.0f, 2.0f, 3.0f})});
}

TEST(NifWrite, RejectsEditsOnBrokenShapesAndShaders) {
  // regression: an edit on a broken shape, shader or texture set must throw, never write into another block
  auto shader = nif_fixture::lighting_shader_block(1, -1, 2, 0, 0);
  shader.data.resize(44);
  auto trailing = nif_fixture::texture_set_block({"a.dds"});
  trailing.data.push_back(std::byte{0});
  const auto shapeless =
      nif_with_root({nif_fixture::shape_block("BSTriShape", -1, Transform{}, 0, 44, -1, -1, -1, {}, {})});
  expect_rejected(read_nif(shapeless), Edit{1, "tex0", as_text("x.dds")});
  const auto unset = nif_with_root({nif_fixture::lighting_shader_block(1, -1, -1, 0, 0)});
  expect_rejected(read_nif(unset), Edit{1, "tex0", as_text("x.dds")});
  const auto wrong_set = nif_with_root({nif_fixture::lighting_shader_block(1, -1, 0, 0, 0)});
  expect_rejected(read_nif(wrong_set), Edit{1, "tex0", as_text("x.dds")});
  const auto trailing_set = nif_with_root({nif_fixture::lighting_shader_block(1, -1, 2, 0, 0), trailing});
  expect_rejected(read_nif(trailing_set), Edit{1, "tex0", as_text("x.dds")});
  const auto short_shader = nif_with_root({shader, nif_fixture::texture_set_block({"a.dds"})});
  expect_rejected(read_nif(short_shader), Edit{1, "glossiness", as_number(0.5f)});
  const auto trailing_shared = nif_with_root({nif_fixture::lighting_shader_block(1, -1, 3, 0, 0),
                                              nif_fixture::lighting_shader_block(1, -1, 3, 0, 0), trailing});
  expect_rejected(read_nif(trailing_shared), Edit{1, "tex0", as_text("x.dds")});
}

TEST(NifImport, QuadRoundTripsNamesTexturesPositionsUvsNormalsAndTriangles) {
  const auto bytes = import_text(
      "o Body\n"
      "v 0 0 0\nv 1 0 0\nv 1 1 0\nv 0 1 0\n"
      "vt 0 0\nvt 65504 0\nvt 1 1\nvt 0.25 0.5\n"
      "vn 0 0 1\n"
      "f 1/1/1 2/2/1 3/3/1 4/4/1\n");
  constexpr std::array<std::uint8_t, 8> vanilla_descriptor{0x07, 0x04, 0x65, 0x00, 0x00, 0xB0, 0x41, 0x00};
  EXPECT_NE(std::search(bytes.begin(), bytes.end(), vanilla_descriptor.begin(), vanilla_descriptor.end(),
                        [](std::byte lhs, std::uint8_t rhs) { return std::to_integer<std::uint8_t>(lhs) == rhs; }),
            bytes.end());
  const auto nif = read_nif(bytes);
  const auto shapes = read_shapes(nif);
  ASSERT_EQ(shapes.size(), 1U);
  const auto& shape = shapes[0];
  EXPECT_EQ(shape.warning, "");
  EXPECT_EQ(shape.name, "Body");
  EXPECT_EQ(shape.material, "materials\\body.bgsm");
  ASSERT_GE(shape.textures.size(), 2U);
  EXPECT_EQ(shape.textures[0], "textures\\body_d.dds");
  EXPECT_EQ(shape.textures[1], "textures\\body_n.dds");
  EXPECT_EQ(shape.verts, (std::vector<float>{0, 0, 0, 1, 0, 0, 1, 1, 0, 0, 1, 0}));
  EXPECT_EQ(shape.tris, (std::vector<std::uint16_t>{0, 1, 2, 0, 2, 3}));
  EXPECT_EQ(shape.uvs, (std::vector<float>{0, 0, 65504, 0, 1, 1, 0.25f, 0.5f}));
  ASSERT_EQ(shape.normals.size(), 12U);
  for (std::size_t v = 0; v < 4; ++v) {
    EXPECT_NEAR(shape.normals[3 * v], 0.0f, 0.01f);
    EXPECT_NEAR(shape.normals[3 * v + 1], 0.0f, 0.01f);
    EXPECT_NEAR(shape.normals[3 * v + 2], 1.0f, 0.01f);
  }
}

TEST(NifImport, TangentFrameFollowsTheUvAxesWithSmoothedNormals) {
  const auto bytes = import_text(
      "v 0 0 0\nv 1 0 0\nv 1 1 0\nv 0 1 0\n"
      "vt 0 0\nvt 1 0\nvt 1 1\nvt 0 1\n"
      "f 1/1 2/2 3/3 4/4\n");
  const auto nif = read_nif(bytes);
  const auto shapes = read_shapes(nif);
  ASSERT_EQ(shapes.size(), 1U);
  const auto& shape = shapes[0];
  ASSERT_EQ(shape.tangents.size(), 12U);
  ASSERT_EQ(shape.bitangents.size(), 12U);
  for (std::size_t v = 0; v < 4; ++v) {
    for (std::size_t k = 0; k < 3; ++k) {
      EXPECT_NEAR(shape.normals[3 * v + k], k == 2 ? 1.0f : 0.0f, 0.01f);
      EXPECT_NEAR(shape.tangents[3 * v + k], k == 0 ? 1.0f : 0.0f, 0.01f);
      EXPECT_NEAR(shape.bitangents[3 * v + k], k == 1 ? 1.0f : 0.0f, 0.01f);
    }
  }
}

TEST(NifImport, BlenderYUpExportIsRestoredToZUp) {
  const auto bytes = import_text("v 1 2 3\nv 0 0 0\nv 0 1 0\nf 1 2 3\n", true);
  const auto nif = read_nif(bytes);
  const auto shapes = read_shapes(nif);
  ASSERT_EQ(shapes.size(), 1U);
  const auto& verts = shapes[0].verts;
  ASSERT_GE(verts.size(), 3U);
  EXPECT_EQ(verts[0], 1.0f);
  EXPECT_EQ(verts[1], -3.0f);
  EXPECT_EQ(verts[2], 2.0f);
}

TEST(NifImport, RejectsObjThatWouldWriteAWrongNif) {
  EXPECT_THROW(import_text("v 0 0 0\nv 1 0 0\nv 0 1 0\n"), std::runtime_error);
  EXPECT_THROW(import_text("v 0 0 0\nv 1 0 0\nv 0 1 0\nf 1 2 4\n"), std::runtime_error);
  EXPECT_THROW(import_text("v 0 0 0\nv 1 0 0\nv 0 1 0\nf 0 1 2\n"), std::runtime_error);
  EXPECT_THROW(import_text("v 0 0 0\nv 1 0 0\nv 0 1 0\nvt 70000 0\nf 1/1 2 3\n"), std::runtime_error);
  EXPECT_THROW(import_text("v 0 0 0\nv 1 0 0\nv nan 0 0\nf 1 2 3\n"), std::runtime_error);
}

TEST(NifImport, BoxCollisionAuthorsOneBodyThatEnclosesTheMeshTightly) {
  const auto bytes = import_text("v -1 -2 0\nv 3 -2 0\nv 3 4 0\nv -1 4 0\nv -1 -2 5\nv 3 4 5\nf 1 2 3\nf 4 5 6\n",
                                 false, true);
  const auto nif = read_nif(bytes);
  ASSERT_EQ(nif.blocks().size(), 7U);
  const auto root = block_bytes(bytes, nif, 0);
  EXPECT_EQ(uint_at(root, root.size() - 12), 5U);
  EXPECT_EQ(uint_at(block_bytes(bytes, nif, 4), 4), 130U);
  const auto owner = block_bytes(bytes, nif, 5);
  ASSERT_EQ(owner.size(), 14U);
  EXPECT_EQ(uint_at(owner, 4), 0x00060080U);
  EXPECT_EQ(uint_at(owner, 10), 0U);

  const auto blobs = read_collision_blobs(nif);
  ASSERT_EQ(blobs.size(), 1U);
  const auto sys = decode_hknp_system(read_hknp_packfile(blobs[0].bytes));
  ASSERT_EQ(sys.bodies.size(), 1U);
  const auto& shape = sys.shapes[static_cast<std::size_t>(sys.bodies[0].shape)];
  ASSERT_EQ(shape.vertices.size(), 8U);
  const std::array<float, 3> low{-1.0f, -2.0f, 0.0f};
  const std::array<float, 3> high{3.0f, 4.0f, 5.0f};
  for (std::size_t k = 0; k < 3; ++k) {
    float vmin = shape.vertices[0][k];
    float vmax = shape.vertices[0][k];
    for (const auto& v : shape.vertices) {
      vmin = std::min(vmin, v[k]);
      vmax = std::max(vmax, v[k]);
    }
    EXPECT_NEAR((vmin - shape.convex_radius) * fo4::nif::k_hknp_game_units_per_metre, low[k], 0.1f);
    EXPECT_NEAR((vmax + shape.convex_radius) * fo4::nif::k_hknp_game_units_per_metre, high[k], 0.1f);
  }
}

TEST(NifImport, FlatBoxCollisionStillImports) {
  const auto bytes = import_text("v 0 0 0\nv 1 0 0\nv 0 1 0\nf 1 2 3\n", false, true);
  const auto nif = read_nif(bytes);
  const auto blobs = read_collision_blobs(nif);
  ASSERT_EQ(blobs.size(), 1U);
  EXPECT_EQ(decode_hknp_system(read_hknp_packfile(blobs[0].bytes)).bodies.size(), 1U);
}

TEST(NifImport, BoxOptionOffWritesNoCollisionBlocks) {
  const auto bytes = import_text("v -1 -2 0\nv 3 -2 0\nv 3 4 0\nf 1 2 3\n");
  const auto nif = read_nif(bytes);
  EXPECT_EQ(nif.blocks().size(), 5U);
  EXPECT_TRUE(read_collision_blobs(nif).empty());
  const auto root = block_bytes(bytes, nif, 0);
  EXPECT_EQ(uint_at(root, root.size() - 12), 0xFFFFFFFFU);
  EXPECT_EQ(uint_at(block_bytes(bytes, nif, 4), 4), 0U);
}

TEST(NifImport, RejectsMoreVerticesThanAShapeCanIndex) {
  std::string obj;
  for (int i = 0; i < 65536; ++i) obj += "v " + std::to_string(i) + " 0 0\n";
  obj += "f";
  for (int i = 1; i <= 65536; ++i) obj += " " + std::to_string(i);
  obj += "\n";
  EXPECT_THROW(import_text(obj), std::runtime_error);
}

TEST(NifFix, RecomputesAWrongTangentFrameAndKeepsTheGeometry) {
  const auto input = quad_with_wrong_frame_nif();
  const auto before = read_shapes(read_nif(input));
  const auto after = read_shapes(read_nif(fix_nif(read_nif(input))));
  ASSERT_EQ(before.size(), 1U);
  ASSERT_EQ(after.size(), 1U);
  EXPECT_EQ(after[0].verts, before[0].verts);
  EXPECT_EQ(after[0].uvs, before[0].uvs);
  EXPECT_EQ(after[0].normals, before[0].normals);
  EXPECT_EQ(after[0].tris, before[0].tris);
  ASSERT_EQ(after[0].tangents.size(), 12U);
  ASSERT_EQ(after[0].bitangents.size(), 12U);
  for (std::size_t v = 0; v < 4; ++v) {
    for (std::size_t k = 0; k < 3; ++k) {
      EXPECT_NEAR(after[0].tangents[3 * v + k], k == 0 ? 1.0f : 0.0f, 0.01f);
      EXPECT_NEAR(after[0].bitangents[3 * v + k], k == 1 ? 1.0f : 0.0f, 0.01f);
    }
  }
}

TEST(NifFix, AddsTheMissingBsxFlagsToTheRoot) {
  const auto fixed = fix_nif(read_nif(quad_with_wrong_frame_nif()));
  const auto nif = read_nif(fixed);
  const auto root = block_bytes(fixed, nif, 0);
  ASSERT_EQ(uint_at(root, 4), 1U);
  const auto bsx = uint_at(root, 8);
  ASSERT_LT(bsx, nif.blocks().size());
  EXPECT_EQ(nif.blocks()[bsx].type, "BSXFlags");
}

TEST(NifFix, TrimsWhitespaceFromTexturePathsAndMaterialNames) {
  const auto shapes = read_shapes(read_nif(fix_nif(read_nif(padded_paths_nif()))));
  ASSERT_EQ(shapes.size(), 1U);
  EXPECT_EQ(shapes[0].material, "materials\\body.bgsm");
  ASSERT_EQ(shapes[0].textures.size(), 2U);
  EXPECT_EQ(shapes[0].textures[0], "textures\\body_d.dds");
  EXPECT_EQ(shapes[0].textures[1], "textures\\body_n.dds");
}

TEST(NifFix, RunningFixAgainChangesNothing) {
  const auto once = fix_nif(read_nif(quad_with_wrong_frame_nif()));
  const auto twice = fix_nif(read_nif(once));
  EXPECT_TRUE(twice == once);
}

TEST(NifFix, RefusesARootThatIsNotANode) {
  const auto nif = read_nif(nif_fixture::build_nif(
      {}, {nif_fixture::shape_block("BSTriShape", -1, Transform{}, 0, 44, -1, -1, -1, {}, {})}, {0}));
  EXPECT_THROW(fix_nif(nif), fo4::parse_error);
}
