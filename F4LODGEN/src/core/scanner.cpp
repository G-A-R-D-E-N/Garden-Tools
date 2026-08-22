#include "f4lod/scanner.hpp"

#include "f4lod/ba2.hpp"
#include "f4lod/worldspace.hpp"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <optional>
#include <set>
#include <sstream>
#include <string_view>
#include <unordered_map>

namespace f4lod {
namespace {

std::string lower_ascii(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return value;
}

bool same_form_key(const FormKey& lhs, const FormKey& rhs) {
    return lhs.id == rhs.id && lower_ascii(lhs.plugin) == lower_ascii(rhs.plugin);
}

std::string trim(std::string value) {
    const auto first = std::find_if_not(value.begin(), value.end(), [](unsigned char c) {
        return std::isspace(c) != 0;
    });
    const auto last = std::find_if_not(value.rbegin(), value.rend(), [](unsigned char c) {
        return std::isspace(c) != 0;
    }).base();
    if (first >= last) {
        return {};
    }
    return std::string(first, last);
}

bool is_plugin_extension(const std::filesystem::path& path) {
    const auto ext = lower_ascii(path.extension().string());
    return ext == ".esm" || ext == ".esp" || ext == ".esl";
}

std::optional<std::filesystem::path> default_plugins_file() {
#ifdef _WIN32
    if (const char* local_app_data = std::getenv("LOCALAPPDATA")) {
        auto path = std::filesystem::path(local_app_data) / "Fallout4" / "plugins.txt";
        if (std::filesystem::exists(path)) {
            return path;
        }
    }
#endif
    return std::nullopt;
}

void add_diagnostic(
    ScanReport& report,
    DiagnosticSeverity severity,
    std::string code,
    std::string message,
    std::filesystem::path path = {}) {
    report.diagnostics.push_back(Diagnostic{
        severity,
        std::move(code),
        std::move(message),
        std::move(path),
    });
}

void add_counts(PluginRecordCounts& destination, const PluginRecordCounts& source) {
    destination.total_records += source.total_records;
    destination.groups += source.groups;
    destination.worldspaces += source.worldspaces;
    destination.cells += source.cells;
    destination.landscapes += source.landscapes;
    destination.references += source.references;
    destination.statics += source.statics;
}

void parse_plugins(ScanReport& report) {
    if (report.plugins_file.empty()) {
        add_diagnostic(
            report,
            DiagnosticSeverity::warning,
            "plugins-file-not-set",
            "No plugins.txt was supplied or auto-detected. Plugin load-order resolution is unavailable for this scan.");
        return;
    }

    std::ifstream input(report.plugins_file, std::ios::binary);
    if (!input) {
        add_diagnostic(
            report,
            DiagnosticSeverity::error,
            "plugins-file-unreadable",
            "Could not open the plugin list.",
            report.plugins_file);
        return;
    }

    std::set<std::string> seen;
    std::string line;
    std::size_t load_order_index = 0;
    bool first_line = true;

    while (std::getline(input, line)) {
        if (first_line) {
            first_line = false;
            if (line.size() >= 3 &&
                static_cast<unsigned char>(line[0]) == 0xEF &&
                static_cast<unsigned char>(line[1]) == 0xBB &&
                static_cast<unsigned char>(line[2]) == 0xBF) {
                line.erase(0, 3);
            }
        }

        line = trim(std::move(line));
        if (line.empty() || line.front() == '#') {
            continue;
        }

        bool enabled = false;
        if (line.front() == '*') {
            enabled = true;
            line.erase(line.begin());
            line = trim(std::move(line));
        }

        if (line.empty()) {
            continue;
        }

        const std::filesystem::path plugin_path{line};
        if (!is_plugin_extension(plugin_path)) {
            add_diagnostic(
                report,
                DiagnosticSeverity::warning,
                "plugins-file-invalid-entry",
                "Ignoring plugins.txt entry that is not an ESM, ESP, or ESL: " + line,
                report.plugins_file);
            continue;
        }

        const auto normalized_name = lower_ascii(plugin_path.filename().string());
        if (!seen.insert(normalized_name).second) {
            add_diagnostic(
                report,
                DiagnosticSeverity::warning,
                "duplicate-plugin-entry",
                "Ignoring duplicate plugins.txt entry: " + plugin_path.filename().string(),
                report.plugins_file);
            continue;
        }

        auto resolved = report.data_root / plugin_path.filename();
        std::error_code ec;
        const bool exists = std::filesystem::is_regular_file(resolved, ec) && !ec;

        PluginEntry entry;
        entry.load_order_index = load_order_index++;
        entry.name = plugin_path.filename().string();
        entry.enabled = enabled;
        entry.exists = exists;
        entry.resolved_path = std::move(resolved);
        report.plugins.push_back(std::move(entry));

        if (enabled && !exists) {
            add_diagnostic(
                report,
                DiagnosticSeverity::error,
                "active-plugin-missing",
                "Active plugin is not visible in the resolved Data directory: " + plugin_path.filename().string(),
                report.plugins.back().resolved_path);
        }
    }
}

void inspect_active_plugins(ScanReport& report) {
    for (auto& plugin : report.plugins) {
        if (!plugin.enabled || !plugin.exists) {
            continue;
        }

        const auto inspection = inspect_plugin(plugin.resolved_path);
        if (!inspection.valid) {
            add_diagnostic(
                report,
                DiagnosticSeverity::error,
                "active-plugin-parse-failed",
                "Could not parse active plugin " + plugin.name + ": " + inspection.error,
                plugin.resolved_path);
            continue;
        }

        plugin.parsed = true;
        plugin.masters = inspection.masters;
        plugin.records = inspection.records;
        add_counts(report.active_records, inspection.records);
    }
}

void validate_master_dependencies(ScanReport& report) {
    std::unordered_map<std::string, std::size_t> listed;
    listed.reserve(report.plugins.size());
    for (std::size_t i = 0; i < report.plugins.size(); ++i) {
        listed.emplace(lower_ascii(report.plugins[i].name), i);
    }

    for (const auto& plugin : report.plugins) {
        if (!plugin.enabled || !plugin.parsed) {
            continue;
        }

        for (const auto& master : plugin.masters) {
            const auto master_path = report.data_root / master;
            std::error_code ec;
            const bool master_exists = std::filesystem::is_regular_file(master_path, ec) && !ec;
            if (!master_exists) {
                add_diagnostic(
                    report,
                    DiagnosticSeverity::error,
                    "plugin-master-missing",
                    plugin.name + " requires missing master " + master + '.',
                    master_path);
                continue;
            }

            const auto found = listed.find(lower_ascii(master));
            if (found == listed.end()) {
                if (lower_ascii(master) != "fallout4.esm") {
                    add_diagnostic(
                        report,
                        DiagnosticSeverity::warning,
                        "plugin-master-not-listed",
                        plugin.name + " requires " + master +
                            ", which exists in Data but is not present in the supplied plugins.txt.",
                        master_path);
                }
                continue;
            }

            const auto& master_entry = report.plugins[found->second];
            if (!master_entry.enabled) {
                add_diagnostic(
                    report,
                    DiagnosticSeverity::error,
                    "plugin-master-disabled",
                    plugin.name + " requires disabled master " + master + '.',
                    master_entry.resolved_path);
                continue;
            }

            if (master_entry.load_order_index >= plugin.load_order_index) {
                add_diagnostic(
                    report,
                    DiagnosticSeverity::error,
                    "plugin-master-after-dependent",
                    plugin.name + " appears before its master " + master + " in the supplied load order.",
                    report.plugins_file);
            }
        }
    }
}

void build_winning_record_summary(ScanReport& report) {
    std::vector<RecordGraphPlugin> inputs;
    inputs.reserve(report.enabled_plugin_count());
    for (const auto& plugin : report.plugins) {
        if (!plugin.enabled || !plugin.exists || !plugin.parsed) {
            continue;
        }
        inputs.push_back(RecordGraphPlugin{
            plugin.load_order_index,
            plugin.name,
            plugin.resolved_path,
        });
    }

    if (inputs.empty()) {
        return;
    }

    const auto graph = build_lod_record_graph(inputs);
    report.winning_records = graph.counts;
    for (const auto& diagnostic : graph.diagnostics) {
        add_diagnostic(
            report,
            DiagnosticSeverity::error,
            "record-graph-" + diagnostic.code,
            diagnostic.message,
            diagnostic.path);
    }

    const auto catalog = build_worldspace_catalog(graph);
    report.worldspaces.catalog_complete = catalog.complete;
    report.worldspaces.worldspaces = catalog.worldspaces.size();
    report.worldspaces.exterior_cells = catalog.exterior_cells.size();
    report.worldspaces.landscape_cells = catalog.landscape_cell_count();

    if (const auto* commonwealth = find_worldspace_by_editor_id(catalog, "Commonwealth")) {
        report.worldspaces.commonwealth_found = true;
        report.worldspaces.commonwealth = commonwealth->key;
        for (const auto& cell : catalog.exterior_cells) {
            if (!same_form_key(cell.worldspace, commonwealth->key)) {
                continue;
            }
            ++report.worldspaces.commonwealth_exterior_cells;
            if (cell.has_landscape) {
                ++report.worldspaces.commonwealth_landscape_cells;
            }
        }
    }

    for (const auto& diagnostic : catalog.diagnostics) {
        add_diagnostic(
            report,
            DiagnosticSeverity::warning,
            "worldspace-catalog-" + diagnostic.code,
            diagnostic.message);
    }
}

void inventory_data(ScanReport& report) {
    std::error_code ec;
    if (!std::filesystem::is_directory(report.data_root, ec) || ec) {
        add_diagnostic(
            report,
            DiagnosticSeverity::error,
            "data-root-invalid",
            "Fallout 4 Data directory does not exist or is not readable.",
            report.data_root);
        return;
    }

    const auto options = std::filesystem::directory_options::skip_permission_denied;
    std::filesystem::recursive_directory_iterator it(report.data_root, options, ec);
    std::filesystem::recursive_directory_iterator end;

    for (; it != end; it.increment(ec)) {
        if (ec) {
            add_diagnostic(
                report,
                DiagnosticSeverity::warning,
                "data-scan-entry-error",
                "A Data-directory entry could not be read and was skipped.",
                it == end ? report.data_root : it->path());
            ec.clear();
            continue;
        }

        if (!it->is_regular_file(ec) || ec) {
            ec.clear();
            continue;
        }

        const auto path = it->path();
        const auto ext = lower_ascii(path.extension().string());

        if (ext == ".ba2") {
            ++report.assets.ba2_archives;
            auto relative = std::filesystem::relative(path, report.data_root, ec);
            report.assets.archives.push_back(ec ? path.filename() : std::move(relative));
            ec.clear();
        } else if (ext == ".esm" || ext == ".esp" || ext == ".esl") {
            ++report.assets.plugin_files;
        } else if (ext == ".nif") {
            ++report.assets.visible_meshes;
        } else if (ext == ".bgsm" || ext == ".bgem") {
            ++report.assets.visible_materials;
        } else if (ext == ".dds") {
            ++report.assets.visible_textures;
        } else {
            ++report.assets.other_files;
        }
    }

    std::sort(report.assets.archives.begin(), report.assets.archives.end(), [](const auto& lhs, const auto& rhs) {
        return lower_ascii(lhs.generic_string()) < lower_ascii(rhs.generic_string());
    });
}

void index_archives(ScanReport& report) {
    for (const auto& relative_archive : report.assets.archives) {
        const auto archive_path = report.data_root / relative_archive;
        const auto inspection = inspect_ba2_index(archive_path);
        if (!inspection.valid) {
            add_diagnostic(
                report,
                DiagnosticSeverity::error,
                "ba2-index-failed",
                "Could not index BA2 archive: " + inspection.error,
                archive_path);
            continue;
        }

        ++report.assets.indexed_ba2_archives;
        report.assets.ba2_files += inspection.files.size();
        report.assets.packed_meshes += inspection.assets.meshes;
        report.assets.packed_materials += inspection.assets.materials;
        report.assets.packed_textures += inspection.assets.textures;
    }
}

std::size_t parsed_enabled_plugin_count(const ScanReport& report) {
    return static_cast<std::size_t>(std::count_if(report.plugins.begin(), report.plugins.end(), [](const PluginEntry& plugin) {
        return plugin.enabled && plugin.parsed;
    }));
}

std::string json_escape(std::string_view input) {
    std::ostringstream out;
    for (const unsigned char c : input) {
        switch (c) {
        case '"': out << "\\\""; break;
        case '\\': out << "\\\\"; break;
        case '\b': out << "\\b"; break;
        case '\f': out << "\\f"; break;
        case '\n': out << "\\n"; break;
        case '\r': out << "\\r"; break;
        case '\t': out << "\\t"; break;
        default:
            if (c < 0x20) {
                out << "\\u" << std::hex << std::setw(4) << std::setfill('0') << static_cast<int>(c)
                    << std::dec << std::setfill(' ');
            } else {
                out << static_cast<char>(c);
            }
            break;
        }
    }
    return out.str();
}

std::string path_json(const std::filesystem::path& path) {
    return json_escape(path.generic_string());
}

void write_record_counts_json(std::ostringstream& out, const PluginRecordCounts& records) {
    out << "{\"totalRecords\": " << records.total_records
        << ", \"groups\": " << records.groups
        << ", \"worldspaces\": " << records.worldspaces
        << ", \"cells\": " << records.cells
        << ", \"landscapes\": " << records.landscapes
        << ", \"references\": " << records.references
        << ", \"statics\": " << records.statics << '}';
}

void write_graph_counts_json(std::ostringstream& out, const RecordGraphCounts& records) {
    out << "{\"candidates\": " << records.candidates
        << ", \"overrides\": " << records.overrides
        << ", \"winners\": " << records.winners
        << ", \"worldspaces\": " << records.worldspaces
        << ", \"cells\": " << records.cells
        << ", \"landscapes\": " << records.landscapes
        << ", \"references\": " << records.references
        << ", \"statics\": " << records.statics << '}';
}

} // namespace

bool ScanReport::success() const noexcept {
    return std::none_of(diagnostics.begin(), diagnostics.end(), [](const Diagnostic& diagnostic) {
        return diagnostic.severity == DiagnosticSeverity::error;
    });
}

std::size_t ScanReport::enabled_plugin_count() const noexcept {
    return static_cast<std::size_t>(std::count_if(plugins.begin(), plugins.end(), [](const PluginEntry& plugin) {
        return plugin.enabled;
    }));
}

ScanReport scan_environment(const ScanOptions& options) {
    ScanReport report;
    report.game_root = options.game_root;
    report.data_root = options.data_root;
    report.plugins_file = options.plugins_file;

    if (report.data_root.empty() && !report.game_root.empty()) {
        report.data_root = report.game_root / "Data";
    }

    if (report.plugins_file.empty()) {
        if (const auto detected = default_plugins_file()) {
            report.plugins_file = *detected;
        }
    }

    if (report.game_root.empty() && report.data_root.empty()) {
        add_diagnostic(
            report,
            DiagnosticSeverity::error,
            "input-root-missing",
            "Provide either a Fallout 4 game root or a Data directory.");
        return report;
    }

    if (!report.game_root.empty()) {
        std::error_code ec;
        const auto executable = report.game_root / "Fallout4.exe";
        if (!std::filesystem::is_regular_file(executable, ec) || ec) {
            add_diagnostic(
                report,
                DiagnosticSeverity::error,
                "fallout4-exe-missing",
                "Fallout4.exe was not found under the supplied game root.",
                executable);
        }
    }

    std::error_code ec;
    const auto fallout4_esm = report.data_root / "Fallout4.esm";
    if (!std::filesystem::is_regular_file(fallout4_esm, ec) || ec) {
        add_diagnostic(
            report,
            DiagnosticSeverity::error,
            "fallout4-master-missing",
            "Fallout4.esm is not visible in the resolved Data directory.",
            fallout4_esm);
    }

    inventory_data(report);
    index_archives(report);
    parse_plugins(report);
    inspect_active_plugins(report);
    validate_master_dependencies(report);
    build_winning_record_summary(report);

    if (report.success()) {
        add_diagnostic(
            report,
            DiagnosticSeverity::info,
            "scan-complete",
            "Environment scan completed without blocking errors.");
    }

    return report;
}

const char* diagnostic_severity_name(DiagnosticSeverity severity) noexcept {
    switch (severity) {
    case DiagnosticSeverity::info: return "info";
    case DiagnosticSeverity::warning: return "warning";
    case DiagnosticSeverity::error: return "error";
    }
    return "unknown";
}

std::string format_scan_report_text(const ScanReport& report) {
    std::ostringstream out;
    out << "F4LODGen environment scan\n";
    if (!report.game_root.empty()) {
        out << "Game root:   " << report.game_root.string() << '\n';
    }
    out << "Data root:   " << report.data_root.string() << '\n';
    if (!report.plugins_file.empty()) {
        out << "Plugin list: " << report.plugins_file.string() << '\n';
    } else {
        out << "Plugin list: <not set>\n";
    }

    out << "\nPlugins\n"
        << "  listed:  " << report.plugins.size() << '\n'
        << "  enabled: " << report.enabled_plugin_count() << '\n'
        << "  parsed:  " << parsed_enabled_plugin_count(report) << '\n';

    out << "\nActive plugin records\n"
        << "  total: " << report.active_records.total_records << '\n'
        << "  WRLD:  " << report.active_records.worldspaces << '\n'
        << "  CELL:  " << report.active_records.cells << '\n'
        << "  LAND:  " << report.active_records.landscapes << '\n'
        << "  REFR:  " << report.active_records.references << '\n'
        << "  STAT:  " << report.active_records.statics << '\n';

    out << "\nWinning LOD record graph\n"
        << "  candidates: " << report.winning_records.candidates << '\n'
        << "  overrides:  " << report.winning_records.overrides << '\n'
        << "  winners:    " << report.winning_records.winners << '\n'
        << "  WRLD:       " << report.winning_records.worldspaces << '\n'
        << "  CELL:       " << report.winning_records.cells << '\n'
        << "  LAND:       " << report.winning_records.landscapes << '\n'
        << "  REFR:       " << report.winning_records.references << '\n'
        << "  STAT:       " << report.winning_records.statics << '\n';

    out << "\nWorldspace catalog\n"
        << "  complete:        " << (report.worldspaces.catalog_complete ? "yes" : "no") << '\n'
        << "  worldspaces:     " << report.worldspaces.worldspaces << '\n'
        << "  exterior cells: " << report.worldspaces.exterior_cells << '\n'
        << "  LAND cells:     " << report.worldspaces.landscape_cells << '\n'
        << "  Commonwealth:   " << (report.worldspaces.commonwealth_found ? "found" : "not found") << '\n';
    if (report.worldspaces.commonwealth_found && report.worldspaces.commonwealth.has_value()) {
        out << "    FormKey:       " << format_form_key(*report.worldspaces.commonwealth) << '\n'
            << "    exterior cells: " << report.worldspaces.commonwealth_exterior_cells << '\n'
            << "    LAND cells:     " << report.worldspaces.commonwealth_landscape_cells << '\n';
    }

    out << "\nLoose Data inventory\n"
        << "  BA2 archives: " << report.assets.ba2_archives << '\n'
        << "  plugins:      " << report.assets.plugin_files << '\n'
        << "  NIF meshes:   " << report.assets.visible_meshes << '\n'
        << "  materials:    " << report.assets.visible_materials << '\n'
        << "  DDS textures: " << report.assets.visible_textures << '\n';

    out << "\nPacked BA2 inventory\n"
        << "  indexed archives: " << report.assets.indexed_ba2_archives << '/' << report.assets.ba2_archives << '\n'
        << "  files:            " << report.assets.ba2_files << '\n'
        << "  NIF meshes:       " << report.assets.packed_meshes << '\n'
        << "  materials:        " << report.assets.packed_materials << '\n'
        << "  DDS textures:     " << report.assets.packed_textures << '\n';

    out << "\nDiagnostics\n";
    for (const auto& diagnostic : report.diagnostics) {
        out << "  [" << diagnostic_severity_name(diagnostic.severity) << "] "
            << diagnostic.code << ": " << diagnostic.message;
        if (!diagnostic.path.empty()) {
            out << " (" << diagnostic.path.string() << ')';
        }
        out << '\n';
    }

    out << "\nResult: " << (report.success() ? "PASS" : "FAIL") << '\n';
    return out.str();
}

std::string format_scan_report_json(const ScanReport& report) {
    std::ostringstream out;
    out << "{\n"
        << "  \"success\": " << (report.success() ? "true" : "false") << ",\n"
        << "  \"gameRoot\": \"" << path_json(report.game_root) << "\",\n"
        << "  \"dataRoot\": \"" << path_json(report.data_root) << "\",\n"
        << "  \"pluginsFile\": \"" << path_json(report.plugins_file) << "\",\n"
        << "  \"plugins\": [\n";

    for (std::size_t i = 0; i < report.plugins.size(); ++i) {
        const auto& plugin = report.plugins[i];
        out << "    {\"index\": " << plugin.load_order_index
            << ", \"name\": \"" << json_escape(plugin.name)
            << "\", \"enabled\": " << (plugin.enabled ? "true" : "false")
            << ", \"exists\": " << (plugin.exists ? "true" : "false")
            << ", \"parsed\": " << (plugin.parsed ? "true" : "false")
            << ", \"path\": \"" << path_json(plugin.resolved_path) << "\", \"masters\": [";

        for (std::size_t master_index = 0; master_index < plugin.masters.size(); ++master_index) {
            if (master_index != 0) {
                out << ", ";
            }
            out << '"' << json_escape(plugin.masters[master_index]) << '"';
        }

        out << "], \"records\": ";
        write_record_counts_json(out, plugin.records);
        out << '}';
        if (i + 1 != report.plugins.size()) {
            out << ',';
        }
        out << '\n';
    }

    out << "  ],\n  \"activeRecords\": ";
    write_record_counts_json(out, report.active_records);
    out << ",\n  \"winningRecords\": ";
    write_graph_counts_json(out, report.winning_records);
    out << ",\n"
        << "  \"worldspaces\": {\n"
        << "    \"catalogComplete\": " << (report.worldspaces.catalog_complete ? "true" : "false") << ",\n"
        << "    \"count\": " << report.worldspaces.worldspaces << ",\n"
        << "    \"exteriorCells\": " << report.worldspaces.exterior_cells << ",\n"
        << "    \"landscapeCells\": " << report.worldspaces.landscape_cells << ",\n"
        << "    \"commonwealthFound\": " << (report.worldspaces.commonwealth_found ? "true" : "false") << ",\n"
        << "    \"commonwealthFormKey\": \""
        << (report.worldspaces.commonwealth.has_value() ? json_escape(format_form_key(*report.worldspaces.commonwealth)) : std::string{})
        << "\",\n"
        << "    \"commonwealthExteriorCells\": " << report.worldspaces.commonwealth_exterior_cells << ",\n"
        << "    \"commonwealthLandscapeCells\": " << report.worldspaces.commonwealth_landscape_cells << "\n"
        << "  },\n"
        << "  \"assets\": {\n"
        << "    \"ba2Archives\": " << report.assets.ba2_archives << ",\n"
        << "    \"indexedBa2Archives\": " << report.assets.indexed_ba2_archives << ",\n"
        << "    \"ba2Files\": " << report.assets.ba2_files << ",\n"
        << "    \"pluginFiles\": " << report.assets.plugin_files << ",\n"
        << "    \"looseMeshes\": " << report.assets.visible_meshes << ",\n"
        << "    \"looseMaterials\": " << report.assets.visible_materials << ",\n"
        << "    \"looseTextures\": " << report.assets.visible_textures << ",\n"
        << "    \"packedMeshes\": " << report.assets.packed_meshes << ",\n"
        << "    \"packedMaterials\": " << report.assets.packed_materials << ",\n"
        << "    \"packedTextures\": " << report.assets.packed_textures << ",\n"
        << "    \"otherFiles\": " << report.assets.other_files << "\n"
        << "  },\n"
        << "  \"diagnostics\": [\n";

    for (std::size_t i = 0; i < report.diagnostics.size(); ++i) {
        const auto& diagnostic = report.diagnostics[i];
        out << "    {\"severity\": \"" << diagnostic_severity_name(diagnostic.severity)
            << "\", \"code\": \"" << json_escape(diagnostic.code)
            << "\", \"message\": \"" << json_escape(diagnostic.message)
            << "\", \"path\": \"" << path_json(diagnostic.path) << "\"}";
        if (i + 1 != report.diagnostics.size()) {
            out << ',';
        }
        out << '\n';
    }

    out << "  ]\n}\n";
    return out.str();
}

} // namespace f4lod
