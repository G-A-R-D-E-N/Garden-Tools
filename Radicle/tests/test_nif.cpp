#include "fo4/error.hpp"
#include "fo4/nif/nif.hpp"
#include "nif_build.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

using fo4::nif::Shape;
using fo4::nif::Transform;
using namespace nif_build;

TEST(NifGeometry, ReadsEveryShapeTypeInBlockOrder) {
  const std::vector<std::string> types{"BSTriShape", "BSSubIndexTriShape", "BSMeshLODTriShape", "BSDynamicTriShape"};
  std::vector<Block> blocks;
  for (const auto& type : types) blocks.push_back(triangle_block(type, Transform{}));
  const auto nif = fo4::nif::read_nif(build_nif({"shape"}, blocks, {}));
  const auto shapes = fo4::nif::read_shapes(nif);
  ASSERT_EQ(shapes.size(), types.size());
  for (std::size_t i = 0; i < shapes.size(); ++i) {
    EXPECT_EQ(nif.blocks()[i].type, types[i]);
    EXPECT_EQ(shapes[i].block, i);
    EXPECT_EQ(shapes[i].name, "shape");
    expect_floats(shapes[i].verts, {0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f});
    EXPECT_EQ(shapes[i].tris, (std::vector<std::uint16_t>{0, 1, 2}));
    EXPECT_EQ(shapes[i].skin_ref, -1);
    EXPECT_EQ(shapes[i].shader_ref, -1);
    EXPECT_EQ(shapes[i].alpha_ref, -1);
  }
}

TEST(NifGeometry, DecodesEveryVertexAttributeCombinationAtBothPrecisions) {
  for (const bool full : {false, true}) {
    for (std::uint32_t mask = 0; mask < 64; ++mask) {
      const std::uint32_t flags = (full ? k_full : 0U) | (mask & 1U ? k_has_uv : 0U) |
                                  (mask & 2U ? k_has_uv2 : 0U) | (mask & 4U ? k_has_normal : 0U) |
                                  (mask & 8U ? k_has_tangent : 0U) | (mask & 16U ? k_has_colour : 0U) |
                                  (mask & 32U ? k_has_skin : 0U);
      SCOPED_TRACE(std::string(full ? "full" : "half") + " mask " + std::to_string(mask));
      const auto first = encode_vertex(full, flags, k_verts[0]);
      auto vertices = first;
      const auto second = encode_vertex(full, flags, k_verts[1]);
      vertices.insert(vertices.end(), second.begin(), second.end());
      const auto nif = fo4::nif::read_nif(
          build_nif({"shape"}, {shape_block("BSTriShape", 0, Transform{}, flags, first.size(), vertices, {0, 1, 1})},
                    {0}));
      const auto shapes = fo4::nif::read_shapes(nif);
      ASSERT_EQ(shapes.size(), 1U);
      const auto& shape = shapes[0];
      const float mid = 128.0f / 255.0f * 2.0f - 1.0f;
      expect_floats(shape.verts, {1.0f, 2.0f, 3.0f, 4.0f, 5.0f, 6.0f});
      expect_floats(shape.uvs, (mask & 1U) ? std::vector<float>{0.5f, 0.25f, 0.5f, 0.25f} : std::vector<float>{});
      expect_floats(shape.normals,
                    (mask & 4U) ? std::vector<float>{1.0f, -1.0f, mid, 1.0f, -1.0f, mid} : std::vector<float>{});
      expect_floats(shape.tangents,
                    (mask & 8U) ? std::vector<float>{-1.0f, 1.0f, mid, -1.0f, 1.0f, mid} : std::vector<float>{});
      expect_floats(shape.colors,
                    (mask & 16U) ? std::vector<float>{1.0f, 0.0f, 0.2f, 0.4f, 1.0f, 0.0f, 0.2f, 0.4f}
                                 : std::vector<float>{});
      const float bitangent_y = 0x11 / 255.0f * 2.0f - 1.0f;
      const float bitangent_z = 0x22 / 255.0f * 2.0f - 1.0f;
      expect_floats(shape.bitangents,
                    (mask & 4U) != 0 && (mask & 8U) != 0
                        ? std::vector<float>{0.0f, bitangent_y, bitangent_z, 0.0f, bitangent_y, bitangent_z}
                        : std::vector<float>{});
      expect_floats(shape.weights, (mask & 32U) != 0
                                       ? std::vector<float>{1.0f, 0.5f, 0.25f, 0.0f, 1.0f, 0.5f, 0.25f, 0.0f}
                                       : std::vector<float>{});
      const std::vector<std::uint8_t> expected_bone_indices =
          (mask & 32U) != 0 ? std::vector<std::uint8_t>{1, 2, 3, 4, 1, 2, 3, 4}
                            : std::vector<std::uint8_t>{};
      EXPECT_EQ(shape.bone_indices, expected_bone_indices);
      EXPECT_EQ(shape.tris, (std::vector<std::uint16_t>{0, 1, 1}));
    }
  }
}

TEST(NifGeometry, DecodesFullPrecisionSixteenByteSlotBeforeUv) {
  const std::uint32_t flags = k_full | k_has_uv | k_has_normal | k_has_tangent;
  const auto first = encode_vertex(true, flags, k_verts[0]);
  auto vertices = first;
  const auto second = encode_vertex(true, flags, k_verts[1]);
  vertices.insert(vertices.end(), second.begin(), second.end());
  const auto nif = fo4::nif::read_nif(
      build_nif({"shape"}, {shape_block("BSTriShape", 0, Transform{}, flags, 28, vertices, {0, 1, 1})}, {0}));
  const auto shapes = fo4::nif::read_shapes(nif);
  ASSERT_EQ(shapes.size(), 1U);
  const auto& shape = shapes[0];
  const float mid = 128.0f / 255.0f * 2.0f - 1.0f;
  expect_floats(shape.verts, {1.0f, 2.0f, 3.0f, 4.0f, 5.0f, 6.0f});
  expect_floats(shape.uvs, {0.5f, 0.25f, 0.5f, 0.25f});
  expect_floats(shape.normals, {1.0f, -1.0f, mid, 1.0f, -1.0f, mid});
}

TEST(NifGeometry, DecodesTailSlotAfterSkinBlock) {
  const std::uint32_t flags =
      k_has_uv | k_has_normal | k_has_tangent | k_has_colour | k_has_skin | k_has_tail_slot;
  const auto first = encode_vertex(false, flags, k_verts[0]);
  auto vertices = first;
  const auto second = encode_vertex(false, flags, k_verts[1]);
  vertices.insert(vertices.end(), second.begin(), second.end());
  const auto nif = fo4::nif::read_nif(
      build_nif({"shape"}, {shape_block("BSTriShape", 0, Transform{}, flags, 40, vertices, {0, 1, 1})}, {0}));
  const auto shapes = fo4::nif::read_shapes(nif);
  ASSERT_EQ(shapes.size(), 1U);
  const auto& shape = shapes[0];
  const float mid = 128.0f / 255.0f * 2.0f - 1.0f;
  expect_floats(shape.verts, {1.0f, 2.0f, 3.0f, 4.0f, 5.0f, 6.0f});
  expect_floats(shape.uvs, {0.5f, 0.25f, 0.5f, 0.25f});
  expect_floats(shape.normals, {1.0f, -1.0f, mid, 1.0f, -1.0f, mid});
  EXPECT_EQ(shape.tris, (std::vector<std::uint16_t>{0, 1, 1}));
}

TEST(NifGeometry, DecodesBinary16SpecialValues) {
  const std::vector<std::uint16_t> bits{0x0000, 0x8000, 0x0001, 0x03FF, 0x0400,
                                        0x3C00, 0xC000, 0x7C00, 0xFC00, 0x7E00};
  std::vector<std::byte> vertices;
  for (const auto value : bits) {
    Writer w;
    w.u16(value);
    w.u16(0);
    w.u16(0);
    w.u16(0);
    vertices.insert(vertices.end(), w.bytes().begin(), w.bytes().end());
  }
  const auto nif = fo4::nif::read_nif(build_nif({"s"}, {shape_block("BSTriShape", 0, Transform{}, 0, 8, vertices, {})}, {}));
  const auto shapes = fo4::nif::read_shapes(nif);
  ASSERT_EQ(shapes.size(), 1U);
  const auto& verts = shapes[0].verts;
  ASSERT_EQ(verts.size(), bits.size() * 3);
  EXPECT_EQ(verts[0], 0.0f);
  EXPECT_FALSE(std::signbit(verts[0]));
  EXPECT_EQ(verts[3], 0.0f);
  EXPECT_TRUE(std::signbit(verts[3]));
  EXPECT_FLOAT_EQ(verts[6], std::ldexp(1.0f, -24));
  EXPECT_FLOAT_EQ(verts[9], std::ldexp(1023.0f, -24));
  EXPECT_FLOAT_EQ(verts[12], std::ldexp(1.0f, -14));
  EXPECT_EQ(verts[15], 1.0f);
  EXPECT_EQ(verts[18], -2.0f);
  EXPECT_TRUE(std::isinf(verts[21]));
  EXPECT_GT(verts[21], 0.0f);
  EXPECT_TRUE(std::isinf(verts[24]));
  EXPECT_LT(verts[24], 0.0f);
  EXPECT_TRUE(std::isnan(verts[27]));
}

TEST(NifGeometry, ZeroDataSizeYieldsEmptyShape) {
  const auto nif = fo4::nif::read_nif(build_nif({"s"}, {shape_block("BSTriShape", 0, Transform{}, 0, 12, {}, {})}, {}));
  const auto shapes = fo4::nif::read_shapes(nif);
  ASSERT_EQ(shapes.size(), 1U);
  EXPECT_TRUE(shapes[0].verts.empty());
  EXPECT_TRUE(shapes[0].tris.empty());
  EXPECT_EQ(shapes[0].block, 0U);
}

TEST(NifGeometry, RejectsDescriptorStrideThatDoesNotMatchDataSize) {
  const auto nif = fo4::nif::read_nif(
      build_nif({"s"}, {shape_block("BSTriShape", 0, Transform{}, k_full, 8, std::vector<std::byte>(36), {})}, {}));
  expect_parse_error([&] { fo4::nif::read_shapes(nif); }, "vertex data size does not match the vertex descriptor");
}

TEST(NifGeometry, RejectsStrideThatDoesNotMatchAttributes) {
  const auto nif = fo4::nif::read_nif(
      build_nif({"s"}, {shape_block("BSTriShape", 0, Transform{}, 0, 12, std::vector<std::byte>(24), {})}, {}));
  expect_parse_error([&] { fo4::nif::read_shapes(nif); },
                     "vertex attributes do not fill the vertex stride exactly");
}

TEST(NifGeometry, RejectsFullPrecisionPositionWithTwelveByteSlot) {
  const auto nif = fo4::nif::read_nif(
      build_nif({"s"}, {shape_block("BSTriShape", 0, Transform{}, k_full, 12, std::vector<std::byte>(36), {})}, {}));
  expect_parse_error([&] { fo4::nif::read_shapes(nif); },
                     "vertex attributes do not fill the vertex stride exactly");
}

TEST(NifGeometry, RejectsTailSlotStrideWithoutItsFourBytes) {
  const std::uint32_t flags =
      k_has_uv | k_has_normal | k_has_tangent | k_has_colour | k_has_skin | k_has_tail_slot;
  const auto nif = fo4::nif::read_nif(
      build_nif({"s"}, {shape_block("BSTriShape", 0, Transform{}, flags, 36, std::vector<std::byte>(72), {})}, {}));
  expect_parse_error([&] { fo4::nif::read_shapes(nif); },
                     "vertex attributes do not fill the vertex stride exactly");
}

TEST(NifGeometry, RejectsTriangleIndexOutOfRange) {
  const auto nif = fo4::nif::read_nif(
      build_nif({"s"}, {shape_block("BSTriShape", 0, Transform{}, k_full, 16, triangle_vertices(), {0, 1, 3})}, {}));
  expect_parse_error([&] { fo4::nif::read_shapes(nif); }, "triangle index is out of range");
}

TEST(NifGeometry, RejectsNameIndexOutOfRange) {
  const auto nif = fo4::nif::read_nif(
      build_nif({"s"}, {shape_block("BSTriShape", 5, Transform{}, k_full, 16, triangle_vertices(), {0, 1, 2})}, {}));
  expect_parse_error([&] { fo4::nif::read_shapes(nif); }, "string index is out of range");
}

TEST(NifNodes, ComposesNestedRotatedScaledChainWithoutRoot) {
  const auto nif = fo4::nif::read_nif(chain_file());
  EXPECT_EQ(nif.roots(), (std::vector<std::uint32_t>{0}));
  EXPECT_EQ(nif.children(0), (std::vector<std::uint32_t>{1}));
  EXPECT_EQ(nif.parent(3), 2);
  EXPECT_EQ(nif.parent(0), -1);

  const auto world = fo4::nif::shape_to_global(nif, 3);
  EXPECT_NEAR(world.t[0], -7.0f, 1e-5);
  EXPECT_NEAR(world.t[1], -28.0f, 1e-5);
  EXPECT_NEAR(world.t[2], 27.0f, 1e-5);
  EXPECT_NEAR(world.s, 2.0f, 1e-5);
  const float expected_r[9] = {0.0f, -1.0f, 0.0f, 0.0f, 0.0f, -1.0f, 1.0f, 0.0f, 0.0f};
  for (std::size_t i = 0; i < 9; ++i) EXPECT_NEAR(world.r[i], expected_r[i], 1e-5) << "r" << i;
}

TEST(NifNodes, BlockTransformReadsTheShapesOwnLocalTransform) {
  const auto nif = fo4::nif::read_nif(chain_file());
  const auto local = fo4::nif::block_transform(nif, 3);
  EXPECT_NEAR(local.t[0], 7.0f, 1e-5);
  EXPECT_NEAR(local.t[1], 8.0f, 1e-5);
  EXPECT_NEAR(local.t[2], 9.0f, 1e-5);
  EXPECT_NEAR(local.s, 1.0f, 1e-5);
}

TEST(NifNodes, BillboardParentReportsMode) {
  const auto nif = fo4::nif::read_nif(build_nif(
      {"node"},
      {node_block("BSMultiBoundNode", Transform{}, {1, 3}, root_trailer()),
       node_block("NiBillboardNode", Transform{}, {2}, mode_trailer(2)),
       triangle_block("BSTriShape", Transform{}), triangle_block("BSTriShape", Transform{})},
      {0}));
  EXPECT_EQ(fo4::nif::billboard_mode(nif, 2), 2);
  EXPECT_EQ(fo4::nif::billboard_mode(nif, 3), -1);
  EXPECT_EQ(fo4::nif::billboard_mode(nif, 1), -1);
  EXPECT_EQ(fo4::nif::billboard_mode(nif, 0), -1);
}

TEST(NifNodes, ParentCycleThrowsParseError) {
  const auto bytes = build_nif(
      {"node"},
      {node_block("BSMultiBoundNode", Transform{}, {}, root_trailer()), node_block("NiNode", Transform{}, {2}),
       node_block("NiNode", Transform{}, {1, 3}), triangle_block("BSTriShape", Transform{})},
      {0});
  expect_parse_error([&] { fo4::nif::read_nif(bytes); }, "node parent chain contains a cycle");
}

TEST(NifFile, EveryTruncatedPrefixThrowsParseError) {
  const auto file = chain_file();
  for (std::size_t length = 0; length < file.size(); ++length) {
    EXPECT_THROW(fo4::nif::read_nif(std::span<const std::byte>(file.data(), length)), fo4::parse_error)
        << "length " << length;
  }
}

TEST(NifFile, RejectsBlockContentThatOverrunsItsDeclaredSize) {
  Writer w;
  w.i32(0);
  w.u32(0);
  w.i32(-1);
  w.u32(0);
  w.transform(Transform{});
  w.i32(-1);
  w.u32(5);
  const Block block{"NiNode", w.bytes()};
  expect_parse_error([&] { fo4::nif::read_nif(build_nif({"node"}, {block}, {0})); },
                     "child list runs past the end of its block");
}

TEST(NifFile, RejectsBsVersionOtherThan130) {
  const auto file = build_nif({"node"}, {node_block("NiNode", Transform{}, {})}, {0}, 155);
  expect_parse_error([&] { fo4::nif::read_nif(file); }, "only BS version 130 is read");
}

TEST(NifFile, RejectsBlockSizeOverstatementLeavingTrailingFooterBytes) {
  const auto file = build_nif({"node"}, {node_block("NiNode", Transform{}, {})}, {0}, 130, 1);
  expect_parse_error([&] { fo4::nif::read_nif(file); }, "trailing bytes after the root table");
}

TEST(NifFile, RejectsBlockSizesRunningPastFileEnd) {
  const auto file = build_nif({"node"}, {node_block("NiNode", Transform{}, {})}, {0}, 130, 1000);
  expect_parse_error([&] { fo4::nif::read_nif(file); }, "block extends past the end of the file");
}

TEST(NifFile, RejectsTrailingBytesAfterFooter) {
  auto file = chain_file();
  file.push_back(std::byte{0});
  expect_parse_error([&] { fo4::nif::read_nif(file); }, "trailing bytes after the root table");
}

TEST(NifFile, RejectsBlockCountOverLimit) {
  Writer w;
  w.text("Gamebryo File Format, Version 20.2.0.7\n");
  w.u32(0x14020007U);
  w.u8(1);
  w.u32(12);
  w.u32(1'000'001U);
  w.u32(130);
  expect_parse_error([&] { fo4::nif::read_nif(w.bytes()); }, "block count exceeds 1,000,000");
}

TEST(NifNodes, RejectsNodeNameIndexOutOfRange) {
  Writer w;
  w.i32(7);
  w.u32(0);
  w.i32(-1);
  w.u32(0);
  w.transform(Transform{});
  w.i32(-1);
  w.u32(0);
  const Block block{"NiNode", w.bytes()};
  expect_parse_error([&] { fo4::nif::read_nif(build_nif({"node"}, {block}, {})); }, "string index is out of range");
}

TEST(NifGeometry, RejectsShapeTriangleDataTruncatedInsideItsBlock) {
  Block block = triangle_block("BSTriShape", Transform{});
  block.data.resize(block.data.size() - 6);
  const auto nif = fo4::nif::read_nif(build_nif({"s"}, {block}, {}));
  expect_parse_error([&] { fo4::nif::read_shapes(nif); }, "unexpected end of data");
}

TEST(NifGeometry, RejectsTriangleCountPastRemainingBytes) {
  Block block = triangle_block("BSTriShape", Transform{});
  block.data[108] = std::byte{100};
  const auto nif = fo4::nif::read_nif(build_nif({"s"}, {block}, {}));
  expect_parse_error([&] { fo4::nif::read_shapes(nif); }, "triangle count runs past the end of its block");
}
