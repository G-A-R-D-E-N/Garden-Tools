#pragma once

#include "f4lod/record_graph.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace f4lod {

struct WorldspaceInfo {
    FormKey key;
    std::string editor_id;
    float default_land_height{-2048.0F};
    float default_water_height{};
};

struct ExteriorCellInfo {
    FormKey key;
    FormKey worldspace;
    std::int32_t x{};
    std::int32_t y{};
    bool has_landscape{};
    bool has_water{};
    bool has_explicit_water_height{};
    float water_height{};
};

struct WorldspaceDiagnostic {
    std::string code;
    std::string message;
    FormKey record;
};

struct WorldspaceCatalog {
    bool complete{true};
    std::vector<WorldspaceInfo> worldspaces;
    std::vector<ExteriorCellInfo> exterior_cells;
    std::vector<WorldspaceDiagnostic> diagnostics;

    [[nodiscard]] std::size_t landscape_cell_count() const noexcept;
};

[[nodiscard]] WorldspaceCatalog build_worldspace_catalog(const RecordGraph& graph);
[[nodiscard]] const WorldspaceInfo* find_worldspace_by_editor_id(
    const WorldspaceCatalog& catalog,
    const std::string& editor_id) noexcept;

} // namespace f4lod
