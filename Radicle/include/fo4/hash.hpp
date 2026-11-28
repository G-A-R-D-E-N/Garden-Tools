#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

namespace fo4 {

inline constexpr std::uint64_t k_fnv64_offset = 0xcbf29ce484222325ULL;
inline constexpr std::uint64_t k_fnv64_prime = 0x100000001b3ULL;

constexpr std::uint64_t fnv1a64(std::string_view text, std::uint64_t hash = k_fnv64_offset) noexcept {
  for (const char ch : text) {
    hash ^= static_cast<unsigned char>(ch);
    hash *= k_fnv64_prime;
  }
  return hash;
}

constexpr std::uint64_t fnv1a64_bytes(std::span<const std::byte> bytes, std::uint64_t hash = k_fnv64_offset) noexcept {
  for (const std::byte byte : bytes) {
    hash ^= std::to_integer<std::uint64_t>(byte);
    hash *= k_fnv64_prime;
  }
  return hash;
}

constexpr std::array<std::uint32_t, 256> make_crc32_table() noexcept {
  std::array<std::uint32_t, 256> table{};
  for (std::uint32_t i = 0; i < 256; ++i) {
    std::uint32_t c = i;
    for (int k = 0; k < 8; ++k) {
      c = (c & 1u) != 0 ? 0xEDB88320u ^ (c >> 1u) : c >> 1u;
    }
    table[i] = c;
  }
  return table;
}

inline constexpr std::array<std::uint32_t, 256> k_crc32_table = make_crc32_table();

constexpr std::uint32_t crc32_raw_step(std::uint32_t crc, std::uint8_t byte) noexcept {
  return (crc >> 8u) ^ k_crc32_table[(crc ^ byte) & 0xFFu];
}

constexpr std::uint32_t crc32_raw(std::span<const std::byte> bytes) noexcept {
  std::uint32_t crc = 0;
  for (const std::byte byte : bytes) {
    crc = crc32_raw_step(crc, std::to_integer<std::uint8_t>(byte));
  }
  return crc;
}

}  // namespace fo4
