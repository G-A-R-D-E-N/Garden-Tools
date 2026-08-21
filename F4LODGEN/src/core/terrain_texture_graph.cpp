#include "f4lod/terrain_texture_graph.hpp"

#include "f4lod/record_data.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstring>
#include <map>
#include <string>
#include <string_view>
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

std::string stable_key(const FormKey& key) {
    return lower_ascii(key.plugin) + ':' + std::to_string(key.id);
}

std::uint16_t read_u16(const unsigned char* data) noexcept {
    return static_cast<std::uint16_t>(data[0]) |
           (static_cast<std::uint16_t>(data[1]) << 8U);
}

std::int16_t read_i16(const unsigned char* data) noexcept {
    const auto raw = read_u16(data);
    std::int16_t value = 0;
    std::memcpy(&value, &raw, sizeof(value));
    return value;
}

std::uint32_t read_u32(const unsigned char* data) noexcept {
    return static_cast<std::uint32_t>(data[0]) |
           (static_cast<std::uint32_t>(data[1]) << 8U) |
           (static_cast<std::uint32_t>(data[2]) << 16U) |
           (static_cast<std::uint32_t>(data[3]) << 24U);
}

float read_f32(const unsigned char* data) noexcept {
    const auto bits = read_u32(data);
    float value = 0.0F;
    std::memcpy(&value, &bits, sizeof(value));
    return value;
}

void diagnostic(
    TerrainTextureGraph& graph,
    bool blocking,
    std::string code,
    std::string message,
    FormKey record) {
    if (blocking) graph.valid = false;
    graph.diagnostics.push_back(TerrainTextureDiagnostic{
        std::move(code),
        std::move(message),
        std::move(record),
    });
}

const WinningRecord* selected_land_record(
    const RecordGraph& graph,
    const TerrainCell& cell) noexcept {
    const auto source = lower_ascii(cell.land_source_plugin);
    for (const auto& candidate : graph.candidates_low_to_high) {
        if (candidate.type != "LAND" || !same_form_key(candidate.key, cell.land)) continue;
        if (lower_ascii(candidate.source_plugin) == source) return &candidate;
    }
    const auto* winner = find_winning_record(graph, cell.land, "LAND");
    if (winner && (source.empty() || lower_ascii(winner->source_plugin) == source)) return winner;
    return nullptr;
}

bool parse_layer_header(
    const RecordGraph& record_graph,
    TerrainTextureGraph& graph,
    const WinningRecord& land_record,
    const SubrecordView& subrecord,
    bool alpha,
    TerrainLandTextureLayer& layer) {
    if (subrecord.data.size() < 8U) {
        diagnostic(
            graph,
            true,
            "land-layer-header-malformed",
            land_record.type + " " + format_form_key(land_record.key) +
                " has a " + subrecord.type + " subrecord smaller than 8 bytes.",
            land_record.key);
        return false;
    }

    layer.alpha = alpha;
    layer.quadrant = subrecord.data[4];
    layer.layer = read_i16(subrecord.data.data() + 6U);

    const auto raw_ltex = read_u32(subrecord.data.data());
    if (raw_ltex == 0U) return true;

    FormKey ltex;
    std::string error;
    if (!resolve_record_form_id(record_graph, land_record.source_plugin, raw_ltex, ltex, error)) {
        diagnostic(
            graph,
            true,
            "land-ltex-formid-unresolved",
            "Could not resolve " + subrecord.type + " LTEX reference in LAND " +
                format_form_key(land_record.key) + ": " + error,
            land_record.key);
        return false;
    }
    layer.has_landscape_texture = true;
    layer.landscape_texture = std::move(ltex);
    return true;
}

bool parse_alpha_samples(
    TerrainTextureGraph& graph,
    const WinningRecord& land_record,
    const SubrecordView& subrecord,
    TerrainLandTextureLayer& layer) {
    if (subrecord.data.size() % 8U != 0U) {
        diagnostic(
            graph,
            true,
            "land-alpha-data-malformed",
            "LAND " + format_form_key(land_record.key) +
                " has VTXT data whose size is not a multiple of 8 bytes.",
            land_record.key);
        return false;
    }

    layer.alpha_samples.reserve(subrecord.data.size() / 8U);
    for (std::size_t offset = 0; offset < subrecord.data.size(); offset += 8U) {
        const auto opacity = read_f32(subrecord.data.data() + offset + 4U);
        if (!std::isfinite(opacity)) {
            diagnostic(
                graph,
                true,
                "land-alpha-opacity-nonfinite",
                "LAND " + format_form_key(land_record.key) +
                    " contains a non-finite VTXT opacity.",
                land_record.key);
            return false;
        }
        layer.alpha_samples.push_back(TerrainAlphaSample{
            read_u16(subrecord.data.data() + offset),
            opacity,
        });
    }
    return true;
}

bool load_texture_set(
    const RecordGraph& record_graph,
    TerrainTextureGraph& graph,
    const FormKey& key,
    std::map<std::string, std::size_t>& known_texture_sets) {
    const auto stable = stable_key(key);
    if (known_texture_sets.contains(stable)) return true;

    const auto* record = find_winning_record(record_graph, key, "TXST");
    if (!record) {
        diagnostic(
            graph,
            true,
            "txst-winner-missing",
            "Referenced TXST winner was not found: " + format_form_key(key) + '.',
            key);
        return false;
    }

    const auto payload = read_record_payload(*record);
    if (!payload.valid) {
        diagnostic(
            graph,
            true,
            "txst-payload-unreadable",
            "Could not read TXST " + format_form_key(key) + ": " + payload.error,
            key);
        return false;
    }

    TextureSetDependency dependency;
    dependency.key = key;
    dependency.source_plugin = record->source_plugin;
    if (const auto* edid = find_subrecord(payload, "EDID")) dependency.editor_id = read_zstring(*edid);

    constexpr std::array<std::string_view, 8> slots{
        "TX00", "TX01", "TX02", "TX03", "TX04", "TX05", "TX06", "TX07",
    };
    for (std::size_t i = 0; i < slots.size(); ++i) {
        if (const auto* texture = find_subrecord(payload, std::string(slots[i]))) {
            dependency.texture_paths[i] = read_zstring(*texture);
        }
    }

    known_texture_sets.emplace(stable, graph.texture_sets.size());
    graph.texture_sets.push_back(std::move(dependency));
    return true;
}

bool load_landscape_texture(
    const RecordGraph& record_graph,
    TerrainTextureGraph& graph,
    const FormKey& key,
    std::map<std::string, std::size_t>& known_ltex,
    std::map<std::string, std::size_t>& known_texture_sets) {
    const auto stable = stable_key(key);
    if (known_ltex.contains(stable)) return true;

    const auto* record = find_winning_record(record_graph, key, "LTEX");
    if (!record) {
        diagnostic(
            graph,
            true,
            "ltex-winner-missing",
            "Referenced LTEX winner was not found: " + format_form_key(key) + '.',
            key);
        return false;
    }

    const auto payload = read_record_payload(*record);
    if (!payload.valid) {
        diagnostic(
            graph,
            true,
            "ltex-payload-unreadable",
            "Could not read LTEX " + format_form_key(key) + ": " + payload.error,
            key);
        return false;
    }

    LandscapeTextureDependency dependency;
    dependency.key = key;
    dependency.source_plugin = record->source_plugin;
    if (const auto* edid = find_subrecord(payload, "EDID")) dependency.editor_id = read_zstring(*edid);

    if (const auto* tnam = find_subrecord(payload, "TNAM")) {
        if (tnam->data.size() < 4U) {
            diagnostic(
                graph,
                true,
                "ltex-tnam-malformed",
                "LTEX " + format_form_key(key) + " has TNAM smaller than 4 bytes.",
                key);
            return false;
        }
        const auto raw_txst = read_u32(tnam->data.data());
        if (raw_txst != 0U) {
            FormKey txst;
            std::string error;
            if (!resolve_record_form_id(record_graph, record->source_plugin, raw_txst, txst, error)) {
                diagnostic(
                    graph,
                    true,
                    "ltex-txst-formid-unresolved",
                    "Could not resolve LTEX " + format_form_key(key) + " TNAM: " + error,
                    key);
                return false;
            }
            dependency.has_texture_set = true;
            dependency.texture_set = txst;
            if (!load_texture_set(record_graph, graph, txst, known_texture_sets)) return false;
        }
    }

    known_ltex.emplace(stable, graph.landscape_textures.size());
    graph.landscape_textures.push_back(std::move(dependency));
    return true;
}

} // namespace

TerrainTextureGraph build_terrain_texture_graph(
    const RecordGraph& record_graph,
    const TerrainWorld& terrain) {
    TerrainTextureGraph graph;
    if (!record_graph.valid) {
        graph.valid = false;
        graph.diagnostics.push_back(TerrainTextureDiagnostic{
            "record-graph-invalid",
            "Terrain texture dependencies require a valid record graph.",
            terrain.worldspace,
        });
        return graph;
    }
    if (!terrain.complete) {
        graph.valid = false;
        graph.diagnostics.push_back(TerrainTextureDiagnostic{
            "terrain-world-incomplete",
            "Terrain texture dependencies require a complete terrain source world.",
            terrain.worldspace,
        });
        return graph;
    }

    std::map<std::string, std::size_t> known_ltex;
    std::map<std::string, std::size_t> known_texture_sets;

    for (const auto& terrain_cell : terrain.cells) {
        TerrainTextureCell cell;
        cell.cell = terrain_cell.cell;
        cell.land = terrain_cell.land;
        cell.land_source_plugin = terrain_cell.land_source_plugin;

        const auto* land_record = selected_land_record(record_graph, terrain_cell);
        if (!land_record) {
            diagnostic(
                graph,
                true,
                "selected-land-record-missing",
                "Could not locate selected LAND " + format_form_key(terrain_cell.land) +
                    " from source " + terrain_cell.land_source_plugin + '.',
                terrain_cell.land);
            graph.cells.push_back(std::move(cell));
            continue;
        }

        const auto payload = read_record_payload(*land_record);
        if (!payload.valid) {
            diagnostic(
                graph,
                true,
                "land-texture-payload-unreadable",
                "Could not read LAND texture layers for " + format_form_key(land_record->key) +
                    ": " + payload.error,
                land_record->key);
            graph.cells.push_back(std::move(cell));
            continue;
        }

        for (std::size_t i = 0; i < payload.subrecords.size(); ++i) {
            const auto& subrecord = payload.subrecords[i];
            if (subrecord.type != "BTXT" && subrecord.type != "ATXT") {
                if (subrecord.type == "VTXT") {
                    diagnostic(
                        graph,
                        true,
                        "land-alpha-data-orphaned",
                        "LAND " + format_form_key(land_record->key) +
                            " contains VTXT not immediately associated with an ATXT layer.",
                        land_record->key);
                }
                continue;
            }

            TerrainLandTextureLayer layer;
            const bool alpha = subrecord.type == "ATXT";
            if (!parse_layer_header(record_graph, graph, *land_record, subrecord, alpha, layer)) continue;

            if (alpha && i + 1U < payload.subrecords.size() && payload.subrecords[i + 1U].type == "VTXT") {
                if (parse_alpha_samples(graph, *land_record, payload.subrecords[i + 1U], layer)) ++i;
            }

            if (layer.has_landscape_texture &&
                !load_landscape_texture(
                    record_graph,
                    graph,
                    layer.landscape_texture,
                    known_ltex,
                    known_texture_sets)) {
                // Dependency loader already emitted a blocking diagnostic.
            }
            cell.layers.push_back(std::move(layer));
        }

        graph.cells.push_back(std::move(cell));
    }

    std::sort(graph.landscape_textures.begin(), graph.landscape_textures.end(), [](const auto& lhs, const auto& rhs) {
        return stable_key(lhs.key) < stable_key(rhs.key);
    });
    std::sort(graph.texture_sets.begin(), graph.texture_sets.end(), [](const auto& lhs, const auto& rhs) {
        return stable_key(lhs.key) < stable_key(rhs.key);
    });
    return graph;
}

} // namespace f4lod
