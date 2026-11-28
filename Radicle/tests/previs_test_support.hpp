#pragma once

#include "fo4/previs/detect.hpp"
#include "fo4/previs/uvd.hpp"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <initializer_list>
#include <span>
#include <string>
#include <vector>

namespace fo4::test::previs {

inline std::vector<std::byte> bytes_of(std::initializer_list<std::uint32_t> words) {
  std::vector<std::byte> out;
  for (const auto v : words) {
    out.push_back(static_cast<std::byte>(v & 0xFFu));
    out.push_back(static_cast<std::byte>((v >> 8) & 0xFFu));
    out.push_back(static_cast<std::byte>((v >> 16) & 0xFFu));
    out.push_back(static_cast<std::byte>((v >> 24) & 0xFFu));
  }
  return out;
}

inline std::vector<std::byte> make_uvd_bytes(const std::string& banner, std::size_t size = 0x110) {
  // The banner lives at +0xBC; the image must stay 16-byte aligned.
  if (size < 0x100) size = 0x100;
  if (0xBC + banner.size() + 1 > size) size = 0xBC + banner.size() + 1;
  size = (size + 15) & ~std::size_t{15};
  std::vector<std::byte> out(size, std::byte{0});
  // Byte offset 0 must hold the LE u32 magic 0xD6000012.
  out[0] = static_cast<std::byte>(0x12);
  out[1] = static_cast<std::byte>(0x00);
  out[2] = static_cast<std::byte>(0x00);
  out[3] = static_cast<std::byte>(0xD6);
  std::memcpy(out.data() + 0xBC, banner.data(), banner.size() + 1);
  const std::span<const std::byte> body(out.data() + 8, out.size() - 8);
  const auto crc = fo4::previs::crc32c(body);
  out[4] = static_cast<std::byte>(crc & 0xFFu);
  out[5] = static_cast<std::byte>((crc >> 8) & 0xFFu);
  out[6] = static_cast<std::byte>((crc >> 16) & 0xFFu);
  out[7] = static_cast<std::byte>((crc >> 24) & 0xFFu);
  return out;
}

inline const fo4::previs::CellIssue* find_issue(const std::vector<fo4::previs::CellIssue>& issues,
                                         fo4::previs::PrevisIssue code) {
  for (const auto& issue : issues) {
    if (issue.code == code) return &issue;
  }
  return nullptr;
}

}  // namespace fo4::test::previs
