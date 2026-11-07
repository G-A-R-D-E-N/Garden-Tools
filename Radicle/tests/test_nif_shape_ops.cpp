#include "fo4/archive/assets.hpp"
#include "fo4/nif/nif.hpp"
#include "fo4/nif/nif_write.hpp"
#include "fo4/nif/shape_ops.hpp"
#include "nif_build.hpp"
#include "real_data_plugins.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <bit>
#include <cctype>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <limits>
#include <string>
#include <utility>
#include <vector>

namespace {

using fo4::nif::Shape;
using fo4::nif::ShapeOp;
using fo4::nif::ShapeOpReport;
using fo4::nif::ShapeOpRequest;
using fo4::nif::Transform;
using fo4::nif::apply_shape_op;
using fo4::nif::block_transform;
using fo4::nif::read_nif;
using fo4::nif::read_shape;
using fo4::nif::read_shapes;
using fo4::nif::write_nif;
using nif_build::Block;
using nif_build::Writer;

using Vec3 = std::array<float, 3>;

constexpr std::uint32_t k_full = nif_build::k_full;
constexpr std::uint32_t k_uv = nif_build::k_has_uv;
constexpr std::uint32_t k_normal = nif_build::k_has_normal;
constexpr std::uint32_t k_tangent = nif_build::k_has_tangent;
constexpr std::uint32_t k_frame = k_full | k_uv | k_normal | k_tangent;
constexpr std::size_t k_sphere_offset = 72;
constexpr float k_quantum = 0.01f;

struct Vertex {
  Vec3 position;
  std::array<float, 2> uv;
  Vec3 normal;
  Vec3 tangent;
  Vec3 bitangent;
};

std::uint8_t byte_of(float value) { return static_cast<std::uint8_t>(std::lround((value + 1.0f) * 127.5f)); }

std::uint16_t half_of(float value) { return value == 1.0f ? std::uint16_t{0x3C00} : std::uint16_t{0}; }

std::size_t stride_of(std::uint32_t flags) {
  std::size_t stride = (flags & k_full) != 0 ? 16 : 8;
  if ((flags & k_uv) != 0) stride += 4;
  if ((flags & k_normal) != 0) stride += 4;
  if ((flags & k_tangent) != 0) stride += 4;
  return stride;
}

std::vector<std::byte> vertex_bytes(const std::vector<Vertex>& vertices, std::uint32_t flags) {
  Writer w;
  for (const auto& v : vertices) {
    for (const float c : v.position) {
      if ((flags & k_full) != 0) {
        w.f32(c);
      } else {
        w.u16(half_of(c));
      }
    }
    if ((flags & k_full) != 0) {
      w.f32(v.bitangent[0]);
    } else {
      w.u16(half_of(v.bitangent[0]));
    }
    if ((flags & k_uv) != 0) {
      w.u16(half_of(v.uv[0]));
      w.u16(half_of(v.uv[1]));
    }
    if ((flags & k_normal) != 0) {
      for (const float c : v.normal) w.u8(byte_of(c));
      w.u8(byte_of(v.bitangent[1]));
    }
    if ((flags & k_tangent) != 0) {
      for (const float c : v.tangent) w.u8(byte_of(c));
      w.u8(byte_of(v.bitangent[2]));
    }
  }
  return w.bytes();
}

Block tri_shape(std::string type, const Transform& xf, const std::vector<Vertex>& vertices, std::uint32_t flags,
                const std::vector<std::uint16_t>& tris) {
  return nif_build::shape_block(std::move(type), -1, xf, flags, stride_of(flags), vertex_bytes(vertices, flags), tris);
}

std::vector<std::byte> file_with(const std::vector<Block>& shapes) {
  std::vector<std::int32_t> children;
  for (std::size_t i = 0; i < shapes.size(); ++i) children.push_back(static_cast<std::int32_t>(i + 1));
  std::vector<Block> blocks{nif_build::node_block("NiNode", Transform{}, children)};
  blocks.insert(blocks.end(), shapes.begin(), shapes.end());
  return nif_build::build_nif({"node"}, blocks, {0});
}

std::vector<Vertex> quad_vertices() {
  const Vec3 normal{0.0f, 0.0f, -1.0f};
  const Vec3 tangent{0.0f, 1.0f, 0.0f};
  const Vec3 bitangent{1.0f, 0.0f, 0.0f};
  return {Vertex{{0.0f, 0.0f, 0.0f}, {0.0f, 0.0f}, normal, tangent, bitangent},
          Vertex{{1.0f, 0.0f, 0.0f}, {1.0f, 0.0f}, normal, tangent, bitangent},
          Vertex{{1.0f, 1.0f, 0.0f}, {1.0f, 1.0f}, normal, tangent, bitangent},
          Vertex{{0.0f, 1.0f, 0.0f}, {0.0f, 1.0f}, normal, tangent, bitangent}};
}

std::vector<Vertex> fold_vertices() {
  const Vec3 normal{0.0f, 0.0f, 1.0f};
  const Vec3 tangent{0.0f, 1.0f, 0.0f};
  const Vec3 bitangent{1.0f, 0.0f, 0.0f};
  return {Vertex{{0.0f, 0.0f, 0.0f}, {0.0f, 0.0f}, normal, tangent, bitangent},
          Vertex{{1.0f, 0.0f, 0.0f}, {1.0f, 0.0f}, normal, tangent, bitangent},
          Vertex{{0.0f, 1.0f, 0.0f}, {0.0f, 1.0f}, normal, tangent, bitangent},
          Vertex{{0.0f, 1.0f, 0.0f}, {0.0f, 0.0f}, normal, tangent, bitangent},
          Vertex{{1.0f, 0.0f, 0.0f}, {1.0f, 0.0f}, normal, tangent, bitangent},
          Vertex{{1.0f, 0.0f, 1.0f}, {1.0f, 1.0f}, normal, tangent, bitangent}};
}

std::vector<Vertex> rectangle_vertices() {
  auto vertices = quad_vertices();
  vertices[1].position = {2.0f, 0.0f, 0.0f};
  vertices[2].position = {2.0f, 4.0f, 0.0f};
  vertices[3].position = {0.0f, 4.0f, 0.0f};
  return vertices;
}

std::vector<std::byte> run(const std::vector<std::byte>& bytes, ShapeOp op, std::vector<std::uint32_t> blocks = {},
                           float crease = 180.0f, ShapeOpReport* report = nullptr) {
  ShapeOpRequest request;
  request.op = op;
  request.blocks = std::move(blocks);
  request.crease_degrees = crease;
  return apply_shape_op(read_nif(bytes), request, report);
}

Shape shape_at(const std::vector<std::byte>& bytes, std::uint32_t block) { return read_shape(read_nif(bytes), block); }

float float_at(const std::vector<std::byte>& bytes, std::size_t offset) {
  std::uint32_t bits = 0;
  for (std::size_t i = 0; i < 4; ++i) {
    bits |= static_cast<std::uint32_t>(std::to_integer<std::uint8_t>(bytes[offset + i])) << (8 * i);
  }
  return std::bit_cast<float>(bits);
}

std::array<float, 4> sphere_of(const std::vector<std::byte>& bytes, std::uint32_t block) {
  const auto at = read_nif(bytes).blocks()[block].offset + k_sphere_offset;
  return {float_at(bytes, at), float_at(bytes, at + 4), float_at(bytes, at + 8), float_at(bytes, at + 12)};
}

void expect_vector(const std::vector<float>& flat, std::size_t vertex, const Vec3& expected, float tolerance) {
  ASSERT_GE(flat.size(), 3 * (vertex + 1));
  for (std::size_t k = 0; k < 3; ++k) {
    EXPECT_NEAR(flat[3 * vertex + k], expected[k], tolerance) << "vertex " << vertex << " axis " << k;
  }
}

bool is_changed(const ShapeOpReport& report, std::uint32_t block) {
  return std::find(report.changed.begin(), report.changed.end(), block) != report.changed.end();
}

bool is_nif(const std::string& path) {
  auto extension = std::filesystem::path(path).extension().string();
  std::transform(extension.begin(), extension.end(), extension.begin(),
                 [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
  return extension == ".nif";
}

struct Agreement {
  double sum{0.0};
  std::size_t count{0};
};

void measure(const std::vector<float>& fresh, const std::vector<float>& authored, bool absolute, Agreement& total) {
  const auto size = std::min(fresh.size(), authored.size());
  for (std::size_t i = 0; i + 2 < size; i += 3) {
    double dot = 0.0;
    double fresh_length = 0.0;
    double authored_length = 0.0;
    for (std::size_t k = 0; k < 3; ++k) {
      dot += static_cast<double>(fresh[i + k]) * authored[i + k];
      fresh_length += static_cast<double>(fresh[i + k]) * fresh[i + k];
      authored_length += static_cast<double>(authored[i + k]) * authored[i + k];
    }
    if (fresh_length <= 0.0 || authored_length <= 0.0) continue;
    const auto cosine = dot / std::sqrt(fresh_length * authored_length);
    total.sum += absolute ? std::abs(cosine) : cosine;
    ++total.count;
  }
}

constexpr std::uint32_t k_skin = nif_build::k_has_skin;
constexpr std::uint32_t k_colour = nif_build::k_has_colour;
constexpr std::size_t k_skinned_stride = 28;
constexpr std::size_t k_coloured_stride = 20;

std::vector<Vertex> split_quad_vertices() {
  const auto quad = quad_vertices();
  return {quad[0], quad[1], quad[2], quad[0], quad[2], quad[3]};
}

std::vector<std::byte> skinned_vertex(const Vec3& position, std::uint16_t first_weight, std::uint8_t bone) {
  Writer w;
  for (const float c : position) w.f32(c);
  w.f32(0.0f);
  w.u16(first_weight);
  w.u16(0);
  w.u16(0);
  w.u16(0);
  w.u8(bone);
  w.u8(0);
  w.u8(0);
  w.u8(0);
  return w.bytes();
}

Block skinned_shape(const std::vector<std::byte>& vertices, const std::vector<std::uint16_t>& tris) {
  return nif_build::shape_block("BSTriShape", -1, Transform{}, k_full | k_skin, k_skinned_stride, vertices, tris);
}

std::vector<std::byte> coloured_vertex(const Vec3& position) {
  Writer w;
  for (const float c : position) w.f32(c);
  w.f32(0.0f);
  w.u8(0);
  w.u8(51);
  w.u8(102);
  w.u8(255);
  return w.bytes();
}

Block coloured_shape(const std::vector<std::byte>& vertices, const std::vector<std::uint16_t>& tris) {
  return nif_build::shape_block("BSTriShape", -1, Transform{}, k_full | k_colour, k_coloured_stride, vertices, tris);
}

std::vector<Vertex> grid_vertices(std::size_t side) {
  const Vec3 normal{0.0f, 0.0f, 1.0f};
  const Vec3 tangent{1.0f, 0.0f, 0.0f};
  const Vec3 bitangent{0.0f, 1.0f, 0.0f};
  const auto span = side > 1 ? static_cast<float>(side - 1) : 1.0f;
  std::vector<Vertex> vertices;
  for (std::size_t y = 0; y < side; ++y) {
    for (std::size_t x = 0; x < side; ++x) {
      const auto fx = static_cast<float>(x);
      const auto fy = static_cast<float>(y);
      vertices.push_back(Vertex{{fx, fy, 0.0f}, {fx / span, fy / span}, normal, tangent, bitangent});
    }
  }
  return vertices;
}

std::vector<std::uint16_t> grid_triangles(std::size_t side) {
  std::vector<std::uint16_t> tris;
  for (std::size_t y = 0; y + 1 < side; ++y) {
    for (std::size_t x = 0; x + 1 < side; ++x) {
      const auto base = static_cast<std::uint16_t>(y * side + x);
      const auto right = static_cast<std::uint16_t>(base + 1);
      const auto up = static_cast<std::uint16_t>(base + side);
      const auto diagonal = static_cast<std::uint16_t>(up + 1);
      tris.push_back(base);
      tris.push_back(right);
      tris.push_back(diagonal);
      tris.push_back(base);
      tris.push_back(diagonal);
      tris.push_back(up);
    }
  }
  return tris;
}

std::vector<std::byte> run_ratio(const std::vector<std::byte>& bytes, ShapeOp op, std::vector<std::uint32_t> blocks,
                                 float ratio, ShapeOpReport* report = nullptr) {
  ShapeOpRequest request;
  request.op = op;
  request.blocks = std::move(blocks);
  request.ratio = ratio;
  return apply_shape_op(read_nif(bytes), request, report);
}

}  // namespace

TEST(ShapeOps, ParsesEveryOperationNameAndRejectsOthers) {
  for (const auto op : {ShapeOp::recalc_normals, ShapeOp::recalc_tangents, ShapeOp::flip_normals,
                        ShapeOp::flip_winding, ShapeOp::apply_transform, ShapeOp::refresh_bounds}) {
    const auto parsed = fo4::nif::parse_shape_op(fo4::nif::shape_op_name(op));
    ASSERT_TRUE(parsed.has_value());
    EXPECT_EQ(*parsed, op);
  }
  EXPECT_EQ(std::string(fo4::nif::shape_op_name(ShapeOp::apply_transform)), "apply_transform");
  EXPECT_FALSE(fo4::nif::parse_shape_op("bake").has_value());
  EXPECT_FALSE(fo4::nif::parse_shape_op("Recalc_Normals").has_value());
}

TEST(ShapeOps, RecalcNormalsGivesAFlatQuadItsFaceNormal) {
  const auto file = file_with({tri_shape("BSTriShape", Transform{}, quad_vertices(), k_frame, {0, 1, 2, 0, 2, 3})});
  ShapeOpReport report;
  const auto out = run(file, ShapeOp::recalc_normals, {1}, 180.0f, &report);
  EXPECT_EQ(report.changed, std::vector<std::uint32_t>{1});
  EXPECT_TRUE(report.skipped.empty());
  for (std::size_t v = 0; v < 4; ++v) expect_vector(shape_at(out, 1).normals, v, {0.0f, 0.0f, 1.0f}, k_quantum);
}

TEST(ShapeOps, HardEdgeStaysSplitAtThirtyDegreesAndSmoothsAtOneEighty) {
  const auto file = file_with({tri_shape("BSTriShape", Transform{}, fold_vertices(), k_frame, {0, 1, 2, 3, 4, 5})});
  const float half_root = std::sqrt(0.5f);
  const auto hard = shape_at(run(file, ShapeOp::recalc_normals, {1}, 30.0f), 1).normals;
  expect_vector(hard, 0, {0.0f, 0.0f, 1.0f}, k_quantum);
  expect_vector(hard, 1, {0.0f, 0.0f, 1.0f}, k_quantum);
  expect_vector(hard, 2, {0.0f, 0.0f, 1.0f}, k_quantum);
  expect_vector(hard, 3, {-half_root, -half_root, 0.0f}, k_quantum);
  expect_vector(hard, 4, {-half_root, -half_root, 0.0f}, k_quantum);
  expect_vector(hard, 5, {-half_root, -half_root, 0.0f}, k_quantum);

  const auto smooth = shape_at(run(file, ShapeOp::recalc_normals, {1}, 180.0f), 1).normals;
  expect_vector(smooth, 0, {0.0f, 0.0f, 1.0f}, k_quantum);
  expect_vector(smooth, 1, {-0.63246f, -0.63246f, 0.44721f}, k_quantum);
  expect_vector(smooth, 4, {-0.63246f, -0.63246f, 0.44721f}, k_quantum);
  expect_vector(smooth, 2, {-0.43615f, -0.43615f, 0.78712f}, k_quantum);
  expect_vector(smooth, 3, {-0.43615f, -0.43615f, 0.78712f}, k_quantum);
  expect_vector(smooth, 5, {-half_root, -half_root, 0.0f}, k_quantum);
}

TEST(ShapeOps, RecalcTangentsReproducesTheUvFrame) {
  const auto file = file_with({tri_shape("BSTriShape", Transform{}, quad_vertices(), k_frame, {0, 1, 2, 0, 2, 3})});
  const auto shape = shape_at(run(file, ShapeOp::recalc_tangents, {1}), 1);
  for (std::size_t v = 0; v < 4; ++v) {
    expect_vector(shape.tangents, v, {1.0f, 0.0f, 0.0f}, k_quantum);
    expect_vector(shape.bitangents, v, {0.0f, 1.0f, 0.0f}, k_quantum);
  }
}

TEST(ShapeOps, FlipNormalsNegatesNormalsTangentsAndBitangents) {
  const auto file = file_with({tri_shape("BSTriShape", Transform{}, quad_vertices(), k_frame, {0, 1, 2, 0, 2, 3})});
  const auto shape = shape_at(run(file, ShapeOp::flip_normals, {1}), 1);
  for (std::size_t v = 0; v < 4; ++v) {
    expect_vector(shape.normals, v, {0.0f, 0.0f, 1.0f}, k_quantum);
    expect_vector(shape.tangents, v, {0.0f, -1.0f, 0.0f}, k_quantum);
    expect_vector(shape.bitangents, v, {-1.0f, 0.0f, 0.0f}, k_quantum);
  }
}

TEST(ShapeOps, FlipWindingSwapsTheSecondAndThirdIndexOfEveryTriangle) {
  const auto file = file_with({tri_shape("BSTriShape", Transform{}, quad_vertices(), k_frame, {0, 1, 2, 0, 2, 3})});
  const auto out = run(file, ShapeOp::flip_winding, {1});
  EXPECT_EQ(shape_at(out, 1).tris, std::vector<std::uint16_t>({0, 2, 1, 0, 3, 2}));
}

TEST(ShapeOps, FlippingTwiceReturnsTheOriginalBytes) {
  const auto file = file_with({tri_shape("BSTriShape", Transform{}, quad_vertices(), k_frame, {0, 1, 2, 0, 2, 3})});
  const auto original = write_nif(read_nif(file));
  for (const auto op : {ShapeOp::flip_normals, ShapeOp::flip_winding}) {
    const auto once = run(file, op, {1});
    EXPECT_NE(once, original);
    EXPECT_EQ(run(once, op, {1}), original);
  }
}

TEST(ShapeOps, ApplyTransformBakesTranslationRotationAndScale) {
  const Transform xf{{10.0f, 20.0f, 30.0f}, {0.0f, -1.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f}, 2.0f};
  const auto file = file_with({tri_shape("BSTriShape", xf, quad_vertices(), k_frame, {0, 1, 2, 0, 2, 3})});
  const auto out = run(file, ShapeOp::apply_transform, {1});
  const auto shape = shape_at(out, 1);
  expect_vector(shape.verts, 0, {10.0f, 20.0f, 30.0f}, 1e-4f);
  expect_vector(shape.verts, 1, {10.0f, 22.0f, 30.0f}, 1e-4f);
  expect_vector(shape.verts, 2, {8.0f, 22.0f, 30.0f}, 1e-4f);
  expect_vector(shape.verts, 3, {8.0f, 20.0f, 30.0f}, 1e-4f);
  expect_vector(shape.normals, 0, {0.0f, 0.0f, -1.0f}, k_quantum);
  expect_vector(shape.tangents, 0, {-1.0f, 0.0f, 0.0f}, k_quantum);
  expect_vector(shape.bitangents, 0, {0.0f, 1.0f, 0.0f}, k_quantum);
  const auto sphere = sphere_of(out, 1);
  EXPECT_NEAR(sphere[0], 9.0f, 1e-4f);
  EXPECT_NEAR(sphere[1], 21.0f, 1e-4f);
  EXPECT_NEAR(sphere[2], 30.0f, 1e-4f);
  EXPECT_NEAR(sphere[3], std::sqrt(2.0f), 1e-4f);
  const auto baked = block_transform(read_nif(out), 1);
  EXPECT_EQ(baked.t[0], 0.0f);
  EXPECT_EQ(baked.t[1], 0.0f);
  EXPECT_EQ(baked.t[2], 0.0f);
  EXPECT_EQ(baked.s, 1.0f);
}

TEST(ShapeOps, RefreshBoundsWritesTheSphereOfAKnownRectangle) {
  const auto file =
      file_with({tri_shape("BSTriShape", Transform{}, rectangle_vertices(), k_frame, {0, 1, 2, 0, 2, 3})});
  const auto out = run(file, ShapeOp::refresh_bounds, {1});
  const auto sphere = sphere_of(out, 1);
  EXPECT_NEAR(sphere[0], 1.0f, 1e-4f);
  EXPECT_NEAR(sphere[1], 2.0f, 1e-4f);
  EXPECT_NEAR(sphere[2], 0.0f, 1e-4f);
  EXPECT_NEAR(sphere[3], std::sqrt(5.0f), 1e-4f);
  expect_vector(shape_at(out, 1).verts, 2, {2.0f, 4.0f, 0.0f}, 1e-4f);
}

TEST(ShapeOps, SkipsNameTheReasonForEveryRefusal) {
  const auto file = file_with({
      tri_shape("BSTriShape", Transform{}, quad_vertices(), k_frame, {0, 1, 2, 0, 2, 3}),
      tri_shape("BSTriShape", Transform{}, quad_vertices(), k_full | k_uv, {0, 1, 2, 0, 2, 3}),
      tri_shape("BSDynamicTriShape", Transform{}, quad_vertices(), k_frame, {0, 1, 2, 0, 2, 3}),
      tri_shape("BSTriShape", Transform{}, quad_vertices(), k_uv | k_normal | k_tangent, {0, 1, 2, 0, 2, 3}),
  });
  ShapeOpReport normals_report;
  run(file, ShapeOp::recalc_normals, {0, 2, 9}, 180.0f, &normals_report);
  EXPECT_TRUE(normals_report.changed.empty());
  EXPECT_EQ(normals_report.skipped, std::vector<std::string>({"block 0: not a tri-shape", "block 2: no normals stored",
                                                              "block 9: no such block"}));

  ShapeOpReport transform_report;
  run(file, ShapeOp::apply_transform, {3, 4}, 180.0f, &transform_report);
  EXPECT_TRUE(transform_report.changed.empty());
  EXPECT_EQ(transform_report.skipped,
            std::vector<std::string>({"block 3: BSDynamicTriShape keeps dynamic vertices after its triangles, "
                                      "which are not baked",
                                      "block 4: half-precision positions have no exact bake"}));
}

TEST(ShapeOps, RefusesACreaseOutsideZeroToOneEighty) {
  const auto file = file_with({tri_shape("BSTriShape", Transform{}, quad_vertices(), k_frame, {0, 1, 2, 0, 2, 3})});
  EXPECT_THROW(run(file, ShapeOp::recalc_normals, {1}, -1.0f), std::invalid_argument);
  EXPECT_THROW(run(file, ShapeOp::recalc_normals, {1}, 181.0f), std::invalid_argument);
}

TEST(ShapeOpsCorpus, SmoothNormalsAndTangentsTrackTheAuthoredFrame) {
  const char* dir = std::getenv("FO4_DATA_DIR");
  if (dir == nullptr) GTEST_SKIP() << "set FO4_DATA_DIR to a Fallout 4 Data folder with its Fallout4 - *.ba2 archives";
  const auto archives = fo4::archive::archive_load_order(dir, {}, installed_master_and_cc_plugins(dir));
  if (archives.empty()) {
    GTEST_SKIP() << "set FO4_DATA_DIR to a Fallout 4 Data folder with its Fallout4 - *.ba2 archives";
  }

  const auto assets = fo4::archive::AssetResolver::open({}, archives);
  Agreement normals;
  Agreement tangents;
  std::size_t nifs = 0;
  for (const auto& reader : assets.archives()) {
    if (reader.kind() != fo4::archive::Ba2Kind::general) continue;
    for (std::size_t i = 0; i < reader.entries().size(); ++i) {
      if (!is_nif(reader.entries()[i].path)) continue;
      const auto bytes = reader.extract(i);
      const auto authored = read_shapes(read_nif(bytes));
      std::vector<std::uint32_t> blocks;
      for (const auto& shape : authored) {
        if (!shape.normals.empty() && !shape.tris.empty()) blocks.push_back(shape.block);
      }
      if (blocks.empty()) continue;
      ++nifs;
      ShapeOpReport smooth_report;
      const auto smooth = run(bytes, ShapeOp::recalc_normals, blocks, 180.0f, &smooth_report);
      ShapeOpReport tangent_report;
      const auto framed = run(bytes, ShapeOp::recalc_tangents, blocks, 180.0f, &tangent_report);
      const auto smooth_shapes = read_shapes(read_nif(smooth));
      const auto framed_shapes = read_shapes(read_nif(framed));
      for (std::size_t k = 0; k < authored.size(); ++k) {
        if (is_changed(smooth_report, authored[k].block)) {
          measure(smooth_shapes[k].normals, authored[k].normals, false, normals);
        }
        if (is_changed(tangent_report, authored[k].block)) {
          measure(framed_shapes[k].tangents, authored[k].tangents, true, tangents);
        }
      }
    }
  }
  const auto normal_mean = normals.count == 0 ? 0.0 : normals.sum / static_cast<double>(normals.count);
  const auto tangent_mean = tangents.count == 0 ? 0.0 : tangents.sum / static_cast<double>(tangents.count);
  std::cout << "shape ops corpus: nifs " << nifs << ", smoothed vertices " << normals.count
            << ", mean dot with authored normals " << normal_mean << ", tangent vertices " << tangents.count
            << ", mean |dot| with authored tangents " << tangent_mean << '\n';
  EXPECT_GT(normals.count, 0U);
  EXPECT_GT(normal_mean, 0.9);
  EXPECT_GT(tangents.count, 0U);
  EXPECT_GT(tangent_mean, 0.8);
}

TEST(ShapeOps, ParsesEveryMeshOperationNameAndFlagsTheFullSet) {
  for (const auto op : {ShapeOp::weld_vertices, ShapeOp::remove_degenerate_triangles,
                        ShapeOp::remove_unused_vertices, ShapeOp::simplify}) {
    const auto parsed = fo4::nif::parse_shape_op(fo4::nif::shape_op_name(op));
    ASSERT_TRUE(parsed.has_value());
    EXPECT_EQ(*parsed, op);
    EXPECT_TRUE(fo4::nif::is_mesh_shape_op(op));
  }
  const auto sanitize = fo4::nif::parse_shape_op("sanitize");
  ASSERT_TRUE(sanitize.has_value());
  EXPECT_EQ(*sanitize, ShapeOp::sanitize);
  EXPECT_FALSE(fo4::nif::is_mesh_shape_op(ShapeOp::sanitize));
  EXPECT_EQ(std::string(fo4::nif::shape_op_name(ShapeOp::weld_vertices)), "weld_vertices");
  EXPECT_EQ(std::string(fo4::nif::shape_op_name(ShapeOp::simplify)), "simplify");
  EXPECT_EQ(std::string(fo4::nif::shape_op_name(ShapeOp::sanitize)), "sanitize");
  EXPECT_FALSE(fo4::nif::is_mesh_shape_op(ShapeOp::recalc_normals));
  EXPECT_FALSE(fo4::nif::is_mesh_shape_op(ShapeOp::refresh_bounds));
  EXPECT_FALSE(fo4::nif::parse_shape_op("weld").has_value());
  EXPECT_FALSE(fo4::nif::parse_shape_op("Weld_Vertices").has_value());
}

TEST(ShapeOps, WeldMergesIdenticalVerticesAndRemapsTriangles) {
  const auto file = file_with({tri_shape("BSTriShape", Transform{}, split_quad_vertices(), k_frame, {0, 1, 2, 3, 4, 5})});
  ShapeOpReport report;
  const auto out = run(file, ShapeOp::weld_vertices, {1}, 180.0f, &report);
  EXPECT_TRUE(is_changed(report, 1));
  EXPECT_TRUE(report.skipped.empty());
  EXPECT_EQ(report.vertices_before, 6U);
  EXPECT_EQ(report.vertices_after, 4U);
  EXPECT_EQ(report.triangles_before, 2U);
  EXPECT_EQ(report.triangles_after, 2U);
  const auto shape = shape_at(out, 1);
  ASSERT_EQ(shape.verts.size(), 12U);
  EXPECT_EQ(shape.tris, std::vector<std::uint16_t>({0, 1, 2, 0, 2, 3}));
  EXPECT_NEAR(shape.verts[9], 0.0f, 1e-6f);
  EXPECT_NEAR(shape.verts[10], 1.0f, 1e-6f);
}

TEST(ShapeOps, WeldKeepsVerticesWhoseUvDiffers) {
  auto vertices = split_quad_vertices();
  vertices[3].uv = {1.0f, 0.0f};
  const auto file = file_with({tri_shape("BSTriShape", Transform{}, vertices, k_frame, {0, 1, 2, 3, 4, 5})});
  ShapeOpReport report;
  const auto out = run(file, ShapeOp::weld_vertices, {1}, 180.0f, &report);
  EXPECT_EQ(report.vertices_before, 6U);
  EXPECT_EQ(report.vertices_after, 5U);
  EXPECT_EQ(shape_at(out, 1).tris, std::vector<std::uint16_t>({0, 1, 2, 3, 2, 4}));
}

TEST(ShapeOps, WeldKeepsColourBytesOfTheSurvivingVertex) {
  std::vector<std::byte> vertices;
  const Vec3 origin{0.0f, 0.0f, 0.0f};
  const auto first = coloured_vertex(origin);
  const auto other = coloured_vertex(Vec3{1.0f, 0.0f, 0.0f});
  vertices.insert(vertices.end(), first.begin(), first.end());
  vertices.insert(vertices.end(), first.begin(), first.end());
  vertices.insert(vertices.end(), other.begin(), other.end());
  const auto file = file_with({coloured_shape(vertices, {0, 1, 2})});
  ShapeOpReport report;
  const auto out = run(file, ShapeOp::weld_vertices, {1}, 180.0f, &report);
  EXPECT_EQ(report.vertices_before, 3U);
  EXPECT_EQ(report.vertices_after, 2U);
  EXPECT_EQ(report.triangles_before, 1U);
  EXPECT_EQ(report.triangles_after, 0U);
  const auto shape = shape_at(out, 1);
  ASSERT_EQ(shape.colors.size(), 8U);
  EXPECT_TRUE(shape.tris.empty());
  EXPECT_NEAR(shape.colors[1], 51.0 / 255.0, 1e-6);
  EXPECT_NEAR(shape.colors[2], 102.0 / 255.0, 1e-6);
  EXPECT_NEAR(shape.colors[3], 1.0, 1e-6);
}

TEST(ShapeOps, WeldMergesOnlyVerticesWithIdenticalSkinBytes) {
  std::vector<std::byte> vertices;
  const Vec3 origin{0.0f, 0.0f, 0.0f};
  const auto first = skinned_vertex(origin, 0x3C00, 0);
  const auto other = skinned_vertex(origin, 0x3800, 1);
  vertices.insert(vertices.end(), first.begin(), first.end());
  vertices.insert(vertices.end(), first.begin(), first.end());
  vertices.insert(vertices.end(), other.begin(), other.end());
  const auto file = file_with({skinned_shape(vertices, {0, 1, 2})});
  ShapeOpReport report;
  const auto out = run(file, ShapeOp::weld_vertices, {1}, 180.0f, &report);
  EXPECT_EQ(report.vertices_before, 3U);
  EXPECT_EQ(report.vertices_after, 2U);
  EXPECT_EQ(report.triangles_before, 1U);
  EXPECT_EQ(report.triangles_after, 0U);
  const auto shape = shape_at(out, 1);
  EXPECT_EQ(shape.verts.size(), 6U);
  EXPECT_TRUE(shape.tris.empty());
}

TEST(ShapeOps, WeldingTwiceReportsNothingLeftToDo) {
  const auto file = file_with({tri_shape("BSTriShape", Transform{}, split_quad_vertices(), k_frame, {0, 1, 2, 3, 4, 5})});
  const auto once = run(file, ShapeOp::weld_vertices, {1});
  ShapeOpReport report;
  const auto twice = run(once, ShapeOp::weld_vertices, {1}, 180.0f, &report);
  EXPECT_TRUE(report.changed.empty());
  EXPECT_EQ(report.skipped, std::vector<std::string>({"block 1: no duplicate vertices"}));
  EXPECT_EQ(twice, write_nif(read_nif(once)));
}

TEST(ShapeOps, RemoveDegenerateDropsFacesWithRepeatedIndex) {
  const auto file = file_with(
      {tri_shape("BSTriShape", Transform{}, quad_vertices(), k_frame, {0, 1, 2, 0, 2, 3, 1, 1, 2})});
  ShapeOpReport report;
  const auto out = run(file, ShapeOp::remove_degenerate_triangles, {1}, 180.0f, &report);
  EXPECT_TRUE(is_changed(report, 1));
  EXPECT_EQ(report.triangles_before, 3U);
  EXPECT_EQ(report.triangles_after, 2U);
  EXPECT_EQ(report.vertices_before, 4U);
  EXPECT_EQ(report.vertices_after, 4U);
  EXPECT_EQ(shape_at(out, 1).tris, std::vector<std::uint16_t>({0, 1, 2, 0, 2, 3}));
}

TEST(ShapeOps, RemoveDegenerateDropsAZeroAreaFace) {
  auto vertices = quad_vertices();
  vertices[2].position = {2.0f, 0.0f, 0.0f};
  const auto file = file_with({tri_shape("BSTriShape", Transform{}, vertices, k_frame, {0, 1, 2})});
  ShapeOpReport report;
  const auto out = run(file, ShapeOp::remove_degenerate_triangles, {1}, 180.0f, &report);
  EXPECT_TRUE(is_changed(report, 1));
  EXPECT_EQ(report.triangles_before, 1U);
  EXPECT_EQ(report.triangles_after, 0U);
  EXPECT_TRUE(shape_at(out, 1).tris.empty());
}

TEST(ShapeOps, RemoveDegenerateKeepsACollinearFaceWhenPositionsAreHalfPrecision) {
  auto vertices = quad_vertices();
  vertices[2].position = {2.0f, 0.0f, 0.0f};
  const auto file = file_with({tri_shape("BSTriShape", Transform{}, vertices, k_uv | k_normal | k_tangent, {0, 1, 2})});
  ShapeOpReport report;
  const auto out = run(file, ShapeOp::remove_degenerate_triangles, {1}, 180.0f, &report);
  EXPECT_TRUE(report.changed.empty());
  EXPECT_EQ(report.skipped,
            std::vector<std::string>({"block 1: no repeated-index faces; zero-area faces need full precision"}));
  EXPECT_EQ(shape_at(out, 1).tris, std::vector<std::uint16_t>({0, 1, 2}));
}

TEST(ShapeOps, RemoveUnusedVerticesRemapsAndShrinks) {
  auto vertices = quad_vertices();
  vertices.push_back(vertices[0]);
  const auto file = file_with({tri_shape("BSTriShape", Transform{}, vertices, k_frame, {0, 2, 3, 0, 2, 4})});
  ShapeOpReport report;
  const auto out = run(file, ShapeOp::remove_unused_vertices, {1}, 180.0f, &report);
  EXPECT_EQ(report.vertices_before, 5U);
  EXPECT_EQ(report.vertices_after, 4U);
  EXPECT_EQ(report.triangles_before, 2U);
  EXPECT_EQ(report.triangles_after, 2U);
  EXPECT_EQ(shape_at(out, 1).tris, std::vector<std::uint16_t>({0, 1, 2, 0, 1, 3}));
  EXPECT_EQ(shape_at(out, 1).verts.size(), 12U);
}

TEST(ShapeOps, RemoveUnusedVerticesSkipsWhenEveryVertexIsUsed) {
  const auto file = file_with({tri_shape("BSTriShape", Transform{}, quad_vertices(), k_frame, {0, 1, 2, 0, 2, 3})});
  ShapeOpReport report;
  const auto out = run(file, ShapeOp::remove_unused_vertices, {1}, 180.0f, &report);
  EXPECT_TRUE(report.changed.empty());
  EXPECT_EQ(report.skipped, std::vector<std::string>({"block 1: every vertex is used by a triangle"}));
  EXPECT_EQ(out, write_nif(read_nif(file)));
}

TEST(ShapeOps, RefusesEveryTypeThatKeepsDataAfterItsTriangles) {
  const auto file = file_with({
      tri_shape("BSSubIndexTriShape", Transform{}, split_quad_vertices(), k_frame, {0, 1, 2, 3, 4, 5}),
      tri_shape("BSDynamicTriShape", Transform{}, split_quad_vertices(), k_frame, {0, 1, 2, 3, 4, 5}),
      tri_shape("BSMeshLODTriShape", Transform{}, split_quad_vertices(), k_frame, {0, 1, 2, 3, 4, 5}),
  });
  ShapeOpReport report;
  const auto out = run(file, ShapeOp::weld_vertices, {1, 2, 3}, 180.0f, &report);
  EXPECT_TRUE(report.changed.empty());
  ASSERT_EQ(report.skipped.size(), 3U);
  EXPECT_EQ(report.skipped[0],
            "block 1: only BSTriShape is supported; BSSubIndexTriShape keeps data after its triangle list that a "
            "changed count would invalidate");
  EXPECT_EQ(report.skipped[1],
            "block 2: only BSTriShape is supported; BSDynamicTriShape keeps data after its triangle list that a "
            "changed count would invalidate");
  EXPECT_EQ(report.skipped[2],
            "block 3: only BSTriShape is supported; BSMeshLODTriShape keeps data after its triangle list that a "
            "changed count would invalidate");
  EXPECT_EQ(out, write_nif(read_nif(file)));
}

TEST(ShapeOps, RefusesShapesWithoutTrianglesOrVertices) {
  const auto file = file_with({tri_shape("BSTriShape", Transform{}, quad_vertices(), k_frame, {}),
                               tri_shape("BSTriShape", Transform{}, std::vector<Vertex>{}, k_frame, {})});
  ShapeOpReport report;
  run(file, ShapeOp::weld_vertices, {1, 2}, 180.0f, &report);
  EXPECT_TRUE(report.changed.empty());
  EXPECT_EQ(report.skipped,
            std::vector<std::string>({"block 1: no triangles", "block 2: no vertex data"}));
}

TEST(ShapeOps, ReportCountsCoverOnlyTheChangedBlocks) {
  const auto file = file_with({tri_shape("BSTriShape", Transform{}, split_quad_vertices(), k_frame, {0, 1, 2, 3, 4, 5}),
                               tri_shape("BSTriShape", Transform{}, split_quad_vertices(), k_frame, {0, 1, 2, 3, 4, 5})});
  ShapeOpReport report;
  run(file, ShapeOp::weld_vertices, {2}, 180.0f, &report);
  EXPECT_EQ(report.changed, std::vector<std::uint32_t>{2});
  EXPECT_EQ(report.vertices_before, 6U);
  EXPECT_EQ(report.vertices_after, 4U);
  EXPECT_EQ(report.triangles_before, 2U);
  EXPECT_EQ(report.triangles_after, 2U);
}

TEST(ShapeOps, AttributeOpReportsOneCountOnBothSides) {
  const auto file = file_with({tri_shape("BSTriShape", Transform{}, quad_vertices(), k_frame, {0, 1, 2, 0, 2, 3})});
  ShapeOpReport report;
  run(file, ShapeOp::refresh_bounds, {1}, 180.0f, &report);
  EXPECT_TRUE(is_changed(report, 1));
  EXPECT_EQ(report.vertices_before, 4U);
  EXPECT_EQ(report.vertices_after, 4U);
  EXPECT_EQ(report.triangles_before, 2U);
  EXPECT_EQ(report.triangles_after, 2U);
}

TEST(ShapeOps, WeldingOneBlockLeavesTheOtherShapesAlone) {
  const auto file = file_with({tri_shape("BSTriShape", Transform{}, split_quad_vertices(), k_frame, {0, 1, 2, 3, 4, 5}),
                               tri_shape("BSTriShape", Transform{}, split_quad_vertices(), k_frame, {0, 1, 2, 3, 4, 5})});
  const auto before = read_shapes(read_nif(file));
  const auto out = run(file, ShapeOp::weld_vertices, {1});
  const auto after = read_shapes(read_nif(out));
  ASSERT_EQ(after.size(), 2U);
  EXPECT_EQ(after[1].verts, before[1].verts);
  EXPECT_EQ(after[1].tris, before[1].tris);
  EXPECT_EQ(after[1].normals, before[1].normals);
}

TEST(ShapeOps, ParsesSimplifyAndSanitizeNames) {
  const auto simplify = fo4::nif::parse_shape_op("simplify");
  const auto sanitize = fo4::nif::parse_shape_op("sanitize");
  ASSERT_TRUE(simplify.has_value());
  ASSERT_TRUE(sanitize.has_value());
  EXPECT_EQ(*simplify, ShapeOp::simplify);
  EXPECT_EQ(*sanitize, ShapeOp::sanitize);
  EXPECT_EQ(std::string(fo4::nif::shape_op_name(ShapeOp::simplify)), "simplify");
  EXPECT_EQ(std::string(fo4::nif::shape_op_name(ShapeOp::sanitize)), "sanitize");
  EXPECT_TRUE(fo4::nif::is_mesh_shape_op(ShapeOp::simplify));
  EXPECT_FALSE(fo4::nif::is_mesh_shape_op(ShapeOp::sanitize));
}

TEST(ShapeOps, SimplifyRefusesARatioOutsideZeroAndOne) {
  const auto file = file_with({tri_shape("BSTriShape", Transform{}, grid_vertices(4), k_frame, grid_triangles(4))});
  EXPECT_THROW(run_ratio(file, ShapeOp::simplify, {1}, 1.0f), std::invalid_argument);
  EXPECT_THROW(run_ratio(file, ShapeOp::simplify, {1}, 0.0f), std::invalid_argument);
  EXPECT_THROW(run_ratio(file, ShapeOp::simplify, {1}, 1.5f), std::invalid_argument);
}

TEST(ShapeOps, SimplifyGridReducesCountsAndReportsTheError) {
  const auto vertices = grid_vertices(4);
  const auto tris = grid_triangles(4);
  ASSERT_EQ(vertices.size(), 16U);
  ASSERT_EQ(tris.size(), 54U);
  const auto file = file_with({tri_shape("BSTriShape", Transform{}, vertices, k_frame, tris)});
  ShapeOpReport report;
  const auto out = run_ratio(file, ShapeOp::simplify, {1}, 0.5f, &report);
  EXPECT_TRUE(is_changed(report, 1));
  EXPECT_EQ(report.vertices_before, 16U);
  EXPECT_EQ(report.triangles_before, 18U);
  EXPECT_LT(report.vertices_after, 16U);
  EXPECT_LE(report.triangles_after, 9U);
  EXPECT_GT(report.triangles_after, 0U);
  EXPECT_GT(report.max_error, 0.0f);
  EXPECT_NEAR(report.max_error, std::sqrt(0.5f), 1e-4f);
  const auto shape = shape_at(out, 1);
  EXPECT_EQ(shape.verts.size(), report.vertices_after * 3);
  EXPECT_EQ(shape.tris.size(), report.triangles_after * 3);
  for (std::size_t corner = 0; corner + 2 < shape.tris.size(); corner += 3) {
    EXPECT_NE(shape.tris[corner], shape.tris[corner + 1]);
    EXPECT_NE(shape.tris[corner + 1], shape.tris[corner + 2]);
    EXPECT_NE(shape.tris[corner], shape.tris[corner + 2]);
  }
}

TEST(ShapeOps, SimplifyRefusesHalfPrecisionAndSkinnedShapes) {
  const auto half = file_with(
      {tri_shape("BSTriShape", Transform{}, grid_vertices(4), k_uv | k_normal | k_tangent, grid_triangles(4))});
  ShapeOpReport half_report;
  run_ratio(half, ShapeOp::simplify, {1}, 0.5f, &half_report);
  EXPECT_TRUE(half_report.changed.empty());
  EXPECT_EQ(half_report.skipped,
            std::vector<std::string>({"block 1: half-precision positions have no exact cluster centroid"}));

  std::vector<std::byte> skinned;
  const auto one = skinned_vertex(Vec3{0.0f, 0.0f, 0.0f}, 0x3C00, 0);
  const auto two = skinned_vertex(Vec3{1.0f, 0.0f, 0.0f}, 0x3C00, 0);
  const auto three = skinned_vertex(Vec3{0.0f, 1.0f, 0.0f}, 0x3C00, 0);
  for (const auto* vertex : {&one, &two, &three}) skinned.insert(skinned.end(), vertex->begin(), vertex->end());
  const auto file = file_with({skinned_shape(skinned, {0, 1, 2})});
  ShapeOpReport skin_report;
  run_ratio(file, ShapeOp::simplify, {1}, 0.5f, &skin_report);
  EXPECT_TRUE(skin_report.changed.empty());
  EXPECT_EQ(skin_report.skipped,
            std::vector<std::string>(
                {"block 1: skinned, so a cluster would merge vertices with different bone weights"}));
}

TEST(ShapeOps, SimplifyRefusesNonFinitePositions) {
  auto vertices = grid_vertices(4);
  vertices[5].position[2] = std::numeric_limits<float>::quiet_NaN();
  const auto file = file_with({tri_shape("BSTriShape", Transform{}, vertices, k_frame, grid_triangles(4))});
  ShapeOpReport report;
  run_ratio(file, ShapeOp::simplify, {1}, 0.5f, &report);
  EXPECT_TRUE(report.changed.empty());
  EXPECT_EQ(report.skipped, std::vector<std::string>({"block 1: a vertex position is not finite"}));
}


TEST(ShapeOps, SanitizeRepairsZeroNormalsFromTheFaceNormal) {
  auto vertices = quad_vertices();
  for (auto& vertex : vertices) vertex.normal = {0.0f, 0.0f, 0.0f};
  const auto file = file_with({tri_shape("BSTriShape", Transform{}, vertices, k_frame, {0, 1, 2, 0, 2, 3})});
  ShapeOpReport report;
  const auto out = run(file, ShapeOp::sanitize, {1}, 180.0f, &report);
  EXPECT_TRUE(is_changed(report, 1));
  EXPECT_EQ(report.vertices_before, report.vertices_after);
  EXPECT_EQ(report.triangles_before, report.triangles_after);
  const auto shape = shape_at(out, 1);
  for (std::size_t v = 0; v < 4; ++v) expect_vector(shape.normals, v, {0.0f, 0.0f, 1.0f}, k_quantum);
}

TEST(ShapeOps, SanitizeRepairsZeroTangentsPerpendicularToTheNormal) {
  auto vertices = quad_vertices();
  for (auto& vertex : vertices) {
    vertex.normal = {0.0f, 0.0f, 0.0f};
    vertex.tangent = {0.0f, 0.0f, 0.0f};
  }
  const auto file = file_with({tri_shape("BSTriShape", Transform{}, vertices, k_frame, {0, 1, 2, 0, 2, 3})});
  const auto shape = shape_at(run(file, ShapeOp::sanitize, {1}), 1);
  for (std::size_t v = 0; v < 4; ++v) {
    expect_vector(shape.normals, v, {0.0f, 0.0f, 1.0f}, k_quantum);
    expect_vector(shape.tangents, v, {0.0f, 1.0f, 0.0f}, k_quantum);
    const auto dot_value = shape.normals[3 * v] * shape.tangents[3 * v] +
                           shape.normals[3 * v + 1] * shape.tangents[3 * v + 1] +
                           shape.normals[3 * v + 2] * shape.tangents[3 * v + 2];
    EXPECT_NEAR(dot_value, 0.0f, 0.02f);
  }
}

TEST(ShapeOps, SanitizeSkipsWhenEveryAttributeIsValid) {
  const auto file = file_with({tri_shape("BSTriShape", Transform{}, quad_vertices(), k_frame, {0, 1, 2, 0, 2, 3})});
  ShapeOpReport report;
  const auto out = run(file, ShapeOp::sanitize, {1}, 180.0f, &report);
  EXPECT_TRUE(report.changed.empty());
  EXPECT_EQ(report.skipped, std::vector<std::string>({"block 1: no invalid normals or tangents"}));
  EXPECT_EQ(out, write_nif(read_nif(file)));
}

TEST(ShapeOps, SanitizeRefusesNonFinitePositions) {
  auto vertices = quad_vertices();
  vertices[0].position[1] = std::numeric_limits<float>::quiet_NaN();
  const auto file = file_with({tri_shape("BSTriShape", Transform{}, vertices, k_frame, {0, 1, 2, 0, 2, 3})});
  ShapeOpReport report;
  run(file, ShapeOp::sanitize, {1}, 180.0f, &report);
  EXPECT_TRUE(report.changed.empty());
  EXPECT_EQ(report.skipped, std::vector<std::string>({"block 1: a vertex position is not finite"}));
}