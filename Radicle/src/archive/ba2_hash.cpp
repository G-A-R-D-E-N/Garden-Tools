#include "fo4/archive/ba2_hash.hpp"

#include <algorithm>
#include <array>
#include <cctype>

namespace fo4::archive {
namespace {

std::array<std::uint32_t, 256> build_table() {
  std::array<std::uint32_t, 256> table{};
  for (std::uint32_t i = 0; i < 256; ++i) {
    auto c = i;
    for (int k = 0; k < 8; ++k) {
      c = (c & 1u) != 0 ? 0xEDB88320u ^ (c >> 1u) : c >> 1u;
    }
    table[i] = c;
  }
  return table;
}

}  // namespace

std::uint32_t ba2_crc32(std::span<const std::byte> data) {
  static const auto table = build_table();
  std::uint32_t crc = 0;
  for (const auto byte : data) {
    const auto value = std::to_integer<std::uint8_t>(byte);
    crc = (crc >> 8u) ^ table[(crc ^ value) & 0xFFu];
  }
  return crc;
}

std::string normalize_ba2_name(std::string_view path) {
  std::string out;
  out.reserve(path.size());
  for (const unsigned char ch : path) {
    if (ch == '/' || ch == '\\') {
      out.push_back('\\');
    } else {
      out.push_back(static_cast<char>(std::tolower(ch)));
    }
  }
  while (!out.empty() && out.back() == '\\') {
    out.pop_back();
  }
  std::size_t first = 0;
  while (first < out.size() && out[first] == '\\') {
    ++first;
  }
  out.erase(0, first);
  return out;
}

std::string normalize_ba2_path(std::string_view path) {
  if (path.empty() || path.size() >= k_max_ba2_path_length) {
    return ".";
  }
  return normalize_ba2_name(path);
}

Ba2Hashes compute_ba2_hashes(std::string_view path) {
  const auto normalized = normalize_ba2_path(path);
  if (normalized == ".") {
    return {};
  }

  const auto sep = normalized.rfind('\\');
  const auto parent = sep == std::string::npos ? "" : normalized.substr(0, sep);
  const auto begin = sep == std::string::npos ? std::size_t{0} : sep + 1;
  const auto dot = normalized.rfind('.');
  // A dot inside the directory part does not start an extension; treat it as
  // absent rather than computing a negative-length stem.
  const auto end = (dot != std::string::npos && dot > begin) ? dot : std::string::npos;
  const auto stem = end == std::string::npos ? normalized.substr(begin)
                                             : normalized.substr(begin, end - begin);
  const auto ext = end == std::string::npos ? "" : normalized.substr(end + 1);

  std::uint32_t extension_word = 0;
  for (std::size_t i = 0; i < std::min<std::size_t>(4, ext.size()); ++i) {
    extension_word |= static_cast<std::uint8_t>(ext[i]) << (8u * i);
  }

  const auto bytes = [](std::string_view text) {
    return std::span<const std::byte>(
        reinterpret_cast<const std::byte*>(text.data()), text.size());
  };

  return Ba2Hashes{
      ba2_crc32(bytes(stem)),
      extension_word,
      ba2_crc32(bytes(parent)),
  };
}

}  // namespace fo4::archive
