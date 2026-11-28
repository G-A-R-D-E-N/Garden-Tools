#pragma once

#include <cstdint>
#include <string_view>

namespace fo4 {

inline constexpr std::uint32_t k_nif_version = 0x14020007U;
inline constexpr std::uint32_t k_nif_user_version = 12;
inline constexpr std::uint32_t k_nif_bs_version = 130;
// Invariant: the NIF reader, the audit and the writers cap a sized string and a texture set at these values; a copy must use them.
inline constexpr std::uint32_t k_nif_max_string_length = 1024U * 1024U;
inline constexpr std::uint32_t k_nif_max_texture_slots = 4096;

inline constexpr double k_cell_world_units = 4096.0;
inline constexpr float k_cell_world_units_f32 = static_cast<float>(k_cell_world_units);

inline constexpr const char* k_game_master_cstr = "Fallout4.esm";
inline constexpr std::string_view k_game_master = k_game_master_cstr;

inline constexpr double k_twips_per_pixel = 20.0;

inline constexpr std::uint32_t k_dds_magic = 0x20534444U;

}  // namespace fo4
