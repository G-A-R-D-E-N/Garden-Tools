#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace fo4::save {

inline constexpr std::uint32_t k_save_version = 15;
inline constexpr std::uint32_t k_max_screenshot_dimension = 16384;

struct SaveHeader {
  std::uint32_t version{0};
  std::uint32_t save_number{0};
  std::string player_name;
  std::uint32_t level{0};
  std::string location;
  std::string playtime;
  std::string race;
  std::uint16_t sex{0};
  float current_xp{0.0F};
  float required_xp{0.0F};
  std::uint64_t filetime{0};
  std::uint32_t screenshot_width{0};
  std::uint32_t screenshot_height{0};
  // Invariant: aliases the buffer passed to parse_save_header; 4 bytes per pixel, exactly as stored in the file.
  // Constraint: the span dangles if the caller's buffer is a temporary or is freed before the header is dropped.
  std::span<const std::byte> screenshot;
  std::uint8_t form_version{0};
  std::string game_version;
  std::vector<std::string> plugins;
  std::vector<std::string> light_plugins;
};

// Invariant: `out` is untouched on failure; text is Windows-1252 decoded to UTF-8; filetime is the raw 100 ns tick count.
bool parse_save_header(std::span<const std::byte> bytes, SaveHeader& out, std::string& error);

}  // namespace fo4::save
