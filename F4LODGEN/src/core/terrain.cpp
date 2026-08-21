#include "f4lod/terrain.hpp"

#include <algorithm>
#include <cmath>
#include <cctype>
#include <cstdint>
#include <iomanip>
#include <sstream>
#include <unordered_map>
#include <utility>

namespace f4lod {
namespace {

std::string lower_ascii(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return value;
}

std::string stable_key(const FormKey& key) {
    std::ostringstream out;
    out << lower_ascii(key.plugin) << ':' << std::hex << std::setw(6) << std::setfill('0') << key.id;
    return out.str();
}

bool same_form_key(const FormKey& lhs, const FormKey& rhs) {
    return lhs.id == rhs.id && lower_ascii(lhs.plugin) == lower_ascii(rhs.plugin);
}

std::uint64_t coordinate_key(std::int32_t x, std::int32_t y) noexcept {
    return (static_cast<std::uint64_t>(static_cast<std::uint32_t>(x)) << 32U) |
           static_cast<std::uint32_t>(y);
}

void update_bounds(TerrainWorld& world, std::int32_t x, std::int32_t y) {
    if (!world.has_bounds) {
        world.has_bounds = true;
        world.min_x = world.max_x = x;
        world.min_y = world.max_y = y;
        return;
    }
    world.min_x = std::min(world.min_x, x);
    world.min_y = std::min(world.min_y, y);
    world.max_x = std::max(world.max_x, x);
    world.max_y = std::max(world.max_y, y);
}

TerrainSeamIssue compare_seam(
    const TerrainCell& first,
    const TerrainCell& second,
    char axis,
    float tolerance) {
    TerrainSeamIssue issue;
    issue.first_cell = first.cell;
    issue.second_cell = second.cell;
    issue.axis = axis;

    for (std::size_t i = 0; i < kLandscapeVertexSide; ++i) {
        const float first_height = axis == 'x'
            ? first.heightmap.at(kLandscapeVertexSide - 1, i)
            : first.heightmap.at(i, kLandscapeVertexSide - 1);
        const float second_height = axis == 'x'
            ? second.heightmap.at(0, i)
            : second.heightmap.at(i, 0);
        const float error = std::fabs(first_height - second_height);
        if (error > tolerance) {
            ++issue.mismatched_samples;
            issue.max_error = std::max(issue.max_error, error);
        }
    }
    return issue;
}

std::unordered_map<std::string, const WinningRecord*> select_land_by_cell(
    TerrainWorld& result,
    const RecordGraph& graph,
    TerrainLandSelection selection) {
    std::unordered_map<std::string, const WinningRecord*> land_by_cell;
    const auto& records = selection == TerrainLandSelection::first_candidate
        ? graph.candidates_low_to_high
        : graph.winners;

    for (const auto& record : records) {
        if (record.type != "LAND" || !record.cell.has_value()) continue;

        const auto key = stable_key(*record.cell);
        const auto [existing, inserted] = land_by_cell.emplace(key, &record);
        if (!inserted && selection == TerrainLandSelection::winning) {
            result.complete = false;
            result.diagnostics.push_back(TerrainDiagnostic{
                "multiple-land-records-for-cell",
                "More than one winning LAND FormKey is associated with CELL " +
                    format_form_key(*record.cell) + '.',
                record.key,
            });
        }
    }
    return land_by_cell;
}

} // namespace

const char* terrain_land_selection_name(TerrainLandSelection selection) noexcept {
    switch (selection) {
    case TerrainLandSelection::winning: return "winning";
    case TerrainLandSelection::first_candidate: return "first-candidate";
    }
    return "winning";
}

TerrainWorld build_terrain_world(
    const RecordGraph& graph,
    const WorldspaceCatalog& catalog,
    const FormKey& worldspace,
    float seam_tolerance,
    TerrainLandSelection land_selection) {
    TerrainWorld result;
    result.worldspace = worldspace;
    result.land_selection = land_selection;
    seam_tolerance = std::max(0.0F, seam_tolerance);

    bool found_world_defaults = false;
    for (const auto& info : catalog.worldspaces) {
        if (!same_form_key(info.key, worldspace)) continue;
        result.default_land_height = info.default_land_height;
        result.default_water_height = info.default_water_height;
        found_world_defaults = true;
        break;
    }
    if (!found_world_defaults) {
        result.complete = false;
        result.diagnostics.push_back(TerrainDiagnostic{
            "worldspace-defaults-not-resolved",
            "Terrain world could not resolve WRLD default land/water heights for " +
                format_form_key(worldspace) + '.',
            worldspace,
        });
    }

    const auto land_by_cell = select_land_by_cell(result, graph, land_selection);

    for (const auto& cell : catalog.exterior_cells) {
        if (!same_form_key(cell.worldspace, worldspace) || !cell.has_landscape) continue;

        const auto found = land_by_cell.find(stable_key(cell.key));
        if (found == land_by_cell.end()) {
            result.complete = false;
            result.diagnostics.push_back(TerrainDiagnostic{
                "land-record-not-resolved",
                "CELL " + format_form_key(cell.key) + " is marked as having LAND but no " +
                    std::string(terrain_land_selection_name(land_selection)) +
                    " LAND record was resolved.",
                cell.key,
            });
            continue;
        }

        const auto* selected_land = found->second;
        auto heightmap = decode_landscape_heightmap(*selected_land);
        if (!heightmap.valid) {
            result.complete = false;
            result.diagnostics.push_back(TerrainDiagnostic{
                "land-height-decode-failed",
                "Could not decode LAND " + format_form_key(selected_land->key) +
                    " from " + selected_land->source_plugin + ": " + heightmap.error,
                selected_land->key,
            });
            continue;
        }

        update_bounds(result, cell.x, cell.y);
        result.cells.push_back(TerrainCell{
            cell.key,
            selected_land->key,
            selected_land->source_plugin,
            cell.x,
            cell.y,
            cell.has_water,
            cell.water_height,
            std::move(heightmap),
        });
    }

    std::sort(result.cells.begin(), result.cells.end(), [](const TerrainCell& lhs, const TerrainCell& rhs) {
        if (lhs.y != rhs.y) return lhs.y < rhs.y;
        if (lhs.x != rhs.x) return lhs.x < rhs.x;
        return stable_key(lhs.cell) < stable_key(rhs.cell);
    });

    std::unordered_map<std::uint64_t, std::size_t> cell_by_coordinate;
    cell_by_coordinate.reserve(result.cells.size());
    for (std::size_t i = 0; i < result.cells.size(); ++i) {
        const auto coordinate = coordinate_key(result.cells[i].x, result.cells[i].y);
        const auto [_, inserted] = cell_by_coordinate.emplace(coordinate, i);
        if (!inserted) {
            result.complete = false;
            result.diagnostics.push_back(TerrainDiagnostic{
                "duplicate-cell-coordinate",
                "Multiple exterior CELL records resolve to grid coordinate (" +
                    std::to_string(result.cells[i].x) + ", " + std::to_string(result.cells[i].y) + ").",
                result.cells[i].cell,
            });
        }
    }

    for (const auto& cell : result.cells) {
        const auto east = cell_by_coordinate.find(coordinate_key(cell.x + 1, cell.y));
        if (east != cell_by_coordinate.end()) {
            const auto issue = compare_seam(cell, result.cells[east->second], 'x', seam_tolerance);
            if (issue.mismatched_samples != 0) result.seam_issues.push_back(issue);
        }

        const auto north = cell_by_coordinate.find(coordinate_key(cell.x, cell.y + 1));
        if (north != cell_by_coordinate.end()) {
            const auto issue = compare_seam(cell, result.cells[north->second], 'y', seam_tolerance);
            if (issue.mismatched_samples != 0) result.seam_issues.push_back(issue);
        }
    }

    return result;
}

} // namespace f4lod
