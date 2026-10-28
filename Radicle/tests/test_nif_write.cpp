#include "fo4/error.hpp"
#include "fo4/nif/nif.hpp"
#include "fo4/nif/nif_write.hpp"
#include "nif_fixture.hpp"

#include <gtest/gtest.h>

#include <bit>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <string>
#include <vector>

namespace {

using fo4::nif::apply_edits;
using fo4::nif::block_transform;
using fo4::nif::Edit;
using fo4::nif::EditValue;
using fo4::nif::NifFile;
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
}
