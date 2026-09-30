#include "fo4/archive/ba2_reader.hpp"

#include "fo4/archive/ba2_hash.hpp"

#include <array>
#include <cstring>
#include <fstream>
#include <limits>
#include <string>
#include <utility>

namespace fo4::archive {
namespace {

constexpr std::uint32_t k_magic = 0x58445442u;  // BTDX
constexpr std::uint32_t k_gnrl = 0x4C524E47u;   // GNRL
constexpr std::uint32_t k_dx10 = 0x30315844u;   // DX10
constexpr std::uint32_t k_chunk_sentinel = 0xBAADF00Du;
constexpr std::uint16_t k_general_chunk_header_size = 0x10u;
constexpr std::uint16_t k_texture_chunk_header_size = 0x18u;
constexpr std::uint64_t k_header_size = 24;

std::string utf8(const std::filesystem::path& path) {
  const auto text = path.u8string();
  return std::string(text.begin(), text.end());
}

class FileCursor {
public:
  explicit FileCursor(const std::filesystem::path& path) : path_(path), stream_(path, std::ios::binary) {
    if (!stream_) throw Ba2Error("could not open BA2: " + utf8(path));
    std::error_code ec;
    size_ = std::filesystem::file_size(path, ec);
    if (ec) throw Ba2Error("could not determine BA2 size: " + utf8(path));
  }

  std::uint64_t size() const noexcept { return size_; }
  std::uint64_t position() const noexcept { return position_; }

  void seek(std::uint64_t offset) {
    if (offset > size_) throw Ba2Error("BA2 offset points outside the archive: " + utf8(path_));
    stream_.seekg(static_cast<std::streamoff>(offset), std::ios::beg);
    if (!stream_) throw Ba2Error("could not seek in BA2: " + utf8(path_));
    position_ = offset;
  }

  void read(void* out, std::size_t length) {
    if (length > size_ - position_) throw Ba2Error("BA2 is truncated: " + utf8(path_));
    stream_.read(static_cast<char*>(out), static_cast<std::streamsize>(length));
    if (!stream_) throw Ba2Error("could not read BA2: " + utf8(path_));
    position_ += length;
  }

  std::uint8_t u8() { return bytes<1>()[0]; }
  std::uint16_t u16() {
    const auto b = bytes<2>();
    return static_cast<std::uint16_t>(b[0] | (b[1] << 8u));
  }
  std::uint32_t u32() {
    const auto b = bytes<4>();
    return static_cast<std::uint32_t>(b[0]) | (static_cast<std::uint32_t>(b[1]) << 8u) |
           (static_cast<std::uint32_t>(b[2]) << 16u) | (static_cast<std::uint32_t>(b[3]) << 24u);
  }
  std::uint64_t u64() {
    const std::uint64_t low = u32();
    const std::uint64_t high = u32();
    return low | (high << 32u);
  }

private:
  template <std::size_t N>
  std::array<std::uint8_t, N> bytes() {
    std::array<std::uint8_t, N> out{};
    read(out.data(), N);
    return out;
  }

  std::filesystem::path path_;
  std::ifstream stream_;
  std::uint64_t size_{0};
  std::uint64_t position_{0};
};

std::string hash_key(std::uint32_t name, std::uint32_t extension, std::uint32_t directory) {
  std::string key(12, '\0');
  std::memcpy(key.data(), &name, 4);
  std::memcpy(key.data() + 4, &extension, 4);
  std::memcpy(key.data() + 8, &directory, 4);
  return key;
}

std::string lookup_key(bool named, std::string_view path) {
  if (named) return normalize_ba2_path(path);
  const auto hashes = compute_ba2_hashes(path);
  return hash_key(hashes.name, hashes.extension, hashes.directory);
}

std::vector<std::byte> read_chunk(FileCursor& file, const Ba2ChunkLocation& chunk) {
  const std::uint32_t length = chunk.packed_size != 0 ? chunk.packed_size : chunk.unpacked_size;
  std::vector<std::byte> out(length);
  file.seek(chunk.offset);
  if (length != 0) file.read(out.data(), length);
  return out;
}

}  // namespace

std::uint64_t Ba2EntryInfo::unpacked_size() const noexcept {
  std::uint64_t total = 0;
  for (const auto& chunk : chunks) total += chunk.unpacked_size;
  return total;
}

Ba2Reader Ba2Reader::open(const std::filesystem::path& path) {
  FileCursor file(path);
  Ba2Reader reader;
  reader.path_ = path;
  if (file.size() < k_header_size) throw Ba2Error("archive truncated before BA2 header: " + utf8(path));
  if (file.u32() != k_magic) throw Ba2Error("not a BA2 archive: missing BTDX magic: " + utf8(path));
  reader.version_ = file.u32();
  const auto tag = file.u32();
  if (tag == k_gnrl) {
    reader.kind_ = Ba2Kind::general;
    if (!is_supported_ba2_general_version(reader.version_)) {
      throw Ba2Error("unsupported GNRL BA2 version " + std::to_string(reader.version_));
    }
  } else if (tag == k_dx10) {
    reader.kind_ = Ba2Kind::texture;
    if (!is_supported_ba2_texture_version(reader.version_)) {
      throw Ba2Error("unsupported DX10 BA2 version " + std::to_string(reader.version_));
    }
  } else {
    throw Ba2Error("unsupported BA2 content type; expected GNRL or DX10: " + utf8(path));
  }
  const bool texture = reader.kind_ == Ba2Kind::texture;
  const auto file_count = file.u32();
  const auto name_table_offset = file.u64();
  reader.has_name_table_ = name_table_offset != 0;

  const std::uint64_t entry_size = texture ? 24 : 16;
  const std::uint64_t chunk_size = texture ? 24 : 20;
  if (file_count > (file.size() - file.position()) / entry_size) {
    throw Ba2Error("BA2 file count exceeds the entry table that fits in the archive");
  }
  reader.entries_.reserve(file_count);
  for (std::uint32_t index = 0; index < file_count; ++index) {
    Ba2EntryInfo entry;
    entry.name_hash = file.u32();
    entry.extension_hash = file.u32();
    entry.directory_hash = file.u32();
    entry.unknown = file.u8();
    const auto chunk_count = file.u8();
    const auto chunk_header_size = file.u16();
    const auto expected = texture ? k_texture_chunk_header_size : k_general_chunk_header_size;
    if (chunk_header_size != expected) {
      throw Ba2Error("BA2 entry " + std::to_string(index) + " has an unsupported chunk header size");
    }
    if (texture) {
      entry.texture.height = file.u16();
      entry.texture.width = file.u16();
      entry.texture.mip_count = file.u8();
      entry.texture.dxgi_format = file.u8();
      entry.texture.flags = file.u8();
      entry.texture.tile_mode = file.u8();
    }
    if (chunk_count > (file.size() - file.position()) / chunk_size) {
      throw Ba2Error("BA2 entry " + std::to_string(index) + " chunk count exceeds the remaining table");
    }
    entry.chunks.reserve(chunk_count);
    for (std::uint8_t c = 0; c < chunk_count; ++c) {
      Ba2ChunkLocation chunk;
      chunk.offset = file.u64();
      chunk.packed_size = file.u32();
      chunk.unpacked_size = file.u32();
      if (texture) {
        chunk.mip_first = file.u16();
        chunk.mip_last = file.u16();
      }
      if (file.u32() != k_chunk_sentinel) {
        throw Ba2Error("BA2 entry " + std::to_string(index) + " has an invalid BAADF00D sentinel");
      }
      const std::uint64_t length = chunk.packed_size != 0 ? chunk.packed_size : chunk.unpacked_size;
      if (chunk.offset > file.size() || length > file.size() - chunk.offset) {
        throw Ba2Error("BA2 entry " + std::to_string(index) + " chunk points outside the archive");
      }
      entry.chunks.push_back(chunk);
    }
    reader.entries_.push_back(std::move(entry));
  }

  if (reader.has_name_table_) {
    file.seek(name_table_offset);
    for (auto& entry : reader.entries_) {
      const auto length = file.u16();
      entry.path.resize(length);
      if (length != 0) file.read(entry.path.data(), length);
    }
  }

  reader.by_path_.reserve(reader.entries_.size());
  for (std::size_t index = 0; index < reader.entries_.size(); ++index) {
    const auto& entry = reader.entries_[index];
    auto key = reader.has_name_table_ ? normalize_ba2_path(entry.path)
                                      : hash_key(entry.name_hash, entry.extension_hash, entry.directory_hash);
    reader.by_path_.emplace(std::move(key), index);
  }
  return reader;
}

std::optional<std::size_t> Ba2Reader::find(std::string_view path) const {
  const auto it = by_path_.find(lookup_key(has_name_table_, path));
  if (it == by_path_.end()) return std::nullopt;
  return it->second;
}

std::vector<std::byte> Ba2Reader::extract(std::size_t index) const {
  const auto& info = entries_.at(index);
  FileCursor file(path_);
  if (kind_ == Ba2Kind::general) {
    Ba2GeneralEntry entry;
    entry.path = info.path;
    for (const auto& chunk : info.chunks) {
      entry.chunks.push_back({read_chunk(file, chunk), chunk.unpacked_size, chunk.packed_size != 0});
    }
    return extract_ba2_general_entry(entry);
  }
  Ba2TextureEntry entry;
  entry.path = info.path.empty() ? std::string("unnamed.dds") : info.path;
  entry.texture = info.texture;
  for (const auto& chunk : info.chunks) {
    entry.chunks.push_back({read_chunk(file, chunk), chunk.unpacked_size, chunk.packed_size != 0,
                            chunk.mip_first, chunk.mip_last});
  }
  return extract_ba2_texture_entry(entry);
}

}  // namespace fo4::archive
