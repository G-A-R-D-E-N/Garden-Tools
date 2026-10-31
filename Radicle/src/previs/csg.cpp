#include "fo4/previs/csg.hpp"

#include "fo4/binary/zlib_bound.hpp"
#include "fo4/error.hpp"

#include <zlib.h>

#include <cstring>
#include <string_view>

namespace fo4::previs {
namespace {

constexpr std::size_t k_header_bytes = 12;

std::uint32_t u32le(std::span<const std::byte> data, std::size_t at) {
  std::uint32_t v = 0;
  std::memcpy(&v, data.data() + at, 4);
  return v;
}

void append_le(std::vector<std::byte>& out, std::uint32_t v) {
  out.push_back(static_cast<std::byte>(v & 0xFFu));
  out.push_back(static_cast<std::byte>((v >> 8) & 0xFFu));
  out.push_back(static_cast<std::byte>((v >> 16) & 0xFFu));
  out.push_back(static_cast<std::byte>((v >> 24) & 0xFFu));
}

bool magic_is(std::span<const std::byte> data, std::string_view magic) {
  if (data.size() < 4) return false;
  for (std::size_t i = 0; i < 4; ++i) {
    if (std::to_integer<char>(data[i]) != magic[i]) return false;
  }
  return true;
}

}  // namespace

CsgFile parse_csg(std::span<const std::byte> data) {
  if (data.size() < k_header_bytes) throw parse_error(0, "CSG is shorter than its header");
  if (!magic_is(data, "bcsg")) throw parse_error(0, "CSG has a bad magic (only bcsg observed)");
  const auto row_count = u32le(data, 4);
  const auto chunk_count = u32le(data, 8);
  const std::uint64_t table_end = k_header_bytes + std::uint64_t{chunk_count} * k_csg_chunk_entry_bytes +
                                  std::uint64_t{row_count} * k_csg_row_bytes;
  if (table_end > data.size()) throw parse_error(4, "CSG tables run past the end of the file");
  CsgFile file;
  file.rows.reserve(row_count);
  file.chunks.reserve(chunk_count);
  const std::size_t chunk_table = k_header_bytes;
  const std::size_t row_table = chunk_table + static_cast<std::size_t>(chunk_count) * k_csg_chunk_entry_bytes;
  for (std::uint64_t i = 0; i < chunk_count; ++i) {
    const std::size_t at = chunk_table + static_cast<std::size_t>(i) * k_csg_chunk_entry_bytes;
    const auto offset = u32le(data, at);
    const auto size = u32le(data, at + 4);
    if (std::uint64_t{offset} + size > data.size()) {
      throw parse_error(at, "CSG chunk runs past the end of the file");
    }
    file.chunks.push_back(std::vector<std::byte>(data.begin() + offset, data.begin() + offset + size));
  }
  for (std::uint64_t i = 0; i < row_count; ++i) {
    CsgRow row;
    std::memcpy(row.bytes.data(), data.data() + row_table + static_cast<std::size_t>(i) * k_csg_row_bytes,
                k_csg_row_bytes);
    file.rows.push_back(row);
  }
  return file;
}

std::vector<std::byte> write_csg(const CsgFile& file) {
  if (file.chunks.size() > 0xFFFFFFFFu || file.rows.size() > 0xFFFFFFFFu) {
    throw parse_error(0, "CSG table count exceeds u32");
  }
  const auto chunk_count = static_cast<std::uint32_t>(file.chunks.size());
  const auto row_count = static_cast<std::uint32_t>(file.rows.size());
  std::vector<std::byte> out;
  for (const char c : std::string_view{"bcsg"}) out.push_back(static_cast<std::byte>(c));
  append_le(out, row_count);
  append_le(out, chunk_count);
  std::uint64_t payload_at = k_header_bytes + file.chunks.size() * k_csg_chunk_entry_bytes +
                             file.rows.size() * k_csg_row_bytes;
  for (const auto& chunk : file.chunks) {
    if (chunk.size() > 0xFFFFFFFFu || payload_at > 0xFFFFFFFFu ||
        payload_at + chunk.size() > 0xFFFFFFFFu) {
      throw parse_error(0, "CSG chunk offset exceeds u32");
    }
    append_le(out, static_cast<std::uint32_t>(payload_at));
    append_le(out, static_cast<std::uint32_t>(chunk.size()));
    payload_at += chunk.size();
  }
  for (const auto& row : file.rows) {
    out.insert(out.end(), row.bytes.begin(), row.bytes.end());
  }
  for (const auto& chunk : file.chunks) {
    out.insert(out.end(), chunk.begin(), chunk.end());
  }
  return out;
}

std::vector<std::string> validate_csg(std::span<const std::byte> data) {
  try {
    (void)parse_csg(data);
    return {};
  } catch (const std::exception& e) {
    return {e.what()};
  }
}

std::vector<std::byte> decompress_csg_chunk(std::span<const std::byte> chunk) {
  if (chunk.empty()) throw parse_error(0, "CSG chunk is empty");
  const std::uint64_t bound = std::uint64_t{chunk.size()} * fo4::binary::k_zlib_max_expansion +
                              fo4::binary::k_zlib_slack_bytes;
  if (bound > 1024ull * 1024ull * 1024ull) throw parse_error(0, "CSG chunk declares too much output");
  std::vector<std::byte> out(static_cast<std::size_t>(bound));
  z_stream stream{};
  stream.next_in = reinterpret_cast<Bytef*>(const_cast<std::byte*>(chunk.data()));
  stream.avail_in = static_cast<uInt>(chunk.size());
  stream.next_out = reinterpret_cast<Bytef*>(out.data());
  stream.avail_out = static_cast<uInt>(out.size());
  if (inflateInit(&stream) != Z_OK) throw parse_error(0, "CSG chunk inflater failed to start");
  const int rc = inflate(&stream, Z_FINISH);
  const std::size_t produced = out.size() - stream.avail_out;
  inflateEnd(&stream);
  if (rc != Z_STREAM_END) throw parse_error(0, "CSG chunk is not a complete zlib stream");
  out.resize(produced);
  return out;
}

}  // namespace fo4::previs
