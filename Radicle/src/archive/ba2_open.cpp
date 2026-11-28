#include "fo4/archive/ba2_open.hpp"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <fstream>
#include <stdexcept>
#include <string>

namespace fo4::archive {
namespace {

constexpr std::uint32_t k_magic = 0x58445442u;
constexpr std::uint32_t k_gnrl = 0x4C524E47u;
constexpr std::uint32_t k_dx10 = 0x30315844u;

std::uint32_t read_le32(std::istream& stream) {
  std::uint8_t bytes[4]{};
  stream.read(reinterpret_cast<char*>(bytes), sizeof(bytes));
  if (!stream) throw std::invalid_argument("could not read BA2 header");
  return static_cast<std::uint32_t>(bytes[0]) | (static_cast<std::uint32_t>(bytes[1]) << 8u) |
         (static_cast<std::uint32_t>(bytes[2]) << 16u) | (static_cast<std::uint32_t>(bytes[3]) << 24u);
}

std::string hex8(std::uint32_t value) {
  constexpr char digits[] = "0123456789ABCDEF";
  std::string text(8, '0');
  for (int i = 7; i >= 0; --i) {
    text[static_cast<std::size_t>(i)] = digits[value & 0xFu];
    value >>= 4u;
  }
  return text;
}

template <typename Archive>
Archive require_addressable(Archive archive) {
  if (!archive.entries.empty() && !archive.has_string_table) {
    throw std::invalid_argument("BA2 has no string table, so entries cannot be addressed by path");
  }
  return archive;
}

}

AnyBa2Archive open_ba2(const std::filesystem::path& path) {
  std::string ext = path.extension().string();
  std::ranges::transform(ext, ext.begin(), [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
  if (ext != ".ba2") {
    throw std::invalid_argument("Radicle archive reading currently supports .ba2 files only");
  }

  std::uint32_t tag = 0;
  {
    std::ifstream stream(path, std::ios::binary);
    if (!stream) throw std::invalid_argument("could not open archive: " + path.string());
    if (read_le32(stream) != k_magic) throw std::invalid_argument("not a BA2 archive: " + path.string());
    (void)read_le32(stream);
    tag = read_le32(stream);
  }

  if (tag == k_gnrl) return require_addressable(read_ba2_general_file(path));
  if (tag == k_dx10) return require_addressable(read_ba2_texture_file(path));
  throw std::invalid_argument("unsupported BA2 content format 0x" + hex8(tag) + " (only GNRL and DX10 are FO4)");
}

}
