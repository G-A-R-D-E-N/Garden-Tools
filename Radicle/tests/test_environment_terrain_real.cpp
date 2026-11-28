#include "fo4/environment/terrain.hpp"
#include "fo4/formid.hpp"
#include "../src/forms/land_normals.hpp"
#include "real_data.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <map>
#include <utility>
#include <vector>

namespace {

constexpr std::uint64_t k_fnv_offset = 0xCBF29CE484222325ull;
constexpr std::uint64_t k_fnv_prime = 0x100000001B3ull;

std::uint64_t fnv1a(std::uint64_t hash, const void* data, std::size_t size) {
  const auto* bytes = static_cast<const unsigned char*>(data);
  for (std::size_t i = 0; i < size; ++i) {
    hash ^= bytes[i];
    hash *= k_fnv_prime;
  }
  return hash;
}

}  // namespace

TEST(TerrainLandReal, CommonwealthCellsDecodeWithinInvariants) {
  const char* dir = ::fo4::test::fo4_data_dir_cstr();
  if (dir == nullptr) GTEST_SKIP() << "set FO4_DATA_DIR to the Fallout 4 Data folder to run this";
  const std::filesystem::path master = std::filesystem::path(dir) / "Fallout4.esm";
  if (!std::filesystem::exists(master)) GTEST_SKIP() << "no Fallout4.esm under " << dir;
  const std::vector<std::filesystem::path> paths{master};
  const auto env = fo4::env::Environment::load(paths);
  auto plugin = fo4::PluginKey::from_filename("Fallout4.esm");

  std::size_t decoded = 0;
  for (const std::uint32_t local : {0x00F540u, 0x00F53Fu, 0x00F53Eu}) {
    SCOPED_TRACE(local);
    const auto* record = env.winner(fo4::FormKey{plugin, fo4::LocalFormId{local}});
    ASSERT_NE(record, nullptr);
    const auto land = fo4::env::decode_landscape(env, *record);

    const auto heights = fo4::env::decode_land_heights(land);
    ASSERT_TRUE(heights.has_value());
    for (const float height : heights->heights) EXPECT_TRUE(std::isfinite(height));
    const std::uint64_t heights_hash = fnv1a(k_fnv_offset, heights->heights.data(), sizeof(heights->heights));

    for (const auto& layer : fo4::env::decode_layer_alpha(land)) {
      for (const float alpha : layer.alpha) {
        EXPECT_TRUE(std::isfinite(alpha));
        EXPECT_GE(alpha, 0.0f);
        EXPECT_LE(alpha, 1.0f);
      }
    }

    const auto blend = fo4::env::blend_cell_weights(land);
    EXPECT_FALSE(blend.layers.empty());
    std::uint64_t blend_hash = k_fnv_offset;
    for (const auto& base : blend.base) {
      const std::uint32_t id = base ? base->local().value() : 0u;
      blend_hash = fnv1a(blend_hash, &id, sizeof(id));
    }
    std::vector<float> totals(fo4::env::k_land_vertices, 0.0f);
    for (const auto& [texture, weights] : blend.layers) {
      const std::uint32_t id = texture.local().value();
      blend_hash = fnv1a(blend_hash, &id, sizeof(id));
      blend_hash = fnv1a(blend_hash, weights.data(), sizeof(weights));
      for (std::size_t i = 0; i < weights.size(); ++i) {
        EXPECT_TRUE(std::isfinite(weights[i]));
        EXPECT_GE(weights[i], 0.0f);
        totals[i] += weights[i];
      }
    }
    for (const float total : totals) EXPECT_LE(total, 1.0f + 1e-5f);

    std::cout << "LAND 0x" << std::hex << local << " heights=" << std::setw(16) << std::setfill('0') << heights_hash
              << " blend=" << std::setw(16) << blend_hash << std::setfill(' ') << std::dec << "\n";
    ++decoded;
  }
  EXPECT_GT(decoded, 0u);
}

// Detects: a height decoder whose row chaining or scale disagrees with the shared edge of the neighbouring cell.
TEST(TerrainLandReal, NeighbouringCellsMatchAtSharedEdges) {
  const char* dir = ::fo4::test::fo4_data_dir_cstr();
  if (dir == nullptr) GTEST_SKIP() << "set FO4_DATA_DIR to the Fallout 4 Data folder to run this";
  const std::filesystem::path master = std::filesystem::path(dir) / "Fallout4.esm";
  if (!std::filesystem::exists(master)) GTEST_SKIP() << "no Fallout4.esm under " << dir;
  const std::vector<std::filesystem::path> paths{master};
  const auto env = fo4::env::Environment::load(paths);
  const auto* world = env.find_by_editor_id("Commonwealth", fo4::Signature::from_string("WRLD"));
  ASSERT_NE(world, nullptr);

  std::map<std::pair<int, int>, fo4::env::LandHeightGrid> grids;
  for (const auto* cell : env.worldspace_cells(world->record->form_key)) {
    const auto data = fo4::env::decode_cell(env, *cell);
    if (!data.grid) continue;
    const auto* landscape = env.cell_landscape(cell->record->form_key);
    if (landscape == nullptr) continue;
    const auto heights = fo4::env::decode_land_heights(fo4::env::decode_landscape(env, *landscape));
    if (heights) grids.emplace(std::pair{data.grid->x, data.grid->y}, *heights);
  }
  ASSERT_GT(grids.size(), 0u);

  std::size_t east_pairs = 0;
  std::size_t east_matches = 0;
  std::size_t north_pairs = 0;
  std::size_t north_matches = 0;
  constexpr std::size_t side = fo4::env::k_land_grid;
  for (const auto& [key, grid] : grids) {
    const auto east = grids.find({key.first + 1, key.second});
    if (east != grids.end()) {
      for (std::size_t y = 0; y < side; ++y) {
        ++east_pairs;
        if (std::fabs(grid.heights[y * side + side - 1] - east->second.heights[y * side]) < 1e-2f) ++east_matches;
      }
    }
    const auto north = grids.find({key.first, key.second + 1});
    if (north != grids.end()) {
      for (std::size_t x = 0; x < side; ++x) {
        ++north_pairs;
        if (std::fabs(grid.heights[(side - 1) * side + x] - north->second.heights[x]) < 1e-2f) ++north_matches;
      }
    }
  }
  std::cout << "LAND shared-edge east " << east_matches << "/" << east_pairs << " north " << north_matches << "/"
            << north_pairs << "\n";
  ASSERT_GT(east_pairs, 0u);
  ASSERT_GT(north_pairs, 0u);
  EXPECT_GE(static_cast<double>(east_matches), 0.99 * static_cast<double>(east_pairs));
  EXPECT_GE(static_cast<double>(north_matches), 0.99 * static_cast<double>(north_pairs));
}

// Detects: a normal recompute that drifts from the CK's triangle list (a quad average scores about 25 % exact on these cells, the CK formula about 40 %).
TEST(TerrainLandReal, RecomputedNormalsTrackTheShippedVnmlOnCommonwealthCells) {
  const char* dir = ::fo4::test::fo4_data_dir_cstr();
  if (dir == nullptr) GTEST_SKIP() << "set FO4_DATA_DIR to the Fallout 4 Data folder to run this";
  const std::filesystem::path master = std::filesystem::path(dir) / "Fallout4.esm";
  if (!std::filesystem::exists(master)) GTEST_SKIP() << "no Fallout4.esm under " << dir;
  const std::vector<std::filesystem::path> paths{master};
  const auto env = fo4::env::Environment::load(paths);
  const auto* world = env.find_by_editor_id("Commonwealth", fo4::Signature::from_string("WRLD"));
  ASSERT_NE(world, nullptr);

  constexpr std::size_t side = fo4::env::k_land_grid;
  struct Cell {
    fo4::land::HeightGrid heights;
    std::array<std::int8_t, 3 * side * side> normals;
  };
  std::map<std::pair<int, int>, Cell> cells;
  for (const auto* cell : env.worldspace_cells(world->record->form_key)) {
    const auto data = fo4::env::decode_cell(env, *cell);
    if (!data.grid) continue;
    const auto* landscape = env.cell_landscape(cell->record->form_key);
    if (landscape == nullptr) continue;
    const auto land = fo4::env::decode_landscape(env, *landscape);
    const auto heights = fo4::env::decode_land_heights(land);
    if (!heights || land.normals.size() != 3 * side * side) continue;
    Cell entry{};
    std::copy(heights->heights.begin(), heights->heights.end(), entry.heights.begin());
    for (std::size_t i = 0; i < entry.normals.size(); ++i) entry.normals[i] = static_cast<std::int8_t>(land.normals[i]);
    cells.emplace(std::pair{data.grid->x, data.grid->y}, entry);
  }
  ASSERT_GT(cells.size(), 0u);

  std::size_t vertices = 0;
  std::size_t exact = 0;
  double angle_sum = 0.0;
  std::size_t stride = 0;
  for (const auto& [key, cell] : cells) {
    if (++stride % 20 != 0) continue;
    if (std::all_of(cell.heights.begin(), cell.heights.end(), [&](float h) { return h == cell.heights[0]; })) continue;
    const auto lookup = [&](int dx, int dy) -> const fo4::land::HeightGrid* {
      const auto found = cells.find({key.first + dx, key.second + dy});
      return found == cells.end() ? nullptr : &found->second.heights;
    };
    const auto computed = fo4::land::compute_land_normals(
        cell.heights, {lookup(1, 0), lookup(-1, 0), lookup(0, 1), lookup(0, -1), lookup(1, 1), lookup(-1, 1), lookup(1, -1), lookup(-1, -1)});
    for (std::size_t row = 1; row + 1 < side; ++row) {
      for (std::size_t column = 1; column + 1 < side; ++column) {
        const auto base = (row * side + column) * 3;
        double dot = 0.0;
        double a_length = 0.0;
        double b_length = 0.0;
        bool same = true;
        for (std::size_t axis = 0; axis < 3; ++axis) {
          const double a = computed[base + axis];
          const double b = cell.normals[base + axis];
          dot += a * b;
          a_length += a * a;
          b_length += b * b;
          same = same && computed[base + axis] == cell.normals[base + axis];
        }
        ++vertices;
        exact += same ? 1 : 0;
        angle_sum += std::acos(std::clamp(dot / std::sqrt(a_length * b_length), -1.0, 1.0)) * 180.0 / 3.14159265358979323846;
      }
    }
  }
  ASSERT_GT(vertices, 0u);
  const double exact_rate = static_cast<double>(exact) / static_cast<double>(vertices);
  const double mean_angle = angle_sum / static_cast<double>(vertices);
  std::cout << "LAND normals interior vertices=" << vertices << " exact=" << exact_rate << " mean_degrees=" << mean_angle << std::endl;
  EXPECT_GE(exact_rate, 0.33);
  EXPECT_LE(mean_angle, 0.6);
}
