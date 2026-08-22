#pragma once

#include "f4lod/btr_probe.hpp"
#include "f4lod/btr_writer.hpp"

#include <array>
#include <string>
#include <vector>

namespace f4lod {

struct TerrainBtrCalibration {
    bool valid{};
    bool writer_compatible{};
    std::vector<std::string> issues;
    TerrainBtrVertexPrecision position_precision{TerrainBtrVertexPrecision::full};
    TerrainBtrTransform shape_transform;
    std::uint16_t av_flags{14U};
    std::uint16_t av_flags2{};
    std::uint8_t vertex_flag3{101U};
    std::uint8_t vertex_flag4{};
    std::uint8_t vertex_flag5{};
    std::uint8_t vertex_flag8{};
    std::string shape_name;
    bool has_position_range{};
    float position_min[3]{};
    float position_max[3]{};
    bool has_uv_range{};
    float uv_min[2]{};
    float uv_max[2]{};
    std::uint32_t source_subindex_entries{};
    std::array<bool, 2> material_present{};
    std::array<BtrMaterialInspection, 2> materials;
};

[[nodiscard]] TerrainBtrCalibration calibrate_terrain_btr_writer(
    const PackedBtrProbe& probe);

[[nodiscard]] bool apply_terrain_btr_calibration(
    TerrainBtrBuildOptions& options,
    const TerrainBtrCalibration& calibration,
    const std::array<std::vector<std::string>, 2>& replacement_textures,
    std::string& error);

} // namespace f4lod
