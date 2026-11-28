#include "fo4/previs/uvd.hpp"

#include "fo4/error.hpp"

#include <array>
#include <cstring>
#include <string>

namespace fo4::previs {
namespace {

std::uint32_t u32le(std::span<const std::byte> data, std::size_t at) {
  std::uint32_t v = 0;
  std::memcpy(&v, data.data() + at, 4);
  return v;
}

std::array<std::uint32_t, 256> build_crc32c_table() {
  std::array<std::uint32_t, 256> table{};
  for (std::uint32_t i = 0; i < 256; ++i) {
    std::uint32_t c = i;
    for (int k = 0; k < 8; ++k) c = (c & 1u) != 0 ? 0x82F63B78u ^ (c >> 1u) : c >> 1u;
    table[i] = c;
  }
  return table;
}

}  // namespace

std::uint32_t crc32c(std::span<const std::byte> data) noexcept {
  static const auto table = build_crc32c_table();
  std::uint32_t crc = 0xFFFFFFFFu;
  for (const auto byte : data) {
    crc = (crc >> 8u) ^ table[(crc ^ std::to_integer<std::uint8_t>(byte)) & 0xFFu];
  }
  return crc ^ 0xFFFFFFFFu;
}

UvdFile parse_uvd(std::span<const std::byte> data) {
  if (data.size() < k_uvd_header_min) throw parse_error(0, "UVD is shorter than its header");
  if (u32le(data, 0) != k_uvd_magic) throw parse_error(0, "UVD has a bad magic");
  UvdFile file;
  file.stored_crc = u32le(data, 4);
  file.bytes = data;
  const auto* base = reinterpret_cast<const char*>(data.data() + k_uvd_banner_offset);
  const std::size_t cap = data.size() - k_uvd_banner_offset;
  std::size_t len = 0;
  while (len < cap && base[len] != '\0') ++len;
  if (len == cap) throw parse_error(k_uvd_banner_offset, "UVD banner is not NUL-terminated");
  file.banner = std::string_view(base, len);
  return file;
}

std::vector<std::string> validate_uvd(std::span<const std::byte> data) {
  UvdFile file;
  try {
    file = parse_uvd(data);
  } catch (const std::exception& e) {
    return {e.what()};
  }
  std::vector<std::string> issues;
  if (data.size() % 16 != 0) issues.push_back("UVD size is not 16-byte aligned");
  if (crc32c(file.body()) != file.stored_crc) issues.push_back("UVD CRC-32C mismatch");
  if (file.banner.empty() || file.banner.front() != 'T') issues.push_back("UVD banner is missing");
  return issues;
}

std::vector<std::byte> write_uvd(const UvdWriteRequest& req) {
  const std::size_t body = 8 + req.payload.size();
  std::size_t size = body;
  if (size < k_uvd_header_min) size = k_uvd_header_min;
  if (k_uvd_banner_offset + req.banner.size() + 1 > size) {
    size = k_uvd_banner_offset + req.banner.size() + 1;
  }
  size = (size + 15) & ~std::size_t{15};
  std::vector<std::byte> out(size, std::byte{0});
  out[0] = static_cast<std::byte>(k_uvd_magic & 0xFFu);
  out[1] = static_cast<std::byte>((k_uvd_magic >> 8) & 0xFFu);
  out[2] = static_cast<std::byte>((k_uvd_magic >> 16) & 0xFFu);
  out[3] = static_cast<std::byte>((k_uvd_magic >> 24) & 0xFFu);
  if (!req.payload.empty()) {
    std::memcpy(out.data() + 8, req.payload.data(), req.payload.size());
  }
  std::memcpy(out.data() + k_uvd_banner_offset, req.banner.data(), req.banner.size());
  out[k_uvd_banner_offset + req.banner.size()] = std::byte{0};
  const auto crc = crc32c(std::span<const std::byte>{out}.subspan(8));
  out[4] = static_cast<std::byte>(crc & 0xFFu);
  out[5] = static_cast<std::byte>((crc >> 8) & 0xFFu);
  out[6] = static_cast<std::byte>((crc >> 16) & 0xFFu);
  out[7] = static_cast<std::byte>((crc >> 24) & 0xFFu);
  return out;
}

}  // namespace fo4::previs
