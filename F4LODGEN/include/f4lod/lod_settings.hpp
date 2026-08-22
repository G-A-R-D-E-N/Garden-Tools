#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <span>
#include <string>

namespace f4lod {

struct LodSettings {
    bool valid{};
    std::string error;
    std::int32_t southwest_x{};
    std::int32_t southwest_y{};
    std::int32_t stride{};
    std::int32_t lod_level_min{};
    std::int32_t lod_level_max{};

    [[nodiscard]] bool supports_level(std::uint32_t level) const noexcept;
};

[[nodiscard]] LodSettings parse_fallout4_lod_settings(std::span<const unsigned char> data);
[[nodiscard]] LodSettings read_fallout4_lod_settings(const std::filesystem::path& path);

} // namespace f4lod
