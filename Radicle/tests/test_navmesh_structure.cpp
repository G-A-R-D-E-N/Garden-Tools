#include "fo4/environment/navmesh.hpp"
#include "fo4/plugin/navmesh_structure.hpp"
#include "fo4/plugin/writer.hpp"
#include "fo4/schema/scalar.hpp"
#include "plugin_bytes.hpp"

#include <gtest/gtest.h>
#include <array>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <limits>
#include <string>

namespace {
using namespace fo4;
using fo4::test::ByteWriter;

FormKey key_for(std::uint32_t id) {
  return {PluginKey::from_filename("Navigation.esm"), LocalFormId{id}};
}

ByteWriter minimal_geometry() {
  ByteWriter b;
  b.u32(15); b.u32(0x12345678); b.u32(0); b.u32(0x810);
  for (int i = 0; i < 8; ++i) b.u32(0);
  for (int i = 0; i < 8; ++i) b.f32(0);
  return b;
}

ByteWriter structure_geometry(bool cover_mapped) {
  ByteWriter b;
  b.u32(15); b.u32(0x12345678); b.u32(0); b.u32(0x810);
  b.u32(5);
  for (int i = 0; i < 5; ++i) { b.f32(static_cast<float>(i)); b.f32(0); b.f32(0); }
  b.u32(2);
  b.u16(1); b.u16(2); b.u16(3); b.u16(0xFFFF); b.u16(1); b.u16(0); b.f32(4.5f); b.u8(0xAA); b.u32(0x4);
  b.u16(2); b.u16(3); b.u16(4); b.u16(0); b.u16(0xFFFF); b.u16(0xFFFF); b.f32(4.5f); b.u8(0xAA); b.u32(0);
  b.u32(1); b.u32(0); b.u32(0x802); b.u16(1); b.u8(0);
  b.u32(1); b.u16(0); b.u32(0x87654321); b.u32(0x812);
  b.u32(1); b.u16(2); b.u16(4); b.u32(0);
  b.u32(cover_mapped ? 1 : 0);
  if (cover_mapped) { b.u16(1); b.u16(0); }
  b.u32(1); b.f32(0.5f); b.f32(0.25f); b.f32(0.125f); b.u16(1); b.u32(7);
  b.u32(1); b.f32(1); b.f32(1); b.f32(0); b.f32(0); b.f32(0); b.f32(10); b.f32(10); b.f32(10);
  b.u32(0);
  return b;
}

Plugin structure_plugin(bool cover_mapped = false, bool with_lists = true) {
  const auto geometry = structure_geometry(cover_mapped);
  ByteWriter pnam; pnam.u16(1); pnam.u16(3);
  ByteWriter nnam; nnam.u16(2);
  ByteWriter mnam; mnam.u32(0x812); mnam.u16(2); mnam.u16(0); mnam.u16(1);
  ByteWriter sentinel; sentinel.u32(0x12345678);
  ByteWriter out;
  auto record = fo4::test::begin_record(out, "TES4", 1, 0);
  ByteWriter header; header.f32(1); header.u32(2); header.u32(0x900);
  fo4::test::write_subrecord(out, "HEDR", header.bytes); fo4::test::end_record(out, record);
  auto group = fo4::test::begin_group(out, "NAVM", 0);
  record = fo4::test::begin_record(out, "NAVM", 0, 0x800, 131);
  fo4::test::write_subrecord(out, "NVNM", geometry.bytes);
  if (with_lists) {
    fo4::test::write_subrecord(out, "PNAM", pnam.bytes);
    fo4::test::write_subrecord(out, "NNAM", nnam.bytes);
    fo4::test::write_subrecord(out, "MNAM", mnam.bytes);
  }
  fo4::test::end_record(out, record);
  record = fo4::test::begin_record(out, "NAVM", 0, 0x802, 131);
  fo4::test::write_subrecord(out, "NVNM", minimal_geometry().bytes);
  fo4::test::end_record(out, record); fo4::test::end_group(out, group);
  group = fo4::test::begin_group(out, "REFR", 0);
  record = fo4::test::begin_record(out, "REFR", 0, 0x812);
  fo4::test::write_subrecord(out, "ZZZZ", sentinel.bytes);
  fo4::test::end_record(out, record); fo4::test::end_group(out, group);
  return read_plugin(out.bytes, "Navigation.esm");
}

ByteWriter sized_geometry(std::uint32_t vertices, std::uint32_t triangles, std::uint32_t edges) {
  ByteWriter b;
  b.u32(15); b.u32(0x12345678); b.u32(0); b.u32(0x810);
  b.u32(vertices);
  for (std::uint32_t i = 0; i < vertices; ++i) { b.f32(0); b.f32(0); b.f32(0); }
  b.u32(triangles);
  for (std::uint32_t i = 0; i < triangles; ++i) {
    b.u16(0); b.u16(1); b.u16(2); b.u16(0xFFFF); b.u16(0xFFFF); b.u16(0xFFFF);
    b.f32(0); b.u8(0); b.u32(0);
  }
  b.u32(edges);
  for (std::uint32_t i = 0; i < edges; ++i) { b.u32(0); b.u32(0x802); b.u16(0); b.u8(0); }
  for (int i = 0; i < 4; ++i) b.u32(0);
  for (int i = 0; i < 9; ++i) b.u32(0);
  return b;
}

Plugin sized_plugin(std::uint32_t vertices, std::uint32_t triangles, std::uint32_t edges) {
  const auto geometry = sized_geometry(vertices, triangles, edges);
  ByteWriter out;
  auto record = fo4::test::begin_record(out, "TES4", 1, 0);
  ByteWriter header; header.f32(1); header.u32(2); header.u32(0x900);
  fo4::test::write_subrecord(out, "HEDR", header.bytes); fo4::test::end_record(out, record);
  auto group = fo4::test::begin_group(out, "NAVM", 0);
  record = fo4::test::begin_record(out, "NAVM", 0, 0x800, 131);
  fo4::test::write_subrecord(out, "NVNM", geometry.bytes);
  fo4::test::end_record(out, record);
  record = fo4::test::begin_record(out, "NAVM", 0, 0x802, 131);
  fo4::test::write_subrecord(out, "NVNM", minimal_geometry().bytes);
  fo4::test::end_record(out, record); fo4::test::end_group(out, group);
  return read_plugin(out.bytes, "Navigation.esm");
}

Plugin unreadable_neighbor_plugin(bool undecodable = false) {
  const auto geometry = structure_geometry(false);
  ByteWriter garbage; garbage.u32(0xFFFFFFFF);
  ByteWriter out;
  auto record = fo4::test::begin_record(out, "TES4", 1, 0);
  ByteWriter header; header.f32(1); header.u32(2); header.u32(0x900);
  fo4::test::write_subrecord(out, "HEDR", header.bytes); fo4::test::end_record(out, record);
  auto group = fo4::test::begin_group(out, "NAVM", 0);
  record = fo4::test::begin_record(out, "NAVM", 0, 0x800, 131);
  fo4::test::write_subrecord(out, "NVNM", geometry.bytes);
  fo4::test::end_record(out, record);
  record = fo4::test::begin_record(out, "NAVM", undecodable ? 0x00040000u : 0u, 0x802, 131);
  if (undecodable) {
    out.u32(16);
    out.raw("not a zlib stream");
  } else {
    fo4::test::write_subrecord(out, "NVNM", garbage.bytes);
  }
  fo4::test::end_record(out, record); fo4::test::end_group(out, group);
  return read_plugin(out.bytes, "Navigation.esm");
}

env::NavmeshTriangle triangle(std::array<std::uint16_t, 3> vertices, std::array<std::int16_t, 3> edges,
                              std::uint32_t flags = 0) {
  env::NavmeshTriangle value;
  value.vertices = vertices;
  value.edges = edges;
  value.clearance = 1.0f;
  value.traversal = 0;
  value.flags = flags;
  return value;
}

template <class Check> void expect_decoded(Plugin& plugin, Check&& check) {
  const auto bytes = write_plugin(plugin);
  const auto root = std::filesystem::temp_directory_path() /
      ("navmesh_structure_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
  std::filesystem::create_directories(root);
  const auto path = root / "Navigation.esm";
  {
    std::ofstream file(path, std::ios::binary);
    file.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
  }
  {
    const std::filesystem::path paths[] = {path};
    const auto loaded = fo4::env::Environment::load(paths);
    const auto geometry = fo4::env::decode_navmesh(loaded, *loaded.winner(key_for(0x800)));
    ASSERT_TRUE(geometry.has_value());
    check(*geometry);
  }
  std::error_code error;
  std::filesystem::remove_all(root, error);
}

TEST(NavmeshStructure, AddVertexAppendsAndRejectsNonFinite) {
  auto plugin = structure_plugin();
  EXPECT_EQ(add_navmesh_vertex(plugin, key_for(0x800), {7, 8, 9}), ScalarMutationStatus::updated);
  EXPECT_THROW(add_navmesh_vertex(plugin, key_for(0x800), {std::numeric_limits<float>::quiet_NaN(), 0, 0}),
               schema::ScalarError);
  expect_decoded(plugin, [](const env::NavmeshGeometry& g) {
    ASSERT_EQ(g.vertices.size(), 6u);
    EXPECT_EQ(g.vertices[5], (std::array<float, 3>{7, 8, 9}));
  });
}

TEST(NavmeshStructure, VertexRemovalRefusesListsCoversAndReferences) {
  auto listed = structure_plugin();
  EXPECT_THROW(remove_navmesh_vertex(listed, key_for(0x800), 0), schema::ScalarError);

  auto plugin = structure_plugin(false, false);
  EXPECT_THROW(remove_navmesh_vertex(plugin, key_for(0x800), 0), schema::ScalarError);
  EXPECT_EQ(remove_navmesh_cover(plugin, key_for(0x800), 0), ScalarMutationStatus::updated);
  EXPECT_THROW(remove_navmesh_vertex(plugin, key_for(0x800), 1), schema::ScalarError);
  EXPECT_EQ(remove_navmesh_vertex(plugin, key_for(0x800), 0), ScalarMutationStatus::updated);
  expect_decoded(plugin, [](const env::NavmeshGeometry& g) {
    ASSERT_EQ(g.vertices.size(), 4u);
    EXPECT_EQ(g.triangles[0].vertices, (std::array<std::uint16_t, 3>{0, 1, 2}));
    EXPECT_EQ(g.triangles[1].vertices, (std::array<std::uint16_t, 3>{1, 2, 3}));
  });
}

TEST(NavmeshStructure, TriangleAddValidatesAndRemovalRenumbersReferences) {
  auto plugin = structure_plugin(false, false);
  EXPECT_EQ(add_navmesh_triangle(plugin, key_for(0x800), triangle({0, 1, 2}, {-1, -1, -1})),
            ScalarMutationStatus::updated);
  EXPECT_THROW(add_navmesh_triangle(plugin, key_for(0x800), triangle({0, 1, 9}, {-1, -1, -1})), schema::ScalarError);
  EXPECT_THROW(add_navmesh_triangle(plugin, key_for(0x800), triangle({0, 1, 2}, {3, -1, -1})), schema::ScalarError);
  EXPECT_THROW(add_navmesh_triangle(plugin, key_for(0x800), triangle({0, 1, 2}, {1, -1, -1}, 0x1)),
               schema::ScalarError);
  EXPECT_EQ(add_navmesh_triangle(plugin, key_for(0x800), triangle({0, 1, 4}, {-1, -1, -1})),
            ScalarMutationStatus::updated);
  EXPECT_EQ(add_navmesh_edge_link(plugin, key_for(0x800), {0, key_for(0x802), 3, 1}), ScalarMutationStatus::updated);
  EXPECT_THROW(remove_navmesh_triangle(plugin, key_for(0x800), 0), schema::ScalarError);
  EXPECT_THROW(remove_navmesh_triangle(plugin, key_for(0x800), 1), schema::ScalarError);
  EXPECT_EQ(remove_navmesh_triangle(plugin, key_for(0x800), 2), ScalarMutationStatus::updated);
  expect_decoded(plugin, [](const env::NavmeshGeometry& g) {
    ASSERT_EQ(g.triangles.size(), 3u);
    ASSERT_EQ(g.edges.size(), 2u);
    EXPECT_EQ(g.edges.back().triangle, 3);
    EXPECT_EQ(g.edges.back().edge, 1);
  });
}

TEST(NavmeshStructure, EdgeLinkRemovalRefusesExtraEdgeAndAddValidatesTargets) {
  auto plugin = structure_plugin();
  EXPECT_THROW(remove_navmesh_edge_link(plugin, key_for(0x800), 0), schema::ScalarError);
  EXPECT_THROW(add_navmesh_edge_link(plugin, key_for(0x800), {3, key_for(0x802), 1, 2}), schema::ScalarError);
  EXPECT_THROW(add_navmesh_edge_link(plugin, key_for(0x800), {1, key_for(0x802), -1, 2}), schema::ScalarError);
  EXPECT_THROW(add_navmesh_edge_link(plugin, key_for(0x800), {1, key_for(0x812), 1, 2}), schema::ScalarError);
  EXPECT_THROW(add_navmesh_edge_link(plugin, key_for(0x800), {1, FormKey{}, 1, 2}), schema::ScalarError);
  EXPECT_THROW(add_navmesh_edge_link(plugin, key_for(0x800), {0, key_for(0x802), 1, 3}), schema::ScalarError);
  EXPECT_EQ(add_navmesh_edge_link(plugin, key_for(0x800), {1, key_for(0x802), 1, 2}), ScalarMutationStatus::updated);
  EXPECT_EQ(remove_navmesh_edge_link(plugin, key_for(0x800), 1), ScalarMutationStatus::updated);
  EXPECT_EQ(remove_navmesh_edge_link(plugin, key_for(0x800), 5), ScalarMutationStatus::subrecord_missing);
}

TEST(NavmeshStructure, DoorAddAndRemoveValidateReferenceKinds) {
  auto plugin = structure_plugin();
  EXPECT_THROW(add_navmesh_door(plugin, key_for(0x800), {1, 0xABC, key_for(0x802)}), schema::ScalarError);
  EXPECT_THROW(add_navmesh_door(plugin, key_for(0x800), {4, 0xABC, key_for(0x812)}), schema::ScalarError);
  EXPECT_EQ(add_navmesh_door(plugin, key_for(0x800), {1, 0xABC, key_for(0x812)}), ScalarMutationStatus::updated);
  EXPECT_EQ(remove_navmesh_door(plugin, key_for(0x800), 0), ScalarMutationStatus::updated);
  EXPECT_EQ(remove_navmesh_door(plugin, key_for(0x800), 5), ScalarMutationStatus::subrecord_missing);
  expect_decoded(plugin, [](const env::NavmeshGeometry& g) {
    ASSERT_EQ(g.doors.size(), 1u);
    EXPECT_EQ(g.doors[0].crc, 0xABCu);
  });
}

TEST(NavmeshStructure, CoverRemovalRefusesMappedCoverAndAddValidatesVertices) {
  auto mapped = structure_plugin(true, false);
  EXPECT_THROW(remove_navmesh_cover(mapped, key_for(0x800), 0), schema::ScalarError);
  EXPECT_THROW(remove_navmesh_triangle(mapped, key_for(0x800), 0), schema::ScalarError);

  auto plugin = structure_plugin();
  EXPECT_THROW(add_navmesh_cover(plugin, key_for(0x800), {{9, 0}, {0, 0, 0, 0}}), schema::ScalarError);
  EXPECT_THROW(add_navmesh_cover(plugin, key_for(0x800), {{9, 0}, {1, 0, 0, 0}}), schema::ScalarError);
  EXPECT_EQ(add_navmesh_cover(plugin, key_for(0x800), {{4, 0}, {1, 0, 0, 0}}), ScalarMutationStatus::updated);
  EXPECT_EQ(remove_navmesh_cover(plugin, key_for(0x800), 1), ScalarMutationStatus::updated);
  EXPECT_EQ(remove_navmesh_cover(plugin, key_for(0x800), 0), ScalarMutationStatus::updated);
  EXPECT_EQ(remove_navmesh_cover(plugin, key_for(0x800), 0), ScalarMutationStatus::subrecord_missing);
}

// Detects: a cover must be removable once its cover triangle mappings are cleared.
TEST(NavmeshStructure, CoverMapEditsUnblockCoverRemoval) {
  auto plugin = structure_plugin(true, false);
  EXPECT_THROW(remove_navmesh_cover(plugin, key_for(0x800), 0), schema::ScalarError);
  EXPECT_THROW(add_navmesh_cover_map(plugin, key_for(0x800), {9, 0}), schema::ScalarError);
  EXPECT_THROW(add_navmesh_cover_map(plugin, key_for(0x800), {0, 9}), schema::ScalarError);
  EXPECT_EQ(add_navmesh_cover_map(plugin, key_for(0x800), {0, 1}), ScalarMutationStatus::updated);
  EXPECT_EQ(remove_navmesh_cover_map(plugin, key_for(0x800), 2), ScalarMutationStatus::subrecord_missing);
  EXPECT_EQ(remove_navmesh_cover_map(plugin, key_for(0x800), 0), ScalarMutationStatus::updated);
  EXPECT_EQ(remove_navmesh_cover_map(plugin, key_for(0x800), 0), ScalarMutationStatus::updated);
  EXPECT_EQ(remove_navmesh_cover(plugin, key_for(0x800), 0), ScalarMutationStatus::updated);
  expect_decoded(plugin, [](const env::NavmeshGeometry& g) { EXPECT_EQ(g.triangles.size(), 2u); });
}

TEST(NavmeshStructure, WaypointAddValidatesAndRemoveReparses) {
  auto plugin = structure_plugin();
  EXPECT_THROW(add_navmesh_waypoint(plugin, key_for(0x800),
                                    {{std::numeric_limits<float>::quiet_NaN(), 0, 0}, 0, 0}),
               schema::ScalarError);
  EXPECT_THROW(add_navmesh_waypoint(plugin, key_for(0x800), {{0, 0, 0}, 9, 0}), schema::ScalarError);
  EXPECT_EQ(add_navmesh_waypoint(plugin, key_for(0x800), {{1, 2, 3}, 1, 5}), ScalarMutationStatus::updated);
  EXPECT_EQ(remove_navmesh_waypoint(plugin, key_for(0x800), 1), ScalarMutationStatus::updated);
  EXPECT_EQ(remove_navmesh_waypoint(plugin, key_for(0x800), 1), ScalarMutationStatus::subrecord_missing);
  expect_decoded(plugin, [](const env::NavmeshGeometry& g) { EXPECT_EQ(g.triangles.size(), 2u); });
}

TEST(NavmeshStructure, MissingAndNonNavmeshRecordsRefuse) {
  auto plugin = structure_plugin();
  EXPECT_EQ(remove_navmesh_vertex(plugin, key_for(0x999), 0), ScalarMutationStatus::record_missing);
  EXPECT_THROW(add_navmesh_vertex(plugin, key_for(0x812), {0, 0, 0}), schema::ScalarError);
  EXPECT_EQ(remove_navmesh_cover(plugin, key_for(0x802), 0), ScalarMutationStatus::subrecord_missing);
}

TEST(NavmeshStructure, EdgeLinkTriangleIsDestinationIndexAndSurvivesLocalRemoval) {
  auto plugin = structure_plugin(false, false);
  EXPECT_EQ(add_navmesh_edge_link(plugin, key_for(0x800), {1, key_for(0x802), 5, 2}), ScalarMutationStatus::updated);
  EXPECT_EQ(add_navmesh_triangle(plugin, key_for(0x800), triangle({0, 1, 2}, {-1, -1, -1})), ScalarMutationStatus::updated);
  EXPECT_EQ(remove_navmesh_triangle(plugin, key_for(0x800), 2), ScalarMutationStatus::updated);
  expect_decoded(plugin, [](const env::NavmeshGeometry& g) {
    ASSERT_EQ(g.triangles.size(), 2u);
    ASSERT_EQ(g.edges.size(), 2u);
    EXPECT_EQ(g.edges.back().triangle, 5);
  });
}

// Detects: removing a triangle left another navmesh's edge link pointing one triangle off.
TEST(NavmeshStructure, TriangleRemovalRefusesWhileAnotherNavmeshLinksAtOrAboveIt) {
  auto plugin = structure_plugin(false, false);
  ASSERT_EQ(add_navmesh_triangle(plugin, key_for(0x800), triangle({0, 1, 2}, {-1, -1, -1})),
            ScalarMutationStatus::updated);
  ASSERT_EQ(add_navmesh_edge_link(plugin, key_for(0x802), {1, key_for(0x800), 2, 0}), ScalarMutationStatus::updated);
  const auto before = fo4::write_plugin(plugin);
  EXPECT_THROW(static_cast<void>(remove_navmesh_triangle(plugin, key_for(0x800), 2)), fo4::schema::ScalarError);
  EXPECT_EQ(fo4::write_plugin(plugin), before);
  ASSERT_EQ(remove_navmesh_edge_link(plugin, key_for(0x802), 0), ScalarMutationStatus::updated);
  EXPECT_EQ(remove_navmesh_triangle(plugin, key_for(0x800), 2), ScalarMutationStatus::updated);
}

TEST(NavmeshStructure, TriangleRemovalRefusesWhenAnotherNavmeshCannotBeRead) {
  auto plugin = unreadable_neighbor_plugin();
  const auto before = fo4::write_plugin(plugin);
  try {
    static_cast<void>(remove_navmesh_triangle(plugin, key_for(0x800), 0));
    FAIL() << "removal proceeded while another navmesh was unreadable";
  } catch (const schema::ScalarError& error) {
    EXPECT_STREQ(error.what(), "cannot remove a triangle while another navmesh in this plugin cannot be read");
  }
  EXPECT_EQ(fo4::write_plugin(plugin), before);
}

TEST(NavmeshStructure, TriangleRemovalRefusesWhenAnotherNavmeshCannotBeDecompressed) {
  auto plugin = unreadable_neighbor_plugin(true);
  try {
    static_cast<void>(remove_navmesh_triangle(plugin, key_for(0x800), 0));
    FAIL() << "removal proceeded while a compressed navmesh that may hold an inbound link was undecodable";
  } catch (const schema::ScalarError& error) {
    EXPECT_STREQ(error.what(), "cannot remove a triangle while another navmesh in this plugin cannot be read");
  }
}

TEST(NavmeshStructure, CountCapsRefuseAtTheIndexedWidth) {
  auto vertices_full = sized_plugin(65535, 0, 0);
  EXPECT_THROW(add_navmesh_vertex(vertices_full, key_for(0x800), {0, 0, 0}), schema::ScalarError);
  auto vertices_room = sized_plugin(65534, 0, 0);
  EXPECT_EQ(add_navmesh_vertex(vertices_room, key_for(0x800), {0, 0, 0}), ScalarMutationStatus::updated);

  auto triangles_full = sized_plugin(3, 32767, 0);
  EXPECT_THROW(add_navmesh_triangle(triangles_full, key_for(0x800), triangle({0, 1, 2}, {-1, -1, -1})),
               schema::ScalarError);
  auto triangles_room = sized_plugin(3, 32766, 0);
  EXPECT_EQ(add_navmesh_triangle(triangles_room, key_for(0x800), triangle({0, 1, 2}, {-1, -1, -1})),
            ScalarMutationStatus::updated);

  auto edges_full = sized_plugin(3, 0, 32767);
  EXPECT_THROW(add_navmesh_edge_link(edges_full, key_for(0x800), {0, key_for(0x802), 0, 0}), schema::ScalarError);
  auto edges_room = sized_plugin(3, 0, 32766);
  EXPECT_EQ(add_navmesh_edge_link(edges_room, key_for(0x800), {0, key_for(0x802), 0, 0}),
            ScalarMutationStatus::updated);
}

TEST(NavmeshStructure, RefusedAndMissingEditsLeaveBytesUnchanged) {
  auto plugin = structure_plugin();
  const auto before = write_plugin(plugin);
  EXPECT_THROW(remove_navmesh_vertex(plugin, key_for(0x800), 2), schema::ScalarError);
  EXPECT_THROW(remove_navmesh_triangle(plugin, key_for(0x800), 0), schema::ScalarError);
  EXPECT_EQ(remove_navmesh_door(plugin, key_for(0x800), 9), ScalarMutationStatus::subrecord_missing);
  EXPECT_EQ(write_plugin(plugin), before);
}

}
