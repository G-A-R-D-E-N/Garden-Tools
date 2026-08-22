#include "f4lod/terrain_source_audit.hpp"

#include <algorithm>
#include <cctype>
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

bool same_form_key(const FormKey& lhs, const FormKey& rhs) noexcept {
    return lhs.id == rhs.id && lower_ascii(lhs.plugin) == lower_ascii(rhs.plugin);
}

std::string key_string(const FormKey& key) {
    return lower_ascii(key.plugin) + ':' + std::to_string(key.id);
}

} // namespace

LandSourceAudit audit_land_sources(
    const RecordGraph& graph,
    const WorldspaceCatalog& catalog,
    const FormKey& worldspace) {
    LandSourceAudit result;
    if (!graph.valid) {
        result.error = "Cannot audit LAND sources from an invalid record graph.";
        return result;
    }
    if (!catalog.complete) {
        result.error = "Cannot audit LAND sources from an incomplete worldspace catalog.";
        return result;
    }

    std::unordered_map<std::string, std::vector<const WinningRecord*>> candidates_by_cell;
    for (const auto& record : graph.candidates_low_to_high) {
        if (record.type != "LAND" || !record.cell.has_value()) continue;
        candidates_by_cell[key_string(*record.cell)].push_back(&record);
    }

    std::unordered_map<std::string, std::vector<const WinningRecord*>> winners_by_cell;
    for (const auto& record : graph.winners) {
        if (record.type != "LAND" || !record.cell.has_value()) continue;
        winners_by_cell[key_string(*record.cell)].push_back(&record);
    }

    for (const auto& cell : catalog.exterior_cells) {
        if (!same_form_key(cell.worldspace, worldspace) || !cell.has_landscape) continue;

        LandSourceAuditCell audit_cell;
        audit_cell.cell = cell.key;
        audit_cell.x = cell.x;
        audit_cell.y = cell.y;
        ++result.landscape_cells;

        const auto candidates = candidates_by_cell.find(key_string(cell.key));
        if (candidates != candidates_by_cell.end()) {
            audit_cell.candidate_count = candidates->second.size();
            if (audit_cell.candidate_count > 1U) ++result.cells_with_multiple_candidates;
            if (!candidates->second.empty()) {
                const auto* first = candidates->second.front();
                audit_cell.has_first_candidate = true;
                audit_cell.first_candidate_key = first->key;
                audit_cell.first_candidate_source = first->source_plugin;
            }
        }

        const auto winners = winners_by_cell.find(key_string(cell.key));
        if (winners != winners_by_cell.end()) {
            audit_cell.winning_form_count = winners->second.size();
            if (audit_cell.winning_form_count > 1U) {
                ++result.cells_with_multiple_winning_forms;
            } else if (audit_cell.winning_form_count == 1U) {
                const auto* winner = winners->second.front();
                audit_cell.has_current_winner = true;
                audit_cell.current_winner_key = winner->key;
                audit_cell.current_winner_source = winner->source_plugin;
            }
        }

        if (audit_cell.has_first_candidate && audit_cell.has_current_winner) {
            audit_cell.first_differs_from_current_winner =
                !same_form_key(audit_cell.first_candidate_key, audit_cell.current_winner_key) ||
                lower_ascii(audit_cell.first_candidate_source) !=
                    lower_ascii(audit_cell.current_winner_source);
            if (audit_cell.first_differs_from_current_winner) ++result.first_vs_winner_differences;
        }

        result.cells.push_back(std::move(audit_cell));
    }

    std::sort(result.cells.begin(), result.cells.end(), [](const auto& lhs, const auto& rhs) {
        if (lhs.y != rhs.y) return lhs.y < rhs.y;
        if (lhs.x != rhs.x) return lhs.x < rhs.x;
        return key_string(lhs.cell) < key_string(rhs.cell);
    });

    result.valid = true;
    return result;
}

} // namespace f4lod
