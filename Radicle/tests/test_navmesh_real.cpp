#include "fo4/c_api_navmesh.h"
#include "fo4/environment/navmesh.hpp"
#include "fo4/plugin/writer.hpp"
#include "real_data_build.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <bit>
#include <cstdlib>
#include <cstring>
#include <filesystem>

namespace {

class NavmeshReal : public testing::TestWithParam<const char*> {};

bool same_key(const fo4::FormKey& native, radicle_form_key value) {
  if (native.owner().empty()) return value.plugin == nullptr && value.local_id == 0;
  return value.plugin && fo4::PluginKey::from_filename(value.plugin) == native.owner() &&
         value.local_id == native.local().value();
}

void expect_parent(const fo4::env::NavmeshParent& native, const radicle_navmesh_parent& value) {
  EXPECT_EQ(value.crc, native.crc);
  EXPECT_TRUE(same_key(native.world, value.world)); EXPECT_TRUE(same_key(native.cell, value.cell));
  EXPECT_EQ(value.grid_y, native.grid_y); EXPECT_EQ(value.grid_x, native.grid_x);
}

bool same_float(float a, float b) {
  return std::bit_cast<std::uint32_t>(a) == std::bit_cast<std::uint32_t>(b);
}

bool same_vec3(const std::array<float, 3>& native, const radicle_navmesh_vertex& value) {
  return same_float(native[0], value.x) && same_float(native[1], value.y) && same_float(native[2], value.z);
}

void expect_geometry(const fo4::env::NavmeshGeometry& native, const radicle_navmesh& value) {
  EXPECT_EQ(value.version, native.version); expect_parent(native.parent, value.parent);
  ASSERT_EQ(value.vertex_count, native.vertices.size());
  EXPECT_TRUE(std::equal(native.vertices.begin(), native.vertices.end(), value.vertices, same_vec3));
  ASSERT_EQ(value.triangle_count, native.triangles.size());
  EXPECT_TRUE(std::equal(native.triangles.begin(), native.triangles.end(), value.triangles,
      [](const auto& a, const auto& b) {
        return std::equal(a.vertices.begin(), a.vertices.end(), b.vertices) &&
               std::equal(a.edges.begin(), a.edges.end(), b.edges) &&
               same_float(a.clearance, b.clearance) && a.traversal == b.traversal && a.flags == b.flags;
      }));
  ASSERT_EQ(value.edge_count, native.edges.size());
  EXPECT_TRUE(std::equal(native.edges.begin(), native.edges.end(), value.edges,
      [](const auto& a, const auto& b) {
        return a.type == b.type && same_key(a.navmesh, b.navmesh) && a.triangle == b.triangle && a.edge == b.edge;
      }));
  ASSERT_EQ(value.door_count, native.doors.size());
  EXPECT_TRUE(std::equal(native.doors.begin(), native.doors.end(), value.doors,
      [](const auto& a, const auto& b) {
        return a.triangle == b.triangle && a.crc == b.crc && same_key(a.door, b.door);
      }));
  ASSERT_EQ(value.opaque_tail_size, native.opaque_tail.size());
  EXPECT_TRUE(std::equal(native.opaque_tail.begin(), native.opaque_tail.end(), value.opaque_tail,
      [](std::byte a, std::uint8_t b) { return std::to_integer<std::uint8_t>(a) == b; }));
}

void expect_info(const fo4::env::NavmeshInfo& native, const radicle_navmesh_info& value) {
  EXPECT_TRUE(same_key(native.navmesh, value.navmesh)); EXPECT_EQ(value.flags, native.flags);
  EXPECT_TRUE(same_vec3(native.location, value.location)); EXPECT_TRUE(same_float(native.preferred, value.preferred));
  ASSERT_EQ(value.edge_count, native.edges.size());
  EXPECT_TRUE(std::equal(native.edges.begin(), native.edges.end(), value.edges, same_key));
  ASSERT_EQ(value.preferred_edge_count, native.preferred_edges.size());
  EXPECT_TRUE(std::equal(native.preferred_edges.begin(), native.preferred_edges.end(), value.preferred_edges, same_key));
  ASSERT_EQ(value.door_count, native.doors.size());
  EXPECT_TRUE(std::equal(native.doors.begin(), native.doors.end(), value.doors,
      [](const auto& a, const auto& b) { return a.crc == b.crc && same_key(a.door, b.door); }));
  EXPECT_EQ(value.has_island, native.has_island ? 1 : 0);
  ASSERT_EQ(value.island_size, native.island.size());
  EXPECT_TRUE(std::equal(native.island.begin(), native.island.end(), value.island,
      [](std::byte a, std::uint8_t b) { return std::to_integer<std::uint8_t>(a) == b; }));
  expect_parent(native.parent, value.parent);
}

TEST_P(NavmeshReal, EveryOfficialNavmeshDecodesAndUnchangedMasterIsIdentical) {
  const auto* dir = std::getenv("FO4_DATA_DIR");
  if (!dir) GTEST_SKIP() << "set FO4_DATA_DIR to installed Fallout 4 data";
  record_game_build(detect_game_build(dir));
  const auto path = std::filesystem::path{dir} / GetParam();
  if (!std::filesystem::exists(path)) GTEST_SKIP() << "missing official master: " << path;
  const std::filesystem::path native_paths[] = {path}; const auto env = fo4::env::Environment::load(native_paths);
  const auto name = path.string(); const char* paths[] = {name.c_str()}; radicle_env* abi = nullptr;
  ASSERT_EQ(radicle_env_load(paths, 1, &abi), RADICLE_STATUS_OK);
  const radicle_record** records = nullptr; std::size_t count = 0;
  ASSERT_EQ(radicle_env_plugin_records(abi, 0, &records, &count), RADICLE_STATUS_OK);
  const auto refs = env.plugin_records(0); ASSERT_EQ(count, refs.size());
  std::size_t navm = 0, navm_failed = 0, navi = 0, navi_failed = 0, infos = 0;
  for (std::size_t i = 0; i < count; ++i) {
    const auto type = refs[i]->record->header.signature;
    if (type == fo4::Signature::from_string("NAVM")) {
      SCOPED_TRACE(refs[i]->record->form_key.to_string()); ++navm;
      std::optional<fo4::env::NavmeshGeometry> native; bool native_ok = true;
      try { native = fo4::env::decode_navmesh(env, *refs[i]); } catch (...) { native_ok = false; }
      const radicle_navmesh* value = nullptr;
      const bool c_ok = radicle_navmesh_decode(abi, records[i], &value) == RADICLE_STATUS_OK && value != nullptr;
      if (!native_ok || !c_ok) ++navm_failed;
      if (native_ok && c_ok) {
        EXPECT_EQ(value->has_geometry, native ? 1 : 0);
        if (native) { expect_geometry(*native, *value); }
      }
      if (value) radicle_navmesh_free(value);
    } else if (type == fo4::Signature::from_string("NAVI")) {
      SCOPED_TRACE(refs[i]->record->form_key.to_string()); ++navi;
      std::vector<fo4::env::NavmeshInfo> native; bool native_ok = true;
      try { native = fo4::env::decode_navmesh_info(env, *refs[i]); } catch (...) { native_ok = false; }
      const radicle_navi* value = nullptr;
      const bool c_ok = radicle_navi_decode(abi, records[i], &value) == RADICLE_STATUS_OK && value != nullptr &&
                        value->info_count == native.size();
      if (!native_ok || !c_ok) ++navi_failed;
      if (native_ok && c_ok) {
        infos += native.size();
        for (std::size_t j = 0; j < native.size(); ++j) { SCOPED_TRACE(j); expect_info(native[j], value->infos[j]); }
      }
      if (value) radicle_navi_free(value);
    }
  }
  radicle_free_record_array(records); radicle_env_destroy(abi);
  RecordProperty("NAVM", static_cast<int>(navm)); RecordProperty("NAVM_FAILED", static_cast<int>(navm_failed));
  RecordProperty("NAVI", static_cast<int>(navi)); RecordProperty("NAVI_FAILED", static_cast<int>(navi_failed));
  RecordProperty("NVMI", static_cast<int>(infos));
  EXPECT_EQ(navm_failed, 0u); EXPECT_EQ(navi_failed, 0u);
  const auto bytes = fo4::write_plugin(env.plugin(0)); const auto original = fo4::plugin_original_bytes(env.plugin(0));
  ASSERT_EQ(bytes.size(), original.size()); EXPECT_EQ(std::memcmp(bytes.data(), original.data(), bytes.size()), 0);
}

INSTANTIATE_TEST_SUITE_P(OfficialMasters, NavmeshReal,
    testing::Values("Fallout4.esm", "DLCRobot.esm", "DLCworkshop01.esm", "DLCCoast.esm", "DLCworkshop02.esm", "DLCworkshop03.esm", "DLCNukaWorld.esm"));

}
