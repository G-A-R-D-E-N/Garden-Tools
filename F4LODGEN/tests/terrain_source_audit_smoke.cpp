#include "f4lod/terrain_source_audit.hpp"

#include <cassert>
#include <utility>

namespace {

f4lod::WinningRecord land(
    f4lod::FormKey key,
    f4lod::FormKey cell,
    const char* source,
    std::size_t load_order) {
    f4lod::WinningRecord record;
    record.key = std::move(key);
    record.type = "LAND";
    record.source_plugin = source;
    record.source_load_order_index = load_order;
    record.cell = std::move(cell);
    return record;
}

} // namespace

int main() {
    const f4lod::FormKey world{"Fallout4.esm", 0x100U};
    const f4lod::FormKey cell_a{"Fallout4.esm", 0x200U};
    const f4lod::FormKey cell_b{"Fallout4.esm", 0x201U};
    const f4lod::FormKey land_a{"Fallout4.esm", 0x300U};
    const f4lod::FormKey land_b1{"Fallout4.esm", 0x301U};
    const f4lod::FormKey land_b2{"Mod.esp", 0x10U};

    f4lod::RecordGraph graph;
    graph.valid = true;
    graph.candidates_low_to_high.push_back(land(land_a, cell_a, "Fallout4.esm", 0U));
    graph.candidates_low_to_high.push_back(land(land_a, cell_a, "LandscapeMod.esp", 1U));
    graph.candidates_low_to_high.push_back(land(land_b1, cell_b, "Fallout4.esm", 0U));
    graph.candidates_low_to_high.push_back(land(land_b2, cell_b, "Mod.esp", 1U));

    graph.winners.push_back(land(land_a, cell_a, "LandscapeMod.esp", 1U));
    // Two distinct winning LAND FormKeys associated with one CELL is an
    // ambiguity the terrain builder must not silently collapse.
    graph.winners.push_back(land(land_b1, cell_b, "Fallout4.esm", 0U));
    graph.winners.push_back(land(land_b2, cell_b, "Mod.esp", 1U));

    f4lod::WorldspaceCatalog catalog;
    catalog.complete = true;
    f4lod::ExteriorCellInfo a;
    a.key = cell_a;
    a.worldspace = world;
    a.x = 0;
    a.y = 0;
    a.has_landscape = true;
    catalog.exterior_cells.push_back(a);

    f4lod::ExteriorCellInfo b;
    b.key = cell_b;
    b.worldspace = world;
    b.x = 1;
    b.y = 0;
    b.has_landscape = true;
    catalog.exterior_cells.push_back(b);

    const auto audit = f4lod::audit_land_sources(graph, catalog, world);
    assert(audit.valid);
    assert(audit.landscape_cells == 2U);
    assert(audit.cells_with_multiple_candidates == 2U);
    assert(audit.cells_with_multiple_winning_forms == 1U);
    assert(audit.first_vs_winner_differences == 1U);
    assert(audit.cells.size() == 2U);

    assert(audit.cells[0].cell.id == cell_a.id);
    assert(audit.cells[0].candidate_count == 2U);
    assert(audit.cells[0].winning_form_count == 1U);
    assert(audit.cells[0].has_first_candidate);
    assert(audit.cells[0].first_candidate_source == "Fallout4.esm");
    assert(audit.cells[0].has_current_winner);
    assert(audit.cells[0].current_winner_source == "LandscapeMod.esp");
    assert(audit.cells[0].first_differs_from_current_winner);

    assert(audit.cells[1].cell.id == cell_b.id);
    assert(audit.cells[1].winning_form_count == 2U);
    assert(!audit.cells[1].has_current_winner);

    auto invalid_graph = graph;
    invalid_graph.valid = false;
    assert(!f4lod::audit_land_sources(invalid_graph, catalog, world).valid);

    auto incomplete_catalog = catalog;
    incomplete_catalog.complete = false;
    assert(!f4lod::audit_land_sources(graph, incomplete_catalog, world).valid);

    return 0;
}
