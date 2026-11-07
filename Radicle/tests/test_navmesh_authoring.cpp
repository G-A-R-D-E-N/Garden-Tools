#include "navmesh_writer_fixture.hpp"
#include "fo4/c_api_navmesh.h"
#include "fo4/c_api_authoring.h"
#include "fo4/plugin/navmesh_writer.hpp"
#include "fo4/plugin/subrecord_authoring.hpp"
#include "fo4/plugin/visit.hpp"
#include "fo4/plugin/writer.hpp"
#include "fo4/schema/scalar.hpp"
#include "fo4/binary/cursor.hpp"

#include <gtest/gtest.h>
#include <limits>
#include <chrono>
#include <fstream>

namespace {
using namespace fo4;

TEST(NavmeshAuthoring, TypedEditsPatchOnlyTheirBytesAndReparse) {
  auto plugin = test::navmesh_writer_fixture();
  const auto before = *find_record(plugin, test::navmesh_key());
  const auto sibling = record_original_bytes(plugin, *find_record(plugin, test::navmesh_key(0x811)));
  const std::vector<std::byte> original(sibling.begin(), sibling.end());
  EXPECT_EQ(set_navmesh_vertex(plugin, test::navmesh_key(), 2, {9, 10, 11}), ScalarMutationStatus::updated);
  env::NavmeshTriangle triangle{{2, 1, 0}, {-1, -1, -1}, 6.5f, 0xAA, 0xABCD8012};
  EXPECT_EQ(set_navmesh_triangle(plugin, test::navmesh_key(), 0, triangle), ScalarMutationStatus::updated);
  EXPECT_EQ(set_navmesh_door(plugin, test::navmesh_key(), 0, {0, 0x87654321, test::navmesh_key(0x812)}), ScalarMutationStatus::updated);
  const auto bytes = write_plugin(plugin);
  const auto read = read_plugin(bytes, "Navigation.esp");
  const auto* after = find_record(read, test::navmesh_key()); ASSERT_NE(after, nullptr);
  const auto& data = after->subrecords[0].data;
  binary::Cursor cursor(data);
  cursor.read_bytes(44); EXPECT_EQ(cursor.read_f32(), 9); EXPECT_EQ(cursor.read_f32(), 10); EXPECT_EQ(cursor.read_f32(), 11);
  cursor.read_u32(); EXPECT_EQ(cursor.read_u16(), 2); EXPECT_EQ(cursor.read_u16(), 1); EXPECT_EQ(cursor.read_u16(), 0);
  cursor.read_bytes(6); EXPECT_EQ(cursor.read_f32(), 6.5f); EXPECT_EQ(cursor.read_u8(), 0xAA); EXPECT_EQ(cursor.read_u32(), 0xABCD8012u);
  cursor.read_bytes(8); EXPECT_EQ(cursor.read_u16(), 0); EXPECT_EQ(cursor.read_u32(), 0x87654321u); EXPECT_EQ(cursor.read_u32(), 0x812u);
  EXPECT_TRUE(std::equal(data.end() - 48, data.end(), before.subrecords[0].data.end() - 48));
  EXPECT_EQ(after->subrecords[1].data, before.subrecords[1].data);
  const auto untouched = record_original_bytes(read, *find_record(read, test::navmesh_key(0x811)));
  EXPECT_EQ(std::vector<std::byte>(untouched.begin(), untouched.end()), original);
  EXPECT_EQ(set_navmesh_triangle(plugin, test::navmesh_key(), 0, triangle), ScalarMutationStatus::unchanged);
}

TEST(NavmeshAuthoring, UneditedPluginWithNavmeshAndNaviRecordsWritesBackByteIdentical) {
  auto plugin = test::navmesh_writer_fixture();
  ASSERT_NE(find_record(plugin, test::navmesh_key()), nullptr);
  ASSERT_NE(find_record(plugin, test::navmesh_key(0x801)), nullptr);
  const auto original = test::original_bytes(plugin);
  ASSERT_FALSE(original.empty());
  plugin.original_range.clear();
  fo4::visit_records(plugin, [](auto& record) { record.original_range.clear(); });
  EXPECT_EQ(write_plugin(plugin), original);
}

TEST(NavmeshAuthoring, RefusalsKeepSerializedBytesIdentical) {
  auto plugin = test::navmesh_writer_fixture(); const auto before = write_plugin(plugin);
  EXPECT_EQ(set_navmesh_vertex(plugin, test::navmesh_key(), 3, {1, 2, 3}), ScalarMutationStatus::subrecord_missing);
  EXPECT_THROW(set_navmesh_vertex(plugin, test::navmesh_key(), 0, {std::numeric_limits<float>::infinity(), 2, 3}), schema::ScalarError);
  env::NavmeshTriangle triangle{{0, 1, 2}, {-1, -1, -1}, 5, 0xAB, 0};
  EXPECT_THROW(set_navmesh_triangle(plugin, test::navmesh_key(), 0, triangle), schema::ScalarError);
  triangle.traversal = 0xAA; triangle.flags = 0xABCD8092;
  EXPECT_THROW(set_navmesh_triangle(plugin, test::navmesh_key(), 0, triangle), schema::ScalarError);
  triangle.flags = 0xABCD8012; triangle.vertices[0] = 3;
  EXPECT_THROW(set_navmesh_triangle(plugin, test::navmesh_key(), 0, triangle), schema::ScalarError);
  EXPECT_THROW(set_navmesh_door(plugin, test::navmesh_key(), 0, {0, 1, test::navmesh_key(0x813)}), schema::ScalarError);
  EXPECT_THROW(set_navmesh_door(plugin, test::navmesh_key(), 0, {0, 1, {}}), schema::ScalarError);
  EXPECT_THROW(set_navmesh_door(plugin, test::navmesh_key(), 0, {0, 1, FormKey::parse("Missing.esm|00000800")}), FormIdError);
  EXPECT_EQ(write_plugin(plugin), before);
}

TEST(NavmeshAuthoring, TriangleFlagChangesRefuseWithoutChangingBytes) {
  for (std::uint32_t bit = 0; bit < 32; ++bit) {
    SCOPED_TRACE(bit);
    auto plugin = test::navmesh_writer_fixture();
    const auto before = write_plugin(plugin);
    env::NavmeshTriangle triangle{{0, 1, 2}, {-1, -1, -1}, 4.5f, 0xAA,
                                  0xABCD8012u ^ (std::uint32_t{1} << bit)};
    EXPECT_THROW(set_navmesh_triangle(plugin, test::navmesh_key(), 0, triangle), schema::ScalarError);
    EXPECT_EQ(write_plugin(plugin), before);
  }
}

TEST(NavmeshAuthoring, EdgeAndDoorIndicesOutsideTheirArraysRefuseWithoutChangingBytes) {
  auto plugin = test::navmesh_writer_fixture(); const auto before = write_plugin(plugin);
  const auto key = test::navmesh_key();
  env::NavmeshTriangle triangle{{2, 1, 0}, {-1, -1, -1}, 6.5f, 0xAA, 0xABCD8012};
  triangle.edges[0] = 1;
  EXPECT_THROW(set_navmesh_triangle(plugin, key, 0, triangle), schema::ScalarError);
  EXPECT_EQ(write_plugin(plugin), before);
  triangle.edges = {-2, -1, -1};
  EXPECT_THROW(set_navmesh_triangle(plugin, key, 0, triangle), schema::ScalarError);
  EXPECT_EQ(write_plugin(plugin), before);
  triangle.edges = {-1, 0, -1};
  EXPECT_THROW(set_navmesh_triangle(plugin, key, 0, triangle), schema::ScalarError);
  EXPECT_EQ(write_plugin(plugin), before);
  EXPECT_THROW(set_navmesh_door(plugin, key, 0, {1, 0x87654321, test::navmesh_key(0x812)}), schema::ScalarError);
  EXPECT_EQ(write_plugin(plugin), before);
  EXPECT_THROW(set_navmesh_door(plugin, key, 0, {-1, 0x87654321, test::navmesh_key(0x812)}), schema::ScalarError);
  EXPECT_EQ(write_plugin(plugin), before);
  triangle.edges = {0, -1, -1};
  EXPECT_EQ(set_navmesh_triangle(plugin, key, 0, triangle), ScalarMutationStatus::updated);
}

TEST(NavmeshAuthoring, FlaggedEdgeSlotRefusesMinusOneWithoutChangingBytes) {
  auto plugin = test::navmesh_writer_fixture(0xFFFF, 0); const auto before = write_plugin(plugin);
  env::NavmeshTriangle triangle{{2, 1, 0}, {-1, -1, -1}, 6.5f, 0xAA, 0xABCD8012};
  EXPECT_THROW(set_navmesh_triangle(plugin, test::navmesh_key(), 0, triangle), schema::ScalarError);
  EXPECT_EQ(write_plugin(plugin), before);
}

TEST(NavmeshAuthoring, UnchangedDanglingTriangleEdgeIsKeptAndChangedOneRefuses) {
  auto plugin = test::navmesh_writer_fixture(1);
  const auto key = test::navmesh_key();
  env::NavmeshTriangle triangle{{2, 1, 0}, {1, -1, -1}, 6.5f, 0xAA, 0xABCD8012};
  EXPECT_EQ(set_navmesh_triangle(plugin, key, 0, triangle), ScalarMutationStatus::updated);
  const auto bytes = write_plugin(plugin);
  const auto read = read_plugin(bytes, "Navigation.esp");
  binary::Cursor cursor(find_record(read, key)->subrecords[0].data);
  cursor.read_bytes(60 + 6); EXPECT_EQ(cursor.read_u16(), 1); EXPECT_EQ(cursor.read_u16(), 0xFFFF);
  triangle.edges[0] = 2;
  EXPECT_THROW(set_navmesh_triangle(plugin, key, 0, triangle), schema::ScalarError);
  EXPECT_EQ(write_plugin(plugin), bytes);
}

TEST(NavmeshAuthoring, MalformedDuplicateAndUnsupportedGeometryFailsClosed) {
  for (const auto mode : {0, 1, 2, 3}) {
    auto plugin = test::navmesh_writer_fixture();
    auto& record = std::get<Record>(plugin.groups[0].children[0]);
    if (mode == 0) record.subrecords.push_back(record.subrecords[0]);
    if (mode == 1) record.subrecords[0].data.pop_back();
    if (mode == 2) record.subrecords[0].data[0] = std::byte{16};
    if (mode == 3) record.header.form_version = 127;
    const auto before = write_plugin(plugin);
    EXPECT_THROW(set_navmesh_vertex(plugin, test::navmesh_key(), 0, {1, 2, 3}), schema::ScalarError);
    EXPECT_EQ(write_plugin(plugin), before);
  }
}

TEST(NavmeshAuthoring, CountedStructuresAndPlacementRefuseGenericAuthoring) {
  auto plugin = test::navmesh_writer_fixture(); const auto before = write_plugin(plugin);
  EXPECT_EQ(add_record_subrecord(plugin, test::navmesh_key(), Signature::from_string("NVNM")), SubrecordAddStatus::not_authorable);
  EXPECT_EQ(remove_record_subrecord(plugin, test::navmesh_key(), Signature::from_string("NVNM"), 0), SubrecordRemoveStatus::not_authorable);
  EXPECT_EQ(create_record(plugin, Signature::from_string("NAVM"), "new").status, RecordCreateStatus::not_authorable);
  EXPECT_EQ(create_record(plugin, Signature::from_string("NAVI"), "new").status, RecordCreateStatus::not_authorable);
  const schema::SubrecordAuthoringConstraint rules[] = {{Signature::from_string("EDID"), 0, 1}};
  EXPECT_EQ(create_record(plugin, Signature::from_string("NAVM"), "new", rules).status, RecordCreateStatus::not_authorable);
  EXPECT_EQ(write_plugin(plugin), before);
}

TEST(NavmeshAuthoring, CAbiExportsValidateInputsAndRoundtrip) {
  const auto source = write_plugin(test::navmesh_writer_fixture()); radicle_plugin* plugin = nullptr;
  const auto root = std::filesystem::temp_directory_path() /
      ("navmesh_authoring_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
  std::filesystem::create_directories(root); const auto path = root / "Navigation.esp";
  { std::ofstream file(path, std::ios::binary); file.write(reinterpret_cast<const char*>(source.data()), static_cast<std::streamsize>(source.size())); }
  ASSERT_EQ(radicle_plugin_read_file(path.string().c_str(), &plugin), RADICLE_STATUS_OK);
  const radicle_form_key key{"Navigation.esp", 0x800};
  const radicle_navmesh_vertex vertex{9, 10, 11};
  EXPECT_EQ(radicle_plugin_set_navmesh_vertex(plugin, key, 2, &vertex), RADICLE_STATUS_OK);
  radicle_navmesh_triangle triangle{{2, 1, 0}, {-1, -1, -1}, 6.5f, 0xAA, 0xABCD8012};
  EXPECT_EQ(radicle_plugin_set_navmesh_triangle(plugin, key, 0, &triangle), RADICLE_STATUS_OK);
  const radicle_navmesh_door_link door{0, 0x87654321, {"Navigation.esp", 0x812}};
  EXPECT_EQ(radicle_plugin_set_navmesh_door(plugin, key, 0, &door), RADICLE_STATUS_OK);
  std::uint8_t* snapshot = nullptr; std::size_t snapshot_size = 0;
  ASSERT_EQ(radicle_plugin_write_bytes(plugin, &snapshot, &snapshot_size), RADICLE_STATUS_OK);
  const std::vector<std::uint8_t> expected(snapshot, snapshot + snapshot_size);
  radicle_free_bytes(snapshot);
  for (std::uint32_t bit = 0; bit < 32; ++bit) {
    SCOPED_TRACE(bit);
    triangle.flags = 0xABCD8012u ^ (std::uint32_t{1} << bit);
    EXPECT_EQ(radicle_plugin_set_navmesh_triangle(plugin, key, 0, &triangle), RADICLE_STATUS_INVALID_ARGUMENT);
  }
  EXPECT_EQ(radicle_plugin_set_navmesh_vertex(plugin, key, 3, &vertex), RADICLE_STATUS_NOT_FOUND);
  EXPECT_EQ(radicle_plugin_set_navmesh_vertex(plugin, key, 0, nullptr), RADICLE_STATUS_INVALID_ARGUMENT);
  triangle.traversal = 0;
  EXPECT_EQ(radicle_plugin_set_navmesh_triangle(plugin, key, 0, &triangle), RADICLE_STATUS_INVALID_ARGUMENT);
  std::uint8_t* bytes = nullptr; std::size_t size = 0;
  ASSERT_EQ(radicle_plugin_write_bytes(plugin, &bytes, &size), RADICLE_STATUS_OK);
  EXPECT_EQ(std::vector<std::uint8_t>(bytes, bytes + size), expected);
  const auto parsed = read_plugin(std::as_bytes(std::span{bytes, size}), "Navigation.esp");
  const auto& data = find_record(parsed, test::navmesh_key())->subrecords[0].data;
  binary::Cursor c(data); c.read_bytes(44); EXPECT_EQ(c.read_f32(), 9);
  radicle_free_bytes(bytes); radicle_plugin_destroy(plugin);
  std::filesystem::remove_all(root);
}

}
