#include "fo4/archive/ba2_texture.hpp"

#include "fo4/archive/ba2.hpp"
#include "ba2_file.hpp"
#include "fo4/archive/ba2_hash.hpp"
#include "fo4/binary/cursor.hpp"
#include "fo4/io/atomic_write.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <fstream>
#include <limits>
#include <optional>
#include <string>
#include <utility>

#include <zlib.h>

namespace fo4::archive {
namespace {

constexpr std::uint32_t k_magic = 0x58445442u;   // BTDX
constexpr std::uint32_t k_dx10 = 0x30315844u;    // DX10
constexpr std::uint32_t k_chunk_sentinel = 0xBAADF00Du;
constexpr std::uint16_t k_texture_chunk_header_size = 0x18u;
constexpr std::size_t k_header_size = 24;
constexpr std::size_t k_entry_size = 24;
constexpr std::size_t k_chunk_record_size = 24;
constexpr std::uint32_t k_dds_magic = 0x20534444u;     // "DDS "
constexpr std::uint32_t k_dds_dx10_fourcc = 0x30315844u;
constexpr std::size_t k_dds_header_size = 128;
constexpr std::size_t k_dds_dx10_header_size = 148;

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
  return normalize_ba2_name(path);
}

bool is_dds_path(std::string_view path) {
  if (path.size() < 4) return false;
  const auto suffix = path.substr(path.size() - 4);
  return suffix[0] == '.' &&
         std::tolower(static_cast<unsigned char>(suffix[1])) == 'd' &&
         std::tolower(static_cast<unsigned char>(suffix[2])) == 'd' &&
         std::tolower(static_cast<unsigned char>(suffix[3])) == 's';
}

// indexed by DXGI_FORMAT value, same order as TwbDDS.GetBitsPerPixel
constexpr std::array<std::uint32_t, 116> k_bits_per_pixel = {
    0, 128, 128, 128, 128, 96, 96, 96, 96, 64, 64, 64, 64, 64, 64, 64,
    64, 64, 64, 64, 64, 64, 64, 32, 32, 32, 32, 32, 32, 32, 32, 32,
    32, 32, 32, 32, 32, 32, 32, 32, 32, 32, 32, 32, 32, 32, 32, 32,
    16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 8, 8, 8, 8,
    8, 8, 1, 32, 32, 32, 4, 4, 4, 8, 8, 8, 8, 8, 8, 4,
    4, 4, 8, 8, 8, 16, 16, 32, 32, 32, 32, 32, 32, 32, 8, 8,
    8, 8, 8, 8, 32, 32, 64, 12, 24, 24, 12, 32, 64, 64, 12, 8,
    8, 8, 16, 16,
};

std::pair<std::uint32_t, std::uint32_t> dds_pitch_and_flag(const Ba2TextureInfo& info) {
  constexpr std::uint32_t linear = 0x80000u;
  constexpr std::uint32_t pitch = 0x8u;
  const std::uint32_t w = info.width;
  const std::uint32_t h = info.height;
  switch (info.dxgi_format) {
    case 72:
      return {w * h / 2, linear};
    case 75: case 78: case 95: case 96: case 98: case 99:
      return {w * h, linear};
    case 29: case 30: case 32: case 91: case 93:
      return {w * 4, pitch};
    case 50: case 52:
      return {w * 2, pitch};
    case 62: case 63: case 64:
      return {w, pitch};
    default: {
      const std::uint32_t bits = info.dxgi_format < k_bits_per_pixel.size()
                                     ? k_bits_per_pixel[info.dxgi_format]
                                     : 0u;
      return {(w * bits) >> 3u, pitch};
    }
  }
}

struct Dx9Format {
  std::uint32_t pitch_or_linear;
  std::uint32_t size_flag;
  std::uint32_t pixel_flags;
  std::uint32_t fourcc;
  std::uint32_t bit_count;
  std::uint32_t masks[4];
};

std::optional<Dx9Format> dx9_format(const Ba2TextureInfo& info) {
  constexpr std::uint32_t linear = 0x80000u;
  constexpr std::uint32_t pitch = 0x8u;
  constexpr std::uint32_t fourcc_flag = 0x4u;
  const std::uint32_t w = info.width;
  const std::uint32_t h = info.height;
  switch (info.dxgi_format) {
    case 71: return Dx9Format{w * h / 2, linear, fourcc_flag, 0x31545844u, 0, {}};
    case 74: return Dx9Format{w * h, linear, fourcc_flag, 0x33545844u, 0, {}};
    case 77: return Dx9Format{w * h, linear, fourcc_flag, 0x35545844u, 0, {}};
    case 80: return Dx9Format{w * h / 2, linear, fourcc_flag, 0x55344342u, 0, {}};
    case 81: return Dx9Format{w * h / 2, linear, fourcc_flag, 0x53344342u, 0, {}};
    case 83: return Dx9Format{w * h, linear, fourcc_flag, 0x55354342u, 0, {}};
    case 84: return Dx9Format{w * h, linear, fourcc_flag, 0x53354342u, 0, {}};
    case 28: return Dx9Format{w * 4, pitch, 0x41u, 0, 32, {0xFFu, 0xFF00u, 0xFF0000u, 0xFF000000u}};
    case 87: return Dx9Format{w * 4, pitch, 0x41u, 0, 32, {0xFF0000u, 0xFF00u, 0xFFu, 0xFF000000u}};
    case 88: return Dx9Format{w * 4, pitch, 0x40u, 0, 32, {0xFF0000u, 0xFF00u, 0xFFu, 0}};
    case 85: return Dx9Format{w * 2, pitch, 0x40u, 0, 16, {0xF800u, 0x7E0u, 0x1Fu, 0}};
    case 86: return Dx9Format{w * 2, pitch, 0x41u, 0, 16, {0x7C00u, 0x3E0u, 0x1Fu, 0x8000u}};
    case 49: return Dx9Format{w * 2, pitch, 0x20001u, 0, 16, {0xFFu, 0, 0, 0xFF00u}};
    case 65: return Dx9Format{w, pitch, 0x2u, 0, 8, {0, 0, 0, 0xFFu}};
    case 61: return Dx9Format{w, pitch, 0x20000u, 0, 8, {0xFFu, 0, 0, 0}};
    default: return std::nullopt;
  }
}

std::vector<std::byte> dds_header_for(const Ba2TextureEntry& entry) {
  if (entry.texture.width == 0 || entry.texture.height == 0) {
    throw Ba2Error("DX10 BA2 texture has zero width or height");
  }

  const auto dx9 = dx9_format(entry.texture);
  const auto [pitch_or_linear, size_flag] =
      dx9 ? std::make_pair(dx9->pitch_or_linear, dx9->size_flag) : dds_pitch_and_flag(entry.texture);
  const auto mip_count = std::max<std::uint32_t>(1u, entry.texture.mip_count);
  const bool cube = (entry.texture.flags & 0x1u) != 0;

  std::vector<std::byte> out;
  out.reserve(dx9 ? k_dds_header_size : k_dds_dx10_header_size);
  append_u32(out, k_dds_magic);
  append_u32(out, 124u);
  append_u32(out, 0x1u | 0x2u | 0x4u | 0x1000u | 0x20000u | size_flag);
  append_u32(out, entry.texture.height);
  append_u32(out, entry.texture.width);
  append_u32(out, pitch_or_linear);
  append_u32(out, 1u);
  append_u32(out, mip_count);
  for (int i = 0; i < 11; ++i) append_u32(out, 0u);
  append_u32(out, 32u);
  if (dx9) {
    append_u32(out, dx9->pixel_flags);
    append_u32(out, dx9->fourcc);
    append_u32(out, dx9->bit_count);
    for (const auto mask : dx9->masks) append_u32(out, mask);
  } else {
    append_u32(out, 0x4u);
    append_u32(out, k_dds_dx10_fourcc);
    for (int i = 0; i < 5; ++i) append_u32(out, 0u);
  }
  append_u32(out, 0x1000u | (mip_count > 1 ? 0x400008u : 0u) | (cube ? 0x8u : 0u));
  append_u32(out, cube ? (0x200u | 0xFC00u) : 0u);
  for (int i = 0; i < 3; ++i) append_u32(out, 0u);
  if (!dx9) {
    append_u32(out, entry.texture.dxgi_format);
    append_u32(out, 3u);
    append_u32(out, cube ? 0x4u : 0u);
    append_u32(out, 1u);
    append_u32(out, 0u);
  }
  if (out.size() != (dx9 ? k_dds_header_size : k_dds_dx10_header_size)) {
    throw Ba2Error("internal DDS header size mismatch");
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

Ba2TextureArchive read_ba2_texture_impl(std::span<const std::byte> data) {
  if (data.size() < k_header_size) {
    throw Ba2Error("archive truncated before BA2 header");
  }

  binary::Cursor cursor(data);
  if (cursor.read_u32() != k_magic) {
    throw Ba2Error("not a BA2 archive: missing BTDX magic");
  }

  Ba2TextureArchive archive;
  archive.version = cursor.read_u32();
  if (!is_supported_ba2_texture_version(archive.version)) {
    throw Ba2Error("unsupported BA2 version " + std::to_string(archive.version) +
                   "; Radicle supports DX10 versions 1, 7 and 8");
  }

  const auto tag = cursor.read_u32();
  if (tag != k_dx10) {
    throw Ba2Error("unsupported BA2 content type; expected DX10 texture archive");
  }

  const auto file_count = cursor.read_u32();
  const auto string_table_offset_raw = read_u64(cursor);
  archive.has_string_table = string_table_offset_raw != 0;

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
    Ba2TextureEntry entry;
    entry.name_hash = cursor.read_u32();
    entry.extension_hash = cursor.read_u32();
    entry.directory_hash = cursor.read_u32();
    entry.unknown = cursor.read_u8();
    const auto chunk_count = cursor.read_u8();
    const auto chunk_header_size = cursor.read_u16();
    if (chunk_header_size != k_texture_chunk_header_size) {
      throw Ba2Error("DX10 entry " + std::to_string(file_index) +
                     " has unsupported chunk header size 0x" +
                     [] (std::uint16_t value) {
                       constexpr char digits[] = "0123456789ABCDEF";
                       std::string text(4, '0');
                       for (int i = 3; i >= 0; --i) {
                         text[static_cast<std::size_t>(i)] = digits[value & 0xFu];
                         value = static_cast<std::uint16_t>(value >> 4u);
                       }
                       return text;
                     }(chunk_header_size) +
                     ", expected 0x0018");
    }

    entry.texture.height = cursor.read_u16();
    entry.texture.width = cursor.read_u16();
    entry.texture.mip_count = cursor.read_u8();
    entry.texture.dxgi_format = cursor.read_u8();
    entry.texture.flags = cursor.read_u8();
    entry.texture.tile_mode = cursor.read_u8();

    if (static_cast<std::size_t>(chunk_count) > cursor.remaining() / k_chunk_record_size) {
      throw Ba2Error("DX10 entry " + std::to_string(file_index) +
                     " chunk count exceeds the remaining chunk table bytes");
    }

    entry.chunks.reserve(chunk_count);
    for (std::uint8_t chunk_index = 0; chunk_index < chunk_count; ++chunk_index) {
      const auto offset = read_u64(cursor);
      const auto packed_size = cursor.read_u32();
      const auto unpacked_size = cursor.read_u32();
      const auto mip_first = cursor.read_u16();
      const auto mip_last = cursor.read_u16();
      const auto sentinel = cursor.read_u32();
      if (sentinel != k_chunk_sentinel) {
        throw Ba2Error("DX10 entry " + std::to_string(file_index) + " chunk " +
                       std::to_string(chunk_index) + " has invalid BAADF00D sentinel");
      }

      entry.chunks.push_back(Ba2TextureChunk{
          {}, unpacked_size, packed_size != 0, mip_first, mip_last});
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

bool is_supported_ba2_texture_version(std::uint32_t version) noexcept {
  return version == 1u || version == 7u || version == 8u;
}

std::size_t ba2_texture_max_dds_header_size(const Ba2TextureInfo& info,
                                            std::string_view path) noexcept {
  if (!is_dds_path(path)) return 0;
  return dx9_format(info) ? k_dds_header_size : k_dds_dx10_header_size;
}

Ba2TextureArchive read_ba2_texture(std::span<const std::byte> data) {
  try {
    return read_ba2_texture_impl(data);
  } catch (const parse_error& error) {
    throw Ba2Error(
        "BA2 parse error at byte " + std::to_string(error.offset()) + ": " +
        error.parse_message());
  }
}

Ba2TextureArchive read_ba2_texture_file(const std::filesystem::path& path) {
  const auto bytes = read_file_bytes(path);
  return read_ba2_texture(bytes);
}

std::vector<std::byte> extract_ba2_texture_entry(const Ba2TextureEntry& entry) {
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
  if (total > k_max_extract_total_bytes) {
    throw Ba2Error("BA2 entry exceeds the " +
                   std::to_string(k_max_extract_total_bytes) +
                   "-byte extraction limit");
  }

  std::vector<std::byte> payload;
  payload.reserve(total);
  for (const auto& chunk : entry.chunks) {
    if (!chunk.compressed) {
      if (chunk.data.size() != chunk.decompressed_size) {
        throw Ba2Error("stored BA2 chunk size does not match header");
      }
      append_bytes(payload, chunk.data);
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
    append_bytes(payload, unpacked);
  }

  if (!is_dds_path(entry.path)) return payload;
  if (payload.size() >= 4 &&
      std::to_integer<std::uint8_t>(payload[0]) == 'D' &&
      std::to_integer<std::uint8_t>(payload[1]) == 'D' &&
      std::to_integer<std::uint8_t>(payload[2]) == 'S' &&
      std::to_integer<std::uint8_t>(payload[3]) == ' ') {
    return payload;
  }
  auto output = dds_header_for(entry);
  output.reserve(output.size() + payload.size());
  append_bytes(output, payload);
  return output;
}

const Ba2TextureEntry* find_ba2_texture_entry(const Ba2TextureArchive& archive,
                                              std::string_view path) {
  const auto wanted = normalized_path(path);
  for (const auto& entry : archive.entries) {
    if (normalized_path(entry.path) == wanted) {
      return &entry;
    }
  }
  return nullptr;
}

Ba2TextureChunk make_stored_ba2_texture_chunk(std::span<const std::byte> data) {
  if (data.size() > std::numeric_limits<std::uint32_t>::max()) {
    throw Ba2Error("BA2 chunk exceeds 32-bit size field");
  }
  return Ba2TextureChunk{
      std::vector<std::byte>(data.begin(), data.end()),
      static_cast<std::uint32_t>(data.size()), false, 0, 0};
}

Ba2TextureChunk make_compressed_ba2_texture_chunk(std::span<const std::byte> data) {
  if (data.size() > std::numeric_limits<std::uint32_t>::max() ||
      data.size() > std::numeric_limits<uLong>::max()) {
    throw Ba2Error("BA2 chunk exceeds zlib or format size limit");
  }
  if (data.empty()) {
    return make_stored_ba2_texture_chunk(data);
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
    return make_stored_ba2_texture_chunk(data);
  }

  return Ba2TextureChunk{
      std::move(packed), static_cast<std::uint32_t>(data.size()), true, 0, 0};
}

std::vector<std::byte> write_ba2_texture(const Ba2TextureArchive& archive) {
  if (!is_supported_ba2_texture_version(archive.version)) {
    throw Ba2Error("Radicle writes proven DX10 versions 1, 7 and 8 only");
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
      throw Ba2Error("BA2 DX10 entry has more than 255 chunks: " + entry.path);
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
  append_u32(out, k_dx10);
  append_u32(out, static_cast<std::uint32_t>(archive.entries.size()));
  append_u64(out, string_table_offset);

  std::size_t chunk_index = 0;
  for (const auto& entry : archive.entries) {
    append_u32(out, entry.name_hash);
    append_u32(out, entry.extension_hash);
    append_u32(out, entry.directory_hash);
    append_u8(out, entry.unknown);
    append_u8(out, static_cast<std::uint8_t>(entry.chunks.size()));
    append_u16(out, k_texture_chunk_header_size);

    append_u16(out, entry.texture.height);
    append_u16(out, entry.texture.width);
    append_u8(out, entry.texture.mip_count);
    append_u8(out, entry.texture.dxgi_format);
    append_u8(out, entry.texture.flags);
    append_u8(out, entry.texture.tile_mode);

    for (const auto& chunk : entry.chunks) {
      append_u64(out, chunk_offsets[chunk_index++]);
      append_u32(out, chunk.compressed ? static_cast<std::uint32_t>(chunk.data.size()) : 0u);
      append_u32(out, chunk.decompressed_size);
      append_u16(out, chunk.mip_first);
      append_u16(out, chunk.mip_last);
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

void write_ba2_texture_file(const Ba2TextureArchive& archive,
                            const std::filesystem::path& path) {
  const auto bytes = write_ba2_texture(archive);
  io::write_file_atomic(path, bytes);
}

}  // namespace fo4::archive
