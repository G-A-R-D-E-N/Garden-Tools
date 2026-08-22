#include "f4lod/archive_order.hpp"
#include "f4lod/assets.hpp"
#include "f4lod/btr_geometry.hpp"
#include "f4lod/btr_material.hpp"
#include "f4lod/nif.hpp"
#include "f4lod/pipeline.hpp"
#include "f4lod/plugin.hpp"
#include "f4lod/record_graph.hpp"
#include "f4lod/scanner.hpp"
#include "f4lod/terrain_job.hpp"
#include "f4lod/terrain_source_audit.hpp"
#include "f4lod/terrain_texture_graph.hpp"
#include "f4lod/worldspace.hpp"

#include <filesystem>
#include <iostream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

constexpr std::string_view kVersion = "0.1.0-dev";

void print_help() {
    std::cout
        << "F4LODGen " << kVersion << '\n'
        << "Modern Fallout 4 LOD generation toolchain\n\n"
        << "Usage:\n"
        << "  f4lodgen --help\n"
        << "  f4lodgen --version\n"
        << "  f4lodgen --print-pipeline\n"
        << "  f4lodgen scan --game <Fallout 4 root> [options]\n"
        << "  f4lodgen scan --data <Data directory> [options]\n"
        << "  f4lodgen terrain-plan --game <Fallout 4 root> [options]\n"
        << "  f4lodgen terrain-plan --data <Data directory> [options]\n"
        << "  f4lodgen inspect-plugin <plugin.esp|esm|esl> [--json]\n"
        << "  f4lodgen inspect-btr <terrain.btr>\n\n"
        << "Environment options:\n"
        << "  --game <path>         Fallout 4 directory containing Fallout4.exe and Data\\\n"
        << "  --data <path>         Resolved Data directory; useful for deployed/VFS environments\n"
        << "  --plugins <path>      Explicit plugins.txt\n"
        << "  --mo2-profile <path>  MO2 profile directory; reads <path>/plugins.txt\n"
        << "  --ini <path>          Fallout4.ini used for [Archive] ordering (terrain-plan)\n"
        << "  --custom-ini <path>   Fallout4Custom.ini; matching keys override --ini\n"
        << "  --worldspace <EDID>   terrain-plan target; defaults to Commonwealth\n"
        << "  --land-source <mode>  developer parity mode: winner (default) or first\n"
        << "  --json                scan only: emit machine-readable JSON\n\n"
        << "For MO2, launch F4LODGen through MO2 or provide a Data view that exposes the\n"
        << "profile's effective files. Scanner and planning commands are read-only.\n";
}

void print_pipeline() {
    std::size_t index = 1;
    for (const auto& stage : f4lod::default_pipeline()) {
        std::cout << index++ << ". " << stage.id << "\n   " << stage.description << '\n';
    }
}

int run_scan(int argc, char** argv) {
    f4lod::ScanOptions options;
    std::filesystem::path mo2_profile;
    bool json = false;

    for (int i = 2; i < argc; ++i) {
        const std::string_view arg{argv[i]};
        auto require_value = [&](std::string_view option) -> const char* {
            if (i + 1 >= argc) {
                std::cerr << "Missing value for " << option << '\n';
                return nullptr;
            }
            return argv[++i];
        };

        if (arg == "--game") {
            const char* value = require_value(arg);
            if (!value) return 2;
            options.game_root = value;
        } else if (arg == "--data") {
            const char* value = require_value(arg);
            if (!value) return 2;
            options.data_root = value;
        } else if (arg == "--plugins") {
            const char* value = require_value(arg);
            if (!value) return 2;
            options.plugins_file = value;
        } else if (arg == "--mo2-profile") {
            const char* value = require_value(arg);
            if (!value) return 2;
            mo2_profile = value;
        } else if (arg == "--json") {
            json = true;
        } else {
            std::cerr << "Unknown scan option: " << arg << '\n';
            return 2;
        }
    }

    if (!mo2_profile.empty()) {
        if (!options.plugins_file.empty()) {
            std::cerr << "Use either --plugins or --mo2-profile, not both.\n";
            return 2;
        }
        options.plugins_file = mo2_profile / "plugins.txt";
    }

    const auto report = f4lod::scan_environment(options);
    std::cout << (json ? f4lod::format_scan_report_json(report)
                      : f4lod::format_scan_report_text(report));
    return report.success() ? 0 : 1;
}

std::vector<f4lod::RecordGraphPlugin> graph_inputs_from_report(const f4lod::ScanReport& report) {
    std::vector<f4lod::RecordGraphPlugin> inputs;
    inputs.reserve(report.enabled_plugin_count());
    for (const auto& plugin : report.plugins) {
        if (!plugin.enabled || !plugin.exists || !plugin.parsed) continue;
        inputs.push_back(f4lod::RecordGraphPlugin{
            plugin.load_order_index,
            plugin.name,
            plugin.resolved_path,
        });
    }
    return inputs;
}

std::vector<std::string> enabled_plugin_names_from_report(const f4lod::ScanReport& report) {
    std::vector<std::string> names;
    names.reserve(report.enabled_plugin_count());
    for (const auto& plugin : report.plugins) {
        if (plugin.enabled && plugin.exists && plugin.parsed) names.push_back(plugin.name);
    }
    return names;
}

void use_mo2_ini_if_present(
    const std::filesystem::path& profile,
    std::filesystem::path& primary_ini,
    std::filesystem::path& custom_ini) {
    if (profile.empty()) return;
    std::error_code ec;
    if (primary_ini.empty()) {
        const auto candidate = profile / "Fallout4.ini";
        if (std::filesystem::is_regular_file(candidate, ec) && !ec) primary_ini = candidate;
        ec.clear();
    }
    if (custom_ini.empty()) {
        const auto candidate = profile / "Fallout4Custom.ini";
        if (std::filesystem::is_regular_file(candidate, ec) && !ec) custom_ini = candidate;
    }
}

int run_terrain_plan(int argc, char** argv) {
    f4lod::ScanOptions options;
    std::filesystem::path mo2_profile;
    std::filesystem::path primary_ini;
    std::filesystem::path custom_ini;
    std::string worldspace_editor_id = "Commonwealth";
    f4lod::TerrainLandSelection land_selection = f4lod::TerrainLandSelection::winning;

    for (int i = 2; i < argc; ++i) {
        const std::string_view arg{argv[i]};
        auto require_value = [&](std::string_view option) -> const char* {
            if (i + 1 >= argc) {
                std::cerr << "Missing value for " << option << '\n';
                return nullptr;
            }
            return argv[++i];
        };

        if (arg == "--game") {
            const char* value = require_value(arg);
            if (!value) return 2;
            options.game_root = value;
        } else if (arg == "--data") {
            const char* value = require_value(arg);
            if (!value) return 2;
            options.data_root = value;
        } else if (arg == "--plugins") {
            const char* value = require_value(arg);
            if (!value) return 2;
            options.plugins_file = value;
        } else if (arg == "--mo2-profile") {
            const char* value = require_value(arg);
            if (!value) return 2;
            mo2_profile = value;
        } else if (arg == "--ini") {
            const char* value = require_value(arg);
            if (!value) return 2;
            primary_ini = value;
        } else if (arg == "--custom-ini") {
            const char* value = require_value(arg);
            if (!value) return 2;
            custom_ini = value;
        } else if (arg == "--worldspace") {
            const char* value = require_value(arg);
            if (!value) return 2;
            worldspace_editor_id = value;
        } else if (arg == "--land-source") {
            const char* value = require_value(arg);
            if (!value) return 2;
            const std::string_view mode{value};
            if (mode == "winner" || mode == "winning") {
                land_selection = f4lod::TerrainLandSelection::winning;
            } else if (mode == "first" || mode == "first-candidate") {
                land_selection = f4lod::TerrainLandSelection::first_candidate;
            } else {
                std::cerr << "--land-source must be winner or first.\n";
                return 2;
            }
        } else {
            std::cerr << "Unknown terrain-plan option: " << arg << '\n';
            return 2;
        }
    }

    if (!mo2_profile.empty()) {
        if (!options.plugins_file.empty()) {
            std::cerr << "Use either --plugins or --mo2-profile, not both.\n";
            return 2;
        }
        options.plugins_file = mo2_profile / "plugins.txt";
        use_mo2_ini_if_present(mo2_profile, primary_ini, custom_ini);
    }

    const auto report = f4lod::scan_environment(options);
    if (!report.success()) {
        std::cerr << f4lod::format_scan_report_text(report);
        std::cerr << "terrain-plan stopped because environment preflight failed.\n";
        return 1;
    }

    const auto inputs = graph_inputs_from_report(report);
    if (inputs.empty()) {
        std::cerr << "terrain-plan found no parsed active plugins.\n";
        return 1;
    }
    const auto graph = f4lod::build_lod_record_graph(inputs);
    if (!graph.valid) {
        std::cerr << "terrain-plan could not build a valid winning record graph.\n";
        for (const auto& diagnostic : graph.diagnostics) {
            std::cerr << "  " << diagnostic.code << ": " << diagnostic.message << '\n';
        }
        return 1;
    }

    const auto catalog = f4lod::build_worldspace_catalog(graph);
    if (!catalog.complete) {
        std::cerr << "terrain-plan worldspace catalog is incomplete.\n";
        for (const auto& diagnostic : catalog.diagnostics) {
            std::cerr << "  " << diagnostic.code << ": " << diagnostic.message << '\n';
        }
        return 1;
    }

    const auto* audit_worldspace = f4lod::find_worldspace_by_editor_id(catalog, worldspace_editor_id);
    if (!audit_worldspace) {
        std::cerr << "terrain-plan could not find worldspace EDID " << worldspace_editor_id << ".\n";
        return 1;
    }
    const auto land_audit = f4lod::audit_land_sources(graph, catalog, audit_worldspace->key);
    if (!land_audit.valid) {
        std::cerr << "terrain-plan LAND source audit failed: " << land_audit.error << '\n';
        return 1;
    }

    f4lod::ArchiveOrderOptions archive_options;
    archive_options.data_root = report.data_root;
    archive_options.primary_ini = primary_ini;
    archive_options.custom_ini = custom_ini;
    archive_options.enabled_plugins_low_to_high = enabled_plugin_names_from_report(report);
    const auto archive_order = f4lod::build_fallout4_archive_order(archive_options);
    if (!archive_order.valid) {
        std::cerr << "terrain-plan could not build Fallout 4 archive precedence: "
                  << archive_order.error << '\n';
        return 1;
    }
    for (const auto& warning : archive_order.warnings) {
        std::cerr << "archive-order warning: " << warning << '\n';
    }

    const f4lod::AssetResolver assets(report.data_root, archive_order.archives_low_to_high);
    const auto plan = f4lod::prepare_terrain_generation_plan(
        graph, catalog, assets, worldspace_editor_id, land_selection);
    if (!plan.valid) {
        std::cerr << "terrain-plan failed: " << plan.error << '\n';
        return 1;
    }

    const auto texture_graph = f4lod::build_terrain_texture_graph(graph, plan.terrain);

    std::cout
        << "F4LODGen terrain generation plan\n"
        << "Worldspace: " << plan.worldspace_editor_id
        << " (" << f4lod::format_form_key(plan.worldspace) << ")\n"
        << "LAND selection: " << f4lod::terrain_land_selection_name(plan.land_selection) << '\n'
        << "Terrain cells: " << plan.terrain.cells.size() << '\n'
        << "Cell bounds: " << plan.terrain.min_x << ',' << plan.terrain.min_y
        << " -> " << plan.terrain.max_x << ',' << plan.terrain.max_y << '\n'
        << "LAND seams: " << plan.terrain.seam_issues.size() << '\n'
        << "LAND source audit: landscape-cells=" << land_audit.landscape_cells
        << " multi-candidate=" << land_audit.cells_with_multiple_candidates
        << " first-vs-winner=" << land_audit.first_vs_winner_differences
        << " multi-winning-form=" << land_audit.cells_with_multiple_winning_forms << '\n'
        << "Archive order: " << archive_order.entries_low_to_high.size()
        << " loaded BA2s (" << archive_order.general_archive_count << " General, "
        << archive_order.texture_archive_count << " Texture)\n"
        << "Terrain texture graph: " << (texture_graph.valid ? "ready" : "incomplete")
        << " cells=" << texture_graph.cells.size()
        << " LTEX=" << texture_graph.landscape_textures.size()
        << " TXST=" << texture_graph.texture_sets.size()
        << " diagnostics=" << texture_graph.diagnostics.size() << '\n'
        << "LOD settings: " << f4lod::asset_source_kind_name(plan.lod_settings_source.kind)
        << " " << plan.lod_settings_source.container.string() << '\n'
        << "LOD anchor: " << plan.lod_settings.southwest_x << ',' << plan.lod_settings.southwest_y << '\n'
        << "LOD range: " << plan.lod_settings.lod_level_min
        << " -> " << plan.lod_settings.lod_level_max << '\n'
        << "LOD stride: " << plan.lod_settings.stride << '\n'
        << "Levels: " << plan.levels.size() << '\n';

    if (land_audit.first_vs_winner_differences != 0U) {
        std::cout
            << "LAND parity note: first resolved LAND candidate differs from the current winner in "
            << land_audit.first_vs_winner_differences
            << " cell(s). Use --land-source first only for #19 comparison testing; release default remains winner.\n";
    }
    if (!texture_graph.valid) {
        for (const auto& diagnostic : texture_graph.diagnostics) {
            std::cout << "terrain-texture diagnostic: " << diagnostic.code
                      << ": " << diagnostic.message << '\n';
        }
    }

    for (const auto& level : plan.levels) {
        std::size_t cell_references = 0;
        for (const auto& block : level.blocks) cell_references += block.cells.size();
        std::cout << "  LOD " << level.lod_level
                  << ": blocks=" << level.blocks.size()
                  << " cells=" << cell_references << '\n';
    }
    std::cout << "Total blocks: " << plan.total_block_count() << '\n'
              << "Result: READY\n";
    return 0;
}

int run_inspect_plugin(int argc, char** argv) {
    if (argc < 3) {
        std::cerr << "inspect-plugin requires a plugin path.\n";
        return 2;
    }

    const std::filesystem::path path{argv[2]};
    bool json = false;
    for (int i = 3; i < argc; ++i) {
        const std::string_view arg{argv[i]};
        if (arg == "--json") json = true;
        else {
            std::cerr << "Unknown inspect-plugin option: " << arg << '\n';
            return 2;
        }
    }

    const auto inspection = f4lod::inspect_plugin(path);
    std::cout << (json ? f4lod::format_plugin_inspection_json(inspection)
                      : f4lod::format_plugin_inspection_text(inspection));
    return inspection.valid ? 0 : 1;
}

void print_btr_property(
    const std::filesystem::path& path,
    const f4lod::BtrInspection& btr,
    std::size_t slot,
    std::int32_t ref) {
    std::cout << "Shader property " << slot << ": ";
    if (ref < 0) {
        std::cout << "<none>\n";
        return;
    }
    if (static_cast<std::size_t>(ref) >= btr.nif.blocks.size()) {
        std::cout << ref << " <out of range>\n";
        return;
    }

    const auto& block = btr.nif.blocks[static_cast<std::size_t>(ref)];
    std::cout << ref << " " << block.type << '\n';
    if (block.type != "BSLightingShaderProperty") return;

    const auto material = f4lod::inspect_btr_material(path, ref);
    if (!material.valid) {
        std::cout << "  material decode: " << material.error << '\n';
        return;
    }

    const auto& lighting = material.lighting;
    std::cout
        << "  shader type: " << lighting.shader_type << '\n'
        << "  flags1: 0x" << std::hex << lighting.shader_flags_1
        << " flags2: 0x" << lighting.shader_flags_2 << std::dec << '\n'
        << "  UV offset: " << lighting.uv_offset[0] << ", " << lighting.uv_offset[1] << '\n'
        << "  UV scale: " << lighting.uv_scale[0] << ", " << lighting.uv_scale[1] << '\n'
        << "  texture set: " << lighting.texture_set << '\n'
        << "  clamp mode: " << lighting.texture_clamp_mode << '\n'
        << "  alpha: " << lighting.alpha << '\n'
        << "  backlight: " << lighting.backlight_power << '\n';

    if (material.texture_set.valid) {
        for (std::size_t i = 0; i < material.texture_set.textures.size(); ++i) {
            std::cout << "    texture[" << i << "]: " << material.texture_set.textures[i] << '\n';
        }
    }
}

int run_inspect_btr(int argc, char** argv) {
    if (argc != 3) {
        std::cerr << "inspect-btr requires exactly one .btr path.\n";
        return 2;
    }

    const std::filesystem::path path{argv[2]};
    const auto btr = f4lod::inspect_fallout4_btr(path);
    if (!btr.valid) {
        std::cerr << "BTR inspection failed: " << btr.error << '\n';
        return 1;
    }

    std::cout
        << "F4LODGen Fallout 4 BTR inspection\n"
        << "Path: " << path.string() << '\n'
        << "NIF: " << btr.nif.header_string << '\n'
        << "Version: " << btr.nif.version
        << " user=" << btr.nif.user_version
        << " bethesda=" << btr.nif.user_version2 << '\n'
        << "Blocks: " << btr.nif.blocks.size() << '\n';

    for (std::size_t i = 0; i < btr.nif.blocks.size(); ++i) {
        const auto& block = btr.nif.blocks[i];
        std::cout << "  [" << i << "] " << block.type
                  << " size=" << block.size
                  << " offset=" << block.offset << '\n';
    }

    std::cout
        << "Root children: " << btr.root_children.size() << '\n'
        << "Terrain block: " << btr.terrain_shape << '\n'
        << "Multibound: " << btr.root_multibound
        << " -> AABB " << btr.bounds_data << '\n'
        << "Bounds center: " << btr.bounds_position[0] << ", "
        << btr.bounds_position[1] << ", " << btr.bounds_position[2] << '\n'
        << "Bounds extent: " << btr.bounds_extent[0] << ", "
        << btr.bounds_extent[1] << ", " << btr.bounds_extent[2] << '\n';

    const auto geometry = f4lod::inspect_fallout4_btr_geometry(path, btr.terrain_shape);
    if (!geometry.valid) {
        std::cerr << "Terrain geometry inspection failed: " << geometry.error << '\n';
        return 1;
    }

    std::cout
        << "Terrain type: " << geometry.type << '\n'
        << "Vertices: " << geometry.vertex_count << '\n'
        << "Triangles: " << geometry.triangle_count << '\n'
        << "Vertex flags: 0x" << std::hex << geometry.vertex_flags << std::dec << '\n'
        << "Vertex stride: " << geometry.vertex_stride << " bytes\n"
        << "Geometry bytes: " << geometry.data_size << '\n';

    if (geometry.type == "BSSubIndexTriShape") {
        std::cout
            << "Subindex triangles: " << geometry.subindex_triangle_count << '\n'
            << "Subindex groups: " << geometry.subindex_a
            << " / " << geometry.subindex_b << '\n';
    }

    print_btr_property(path, btr, 0, geometry.shader_properties[0]);
    print_btr_property(path, btr, 1, geometry.shader_properties[1]);
    if (btr.water_node >= 0) std::cout << "Water node: " << btr.water_node << '\n';
    return 0;
}

} // namespace

int main(int argc, char** argv) {
    if (argc == 1) {
        print_help();
        return 0;
    }

    const std::string_view arg{argv[1]};
    if (arg == "--help" || arg == "-h") {
        print_help();
        return 0;
    }
    if (arg == "--version") {
        std::cout << kVersion << '\n';
        return 0;
    }
    if (arg == "--print-pipeline") {
        print_pipeline();
        return 0;
    }
    if (arg == "scan") return run_scan(argc, argv);
    if (arg == "terrain-plan") return run_terrain_plan(argc, argv);
    if (arg == "inspect-plugin") return run_inspect_plugin(argc, argv);
    if (arg == "inspect-btr") return run_inspect_btr(argc, argv);

    std::cerr << "Unknown argument: " << arg << "\nUse --help for available commands.\n";
    return 2;
}
