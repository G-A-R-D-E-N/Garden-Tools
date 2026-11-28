#include "fo4/nif/header.hpp"

#include "fo4/binary/cursor.hpp"

#include <stdexcept>
#include <exception>
#include <string_view>
#include <utility>

namespace fo4::nif {
namespace {

constexpr std::uint32_t k_v3_1 = 0x03010000U;
constexpr std::uint32_t k_v5_0_0_1 = 0x05000001U;
constexpr std::uint32_t k_v10_0_1_8 = 0x0A000108U;
constexpr std::uint32_t k_v10_1_0_106 = 0x0A01006AU;
constexpr std::uint32_t k_v10_2_0_0 = 0x0A020000U;
constexpr std::uint32_t k_v20_0_0_3 = 0x14000003U;
constexpr std::uint32_t k_v20_0_0_4 = 0x14000004U;
constexpr std::uint32_t k_v20_0_0_5 = 0x14000005U;
constexpr std::uint32_t k_v20_2_0_7 = 0x14020007U;
constexpr std::uint32_t k_v30_0_0_2 = 0x1E000002U;
constexpr std::uint32_t k_unknown_version = 0xFFFFFFFFU;
constexpr std::size_t k_line_limit = 127;
constexpr std::uint32_t k_max_type_name = 1024;
constexpr std::uint32_t k_max_embed = 1U << 20;

bool is_bethesda(std::uint32_t file, std::uint32_t user) noexcept {
  const bool oblivion = ((file == k_v10_1_0_106 || file == k_v10_2_0_0) && user >= 3 && user < 11) ||
                        (file == k_v20_0_0_4 && (user == 10 || user == 11)) || (file == k_v20_0_0_5 && user == 11);
  return (file == k_v20_2_0_7 && user >= 11) || oblivion;
}

bool digit(char c) noexcept { return c >= '0' && c <= '9'; }

// Invariant: tokenises like the regex 25[0-5]|2[0-4][0-9]|1[0-9][0-9]|[1-9]?[0-9], so "20.2.0.7" gives 20, 2, 0, 7.
std::uint32_t parse_banner_version(std::string_view text) noexcept {
  std::uint32_t parts[4]{};
  std::size_t at = 0;
  for (std::size_t part = 0; part < 4; ++part) {
    while (at < text.size() && !digit(text[at])) ++at;
    if (at >= text.size()) break;
    const auto rest = text.substr(at);
    std::size_t length = 1;
    if (rest.size() >= 3 && rest[0] == '2' && rest[1] == '5' && rest[2] >= '0' && rest[2] <= '5') {
      length = 3;
    } else if (rest.size() >= 3 && rest[0] == '2' && rest[1] >= '0' && rest[1] <= '4' && digit(rest[2])) {
      length = 3;
    } else if (rest.size() >= 3 && rest[0] == '1' && digit(rest[1]) && digit(rest[2])) {
      length = 3;
    } else if (rest.size() >= 2 && rest[0] >= '1' && rest[0] <= '9' && digit(rest[1])) {
      length = 2;
    }
    std::uint32_t value = 0;
    for (std::size_t i = 0; i < length; ++i) value = value * 10U + static_cast<std::uint32_t>(rest[i] - '0');
    parts[part] = value;
    at += length;
  }
  return (parts[0] << 24U) | (parts[1] << 16U) | (parts[2] << 8U) | parts[3];
}

std::string sized_string(binary::Cursor& cursor, int prefix_bytes, std::uint32_t max_length) {
  const std::uint32_t length = prefix_bytes == 1 ? cursor.read_u8() : cursor.read_u32();
  if (length > max_length) throw std::runtime_error("NIF header string is too long");
  const auto bytes = cursor.read_bytes(length);
  return std::string(reinterpret_cast<const char*>(bytes.data()), bytes.size());
}

void cut_at_nul(std::string& text) {
  if (const auto nul = text.find('\0'); nul != std::string::npos) text.resize(nul);
}

void read_into(std::span<const std::byte> bytes, NifHeaderInfo& info) {
  binary::Cursor cursor(bytes);
  std::string line;
  for (;;) {
    const auto b = cursor.read_u8();
    if (b == '\n') break;
    line.push_back(static_cast<char>(b));
    if (line.size() > k_line_limit) return;
  }
  cut_at_nul(line);
  const bool net_immerse = line.find("NetImmerse File Format") != std::string::npos;
  const bool gamebryo = line.find("Gamebryo File Format") != std::string::npos;
  if (line.find("NDSNIF....@....@....") != std::string::npos || (!net_immerse && !gamebryo)) return;

  auto parsed = k_unknown_version;
  if (const auto at = line.find(", Version "); at != std::string::npos) {
    parsed = parse_banner_version(std::string_view(line).substr(at + 10));
  }
  if (parsed <= k_v3_1) return;

  const auto file = cursor.read_u32();
  info.file_version = file;
  if (file >= k_v20_0_0_3 && cursor.read_u8() != 1) return;
  const auto user = file >= k_v10_0_1_8 ? cursor.read_u32() : 0U;
  info.user_version = user;
  const auto block_count = cursor.read_u32();
  info.block_count = block_count;

  if (is_bethesda(file, user)) {
    const auto stream_version = cursor.read_u32();
    sized_string(cursor, 1, 255);
    if (stream_version > 130) cursor.read_u32();
    sized_string(cursor, 1, 255);
    sized_string(cursor, 1, 255);
    if (stream_version == 130) sized_string(cursor, 1, 255);
  } else if (file >= k_v30_0_0_2) {
    const auto embed = cursor.read_u32();
    if (embed > k_max_embed) return;
    cursor.read_bytes(embed);
  }
  if (file < k_v5_0_0_1) return;

  const auto type_count = cursor.read_u16();
  if (type_count == 0 && block_count > 0) return;
  for (std::uint16_t i = 0; i < type_count; ++i) {
    auto name = sized_string(cursor, 4, k_max_type_name);
    cut_at_nul(name);
    info.block_types.push_back(std::move(name));
  }
  info.readable = true;
}

}  // namespace

NifHeaderInfo read_nif_header(std::span<const std::byte> bytes) noexcept {
  NifHeaderInfo info;
  try {
    read_into(bytes, info);
  } catch (const std::exception&) {
    info.readable = false;
  } catch (...) {
    info.readable = false;
  }
  return info;
}

NifSkinScan scan_nif_skin(std::span<const std::byte> bytes) noexcept {
  const auto info = read_nif_header(bytes);
  for (const auto& name : info.block_types) {
    if (name == "BSSkin::Instance" || name == "NiSkinInstance" || name == "BSDismemberSkinInstance") {
      return NifSkinScan::skin;
    }
  }
  return info.readable ? NifSkinScan::no_skin : NifSkinScan::unknown;
}

}  // namespace fo4::nif
