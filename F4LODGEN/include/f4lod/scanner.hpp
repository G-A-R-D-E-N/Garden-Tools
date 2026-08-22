#pragma once

#include "f4lod/plugin.hpp"
#include "f4lod/record_graph.hpp"

#include <cstddef>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace f4lod {

enum class DiagnosticSeverity {
    info,
    warning,
    error,
};

struct Diagnostic {
    DiagnosticSeverity severity{DiagnosticSeverity::info};
    std::string code;
    std::string message;
    std::filesystem::path path;
};

struct PluginEntry {
    std::size_t load_order_index{};
    std::string name;
    bool enabled{};
    bool exists{};
    bool parsed{};
    std::filesystem::path resolved_path;
    std::vector<std::string> masters;
    PluginRecordCounts records;
};

struct AssetInventory {
    std::size_t ba2_archives{};
    std::size_t indexed_ba2_archives{};
    std::size_t ba2_files{};
    std::size_t plugin_files{};
    std::size_t visible_meshes{};
    std::size_t visible_materials{};
    std::size_t visible_textures{};
    std::size_t packed_meshes{};
    std::size_t packed_materials{};
    std::size_t packed_textures{};
    std::size_t other_files{};
    std::vector<std::filesystem::path> archives;
};

struct WorldspaceScanSummary {
    bool catalog_complete{true};
    std::size_t worldspaces{};
    std::size_t exterior_cells{};
    std::size_t landscape_cells{};
    bool commonwealth_found{};
    std::optional<FormKey> commonwealth;
    std::size_t commonwealth_exterior_cells{};
    std::size_t commonwealth_landscape_cells{};
};

struct ScanOptions {
    std::filesystem::path game_root;
    std::filesystem::path data_root;
    std::filesystem::path plugins_file;
};

struct ScanReport {
    std::filesystem::path game_root;
    std::filesystem::path data_root;
    std::filesystem::path plugins_file;
    std::vector<PluginEntry> plugins;
    AssetInventory assets;
    PluginRecordCounts active_records;
    RecordGraphCounts winning_records;
    WorldspaceScanSummary worldspaces;
    std::vector<Diagnostic> diagnostics;

    [[nodiscard]] bool success() const noexcept;
    [[nodiscard]] std::size_t enabled_plugin_count() const noexcept;
};

[[nodiscard]] ScanReport scan_environment(const ScanOptions& options);
[[nodiscard]] std::string format_scan_report_text(const ScanReport& report);
[[nodiscard]] std::string format_scan_report_json(const ScanReport& report);
[[nodiscard]] const char* diagnostic_severity_name(DiagnosticSeverity severity) noexcept;

} // namespace f4lod
