#include "fo4/nif/hknp_decode.hpp"
#include "fo4/nif/hknp_encode.hpp"
#include "fo4/nif/hknp_packfile.hpp"
#include "hknp_mesh_walk.hpp"

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <vector>

namespace {

using fo4::nif::HknpMeshTriangle;
using fo4::nif::HknpShape;
using fo4::nif::HknpShapeKind;
using fo4::nif::HknpVec3;
using hknp_mesh_walk::Triangle;

HknpShape grid_mesh(std::size_t n, std::uint32_t materials) {
  HknpShape mesh;
  mesh.kind = HknpShapeKind::compressed_mesh;
  mesh.class_name = "hknpCompressedMeshShape";
  mesh.material_crc = 1U;
  for (std::size_t y = 0; y <= n; ++y) {
    for (std::size_t x = 0; x <= n; ++x) {
      mesh.mesh_vertices.push_back({static_cast<float>(x), static_cast<float>(y), 0.25f * static_cast<float>((x * 7 + y * 3) % 5)});
    }
  }
  for (std::size_t y = 0; y < n; ++y) {
    for (std::size_t x = 0; x < n; ++x) {
      const auto a = static_cast<std::uint32_t>(y * (n + 1) + x);
      const auto b = a + 1;
      const auto c = a + static_cast<std::uint32_t>(n + 2);
      const auto d = a + static_cast<std::uint32_t>(n + 1);
      const auto crc = static_cast<std::uint32_t>(10 + (x + y) % materials);
      mesh.mesh_triangles.push_back(HknpMeshTriangle{{a, b, c, 0U}, false, crc});
      mesh.mesh_triangles.push_back(HknpMeshTriangle{{a, c, d, 0U}, false, crc});
    }
  }
  return mesh;
}

std::vector<std::byte> packed(const HknpShape& shape) {
  return fo4::nif::write_hknp_packfile(fo4::nif::author_hknp_static_body(shape, {}));
}

fo4::nif::HknpSystem decoded(const HknpShape& shape) {
  return fo4::nif::decode_hknp_system(fo4::nif::read_hknp_packfile(packed(shape)));
}

const fo4::nif::HknpPackObject& data_object(const fo4::nif::HknpPackfile& pack) {
  for (const auto& object : pack.objects) {
    if (object.class_name == "hknpCompressedMeshShapeData") return object;
  }
  throw std::runtime_error("no compressed mesh data object");
}

}  // namespace

// Detects: an encoder that partitions or quantises triangles so the decoded triangle set, its corners or its materials drift from the input.
TEST(HknpMeshEncode, AuthoredMeshDecodesBackToItsTrianglesAndMaterials) {
  const auto mesh = grid_mesh(12, 3);
  const auto sys = decoded(mesh);
  ASSERT_EQ(sys.shapes.size(), 1U);
  ASSERT_EQ(sys.shapes[0].kind, HknpShapeKind::compressed_mesh);
  EXPECT_EQ(sys.shapes[0].mesh_triangles.size(), mesh.mesh_triangles.size());
  const auto result = hknp_mesh_walk::match_triangles(hknp_mesh_walk::triangles_of(mesh),
                                                      hknp_mesh_walk::triangles_of(sys.shapes[0]), 0.01f);
  EXPECT_EQ(result.unmatched, 0U);
  EXPECT_EQ(result.matched, mesh.mesh_triangles.size());
  EXPECT_LE(result.worst, 0.01f);
}

// Detects: a section or mesh tree that stops reaching a primitive or section, reaches one twice, or runs past its node count.
TEST(HknpMeshEncode, BothTreesEnumerateEveryPrimitiveAndSectionOnce) {
  const auto pack = fo4::nif::author_hknp_static_body(grid_mesh(12, 3), {});
  const auto& data = data_object(pack);
  const std::size_t sections = hknp_mesh_walk::u32_at(data, 0x58);
  ASSERT_GE(sections, 1U);
  const auto section_walk = hknp_mesh_walk::section_tree_leaves(data, hknp_mesh_walk::local_target(data, 0x10),
                                                                hknp_mesh_walk::u32_at(data, 0x18));
  EXPECT_TRUE(section_walk.in_range);
  ASSERT_EQ(section_walk.leaves.size(), sections);
  std::vector<bool> seen_section(sections, false);
  for (const std::size_t section : section_walk.leaves) {
    ASSERT_LT(section, sections);
    EXPECT_FALSE(seen_section[section]);
    seen_section[section] = true;
  }
  const std::size_t base = hknp_mesh_walk::local_target(data, 0x50);
  for (std::size_t s = 0; s < sections; ++s) {
    const std::size_t record = base + 0x60 * s;
    const std::size_t prims = hknp_mesh_walk::u32_at(data, record + 0x50) & 0xFFU;
    const auto walk = hknp_mesh_walk::mesh_tree_leaves(data, hknp_mesh_walk::local_target(data, record),
                                                       hknp_mesh_walk::u32_at(data, record + 0x08));
    EXPECT_TRUE(walk.in_range) << "section " << s;
    ASSERT_EQ(walk.leaves.size(), prims) << "section " << s;
    std::vector<bool> seen(prims, false);
    for (const std::size_t prim : walk.leaves) {
      ASSERT_LT(prim, prims) << "section " << s;
      EXPECT_FALSE(seen[prim]) << "section " << s << " prim " << prim;
      seen[prim] = true;
    }
  }
}

// Detects: a mesh past the 128-primitive or 255-vertex section limits written as one section, which the engine could not walk.
TEST(HknpMeshEncode, LargeMeshSplitsIntoSectionsWithinTheLimits) {
  const auto mesh = grid_mesh(40, 4);
  const auto sys = decoded(mesh);
  ASSERT_EQ(sys.shapes.size(), 1U);
  const auto& decoded_shape = sys.shapes[0];
  EXPECT_GE(decoded_shape.mesh_section_counts.size(), 25U);
  for (const auto count : decoded_shape.mesh_section_counts) EXPECT_LE(count, 128U);
  const auto result = hknp_mesh_walk::match_triangles(hknp_mesh_walk::triangles_of(mesh),
                                                      hknp_mesh_walk::triangles_of(decoded_shape), 0.01f);
  EXPECT_EQ(result.unmatched, 0U);
}

// Detects: quads or a flat mesh whose plane is stored with spurious spread, or a quad read back as a triangle.
TEST(HknpMeshEncode, QuadsAndAFlatMeshDecodeWithinQuantisation) {
  HknpShape mesh;
  mesh.kind = HknpShapeKind::compressed_mesh;
  mesh.class_name = "hknpCompressedMeshShape";
  mesh.material_crc = 1U;
  mesh.mesh_vertices = {{0.0f, 0.0f, 0.0f}, {1.0f, 0.0f, 0.0f}, {1.0f, 1.0f, 0.0f}, {0.0f, 1.0f, 0.0f}, {2.0f, 0.0f, 0.0f}};
  mesh.mesh_triangles.push_back(HknpMeshTriangle{{0U, 1U, 2U, 3U}, true, 7U});
  mesh.mesh_triangles.push_back(HknpMeshTriangle{{1U, 4U, 2U, 0U}, false, 8U});
  const auto sys = decoded(mesh);
  ASSERT_EQ(sys.shapes.size(), 1U);
  ASSERT_EQ(sys.shapes[0].mesh_triangles.size(), 2U);
  EXPECT_TRUE(sys.shapes[0].mesh_triangles[0].quad);
  EXPECT_FALSE(sys.shapes[0].mesh_triangles[1].quad);
  for (const auto& vertex : sys.shapes[0].mesh_vertices) EXPECT_EQ(vertex[2], 0.0f);
  const auto result = hknp_mesh_walk::match_triangles(hknp_mesh_walk::triangles_of(mesh),
                                                      hknp_mesh_walk::triangles_of(sys.shapes[0]), 2.0e-3f);
  EXPECT_EQ(result.unmatched, 0U);
}

// Detects: triangles the encoder accepts although no reader could walk them, such as an index outside the vertex list, a quad whose fourth vertex repeats its third, a NaN vertex, or more than 256 materials.
TEST(HknpMeshEncode, MeshAuthorRejectsTrianglesItCannotWrite) {
  auto empty = grid_mesh(2, 1);
  empty.mesh_triangles.clear();
  EXPECT_THROW(packed(empty), std::invalid_argument);

  auto outside = grid_mesh(2, 1);
  outside.mesh_triangles[0].indices[2] = 999U;
  EXPECT_THROW(packed(outside), std::invalid_argument);

  auto degenerate_quad = grid_mesh(2, 1);
  degenerate_quad.mesh_triangles[0].quad = true;
  degenerate_quad.mesh_triangles[0].indices[3] = degenerate_quad.mesh_triangles[0].indices[2];
  EXPECT_THROW(packed(degenerate_quad), std::invalid_argument);

  auto nan = grid_mesh(2, 1);
  nan.mesh_vertices[1][1] = std::numeric_limits<float>::quiet_NaN();
  EXPECT_THROW(packed(nan), std::invalid_argument);

  auto many = grid_mesh(2, 1);
  for (std::uint32_t i = 0; i < 257; ++i) {
    many.mesh_triangles.push_back(HknpMeshTriangle{{0U, 1U, 2U, 0U}, false, 1000U + i});
  }
  EXPECT_THROW(packed(many), std::invalid_argument);
}
