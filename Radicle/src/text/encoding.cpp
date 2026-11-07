#include "fo4/text/encoding.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <iterator>

namespace fo4::text {
namespace {

constexpr std::array<char32_t, 32> k_high{
    0x20AC, 0x0081, 0x201A, 0x0192, 0x201E, 0x2026, 0x2020, 0x2021, 0x02C6, 0x2030, 0x0160,
    0x2039, 0x0152, 0x008D, 0x017D, 0x008F, 0x0090, 0x2018, 0x2019, 0x201C, 0x201D, 0x2022,
    0x2013, 0x2014, 0x02DC, 0x2122, 0x0161, 0x203A, 0x0153, 0x009D, 0x017E, 0x0178};

struct UpperRange {
  char32_t first;
  char32_t last;
  std::uint32_t step;
  std::int32_t delta;
};

constexpr UpperRange k_upper[] = {
#include "upper_table.inc"
};

constexpr char32_t k_invalid_base = 0x110000;

void append(std::string& out, char32_t cp) {
  if (cp < 0x80) {
    out.push_back(static_cast<char>(cp));
  } else if (cp < 0x800) {
    out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
    out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
  } else if (cp < 0x10000) {
    out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
    out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
    out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
  } else {
    out.push_back(static_cast<char>(0xF0 | (cp >> 18)));
    out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
    out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
    out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
  }
}

std::optional<char32_t> decode(std::string_view text, std::size_t& at) noexcept {
  const auto lead = static_cast<std::uint8_t>(text[at]);
  if (lead < 0x80) {
    ++at;
    return lead;
  }
  std::size_t length = 0;
  char32_t cp = 0;
  char32_t minimum = 0;
  if (lead >= 0xC2 && lead <= 0xDF) {
    length = 2, cp = lead & 0x1F, minimum = 0x80;
  } else if (lead >= 0xE0 && lead <= 0xEF) {
    length = 3, cp = lead & 0x0F, minimum = 0x800;
  } else if (lead >= 0xF0 && lead <= 0xF4) {
    length = 4, cp = lead & 0x07, minimum = 0x10000;
  } else {
    ++at;
    return std::nullopt;
  }
  if (at + length > text.size()) {
    ++at;
    return std::nullopt;
  }
  for (std::size_t i = 1; i < length; ++i) {
    const auto next = static_cast<std::uint8_t>(text[at + i]);
    if ((next & 0xC0) != 0x80) {
      ++at;
      return std::nullopt;
    }
    cp = (cp << 6) | (next & 0x3F);
  }
  if (cp < minimum || cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF)) {
    ++at;
    return std::nullopt;
  }
  at += length;
  return cp;
}

char32_t fold_code_point(char32_t cp) noexcept {
  if (cp < 0x80) return cp >= 'A' && cp <= 'Z' ? cp + 32 : cp;
  const auto* it = std::upper_bound(std::begin(k_upper), std::end(k_upper), cp,
                                    [](char32_t value, const UpperRange& range) { return value < range.first; });
  if (it == std::begin(k_upper)) return cp;
  it -= 1;
  if (cp > it->last || (cp - it->first) % it->step != 0) return cp;
  const auto upper = static_cast<char32_t>(static_cast<std::int32_t>(cp) + it->delta);
  return upper >= 'A' && upper <= 'Z' ? upper + 32 : upper;
}

char32_t next_folded(std::string_view text, std::size_t& at) noexcept {
  const auto start = at;
  const auto cp = decode(text, at);
  return cp ? fold_code_point(*cp) : k_invalid_base + static_cast<std::uint8_t>(text[start]);
}

}

std::string windows1252_to_utf8(std::string_view bytes) {
  std::string out;
  out.reserve(bytes.size());
  for (const char ch : bytes) {
    const auto byte = static_cast<std::uint8_t>(ch);
    append(out, byte >= 0x80 && byte < 0xA0 ? k_high[byte - 0x80] : char32_t{byte});
  }
  return out;
}

std::optional<std::string> utf8_to_windows1252(std::string_view utf8) {
  std::string out;
  out.reserve(utf8.size());
  for (std::size_t at = 0; at < utf8.size();) {
    const auto cp = decode(utf8, at);
    if (!cp) return std::nullopt;
    if (*cp < 0x80 || (*cp >= 0xA0 && *cp <= 0xFF)) {
      out.push_back(static_cast<char>(*cp));
      continue;
    }
    const auto it = std::find(k_high.begin(), k_high.end(), *cp);
    if (it == k_high.end()) return std::nullopt;
    out.push_back(static_cast<char>(0x80 + (it - k_high.begin())));
  }
  return out;
}

bool is_valid_utf8(std::string_view utf8) noexcept {
  for (std::size_t at = 0; at < utf8.size();) {
    if (!decode(utf8, at)) return false;
  }
  return true;
}

std::string fold(std::string_view utf8) {
  std::string out;
  out.reserve(utf8.size());
  for (std::size_t at = 0; at < utf8.size();) {
    const auto start = at;
    const auto cp = next_folded(utf8, at);
    if (cp >= k_invalid_base) {
      out.push_back(utf8[start]);
    } else {
      append(out, cp);
    }
  }
  return out;
}

int compare_folded(std::string_view a, std::string_view b) noexcept {
  std::size_t i = 0;
  std::size_t j = 0;
  while (i < a.size() && j < b.size()) {
    const auto x = next_folded(a, i);
    const auto y = next_folded(b, j);
    if (x != y) return x < y ? -1 : 1;
  }
  if (i < a.size()) return 1;
  if (j < b.size()) return -1;
  return 0;
}

bool equals_folded(std::string_view a, std::string_view b) noexcept { return compare_folded(a, b) == 0; }

std::size_t hash_folded(std::string_view text) noexcept {
  std::size_t h = 14695981039346656037ull;
  for (std::size_t at = 0; at < text.size();) {
    h ^= next_folded(text, at);
    h *= 1099511628211ull;
  }
  return h;
}

}
