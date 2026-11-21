#pragma once

#include "fo4/previs/uvd_tile_model.hpp"

#include <cstdint>
#include <cstring>
#include <span>
#include <string>

namespace fo4::previs::detail {

inline constexpr std::uint64_t align4(std::uint64_t n) { return (n + 3u) & ~std::uint64_t{3}; }
inline constexpr std::uint64_t align16(std::uint64_t n) { return (n + 15u) & ~std::uint64_t{15}; }

inline std::string hex(std::uint64_t n) {
  static constexpr char digits[] = "0123456789abcdef";
  std::string out;
  do {
    out.insert(out.begin(), digits[n & 0xFu]);
    n >>= 4u;
  } while (n != 0);
  return "0x" + out;
}

[[noreturn]] inline void fail(const std::string& message) { throw UvdTileLayoutError(message); }

inline std::uint32_t rd32(std::span<const std::byte> b, std::uint64_t at) {
  std::uint32_t v = 0;
  std::memcpy(&v, b.data() + at, 4);
  return v;
}

inline std::uint16_t rd16(std::span<const std::byte> b, std::uint64_t at) {
  std::uint16_t v = 0;
  std::memcpy(&v, b.data() + at, 2);
  return v;
}

inline float rdf(std::span<const std::byte> b, std::uint64_t at) {
  float v = 0.0f;
  std::memcpy(&v, b.data() + at, 4);
  return v;
}

inline void wr32(std::span<std::byte> b, std::uint64_t at, std::uint32_t v) { std::memcpy(b.data() + at, &v, 4); }
inline void wr16(std::span<std::byte> b, std::uint64_t at, std::uint16_t v) { std::memcpy(b.data() + at, &v, 2); }
inline void wrf(std::span<std::byte> b, std::uint64_t at, float v) { std::memcpy(b.data() + at, &v, 4); }

}  // namespace fo4::previs::detail
