#pragma once

#include "f4lod/record_graph.hpp"
#include "f4lod/worldspace.hpp"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace f4lod {

struct LandSourceAuditCell {
    FormKey cell;
    std::int32_t x{};
    std::int32_t y{};
    std::size_t candidate_count{};
    std::size_t winning_form_count{};
    bool has_first_candidate{};
    FormKey first_candidate_key;
    std::string first_candidate_source;
    bool has_current_winner{};
    FormKey current_winner_key;
    std::string current_winner_source;
    bool first_differs_from_current_winner{};
};

struct LandSourceAudit {
    bool valid{};
    std::string error;
    std::size_t landscape_cells{};
    std::size_t cells_with_multiple_candidates{};
    std::size_t cells_with_multiple_winning_forms{};
    std::size_t first_vs_winner_differences{};
    std::vector<LandSourceAuditCell> cells;
};

// Audits, but does not change, terrain LAND selection. It compares the first
// resolved LAND candidate for each exterior cell against the current winning
// LAND selection so xLODGen's documented "first LAND record for a cell"
// behavior can be validated on real profiles before generation policy changes.
[[nodiscard]] LandSourceAudit audit_land_sources(
    const RecordGraph& graph,
    const WorldspaceCatalog& catalog,
    const FormKey& worldspace);

} // namespace f4lod
