#pragma once

// Read-side invariant: `Vis/<plugin>/<cell>.uvd` tomes are read and validated only.
// Native tome writing is Phase 4. The real-data test must pin the CRC variant.

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace fo4::previs {

inline constexpr std::uint32_t k_uvd_magic = 0xD6000012u;
inline constexpr std::size_t k_uvd_banner_offset = 0xBC;
inline constexpr std::size_t k_uvd_header_min = 0x100;

struct UvdFile {
  std::uint32_t stored_crc{0};
  std::string_view banner;
  // Borrowed image; the caller must keep the bytes alive while the view is used.
  std::span<const std::byte> bytes;

  // CRC invariant: covers bytes [8, end).
  [[nodiscard]] std::span<const std::byte> body() const noexcept {
    return bytes.size() <= 8 ? std::span<const std::byte>{} : bytes.subspan(8);
  }
};

UvdFile parse_uvd(std::span<const std::byte> data);

// CRC-32C (Castagnoli); init and xorout must be 0xFFFFFFFF.
std::uint32_t crc32c(std::span<const std::byte> data) noexcept;

// A bad magic, size, alignment, CRC or banner must be reported.
std::vector<std::string> validate_uvd(std::span<const std::byte> data);

}  // namespace fo4::previs
