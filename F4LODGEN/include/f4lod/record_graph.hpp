#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace f4lod {

struct FormKey {
    std::string plugin;
    std::uint32_t id{};
};

struct RecordGraphPlugin {
    std::size_t load_order_index{};
    std::string name;
    std::filesystem::path path;
};

struct RecordGraphSource {
    std::size_t load_order_index{};
    std::string name;
    std::filesystem::path path;
    std::vector<std::string> masters;
};

struct WinningRecord {
    FormKey key;
    std::string type;
    std::string source_plugin;
    std::filesystem::path source_path;
    std::size_t source_load_order_index{};
    std::uint32_t flags{};
    std::uint32_t data_size{};
    std::uint64_t file_offset{};
    std::optional<FormKey> worldspace;
    std::optional<FormKey> cell;
};

struct RecordOverrideChain {
    FormKey key;
    std::string type;
    std::vector<WinningRecord> records_low_to_high;
};

struct RecordGraphCounts {
    std::size_t candidates{};
    std::size_t overrides{};
    std::size_t winners{};
    std::size_t worldspaces{};
    std::size_t cells{};
    std::size_t landscapes{};
    std::size_t landscape_textures{};
    std::size_t texture_sets{};
    std::size_t references{};
    std::size_t statics{};
};

struct RecordGraphDiagnostic {
    std::string code;
    std::string message;
    std::filesystem::path path;
};

struct RecordGraph {
    bool valid{true};
    RecordGraphCounts counts;

    // Source plugin + master tables retained once so raw FormIDs inside record
    // payloads can be resolved with exactly the same file-local master-index
    // semantics used for record headers.
    std::vector<RecordGraphSource> sources_low_to_high;

    std::vector<WinningRecord> candidates_low_to_high;
    std::vector<RecordOverrideChain> override_chains;
    std::vector<WinningRecord> winners;
    std::vector<RecordGraphDiagnostic> diagnostics;
};

[[nodiscard]] RecordGraph build_lod_record_graph(std::span<const RecordGraphPlugin> plugins);
[[nodiscard]] const RecordGraphSource* find_record_graph_source(
    const RecordGraph& graph,
    std::string_view source_plugin) noexcept;
[[nodiscard]] bool resolve_record_form_id(
    const RecordGraph& graph,
    std::string_view source_plugin,
    std::uint32_t raw_form_id,
    FormKey& key,
    std::string& error);
[[nodiscard]] const RecordOverrideChain* find_override_chain(
    const RecordGraph& graph,
    const FormKey& key) noexcept;
[[nodiscard]] const WinningRecord* find_winning_record(
    const RecordGraph& graph,
    const FormKey& key,
    std::string_view expected_type = {}) noexcept;
[[nodiscard]] std::string format_form_key(const FormKey& key);

} // namespace f4lod
