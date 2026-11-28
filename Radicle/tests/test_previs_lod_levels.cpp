#include "fo4/previs/csg.hpp"
#include "fo4/previs/lod_levels.hpp"
#include "fo4/previs/precombine_plan.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <map>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>

// Constraint: a shipped object gives each LOD level its own vertex block, and level 1 is the coarser of the two, measured on 241 objects.
TEST(PrevisLodLevels, WeldsEachHigherLevelOntoItsOwnVertexBlock) {
  // Constraint: 100 vertices on a 50-unit circle keep wide triangles, so both weld grids still hold geometry.
  constexpr double k_pi = 3.14159265358979323846;
  std::vector<float> positions;
  for (int vertex = 0; vertex < 100; ++vertex) {
    const double angle = 2.0 * k_pi * static_cast<double>(vertex) / 100.0;
    positions.push_back(static_cast<float>(50.0 * std::cos(angle)));
    positions.push_back(static_cast<float>(50.0 * std::sin(angle)));
    positions.push_back(0.0f);
  }
  std::vector<std::uint16_t> indices;
  for (std::uint16_t vertex = 1; vertex + 1 < 100; ++vertex) {
    indices.insert(indices.end(), {0, vertex, static_cast<std::uint16_t>(vertex + 1)});
  }
  const auto levels = fo4::previs::reduce_mesh_levels(positions, indices);
  ASSERT_EQ(levels.size(), fo4::previs::k_previs_lod_levels);
  EXPECT_EQ(levels[0].kept_vertices.size(), positions.size() / 3u);
  EXPECT_EQ(levels[0].triangles, indices);

  for (std::size_t level = 0; level < levels.size(); ++level) {
    const auto& kept = levels[level].kept_vertices;
    if (levels[level].triangles.empty()) continue;
    EXPECT_GT(kept.size(), 2u);
    EXPECT_LT(kept.size(), positions.size() / 3u + 1u);
    for (std::size_t slot = 0; slot < kept.size(); ++slot) {
      EXPECT_LT(kept[slot], positions.size() / 3u);
      if (slot > 0) {
        EXPECT_LT(kept[slot - 1], kept[slot]);
      }
    }
    for (const auto index : levels[level].triangles) EXPECT_LT(index, kept.size());
    for (std::size_t corner = 0; corner + 2 < levels[level].triangles.size(); corner += 3) {
      const auto a = levels[level].triangles[corner];
      const auto b = levels[level].triangles[corner + 1];
      const auto c = levels[level].triangles[corner + 2];
      EXPECT_TRUE(a != b && b != c && a != c);
    }
  }
  ASSERT_FALSE(levels[1].triangles.empty());
  ASSERT_FALSE(levels[2].triangles.empty());
  // Constraint: a coarse fan puts both cells on the same grid, so level 1 is required to keep no more than level 2, not strictly fewer.
  EXPECT_LE(levels[1].kept_vertices.size(), levels[2].kept_vertices.size());
  EXPECT_LT(levels[2].kept_vertices.size(), positions.size() / 3u);


  std::vector<std::uint16_t> partial{0, 1};
  EXPECT_THROW(fo4::previs::reduce_mesh_levels(positions, partial), std::invalid_argument);
  std::vector<std::uint16_t> outside{0, 1, 100};
  EXPECT_THROW(fo4::previs::reduce_mesh_levels(positions, outside), std::invalid_argument);

  // Constraint: packing copies each level's own bytes in level order, so a higher level's indices start at its block.
  std::vector<std::byte> source(positions.size() / 3u * 4u);
  for (std::size_t vertex = 0; vertex + 1 < source.size(); vertex += 4) {
    source[vertex] = static_cast<std::byte>(vertex & 0xFFu);
    source[vertex + 1] = std::byte{0x5A};
  }
  const auto packed = fo4::previs::pack_lod_levels(levels, source, 4);
  ASSERT_TRUE(packed.has_value());
  std::uint32_t expected_vertices = 0;
  for (const auto& level : levels) expected_vertices += static_cast<std::uint32_t>(level.kept_vertices.size());
  EXPECT_EQ(packed->vertex_count, expected_vertices);
  EXPECT_EQ(packed->vertices.size(), static_cast<std::size_t>(expected_vertices) * 4u);
  EXPECT_EQ(std::vector<std::byte>(packed->vertices.begin(), packed->vertices.begin() + 400), source);
  std::uint32_t block_start = 0;
  for (std::size_t level = 0; level < fo4::previs::k_previs_lod_levels; ++level) {
    const std::span<const std::uint16_t> level_indices =
        level == 0 ? std::span<const std::uint16_t>(packed->indices)
                   : std::span<const std::uint16_t>(packed->higher_lod_indices[level - 1]);
    for (const auto index : level_indices) {
      EXPECT_GE(index, block_start);
      EXPECT_LT(index, block_start + packed->level_vertex_count[level]);
    }
    block_start += packed->level_vertex_count[level];
  }
  EXPECT_EQ(block_start, packed->vertex_count);
  EXPECT_FALSE(fo4::previs::pack_lod_levels(levels, std::span<const std::byte>(source.data(), 3), 4).has_value());
}

namespace {

// Constraint: a target names a triangle fraction of level 0, so every target test needs a mesh whose reduction is measurable.
void fan_mesh(std::vector<float>& positions, std::vector<std::uint16_t>& indices) {
  constexpr double k_pi = 3.14159265358979323846;
  positions.clear();
  indices.clear();
  // Constraint: a fine fan gives the ratio solve enough triangles to hit a shipped count instead of rounding to none.
  constexpr int k_fan_vertices = 320;
  for (int vertex = 0; vertex < k_fan_vertices; ++vertex) {
    const double angle = 2.0 * k_pi * static_cast<double>(vertex) / k_fan_vertices;
    positions.push_back(static_cast<float>(50.0 * std::cos(angle)));
    positions.push_back(static_cast<float>(50.0 * std::sin(angle)));
    positions.push_back(0.0f);
  }
  for (std::uint16_t vertex = 1; vertex + 1 < k_fan_vertices; ++vertex) {
    indices.insert(indices.end(), {0, vertex, static_cast<std::uint16_t>(vertex + 1)});
  }
}

}  // namespace

// Constraint: a pinned object ratio must survive the cell solve, so the produced ratio cannot drift past what the grid allows.
TEST(PrevisLodLevels, HitsAPinnedTriangleRatio) {
  std::vector<float> positions;
  std::vector<std::uint16_t> indices;
  fan_mesh(positions, indices);
  const auto triangles = indices.size() / 3u;
  const auto levels = fo4::previs::reduce_mesh_levels_to_target(
      positions, indices, fo4::previs::LodLevelTarget{0.50f, 0.25f});
  ASSERT_FALSE(levels[1].triangles.empty());
  ASSERT_FALSE(levels[2].triangles.empty());
  const auto ratios = fo4::previs::lod_triangle_ratios(levels);
  EXPECT_NEAR(static_cast<double>(ratios[0]), 1.0, 1e-6);
  // Constraint: the solve lands on the nearest reachable count, so each level is within one triangle of the requested fraction.
  EXPECT_NEAR(static_cast<double>(levels[1].triangles.size() / 3u),
              static_cast<double>(std::lround(0.50 * static_cast<double>(triangles))), 3.0);
  EXPECT_NEAR(static_cast<double>(levels[2].triangles.size() / 3u),
              static_cast<double>(std::lround(0.25 * static_cast<double>(triangles))), 3.0);
  EXPECT_LT(levels[2].triangles.size() / 3u, levels[1].triangles.size() / 3u);
  EXPECT_LT(levels[1].triangles.size() / 3u, triangles);
  EXPECT_LT(std::fabs(static_cast<double>(ratios[1]) - 0.50), 0.15);
  EXPECT_LT(std::fabs(static_cast<double>(ratios[2]) - 0.25), 0.15);
  for (std::size_t level = 1; level < levels.size(); ++level) {
    for (const auto index : levels[level].triangles) EXPECT_LT(index, levels[level].kept_vertices.size());
  }
  // Constraint: a ratio the weld itself produced is a reachable count, so pinning it must reproduce that count exactly.
  const auto shipped = fo4::previs::reduce_mesh_levels(positions, indices);
  const auto shipped_ratios = fo4::previs::lod_triangle_ratios(shipped);
  const auto replay = fo4::previs::reduce_mesh_levels_to_target(
      positions, indices, fo4::previs::LodLevelTarget{shipped_ratios[1], shipped_ratios[2]});
  EXPECT_EQ(replay[1].triangles.size(), shipped[1].triangles.size());
  EXPECT_EQ(replay[2].triangles.size(), shipped[2].triangles.size());
}

// Constraint: an object without a target keeps the measured edge rule, so the new path cannot change an unpinned build.
TEST(PrevisLodLevels, FallsBackToTheEdgeRuleWithoutATarget) {
  std::vector<float> positions;
  std::vector<std::uint16_t> indices;
  fan_mesh(positions, indices);
  const auto edge = fo4::previs::reduce_mesh_levels(positions, indices);
  const auto no_target = fo4::previs::reduce_mesh_levels_to_target(positions, indices, fo4::previs::LodLevelTarget{});
  EXPECT_EQ(no_target[1].triangles, edge[1].triangles);
  EXPECT_EQ(no_target[2].triangles, edge[2].triangles);
  const auto full_target = fo4::previs::reduce_mesh_levels_to_target(
      positions, indices, fo4::previs::LodLevelTarget{1.0f, 0.0f});
  EXPECT_EQ(full_target[1].triangles, edge[1].triangles);
}

// Constraint: the writer reads the target off the group it emits, so material reordering must not drop a pinned ratio.
TEST(PrevisLodLevels, KeepsAPinnedTargetThroughMaterialOrdering) {
  fo4::previs::PrecombinePlan plan;
  plan.cell = fo4::FormKey::parse("000801:Mod.esm");
  fo4::previs::PrecombineModelGroup rock;
  rock.model_path = "Meshes\\Rock.nif";
  rock.lod_target = fo4::previs::PrecombineLodTarget{0.5f, 0.25f};
  fo4::previs::PrecombineModelGroup pipe;
  pipe.model_path = "Meshes\\Pipe.nif";
  plan.groups = {rock, pipe};
  const std::map<std::string, std::string, std::less<>> materials{{"Meshes\\Rock.nif", "materials\\rock.bgsm"},
                                                                {"Meshes\\Pipe.nif", ""}};
  const auto ordered = fo4::previs::order_plan_by_material(plan, materials);
  ASSERT_EQ(ordered.plan.groups.size(), 2u);
  bool found = false;
  for (const auto& group : ordered.plan.groups) {
    if (group.model_path != "Meshes\\Rock.nif") continue;
    found = true;
    ASSERT_TRUE(group.lod_target.has_value());
    EXPECT_FLOAT_EQ(group.lod_target->level1, 0.5f);
    EXPECT_FLOAT_EQ(group.lod_target->level2, 0.25f);
  }
  EXPECT_TRUE(found);
  for (const auto& group : ordered.plan.groups) {
    if (group.model_path == "Meshes\\Pipe.nif") {
      EXPECT_FALSE(group.lod_target.has_value());
    }
  }
}

// Constraint: a backend request names ratios by model path, so the JSON parse must reach the plan and the emitted counts.
TEST(PrevisLodLevels, JsonTargetsReachThePlanAndTheEmittedLevelCounts) {
  // Constraint: the JSON form escapes its own backslash, so the key arrives as Meshes\Rock.nif and must normalize to the plan's group.
  const auto document = nlohmann::json::parse(
      R"({"meshes\\rock.nif": [0.5, 0.25], "MESHES\\PIPE.NIF": {"level1": 0.4, "level2": 0.2}})");
  const auto targets = fo4::previs::lod_targets_from_json(document);
  ASSERT_EQ(targets.size(), 2u);
  fo4::previs::PrecombinePlan plan;
  plan.cell = fo4::FormKey::parse("000801:Mod.esm");
  fo4::previs::PrecombineModelGroup rock;
  rock.model_path = "Meshes\\Rock.nif";
  fo4::previs::PrecombineModelGroup pipe;
  pipe.model_path = "Meshes\\Pipe.nif";
  plan.groups = {rock, pipe};
  fo4::previs::attach_lod_targets(plan, targets);
  ASSERT_EQ(plan.groups.size(), 2u);
  for (const auto& group : plan.groups) ASSERT_TRUE(group.lod_target.has_value()) << group.model_path;

  std::vector<float> positions;
  std::vector<std::uint16_t> indices;
  fan_mesh(positions, indices);
  const auto triangles = indices.size() / 3u;
  for (const auto& group : plan.groups) {
    const auto expected = group.model_path == "Meshes\\Rock.nif" ? fo4::previs::LodLevelTarget{0.5f, 0.25f}
                                                                 : fo4::previs::LodLevelTarget{0.4f, 0.2f};
    EXPECT_FLOAT_EQ(group.lod_target->level1, expected.level1) << group.model_path;
    EXPECT_FLOAT_EQ(group.lod_target->level2, expected.level2) << group.model_path;
    // Constraint: the writer reads the same two fractions, so the emitted level counts must land on the plan's ratios.
    const auto levels = fo4::previs::reduce_mesh_levels_to_target(positions, indices, expected);
    ASSERT_FALSE(levels[1].triangles.empty()) << group.model_path;
    ASSERT_FALSE(levels[2].triangles.empty()) << group.model_path;
    const auto emitted = fo4::previs::lod_triangle_ratios(levels);
    EXPECT_LT(std::fabs(static_cast<double>(emitted[1]) - expected.level1), 0.06) << group.model_path;
    EXPECT_LT(std::fabs(static_cast<double>(emitted[2]) - expected.level2), 0.06) << group.model_path;
    EXPECT_LT(levels[2].triangles.size() / 3u, levels[1].triangles.size() / 3u) << group.model_path;
    EXPECT_LT(levels[1].triangles.size() / 3u, triangles) << group.model_path;
  }
}

// Constraint: a pinned level is reproducible only through the cell it solved to, so the reported grid must rebuild it.
TEST(PrevisLodLevels, ReportsTheCellItsTargetSolvedTo) {
  std::vector<float> positions;
  std::vector<std::uint16_t> indices;
  fan_mesh(positions, indices);
  const fo4::previs::LodLevelTarget target{0.5f, 0.25f};
  const auto cells = fo4::previs::solve_lod_weld_cells(positions, indices, target);
  EXPECT_GT(cells.level1, 0.0f);
  EXPECT_GT(cells.level2, 0.0f);
  // Constraint: the two ratios are different counts, so a shared cell would rebuild identical levels and fail below.
  EXPECT_NE(cells.level1, cells.level2);

  const auto pinned = fo4::previs::reduce_mesh_levels_to_target(positions, indices, target);
  const auto rebuilt = fo4::previs::reduce_mesh_levels_with_cells(positions, indices, cells);
  EXPECT_LT(rebuilt[2].triangles.size(), rebuilt[1].triangles.size());
  for (std::size_t level = 0; level < pinned.size(); ++level) {
    EXPECT_EQ(rebuilt[level].triangles, pinned[level].triangles) << level;
    EXPECT_EQ(rebuilt[level].kept_vertices, pinned[level].kept_vertices) << level;
  }

  // Constraint: a pair outside (0,1) names no reduction, so it solves no cell and the edge rule stands in.
  const auto invalid = fo4::previs::solve_lod_weld_cells(positions, indices, fo4::previs::LodLevelTarget{2.0f, 0.0f});
  EXPECT_EQ(invalid.level1, 0.0f);
  EXPECT_EQ(invalid.level2, 0.0f);
  const auto fallback = fo4::previs::reduce_mesh_levels_to_target(positions, indices, fo4::previs::LodLevelTarget{2.0f, 0.0f});
  const auto rule = fo4::previs::reduce_mesh_levels(positions, indices);
  for (std::size_t level = 0; level < rule.size(); ++level) {
    EXPECT_EQ(fallback[level].triangles, rule[level].triangles) << level;
  }
}

// Constraint: the CSG row, its stream and the packed blocks must describe the same levels, so a mismatch in any of them fails here as it does in tools/previs/lod_contract.cpp.
TEST(PrevisLodLevels, PackedLevelsReachTheCsgRowAndStreamUnchanged) {
  std::vector<float> positions;
  std::vector<std::uint16_t> indices;
  fan_mesh(positions, indices);
  constexpr std::size_t k_stride = 24;
  const std::size_t vertex_count = positions.size() / 3u;
  std::vector<std::byte> source(vertex_count * k_stride);
  for (std::size_t at = 0; at < source.size(); ++at) source[at] = static_cast<std::byte>((at * 7u + 3u) & 0xFFu);
  const auto levels = fo4::previs::reduce_mesh_levels_to_target(positions, indices, fo4::previs::LodLevelTarget{0.5f, 0.25f});
  ASSERT_FALSE(levels[1].triangles.empty());
  ASSERT_FALSE(levels[2].triangles.empty());
  const auto packed = fo4::previs::pack_lod_levels(levels, source, k_stride);
  ASSERT_TRUE(packed.has_value());
  EXPECT_EQ(packed->level_vertex_count[0], vertex_count);
  EXPECT_GT(packed->level_vertex_count[1], 0u);
  EXPECT_GT(packed->level_vertex_count[2], 0u);
  EXPECT_TRUE(std::equal(source.begin(), source.end(), packed->vertices.begin()));

  fo4::previs::CsgMeshObject mesh;
  mesh.vertex_desc = *fo4::previs::csg_shipped_descriptor(k_stride);
  mesh.vertex_count = packed->vertex_count;
  mesh.vertices = packed->vertices;
  mesh.indices = packed->indices;
  mesh.higher_lod_indices = packed->higher_lod_indices;
  mesh.level_vertex_count = packed->level_vertex_count;
  const auto built = fo4::previs::build_csg(std::span<const fo4::previs::CsgMeshObject>(&mesh, 1));
  ASSERT_EQ(built.rows.size(), 1u);
  const auto& row = built.rows[0];
  const std::size_t words0 = levels[0].triangles.size();
  const std::size_t words1 = levels[1].triangles.size();
  for (std::size_t level = 0; level < fo4::previs::k_previs_lod_levels; ++level) {
    EXPECT_EQ(row.lod_triangle_count[level], levels[level].triangles.size() / 3u) << level;
    EXPECT_EQ(row.lod_vertex_count[level], packed->level_vertex_count[level]) << level;
  }
  EXPECT_EQ(row.lod_index_offset[0], 0u);
  EXPECT_EQ(row.lod_index_offset[1], words0);
  EXPECT_EQ(row.lod_index_offset[2], words0 + words1);

  std::vector<std::byte> expected(packed->vertices);
  for (const auto* list : {&packed->indices, &packed->higher_lod_indices[0], &packed->higher_lod_indices[1]}) {
    for (const auto index : *list) {
      expected.push_back(static_cast<std::byte>(index & 0xFFu));
      expected.push_back(static_cast<std::byte>((index >> 8U) & 0xFFu));
    }
  }
  EXPECT_EQ(fo4::previs::inflate_csg_stream(fo4::previs::parse_csg(built.bytes)), expected);

  // Constraint: a level-2 index that points into level 1's block must be refused, or the engine would draw level 1's vertices at level 2.
  auto crossed = mesh;
  crossed.higher_lod_indices[1][0] = static_cast<std::uint16_t>(packed->level_vertex_count[0]);
  EXPECT_ANY_THROW(fo4::previs::build_csg(std::span<const fo4::previs::CsgMeshObject>(&crossed, 1)));
}
