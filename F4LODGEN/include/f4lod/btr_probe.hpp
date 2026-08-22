#pragma once

#include "f4lod/ba2.hpp"
#include "f4lod/btr_geometry.hpp"
#include "f4lod/btr_material.hpp"
#include "f4lod/nif.hpp"

#include <array>
#include <filesystem>
#include <string>

namespace f4lod {

struct PackedBtrProbe {
    bool valid{};
    std::string error;
    std::filesystem::path archive;
    std::string game_path;
    BtrInspection btr;
    BtrGeometryInspection geometry;
    std::array<bool, 2> material_present{};
    std::array<BtrMaterialInspection, 2> materials;
};

[[nodiscard]] PackedBtrProbe probe_btr_from_general_ba2(
    const Ba2Inspection& archive,
    const std::string& game_path);

} // namespace f4lod
