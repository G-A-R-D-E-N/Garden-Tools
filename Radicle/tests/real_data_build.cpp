#include "real_data_build.hpp"

#include <gtest/gtest.h>

#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <system_error>
#include <vector>

namespace {

constexpr std::uint32_t k_fixed_file_info_signature = 0xFEEF04BDu;
constexpr std::uint32_t k_fixed_file_info_struct_version = 0x00010000u;
constexpr std::size_t k_fixed_file_info_prefix = 16;
constexpr std::size_t k_version_scan_chunk = std::size_t{1} << 20;

std::uint32_t read_u32_le(std::span<const std::byte> bytes, std::size_t offset) {
  std::uint32_t value = 0;
  for (std::size_t i = 0; i < 4; ++i) {
    value |= std::to_integer<std::uint32_t>(bytes[offset + i]) << (8 * i);
  }
  return value;
}

std::string parse_file_version_at(std::span<const std::byte> bytes, std::size_t at) {
  if (at + k_fixed_file_info_prefix > bytes.size()) return {};
  if (read_u32_le(bytes, at) != k_fixed_file_info_signature) return {};
  if (read_u32_le(bytes, at + 4) != k_fixed_file_info_struct_version) return {};
  const auto ms = read_u32_le(bytes, at + 8);
  const auto ls = read_u32_le(bytes, at + 12);
  return std::to_string(ms >> 16) + '.' + std::to_string(ms & 0xFFFFu) + '.' +
         std::to_string(ls >> 16) + '.' + std::to_string(ls & 0xFFFFu);
}

std::string scan_file_version_backward(std::ifstream& in, std::uintmax_t size) {
  if (size < k_fixed_file_info_prefix) return {};
  std::uintmax_t hi = size - k_fixed_file_info_prefix + 1;
  std::vector<std::byte> window;
  while (hi > 0) {
    const auto lo = hi > k_version_scan_chunk ? hi - k_version_scan_chunk : 0;
    const auto len = static_cast<std::size_t>(hi - lo) + k_fixed_file_info_prefix - 1;
    window.resize(len);
    in.seekg(static_cast<std::streamoff>(lo));
    if (!in.read(reinterpret_cast<char*>(window.data()), static_cast<std::streamsize>(len))) return {};
    for (auto at = hi; at-- > lo;) {
      if (auto version = parse_file_version_at(window, static_cast<std::size_t>(at - lo)); !version.empty()) {
        return version;
      }
    }
    hi = lo;
  }
  return {};
}

}  // namespace

std::string parse_file_version(std::span<const std::byte> bytes) {
  for (std::size_t at = 0; at < bytes.size(); ++at) {
    if (auto version = parse_file_version_at(bytes, at); !version.empty()) return version;
  }
  return {};
}

GameBuild game_build_from_version(const std::string& file_version) {
  unsigned major = 0;
  unsigned minor = 0;
  unsigned build = 0;
  char dot1 = 0;
  char dot2 = 0;
  std::istringstream in{file_version};
  if (!(in >> major >> dot1 >> minor >> dot2 >> build) || dot1 != '.' || dot2 != '.') {
    return {file_version, GameRelease::Unknown};
  }
  if (major == 1 && minor == 10 && build == 163) return {file_version, GameRelease::OG};
  if (major == 1 && minor == 10 && (build == 980 || build == 984)) return {file_version, GameRelease::NG};
  if (major == 1 && minor == 11) return {file_version, GameRelease::AE};
  return {file_version, GameRelease::Unknown};
}

GameBuild detect_game_build(const std::filesystem::path& data_dir) {
  if (const char* override_version = std::getenv("FO4_GAME_BUILD");
      override_version != nullptr && *override_version != '\0') {
    return game_build_from_version(override_version);
  }
  const auto exe = data_dir / ".." / "Fallout4.exe";
  std::error_code ec;
  const auto size = std::filesystem::file_size(exe, ec);
  if (ec) return game_build_from_version("");
  std::ifstream in{exe, std::ios::binary};
  return game_build_from_version(scan_file_version_backward(in, size));
}

bool exact_counts_known(const GameBuild& build) {
  return build.release == GameRelease::AE && build.file_version == "1.11.240.0";
}

void record_game_build(const GameBuild& build) {
  testing::Test::RecordProperty("game_build", build.file_version.empty() ? std::string{"unknown"} : build.file_version);
}
