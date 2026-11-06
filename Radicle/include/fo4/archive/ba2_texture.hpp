#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace fo4::archive {

struct Ba2TextureInfo {
  std::uint16_t height{0};
  std::uint16_t width{0};
  std::uint8_t mip_count{0};
  std::uint8_t dxgi_format{0};
  std::uint8_t flags{0};
  std::uint8_t tile_mode{0};
};

struct Ba2TextureChunk {
  std::vector<std::byte> data;
  std::uint32_t decompressed_size{0};
  bool compressed{false};
  std::uint16_t mip_first{0};
  std::uint16_t mip_last{0};
};

struct Ba2TextureEntry {
  std::uint32_t name_hash{0};
  std::uint32_t extension_hash{0};
  std::uint32_t directory_hash{0};
  std::uint8_t unknown{0};
  std::string path;
  Ba2TextureInfo texture;
  std::vector<Ba2TextureChunk> chunks;
};

struct Ba2TextureArchive {
  std::uint32_t version{1};
  bool has_string_table{true};
  std::vector<Ba2TextureEntry> entries;
};

// Supported versions: 1, 7 (seen on NG/AE installs), 8 (seen on AE 1.11.240.0).
bool is_supported_ba2_texture_version(std::uint32_t version) noexcept;
std::size_t ba2_texture_max_dds_header_size(const Ba2TextureInfo& info,
                                            std::string_view path) noexcept;

Ba2TextureArchive read_ba2_texture(std::span<const std::byte> data);
Ba2TextureArchive read_ba2_texture_file(const std::filesystem::path& path);

std::vector<std::byte> write_ba2_texture(const Ba2TextureArchive& archive);
void write_ba2_texture_file(const Ba2TextureArchive& archive,
                            const std::filesystem::path& path);

std::vector<std::byte> extract_ba2_texture_entry(const Ba2TextureEntry& entry);
const Ba2TextureEntry* find_ba2_texture_entry(const Ba2TextureArchive& archive,
                                              std::string_view path);

Ba2TextureChunk make_stored_ba2_texture_chunk(std::span<const std::byte> data);
Ba2TextureChunk make_compressed_ba2_texture_chunk(std::span<const std::byte> data);

}  // namespace fo4::archive
