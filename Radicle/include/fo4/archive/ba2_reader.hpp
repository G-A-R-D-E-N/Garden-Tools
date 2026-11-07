#pragma once

#include "fo4/archive/ba2.hpp"
#include "fo4/archive/ba2_texture.hpp"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace fo4::archive {

class FileCursor;

class Ba2ByteSource {
public:
  virtual ~Ba2ByteSource() = default;
  virtual std::uint64_t size() const noexcept = 0;
  // Reads must fill the range or throw, always from the same held source.
  virtual void read(std::uint64_t offset, std::span<std::byte> out) const = 0;
};

std::string hash_byte_source(const std::shared_ptr<const Ba2ByteSource>& source);

enum class Ba2Kind : std::uint8_t {
  general,
  texture,
};

struct Ba2ChunkLocation {
  std::uint64_t offset{0};
  std::uint32_t packed_size{0};
  std::uint32_t unpacked_size{0};
  std::uint16_t mip_first{0};
  std::uint16_t mip_last{0};
};

struct Ba2EntryInfo {
  std::string path;
  std::uint32_t name_hash{0};
  std::uint32_t extension_hash{0};
  std::uint32_t directory_hash{0};
  std::uint8_t unknown{0};
  Ba2TextureInfo texture;
  std::vector<Ba2ChunkLocation> chunks;

  std::uint64_t unpacked_size() const noexcept;
};

struct Ba2ExtractedPayload {
  std::vector<std::byte> bytes;
  std::uint64_t size{0};
  std::string sha256;
};

struct Ba2IndexLimits {
  std::size_t max_entries{1000000};
  std::uint64_t max_bytes{256ull * 1024 * 1024};
};

class Ba2Snapshot {
public:
  static Ba2Snapshot open(std::shared_ptr<const Ba2ByteSource> source);
  static Ba2Snapshot open(std::shared_ptr<const Ba2ByteSource> source, Ba2IndexLimits limits);

  const std::vector<Ba2EntryInfo>& entries() const noexcept;
  Ba2Kind kind() const noexcept;
  std::uint32_t version() const noexcept;
  bool has_name_table() const noexcept;
  const std::string& archive_sha256() const noexcept;
  std::uint64_t index_bytes() const noexcept;
  std::optional<std::size_t> find(std::string_view path) const;
  Ba2ExtractedPayload extract(std::size_t index, std::uint64_t max_bytes) const;
  std::vector<Ba2ExtractedPayload> extract_many(std::span<const std::size_t> indices,
                                               std::uint64_t max_total_bytes) const;

private:
  Ba2Snapshot() = default;
  struct Impl;
  std::shared_ptr<Impl> impl_;
};

// Immutable after open; extraction opens its own stream, so const members are thread-safe.
class Ba2Reader {
public:
  static Ba2Reader open(const std::filesystem::path& path);

  const std::filesystem::path& path() const noexcept { return path_; }
  Ba2Kind kind() const noexcept { return kind_; }
  std::uint32_t version() const noexcept { return version_; }
  bool has_name_table() const noexcept { return has_name_table_; }
  const std::vector<Ba2EntryInfo>& entries() const noexcept { return entries_; }

  std::optional<std::size_t> find(std::string_view path) const;
  std::vector<std::byte> extract(std::size_t index) const;

private:
  friend class Ba2Snapshot;
  static Ba2Reader open_source(const std::shared_ptr<const Ba2ByteSource>& source,
                               const std::filesystem::path& label, Ba2IndexLimits limits,
                               std::uint64_t* index_bytes);
  std::vector<std::byte> extract_with(FileCursor& file, std::size_t index,
                                      std::uint64_t max_input_bytes) const;

  std::filesystem::path path_;
  Ba2Kind kind_{Ba2Kind::general};
  std::uint32_t version_{0};
  bool has_name_table_{false};
  std::vector<Ba2EntryInfo> entries_;
  std::unordered_map<std::string, std::size_t> by_path_;
};

}  // namespace fo4::archive
