#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace fo4::archive {

inline constexpr std::size_t k_max_extract_total_bytes = std::size_t{1} << 30;

class Ba2Error : public std::runtime_error {
public:
  using std::runtime_error::runtime_error;
};

struct Ba2GeneralChunk {
  std::vector<std::byte> data;
  std::uint32_t decompressed_size{0};
  bool compressed{false};
};

struct Ba2GeneralEntry {
  std::uint32_t name_hash{0};
  std::uint32_t extension_hash{0};
  std::uint32_t directory_hash{0};
  std::uint8_t unknown{0};
  std::string path;
  std::vector<Ba2GeneralChunk> chunks;
};

struct Ba2GeneralArchive {
  std::uint32_t version{1};
  bool has_string_table{true};
  std::vector<Ba2GeneralEntry> entries;
};

// Supported versions: 1, 7 (seen on NG/AE installs), 8 (seen on AE 1.11.240.0).
bool is_supported_ba2_general_version(std::uint32_t version) noexcept;

Ba2GeneralArchive read_ba2_general(std::span<const std::byte> data);
Ba2GeneralArchive read_ba2_general_file(const std::filesystem::path& path);

std::vector<std::byte> write_ba2_general(const Ba2GeneralArchive& archive);
void write_ba2_general_file(const Ba2GeneralArchive& archive,
                            const std::filesystem::path& path);

std::vector<std::byte> extract_ba2_general_entry(const Ba2GeneralEntry& entry);
// Not noexcept: path normalization allocates, so OOM surfaces as bad_alloc.
const Ba2GeneralEntry* find_ba2_general_entry(const Ba2GeneralArchive& archive,
                                               std::string_view path);

Ba2GeneralChunk make_stored_ba2_chunk(std::span<const std::byte> data);
Ba2GeneralChunk make_compressed_ba2_chunk(std::span<const std::byte> data);

}  // namespace fo4::archive
