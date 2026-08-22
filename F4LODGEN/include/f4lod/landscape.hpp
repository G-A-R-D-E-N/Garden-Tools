#pragma once

#include "f4lod/record_graph.hpp"

#include <array>
#include <cstddef>
#include <string>

namespace f4lod {

inline constexpr std::size_t kLandscapeVertexSide = 33;
inline constexpr std::size_t kLandscapeVertexCount = kLandscapeVertexSide * kLandscapeVertexSide;

struct LandscapeHeightmap {
    bool valid{};
    std::string error;
    FormKey land;
    float height_offset{};
    float min_height{};
    float max_height{};
    std::array<float, kLandscapeVertexCount> heights{};

    [[nodiscard]] float at(std::size_t x, std::size_t y) const noexcept {
        return heights[y * kLandscapeVertexSide + x];
    }
};

[[nodiscard]] LandscapeHeightmap decode_landscape_heightmap(const WinningRecord& land_record);

} // namespace f4lod
