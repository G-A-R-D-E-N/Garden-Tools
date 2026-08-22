#pragma once

#include "f4lod/btr_calibration.hpp"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace f4lod {

struct CommonwealthBtrSample {
    std::string game_path;
    TerrainBtrCalibration calibration;
};

struct CommonwealthLevelCalibration {
    bool valid{};
    bool writer_compatible{};
    std::string error;
    std::uint32_t lod_level{};
    TerrainBtrVertexPrecision position_precision{TerrainBtrVertexPrecision::full};
    std::uint16_t av_flags{};
    std::uint16_t av_flags2{};
    std::uint8_t vertex_flag3{};
    std::uint8_t vertex_flag4{};
    std::uint8_t vertex_flag5{};
    std::uint8_t vertex_flag8{};
    float scale{};
    std::vector<std::string> issues;
    std::vector<CommonwealthBtrSample> samples;
};

struct CommonwealthBtrCalibrationSet {
    bool valid{};
    bool writer_compatible{};
    std::string error;
    std::filesystem::path archive;
    std::vector<CommonwealthLevelCalibration> levels;
};

[[nodiscard]] CommonwealthBtrCalibrationSet calibrate_installed_commonwealth_btrs(
    const std::filesystem::path& data_root,
    std::size_t samples_per_level = 3U);

[[nodiscard]] std::string format_commonwealth_calibration_text(
    const CommonwealthBtrCalibrationSet& calibration);
[[nodiscard]] std::string format_commonwealth_calibration_json(
    const CommonwealthBtrCalibrationSet& calibration);

} // namespace f4lod
