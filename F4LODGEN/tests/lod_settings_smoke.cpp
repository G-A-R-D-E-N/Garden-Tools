#include "f4lod/lod_settings.hpp"
#include "f4lod/terrain_blocks.hpp"

#include <array>
#include <cassert>
#include <cstdint>

namespace {

void write_u16(std::array<unsigned char, 16>& data, std::size_t offset, std::uint16_t value) {
    data[offset] = static_cast<unsigned char>(value & 0xFFU);
    data[offset + 1] = static_cast<unsigned char>((value >> 8U) & 0xFFU);
}

void write_u32(std::array<unsigned char, 16>& data, std::size_t offset, std::uint32_t value) {
    data[offset] = static_cast<unsigned char>(value & 0xFFU);
    data[offset + 1] = static_cast<unsigned char>((value >> 8U) & 0xFFU);
    data[offset + 2] = static_cast<unsigned char>((value >> 16U) & 0xFFU);
    data[offset + 3] = static_cast<unsigned char>((value >> 24U) & 0xFFU);
}

} // namespace

int main() {
    std::array<unsigned char, 16> bytes{};
    write_u16(bytes, 0, static_cast<std::uint16_t>(-64));
    write_u16(bytes, 2, static_cast<std::uint16_t>(-48));
    write_u32(bytes, 4, 128);
    write_u32(bytes, 8, 4);
    write_u32(bytes, 12, 32);

    const auto settings = f4lod::parse_fallout4_lod_settings(bytes);
    assert(settings.valid);
    assert(settings.southwest_x == -64);
    assert(settings.southwest_y == -48);
    assert(settings.stride == 128);
    assert(settings.lod_level_min == 4);
    assert(settings.lod_level_max == 32);
    assert(settings.supports_level(4));
    assert(settings.supports_level(8));
    assert(settings.supports_level(16));
    assert(settings.supports_level(32));
    assert(!settings.supports_level(2));
    assert(!settings.supports_level(64));

    f4lod::TerrainWorld world;
    f4lod::TerrainCell cell;
    cell.x = -63;
    cell.y = -47;
    cell.heightmap.valid = true;
    world.cells.push_back(cell);

    const auto plan = f4lod::plan_terrain_blocks(
        world,
        {settings.southwest_x, settings.southwest_y},
        4);
    assert(plan.valid);
    assert(plan.blocks.size() == 1);
    assert(plan.blocks[0].x == -64);
    assert(plan.blocks[0].y == -48);
    assert(plan.blocks[0].cells[0].local_x == 1);
    assert(plan.blocks[0].cells[0].local_y == 1);

    auto invalid = bytes;
    write_u32(invalid, 8, 3);
    assert(!f4lod::parse_fallout4_lod_settings(invalid).valid);

    std::array<unsigned char, 15> short_data{};
    assert(!f4lod::parse_fallout4_lod_settings(short_data).valid);

    return 0;
}
