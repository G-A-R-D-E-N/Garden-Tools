#include "f4lod/terrain_texture_assets.hpp"

#include <algorithm>
#include <cctype>
#include <map>
#include <utility>

namespace f4lod {
namespace {

std::string normalized_key(std::string value) {
    for (char& c : value) {
        if (c == '\\') c = '/';
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    while (!value.empty() && value.front() == '/') value.erase(value.begin());
    return value;
}

} // namespace

TerrainTextureAssetReport resolve_terrain_texture_assets(
    const TerrainTextureGraph& graph,
    const AssetResolver& assets) {
    TerrainTextureAssetReport report;
    if (!graph.valid) {
        report.valid = false;
        report.diagnostics.push_back(TerrainTextureAssetDiagnostic{
            "terrain-texture-graph-invalid",
            "Cannot resolve DDS assets from an invalid terrain texture dependency graph.",
            {},
        });
        return report;
    }

    std::map<std::string, std::string> unique_paths;
    for (const auto& texture_set : graph.texture_sets) {
        for (const auto& path : texture_set.texture_paths) {
            if (path.empty()) continue;
            ++report.referenced_slots;
            unique_paths.try_emplace(normalized_key(path), path);
        }
    }
    report.unique_paths = unique_paths.size();
    report.textures.reserve(unique_paths.size());

    for (const auto& [_, original_path] : unique_paths) {
        const auto resolved = assets.read(original_path);
        if (!resolved.valid) {
            report.valid = false;
            report.diagnostics.push_back(TerrainTextureAssetDiagnostic{
                "terrain-dds-unresolved",
                "Could not resolve terrain DDS: " + resolved.error,
                original_path,
            });
            continue;
        }

        const auto inspection = inspect_dds(resolved.data);
        if (!inspection.valid) {
            report.valid = false;
            report.diagnostics.push_back(TerrainTextureAssetDiagnostic{
                "terrain-dds-invalid",
                "Resolved terrain texture is not a supported complete DDS: " + inspection.error,
                original_path,
            });
            continue;
        }

        report.textures.push_back(TerrainResolvedTexture{
            original_path,
            resolved.source,
            inspection,
        });
    }

    std::sort(report.textures.begin(), report.textures.end(), [](const auto& lhs, const auto& rhs) {
        return normalized_key(lhs.game_path) < normalized_key(rhs.game_path);
    });
    return report;
}

} // namespace f4lod
