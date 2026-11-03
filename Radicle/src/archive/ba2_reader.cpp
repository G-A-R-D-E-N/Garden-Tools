#include "fo4/archive/ba2_reader.hpp"

#include "fo4/archive/ba2_hash.hpp"
#include "fo4/io/path_text.hpp"
#include "fo4/text/encoding.hpp"

#include <array>
#include <algorithm>
#include <cstring>
#include <fstream>
#include <limits>
#include <memory>
#include <mutex>
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

using io::utf8;

class Sha256 {
public:
  void update(std::span<const std::byte> bytes) {
    total_ += bytes.size();
    for (const auto byte : bytes) {
      block_[used_++] = std::to_integer<std::uint8_t>(byte);
      if (used_ == block_.size()) {
        compress(block_.data());
        used_ = 0;
      }
    }
  }

  std::string finish() {
    const std::uint64_t bits = total_ * 8;
    block_[used_++] = 0x80;
    if (used_ > 56) {
      std::fill(block_.begin() + static_cast<std::ptrdiff_t>(used_), block_.end(), std::uint8_t{0});
      compress(block_.data());
      used_ = 0;
    }
    std::fill(block_.begin() + static_cast<std::ptrdiff_t>(used_), block_.begin() + 56, 0);
    for (std::size_t i = 0; i < 8; ++i) block_[63 - i] = static_cast<std::uint8_t>(bits >> (i * 8));
    compress(block_.data());
    static constexpr char hex[] = "0123456789abcdef";
    std::string out;
    out.reserve(64);
    for (const auto word : state_) {
      for (int shift = 28; shift >= 0; shift -= 4) out.push_back(hex[(word >> shift) & 0x0f]);
    }
    return out;
  }

private:
  static std::uint32_t rotate(std::uint32_t value, unsigned bits) { return (value >> bits) | (value << (32 - bits)); }
  void compress(const std::uint8_t* block) {
    static constexpr std::array<std::uint32_t, 64> k = {
        0x428a2f98U,0x71374491U,0xb5c0fbcfU,0xe9b5dba5U,0x3956c25bU,0x59f111f1U,0x923f82a4U,0xab1c5ed5U,
        0xd807aa98U,0x12835b01U,0x243185beU,0x550c7dc3U,0x72be5d74U,0x80deb1feU,0x9bdc06a7U,0xc19bf174U,
        0xe49b69c1U,0xefbe4786U,0x0fc19dc6U,0x240ca1ccU,0x2de92c6fU,0x4a7484aaU,0x5cb0a9dcU,0x76f988daU,
        0x983e5152U,0xa831c66dU,0xb00327c8U,0xbf597fc7U,0xc6e00bf3U,0xd5a79147U,0x06ca6351U,0x14292967U,
        0x27b70a85U,0x2e1b2138U,0x4d2c6dfcU,0x53380d13U,0x650a7354U,0x766a0abbU,0x81c2c92eU,0x92722c85U,
        0xa2bfe8a1U,0xa81a664bU,0xc24b8b70U,0xc76c51a3U,0xd192e819U,0xd6990624U,0xf40e3585U,0x106aa070U,
        0x19a4c116U,0x1e376c08U,0x2748774cU,0x34b0bcb5U,0x391c0cb3U,0x4ed8aa4aU,0x5b9cca4fU,0x682e6ff3U,
        0x748f82eeU,0x78a5636fU,0x84c87814U,0x8cc70208U,0x90befffaU,0xa4506cebU,0xbef9a3f7U,0xc67178f2U};
    std::array<std::uint32_t, 64> w{};
    for (std::size_t i = 0; i < 16; ++i) {
      w[i] = (static_cast<std::uint32_t>(block[i * 4]) << 24) | (static_cast<std::uint32_t>(block[i * 4 + 1]) << 16) |
             (static_cast<std::uint32_t>(block[i * 4 + 2]) << 8) | block[i * 4 + 3];
    }
    for (std::size_t i = 16; i < 64; ++i) {
      const auto a = rotate(w[i - 15], 7) ^ rotate(w[i - 15], 18) ^ (w[i - 15] >> 3);
      const auto b = rotate(w[i - 2], 17) ^ rotate(w[i - 2], 19) ^ (w[i - 2] >> 10);
      w[i] = w[i - 16] + a + w[i - 7] + b;
    }
    auto [a,b,c,d,e,f,g,h] = state_;
    for (std::size_t i = 0; i < 64; ++i) {
      const auto t1 = h + (rotate(e, 6) ^ rotate(e, 11) ^ rotate(e, 25)) + ((e & f) ^ (~e & g)) + k[i] + w[i];
      const auto t2 = (rotate(a, 2) ^ rotate(a, 13) ^ rotate(a, 22)) + ((a & b) ^ (a & c) ^ (b & c));
      h = g; g = f; f = e; e = d + t1; d = c; c = b; b = a; a = t1 + t2;
    }
    state_[0] += a; state_[1] += b; state_[2] += c; state_[3] += d;
    state_[4] += e; state_[5] += f; state_[6] += g; state_[7] += h;
  }

  std::array<std::uint32_t, 8> state_{0x6a09e667U,0xbb67ae85U,0x3c6ef372U,0xa54ff53aU,
                                      0x510e527fU,0x9b05688cU,0x1f83d9abU,0x5be0cd19U};
  std::array<std::uint8_t, 64> block_{};
  std::size_t used_{0};
  std::uint64_t total_{0};
};

class PathByteSource final : public Ba2ByteSource {
public:
  explicit PathByteSource(const std::filesystem::path& path) : path_(path), stream_(path, std::ios::binary) {
    if (!stream_) throw Ba2Error("could not open BA2: " + utf8(path));
    stream_.seekg(0, std::ios::end);
    const auto end = stream_.tellg();
    if (end < 0) throw Ba2Error("could not determine BA2 size: " + utf8(path));
    size_ = static_cast<std::uint64_t>(end);
  }
  std::uint64_t size() const noexcept override { return size_; }
  void read(std::uint64_t offset, std::span<std::byte> out) const override {
    if (offset > size_ || out.size() > size_ - offset) throw Ba2Error("BA2 is truncated: " + utf8(path_));
    stream_.clear();
    stream_.seekg(static_cast<std::streamoff>(offset), std::ios::beg);
    if (!stream_) throw Ba2Error("could not seek in BA2: " + utf8(path_));
    stream_.read(reinterpret_cast<char*>(out.data()), static_cast<std::streamsize>(out.size()));
    if (!stream_) throw Ba2Error("could not read BA2: " + utf8(path_));
  }
private:
  std::filesystem::path path_;
  mutable std::ifstream stream_;
  std::uint64_t size_{0};
};

}

class FileCursor {
public:
  explicit FileCursor(std::shared_ptr<const Ba2ByteSource> source, std::filesystem::path path = {})
      : source_(std::move(source)), path_(std::move(path)), size_(source_ ? source_->size() : 0) {
    if (!source_) throw Ba2Error("BA2 source is missing");
  }

  std::uint64_t size() const noexcept { return size_; }
  std::uint64_t position() const noexcept { return position_; }

  void seek(std::uint64_t offset) {
    if (offset > size_) throw Ba2Error("BA2 offset points outside the archive: " + utf8(path_));
    position_ = offset;
  }

  void read(void* out, std::size_t length) {
    if (length > size_ - position_) throw Ba2Error("BA2 is truncated: " + utf8(path_));
    source_->read(position_, {static_cast<std::byte*>(out), length});
    position_ += length;
  }

  std::string hash() {
    if (source_->size() != size_) throw Ba2Error("BA2 source size changed: " + utf8(path_));
    Sha256 hash;
    std::array<std::byte, 65536> buffer{};
    for (std::uint64_t offset = 0; offset < size_;) {
      const auto length = static_cast<std::size_t>(std::min<std::uint64_t>(buffer.size(), size_ - offset));
      source_->read(offset, {buffer.data(), length});
      hash.update({buffer.data(), length});
      offset += length;
    }
    if (source_->size() != size_) throw Ba2Error("BA2 source size changed: " + utf8(path_));
    return hash.finish();
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

  std::shared_ptr<const Ba2ByteSource> source_;
  std::filesystem::path path_;
  std::uint64_t size_{0};
  std::uint64_t position_{0};
};

std::string hash_byte_source(const std::shared_ptr<const Ba2ByteSource>& source) {
  return FileCursor(source).hash();
}

namespace {

std::string hash_key(std::uint32_t name, std::uint32_t extension, std::uint32_t directory) {
  std::string key(12, '\0');
  std::memcpy(key.data(), &name, 4);
  std::memcpy(key.data() + 4, &extension, 4);
  std::memcpy(key.data() + 8, &directory, 4);
  return key;
}

std::string lookup_key(bool named, std::string_view path) {
  if (named) return normalize_ba2_name(path);
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
  return open_source(std::make_shared<PathByteSource>(path), path,
                     {std::numeric_limits<std::size_t>::max(), std::numeric_limits<std::uint64_t>::max()}, nullptr);
}

Ba2Reader Ba2Reader::open_source(const std::shared_ptr<const Ba2ByteSource>& source,
                                 const std::filesystem::path& label, Ba2IndexLimits limits,
                                 std::uint64_t* index_bytes) {
  FileCursor file(source, label);
  Ba2Reader reader;
  reader.path_ = label;
  if (file.size() < k_header_size) throw Ba2Error("archive truncated before BA2 header: " + utf8(label));
  if (file.u32() != k_magic) throw Ba2Error("not a BA2 archive: missing BTDX magic: " + utf8(label));
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
    throw Ba2Error("unsupported BA2 content type; expected GNRL or DX10: " + utf8(label));
  }
  const bool texture = reader.kind_ == Ba2Kind::texture;
  const auto file_count = file.u32();
  if (file_count > limits.max_entries) throw Ba2Error("BA2 index entry limit exceeded");
  std::uint64_t used = 0;
  const auto charge = [&](std::uint64_t amount) {
    if (amount > limits.max_bytes - used) throw Ba2Error("BA2 index memory limit exceeded");
    used += amount;
  };
  charge(static_cast<std::uint64_t>(file_count) * (sizeof(Ba2EntryInfo) + 128));
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
    charge(static_cast<std::uint64_t>(chunk_count) * sizeof(Ba2ChunkLocation));
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

  reader.by_path_.reserve(reader.entries_.size());
  if (reader.has_name_table_) file.seek(name_table_offset);
  for (std::size_t index = 0; index < reader.entries_.size(); ++index) {
    auto& entry = reader.entries_[index];
    std::string key;
    if (reader.has_name_table_) {
      const auto length = file.u16();
      charge(static_cast<std::uint64_t>(length) * 8 + 256);
      std::string stored(length, '\0');
      if (!stored.empty()) file.read(stored.data(), stored.size());
      key = normalize_ba2_name(stored);
      entry.path = text::windows1252_to_utf8(stored);
    } else {
      key = hash_key(entry.name_hash, entry.extension_hash, entry.directory_hash);
    }
    reader.by_path_.emplace(std::move(key), index);
  }
  if (index_bytes) *index_bytes = used;
  return reader;
}

std::optional<std::size_t> Ba2Reader::find(std::string_view path) const {
  const auto stored = text::utf8_to_windows1252(path);
  if (!stored) return std::nullopt;
  const auto it = by_path_.find(lookup_key(has_name_table_, *stored));
  if (it == by_path_.end()) return std::nullopt;
  return it->second;
}

std::vector<std::byte> Ba2Reader::extract(std::size_t index) const {
  FileCursor file(std::make_shared<PathByteSource>(path_), path_);
  return extract_with(file, index, std::numeric_limits<std::uint64_t>::max());
}

std::vector<std::byte> Ba2Reader::extract_with(FileCursor& file, std::size_t index,
                                               std::uint64_t max_input_bytes) const {
  const auto& info = entries_.at(index);
  std::uint64_t input_size = 0;
  for (const auto& chunk : info.chunks) {
    const auto length = chunk.packed_size != 0 ? chunk.packed_size : chunk.unpacked_size;
    if (length > max_input_bytes - input_size) throw Ba2Error("BA2 entry exceeds the caller extraction limit");
    input_size += length;
  }
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

struct Ba2Snapshot::Impl {
  explicit Impl(std::shared_ptr<const Ba2ByteSource> held_source)
      : source(std::move(held_source)), file(source, "held BA2 source") {}

  std::shared_ptr<const Ba2ByteSource> source;
  FileCursor file;
  Ba2Reader reader;
  std::string digest;
  std::uint64_t indexBytes{0};
  mutable std::mutex mutex;
};

Ba2Snapshot Ba2Snapshot::open(std::shared_ptr<const Ba2ByteSource> source) {
  return open(std::move(source), {});
}

Ba2Snapshot Ba2Snapshot::open(std::shared_ptr<const Ba2ByteSource> source, Ba2IndexLimits limits) {
  if (!source) throw Ba2Error("BA2 source is missing");
  auto impl = std::make_shared<Impl>(std::move(source));
  const auto before = impl->file.hash();
  impl->reader = Ba2Reader::open_source(impl->source, "held BA2 source", limits, &impl->indexBytes);
  const auto after = impl->file.hash();
  if (before != after) throw Ba2Error("BA2 source changed while its index was read");
  impl->digest = after;
  Ba2Snapshot snapshot;
  snapshot.impl_ = std::move(impl);
  return snapshot;
}

const std::vector<Ba2EntryInfo>& Ba2Snapshot::entries() const noexcept { return impl_->reader.entries(); }
Ba2Kind Ba2Snapshot::kind() const noexcept { return impl_->reader.kind(); }
std::uint32_t Ba2Snapshot::version() const noexcept { return impl_->reader.version(); }
bool Ba2Snapshot::has_name_table() const noexcept { return impl_->reader.has_name_table(); }
const std::string& Ba2Snapshot::archive_sha256() const noexcept { return impl_->digest; }
std::uint64_t Ba2Snapshot::index_bytes() const noexcept { return impl_->indexBytes; }
std::optional<std::size_t> Ba2Snapshot::find(std::string_view path) const { return impl_->reader.find(path); }

Ba2ExtractedPayload Ba2Snapshot::extract(std::size_t index, std::uint64_t max_bytes) const {
  const std::array<std::size_t, 1> indices{index};
  auto results = extract_many(indices, max_bytes);
  return std::move(results.front());
}

std::vector<Ba2ExtractedPayload> Ba2Snapshot::extract_many(std::span<const std::size_t> indices,
                                                          std::uint64_t max_total_bytes) const {
  std::lock_guard lock(impl_->mutex);
  if (indices.empty()) return {};
  if (impl_->file.hash() != impl_->digest) throw Ba2Error("BA2 source changed after its index was read");
  const auto byte_limit = std::min<std::uint64_t>(max_total_bytes, k_max_extract_total_bytes);
  std::vector<Ba2ExtractedPayload> payloads;
  payloads.reserve(indices.size());
  std::uint64_t output_total = 0;
  for (const auto index : indices) {
    const auto& info = impl_->reader.entries().at(index);
    std::uint64_t expected_size = 0;
    std::uint64_t input_size = 0;
    std::uint64_t scratch_size = 0;
    for (const auto& chunk : info.chunks) {
      if (chunk.unpacked_size > std::numeric_limits<std::uint64_t>::max() - expected_size) {
        throw Ba2Error("BA2 entry size overflows its format limit");
      }
      expected_size += chunk.unpacked_size;
      const auto length = chunk.packed_size != 0 ? chunk.packed_size : chunk.unpacked_size;
      if (length > std::numeric_limits<std::uint64_t>::max() - input_size) {
        throw Ba2Error("BA2 entry input size overflows its format limit");
      }
      input_size += length;
      if (chunk.packed_size != 0) scratch_size = std::max<std::uint64_t>(scratch_size, chunk.unpacked_size);
    }
    const auto dds_path = info.path.empty() ? std::string_view("unnamed.dds") : std::string_view(info.path);
    const auto header_size = impl_->reader.kind() == Ba2Kind::texture
                                 ? ba2_texture_max_dds_header_size(info.texture, dds_path)
                                 : 0u;
    if (header_size > std::numeric_limits<std::uint64_t>::max() - expected_size) {
      throw Ba2Error("BA2 entry size overflows its format limit");
    }
    const auto output_size = expected_size + header_size;
    if (output_total > byte_limit || output_size > byte_limit - output_total) {
      throw Ba2Error("BA2 extraction batch exceeds the caller byte limit");
    }
    auto input_limit = byte_limit - output_total - output_size;
    const auto working_scratch = header_size != 0 ? output_size : scratch_size;
    if (working_scratch > input_limit) throw Ba2Error("BA2 extraction batch exceeds the caller byte limit");
    input_limit -= working_scratch;
    if (input_size > input_limit) throw Ba2Error("BA2 extraction batch exceeds the caller byte limit");
    auto bytes = impl_->reader.extract_with(impl_->file, index, input_limit);
    if (bytes.size() > output_size) throw Ba2Error("BA2 extracted payload exceeds its indexed size");
    Sha256 hash;
    hash.update(bytes);
    const auto size = static_cast<std::uint64_t>(bytes.size());
    output_total += size;
    payloads.push_back({std::move(bytes), size, hash.finish()});
  }
  if (impl_->file.hash() != impl_->digest) throw Ba2Error("BA2 source changed while its payload was read");
  return payloads;
}

}  // namespace fo4::archive
