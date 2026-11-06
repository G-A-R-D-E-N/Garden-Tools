#include "fo4/archive/ba2.hpp"

#include "ba2_file.hpp"

#include "fo4/binary/cursor.hpp"
#include "fo4/io/atomic_write.hpp"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <limits>
#include <string>
#include <utility>

#include <zlib.h>

namespace fo4::archive {
namespace {

constexpr std::uint32_t k_magic = 0x58445442u;  // BTDX
constexpr std::uint32_t k_gnrl = 0x4C524E47u;   // GNRL
constexpr std::uint32_t k_chunk_sentinel = 0xBAADF00Du;
constexpr std::uint16_t k_general_chunk_header_size = 0x10u;
constexpr std::size_t k_header_size = 24;
constexpr std::size_t k_entry_size = 16;
constexpr std::size_t k_chunk_record_size = 20;

std::uint64_t read_u64(binary::Cursor& cursor) {
  const auto low = static_cast<std::uint64_t>(cursor.read_u32());
  const auto high = static_cast<std::uint64_t>(cursor.read_u32());
  return low | (high << 32u);
}

void append_u8(std::vector<std::byte>& out, std::uint8_t value) {
  out.push_back(static_cast<std::byte>(value));
}

void append_u16(std::vector<std::byte>& out, std::uint16_t value) {
  append_u8(out, static_cast<std::uint8_t>(value & 0xFFu));
  append_u8(out, static_cast<std::uint8_t>((value >> 8u) & 0xFFu));
}

void append_u32(std::vector<std::byte>& out, std::uint32_t value) {
  append_u16(out, static_cast<std::uint16_t>(value & 0xFFFFu));
  append_u16(out, static_cast<std::uint16_t>((value >> 16u) & 0xFFFFu));
}

void append_u64(std::vector<std::byte>& out, std::uint64_t value) {
  append_u32(out, static_cast<std::uint32_t>(value & 0xFFFFFFFFull));
  append_u32(out, static_cast<std::uint32_t>(value >> 32u));
}

void append_bytes(std::vector<std::byte>& out, std::span<const std::byte> data) {
  out.insert(out.end(), data.begin(), data.end());
}

std::string normalized_path(std::string_view path) {
  std::string out;
  out.reserve(path.size());
  for (const unsigned char ch : path) {
    if (ch == '/' || ch == '\\') {
      out.push_back('\\');
    } else {
      out.push_back(static_cast<char>(std::tolower(ch)));
    }
  }
  return out;
}

std::size_t checked_size(std::uint64_t value, std::string_view what) {
  if (value > static_cast<std::uint64_t>(std::numeric_limits<std::size_t>::max())) {
    throw Ba2Error(std::string(what) + " is too large for this process");
  }
  return static_cast<std::size_t>(value);
}

using detail::read_file_bytes;

Ba2GeneralArchive read_ba2_general_impl(std::span<const std::byte> data) {
  if (data.size() < k_header_size) {
    throw Ba2Error("archive truncated before BA2 header");
  }

  binary::Cursor cursor(data);
  if (cursor.read_u32() != k_magic) {
    throw Ba2Error("not a BA2 archive: missing BTDX magic");
  }

  Ba2GeneralArchive archive;
  archive.version = cursor.read_u32();
  if (!is_supported_ba2_general_version(archive.version)) {
    throw Ba2Error("unsupported BA2 version " + std::to_string(archive.version) +
                   "; Radicle supports GNRL versions 1, 7 and 8");
  }

  const auto tag = cursor.read_u32();
  if (tag != k_gnrl) {
    throw Ba2Error("unsupported BA2 content type; Radicle currently supports GNRL only");
  }

  const auto file_count = cursor.read_u32();
  const auto string_table_offset_raw = read_u64(cursor);
  archive.has_string_table = string_table_offset_raw != 0;

  // Every GNRL file entry consumes at least its fixed 16-byte header before
  // any chunk records. Prove that minimum table can physically fit before
  // reserving attacker-controlled file_count entries.
  if (static_cast<std::size_t>(file_count) > cursor.remaining() / k_entry_size) {
    throw Ba2Error("BA2 file count exceeds the minimum entry table that fits in the archive");
  }

  struct ChunkLocation {
    std::size_t entry_index;
    std::size_t chunk_index;
    std::uint64_t offset;
    std::uint32_t packed_size;
    std::uint32_t unpacked_size;
  };

  archive.entries.reserve(file_count);
  std::vector<ChunkLocation> chunk_locations;

  for (std::uint32_t file_index = 0; file_index < file_count; ++file_index) {
    Ba2GeneralEntry entry;
    entry.name_hash = cursor.read_u32();
    entry.extension_hash = cursor.read_u32();
    entry.directory_hash = cursor.read_u32();
    entry.unknown = cursor.read_u8();
    const auto chunk_count = cursor.read_u8();
    const auto chunk_header_size = cursor.read_u16();
    if (chunk_header_size != k_general_chunk_header_size) {
      throw Ba2Error("GNRL entry " + std::to_string(file_index) +
                     " has unsupported chunk header size 0x" +
                     [] (std::uint16_t value) {
                       constexpr char digits[] = "0123456789ABCDEF";
                       std::string text(4, '0');
                       for (int i = 3; i >= 0; --i) {
                         text[static_cast<std::size_t>(i)] = digits[value & 0xFu];
                         value = static_cast<std::uint16_t>(value >> 4u);
                       }
                       return text;
                     }(chunk_header_size));
    }

    if (static_cast<std::size_t>(chunk_count) > cursor.remaining() / k_chunk_record_size) {
      throw Ba2Error("GNRL entry " + std::to_string(file_index) +
                     " chunk count exceeds the remaining chunk table bytes");
    }

    entry.chunks.reserve(chunk_count);
    for (std::uint8_t chunk_index = 0; chunk_index < chunk_count; ++chunk_index) {
      const auto offset = read_u64(cursor);
      const auto packed_size = cursor.read_u32();
      const auto unpacked_size = cursor.read_u32();
      const auto sentinel = cursor.read_u32();
      if (sentinel != k_chunk_sentinel) {
        throw Ba2Error("GNRL entry " + std::to_string(file_index) + " chunk " +
                       std::to_string(chunk_index) + " has invalid BAADF00D sentinel");
      }

      entry.chunks.push_back(Ba2GeneralChunk{
          {}, unpacked_size, packed_size != 0});
      chunk_locations.push_back(ChunkLocation{
          archive.entries.size(), entry.chunks.size() - 1, offset, packed_size, unpacked_size});
    }

    archive.entries.push_back(std::move(entry));
  }

  for (const auto& location : chunk_locations) {
    const auto offset = checked_size(location.offset, "BA2 chunk offset");
    const auto length = static_cast<std::size_t>(
        location.packed_size != 0 ? location.packed_size : location.unpacked_size);
    if (offset > data.size() || length > data.size() - offset) {
      throw Ba2Error("BA2 chunk points outside archive data");
    }
    auto& chunk = archive.entries[location.entry_index].chunks[location.chunk_index];
    const auto bytes = data.subspan(offset, length);
    chunk.data.assign(bytes.begin(), bytes.end());
  }

  if (archive.has_string_table) {
    const auto string_table_offset = checked_size(string_table_offset_raw, "BA2 string table offset");
    if (string_table_offset > data.size()) {
      throw Ba2Error("BA2 string table points outside archive data");
    }
    binary::Cursor strings(data.subspan(string_table_offset), string_table_offset);
    for (auto& entry : archive.entries) {
      const auto length = strings.read_u16();
      const auto bytes = strings.read_bytes(length);
      entry.path = text::windows1252_to_utf8(std::string_view(reinterpret_cast<const char*>(bytes.data()), bytes.size()));
    }
  }

  return archive;
}

}  // namespace

bool is_supported_ba2_general_version(std::uint32_t version) noexcept {
  return version == 1u || version == 7u || version == 8u;
}

Ba2GeneralArchive read_ba2_general(std::span<const std::byte> data) {
  try {
    return read_ba2_general_impl(data);
  } catch (const parse_error& error) {
    throw Ba2Error(
        "BA2 parse error at byte " + std::to_string(error.offset()) + ": " +
        error.parse_message());
  }
}

Ba2GeneralArchive read_ba2_general_file(const std::filesystem::path& path) {
  const auto bytes = read_file_bytes(path);
  return read_ba2_general(bytes);
}

std::vector<std::byte> extract_ba2_general_entry(const Ba2GeneralEntry& entry) {
  std::size_t total = 0;
  for (const auto& chunk : entry.chunks) {
    if (chunk.compressed) {
      detail::require_zlib_expansion_bound(chunk.data.size(), chunk.decompressed_size);
    }
    if (chunk.decompressed_size > std::numeric_limits<std::size_t>::max() - total) {
      throw Ba2Error("BA2 entry is too large to extract");
    }
    total += static_cast<std::size_t>(chunk.decompressed_size);
  }
  // decompressed_size is attacker-controlled per chunk and the chunk count is
  // unbounded, so cap the total before allocating anything. A few MB BA2 can
  // otherwise demand many GB of RAM.
  if (total > k_max_extract_total_bytes) {
    throw Ba2Error("BA2 entry exceeds the " +
                   std::to_string(k_max_extract_total_bytes) +
                   "-byte extraction limit");
  }

  std::vector<std::byte> output;
  output.reserve(total);
  for (const auto& chunk : entry.chunks) {
    if (!chunk.compressed) {
      if (chunk.data.size() != chunk.decompressed_size) {
        throw Ba2Error("stored BA2 chunk size does not match header");
      }
      append_bytes(output, chunk.data);
      continue;
    }

    std::vector<std::byte> unpacked(chunk.decompressed_size);
    uLongf out_size = static_cast<uLongf>(unpacked.size());
    const auto result = uncompress(
        reinterpret_cast<Bytef*>(unpacked.data()), &out_size,
        reinterpret_cast<const Bytef*>(chunk.data.data()),
        static_cast<uLong>(chunk.data.size()));
    if (result != Z_OK || out_size != unpacked.size()) {
      throw Ba2Error("zlib failed to decompress BA2 chunk");
    }
    append_bytes(output, unpacked);
  }
  return output;
}

const Ba2GeneralEntry* find_ba2_general_entry(const Ba2GeneralArchive& archive,
                                               std::string_view path) {
  // Not noexcept: normalized_path allocates, and an allocation failure must
  // propagate as bad_alloc instead of terminating via an escaping exception.
  const auto wanted = normalized_path(path);
  for (const auto& entry : archive.entries) {
    if (normalized_path(entry.path) == wanted) {
      return &entry;
    }
  }
  return nullptr;
}

Ba2GeneralChunk make_stored_ba2_chunk(std::span<const std::byte> data) {
  if (data.size() > std::numeric_limits<std::uint32_t>::max()) {
    throw Ba2Error("BA2 chunk exceeds 32-bit size field");
  }
  return Ba2GeneralChunk{
      std::vector<std::byte>(data.begin(), data.end()),
      static_cast<std::uint32_t>(data.size()), false};
}

Ba2GeneralChunk make_compressed_ba2_chunk(std::span<const std::byte> data) {
  if (data.size() > std::numeric_limits<std::uint32_t>::max() ||
      data.size() > std::numeric_limits<uLong>::max()) {
    throw Ba2Error("BA2 chunk exceeds zlib or format size limit");
  }
  if (data.empty()) {
    return make_stored_ba2_chunk(data);
  }

  uLongf packed_size = compressBound(static_cast<uLong>(data.size()));
  std::vector<std::byte> packed(static_cast<std::size_t>(packed_size));
  const auto result = compress2(
      reinterpret_cast<Bytef*>(packed.data()), &packed_size,
      reinterpret_cast<const Bytef*>(data.data()), static_cast<uLong>(data.size()), Z_BEST_COMPRESSION);
  if (result != Z_OK) {
    throw Ba2Error("zlib failed to compress BA2 chunk");
  }
  packed.resize(static_cast<std::size_t>(packed_size));
  if (packed.size() >= data.size()) {
    return make_stored_ba2_chunk(data);
  }

  return Ba2GeneralChunk{
      std::move(packed), static_cast<std::uint32_t>(data.size()), true};
}

std::vector<std::byte> write_ba2_general(const Ba2GeneralArchive& archive) {
  if (!is_supported_ba2_general_version(archive.version)) {
    throw Ba2Error("Radicle writes proven GNRL versions 1, 7 and 8 only");
  }
  if (archive.entries.size() > std::numeric_limits<std::uint32_t>::max()) {
    throw Ba2Error("BA2 has too many entries for the 32-bit file count");
  }

  const auto stored = detail::stored_ba2_paths(archive.entries, archive.has_string_table);
  std::uint64_t table_size = 0;
  std::uint64_t data_size = 0;
  std::size_t total_chunks = 0;
  for (std::size_t index = 0; index < archive.entries.size(); ++index) {
    const auto& entry = archive.entries[index];
    if (entry.chunks.size() > std::numeric_limits<std::uint8_t>::max()) {
      throw Ba2Error("BA2 GNRL entry has more than 255 chunks: " + entry.path);
    }
    if (archive.has_string_table && stored[index].size() > std::numeric_limits<std::uint16_t>::max()) {
      throw Ba2Error("BA2 entry path exceeds u16 string-table length: " + entry.path);
    }
    table_size += k_entry_size +
                  static_cast<std::uint64_t>(entry.chunks.size()) * k_chunk_record_size;
    total_chunks += entry.chunks.size();
    for (const auto& chunk : entry.chunks) {
      if (!chunk.compressed && chunk.data.size() != chunk.decompressed_size) {
        throw Ba2Error("stored BA2 chunk size does not match decompressed_size");
      }
      if (chunk.data.size() > std::numeric_limits<std::uint32_t>::max()) {
        throw Ba2Error("BA2 chunk exceeds 32-bit compressed size field");
      }
      data_size += chunk.data.size();
    }
  }

  const auto data_start = static_cast<std::uint64_t>(k_header_size) + table_size;
  const auto string_table_offset = archive.has_string_table ? data_start + data_size : 0ull;

  std::vector<std::uint64_t> chunk_offsets;
  chunk_offsets.reserve(total_chunks);
  auto next_offset = data_start;
  for (const auto& entry : archive.entries) {
    for (const auto& chunk : entry.chunks) {
      chunk_offsets.push_back(next_offset);
      next_offset += chunk.data.size();
    }
  }

  std::uint64_t string_table_size = 0;
  if (archive.has_string_table) {
    for (const auto& path : stored) string_table_size += 2u + path.size();
  }

  const auto total_size64 = data_start + data_size + string_table_size;
  const auto total_size = checked_size(total_size64, "BA2 output size");
  std::vector<std::byte> out;
  out.reserve(total_size);

  append_u32(out, k_magic);
  append_u32(out, archive.version);
  append_u32(out, k_gnrl);
  append_u32(out, static_cast<std::uint32_t>(archive.entries.size()));
  append_u64(out, string_table_offset);

  std::size_t chunk_index = 0;
  for (const auto& entry : archive.entries) {
    append_u32(out, entry.name_hash);
    append_u32(out, entry.extension_hash);
    append_u32(out, entry.directory_hash);
    append_u8(out, entry.unknown);
    append_u8(out, static_cast<std::uint8_t>(entry.chunks.size()));
    append_u16(out, k_general_chunk_header_size);

    for (const auto& chunk : entry.chunks) {
      append_u64(out, chunk_offsets[chunk_index++]);
      append_u32(out, chunk.compressed ? static_cast<std::uint32_t>(chunk.data.size()) : 0u);
      append_u32(out, chunk.decompressed_size);
      append_u32(out, k_chunk_sentinel);
    }
  }

  for (const auto& entry : archive.entries) {
    for (const auto& chunk : entry.chunks) {
      append_bytes(out, chunk.data);
    }
  }

  if (archive.has_string_table) {
    for (const auto& path : stored) {
      append_u16(out, static_cast<std::uint16_t>(path.size()));
      const auto* begin = reinterpret_cast<const std::byte*>(path.data());
      append_bytes(out, std::span<const std::byte>(begin, path.size()));
    }
  }

  if (out.size() != total_size) {
    throw Ba2Error("internal BA2 size calculation mismatch");
  }
  return out;
}

void write_ba2_general_file(const Ba2GeneralArchive& archive,
                            const std::filesystem::path& path) {
  const auto bytes = write_ba2_general(archive);
  io::write_file_atomic(path, bytes);
}

}  // namespace fo4::archive
