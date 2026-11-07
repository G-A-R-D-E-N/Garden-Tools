#include "fo4/error.hpp"
#include "fo4/nif/nif.hpp"
#include "fo4/nif/nif_write.hpp"
#include "nif_fixture.hpp"

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <vector>

namespace {

using fo4::nif::apply_vertex_color;
using fo4::nif::ColorChannel;
using fo4::nif::GradientAxis;
using fo4::nif::NifFile;
using fo4::nif::read_nif;
using fo4::nif::read_shapes;
using fo4::nif::Shape;
using fo4::nif::Transform;
using fo4::nif::VertexColorKind;
using fo4::nif::VertexColorOp;
using nif_fixture::Block;
using nif_fixture::Writer;

constexpr std::uint32_t k_root = 0;
constexpr std::uint32_t k_shape = 1;
constexpr std::uint32_t k_full_uv = nif_fixture::k_full | nif_fixture::k_has_uv;
constexpr std::uint32_t k_full_uv_colour = k_full_uv | nif_fixture::k_has_colour;

constexpr float k_positions[3][3] = {{0.0f, 0.0f, 0.0f}, {1.0f, 0.0f, 0.0f}, {0.0f, 4.0f, 2.0f}};
constexpr std::uint8_t k_colours[3][4] = {{0, 51, 102, 153}, {204, 255, 128, 64}, {10, 20, 30, 40}};
constexpr std::uint64_t k_half_offsets = (2ULL << 8) | (3ULL << 16) | (4ULL << 20) | (5ULL << 28) | (8ULL << 36);

float unit(std::uint8_t byte) { return static_cast<float>(byte) / 255.0f; }

std::vector<std::byte> vertices(bool colour, float first_x = k_positions[0][0]) {
  Writer w;
  for (std::size_t v = 0; v < 3; ++v) {
    for (std::size_t c = 0; c < 3; ++c) w.f32(v == 0 && c == 0 ? first_x : k_positions[v][c]);
    w.f32(0.0f);
    w.u16(0x3800);
    w.u16(0x3400);
    if (colour) {
      for (const std::uint8_t value : k_colours[v]) w.u8(value);
    }
  }
  return w.bytes();
}

std::vector<std::byte> half_skinned_vertices(bool tail = true) {
  constexpr std::uint16_t k_half_positions[3][3] = {{0x0000, 0x0000, 0x0000}, {0x3C00, 0x0000, 0x0000}, {0x0000, 0x4400, 0x4000}};
  constexpr std::uint16_t k_weights[4] = {0x3C00, 0x3800, 0x3400, 0x0000};
  Writer w;
  for (std::size_t v = 0; v < 3; ++v) {
    for (const std::uint16_t value : k_half_positions[v]) w.u16(value);
    w.u16(0x3800);
    w.u16(0x3800);
    w.u16(0x3400);
    for (std::uint8_t i = 0; i < 8; ++i) w.u8(static_cast<std::uint8_t>(0x40 + i));
    for (const std::uint16_t weight : k_weights) w.u16(weight);
    for (std::uint8_t i = 0; i < 4; ++i) w.u8(0);
    if (tail) w.u32(0);
  }
  return w.bytes();
}

std::vector<std::byte> nif_with_root(std::vector<Block> blocks) {
  blocks.insert(blocks.begin(), nif_fixture::node_block("NiNode", -1, Transform{}, {}));
  return nif_fixture::build_nif({}, blocks, {0});
}

std::vector<std::byte> colour_nif(bool colour) {
  return nif_with_root({nif_fixture::shape_block("BSTriShape", -1, Transform{}, colour ? k_full_uv_colour : k_full_uv,
                                                 colour ? 24 : 20, -1, -1, -1, vertices(colour), {0, 1, 2})});
}

Shape shape_of(const std::vector<std::byte>& bytes) {
  const auto nif = read_nif(bytes);
  return read_shapes(nif).at(0);
}

VertexColorOp op_for(VertexColorKind kind) {
  VertexColorOp op;
  op.kind = kind;
  op.block = k_shape;
  return op;
}

void expect_rejected(const NifFile& nif, const VertexColorOp& op) {
  EXPECT_THROW(apply_vertex_color(nif, op), fo4::parse_error);
}

}  // namespace

TEST(NifVertexColor, FillPaintsEveryVertexAndRereadsExactly) {
  const auto bytes = colour_nif(true);
  const auto nif = read_nif(bytes);
  const auto before = read_shapes(nif).at(0);
  auto op = op_for(VertexColorKind::fill);
  op.color = {unit(51), unit(102), unit(153), unit(204)};
  const auto out = apply_vertex_color(nif, op);
  const auto after = shape_of(out);
  ASSERT_EQ(after.colors.size(), 12U);
  for (std::size_t v = 0; v < 3; ++v) {
    for (std::size_t c = 0; c < 4; ++c) EXPECT_EQ(after.colors[4 * v + c], op.color[c]);
  }
  EXPECT_EQ(after.verts, before.verts);
  EXPECT_EQ(after.uvs, before.uvs);
  EXPECT_EQ(after.tris, before.tris);
  EXPECT_EQ(out.size(), bytes.size());
}

TEST(NifVertexColor, RewritingTheReadColoursIsByteIdentical) {
  const auto bytes = colour_nif(true);
  auto current = bytes;
  for (std::uint32_t v = 0; v < 3; ++v) {
    const auto colours = shape_of(current).colors;
    auto op = op_for(VertexColorKind::fill);
    op.vertices = {v};
    op.color = {colours[4 * v], colours[4 * v + 1], colours[4 * v + 2], colours[4 * v + 3]};
    current = apply_vertex_color(read_nif(current), op);
  }
  EXPECT_EQ(current, bytes);
}

TEST(NifVertexColor, ChannelWritesOnlyTheChosenChannelOfTheChosenVertices) {
  const auto bytes = colour_nif(true);
  const auto nif = read_nif(bytes);
  const auto before = read_shapes(nif).at(0).colors;
  auto op = op_for(VertexColorKind::channel);
  op.vertices = {2};
  op.channel = ColorChannel::alpha;
  op.value = 0.0f;
  const auto after = shape_of(apply_vertex_color(nif, op)).colors;
  auto expected = before;
  expected[11] = 0.0f;
  EXPECT_EQ(after, expected);
}

TEST(NifVertexColor, GradientRampsAlongTheAxisAcrossEveryVertex) {
  const auto nif = read_nif(colour_nif(true));
  auto op = op_for(VertexColorKind::gradient);
  op.axis = GradientAxis::y;
  op.color = {0.0f, 0.0f, 0.0f, 1.0f};
  op.end_color = {1.0f, 1.0f, 1.0f, 1.0f};
  const auto after = shape_of(apply_vertex_color(nif, op)).colors;
  const std::vector<float> expected{0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f};
  EXPECT_EQ(after, expected);
}

TEST(NifVertexColor, GradientBoundsComeFromTheWholeShapeNotTheTargetedVertices) {
  const auto nif = read_nif(colour_nif(true));
  const auto before = read_shapes(nif).at(0).colors;
  auto op = op_for(VertexColorKind::gradient);
  op.axis = GradientAxis::y;
  op.vertices = {2};
  op.color = {0.0f, 0.0f, 0.0f, 1.0f};
  op.end_color = {1.0f, 1.0f, 1.0f, 1.0f};
  const auto after = shape_of(apply_vertex_color(nif, op)).colors;
  auto expected = before;
  for (std::size_t c = 0; c < 4; ++c) expected[8 + c] = 1.0f;
  EXPECT_EQ(after, expected);
}

TEST(NifVertexColor, EnableAddsWhiteColoursAndKeepsEveryOtherVertexByte) {
  const auto bytes = colour_nif(false);
  const auto nif = read_nif(bytes);
  const auto before = read_shapes(nif).at(0);
  const auto out = apply_vertex_color(nif, op_for(VertexColorKind::enable));
  const auto out_nif = read_nif(out);
  const auto after = read_shapes(out_nif).at(0);
  EXPECT_EQ(after.colors, std::vector<float>(12, 1.0f));
  EXPECT_EQ(after.verts, before.verts);
  EXPECT_EQ(after.uvs, before.uvs);
  EXPECT_EQ(after.tris, before.tris);
  EXPECT_EQ(out.size(), bytes.size() + 12);
  EXPECT_EQ(out_nif.blocks()[k_shape].size, nif.blocks()[k_shape].size + 12);
}

TEST(NifVertexColor, EnableKeepsSegmentDataAfterTheTriangles) {
  auto shape = nif_fixture::shape_block("BSSubIndexTriShape", -1, Transform{}, k_full_uv, 20, -1, -1, -1,
                                        vertices(false), {0, 1, 2});
  shape.data.insert(shape.data.end(), {std::byte{0xDE}, std::byte{0xAD}, std::byte{0xBE}, std::byte{0xEF}});
  const auto nif = read_nif(nif_with_root({shape}));
  const auto out = apply_vertex_color(nif, op_for(VertexColorKind::enable));
  const auto out_nif = read_nif(out);
  const auto& info = out_nif.blocks()[k_shape];
  const auto shape_bytes = std::span<const std::byte>(out).subspan(info.offset, info.size);
  ASSERT_GE(shape_bytes.size(), 4U);
  EXPECT_EQ(shape_bytes[shape_bytes.size() - 4], std::byte{0xDE});
  EXPECT_EQ(shape_bytes[shape_bytes.size() - 3], std::byte{0xAD});
  EXPECT_EQ(shape_bytes[shape_bytes.size() - 2], std::byte{0xBE});
  EXPECT_EQ(shape_bytes[shape_bytes.size() - 1], std::byte{0xEF});
}

TEST(NifVertexColor, RefusesInvalidOperations) {
  const auto plain = read_nif(colour_nif(false));
  const auto coloured = read_nif(colour_nif(true));

  auto fill = op_for(VertexColorKind::fill);
  fill.color = {1.0f, 1.0f, 1.0f, 1.0f};
  expect_rejected(plain, fill);

  expect_rejected(coloured, op_for(VertexColorKind::enable));

  auto enable_listed = op_for(VertexColorKind::enable);
  enable_listed.vertices = {0};
  expect_rejected(plain, enable_listed);

  auto far = fill;
  far.vertices = {3};
  expect_rejected(coloured, far);

  auto bright = fill;
  bright.color = {1.5f, 1.0f, 1.0f, 1.0f};
  expect_rejected(coloured, bright);

  auto not_finite = fill;
  not_finite.color = {std::numeric_limits<float>::quiet_NaN(), 1.0f, 1.0f, 1.0f};
  expect_rejected(coloured, not_finite);

  auto channel = op_for(VertexColorKind::channel);
  channel.value = 2.0f;
  expect_rejected(coloured, channel);

  auto root = fill;
  root.block = k_root;
  expect_rejected(coloured, root);

  const auto dynamic = read_nif(nif_with_root({nif_fixture::shape_block(
      "BSDynamicTriShape", -1, Transform{}, k_full_uv, 20, -1, -1, -1, vertices(false), {0, 1, 2})}));
  expect_rejected(dynamic, op_for(VertexColorKind::enable));

  const auto empty = read_nif(nif_with_root({nif_fixture::shape_block("BSTriShape", -1, Transform{}, k_full_uv, 20,
                                                                      -1, -1, -1, {}, {})}));
  expect_rejected(empty, op_for(VertexColorKind::enable));
}

TEST(NifVertexColor, EnableRewritesEveryOffsetNibbleAfterTheInsertedColour) {
  const auto bytes = nif_with_root({nif_fixture::shape_block("BSTriShape", -1, Transform{}, 0x15B, 36, -1, -1, -1,
                                                             half_skinned_vertices(), {0, 1, 2}, 0, k_half_offsets)});
  const auto nif = read_nif(bytes);
  ASSERT_EQ(nif_fixture::descriptor_of(bytes, nif, k_shape), (std::uint64_t{0x15B} << 44) | 9 | k_half_offsets);
  const auto before = read_shapes(nif).at(0);
  const auto out = apply_vertex_color(nif, op_for(VertexColorKind::enable));
  const auto out_nif = read_nif(out);
  EXPECT_EQ(nif_fixture::descriptor_of(out, out_nif, k_shape),
            (std::uint64_t{0x17B} << 44) | 10 | (2ULL << 8) | (3ULL << 16) | (4ULL << 20) | (5ULL << 24) |
                (6ULL << 28) | (9ULL << 36));
  const auto after = read_shapes(out_nif).at(0);
  EXPECT_EQ(after.colors, std::vector<float>(12, 1.0f));
  EXPECT_EQ(after.verts, before.verts);
  EXPECT_EQ(after.uvs, before.uvs);
  EXPECT_EQ(after.normals, before.normals);
  EXPECT_EQ(after.tangents, before.tangents);
  EXPECT_EQ(after.weights, before.weights);
  EXPECT_EQ(after.bone_indices, before.bone_indices);
  EXPECT_EQ(after.tris, before.tris);
}

TEST(NifVertexColor, EnableKeepsOffsetsThatPointBeforeTheInsertedColour) {
  constexpr std::uint64_t offsets = (2ULL << 8) | (3ULL << 16) | (4ULL << 20) | (5ULL << 28) | (2ULL << 36);
  const auto bytes = nif_with_root({nif_fixture::shape_block("BSTriShape", -1, Transform{}, 0x5B, 32, -1, -1, -1,
                                                             half_skinned_vertices(false), {0, 1, 2}, 0, offsets)});
  const auto nif = read_nif(bytes);
  ASSERT_EQ(nif_fixture::descriptor_of(bytes, nif, k_shape), (std::uint64_t{0x5B} << 44) | 8 | offsets);
  const auto out = apply_vertex_color(nif, op_for(VertexColorKind::enable));
  const auto out_nif = read_nif(out);
  EXPECT_EQ(nif_fixture::descriptor_of(out, out_nif, k_shape),
            (std::uint64_t{0x7B} << 44) | 9 | (2ULL << 8) | (3ULL << 16) | (4ULL << 20) | (5ULL << 24) |
                (6ULL << 28) | (2ULL << 36));
  EXPECT_EQ(read_shapes(out_nif).at(0).colors, std::vector<float>(12, 1.0f));
}

TEST(NifVertexColor, EnableMovesLandDataNibbleOnlyWhenAtOrAfterTheColour) {
  const auto check = [](std::uint64_t land, std::uint64_t moved) {
    const std::uint64_t offsets = (2ULL << 8) | (3ULL << 16) | (4ULL << 20) | (5ULL << 28) | (land << 32);
    const auto bytes = nif_with_root({nif_fixture::shape_block("BSTriShape", -1, Transform{}, 0x5B, 32, -1, -1, -1,
                                                               half_skinned_vertices(false), {0, 1, 2}, 0, offsets)});
    const auto nif = read_nif(bytes);
    ASSERT_EQ(nif_fixture::descriptor_of(bytes, nif, k_shape), (std::uint64_t{0x5B} << 44) | 8 | offsets);
    const auto out = apply_vertex_color(nif, op_for(VertexColorKind::enable));
    const auto out_nif = read_nif(out);
    EXPECT_EQ(nif_fixture::descriptor_of(out, out_nif, k_shape),
              (std::uint64_t{0x7B} << 44) | 9 | (2ULL << 8) | (3ULL << 16) | (4ULL << 20) | (5ULL << 24) |
                  (6ULL << 28) | (moved << 32));
  };
  check(2, 2);
  check(5, 6);
}

TEST(NifVertexColor, EnableKeepsNaNUvHalvesBitForBit) {
  Writer w;
  for (std::size_t v = 0; v < 3; ++v) {
    for (std::size_t c = 0; c < 3; ++c) w.f32(k_positions[v][c]);
    w.f32(0.0f);
    w.u16(0x7FFF);
    w.u16(0xFFFF);
  }
  const auto nif = read_nif(nif_with_root({nif_fixture::shape_block("BSTriShape", -1, Transform{}, k_full_uv, 20, -1,
                                                                    -1, -1, w.bytes(), {0, 1, 2})}));
  const auto before = read_shapes(nif).at(0);
  const auto after = shape_of(apply_vertex_color(nif, op_for(VertexColorKind::enable)));
  ASSERT_EQ(after.uvs.size(), 6U);
  EXPECT_TRUE(nif_fixture::same_floats(after.uvs, before.uvs));
  EXPECT_TRUE(nif_fixture::same_floats(after.verts, before.verts));
}

TEST(NifVertexColor, RefusesAStrideThatDoesNotFillItsAttributes) {
  const auto plain = read_nif(nif_with_root({nif_fixture::shape_block("BSTriShape", -1, Transform{}, k_full_uv, 8,
                                                                      -1, -1, -1, std::vector<std::byte>(24), {0, 1, 2})}));
  expect_rejected(plain, op_for(VertexColorKind::enable));

  const auto coloured = read_nif(nif_with_root({nif_fixture::shape_block(
      "BSTriShape", -1, Transform{}, k_full_uv_colour, 8, -1, -1, -1, std::vector<std::byte>(24), {0, 1, 2})}));
  auto fill = op_for(VertexColorKind::fill);
  fill.color = {1.0f, 1.0f, 1.0f, 1.0f};
  expect_rejected(coloured, fill);
}

TEST(NifVertexColor, GradientReadsOnlyTheTargetShape) {
  const auto bytes = nif_with_root({
      nif_fixture::shape_block("BSTriShape", -1, Transform{}, k_full_uv_colour, 24, -1, -1, -1, vertices(true), {0, 1, 2}),
      nif_fixture::shape_block("BSTriShape", -1, Transform{}, k_full_uv_colour, 8, -1, -1, -1,
                               std::vector<std::byte>(24), {0, 1, 2}),
  });
  auto op = op_for(VertexColorKind::gradient);
  op.axis = GradientAxis::y;
  op.color = {0.0f, 0.0f, 0.0f, 1.0f};
  op.end_color = {1.0f, 1.0f, 1.0f, 1.0f};
  EXPECT_EQ(apply_vertex_color(read_nif(bytes), op).size(), bytes.size());
}

TEST(NifVertexColor, GradientRefusesNonFinitePositions) {
  const auto nif = read_nif(nif_with_root({nif_fixture::shape_block(
      "BSTriShape", -1, Transform{}, k_full_uv_colour, 24, -1, -1, -1,
      vertices(true, std::numeric_limits<float>::quiet_NaN()), {0, 1, 2})}));
  auto op = op_for(VertexColorKind::gradient);
  op.axis = GradientAxis::y;
  op.color = {0.0f, 0.0f, 0.0f, 1.0f};
  op.end_color = {1.0f, 1.0f, 1.0f, 1.0f};
  expect_rejected(nif, op);
}
