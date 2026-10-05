#include "fo4/seq/seq.hpp"

#include "fo4/error.hpp"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace fo4::seq {

SeqFile SeqFile::fromBytes(const std::vector<std::uint8_t>& bytes) {
  if (bytes.size() % sizeof(std::uint32_t) != 0) {
    throw parse_error(bytes.size(), "a .seq file is an array of 4-byte entries; " +
                                        std::to_string(bytes.size()) +
                                        " bytes is not a whole number of entries");
  }

  SeqFile file;
  file.entries.reserve(bytes.size() / sizeof(std::uint32_t));
  for (std::size_t offset = 0; offset < bytes.size(); offset += sizeof(std::uint32_t)) {
    const auto value = static_cast<std::uint32_t>(bytes[offset]) |
                       static_cast<std::uint32_t>(bytes[offset + 1]) << 8 |
                       static_cast<std::uint32_t>(bytes[offset + 2]) << 16 |
                       static_cast<std::uint32_t>(bytes[offset + 3]) << 24;
    file.entries.push_back(RawFormId{value});
  }
  return file;
}

std::vector<std::uint8_t> SeqFile::toBytes() const {
  std::vector<std::uint8_t> bytes;
  bytes.reserve(entries.size() * sizeof(std::uint32_t));
  for (const auto entry : entries) {
    const auto value = entry.value();
    bytes.push_back(static_cast<std::uint8_t>(value & 0xFFu));
    bytes.push_back(static_cast<std::uint8_t>((value >> 8) & 0xFFu));
    bytes.push_back(static_cast<std::uint8_t>((value >> 16) & 0xFFu));
    bytes.push_back(static_cast<std::uint8_t>((value >> 24) & 0xFFu));
  }
  return bytes;
}

}
