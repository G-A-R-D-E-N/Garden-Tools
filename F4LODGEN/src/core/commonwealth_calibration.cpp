#include "f4lod/commonwealth_calibration.hpp"

#include "f4lod/ba2.hpp"
#include "f4lod/btr_probe.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <filesystem>
#include <string_view>
#include <vector>

namespace f4lod {
namespace {

std::string lower_ascii(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return value;
}

std::string normalize_path(std::string value) {
    for (char& c : value) if (c == '\\') c = '/';
    return lower_ascii(std::move(value));
}

bool parse_commonwealth_btr_level(const std::string& path, std::uint32_t& level) {
    const auto normalized = normalize_path(path);
    constexpr std::string_view prefix = "meshes/terrain/commonwealth/commonwealth.";
    if (!normalized.starts_with(prefix) || !normalized.ends_with(".btr")) return false;
    const auto rest = normalized.substr(prefix.size());
    const auto dot = rest.find('.');
    if (dot == std::string::npos) return false;
    try {
        const auto parsed = std::stoul(rest.substr(0, dot));
        if (parsed != 4U && parsed != 8U && parsed != 16U && parsed != 32U) return false;
        level = static_cast<std::uint32_t>(parsed);
        return true;
    } catch (...) {
        return false;
    }
}

std::filesystem::path find_base_mesh_archive(const std::filesystem::path& data_root) {
    std::error_code ec;
    if (!std::filesystem::is_directory(data_root, ec) || ec) return {};
    for (const auto& entry : std::filesystem::directory_iterator(
             data_root, std::filesystem::directory_options::skip_permission_denied, ec)) {
        if (ec) break;
        if (!entry.is_regular_file(ec) || ec) { ec.clear(); continue; }
        if (lower_ascii(entry.path().filename().string()) == "fallout4 - meshes.ba2") return entry.path();
    }
    return {};
}

std::vector<std::string> choose_samples(std::vector<std::string> paths, std::size_t count) {
    std::sort(paths.begin(), paths.end());
    paths.erase(std::unique(paths.begin(), paths.end()), paths.end());
    if (paths.size() <= count || count == 0U) return paths;
    if (count == 1U) return {paths[paths.size() / 2U]};

    std::vector<std::string> selected;
    selected.reserve(count);
    for (std::size_t i = 0; i < count; ++i) {
        const std::size_t index = i * (paths.size() - 1U) / (count - 1U);
        if (selected.empty() || selected.back() != paths[index]) selected.push_back(paths[index]);
    }
    return selected;
}

bool same_encoding(const TerrainBtrCalibration& a, const TerrainBtrCalibration& b) noexcept {
    return a.position_precision == b.position_precision &&
           a.av_flags == b.av_flags && a.av_flags2 == b.av_flags2 &&
           a.vertex_flag3 == b.vertex_flag3 && a.vertex_flag4 == b.vertex_flag4 &&
           a.vertex_flag5 == b.vertex_flag5 && a.vertex_flag8 == b.vertex_flag8 &&
           std::fabs(a.shape_transform.scale - b.shape_transform.scale) <= 0.001F;
}

void build_consensus(CommonwealthLevelCalibration& level) {
    if (level.samples.empty()) {
        level.error = "No Commonwealth BTR samples were available for this LOD level.";
        return;
    }
    const auto& first = level.samples.front().calibration;
    if (!first.valid) {
        level.error = "First Commonwealth BTR sample did not produce a valid calibration.";
        return;
    }
    level.position_precision = first.position_precision;
    level.av_flags = first.av_flags;
    level.av_flags2 = first.av_flags2;
    level.vertex_flag3 = first.vertex_flag3;
    level.vertex_flag4 = first.vertex_flag4;
    level.vertex_flag5 = first.vertex_flag5;
    level.vertex_flag8 = first.vertex_flag8;
    level.scale = first.shape_transform.scale;

    bool compatible = true;
    for (const auto& sample : level.samples) {
        if (!sample.calibration.valid) {
            compatible = false;
            level.issues.push_back(sample.game_path + ": calibration invalid.");
            continue;
        }
        if (!sample.calibration.writer_compatible) {
            compatible = false;
            for (const auto& issue : sample.calibration.issues) {
                level.issues.push_back(sample.game_path + ": " + issue);
            }
        }
        if (!same_encoding(first, sample.calibration)) {
            compatible = false;
            level.issues.push_back(sample.game_path + ": encoding differs from the level consensus sample.");
        }
    }
    level.valid = true;
    level.writer_compatible = compatible;
}

} // namespace

CommonwealthBtrCalibrationSet calibrate_installed_commonwealth_btrs(
    const std::filesystem::path& data_root,
    std::size_t samples_per_level) {
    CommonwealthBtrCalibrationSet result;
    if (samples_per_level == 0U) {
        result.error = "Commonwealth calibration requires at least one sample per LOD level.";
        return result;
    }

    result.archive = find_base_mesh_archive(data_root);
    if (result.archive.empty()) {
        result.error = "Could not find base-game Fallout4 - Meshes.ba2 in the resolved Data directory.";
        return result;
    }

    const auto archive = inspect_ba2_index(result.archive);
    if (!archive.valid || archive.type != Ba2ArchiveType::general) {
        result.error = "Base Fallout 4 meshes archive is not a readable General BA2: " + archive.error;
        return result;
    }

    constexpr std::array<std::uint32_t, 4> levels{4U, 8U, 16U, 32U};
    std::array<std::vector<std::string>, 4> paths_by_level;
    for (const auto& entry : archive.general_entries) {
        std::uint32_t level = 0;
        if (!parse_commonwealth_btr_level(entry.name, level)) continue;
        const auto found = std::find(levels.begin(), levels.end(), level);
        if (found != levels.end()) paths_by_level[static_cast<std::size_t>(found - levels.begin())].push_back(entry.name);
    }

    result.levels.reserve(levels.size());
    bool all_valid = true;
    bool all_compatible = true;
    for (std::size_t level_index = 0; level_index < levels.size(); ++level_index) {
        CommonwealthLevelCalibration level;
        level.lod_level = levels[level_index];
        const auto selected = choose_samples(paths_by_level[level_index], samples_per_level);
        if (selected.empty()) {
            level.error = "Base meshes archive contains no Commonwealth BTRs for LOD " +
                          std::to_string(level.lod_level) + '.';
            all_valid = false;
            all_compatible = false;
            result.levels.push_back(std::move(level));
            continue;
        }

        for (const auto& path : selected) {
            CommonwealthBtrSample sample;
            sample.game_path = path;
            const auto probe = probe_btr_from_general_ba2(archive, path);
            if (!probe.valid) {
                sample.calibration.issues.push_back("Probe failed: " + probe.error);
            } else {
                sample.calibration = calibrate_terrain_btr_writer(probe);
            }
            level.samples.push_back(std::move(sample));
        }
        build_consensus(level);
        all_valid = all_valid && level.valid;
        all_compatible = all_compatible && level.writer_compatible;
        result.levels.push_back(std::move(level));
    }

    result.valid = all_valid;
    result.writer_compatible = all_valid && all_compatible;
    if (!result.valid) result.error = "One or more Commonwealth LOD levels could not be calibrated from the installed base game.";
    return result;
}

} // namespace f4lod
