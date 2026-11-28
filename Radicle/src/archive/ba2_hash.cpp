#include "fo4/archive/ba2_hash.hpp"
#include "fo4/hash.hpp"

#include <algorithm>
#include <array>
#include <cctype>

namespace fo4::archive {
std::uint32_t ba2_crc32(std::span<const std::byte> data) {
  return crc32_raw(data);
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
