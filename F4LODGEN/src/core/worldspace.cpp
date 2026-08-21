#include "f4lod/worldspace.hpp"

#include "f4lod/record_data.hpp"

#include <algorithm>
#include <cmath>
#include <cctype>
#include <iomanip>
#include <sstream>
#include <unordered_map>
#include <utility>

namespace f4lod {
namespace {

constexpr unsigned char kCellHasWater = 0x02U;
// Bethesda/xEdit "Default" CELL water height sentinel. -2^31 is exactly
// representable as float, so compare it exactly instead of treating all very
// negative water heights as equivalent.
constexpr float kDefaultWaterHeightSentinel = -2147483648.0F;

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

void add_payload_diagnostic(
    WorldspaceCatalog& catalog,
    const WinningRecord& record,
    std::string code,
    std::string message) {
    catalog.complete = false;
    catalog.diagnostics.push_back(WorldspaceDiagnostic{
        std::move(code),
        std::move(message),
        record.key,
    });
}

bool is_default_water_value(float value) noexcept {
    return !std::isfinite(value) || value == kDefaultWaterHeightSentinel;
}

} // namespace

std::size_t WorldspaceCatalog::landscape_cell_count() const noexcept {
    return static_cast<std::size_t>(std::count_if(
        exterior_cells.begin(),
        exterior_cells.end(),
        [](const ExteriorCellInfo& cell) { return cell.has_landscape; }));
}

WorldspaceCatalog build_worldspace_catalog(const RecordGraph& graph) {
    WorldspaceCatalog catalog;
    std::unordered_map<std::string, std::size_t> cell_index;

    for (const auto& record : graph.winners) {
        if (record.type == "WRLD") {
            const auto payload = read_record_payload(record);
            if (!payload.valid) {
                add_payload_diagnostic(
                    catalog,
                    record,
                    "worldspace-payload-unreadable",
                    "Could not read WRLD " + format_form_key(record.key) + ": " + payload.error);
                continue;
            }

            WorldspaceInfo info;
            info.key = record.key;
            if (const auto* edid = find_subrecord(payload, "EDID")) {
                info.editor_id = read_zstring(*edid);
            }
            if (const auto* dnam = find_subrecord(payload, "DNAM")) {
                float land = 0.0F;
                float water = 0.0F;
                if (!read_f32_pair(*dnam, land, water)) {
                    add_payload_diagnostic(
                        catalog,
                        record,
                        "worldspace-land-data-malformed",
                        "WRLD " + format_form_key(record.key) + " has DNAM Land Data smaller than 8 bytes.");
                    continue;
                }
                if (std::isfinite(land)) info.default_land_height = land;
                if (std::isfinite(water)) info.default_water_height = water;
            }
            catalog.worldspaces.push_back(std::move(info));
            continue;
        }

        if (record.type != "CELL" || !record.worldspace.has_value()) continue;

        const auto payload = read_record_payload(record);
        if (!payload.valid) {
            add_payload_diagnostic(
                catalog,
                record,
                "cell-payload-unreadable",
                "Could not read CELL " + format_form_key(record.key) + ": " + payload.error);
            continue;
        }

        const auto* xclc = find_subrecord(payload, "XCLC");
        if (!xclc) continue;

        std::int32_t x = 0;
        std::int32_t y = 0;
        if (!read_i32_pair(*xclc, x, y)) {
            add_payload_diagnostic(
                catalog,
                record,
                "cell-grid-malformed",
                "CELL " + format_form_key(record.key) + " has an XCLC subrecord smaller than 8 bytes.");
            continue;
        }

        ExteriorCellInfo cell;
        cell.key = record.key;
        cell.worldspace = *record.worldspace;
        cell.x = x;
        cell.y = y;

        if (const auto* data = find_subrecord(payload, "DATA"); data && !data->data.empty()) {
            cell.has_water = (data->data[0] & kCellHasWater) != 0U;
        }
        if (cell.has_water) {
            if (const auto* xclw = find_subrecord(payload, "XCLW")) {
                float water = 0.0F;
                if (!read_f32(*xclw, water)) {
                    add_payload_diagnostic(
                        catalog,
                        record,
                        "cell-water-height-malformed",
                        "CELL " + format_form_key(record.key) + " has an XCLW subrecord smaller than 4 bytes.");
                    continue;
                }
                if (!is_default_water_value(water)) {
                    cell.has_explicit_water_height = true;
                    cell.water_height = water;
                }
            }
        }

        const auto index = catalog.exterior_cells.size();
        catalog.exterior_cells.push_back(std::move(cell));
        cell_index.emplace(stable_key(record.key), index);
    }

    std::unordered_map<std::string, float> default_water_by_world;
    default_water_by_world.reserve(catalog.worldspaces.size());
    for (const auto& worldspace : catalog.worldspaces) {
        default_water_by_world.emplace(stable_key(worldspace.key), worldspace.default_water_height);
    }
    for (auto& cell : catalog.exterior_cells) {
        if (!cell.has_water || cell.has_explicit_water_height) continue;
        const auto found = default_water_by_world.find(stable_key(cell.worldspace));
        if (found == default_water_by_world.end()) {
            catalog.complete = false;
            catalog.diagnostics.push_back(WorldspaceDiagnostic{
                "cell-default-water-unresolved",
                "CELL " + format_form_key(cell.key) +
                    " uses default water height but its WRLD was not resolved.",
                cell.key,
            });
            continue;
        }
        cell.water_height = found->second;
    }

    for (const auto& record : graph.winners) {
        if (record.type != "LAND" || !record.cell.has_value()) continue;
        const auto found = cell_index.find(stable_key(*record.cell));
        if (found != cell_index.end()) catalog.exterior_cells[found->second].has_landscape = true;
    }

    std::sort(catalog.worldspaces.begin(), catalog.worldspaces.end(), [](const auto& lhs, const auto& rhs) {
        const auto lhs_id = lower_ascii(lhs.editor_id);
        const auto rhs_id = lower_ascii(rhs.editor_id);
        if (lhs_id != rhs_id) return lhs_id < rhs_id;
        return stable_key(lhs.key) < stable_key(rhs.key);
    });

    std::sort(catalog.exterior_cells.begin(), catalog.exterior_cells.end(), [](const auto& lhs, const auto& rhs) {
        const auto lhs_world = stable_key(lhs.worldspace);
        const auto rhs_world = stable_key(rhs.worldspace);
        if (lhs_world != rhs_world) return lhs_world < rhs_world;
        if (lhs.y != rhs.y) return lhs.y < rhs.y;
        if (lhs.x != rhs.x) return lhs.x < rhs.x;
        return stable_key(lhs.key) < stable_key(rhs.key);
    });

    return catalog;
}

const WorldspaceInfo* find_worldspace_by_editor_id(
    const WorldspaceCatalog& catalog,
    const std::string& editor_id) noexcept {
    const auto target = lower_ascii(editor_id);
    for (const auto& worldspace : catalog.worldspaces) {
        if (lower_ascii(worldspace.editor_id) == target) return &worldspace;
    }
    return nullptr;
}

} // namespace f4lod
