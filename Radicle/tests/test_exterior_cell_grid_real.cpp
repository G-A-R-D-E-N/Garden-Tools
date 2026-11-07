// Real-data proof of the exterior block/sub-block label formula.
//
// #71's safety rule is "do not guess block/sub-block coordinate bucketing".
// The hand-written boundary cases in test_exterior_cell_grid.cpp encode the
// rule; this file checks it against every exterior CELL Bethesda ships, which
// is what turns it from a reading of xEdit's source into a measured fact.
//
// Needs the game's Data folder, so it is gated on FO4_DATA_DIR and skips
// without it. CI has no game files and will skip.

#include "fo4/environment/cell_view.hpp"
#include "fo4/environment/environment.hpp"
#include "fo4/forms/cell.hpp"
#include "fo4/forms/placed_reference.hpp"
#include "fo4/plugin/container.hpp"
#include "fo4/plugin/reader.hpp"
#include "real_data_build.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <map>
#include <optional>
#include <string>
#include <utility>
#include <variant>
#include <vector>

namespace {

constexpr auto k_cell = fo4::Signature::from_string("CELL");
constexpr auto k_wrld = fo4::Signature::from_string("WRLD");
constexpr auto k_xclc = fo4::Signature::from_string("XCLC");
constexpr auto k_data = fo4::Signature::from_string("DATA");
constexpr auto k_name = fo4::Signature::from_string("NAME");
constexpr auto k_xscl = fo4::Signature::from_string("XSCL");

struct GridCounts {
  std::size_t checked{0};
  std::size_t negative{0};
};

std::optional<std::pair<std::int32_t, std::int32_t>> cell_grid(const fo4::Record& record) {
  for (const auto& sub : record.subrecords) {
    if (sub.signature != k_xclc || sub.data.size() < 8) continue;
    std::int32_t x = 0;
    std::int32_t y = 0;
    std::memcpy(&x, sub.data.data(), 4);
    std::memcpy(&y, sub.data.data() + 4, 4);
    return std::pair{x, y};
  }
  return std::nullopt;
}

// Walks the WRLD hierarchy carrying the enclosing block/sub-block labels down,
// then asserts every CELL's own XCLC reproduces them through the public API.
void check_group(const fo4::Group& group,
                 std::optional<std::uint32_t> block,
                 std::optional<std::uint32_t> subblock,
                 GridCounts& counts) {
  if (group.header.group_type == fo4::forms::k_exterior_cell_block_group_type) {
    block = group.header.label_as_u32();
  } else if (group.header.group_type == fo4::forms::k_exterior_cell_subblock_group_type) {
    subblock = group.header.label_as_u32();
  }

  for (const auto& child : group.children) {
    if (const auto* nested = std::get_if<fo4::Group>(&child)) {
      check_group(*nested, block, subblock, counts);
      continue;
    }
    const auto& record = std::get<fo4::Record>(child);
    if (record.header.signature != k_cell || !block || !subblock) continue;
    const auto grid = cell_grid(record);
    if (!grid) continue;

    const auto [x, y] = *grid;
    const auto layout = fo4::forms::exterior_cell_grid_layout(x, y);
    EXPECT_EQ(fo4::forms::exterior_grid_group_label_u32(layout.block_x, layout.block_y), *block)
        << "block label mismatch at cell " << x << "," << y;
    EXPECT_EQ(fo4::forms::exterior_grid_group_label_u32(layout.subblock_x, layout.subblock_y),
              *subblock)
        << "sub-block label mismatch at cell " << x << "," << y;

    ++counts.checked;
    if (x < 0 || y < 0) ++counts.negative;
  }
}

struct CellPick {
  fo4::FormKey key;
  std::string editor_id;
};

struct CellSample {
  fo4::env::Environment env;
  CellPick interior;
  CellPick exterior;
};

std::optional<CellPick> pick_named_cell(const fo4::env::Environment& env,
                                        const std::vector<const fo4::env::RecordRef*>& candidates) {
  std::vector<std::pair<std::size_t, fo4::FormKey>> ranked;
  ranked.reserve(candidates.size());
  for (const auto* ref : candidates) {
    ranked.emplace_back(env.cell_children(ref->record->form_key).size(), ref->record->form_key);
  }
  std::stable_sort(ranked.begin(), ranked.end(),
                   [](const auto& a, const auto& b) { return a.first > b.first; });
  for (const auto& entry : ranked) {
    const auto& key = entry.second;
    const auto summary = fo4::env::describe_cell(env, key);
    if (!summary || summary->editor_id.empty()) continue;
    if (fo4::env::resolve_cell_id(env, summary->editor_id) != key) continue;
    return CellPick{key, summary->editor_id};
  }
  return std::nullopt;
}

std::optional<CellSample> load_cell_sample(const std::filesystem::path& master) {
  const std::vector<std::filesystem::path> paths{master};
  auto env = fo4::env::Environment::load(paths);
  const auto* world = env.find_by_editor_id("Commonwealth", k_wrld);
  if (world == nullptr) return std::nullopt;
  auto interior = pick_named_cell(env, env.interior_cells());
  auto exterior = pick_named_cell(env, env.worldspace_cells(world->record->form_key));
  if (!interior || !exterior) return std::nullopt;
  return CellSample{std::move(env), std::move(*interior), std::move(*exterior)};
}

const CellSample* cell_sample(const std::filesystem::path& master) {
  static const std::optional<CellSample> sample = load_cell_sample(master);
  return sample ? &*sample : nullptr;
}

const fo4::Subrecord* find_subrecord(const fo4::Record& record, fo4::Signature signature) {
  for (const auto& sub : record.subrecords) {
    if (sub.signature == signature) return &sub;
  }
  return nullptr;
}

std::array<float, 6> raw_pose(const fo4::Record& record) {
  std::array<float, 6> pose{};
  const auto* data = find_subrecord(record, k_data);
  if (data != nullptr && data->data.size() == pose.size() * sizeof(float)) {
    std::memcpy(pose.data(), data->data.data(), data->data.size());
  }
  return pose;
}

float raw_scale(const fo4::Record& record) {
  float value = 1.0f;
  const auto* scale = find_subrecord(record, k_xscl);
  if (scale != nullptr && scale->data.size() == sizeof(float)) {
    std::memcpy(&value, scale->data.data(), sizeof(float));
  }
  return value;
}

std::optional<fo4::FormKey> raw_base(const fo4::Record& record, const fo4::PluginKey& master) {
  const auto* name = find_subrecord(record, k_name);
  if (name == nullptr || name->data.size() != sizeof(std::uint32_t)) return std::nullopt;
  std::uint32_t raw = 0;
  std::memcpy(&raw, name->data.data(), sizeof(raw));
  if (raw == 0 || (raw >> 24) != 0) return std::nullopt;
  return fo4::FormKey{master, fo4::LocalFormId{raw & 0x00FFFFFFu}};
}

}  // namespace

TEST(ExteriorCellGridRealData, ReproducesShippedWorldspaceLabels) {
  const char* dir = std::getenv("FO4_DATA_DIR");
  if (dir == nullptr) {
    GTEST_SKIP() << "set FO4_DATA_DIR to the Fallout 4 Data folder to run this";
  }
  record_game_build(detect_game_build(dir));

  GridCounts counts;
  int files = 0;
  for (const char* name : {"Fallout4.esm", "DLCCoast.esm", "DLCNukaWorld.esm", "DLCRobot.esm"}) {
    const auto path = std::filesystem::path{dir} / name;
    if (!std::filesystem::exists(path)) continue;

    auto plugin = fo4::read_plugin_file(path);
    for (const auto& group : plugin.groups) {
      if (group.header.group_type != static_cast<std::int32_t>(fo4::GroupType::top) ||
          group.header.top_label() != k_wrld) {
        continue;
      }
      check_group(group, std::nullopt, std::nullopt, counts);
    }
    ++files;
  }

  if (files == 0) {
    GTEST_SKIP() << "no official masters found under " << dir;
  }

  // Guard against the walk silently finding nothing and passing vacuously.
  EXPECT_GT(counts.checked, 10000u);
  // Negative coordinates are the case floor division gets right and C
  // truncation gets wrong, so the sample has to actually contain them.
  EXPECT_GT(counts.negative, 1000u);
}

// Invariant: list_cell_references must keep every reference with its base, pose and scale.
TEST(CellViewReal, ListCellReferencesMatchesRawGroupWalkOnVanillaCells) {
  const char* dir = std::getenv("FO4_DATA_DIR");
  if (dir == nullptr) {
    GTEST_SKIP() << "set FO4_DATA_DIR to the Fallout 4 Data folder to run this";
  }
  const std::filesystem::path master = std::filesystem::path(dir) / "Fallout4.esm";
  if (!std::filesystem::exists(master)) GTEST_SKIP() << "no Fallout4.esm under " << dir;
  record_game_build(detect_game_build(dir));

  const auto* sample = cell_sample(master);
  ASSERT_NE(sample, nullptr) << "Commonwealth with a named interior and exterior cell is missing from Fallout4.esm";
  const auto master_key = fo4::PluginKey::from_filename("Fallout4.esm");

  for (const auto* pick : {&sample->interior, &sample->exterior}) {
    SCOPED_TRACE(pick->editor_id);
    const auto listing = fo4::env::list_cell_references(sample->env, pick->key);
    const auto* cell_ref = sample->env.winner(pick->key);
    ASSERT_NE(cell_ref, nullptr);
    const auto raw =
        fo4::forms::placed_references_for_cell(sample->env.plugin(cell_ref->plugin), pick->key);

    std::map<fo4::FormKey, const fo4::forms::PlacedReferenceView*> raw_by_key;
    std::size_t live = 0;
    for (const auto& view : raw) {
      if (view.deleted) continue;
      ++live;
      raw_by_key.emplace(view.record->form_key, &view);
    }
    ASSERT_GT(live, 0u);
    ASSERT_EQ(raw_by_key.size(), live);
    ASSERT_EQ(listing.references.size(), live);

    for (const auto& ref : listing.references) {
      SCOPED_TRACE(ref.key.to_string());
      const auto found = raw_by_key.find(ref.key);
      ASSERT_NE(found, raw_by_key.end());
      const auto& view = *found->second;
      const auto& record = *view.record;
      EXPECT_EQ(ref.persistent, view.persistent);
      EXPECT_EQ(ref.base, raw_base(record, master_key));
      EXPECT_EQ(ref.scale, raw_scale(record));
      EXPECT_EQ((std::array<float, 6>{ref.transform.x, ref.transform.y, ref.transform.z, ref.transform.rx,
                                      ref.transform.ry, ref.transform.rz}),
                raw_pose(record));
    }
  }
}

// Invariant: a cell read must stay under 2000 ms per cell.
TEST(CellViewReal, ListCellReferencesStaysWithinCeilingOnVanillaCells) {
  const char* dir = std::getenv("FO4_DATA_DIR");
  if (dir == nullptr) {
    GTEST_SKIP() << "set FO4_DATA_DIR to the Fallout 4 Data folder to run this";
  }
  const std::filesystem::path master = std::filesystem::path(dir) / "Fallout4.esm";
  if (!std::filesystem::exists(master)) GTEST_SKIP() << "no Fallout4.esm under " << dir;
  record_game_build(detect_game_build(dir));

  const auto* sample = cell_sample(master);
  ASSERT_NE(sample, nullptr) << "Commonwealth with a named interior and exterior cell is missing from Fallout4.esm";
  constexpr double k_ceiling_ms = 2000.0;

  for (const auto* pick : {&sample->interior, &sample->exterior}) {
    const auto start = std::chrono::steady_clock::now();
    const auto listing = fo4::env::list_cell_references(sample->env, pick->key);
    const double ms =
        std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
    std::cout << "CELL_PERF cell=" << pick->editor_id << " refs=" << listing.references.size() << " ms="
              << std::fixed << std::setprecision(3) << ms << std::defaultfloat << "\n";
    EXPECT_GT(listing.references.size(), 0u) << pick->editor_id;
    EXPECT_LT(ms, k_ceiling_ms) << pick->editor_id;
  }
}
