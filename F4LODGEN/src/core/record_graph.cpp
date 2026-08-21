#include "f4lod/record_graph.hpp"

#include "f4lod/plugin.hpp"

#include <algorithm>
#include <cctype>
#include <iomanip>
#include <map>
#include <sstream>
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

std::string map_key(const FormKey& key) {
    std::ostringstream out;
    out << lower_ascii(key.plugin) << ':' << std::hex << std::setw(6) << std::setfill('0') << key.id;
    return out.str();
}

bool same_form_key(const FormKey& lhs, const FormKey& rhs) noexcept {
    return lhs.id == rhs.id && lower_ascii(lhs.plugin) == lower_ascii(rhs.plugin);
}

bool resolve_with_source(
    const RecordGraphSource& source,
    std::uint32_t raw_form_id,
    std::string_view description,
    std::uint64_t file_offset,
    FormKey& key,
    std::string& error) {
    const auto master_index = static_cast<std::size_t>((raw_form_id >> 24U) & 0xFFU);
    const auto object_id = raw_form_id & 0x00FFFFFFU;

    if (master_index < source.masters.size()) {
        key.plugin = source.masters[master_index];
        key.id = object_id;
        return true;
    }
    if (master_index == source.masters.size()) {
        key.plugin = source.name;
        key.id = object_id;
        return true;
    }

    std::ostringstream message;
    message << source.name << ' ' << description;
    if (file_offset != 0U) message << " at 0x" << std::hex << file_offset;
    message << " uses master index " << std::dec << master_index
            << " but the plugin only declares " << source.masters.size() << " master(s).";
    error = message.str();
    return false;
}

bool resolve_optional_parent(
    const RecordGraphSource& source,
    const PluginRecordHeader& record,
    std::uint32_t raw_parent,
    std::string_view description,
    std::optional<FormKey>& destination,
    std::string& error) {
    if (raw_parent == 0U) {
        destination.reset();
        return true;
    }

    FormKey key;
    if (!resolve_with_source(
            source,
            raw_parent,
            description,
            record.file_offset,
            key,
            error)) {
        return false;
    }
    destination = std::move(key);
    return true;
}

void count_winner(RecordGraphCounts& counts, std::string_view type) {
    ++counts.winners;
    if (type == "WRLD") ++counts.worldspaces;
    else if (type == "CELL") ++counts.cells;
    else if (type == "LAND") ++counts.landscapes;
    else if (type == "LTEX") ++counts.landscape_textures;
    else if (type == "TXST") ++counts.texture_sets;
    else if (type == "REFR") ++counts.references;
    else if (type == "STAT") ++counts.statics;
}

} // namespace

std::string format_form_key(const FormKey& key) {
    std::ostringstream out;
    out << std::uppercase << std::hex << std::setw(6) << std::setfill('0') << key.id << ':' << key.plugin;
    return out.str();
}

const RecordGraphSource* find_record_graph_source(
    const RecordGraph& graph,
    std::string_view source_plugin) noexcept {
    const auto target = lower_ascii(std::string(source_plugin));
    for (const auto& source : graph.sources_low_to_high) {
        if (lower_ascii(source.name) == target) return &source;
    }
    return nullptr;
}

bool resolve_record_form_id(
    const RecordGraph& graph,
    std::string_view source_plugin,
    std::uint32_t raw_form_id,
    FormKey& key,
    std::string& error) {
    const auto* source = find_record_graph_source(graph, source_plugin);
    if (!source) {
        error = "Record graph does not contain source plugin context for " + std::string(source_plugin) + '.';
        return false;
    }
    return resolve_with_source(*source, raw_form_id, "payload FormID", 0U, key, error);
}

const RecordOverrideChain* find_override_chain(
    const RecordGraph& graph,
    const FormKey& key) noexcept {
    for (const auto& chain : graph.override_chains) {
        if (same_form_key(chain.key, key)) return &chain;
    }
    return nullptr;
}

const WinningRecord* find_winning_record(
    const RecordGraph& graph,
    const FormKey& key,
    std::string_view expected_type) noexcept {
    for (const auto& record : graph.winners) {
        if (!same_form_key(record.key, key)) continue;
        if (!expected_type.empty() && record.type != expected_type) continue;
        return &record;
    }
    return nullptr;
}

RecordGraph build_lod_record_graph(std::span<const RecordGraphPlugin> plugins) {
    RecordGraph graph;
    std::vector<RecordGraphPlugin> ordered(plugins.begin(), plugins.end());
    std::stable_sort(ordered.begin(), ordered.end(), [](const auto& lhs, const auto& rhs) {
        return lhs.load_order_index < rhs.load_order_index;
    });

    std::map<std::string, WinningRecord> winners;
    std::map<std::string, RecordOverrideChain> chains;

    for (const auto& plugin : ordered) {
        const auto index = index_lod_records(plugin.path);
        if (!index.inspection.valid) {
            graph.valid = false;
            graph.diagnostics.push_back(RecordGraphDiagnostic{
                "plugin-index-failed",
                "Could not index " + plugin.name + ": " + index.inspection.error,
                plugin.path,
            });
            continue;
        }

        graph.sources_low_to_high.push_back(RecordGraphSource{
            plugin.load_order_index,
            plugin.name,
            plugin.path,
            index.inspection.masters,
        });
        const auto& source = graph.sources_low_to_high.back();

        for (const auto& record : index.lod_records) {
            ++graph.counts.candidates;

            FormKey key;
            std::string resolution_error;
            if (!resolve_with_source(
                    source,
                    record.raw_form_id,
                    "record " + record.type,
                    record.file_offset,
                    key,
                    resolution_error)) {
                graph.valid = false;
                graph.diagnostics.push_back(RecordGraphDiagnostic{
                    "invalid-record-master-index",
                    std::move(resolution_error),
                    plugin.path,
                });
                continue;
            }

            WinningRecord candidate;
            candidate.key = std::move(key);
            candidate.type = record.type;
            candidate.source_plugin = plugin.name;
            candidate.source_path = plugin.path;
            candidate.source_load_order_index = plugin.load_order_index;
            candidate.flags = record.flags;
            candidate.data_size = record.data_size;
            candidate.file_offset = record.file_offset;

            if (!resolve_optional_parent(
                    source,
                    record,
                    record.parent_world_raw_form_id,
                    "worldspace group owner",
                    candidate.worldspace,
                    resolution_error) ||
                !resolve_optional_parent(
                    source,
                    record,
                    record.parent_cell_raw_form_id,
                    "cell group owner",
                    candidate.cell,
                    resolution_error)) {
                graph.valid = false;
                graph.diagnostics.push_back(RecordGraphDiagnostic{
                    "invalid-parent-master-index",
                    std::move(resolution_error),
                    plugin.path,
                });
                continue;
            }

            graph.candidates_low_to_high.push_back(candidate);

            const auto key_string = map_key(candidate.key);
            auto [chain_it, chain_inserted] = chains.try_emplace(key_string);
            if (chain_inserted) {
                chain_it->second.key = candidate.key;
                chain_it->second.type = candidate.type;
            } else if (chain_it->second.type != candidate.type) {
                graph.valid = false;
                graph.diagnostics.push_back(RecordGraphDiagnostic{
                    "record-type-mismatch",
                    "Form " + format_form_key(candidate.key) + " changes type from " +
                        chain_it->second.type + " to " + candidate.type + '.',
                    plugin.path,
                });
            }
            chain_it->second.records_low_to_high.push_back(candidate);

            const auto existing = winners.find(key_string);
            if (existing != winners.end()) {
                ++graph.counts.overrides;
                existing->second = std::move(candidate);
            } else {
                winners.emplace(key_string, std::move(candidate));
            }
        }
    }

    graph.override_chains.reserve(chains.size());
    for (auto& [_, chain] : chains) graph.override_chains.push_back(std::move(chain));
    std::sort(graph.override_chains.begin(), graph.override_chains.end(), [](const auto& lhs, const auto& rhs) {
        if (lhs.type != rhs.type) return lhs.type < rhs.type;
        const auto lhs_plugin = lower_ascii(lhs.key.plugin);
        const auto rhs_plugin = lower_ascii(rhs.key.plugin);
        if (lhs_plugin != rhs_plugin) return lhs_plugin < rhs_plugin;
        return lhs.key.id < rhs.key.id;
    });

    graph.winners.reserve(winners.size());
    for (auto& [_, winner] : winners) {
        count_winner(graph.counts, winner.type);
        graph.winners.push_back(std::move(winner));
    }
    std::sort(graph.winners.begin(), graph.winners.end(), [](const WinningRecord& lhs, const WinningRecord& rhs) {
        if (lhs.type != rhs.type) return lhs.type < rhs.type;
        const auto lhs_plugin = lower_ascii(lhs.key.plugin);
        const auto rhs_plugin = lower_ascii(rhs.key.plugin);
        if (lhs_plugin != rhs_plugin) return lhs_plugin < rhs_plugin;
        return lhs.key.id < rhs.key.id;
    });

    return graph;
}

} // namespace f4lod
