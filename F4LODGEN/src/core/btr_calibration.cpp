#include "f4lod/btr_calibration.hpp"

#include "f4lod/btr_material_writer.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>

namespace f4lod {
namespace {

bool finite_transform(const TerrainBtrTransform& transform) noexcept {
    if (!std::isfinite(transform.translation_x) || !std::isfinite(transform.translation_y) ||
        !std::isfinite(transform.translation_z) || !std::isfinite(transform.scale) ||
        transform.scale <= 0.0F) return false;
    for (float value : transform.rotation) if (!std::isfinite(value)) return false;
    return true;
}

bool approximately_orthonormal(const float (&r)[9]) noexcept {
    constexpr float tolerance = 0.002F;
    for (int row = 0; row < 3; ++row) {
        float length = 0.0F;
        for (int col = 0; col < 3; ++col) length += r[row * 3 + col] * r[row * 3 + col];
        if (std::fabs(length - 1.0F) > tolerance) return false;
    }
    for (int a = 0; a < 3; ++a) {
        for (int b = a + 1; b < 3; ++b) {
            float dot = 0.0F;
            for (int col = 0; col < 3; ++col) dot += r[a * 3 + col] * r[b * 3 + col];
            if (std::fabs(dot) > tolerance) return false;
        }
    }
    return true;
}

void add_issue(TerrainBtrCalibration& result, std::string issue) {
    result.issues.push_back(std::move(issue));
}

} // namespace

TerrainBtrCalibration calibrate_terrain_btr_writer(const PackedBtrProbe& probe) {
    TerrainBtrCalibration result;
    if (!probe.valid || !probe.btr.valid || !probe.geometry.valid) {
        result.issues.push_back("BTR probe is invalid and cannot be used for calibration.");
        return result;
    }

    const auto& g = probe.geometry;
    result.position_precision = g.full_precision_positions
        ? TerrainBtrVertexPrecision::full
        : TerrainBtrVertexPrecision::half;
    result.shape_transform.translation_x = g.translation[0];
    result.shape_transform.translation_y = g.translation[1];
    result.shape_transform.translation_z = g.translation[2];
    std::copy(std::begin(g.rotation), std::end(g.rotation), std::begin(result.shape_transform.rotation));
    result.shape_transform.scale = g.scale;
    result.av_flags = g.av_flags;
    result.av_flags2 = g.av_flags2;
    result.vertex_flag3 = g.vertex_flag3;
    result.vertex_flag4 = g.vertex_flag4;
    result.vertex_flag5 = g.vertex_flag5;
    result.vertex_flag8 = g.vertex_flag8;
    result.has_position_range = g.has_position_range;
    std::copy(std::begin(g.position_min), std::end(g.position_min), std::begin(result.position_min));
    std::copy(std::begin(g.position_max), std::end(g.position_max), std::begin(result.position_max));
    result.has_uv_range = g.has_uv_range;
    std::copy(std::begin(g.uv_min), std::end(g.uv_min), std::begin(result.uv_min));
    std::copy(std::begin(g.uv_max), std::end(g.uv_max), std::begin(result.uv_max));
    result.source_subindex_entries = g.subindex_a;
    result.material_present = probe.material_present;
    result.materials = probe.materials;

    if (g.name_index >= 0 && static_cast<std::size_t>(g.name_index) < probe.btr.nif.strings.size()) {
        result.shape_name = probe.btr.nif.strings[static_cast<std::size_t>(g.name_index)];
    }

    if (g.type != "BSSubIndexTriShape") {
        add_issue(result, "Commonwealth terrain sample is not BSSubIndexTriShape.");
    }
    constexpr std::uint16_t supported_streams = 16U | 32U | 128U | 16384U;
    if ((g.vertex_flags & 16U) == 0U || (g.vertex_flags & 32U) == 0U || (g.vertex_flags & 128U) == 0U) {
        add_issue(result, "Terrain sample does not contain the required position, UV, and normal streams.");
    }
    if ((g.vertex_flags & ~supported_streams) != 0U) {
        add_issue(result, "Terrain sample uses vertex streams not yet emitted by the F4LODGen terrain writer.");
    }
    const std::uint32_t expected_stride = result.position_precision == TerrainBtrVertexPrecision::full ? 24U : 16U;
    if (g.vertex_stride != expected_stride) {
        add_issue(result, "Terrain sample vertex stride does not match the supported calibrated position/UV/normal layout.");
    }
    if (!finite_transform(result.shape_transform)) {
        add_issue(result, "Terrain sample contains an invalid transform.");
    } else if (!approximately_orthonormal(result.shape_transform.rotation)) {
        add_issue(result, "Terrain sample rotation is not orthonormal; safe inverse transform cannot be guaranteed.");
    }
    if (g.subindex_a != 0U || g.subindex_b != 0U || !g.subindex_segments.empty()) {
        add_issue(result, "Terrain sample uses a non-empty BSSubIndexTriShape table; generated triangle segmentation is not implemented yet.");
    }
    for (std::size_t slot = 0; slot < 2U; ++slot) {
        if (g.shader_properties[slot] >= 0 && !probe.material_present[slot]) {
            add_issue(result, "Terrain sample references a shader property that is not a calibrated BSLightingShaderProperty.");
        }
    }
    if (!probe.material_present[0] && !probe.material_present[1]) {
        add_issue(result, "Terrain sample has no calibrated lighting material and would not produce renderable terrain.");
    }

    result.valid = true;
    result.writer_compatible = result.issues.empty();
    return result;
}

bool apply_terrain_btr_calibration(
    TerrainBtrBuildOptions& options,
    const TerrainBtrCalibration& calibration,
    const std::array<std::vector<std::string>, 2>& replacement_textures,
    std::string& error) {
    if (!calibration.valid) {
        error = "Terrain BTR calibration is invalid.";
        return false;
    }
    if (!calibration.writer_compatible) {
        error = "Terrain BTR calibration contains unsupported source features.";
        if (!calibration.issues.empty()) error += " First issue: " + calibration.issues.front();
        return false;
    }

    options.position_precision = calibration.position_precision;
    options.shape_transform = calibration.shape_transform;
    options.av_flags = calibration.av_flags;
    options.av_flags2 = calibration.av_flags2;
    options.vertex_flag3 = calibration.vertex_flag3;
    options.vertex_flag4 = calibration.vertex_flag4;
    options.vertex_flag5 = calibration.vertex_flag5;
    options.vertex_flag8 = calibration.vertex_flag8;
    if (!calibration.shape_name.empty()) options.shape_name = calibration.shape_name;

    for (std::size_t slot = 0; slot < 2U; ++slot) {
        if (!calibration.material_present[slot]) continue;
        const auto attached = attach_calibrated_lighting_material(
            options,
            calibration.materials[slot],
            slot,
            replacement_textures[slot]);
        if (!attached.valid) {
            error = "Could not attach calibrated terrain material slot " +
                    std::to_string(slot) + ": " + attached.error;
            return false;
        }
    }
    return true;
}

} // namespace f4lod
