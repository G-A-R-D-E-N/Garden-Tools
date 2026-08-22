#include "f4lod/lod_settings.hpp"

#include <array>
#include <fstream>
#include <vector>

namespace f4lod {
namespace {

std::uint16_t read_u16(const unsigned char* data) noexcept {
    return static_cast<std::uint16_t>(data[0]) |
           (static_cast<std::uint16_t>(data[1]) << 8U);
}

std::uint32_t read_u32(const unsigned char* data) noexcept {
    return static_cast<std::uint32_t>(data[0]) |
           (static_cast<std::uint32_t>(data[1]) << 8U) |
           (static_cast<std::uint32_t>(data[2]) << 16U) |
           (static_cast<std::uint32_t>(data[3]) << 24U);
}

std::int32_t sign_extend_i16(std::uint16_t value) noexcept {
    return value < 0x8000U
        ? static_cast<std::int32_t>(value)
        : static_cast<std::int32_t>(value) - 0x10000;
}

std::int32_t as_i32(std::uint32_t value) noexcept {
    if (value <= 0x7FFFFFFFU) {
        return static_cast<std::int32_t>(value);
    }
    return static_cast<std::int32_t>(static_cast<std::int64_t>(value) - 0x100000000LL);
}

bool is_power_of_two(std::uint32_t value) noexcept {
    return value != 0 && (value & (value - 1U)) == 0;
}

} // namespace

bool LodSettings::supports_level(std::uint32_t level) const noexcept {
    if (!valid || level > 0x7FFFFFFFU) {
        return false;
    }
    const auto signed_level = static_cast<std::int32_t>(level);
    return is_power_of_two(level) &&
           signed_level >= lod_level_min &&
           signed_level <= lod_level_max;
}

LodSettings parse_fallout4_lod_settings(std::span<const unsigned char> data) {
    LodSettings result;
    if (data.size() != 16) {
        result.error = "Fallout 4 lodsettings data must be exactly 16 bytes.";
        return result;
    }

    result.southwest_x = sign_extend_i16(read_u16(data.data()));
    result.southwest_y = sign_extend_i16(read_u16(data.data() + 2));
    result.stride = as_i32(read_u32(data.data() + 4));
    result.lod_level_min = as_i32(read_u32(data.data() + 8));
    result.lod_level_max = as_i32(read_u32(data.data() + 12));

    if (result.stride <= 0) {
        result.error = "LOD stride must be positive.";
        return result;
    }
    if (result.lod_level_min <= 0 || result.lod_level_max <= 0) {
        result.error = "LOD level range must be positive.";
        return result;
    }
    if (result.lod_level_min > result.lod_level_max) {
        result.error = "LOD minimum level exceeds maximum level.";
        return result;
    }
    if (!is_power_of_two(static_cast<std::uint32_t>(result.lod_level_min)) ||
        !is_power_of_two(static_cast<std::uint32_t>(result.lod_level_max))) {
        result.error = "LOD minimum and maximum levels must be powers of two.";
        return result;
    }

    result.valid = true;
    return result;
}

LodSettings read_fallout4_lod_settings(const std::filesystem::path& path) {
    LodSettings result;
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        result.error = "Could not open lodsettings file.";
        return result;
    }

    std::array<unsigned char, 17> buffer{};
    input.read(reinterpret_cast<char*>(buffer.data()), static_cast<std::streamsize>(buffer.size()));
    const auto count = input.gcount();
    if (count != 16 || input.peek() != std::char_traits<char>::eof()) {
        result.error = "Fallout 4 lodsettings file must contain exactly 16 bytes.";
        return result;
    }

    return parse_fallout4_lod_settings(
        std::span<const unsigned char>(buffer.data(), static_cast<std::size_t>(count)));
}

} // namespace f4lod
