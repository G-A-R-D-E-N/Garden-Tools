#include "f4lod/landscape.hpp"

#include "f4lod/record_data.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>

namespace f4lod {
namespace {

float read_f32_le(const unsigned char* data) noexcept {
    const std::uint32_t bits =
        static_cast<std::uint32_t>(data[0]) |
        (static_cast<std::uint32_t>(data[1]) << 8U) |
        (static_cast<std::uint32_t>(data[2]) << 16U) |
        (static_cast<std::uint32_t>(data[3]) << 24U);
    float value = 0.0F;
    std::memcpy(&value, &bits, sizeof(value));
    return value;
}

int signed_delta(unsigned char value) noexcept {
    return value < 0x80U ? static_cast<int>(value) : static_cast<int>(value) - 256;
}

} // namespace

LandscapeHeightmap decode_landscape_heightmap(const WinningRecord& land_record) {
    LandscapeHeightmap result;
    result.land = land_record.key;

    if (land_record.type != "LAND") {
        result.error = "Record is not LAND.";
        return result;
    }

    const auto payload = read_record_payload(land_record);
    if (!payload.valid) {
        result.error = "Could not read LAND payload: " + payload.error;
        return result;
    }

    const auto* vhgt = find_subrecord(payload, "VHGT");
    constexpr std::size_t required_size = 4 + kLandscapeVertexCount;
    if (!vhgt) {
        result.error = "LAND record has no VHGT subrecord.";
        return result;
    }
    if (vhgt->data.size() < required_size) {
        result.error = "VHGT subrecord is smaller than the Fallout 4 33x33 height grid.";
        return result;
    }

    const float offset_units = read_f32_le(vhgt->data.data());
    if (!std::isfinite(offset_units)) {
        result.error = "VHGT height offset is not finite.";
        return result;
    }

    result.height_offset = offset_units * 8.0F;
    result.min_height = std::numeric_limits<float>::infinity();
    result.max_height = -std::numeric_limits<float>::infinity();

    int row_start = 0;
    const unsigned char* deltas = vhgt->data.data() + 4;
    for (std::size_t y = 0; y < kLandscapeVertexSide; ++y) {
        row_start += signed_delta(deltas[y * kLandscapeVertexSide]);
        int height_steps = row_start;

        float height = result.height_offset + static_cast<float>(height_steps) * 8.0F;
        result.heights[y * kLandscapeVertexSide] = height;
        result.min_height = std::min(result.min_height, height);
        result.max_height = std::max(result.max_height, height);

        for (std::size_t x = 1; x < kLandscapeVertexSide; ++x) {
            height_steps += signed_delta(deltas[y * kLandscapeVertexSide + x]);
            height = result.height_offset + static_cast<float>(height_steps) * 8.0F;
            result.heights[y * kLandscapeVertexSide + x] = height;
            result.min_height = std::min(result.min_height, height);
            result.max_height = std::max(result.max_height, height);
        }
    }

    result.valid = true;
    return result;
}

} // namespace f4lod
