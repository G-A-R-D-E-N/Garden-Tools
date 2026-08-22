#pragma once

#include "f4lod/btr_material.hpp"
#include "f4lod/btr_writer.hpp"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace f4lod {

struct BtrMaterialAttachResult {
    bool valid{};
    std::string error;
    std::int32_t property_block{-1};
    std::int32_t texture_set_block{-1};
};

[[nodiscard]] BtrMaterialAttachResult attach_calibrated_lighting_material(
    TerrainBtrBuildOptions& options,
    const BtrMaterialInspection& calibration,
    std::size_t property_slot = 0,
    const std::vector<std::string>& replacement_textures = {});

} // namespace f4lod
