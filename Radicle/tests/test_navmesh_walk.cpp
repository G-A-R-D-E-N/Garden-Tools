#include "fo4/c_api_navmesh.h"
#include "fo4/environment/navmesh_path.hpp"
#include "fo4/environment/navmesh_walk.hpp"
#include "plugin_bytes.hpp"

#include <gtest/gtest.h>

#include <array>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <limits>
#include <string>
#include <vector>

namespace {

using fo4::test::ByteWriter;

struct Tri {
  std::array<std::uint16_t, 3> vertices;
  std::array<std::uint16_t, 3> edges;
  std::uint8_t traversal;
  std::uint32_t flags;
};

struct Link {
  std::uint32_t type;
  std::uint32_t navmesh;
  std::uint16_t triangle;
  std::uint8_t edge;
};

struct DoorRow {
  std::uint16_t triangle;
  std::uint32_t door;
};

struct Mesh {
  std::vector<std::array<float, 3>> vertices;
  std::vector<Tri> triangles;
  std::vector<Link> links;
  std::vector<DoorRow> doors;
  bool exterior{false};
};

ByteWriter encode(const Mesh& mesh) {
  ByteWriter b;
  b.u32(15); b.u32(0x12345678); b.u32(mesh.exterior ? 0x810 : 0);
  if (mesh.exterior) { b.u16(0xFFFE); b.u16(7); } else b.u32(0x811);
  b.u32(static_cast<std::uint32_t>(mesh.vertices.size()));
  for (const auto& v : mesh.vertices) { b.f32(v[0]); b.f32(v[1]); b.f32(v[2]); }
  b.u32(static_cast<std::uint32_t>(mesh.triangles.size()));
  for (const auto& t : mesh.triangles) {
    for (const auto v : t.vertices) b.u16(v);
    for (const auto e : t.edges) b.u16(e);
    b.f32(0); b.u8(t.traversal); b.u32(t.flags);
  }
  b.u32(static_cast<std::uint32_t>(mesh.links.size()));
  for (const auto& l : mesh.links) { b.u32(l.type); b.u32(l.navmesh); b.u16(l.triangle); b.u8(l.edge); }
  b.u32(static_cast<std::uint32_t>(mesh.doors.size()));
  for (const auto& d : mesh.doors) { b.u16(d.triangle); b.u32(0xE48B73F3); b.u32(d.door); }
  for (int i = 0; i < 4; ++i) b.u32(0);
  for (int i = 0; i < 8; ++i) b.f32(0);
  return b;
}

constexpr std::uint16_t k_none = 0xFFFF;
constexpr float k_scale = fo4::env::k_walk_metres_per_unit;

Mesh mesh_a() {
  Mesh m;
  m.exterior = true;
  m.vertices = {{0, 0, 0}, {100, 0, 0}, {0, 100, 0}, {100, 100, 0}};
  m.triangles = {
      {{0, 1, 2}, {0, 1, 3}, 0x01, 0x7u},
      {{1, 3, 2}, {k_none, 2, 4}, 0x04, 0x6u},
      {{0, 1, 9}, {k_none, k_none, k_none}, 0x01, 0},
  };
  m.links = {{0, 0x801, 1, 0}, {0, 0x800, 0, 2}, {1, 0x801, 1, 0}, {0, 0x8FF, 0, 0}, {0, 0x800, 2, 0}};
  m.doors = {{0, 0x810}, {1, 0x810}, {0, 0x810}, {1, 0x811}, {2, 0x811}, {7, 0x812}, {0, 0}};
  return m;
}

Mesh mesh_b() {
  Mesh m;
  m.vertices = {{100, 0, 0}, {0, 0, 0}, {0, -100, 0}, {0, 100, 0}};
  m.triangles = {
      {{0, 1, 3}, {k_none, k_none, k_none}, 0, 0},
      {{0, 1, 2}, {k_none, k_none, k_none}, 0, 0},
  };
  return m;
}

Mesh mesh_fan() {
  Mesh m;
  m.vertices = {{0, 0, 0}, {100, 0, 0}, {50, 86.6f, 0}, {-50, 86.6f, 0}, {-100, 0, 0}, {-50, -86.6f, 0}, {50, -86.6f, 0}};
  m.triangles = {
      {{0, 1, 2}, {5, 1, 1}, 0, 0x2u},
      {{0, 2, 3}, {0, k_none, 2}, 0x01, 0},
      {{0, 3, 4}, {1, k_none, 3}, 0, 0},
      {{0, 4, 5}, {2, 0, 4}, 0, 0x2u},
      {{0, 5, 6}, {3, k_none, 5}, 0, 0},
      {{0, 6, 1}, {4, k_none, 0}, 0, 0},
  };
  m.links = {{0, 0x804, 0, 0}, {1, 0x804, 1, 1}};
  m.doors = {{1, 0x830}, {5, 0x831}};
  return m;
}

Mesh mesh_annex() {
  Mesh m;
  m.vertices = {{-100, 0, 0}, {-50, -86.6f, 0}, {-161.6f, -93.3f, 0}, {-100, -170, 0}};
  m.triangles = {
      {{0, 1, 2}, {0, 1, k_none}, 0, 0x1u},
      {{2, 1, 3}, {0, k_none, k_none}, 0, 0},
  };
  m.links = {{0, 0x803, 3, 1}};
  return m;
}

ByteWriter race_data(std::size_t size, std::uint32_t flags2) {
  ByteWriter b;
  b.bytes.resize(size);
  if (size >= 92) {
    b.bytes[88] = std::byte{static_cast<std::uint8_t>(flags2)};
    b.bytes[89] = std::byte{static_cast<std::uint8_t>(flags2 >> 8)};
    b.bytes[90] = std::byte{static_cast<std::uint8_t>(flags2 >> 16)};
    b.bytes[91] = std::byte{static_cast<std::uint8_t>(flags2 >> 24)};
  }
  return b;
}

radicle_form_key key(std::uint32_t id) { return {"Walk.esm", id}; }

class NavmeshWalk : public testing::Test {
protected:
  void SetUp() override {
    root = std::filesystem::temp_directory_path() /
        ("radicle_walk_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::create_directories(root);
    path = root / "Walk.esm";
    ByteWriter out;
    auto r = fo4::test::begin_record(out, "TES4", 1, 0);
    ByteWriter h; h.f32(1); h.u32(4); h.u32(0x900);
    fo4::test::write_subrecord(out, "HEDR", h.bytes); fo4::test::end_record(out, r);
    auto g = fo4::test::begin_group(out, "NAVM", 0);
    r = fo4::test::begin_record(out, "NAVM", 0, 0x800);
    fo4::test::write_subrecord(out, "NVNM", encode(mesh_a()).bytes); fo4::test::end_record(out, r);
    r = fo4::test::begin_record(out, "NAVM", 0, 0x801);
    fo4::test::write_subrecord(out, "NVNM", encode(mesh_b()).bytes); fo4::test::end_record(out, r);
    r = fo4::test::begin_record(out, "NAVM", 0, 0x802);
    fo4::test::end_record(out, r);
    r = fo4::test::begin_record(out, "NAVM", 0, 0x803);
    fo4::test::write_subrecord(out, "NVNM", encode(mesh_fan()).bytes); fo4::test::end_record(out, r);
    r = fo4::test::begin_record(out, "NAVM", 0, 0x804);
    fo4::test::write_subrecord(out, "NVNM", encode(mesh_annex()).bytes); fo4::test::end_record(out, r);
    fo4::test::end_group(out, g);
    g = fo4::test::begin_group(out, "REFR", 0);
    r = fo4::test::begin_record(out, "REFR", 0, 0x830);
    ByteWriter name; name.u32(0x900);
    ByteWriter lock; lock.u8(255); lock.u8(0); lock.u8(0); lock.u8(0); lock.u32(0x832); lock.u8(0); lock.bytes.resize(16);
    fo4::test::write_subrecord(out, "NAME", name.bytes); fo4::test::write_subrecord(out, "XLOC", lock.bytes);
    fo4::test::end_record(out, r);
    fo4::test::end_group(out, g);
    g = fo4::test::begin_group(out, "RACE", 0);
    const auto add_race = [&](std::uint32_t id, const ByteWriter& data) {
      const auto record_offset = fo4::test::begin_record(out, "RACE", 0, id);
      fo4::test::write_subrecord(out, "DATA", data.bytes); fo4::test::end_record(out, record_offset);
    };
    add_race(0x820, race_data(124, 0x800));
    add_race(0x821, race_data(124, 0x7FF));
    add_race(0x822, race_data(40, 0));
    r = fo4::test::begin_record(out, "RACE", 0, 0x823);
    fo4::test::end_record(out, r);
    fo4::test::end_group(out, g);
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    file.write(reinterpret_cast<const char*>(out.bytes.data()), static_cast<std::streamsize>(out.bytes.size()));
  }
  void TearDown() override { std::error_code error; std::filesystem::remove_all(root, error); }

  fo4::env::Environment load() {
    const std::filesystem::path paths[] = {path};
    return fo4::env::Environment::load(paths);
  }
  static const fo4::env::RecordRef& record(const fo4::env::Environment& env, std::uint32_t id) {
    return *env.winner(fo4::FormKey{fo4::PluginKey::from_filename("Walk.esm"), fo4::LocalFormId{id}});
  }
  radicle_env* open_env() {
    const auto name = path.string(); const char* paths[] = {name.c_str()};
    radicle_env* env = nullptr;
    EXPECT_EQ(radicle_env_load(paths, 1, &env), RADICLE_STATUS_OK);
    return env;
  }

  std::filesystem::path root, path;
};

void expect_point(const std::array<float, 3>& value, float x, float y, float z) {
  EXPECT_NEAR(value[0], x, 1e-4f);
  EXPECT_NEAR(value[1], y, 1e-4f);
  EXPECT_NEAR(value[2], z, 1e-4f);
}

TEST_F(NavmeshWalk, ViewConvertsAxesAndScaleAndFillsClassesAndSkipsBadTriangles) {
  const auto env = load();
  const auto view = fo4::env::build_walk_view(env, record(env, 0x800), 0.1f);
  ASSERT_TRUE(view);
  EXPECT_EQ(view->navmesh.local().value(), 0x800u);
  EXPECT_TRUE(view->has_grid);
  EXPECT_EQ(view->grid_x, 7);
  EXPECT_EQ(view->grid_y, -2);
  ASSERT_EQ(view->vertices.size(), 4u);
  expect_point(view->vertices[1], 100 * k_scale, 0, 0);
  expect_point(view->vertices[2], 0, 0, -100 * k_scale);
  ASSERT_EQ(view->triangles.size(), 3u);
  EXPECT_EQ(view->triangles[1], (std::array<std::uint32_t, 3>{1, 3, 2}));
  EXPECT_EQ(view->triangles[2], (std::array<std::uint32_t, 3>{0, 0, 0}));
  EXPECT_EQ(view->triangle_class, (std::vector<std::uint32_t>{0x01, 0x04, 0}));
  EXPECT_EQ(view->skipped_triangles, 1u);

  const auto interior = fo4::env::build_walk_view(env, record(env, 0x801), 0.1f);
  ASSERT_TRUE(interior);
  EXPECT_FALSE(interior->has_grid);
  EXPECT_FALSE(fo4::env::build_walk_view(env, record(env, 0x802), 0.1f));
}

TEST_F(NavmeshWalk, LinksAreResolvedInsetAndCarryTheEdgeVerdict) {
  const auto env = load();
  const auto view = fo4::env::build_walk_view(env, record(env, 0x800), 0.1f);
  ASSERT_TRUE(view);
  ASSERT_EQ(view->links.size(), 3u);

  const auto& first = view->links[0];
  EXPECT_EQ(first.source_triangle, 0u);
  EXPECT_EQ(first.source_edge, 0u);
  EXPECT_EQ(first.target_navmesh.local().value(), 0x801u);
  EXPECT_EQ(first.target_triangle, 1u);
  EXPECT_EQ(first.target_edge, 0u);
  EXPECT_EQ(first.verdict, fo4::env::PathVerdict::passable);
  expect_point(first.start, 50 * k_scale - 0.044721f, 0, -0.089443f);
  expect_point(first.end, 50 * k_scale - 0.044721f, 0, 0.089443f);

  const auto& own = view->links[1];
  EXPECT_EQ(own.source_triangle, 0u);
  EXPECT_EQ(own.source_edge, 1u);
  EXPECT_EQ(own.target_navmesh.local().value(), 0x800u);
  EXPECT_EQ(own.target_triangle, 0u);
  EXPECT_EQ(own.target_edge, 2u);
  EXPECT_EQ(own.verdict, fo4::env::PathVerdict::passable);

  const auto& typed = view->links[2];
  EXPECT_EQ(typed.source_triangle, 1u);
  EXPECT_EQ(typed.source_edge, 1u);
  EXPECT_EQ(typed.target_triangle, 1u);
  EXPECT_EQ(typed.verdict, fo4::env::PathVerdict::unknown);
}

TEST_F(NavmeshWalk, InsetIsClampedToHalfWayToTheCentroid) {
  const auto env = load();
  const auto view = fo4::env::build_walk_view(env, record(env, 0x800), 100.0f);
  ASSERT_TRUE(view);
  ASSERT_FALSE(view->links.empty());
  expect_point(view->links[0].start, (50 - 8.33333f) * k_scale, 0, -16.66667f * k_scale);
  expect_point(view->links[0].end, (50 - 8.33333f) * k_scale, 0, 16.66667f * k_scale);
}

TEST_F(NavmeshWalk, DoorRegionsGroupValidTrianglesByDoorKeyAndDropTheRest) {
  const auto env = load();
  const auto view = fo4::env::build_walk_view(env, record(env, 0x800), 0.1f);
  ASSERT_TRUE(view);
  ASSERT_EQ(view->doors.size(), 2u);
  EXPECT_EQ(view->doors[0].door.local().value(), 0x810u);
  EXPECT_EQ(view->doors[0].triangles, (std::vector<std::uint32_t>{0, 1}));
  EXPECT_EQ(view->doors[1].door.local().value(), 0x811u);
  EXPECT_EQ(view->doors[1].triangles, (std::vector<std::uint32_t>{1}));
}

TEST_F(NavmeshWalk, AbiViewMatchesTheCoreAndRefusesBadArguments) {
  const auto env = load();
  const auto core = fo4::env::build_walk_view(env, record(env, 0x800), 0.1f);
  ASSERT_TRUE(core);
  radicle_env* abi = open_env();
  ASSERT_NE(abi, nullptr);
  const radicle_record* navm = nullptr;
  ASSERT_EQ(radicle_env_winner(abi, key(0x800), &navm), RADICLE_STATUS_OK);
  const radicle_walk_view* view = nullptr;
  ASSERT_EQ(radicle_navmesh_walk_view(abi, navm, 0.1f, &view), RADICLE_STATUS_OK);
  ASSERT_NE(view, nullptr);
  EXPECT_EQ(view->has_geometry, 1);
  EXPECT_EQ(view->navmesh.local_id, 0x800u);
  EXPECT_EQ(view->has_grid, 1);
  EXPECT_EQ(view->grid_x, 7);
  EXPECT_EQ(view->grid_y, -2);
  EXPECT_FLOAT_EQ(view->metres_per_unit, k_scale);
  ASSERT_EQ(view->vertex_count, 4u);
  EXPECT_FLOAT_EQ(view->vertices[2].z, core->vertices[2][2]);
  ASSERT_EQ(view->triangle_count, 3u);
  EXPECT_EQ(view->triangles[3], 1u);
  EXPECT_EQ(view->triangles[4], 3u);
  EXPECT_EQ(view->triangle_class[0], 0x01u);
  EXPECT_EQ(view->skipped_triangle_count, 1u);
  ASSERT_EQ(view->link_count, 3u);
  EXPECT_EQ(view->links[0].verdict, RADICLE_PATH_PASSABLE);
  EXPECT_EQ(view->links[0].target_navmesh.local_id, 0x801u);
  EXPECT_EQ(view->links[0].target_triangle, 1u);
  EXPECT_EQ(view->links[2].verdict, RADICLE_PATH_UNKNOWN);
  EXPECT_FLOAT_EQ(view->links[0].start.x, core->links[0].start[0]);
  ASSERT_EQ(view->door_count, 2u);
  EXPECT_EQ(view->doors[0].door.local_id, 0x810u);
  EXPECT_EQ(view->doors[0].first_triangle, 0u);
  EXPECT_EQ(view->doors[0].triangle_count, 2u);
  EXPECT_EQ(view->doors[1].first_triangle, 2u);
  ASSERT_EQ(view->door_triangle_count, 3u);
  EXPECT_EQ(view->door_triangles[2], 1u);
  radicle_navmesh_walk_view_free(view);
  radicle_navmesh_walk_view_free(nullptr);

  const radicle_record* empty = nullptr;
  ASSERT_EQ(radicle_env_winner(abi, key(0x802), &empty), RADICLE_STATUS_OK);
  ASSERT_EQ(radicle_navmesh_walk_view(abi, empty, 0.1f, &view), RADICLE_STATUS_OK);
  EXPECT_EQ(view->has_geometry, 0);
  EXPECT_EQ(view->vertex_count, 0u);
  radicle_navmesh_walk_view_free(view);

  view = nullptr;
  EXPECT_EQ(radicle_navmesh_walk_view(nullptr, navm, 0.1f, &view), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_navmesh_walk_view(abi, nullptr, 0.1f, &view), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_navmesh_walk_view(abi, navm, 0.1f, nullptr), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_navmesh_walk_view(abi, navm, -1.0f, &view), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_navmesh_walk_view(abi, navm, std::numeric_limits<float>::quiet_NaN(), &view),
            RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(view, nullptr);
  radicle_env* other = open_env();
  EXPECT_EQ(radicle_navmesh_walk_view(other, navm, 0.1f, &view), RADICLE_STATUS_INVALID_ARGUMENT);
  radicle_env_destroy(other);
  radicle_env_destroy(abi);
}

TEST_F(NavmeshWalk, AgentProfileReadsTheLargePathingFlagFromTheRaceAndDefaultsWithoutOne) {
  const auto env = load();
  const auto fallback = fo4::env::agent_profile(env, std::nullopt);
  ASSERT_TRUE(fallback);
  EXPECT_NEAR(fallback->radius, 35 * k_scale, 1e-6f);
  EXPECT_NEAR(fallback->height, 128 * k_scale, 1e-6f);
  EXPECT_FALSE(fallback->large);
  EXPECT_EQ(fallback->forbidden_class, 0u);

  const auto race_key = [](std::uint32_t id) {
    return fo4::FormKey{fo4::PluginKey::from_filename("Walk.esm"), fo4::LocalFormId{id}};
  };
  const auto large = fo4::env::agent_profile(env, race_key(0x820));
  ASSERT_TRUE(large);
  EXPECT_TRUE(large->large);
  EXPECT_EQ(large->forbidden_class, fo4::env::k_walk_class_large_forbidden);
  const auto normal = fo4::env::agent_profile(env, race_key(0x821));
  ASSERT_TRUE(normal);
  EXPECT_FALSE(normal->large);
  EXPECT_EQ(normal->forbidden_class, 0u);
  EXPECT_FALSE(fo4::env::agent_profile(env, race_key(0x800)));
  EXPECT_FALSE(fo4::env::agent_profile(env, race_key(0x9FF)));
  EXPECT_THROW(fo4::env::agent_profile(env, race_key(0x822)), fo4::env::WorldDecodeError);
  EXPECT_THROW(fo4::env::agent_profile(env, race_key(0x823)), fo4::env::WorldDecodeError);

  const auto view = fo4::env::build_walk_view(env, record(env, 0x800), 0.1f);
  ASSERT_TRUE(view);
  EXPECT_NE(view->triangle_class[0] & large->forbidden_class, 0u);
  EXPECT_EQ(view->triangle_class[1] & large->forbidden_class, 0u);
  EXPECT_EQ(view->triangle_class[0] & normal->forbidden_class, 0u);
}

TEST_F(NavmeshWalk, AgentProfileThroughTheAbiReportsStatusPerCase) {
  radicle_env* abi = open_env();
  ASSERT_NE(abi, nullptr);
  radicle_agent_profile profile{};
  ASSERT_EQ(radicle_walk_agent_profile(abi, radicle_form_key{}, &profile), RADICLE_STATUS_OK);
  EXPECT_EQ(profile.large, 0);
  EXPECT_FLOAT_EQ(profile.radius, 35 * k_scale);
  EXPECT_FLOAT_EQ(profile.height, 128 * k_scale);
  ASSERT_EQ(radicle_walk_agent_profile(abi, key(0x820), &profile), RADICLE_STATUS_OK);
  EXPECT_EQ(profile.large, 1);
  EXPECT_EQ(profile.forbidden_class, fo4::env::k_walk_class_large_forbidden);
  ASSERT_EQ(radicle_walk_agent_profile(abi, key(0x821), &profile), RADICLE_STATUS_OK);
  EXPECT_EQ(profile.large, 0);
  EXPECT_EQ(profile.forbidden_class, 0u);
  EXPECT_EQ(radicle_walk_agent_profile(abi, key(0x800), &profile), RADICLE_STATUS_NOT_FOUND);
  EXPECT_EQ(radicle_walk_agent_profile(abi, key(0x822), &profile), RADICLE_STATUS_PARSE_ERROR);
  EXPECT_EQ(radicle_walk_agent_profile(abi, radicle_form_key{"Walk.esm", 0x7FFFFFFF}, &profile),
            RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_walk_agent_profile(nullptr, key(0x820), &profile), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_walk_agent_profile(abi, key(0x820), nullptr), RADICLE_STATUS_INVALID_ARGUMENT);
  radicle_env_destroy(abi);
}

using fo4::env::NavmeshPathStatus;

fo4::FormKey core_key(std::uint32_t id) {
  return fo4::FormKey{fo4::PluginKey::from_filename("Walk.esm"), fo4::LocalFormId{id}};
}

std::vector<std::uint32_t> triangles_of(const fo4::env::NavmeshPath& path) {
  std::vector<std::uint32_t> out;
  for (const auto& node : path.nodes) out.push_back(node.triangle);
  return out;
}

constexpr float k_hop = 57.735f;

TEST_F(NavmeshWalk, PathTakesTheShorterWayRoundTheFanAndHonoursTheTraversalClassFilter) {
  const auto env = load();
  fo4::env::NavmeshPathOptions options;
  const auto direct = fo4::env::find_navmesh_path(env, core_key(0x803), 0, core_key(0x803), 2, options);
  ASSERT_EQ(direct.status, NavmeshPathStatus::found);
  EXPECT_EQ(triangles_of(direct), (std::vector<std::uint32_t>{0, 1, 2}));
  EXPECT_NEAR(direct.cost, 2 * k_hop, 0.05f);

  const auto same = fo4::env::find_navmesh_path(env, core_key(0x803), 4, core_key(0x803), 4, options);
  ASSERT_EQ(same.status, NavmeshPathStatus::found);
  EXPECT_EQ(triangles_of(same), (std::vector<std::uint32_t>{4}));
  EXPECT_FLOAT_EQ(same.cost, 0.0f);

  options.forbidden_class = 0x1;
  const auto around = fo4::env::find_navmesh_path(env, core_key(0x803), 0, core_key(0x803), 2, options);
  ASSERT_EQ(around.status, NavmeshPathStatus::found);
  EXPECT_EQ(triangles_of(around), (std::vector<std::uint32_t>{0, 5, 4, 3, 2}));
  EXPECT_NEAR(around.cost, 4 * k_hop, 0.1f);
  const auto forbidden_goal = fo4::env::find_navmesh_path(env, core_key(0x803), 0, core_key(0x803), 1, options);
  EXPECT_EQ(forbidden_goal.status, NavmeshPathStatus::no_path);
  EXPECT_TRUE(forbidden_goal.nodes.empty());
}

TEST_F(NavmeshWalk, PathRefusesUnresolvedEndpointsAndMalformedTriangles) {
  const auto env = load();
  const fo4::env::NavmeshPathOptions options;
  EXPECT_EQ(fo4::env::find_navmesh_path(env, core_key(0x803), 6, core_key(0x803), 0, options).status,
            NavmeshPathStatus::invalid_endpoint);
  EXPECT_EQ(fo4::env::find_navmesh_path(env, core_key(0x803), 0, core_key(0x803), 6, options).status,
            NavmeshPathStatus::invalid_endpoint);
  EXPECT_EQ(fo4::env::find_navmesh_path(env, core_key(0x9FF), 0, core_key(0x803), 0, options).status,
            NavmeshPathStatus::invalid_endpoint);
  EXPECT_EQ(fo4::env::find_navmesh_path(env, core_key(0x802), 0, core_key(0x803), 0, options).status,
            NavmeshPathStatus::invalid_endpoint);
  EXPECT_EQ(fo4::env::find_navmesh_path(env, core_key(0x800), 2, core_key(0x800), 0, options).status,
            NavmeshPathStatus::invalid_endpoint);
}

TEST_F(NavmeshWalk, PathDoorsFollowTheCallerStateAndLockAndUnknownDoorsStayClosedByDefault) {
  const auto env = load();
  fo4::env::NavmeshPathOptions options;
  options.door_states = {{core_key(0x830), {true, false}}};
  const auto detour = fo4::env::find_navmesh_path(env, core_key(0x803), 0, core_key(0x803), 2, options);
  ASSERT_EQ(detour.status, NavmeshPathStatus::found);
  EXPECT_EQ(triangles_of(detour), (std::vector<std::uint32_t>{0, 5, 4, 3, 2}));
  for (const auto& node : detour.nodes) EXPECT_FALSE(node.needs_opening);

  options.door_states = {{core_key(0x830), {true, true}}};
  const auto opened = fo4::env::find_navmesh_path(env, core_key(0x803), 0, core_key(0x803), 2, options);
  ASSERT_EQ(opened.status, NavmeshPathStatus::found);
  EXPECT_EQ(triangles_of(opened), (std::vector<std::uint32_t>{0, 1, 2}));
  EXPECT_TRUE(opened.nodes[1].needs_opening);
  EXPECT_FALSE(opened.nodes[0].needs_opening);

  options.door_states = {{core_key(0x830), {true, false}}, {core_key(0x831), {true, true}}};
  const auto shut = fo4::env::find_navmesh_path(env, core_key(0x803), 0, core_key(0x803), 2, options);
  EXPECT_EQ(shut.status, NavmeshPathStatus::no_path);
  options.allow_unknown_doors = true;
  const auto through_unknown = fo4::env::find_navmesh_path(env, core_key(0x803), 0, core_key(0x803), 2, options);
  ASSERT_EQ(through_unknown.status, NavmeshPathStatus::found);
  EXPECT_EQ(triangles_of(through_unknown), (std::vector<std::uint32_t>{0, 5, 4, 3, 2}));

  options = {};
  options.door_states = {{core_key(0x830), {true, false}}};
  const auto blocked_start = fo4::env::find_navmesh_path(env, core_key(0x803), 1, core_key(0x803), 3, options);
  EXPECT_EQ(blocked_start.status, NavmeshPathStatus::no_path);
}

TEST_F(NavmeshWalk, PathCrossesEdgeLinksOnlyWhenTheTypeIsKnownUnlessAskedOtherwise) {
  const auto env = load();
  fo4::env::NavmeshPathOptions options;
  options.door_states = {{core_key(0x831), {true, false}}};
  const auto portal = fo4::env::find_navmesh_path(env, core_key(0x803), 0, core_key(0x804), 1, options);
  ASSERT_EQ(portal.status, NavmeshPathStatus::found);
  EXPECT_EQ(triangles_of(portal), (std::vector<std::uint32_t>{0, 1, 2, 3, 0, 1}));
  EXPECT_EQ(portal.nodes[3].navmesh, core_key(0x803));
  EXPECT_EQ(portal.nodes[4].navmesh, core_key(0x804));
  EXPECT_TRUE(portal.nodes[4].via_link);
  EXPECT_FALSE(portal.nodes[5].via_link);
  EXPECT_FALSE(portal.nodes[3].via_link);

  options.allow_unknown_links = true;
  const auto shortcut = fo4::env::find_navmesh_path(env, core_key(0x803), 0, core_key(0x804), 1, options);
  ASSERT_EQ(shortcut.status, NavmeshPathStatus::found);
  EXPECT_EQ(triangles_of(shortcut), (std::vector<std::uint32_t>{0, 1}));
  EXPECT_TRUE(shortcut.nodes[1].via_link);
  EXPECT_LT(shortcut.cost, portal.cost);

  const auto back = fo4::env::find_navmesh_path(env, core_key(0x804), 1, core_key(0x803), 3, fo4::env::NavmeshPathOptions{});
  ASSERT_EQ(back.status, NavmeshPathStatus::found);
  EXPECT_EQ(triangles_of(back), (std::vector<std::uint32_t>{1, 0, 3}));
}

TEST_F(NavmeshWalk, PathThroughTheAbiSizesTheBufferZeroesOutputsAndReportsStatusPerCase) {
  radicle_env* abi = open_env();
  ASSERT_NE(abi, nullptr);
  size_t count = 99;
  int32_t found = 7;
  float cost = 5.0f;
  ASSERT_EQ(radicle_navmesh_find_path(abi, key(0x803), 0, key(0x803), 2, nullptr, nullptr, 0, &count, &found, &cost),
            RADICLE_STATUS_OK);
  EXPECT_EQ(count, 3u);
  EXPECT_EQ(found, 1);
  EXPECT_NEAR(cost, 2 * k_hop, 0.05f);

  radicle_navmesh_path_node nodes[2];
  for (auto& node : nodes) node = {key(0x9FF), 77, 77};
  ASSERT_EQ(radicle_navmesh_find_path(abi, key(0x803), 0, key(0x803), 2, nullptr, nodes, 2, &count, &found, &cost),
            RADICLE_STATUS_OK);
  EXPECT_EQ(count, 3u);
  EXPECT_EQ(nodes[0].triangle, 0u);
  EXPECT_EQ(nodes[1].triangle, 1u);
  EXPECT_EQ(nodes[1].navmesh.local_id, 0x803u);

  radicle_navmesh_path_node four[4];
  const radicle_navmesh_path_door doors[] = {{key(0x830), {1, 1}}};
  radicle_navmesh_path_options options{};
  options.doors = doors;
  options.door_count = 1;
  ASSERT_EQ(radicle_navmesh_find_path(abi, key(0x803), 0, key(0x803), 2, &options, four, 4, &count, &found, &cost),
            RADICLE_STATUS_OK);
  EXPECT_EQ(count, 3u);
  EXPECT_EQ(four[1].flags, RADICLE_NAVMESH_PATH_NODE_NEEDS_OPENING);
  EXPECT_EQ(four[3].triangle, 0u);
  EXPECT_EQ(four[3].navmesh.plugin, nullptr);

  options = {};
  options.flags = RADICLE_NAVMESH_PATH_ALLOW_UNKNOWN_LINKS;
  ASSERT_EQ(radicle_navmesh_find_path(abi, key(0x803), 0, key(0x804), 1, &options, four, 4, &count, &found, &cost),
            RADICLE_STATUS_OK);
  EXPECT_EQ(count, 2u);
  EXPECT_EQ(four[1].navmesh.local_id, 0x804u);
  EXPECT_EQ(four[1].flags, RADICLE_NAVMESH_PATH_NODE_VIA_LINK);

  const radicle_navmesh_path_door shut[] = {{key(0x830), {1, 0}}, {key(0x831), {1, 1}}};
  options = {};
  options.doors = shut;
  options.door_count = 2;
  four[0] = {key(0x803), 5, 5};
  ASSERT_EQ(radicle_navmesh_find_path(abi, key(0x803), 0, key(0x803), 2, &options, four, 4, &count, &found, &cost),
            RADICLE_STATUS_OK);
  EXPECT_EQ(found, 0);
  EXPECT_EQ(count, 0u);
  EXPECT_FLOAT_EQ(cost, 0.0f);
  EXPECT_EQ(four[0].navmesh.plugin, nullptr);
  EXPECT_EQ(four[0].triangle, 0u);

  found = 7; count = 99; cost = 5.0f;
  EXPECT_EQ(radicle_navmesh_find_path(abi, key(0x803), 9, key(0x803), 2, nullptr, nullptr, 0, &count, &found, &cost),
            RADICLE_STATUS_NOT_FOUND);
  EXPECT_EQ(count, 0u);
  EXPECT_EQ(found, 0);
  EXPECT_EQ(radicle_navmesh_find_path(nullptr, key(0x803), 0, key(0x803), 2, nullptr, nullptr, 0, &count, &found, &cost),
            RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_navmesh_find_path(abi, key(0x803), 0, key(0x803), 2, nullptr, nullptr, 0, nullptr, &found, &cost),
            RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_navmesh_find_path(abi, key(0x803), 0, key(0x803), 2, nullptr, nullptr, 2, &count, &found, &cost),
            RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_navmesh_find_path(abi, radicle_form_key{}, 0, key(0x803), 2, nullptr, nullptr, 0, &count, &found, &cost),
            RADICLE_STATUS_INVALID_ARGUMENT);
  options = {};
  options.door_count = 1;
  EXPECT_EQ(radicle_navmesh_find_path(abi, key(0x803), 0, key(0x803), 2, &options, nullptr, 0, &count, &found, &cost),
            RADICLE_STATUS_INVALID_ARGUMENT);
  radicle_env_destroy(abi);
}

}
