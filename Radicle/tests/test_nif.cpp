#include "fo4/error.hpp"
#include "fo4/nif/nif.hpp"

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

namespace {

using fo4::nif::Shape;
using fo4::nif::Transform;

constexpr std::uint32_t k_has_uv = 1U << 1;
constexpr std::uint32_t k_has_uv2 = 1U << 2;
constexpr std::uint32_t k_has_normal = 1U << 3;
constexpr std::uint32_t k_has_tangent = 1U << 4;
constexpr std::uint32_t k_has_colour = 1U << 5;
constexpr std::uint32_t k_has_skin = 1U << 6;
constexpr std::uint32_t k_has_tail_slot = 1U << 8;
constexpr std::uint32_t k_full = 1U << 10;

class Writer {
public:
  void u8(std::uint8_t value) { bytes_.push_back(std::byte{value}); }
  void u16(std::uint16_t value) {
    u8(static_cast<std::uint8_t>(value));
    u8(static_cast<std::uint8_t>(value >> 8U));
  }
  void u32(std::uint32_t value) {
    u16(static_cast<std::uint16_t>(value));
    u16(static_cast<std::uint16_t>(value >> 16U));
  }
  void u64(std::uint64_t value) {
    u32(static_cast<std::uint32_t>(value));
    u32(static_cast<std::uint32_t>(value >> 32U));
  }
  void i32(std::int32_t value) { u32(static_cast<std::uint32_t>(value)); }
  void f32(float value) { u32(std::bit_cast<std::uint32_t>(value)); }
  void text(std::string_view value) {
    for (const char ch : value) u8(static_cast<std::uint8_t>(ch));
  }
  void sized(std::string_view value) {
    u32(static_cast<std::uint32_t>(value.size()));
    text(value);
  }
  void short_string(std::string_view value) {
    u8(static_cast<std::uint8_t>(value.size()));
    text(value);
  }
  void append(const std::vector<std::byte>& bytes) { bytes_.insert(bytes_.end(), bytes.begin(), bytes.end()); }
  void transform(const Transform& xf) {
    for (const float value : xf.t) f32(value);
    for (const float value : xf.r) f32(value);
    f32(xf.s);
  }
  const std::vector<std::byte>& bytes() const { return bytes_; }

private:
  std::vector<std::byte> bytes_;
};

struct Block {
  std::string type;
  std::vector<std::byte> data;
};

std::vector<std::byte> build_nif(const std::vector<std::string>& strings, const std::vector<Block>& blocks,
                                 const std::vector<std::int32_t>& roots, std::uint32_t bs_version = 130,
                                 std::uint32_t size_bias = 0) {
  std::vector<std::string> types;
  std::vector<std::uint16_t> indices;
  for (const auto& block : blocks) {
    auto found = std::find(types.begin(), types.end(), block.type);
    if (found == types.end()) {
      types.push_back(block.type);
      found = types.end() - 1;
    }
    indices.push_back(static_cast<std::uint16_t>(found - types.begin()));
  }
  Writer w;
  w.text("Gamebryo File Format, Version 20.2.0.7\n");
  w.u32(0x14020007U);
  w.u8(1);
  w.u32(12);
  w.u32(static_cast<std::uint32_t>(blocks.size()));
  w.u32(bs_version);
  for (int i = 0; i < 4; ++i) w.short_string("");
  w.u16(static_cast<std::uint16_t>(types.size()));
  for (const auto& type : types) w.sized(type);
  for (const auto index : indices) w.u16(index);
  for (std::size_t i = 0; i < blocks.size(); ++i) {
    w.u32(static_cast<std::uint32_t>(blocks[i].data.size()) + (i == 0 ? size_bias : 0U));
  }
  w.u32(static_cast<std::uint32_t>(strings.size()));
  w.u32(0);
  for (const auto& value : strings) w.sized(value);
  w.u32(0);
  for (const auto& block : blocks) w.append(block.data);
  w.u32(static_cast<std::uint32_t>(roots.size()));
  for (const auto root : roots) w.i32(root);
  return w.bytes();
}

std::vector<std::byte> root_trailer() {
  Writer w;
  w.i32(-1);
  w.u32(0);
  return w.bytes();
}

std::vector<std::byte> mode_trailer(std::uint16_t mode) {
  Writer w;
  w.u16(mode);
  return w.bytes();
}

Block node_block(std::string type, const Transform& xf, const std::vector<std::int32_t>& children,
                 const std::vector<std::byte>& trailing = {}) {
  Writer w;
  w.i32(0);
  w.u32(0);
  w.i32(-1);
  w.u32(0);
  w.transform(xf);
  w.i32(-1);
  w.u32(static_cast<std::uint32_t>(children.size()));
  for (const auto child : children) w.i32(child);
  w.append(trailing);
  return Block{std::move(type), w.bytes()};
}

Block shape_block(std::string type, std::int32_t name, const Transform& xf, std::uint32_t flags, std::size_t stride,
                  const std::vector<std::byte>& vertices, const std::vector<std::uint16_t>& tris) {
  Writer w;
  w.i32(name);
  w.u32(0);
  w.i32(-1);
  w.u32(0);
  w.transform(xf);
  w.i32(-1);
  for (int i = 0; i < 4; ++i) w.f32(0.0f);
  w.i32(-1);
  w.i32(-1);
  w.i32(-1);
  w.u64((static_cast<std::uint64_t>(flags) << 44U) | static_cast<std::uint64_t>(stride / 4));
  w.u32(static_cast<std::uint32_t>(tris.size() / 3));
  w.u16(static_cast<std::uint16_t>(vertices.size() / stride));
  w.u32(static_cast<std::uint32_t>(vertices.size() + tris.size() * 2));
  w.append(vertices);
  for (const auto index : tris) w.u16(index);
  return Block{std::move(type), w.bytes()};
}

std::vector<std::byte> triangle_vertices() {
  Writer w;
  const float positions[9] = {0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f};
  for (std::size_t i = 0; i < 9; ++i) {
    w.f32(positions[i]);
    if (i % 3 == 2) w.u32(0);
  }
  return w.bytes();
}

Block triangle_block(std::string type, const Transform& xf) {
  return shape_block(std::move(type), 0, xf, k_full, 16, triangle_vertices(), {0, 1, 2});
}

std::vector<std::byte> chain_file() {
  const Transform root{{100.0f, 0.0f, 0.0f}, {1.0f, 0.0f, 0.0f, 0.0f, 0.0f, -1.0f, 0.0f, 1.0f, 0.0f}, 3.0f};
  const Transform mid{{1.0f, 2.0f, 3.0f}, {1.0f, 0.0f, 0.0f, 0.0f, 0.0f, -1.0f, 0.0f, 1.0f, 0.0f}, 2.0f};
  const Transform leaf{{4.0f, 5.0f, 6.0f}, {0.0f, -1.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f}, 1.0f};
  const Transform shape_xf{{7.0f, 8.0f, 9.0f}, {1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f}, 1.0f};
  return build_nif({"node"},
                   {node_block("BSMultiBoundNode", root, {1}, root_trailer()), node_block("NiNode", mid, {2}),
                    node_block("NiNode", leaf, {3}), triangle_block("BSTriShape", shape_xf)},
                   {0});
}

void expect_floats(const std::vector<float>& actual, const std::vector<float>& expected) {
  ASSERT_EQ(actual.size(), expected.size());
  for (std::size_t i = 0; i < expected.size(); ++i) {
    EXPECT_NEAR(actual[i], expected[i], 1e-5) << "index " << i;
  }
}

template <typename Fn>
void expect_parse_error(Fn&& fn, std::string_view message) {
  try {
    fn();
    ADD_FAILURE() << "expected fo4::parse_error containing \"" << message << "\"";
  } catch (const fo4::parse_error& error) {
    EXPECT_NE(error.parse_message().find(message), std::string::npos) << error.parse_message();
  }
}

struct Vert {
  float pos[3];
  std::uint16_t half[3];
};

constexpr Vert k_verts[2] = {{{1.0f, 2.0f, 3.0f}, {0x3C00, 0x4000, 0x4200}},
                             {{4.0f, 5.0f, 6.0f}, {0x4400, 0x4500, 0x4600}}};

std::vector<std::byte> encode_vertex(bool full, std::uint32_t flags, const Vert& vert) {
  Writer w;
  for (std::size_t i = 0; i < 3; ++i) {
    if (full) {
      w.f32(vert.pos[i]);
    } else {
      w.u16(vert.half[i]);
    }
  }
  if (full) w.u32(0);
  if (!full) w.u16(0);
  if ((flags & k_has_uv) != 0) {
    w.u16(0x3800);
    w.u16(0x3400);
  }
  if ((flags & k_has_uv2) != 0) {
    for (int i = 0; i < 4; ++i) w.u8(0xAB);
  }
  if ((flags & k_has_normal) != 0) {
    w.u8(255);
    w.u8(0);
    w.u8(128);
    w.u8(0x11);
  }
  if ((flags & k_has_tangent) != 0) {
    w.u8(0);
    w.u8(255);
    w.u8(128);
    w.u8(0x22);
  }
  if ((flags & k_has_colour) != 0) {
    w.u8(255);
    w.u8(0);
    w.u8(51);
    w.u8(102);
  }
  if ((flags & k_has_skin) != 0) {
    w.u16(0x3C00);
    w.u16(0x3800);
    w.u16(0x3400);
    w.u16(0);
    for (std::uint8_t index = 1; index <= 4; ++index) w.u8(index);
  }
  if ((flags & k_has_tail_slot) != 0) w.u32(0xEFU);
  return w.bytes();
}

}  // namespace

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
  const auto nif = fo4::nif::read_nif(build_nif(
      {"node"},
      {node_block("BSMultiBoundNode", Transform{}, {}, root_trailer()), node_block("NiNode", Transform{}, {2}),
       node_block("NiNode", Transform{}, {1, 3}), triangle_block("BSTriShape", Transform{})},
      {0}));
  expect_parse_error([&] { fo4::nif::shape_to_global(nif, 3); }, "parent chain contains a cycle");
  expect_parse_error([&] { fo4::nif::billboard_mode(nif, 3); }, "parent chain contains a cycle");
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
