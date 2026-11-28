#include "fo4/error.hpp"
#include "fo4/nif/collision.hpp"
#include "fo4/nif/hknp_decode.hpp"
#include "fo4/nif/hknp_packfile.hpp"
#include "fo4/nif/nif.hpp"
#include "fo4/nif/physics_nif.hpp"
#include "fo4/nif/shape_mesh.hpp"
#include "fo4/nif/vertex_desc.hpp"
#include "fo4/previs/csg.hpp"
#include "fo4/previs/detect.hpp"
#include "fo4/previs/oc_nif_rewrite.hpp"
#include "fo4/previs/shared_geom_extra.hpp"
#include "plugin_bytes.hpp"
#include "previs_test_support.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <limits>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace {

std::vector<std::byte> read_bytes(const std::filesystem::path& path) {
  std::ifstream in(path, std::ios::binary | std::ios::ate);
  if (!in) return {};
  const auto size = in.tellg();
  if (size <= 0) return {};
  in.seekg(0);
  std::vector<std::byte> bytes(static_cast<std::size_t>(size));
  in.read(reinterpret_cast<char*>(bytes.data()), size);
  return bytes;
}

fo4::previs::CsgRow make_csg_row(std::uint32_t offset, std::uint32_t word_a, std::uint32_t word_b,
                                 std::uint32_t vertices, std::uint32_t indices) {
  fo4::previs::CsgRow row{};
  const std::uint32_t fields[5] = {offset, word_a, word_b, vertices, indices};
  for (std::size_t i = 0; i < 5; ++i) {
    for (std::size_t b = 0; b < 4; ++b) {
      row.bytes[i * 4 + b] = static_cast<std::byte>((fields[i] >> (8 * b)) & 0xFFu);
    }
  }
  return row;
}

std::vector<std::vector<std::byte>> stream_chunks(std::size_t stream_bytes) {
  std::vector<std::vector<std::byte>> chunks;
  for (std::size_t at = 0; at < stream_bytes; at += fo4::previs::k_csg_inflated_chunk_max) {
    const auto length = std::min(fo4::previs::k_csg_inflated_chunk_max, stream_bytes - at);
    chunks.push_back(fo4::test::zlib_compress(std::vector<std::byte>(length)));
  }
  return chunks;
}

// Constraint: positions must default to halves-exact floats and later bytes are counters.
fo4::nif::ShapeMeshData make_shape(std::uint64_t descriptor, std::size_t vertices, const std::vector<float>& x) {
  fo4::nif::ShapeMeshData shape;
  shape.vertex_desc = descriptor;
  const auto stride = static_cast<std::size_t>(descriptor & 0xFu) * 4u;
  const bool full = ((descriptor >> 44U) & fo4::nif::k_vertex_full_precision) != 0;
  const auto slot = full ? 16u : 8u;
  for (std::size_t v = 0; v < vertices; ++v) {
    std::vector<std::byte> vertex(stride);
    if (full) {
      const float lanes[4] = {x[v % x.size()], 128.0f - static_cast<float>(v), 0.25f, -1.0f};
      std::memcpy(vertex.data(), lanes, sizeof lanes);
    } else {
      for (std::size_t i = 0; i < slot; ++i) vertex[i] = static_cast<std::byte>((v * 31u + i * 5u) & 0xFFu);
    }
    for (std::size_t i = slot; i < stride; ++i) vertex[i] = static_cast<std::byte>((v * 17u + i * 3u + 1u) & 0xFFu);
    shape.vertices.insert(shape.vertices.end(), vertex.begin(), vertex.end());
  }
  for (std::uint16_t i = 0; i < 3; ++i) shape.indices.push_back(i);
  return shape;
}

// Constraint: a refusal must name its cause in the thrown text.
std::string refusal_text(fo4::nif::ShapeMeshData shape, std::size_t trim = 0) {
  if (trim != 0) shape.vertices.resize(shape.vertices.size() - trim);
  try {
    (void)fo4::previs::csg_mesh_from_shape(shape);
  } catch (const fo4::parse_error& error) {
    return error.parse_message();
  }
  return "";
}

}  // namespace

// Constraint: the file hash is a measured function of the plugin stem, so a patch names its own container.
TEST(PrevisCsgWriter, FileHashMatchesShippedPlugins) {
  EXPECT_EQ(fo4::previs::csg_file_hash("DLCworkshop01.esm"), 0x8e566007u);
  EXPECT_EQ(fo4::previs::csg_file_hash("DLCCoast.esm"), 0x2088054du);
  EXPECT_EQ(fo4::previs::csg_file_hash("DLCworkshop01.esp"), 0x8e566007u);
}

// Constraint: a row's offset chains at vertex_count * stride + 2 * index_count, so a row is a stream address.
TEST(PrevisCsgWriter, RowsChainAtTheirOwnStreamBytes) {
  const auto mesh = [](std::uint64_t descriptor, std::uint32_t vertices, std::uint16_t words) {
    fo4::previs::CsgMeshObject out;
    out.vertex_desc = descriptor;
    out.vertex_count = vertices;
    out.vertices.assign(static_cast<std::size_t>(vertices) * out.vertex_bytes(), std::byte{0x11});
    for (std::uint16_t i = 0; i < words; ++i) out.indices.push_back(static_cast<std::uint16_t>(i % vertices));
    return out;
  };
  std::vector<fo4::previs::CsgMeshObject> meshes;
  meshes.push_back(mesh(0x0003b00005430206ULL, 25, 96));
  meshes.push_back(mesh(0x0001B00000430205ULL, 7, 12));
  const auto built = fo4::previs::build_csg(meshes);
  ASSERT_EQ(built.rows.size(), 2u);
  EXPECT_EQ(built.rows[0].data_offset, 0u);
  EXPECT_EQ(built.rows[0].vertex_count, 25u);
  EXPECT_EQ(built.rows[0].index_count, 96u);
  EXPECT_EQ(built.rows[1].data_offset, static_cast<std::uint32_t>(meshes[0].stream_bytes()));

  const auto parsed = fo4::previs::parse_csg(built.bytes);
  ASSERT_EQ(parsed.rows.size(), 2u);
  const auto first = fo4::previs::decode_csg_row(parsed.rows[0].bytes);
  EXPECT_EQ(first.data_offset, 0u);
  EXPECT_EQ(first.vertex_desc, 0x0003b00005430206ULL);
  EXPECT_EQ(first.vertex_count, 25u);
  EXPECT_EQ(first.index_count, 96u);
  const auto second = fo4::previs::decode_csg_row(parsed.rows[1].bytes);
  EXPECT_EQ(second.data_offset, static_cast<std::uint32_t>(meshes[0].stream_bytes()));
  EXPECT_EQ(second.vertex_desc, 0x0001B00000430205ULL);
  const auto stream = fo4::previs::inflate_csg_stream(parsed);
  EXPECT_EQ(stream.size(), meshes[0].stream_bytes() + meshes[1].stream_bytes());
  EXPECT_TRUE(fo4::previs::validate_csg(built.bytes).empty());
}

// Constraint: a stream longer than 65536 bytes is chunked at the measured size, the last chunk smaller.
TEST(PrevisCsgWriter, ChunksAtTheMeasuredInflatedSize) {
  fo4::previs::CsgMeshObject mesh;
  mesh.vertex_desc = 0x0003b00005430206ULL;
  mesh.vertex_count = 6000;
  mesh.vertices.assign(6000u * 24u, std::byte{0x7f});
  for (std::uint16_t i = 0; i < 300; ++i) mesh.indices.push_back(static_cast<std::uint16_t>(i % 6000));
  const auto built = fo4::previs::build_csg(std::vector<fo4::previs::CsgMeshObject>{mesh});
  const auto parsed = fo4::previs::parse_csg(built.bytes);
  ASSERT_EQ(parsed.chunks.size(), 3u);
  EXPECT_EQ(fo4::previs::decompress_csg_chunk(parsed.chunks[0]).size(), fo4::previs::k_csg_inflated_chunk_max);
  EXPECT_LT(fo4::previs::decompress_csg_chunk(parsed.chunks[2]).size(), fo4::previs::k_csg_inflated_chunk_max);
  EXPECT_EQ(fo4::previs::inflate_csg_stream(parsed).size(), mesh.stream_bytes());
  EXPECT_TRUE(fo4::previs::validate_csg(built.bytes).empty());
}

// Constraint: an existing container is extended, so a row written earlier keeps its offset and its bytes while the new rows start after the old stream.
TEST(PrevisCsgWriter, ExistingContainerIsExtendedNotReplaced) {
  const auto mesh = [](std::uint32_t vertices, std::uint8_t fill) {
    fo4::previs::CsgMeshObject out;
    out.vertex_desc = 0x0003b00005430206ULL;
    out.vertex_count = vertices;
    out.vertices.assign(static_cast<std::size_t>(vertices) * out.vertex_bytes(), std::byte{fill});
    for (std::uint16_t i = 0; i < 30; ++i) out.indices.push_back(static_cast<std::uint16_t>(i % vertices));
    return out;
  };
  const auto first_mesh = mesh(6000, 0x7f);
  const auto first = fo4::previs::build_csg(std::vector<fo4::previs::CsgMeshObject>{first_mesh});
  const auto first_stream = fo4::previs::inflate_csg_stream(fo4::previs::parse_csg(first.bytes));
  ASSERT_EQ(first_stream.size(), first_mesh.stream_bytes());

  const auto second_mesh = mesh(900, 0x22);
  const auto second = fo4::previs::build_csg(std::vector<fo4::previs::CsgMeshObject>{second_mesh}, first.bytes);
  ASSERT_EQ(second.rows.size(), 1u);
  EXPECT_EQ(second.rows[0].data_offset, static_cast<std::uint32_t>(first_stream.size()));
  EXPECT_TRUE(fo4::previs::validate_csg(second.bytes).empty());

  const auto parsed = fo4::previs::parse_csg(second.bytes);
  ASSERT_EQ(parsed.rows.size(), 2u);
  EXPECT_EQ(parsed.rows[0].bytes, fo4::previs::parse_csg(first.bytes).rows[0].bytes);
  EXPECT_EQ(fo4::previs::decode_csg_row(parsed.rows[1].bytes).data_offset, static_cast<std::uint32_t>(first_stream.size()));
  const auto stream = fo4::previs::inflate_csg_stream(parsed);
  ASSERT_EQ(stream.size(), first_stream.size() + second_mesh.stream_bytes());
  EXPECT_TRUE(std::equal(first_stream.begin(), first_stream.end(), stream.begin()));
  EXPECT_EQ(stream[first_stream.size()], std::byte{0x22});
  for (std::size_t i = 0; i + 1 < parsed.chunks.size(); ++i) {
    EXPECT_EQ(fo4::previs::decompress_csg_chunk(parsed.chunks[i]).size(), fo4::previs::k_csg_inflated_chunk_max);
  }

  const std::vector<std::byte> garbage(64, std::byte{0x41});
  EXPECT_THROW(fo4::previs::build_csg(std::vector<fo4::previs::CsgMeshObject>{second_mesh}, garbage), std::exception);
}

// Constraint: an _OC beside the patch addresses its own stream and re-reads as one object at that offset.
TEST(PrevisCsgWriter, SharedExtraAddressesTheBuiltStream) {
  const fs::path oracle = fs::path(FO4_TESTDATA_DIR) / "previs" / "oracles" / "00000FEC_0A82376A_OC.NIF";
  if (!fs::exists(oracle)) GTEST_SKIP();
  const auto bytes = read_bytes(oracle);
  const auto nif = fo4::nif::read_nif(bytes);
  std::optional<std::vector<std::byte>> rebuilt_extra;
  std::optional<fo4::previs::SharedGeomObjectWrite> written;
  for (const auto& block : nif.blocks()) {
    if (block.type != "BSPackedCombinedSharedGeomDataExtra") continue;
    const auto extra = std::span<const std::byte>(bytes).subspan(block.offset, block.size);
    const auto header = fo4::previs::read_shared_geom_header(extra);
    if (!header.has_value() || header->objects.empty()) continue;
    const auto& prototype = header->objects.front();
    fo4::previs::SharedGeomObjectWrite object;
    object.data_offset = 0x1000u;
    object.vertex_desc = prototype.vertex_desc;
    object.vertex_count = prototype.vertex_count;
    object.triangle_count = {static_cast<std::uint32_t>(prototype.triangles()), 0u, 0u};
    fo4::previs::SharedCombinedRecordXform record;
    record.translation[0] = 100.0f;
    object.records.push_back(record);
    written = object;
    rebuilt_extra = fo4::previs::build_shared_geom_extra(
        extra, fo4::previs::csg_file_hash("MyPatch.esp"),
        std::span<const fo4::previs::SharedGeomObjectWrite>(&*written, 1));
    break;
  }
  ASSERT_TRUE(written.has_value());
  ASSERT_TRUE(rebuilt_extra.has_value());
  const auto replaced = fo4::previs::replace_shared_geom_extra_block(bytes, *rebuilt_extra);
  ASSERT_TRUE(replaced.has_value());
  const auto rebuilt = fo4::nif::read_nif(*replaced);
  bool checked = false;
  for (const auto& block : rebuilt.blocks()) {
    if (block.type != "BSPackedCombinedSharedGeomDataExtra") continue;
    const auto header = fo4::previs::read_shared_geom_header(
        std::span<const std::byte>(*replaced).subspan(block.offset, block.size));
    ASSERT_TRUE(header.has_value());
    ASSERT_EQ(header->objects.size(), 1u);
    EXPECT_EQ(header->objects[0].data_offset, 0x1000u);
    EXPECT_EQ(header->objects[0].file_hash, fo4::previs::csg_file_hash("MyPatch.esp"));
    EXPECT_EQ(header->objects[0].vertex_count, written->vertex_count);
    EXPECT_EQ(header->vertex_count, written->vertex_count);
    EXPECT_EQ(header->triangle_count, written->triangle_count[0] + written->triangle_count[1] + written->triangle_count[2]);
    fo4::previs::SharedCombinedRecordXform placed{};
    ASSERT_TRUE(fo4::previs::read_shared_combined_record_xform(
        std::span<const std::byte>(*replaced).subspan(block.offset, block.size), 0, 0, placed));
    EXPECT_FLOAT_EQ(placed.translation[0], 100.0f);
    checked = true;
    break;
  }
  EXPECT_TRUE(checked);
}

// Constraint: every object of a material must keep its own CSG offset, so a dropped model fails this test.
TEST(PrevisCsgWriter, SharedExtraHoldsEveryObjectOfAMaterial) {
  const fs::path oracle = fs::path(FO4_TESTDATA_DIR) / "previs" / "oracles" / "00000FEC_0A82376A_OC.NIF";
  if (!fs::exists(oracle)) GTEST_SKIP();
  const auto bytes = read_bytes(oracle);
  const auto nif = fo4::nif::read_nif(bytes);
  std::optional<std::vector<std::byte>> rebuilt;
  std::vector<fo4::previs::SharedGeomObjectWrite> objects;
  for (const auto& block : nif.blocks()) {
    if (block.type != "BSPackedCombinedSharedGeomDataExtra") continue;
    const auto extra = std::span<const std::byte>(bytes).subspan(block.offset, block.size);
    const auto header = fo4::previs::read_shared_geom_header(extra);
    if (!header.has_value() || header->objects.empty()) continue;
    const auto& prototype = header->objects.front();
    for (std::uint32_t index = 0; index < 2; ++index) {
      fo4::previs::SharedGeomObjectWrite object;
      object.data_offset = 0x1000u + index * 0x200u;
      object.vertex_desc = prototype.vertex_desc;
      object.vertex_count = prototype.vertex_count;
      object.triangle_count = {static_cast<std::uint32_t>(prototype.triangles()), 0u, 0u};
      fo4::previs::SharedCombinedRecordXform record;
      record.translation[0] = 10.0f * static_cast<float>(index + 1);
      object.records.push_back(record);
      objects.push_back(object);
    }
    rebuilt = fo4::previs::build_shared_geom_extra(extra, fo4::previs::csg_file_hash("MyPatch.esp"), objects);
    break;
  }
  ASSERT_TRUE(rebuilt.has_value());
  ASSERT_EQ(objects.size(), 2u);
  const auto replaced = fo4::previs::replace_shared_geom_extra_block(bytes, *rebuilt);
  ASSERT_TRUE(replaced.has_value());
  const auto after = fo4::nif::read_nif(*replaced);
  bool checked = false;
  for (const auto& block : after.blocks()) {
    if (block.type != "BSPackedCombinedSharedGeomDataExtra") continue;
    const auto header = fo4::previs::read_shared_geom_header(
        std::span<const std::byte>(*replaced).subspan(block.offset, block.size));
    ASSERT_TRUE(header.has_value());
    ASSERT_EQ(header->objects.size(), 2u);
    EXPECT_EQ(header->objects[0].data_offset, 0x1000u);
    EXPECT_EQ(header->objects[1].data_offset, 0x1200u);
    EXPECT_EQ(header->objects[0].file_hash, fo4::previs::csg_file_hash("MyPatch.esp"));
    EXPECT_EQ(header->vertex_count, 2u * objects[0].vertex_count);
    EXPECT_EQ(header->triangle_count, 2u * (objects[0].triangle_count[0] + objects[0].triangle_count[1] + objects[0].triangle_count[2]));
    checked = true;
    break;
  }
  EXPECT_TRUE(checked);
}

// Constraint: the physics NIF carries the vanilla block set and reads back as one static collision body.

// Constraint: a shipped object carries up to 630 instances, so a run above 32 records must still bake and read back.
TEST(PrevisCsgWriter, SharedExtraKeepsEveryInstanceAboveThirtyTwoRecords) {
  const fs::path oracle = fs::path(FO4_TESTDATA_DIR) / "previs" / "oracles" / "00000FEC_0A82376A_OC.NIF";
  if (!fs::exists(oracle)) GTEST_SKIP();
  const auto bytes = read_bytes(oracle);
  const auto nif = fo4::nif::read_nif(bytes);
  std::optional<std::vector<std::byte>> rebuilt;
  fo4::previs::SharedGeomObjectWrite object;
  std::uint32_t vertex_count = 0;
  std::uint32_t triangles = 0;
  for (const auto& block : nif.blocks()) {
    if (block.type != "BSPackedCombinedSharedGeomDataExtra") continue;
    const auto extra = std::span<const std::byte>(bytes).subspan(block.offset, block.size);
    const auto header = fo4::previs::read_shared_geom_header(extra);
    if (!header.has_value() || header->objects.empty()) continue;
    const auto& prototype = header->objects.front();
    object.data_offset = 0x1000u;
    object.vertex_desc = prototype.vertex_desc;
    object.vertex_count = prototype.vertex_count;
    object.triangle_count = {static_cast<std::uint32_t>(prototype.triangles()), 0u, 0u};
    vertex_count = prototype.vertex_count;
    triangles = object.triangle_count[0];
    for (std::uint32_t instance = 0; instance < 47; ++instance) {
      fo4::previs::SharedCombinedRecordXform record;
      record.translation[0] = static_cast<float>(instance);
      object.records.push_back(record);
    }
    rebuilt = fo4::previs::build_shared_geom_extra(
        extra, fo4::previs::csg_file_hash("MyPatch.esp"),
        std::span<const fo4::previs::SharedGeomObjectWrite>(&object, 1));
    break;
  }
  ASSERT_EQ(object.records.size(), 47u);
  ASSERT_TRUE(rebuilt.has_value());
  const auto replaced = fo4::previs::replace_shared_geom_extra_block(bytes, *rebuilt);
  ASSERT_TRUE(replaced.has_value());
  const auto after = fo4::nif::read_nif(*replaced);
  bool checked = false;
  for (const auto& block : after.blocks()) {
    if (block.type != "BSPackedCombinedSharedGeomDataExtra") continue;
    const auto extra = std::span<const std::byte>(*replaced).subspan(block.offset, block.size);
    const auto header = fo4::previs::read_shared_geom_header(extra);
    ASSERT_TRUE(header.has_value());
    ASSERT_EQ(header->objects.size(), 1u);
    EXPECT_EQ(header->objects[0].combined_count, 47u);
    EXPECT_EQ(header->vertex_count, 47u * vertex_count);
    EXPECT_EQ(header->triangle_count, 47u * triangles);
    fo4::previs::SharedCombinedRecordXform placed{};
    ASSERT_TRUE(fo4::previs::read_shared_combined_record_xform(extra, 0, 46, placed));
    EXPECT_FLOAT_EQ(placed.translation[0], 46.0f);
    checked = true;
    break;
  }
  EXPECT_TRUE(checked);
}

TEST(PrevisPhysicsNif, ReadsBackAsOneCollisionBody) {
  fo4::nif::PhysicsMeshInput mesh;
  mesh.positions = {0.0f, 0.0f, 0.0f, 100.0f, 0.0f, 0.0f, 0.0f, 100.0f, 0.0f, 100.0f, 100.0f, 0.0f};
  mesh.indices = {0, 1, 2, 1, 3, 2};
  const auto bytes = fo4::nif::build_physics_nif(
      "00000C10_Physics", std::span<const fo4::nif::PhysicsMeshInput>(&mesh, 1));
  const auto nif = fo4::nif::read_nif(bytes);
  std::vector<std::string> types;
  for (const auto& block : nif.blocks()) types.push_back(block.type);
  const std::vector<std::string> expected{"NiNode", "BSXFlags", "bhkNPCollisionObject", "bhkPhysicsSystem"};
  EXPECT_EQ(types, expected);
  const auto blobs = fo4::nif::read_collision_blobs(nif);
  ASSERT_EQ(blobs.size(), 1u);
  EXPECT_EQ(fo4::nif::decode_hknp_system(fo4::nif::read_hknp_packfile(blobs[0].bytes)).bodies.size(), 1u);
}

// Constraint: a written object keeps one vertex block per level, so each level offset still counts the words below it.
TEST(PrevisCsgWriter, WritesEveryLodLevelOverOneVertexBuffer) {
  fo4::previs::CsgMeshObject mesh;
  mesh.vertex_desc = 0x0003b00005430206ULL;
  mesh.level_vertex_count = {24u, 12u, 8u};
  mesh.vertex_count = 44;
  mesh.vertices.assign(44u * mesh.vertex_bytes(), std::byte{0x22});
  for (std::uint16_t i = 0; i < 96; ++i) mesh.indices.push_back(static_cast<std::uint16_t>(i % 24));
  mesh.higher_lod_indices[0].assign({24, 25, 26, 27, 28, 29, 30, 31, 24, 26, 28, 30, 25, 27, 29, 31,
                                     24, 25, 26, 27, 28, 29, 30, 31, 24, 26, 28, 30, 25, 27, 29, 31,
                                     24, 25, 26, 27, 28, 29, 30, 31, 24, 26, 28, 30, 25, 27, 29, 31});
  mesh.higher_lod_indices[1].assign({36, 37, 38, 39, 40, 41, 42, 43, 36, 38, 40, 42, 37, 39, 41, 43,
                                     36, 37, 38, 39, 40, 41, 42, 43});
  const auto built = fo4::previs::build_csg(std::vector<fo4::previs::CsgMeshObject>{mesh});
  ASSERT_EQ(built.rows.size(), 1u);
  const auto& row = built.rows[0];
  EXPECT_EQ(row.lod_triangle_count[0], 32u);
  EXPECT_EQ(row.lod_triangle_count[1], 16u);
  EXPECT_EQ(row.lod_triangle_count[2], 8u);
  EXPECT_EQ(row.lod_vertex_count[0], 24u);
  EXPECT_EQ(row.lod_vertex_count[1], 12u);
  EXPECT_EQ(row.lod_vertex_count[2], 8u);
  EXPECT_EQ(row.lod_vertex_offset[0], 0u);
  EXPECT_EQ(row.lod_vertex_offset[1], 24u);
  EXPECT_EQ(row.lod_vertex_offset[2], 36u);
  EXPECT_EQ(row.lod_index_offset[0], 0u);
  EXPECT_EQ(row.lod_index_offset[1], 96u);
  EXPECT_EQ(row.lod_index_offset[2], 144u);
  EXPECT_EQ(row.index_count, 168u);
  EXPECT_EQ(fo4::previs::inflate_csg_stream(fo4::previs::parse_csg(built.bytes)).size(), mesh.stream_bytes());

  const fs::path oracle = fs::path(FO4_TESTDATA_DIR) / "previs" / "oracles" / "00000FEC_0A82376A_OC.NIF";
  if (!fs::exists(oracle)) GTEST_SKIP();
  const auto bytes = read_bytes(oracle);
  const auto nif = fo4::nif::read_nif(bytes);
  std::optional<std::vector<std::byte>> rebuilt;
  for (const auto& block : nif.blocks()) {
    if (block.type != "BSPackedCombinedSharedGeomDataExtra") continue;
    const auto extra = std::span<const std::byte>(bytes).subspan(block.offset, block.size);
    const auto header = fo4::previs::read_shared_geom_header(extra);
    if (!header.has_value() || header->objects.empty()) continue;
    fo4::previs::SharedGeomObjectWrite object;
    object.data_offset = row.data_offset;
    object.vertex_desc = mesh.vertex_desc;
    object.vertex_count = mesh.vertex_count;
    object.triangle_count = row.lod_triangle_count;
    object.records.push_back(fo4::previs::SharedCombinedRecordXform{});
    rebuilt = fo4::previs::build_shared_geom_extra(
        extra, fo4::previs::csg_file_hash("MyPatch.esp"),
        std::span<const fo4::previs::SharedGeomObjectWrite>(&object, 1));
    const auto written = rebuilt.has_value() ? fo4::previs::read_shared_geom_header(*rebuilt) : std::nullopt;
    ASSERT_TRUE(written.has_value());
    ASSERT_EQ(written->objects.size(), 1u);
    EXPECT_EQ(written->objects[0].lod_levels, fo4::previs::k_previs_lod_levels);
    EXPECT_EQ(written->objects[0].triangle_count[0], row.lod_triangle_count[0]);
    EXPECT_EQ(written->objects[0].triangle_count[1], row.lod_triangle_count[1]);
    EXPECT_EQ(written->objects[0].triangle_count[2], row.lod_triangle_count[2]);
    EXPECT_EQ(written->objects[0].triangle_offset[0], 0u);
    EXPECT_EQ(written->objects[0].triangle_offset[1], 3u * row.lod_triangle_count[0]);
    EXPECT_EQ(written->objects[0].triangle_offset[2], 3u * (row.lod_triangle_count[0] + row.lod_triangle_count[1]));
    break;
  }
  EXPECT_TRUE(rebuilt.has_value());
}

// Constraint: the row decoder reads five fields in file order, and the stride table maps measured descriptors only.
TEST(PrevisCsgCheck, DecodesTheMeasuredRowLayoutAndStride) {
  const auto raw = make_csg_row(17448, 0x05430206u, 0x0003B000u, 574, 1836);
  const auto row = fo4::previs::decode_csg_row(raw.bytes);
  EXPECT_EQ(row.data_offset, 17448u);
  EXPECT_EQ(row.vertex_desc, 0x0003B00005430206ull);
  EXPECT_EQ(row.vertex_count, 574u);
  EXPECT_EQ(row.index_count, 1836u);
  ASSERT_TRUE(fo4::previs::csg_vertex_stride(row).has_value());
  EXPECT_EQ(*fo4::previs::csg_vertex_stride(row), 24u);
  // Invariant: shipped row 0 closes against row 1 exactly: 574 * 24 + 1836 * 2 = 17448.
  EXPECT_EQ(fo4::previs::csg_row_stream_bytes(row), 17448u);

  const auto small_raw = make_csg_row(0, 0x00430205u, 0x0001B000u, 100, 50);
  const auto small = fo4::previs::decode_csg_row(small_raw.bytes);
  ASSERT_TRUE(fo4::previs::csg_vertex_stride(small).has_value());
  EXPECT_EQ(*fo4::previs::csg_vertex_stride(small), 20u);
  EXPECT_EQ(fo4::previs::csg_row_stream_bytes(small), 100 * 20u + 50 * 2u);

  const auto unknown_raw = make_csg_row(0, 0x12345678u, 0x00000000u, 1, 1);
  const auto unknown = fo4::previs::decode_csg_row(unknown_raw.bytes);
  EXPECT_FALSE(fo4::previs::csg_vertex_stride(unknown).has_value());
  EXPECT_EQ(fo4::previs::csg_row_stream_bytes(unknown), 0u);

  // Constraint: the two words must decode as the mesh's packed vertex descriptor, not a CSG-only layout.
  EXPECT_EQ(row.vertex_desc & 0xFull, 6ull);
  EXPECT_EQ(row.vertex_desc >> 44U, 0x3Bull);
  EXPECT_EQ((row.vertex_desc >> 8U) & 0xFull, 2ull);
  EXPECT_EQ((row.vertex_desc >> 16U) & 0xFull, 3ull);
  EXPECT_EQ((row.vertex_desc >> 20U) & 0xFull, 4ull);
  EXPECT_EQ((row.vertex_desc >> 24U) & 0xFull, 5ull);
  EXPECT_EQ(small.vertex_desc, 0x0001B00000430205ull);
  EXPECT_EQ(small.vertex_desc >> 44U, 0x1Bull);
  // Constraint: a stride table must not map a descriptor to a size its size nibble does not declare.
  EXPECT_EQ(small.vertex_desc & 0xFull, *fo4::previs::csg_vertex_stride(small) / 4u);
  EXPECT_EQ(row.vertex_desc & 0xFull, *fo4::previs::csg_vertex_stride(row) / 4u);
}

// Constraint: a per-cell verdict must flag a missing row, a count mismatch or an overrun, and skip an unmeasured layout.
TEST(PrevisCsgCheck, JudgesACellAgainstTheRowTable) {
  fo4::previs::CsgFile file;
  file.rows.push_back(make_csg_row(0, 0x05430206u, 0x0003B000u, 574, 1836));
  file.rows.push_back(make_csg_row(17448, 0x00430205u, 0x0001B000u, 191, 105));
  file.chunks = stream_chunks(17448 + 191 * 20 + 105 * 2);
  const auto index = fo4::previs::CsgRowIndex::build(file);
  ASSERT_EQ(index.rows().size(), 2u);
  EXPECT_EQ(index.stream_length(), 21478u);
  ASSERT_NE(index.find(17448), nullptr);
  EXPECT_EQ(index.find(17448)->vertex_count, 191u);
  EXPECT_EQ(index.find(17449), nullptr);

  // Invariant: the spans carry the objects' own descriptors, so the row words must equal them.
  const std::vector<fo4::previs::CsgMeshSpan> good{
      {1, 0, 574, 1836, 0x0003B00005430206ull},
      {2, 17448, 191, 105, 0x0001B00000430205ull},
  };
  const auto ok = fo4::previs::check_csg_cell(index, good);
  EXPECT_TRUE(ok.checked);
  EXPECT_FALSE(ok.broken);
  EXPECT_TRUE(ok.issues.empty());

  // Constraint: a row with another descriptor must make the verdict broken, even when its counts agree.
  const std::vector<fo4::previs::CsgMeshSpan> wrong_layout{{7, 0, 574, 1836, 0x0001B00000430205ull}};
  const auto mismatched = fo4::previs::check_csg_cell(index, wrong_layout);
  EXPECT_TRUE(mismatched.checked);
  EXPECT_TRUE(mismatched.broken);
  ASSERT_EQ(mismatched.issues.size(), 1u);
  EXPECT_NE(mismatched.issues[0].find("20-byte"), std::string::npos);
  EXPECT_NE(mismatched.issues[0].find("24-byte"), std::string::npos);

  const std::vector<fo4::previs::CsgMeshSpan> dangling{{3, 9000, 4, 2}};
  const auto missing = fo4::previs::check_csg_cell(index, dangling);
  EXPECT_TRUE(missing.checked);
  EXPECT_TRUE(missing.broken);
  EXPECT_EQ(missing.issues.size(), 1u);

  const std::vector<fo4::previs::CsgMeshSpan> wrong_indices{{4, 0, 574, 1837}};
  EXPECT_TRUE(fo4::previs::check_csg_cell(index, wrong_indices).broken);
  const std::vector<fo4::previs::CsgMeshSpan> wrong_vertices{{4, 17448, 192, 105}};
  EXPECT_TRUE(fo4::previs::check_csg_cell(index, wrong_vertices).broken);

  // Constraint: 3000 * 24 + 100 * 2 = 72,200 bytes must overrun the single 65,536-byte chunk.
  fo4::previs::CsgFile big;
  big.rows.push_back(make_csg_row(0, 0x05430206u, 0x0003B000u, 3000, 100));
  big.chunks = stream_chunks(fo4::previs::k_csg_inflated_chunk_max);
  const auto big_index = fo4::previs::CsgRowIndex::build(big);
  const std::vector<fo4::previs::CsgMeshSpan> overrun{{5, 0, 3000, 100}};
  EXPECT_TRUE(fo4::previs::check_csg_cell(big_index, overrun).broken);

  // Constraint: an unmeasured layout word pair must be a skip, not a verdict.
  fo4::previs::CsgFile odd;
  odd.rows.push_back(make_csg_row(0, 0x0043ABCDu, 0x0001B000u, 4, 2));
  odd.chunks = stream_chunks(64);
  const auto odd_index = fo4::previs::CsgRowIndex::build(odd);
  const std::vector<fo4::previs::CsgMeshSpan> unknown_layout{{6, 0, 4, 2}};
  const auto skipped = fo4::previs::check_csg_cell(odd_index, unknown_layout);
  EXPECT_FALSE(skipped.checked);
  EXPECT_FALSE(skipped.broken);
  EXPECT_FALSE(skipped.issues.empty());

  // Constraint: an unmeasured mesh must not hide a broken one in either order, nor swap its finding.
  fo4::previs::CsgFile mixed;
  mixed.rows.push_back(make_csg_row(0, 0x05430206u, 0x0003B000u, 574, 1836));
  mixed.rows.push_back(make_csg_row(17448, 0x0043ABCDu, 0x0001B000u, 4, 2));
  mixed.chunks = stream_chunks(64);
  const auto mixed_index = fo4::previs::CsgRowIndex::build(mixed);
  const fo4::previs::CsgMeshSpan unmeasured_span{8, 17448, 4, 2};
  const fo4::previs::CsgMeshSpan broken_span{9, 9000, 4, 2};
  for (const bool broken_first : {true, false}) {
    const std::vector<fo4::previs::CsgMeshSpan> both =
        broken_first ? std::vector<fo4::previs::CsgMeshSpan>{broken_span, unmeasured_span}
                     : std::vector<fo4::previs::CsgMeshSpan>{unmeasured_span, broken_span};
    const auto verdict = fo4::previs::check_csg_cell(mixed_index, both);
    EXPECT_TRUE(verdict.checked);
    EXPECT_TRUE(verdict.broken);
    ASSERT_EQ(verdict.issues.size(), 1u);
    EXPECT_NE(verdict.issues[0].find("no row"), std::string::npos);
  }
}

// Detects: the checker accepting a descriptor no shipped container declares, which it must not, or a wrong stride for a shipped one.
TEST(PrevisCsgCheck, AcceptsOnlyDescriptorsAShippedContainerDeclares) {
  struct Shipped {
    std::uint64_t desc;
    std::size_t stride;
  };
  const Shipped shipped[] = {{0x0000900000020003ull, 12},
                             {0x0002900003020004ull, 16},
                             {0x0001B00000430205ull, 20},
                             {0x0003B00005430206ull, 24}};
  for (const auto& entry : shipped) {
    const auto word_a = static_cast<std::uint32_t>(entry.desc & 0xFFFFFFFFu);
    const auto word_b = static_cast<std::uint32_t>(entry.desc >> 32U);
    const auto raw = make_csg_row(0, word_a, word_b, 4, 6);
    const auto row = fo4::previs::decode_csg_row(raw.bytes);
    ASSERT_TRUE(fo4::previs::csg_vertex_stride(row).has_value()) << std::hex << entry.desc;
    EXPECT_EQ(*fo4::previs::csg_vertex_stride(row), entry.stride);
    EXPECT_EQ(row.vertex_desc & 0xFull, entry.stride / 4u);
    EXPECT_EQ(fo4::previs::csg_row_stream_bytes(row), 4 * entry.stride + 6u * 2u);
    EXPECT_EQ(fo4::nif::vertex_layout_fault(fo4::nif::VertexDesc{entry.desc}), "") << std::hex << entry.desc;
  }

  constexpr std::uint64_t position_only = 0x0000100000000003ull;
  const auto position_raw = make_csg_row(0, static_cast<std::uint32_t>(position_only & 0xFFFFFFFFu),
                                         static_cast<std::uint32_t>(position_only >> 32U), 4, 6);
  const auto position_row = fo4::previs::decode_csg_row(position_raw.bytes);
  EXPECT_FALSE(fo4::previs::csg_vertex_stride(position_row).has_value());
  EXPECT_EQ(fo4::previs::csg_row_stream_bytes(position_row), 0u);
  EXPECT_FALSE(fo4::nif::vertex_layout_fault(fo4::nif::VertexDesc{position_only}).empty());

  fo4::previs::CsgFile file;
  file.rows.push_back(position_raw);
  file.chunks = stream_chunks(4 * 12 + 6 * 2);
  const auto index = fo4::previs::CsgRowIndex::build(file);
  const std::vector<fo4::previs::CsgMeshSpan> position_span{{1, 0, 4, 6, position_only}};
  const auto verdict = fo4::previs::check_csg_cell(index, position_span);
  EXPECT_FALSE(verdict.checked);
  EXPECT_FALSE(verdict.broken);
  ASSERT_EQ(verdict.issues.size(), 1u);
  EXPECT_NE(verdict.issues[0].find("unmeasured vertex layout"), std::string::npos);

  const auto skewed_raw = make_csg_row(0, 0x00420205u, 0x0001B000u, 4, 6);
  EXPECT_FALSE(fo4::previs::csg_vertex_stride(fo4::previs::decode_csg_row(skewed_raw.bytes)).has_value());
  const auto wide_raw = make_csg_row(0, 0x00000004u, 0x00001000u, 4, 6);
  EXPECT_FALSE(fo4::previs::csg_vertex_stride(fo4::previs::decode_csg_row(wide_raw.bytes)).has_value());
}

// Detects: the end check must use the inflated stream length, not the padded chunk count.
TEST(PrevisCsgCheck, EndCheckUsesTheInflatedStreamLength) {
  constexpr std::size_t mesh_bytes = 100 * 20 + 50 * 2;
  for (const std::size_t length : {mesh_bytes, mesh_bytes - 1, fo4::previs::k_csg_inflated_chunk_max + mesh_bytes}) {
    fo4::previs::CsgFile file;
    const std::uint32_t offset = length > fo4::previs::k_csg_inflated_chunk_max ? 65536u : 0u;
    file.rows.push_back(make_csg_row(offset, 0x00430205u, 0x0001B000u, 100, 50));
    file.chunks = stream_chunks(length);
    const auto index = fo4::previs::CsgRowIndex::build(file);
    EXPECT_EQ(index.stream_length(), length);
    const std::vector<fo4::previs::CsgMeshSpan> mesh{{1, offset, 100, 50, 0x0001B00000430205ull}};
    const auto verdict = fo4::previs::check_csg_cell(index, mesh);
    EXPECT_TRUE(verdict.checked);
    EXPECT_EQ(verdict.broken, length == mesh_bytes - 1) << "stream of " << length << " bytes";
  }

  fo4::previs::CsgFile empty;
  EXPECT_EQ(fo4::previs::CsgRowIndex::build(empty).stream_length(), 0u);

  fo4::previs::CsgFile bad;
  bad.chunks.push_back(std::vector<std::byte>{std::byte{0x78}, std::byte{0x9C}});
  EXPECT_THROW(fo4::previs::CsgRowIndex::build(bad), fo4::parse_error);
}

// Detects: a row table whose ordering hides a present row and still passes as a clean cell.
TEST(PrevisCsgCheck, EnforcesTheRowTableOffsetOrder) {
  fo4::previs::CsgFile file;
  file.rows.push_back(make_csg_row(17448, 0x00430205u, 0x0001B000u, 191, 105));
  file.rows.push_back(make_csg_row(0, 0x05430206u, 0x0003B000u, 574, 1836));
  file.chunks = stream_chunks(17448 + 191 * 20 + 105 * 2);
  const auto index = fo4::previs::CsgRowIndex::build(file);
  EXPECT_EQ(index.unsorted_rows(), 1u);
  ASSERT_NE(index.find(0), nullptr);
  EXPECT_EQ(index.find(0)->vertex_count, 574u);
  ASSERT_NE(index.find(17448), nullptr);
  EXPECT_EQ(index.find(17448)->vertex_count, 191u);

  // Constraint: every row is present, and the table is still reported malformed.
  const std::vector<fo4::previs::CsgMeshSpan> meshes{{1, 0, 574, 1836}, {2, 17448, 191, 105}};
  const auto verdict = fo4::previs::check_csg_cell(index, meshes);
  EXPECT_TRUE(verdict.checked);
  EXPECT_TRUE(verdict.broken);
  ASSERT_FALSE(verdict.issues.empty());
  EXPECT_NE(verdict.issues.front().find("not sorted"), std::string::npos);

  // Control: the same rows in stored order are clean, so the finding is the ordering and nothing else.
  fo4::previs::CsgFile ordered;
  ordered.rows.push_back(make_csg_row(0, 0x05430206u, 0x0003B000u, 574, 1836));
  ordered.rows.push_back(make_csg_row(17448, 0x00430205u, 0x0001B000u, 191, 105));
  ordered.chunks = file.chunks;
  const auto ordered_index = fo4::previs::CsgRowIndex::build(ordered);
  EXPECT_EQ(ordered_index.unsorted_rows(), 0u);
  EXPECT_FALSE(fo4::previs::check_csg_cell(ordered_index, meshes).broken);
}

// Detects: build_csg writing a row the checker reads as broken, which must never flag a cell the generator wrote.
TEST(PrevisCsgCheck, ContainersFromBuildCsgPassTheCellCheck) {
  fo4::previs::CsgMeshObject lods;
  lods.vertex_desc = 0x0003B00005430206ull;
  lods.level_vertex_count = {24u, 12u, 8u};
  lods.vertex_count = 44;
  lods.vertices.assign(44u * lods.vertex_bytes(), std::byte{0x22});
  for (std::uint16_t i = 0; i < 96; ++i) lods.indices.push_back(static_cast<std::uint16_t>(i % 24));
  lods.higher_lod_indices[0].assign(48, 25);
  lods.higher_lod_indices[1].assign(24, 37);
  fo4::previs::CsgMeshObject single;
  single.vertex_desc = 0x0001B00000430205ull;
  single.vertex_count = 7;
  single.vertices.assign(7u * single.vertex_bytes(), std::byte{0x33});
  for (std::uint16_t i = 0; i < 12; ++i) single.indices.push_back(static_cast<std::uint16_t>(i % 7));

  const auto first = fo4::previs::build_csg(std::vector<fo4::previs::CsgMeshObject>{lods});
  const auto grown = fo4::previs::build_csg(std::vector<fo4::previs::CsgMeshObject>{single}, first.bytes);
  const auto index = fo4::previs::CsgRowIndex::build(fo4::previs::parse_csg(grown.bytes));
  ASSERT_EQ(index.rows().size(), 2u);
  EXPECT_EQ(index.unsorted_rows(), 0u);
  EXPECT_EQ(index.stream_length(), lods.stream_bytes() + single.stream_bytes());

  std::vector<fo4::previs::CsgMeshSpan> spans;
  std::uint32_t id = 1;
  for (const auto& row : {first.rows[0], grown.rows[0]}) {
    const auto triangles = row.lod_triangle_count[0] + row.lod_triangle_count[1] + row.lod_triangle_count[2];
    EXPECT_EQ(triangles * 3, row.index_count);
    spans.push_back({id++, row.data_offset, row.vertex_count, triangles * 3, row.vertex_desc});
  }
  const auto verdict = fo4::previs::check_csg_cell(index, spans);
  EXPECT_TRUE(verdict.checked);
  EXPECT_FALSE(verdict.broken) << (verdict.issues.empty() ? "" : verdict.issues.front());
}

// Detects: the generator writing a source shape's own layout (full-precision, 28 or 32 bytes) into the container, which the checker must skip, never pass as clean.
TEST(PrevisCsgCheck, GeneratedMeshesOfEveryShippedKindPassTheCellCheckClean) {
  struct Kind {
    std::uint64_t source;
    std::uint64_t shipped;
    std::size_t shipped_stride;
  };
  const Kind kinds[] = {{0x0000900000020003ull, 0x0000900000020003ull, 12}, {0x0002900003020004ull, 0x0002900003020004ull, 16},
                        {0x0001B00000430205ull, 0x0001B00000430205ull, 20}, {0x0003B00005430206ull, 0x0003B00005430206ull, 24},
                        {0x0040900000040005ull, 0x0000900000020003ull, 12}, {0x0042900005040006ull, 0x0002900003020004ull, 16},
                        {0x0041B00000650407ull, 0x0001B00000430205ull, 20}, {0x0043B00007650408ull, 0x0003B00005430206ull, 24}};
  const std::vector<float> x{-1.0f, -0.5f, 0.0f, 0.5f, 1.0f};
  const std::uint16_t expected_x[] = {0xBC00, 0xB800, 0x0000, 0x3800, 0x3C00};
  const std::uint16_t expected_y[] = {0x5800, 0x57F0, 0x57E0, 0x57D0, 0x57C0};
  constexpr std::size_t k_vertices = 5;
  std::vector<fo4::previs::CsgMeshObject> meshes;
  for (const auto& kind : kinds) {
    SCOPED_TRACE(testing::Message() << std::hex << kind.source);
    ASSERT_EQ(fo4::nif::vertex_layout_fault(fo4::nif::VertexDesc{kind.source}), "");
    const auto shape = make_shape(kind.source, k_vertices, x);
    const auto mesh = fo4::previs::csg_mesh_from_shape(shape);
    EXPECT_EQ(mesh.vertex_desc, kind.shipped);
    ASSERT_EQ(fo4::previs::csg_vertex_stride(mesh.vertex_desc), kind.shipped_stride);
    ASSERT_EQ(mesh.vertex_count, k_vertices);
    ASSERT_EQ(mesh.vertices.size(), k_vertices * kind.shipped_stride);
    const auto source_stride = static_cast<std::size_t>(kind.source & 0xFu) * 4u;
    if (kind.source == kind.shipped) {
      EXPECT_EQ(mesh.vertices, shape.vertices);
    } else {
      for (std::size_t v = 0; v < k_vertices; ++v) {
        const auto* at = mesh.vertices.data() + v * kind.shipped_stride;
        std::uint16_t lane[4]{};
        std::memcpy(lane, at, sizeof lane);
        EXPECT_EQ(lane[0], expected_x[v]);
        EXPECT_EQ(lane[1], expected_y[v]);
        EXPECT_EQ(lane[2], 0x3400u);
        EXPECT_EQ(lane[3], 0xBC00u);
        EXPECT_EQ(std::memcmp(at + 8, shape.vertices.data() + v * source_stride + 16, kind.shipped_stride - 8), 0);
      }
    }
    meshes.push_back(mesh);
  }

  const auto built = fo4::previs::build_csg(meshes);
  const auto index = fo4::previs::CsgRowIndex::build(fo4::previs::parse_csg(built.bytes));
  ASSERT_EQ(index.rows().size(), meshes.size());
  std::vector<fo4::previs::CsgMeshSpan> spans;
  std::uint32_t id = 1;
  for (std::size_t i = 0; i < meshes.size(); ++i) {
    spans.push_back({id++, built.rows[i].data_offset, meshes[i].vertex_count, built.rows[i].index_count, meshes[i].vertex_desc});
  }
  const auto verdict = fo4::previs::check_csg_cell(index, spans);
  EXPECT_TRUE(verdict.checked);
  EXPECT_FALSE(verdict.broken);
  EXPECT_TRUE(verdict.issues.empty()) << (verdict.issues.empty() ? "" : verdict.issues.front());
}

// Detects: an unshipped layout or unrepresentable position reaching the container, which must be refused, or a tie not going to the even half.
TEST(PrevisCsgCheck, UnshippedShapeLayoutsAreRefusedNotWritten) {
  const std::vector<float> plain{0.0f};
  EXPECT_NE(refusal_text(make_shape(0x0005B00050430208ull, 4, plain)).find("none of the four layouts the game ships"), std::string::npos);
  EXPECT_NE(refusal_text(make_shape(0x0000100000000003ull, 4, plain)).find("the shape's vertex layout"), std::string::npos);

  const std::uint64_t full = 0x0043B00007650408ull;
  const float nan = std::numeric_limits<float>::quiet_NaN();
  const float inf = std::numeric_limits<float>::infinity();
  for (const float bad : {65536.0f, 65520.0f, 65505.0f, -65536.0f, nan, inf, -inf}) {
    EXPECT_NE(refusal_text(make_shape(full, 4, {bad})).find("not finite or is past the half range of 65504"), std::string::npos) << bad;
  }
  auto bad_lane = make_shape(full, 4, {1.0f});
  const float past = 70000.0f;
  std::memcpy(bad_lane.vertices.data() + 2 * 32 + 12, &past, sizeof past);
  EXPECT_NE(refusal_text(bad_lane).find("at vertex 2"), std::string::npos);
  EXPECT_NE(refusal_text(make_shape(full, 4, plain), 5).find("not whole vertices"), std::string::npos);

  struct Round {
    float value;
    std::uint16_t half;
  };
  const Round rounded[] = {{0.1f, 0x2E66},          {1.0e-8f, 0x0000},       {-0.1f, 0xAE66},         {573.25f, 0x607A},
                           {573.75f, 0x607C},       {-573.25f, 0xE07A},      {-573.75f, 0xE07C},      {5.9604645e-08f, 0x0001},
                           {6.1035156e-05f, 0x0400}, {-0.0f, 0x8000},        {65504.0f, 0x7BFF},      {-65504.0f, 0xFBFF}};
  for (const auto& expect : rounded) {
    const auto mesh = fo4::previs::csg_mesh_from_shape(make_shape(full, 1, {expect.value}));
    std::uint16_t first = 0;
    std::memcpy(&first, mesh.vertices.data(), sizeof first);
    EXPECT_EQ(first, expect.half) << expect.value;
  }
  const auto lane_four = fo4::previs::csg_mesh_from_shape(make_shape(full, 1, {573.25f}));
  EXPECT_EQ(lane_four.vertices.size(), 24u);
  auto fourth = make_shape(full, 1, {0.0f});
  const float tie = 573.75f;
  std::memcpy(fourth.vertices.data() + 12, &tie, sizeof tie);
  const auto fourth_mesh = fo4::previs::csg_mesh_from_shape(fourth);
  std::uint16_t lane[4]{};
  std::memcpy(lane, fourth_mesh.vertices.data(), sizeof lane);
  EXPECT_EQ(lane[3], 0x607C);

  const auto positions = fo4::previs::csg_mesh_positions(fo4::previs::csg_mesh_from_shape(make_shape(full, 2, {573.25f, 0.1f})));
  ASSERT_EQ(positions.size(), 6u);
  EXPECT_EQ(positions[0], 573.0f);
  EXPECT_EQ(positions[1], 128.0f);
  EXPECT_EQ(positions[2], 0.25f);
  EXPECT_EQ(positions[3], 0.0999755859375f);
  EXPECT_EQ(positions[4], 127.0f);

  auto stored = make_shape(0x0000900000020003ull, 3, plain);
  stored.vertices[12 + 1] = std::byte{0x7C};
  const auto stored_mesh = fo4::previs::csg_mesh_from_shape(stored);
  const auto stored_text = [&] {
    try {
      (void)fo4::previs::csg_mesh_positions(stored_mesh);
    } catch (const fo4::parse_error& error) {
      return error.parse_message();
    }
    return std::string();
  }();
  EXPECT_NE(stored_text.find("half infinity or NaN at vertex 1"), std::string::npos) << stored_text;

  fo4::previs::CsgMeshObject raw;
  raw.vertex_desc = full;
  raw.vertex_count = 4;
  raw.vertices.assign(4u * raw.vertex_bytes(), std::byte{0x11});
  raw.indices = {0, 1, 2};
  EXPECT_THROW(fo4::previs::build_csg(std::vector<fo4::previs::CsgMeshObject>{raw}), fo4::parse_error);

  fo4::previs::CsgFile file;
  file.rows.push_back(make_csg_row(0, 0x07650408u, 0x0043B000u, 4, 3));
  file.chunks = stream_chunks(4 * 32 + 3 * 2);
  const auto index = fo4::previs::CsgRowIndex::build(file);
  const std::vector<fo4::previs::CsgMeshSpan> span{{1, 0, 4, 3, full}};
  EXPECT_FALSE(fo4::previs::check_csg_cell(index, span).checked);
}

// Constraint: csg_rows_missing must fire only on a broken CSG verdict, and keep the verdict's detail.
TEST(PrevisCsgCheck, CsgRowsMissingIsDrivenByTheVerdict) {
  fo4::previs::CellPrevisInput clean{};
  clean.has_xcri = true;
  clean.cdx_checked = true;
  clean.has_cdx_row = true;
  clean.oc_nif_checked = true;
  EXPECT_FALSE(fo4::previs::diagnose_cell(clean).broken);

  auto broken = clean;
  broken.csg_checked = true;
  broken.csg_broken = true;
  broken.csg_detail = "CSG has no row at stream offset 4096 for OC id 7";
  const auto status = fo4::previs::diagnose_cell(broken);
  ASSERT_TRUE(status.broken);
  const auto* issue = fo4::test::previs::find_issue(status.issues, fo4::previs::PrevisIssue::csg_rows_missing);
  ASSERT_NE(issue, nullptr);
  EXPECT_EQ(issue->detail, broken.csg_detail);
  EXPECT_EQ(fo4::previs::to_string(fo4::previs::PrevisIssue::csg_rows_missing), "csg_rows_missing");
  EXPECT_EQ(static_cast<int>(fo4::previs::PrevisIssue::csg_rows_missing), 14);
  EXPECT_EQ(static_cast<int>(fo4::previs::PrevisIssue::tome_unreadable), 13);

  // Constraint: an absent answer must stay silent, not become a skip.
  auto unanswered = clean;
  unanswered.csg_checked = false;
  unanswered.csg_broken = true;
  const auto silent = fo4::previs::diagnose_cell(unanswered);
  EXPECT_FALSE(silent.broken);
  EXPECT_EQ(fo4::test::previs::find_issue(silent.issues, fo4::previs::PrevisIssue::csg_rows_missing), nullptr);
  EXPECT_FALSE(silent.skip_unsupported);
}

// Detects: a geometry check that could not run must not read as a clean cell.
TEST(PrevisCsgCheck, ACsgCheckThatDidNotRunIsASkip) {
  fo4::previs::CellPrevisInput in{};
  in.has_xcri = true;
  in.cdx_checked = true;
  in.has_cdx_row = true;
  in.oc_nif_checked = true;
  in.csg_not_run = true;
  in.csg_detail = "the plugin ships no Geometry.csg";
  const auto status = fo4::previs::diagnose_cell(in);
  EXPECT_FALSE(status.broken);
  EXPECT_TRUE(status.skip_unsupported);
  ASSERT_EQ(status.skips.size(), 1u);
  EXPECT_EQ(status.skips[0], "geometry check did not run: the plugin ships no Geometry.csg");

  in.csg_not_run = false;
  in.csg_checked = true;
  const auto ran = fo4::previs::diagnose_cell(in);
  EXPECT_FALSE(ran.skip_unsupported);
  EXPECT_TRUE(ran.skips.empty());

  fo4::previs::CellPrevisInput plain{};
  plain.csg_not_run = true;
  EXPECT_FALSE(fo4::previs::diagnose_cell(plain).skip_unsupported);
}
