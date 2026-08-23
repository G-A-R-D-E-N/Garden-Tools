#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "fo4/archive/ba2.hpp"
#include "fo4/archive/ba2_hash.hpp"
#include "fo4/archive/ba2_texture.hpp"

namespace {

using fo4::archive::Ba2Error;
using fo4::archive::Ba2Hashes;
using fo4::archive::Ba2TextureArchive;
using fo4::archive::Ba2TextureChunk;
using fo4::archive::Ba2TextureEntry;

std::vector<std::byte> bytes_of(std::string_view text) {
  std::vector<std::byte> out;
  out.reserve(text.size());
  for (const unsigned char ch : text) {
    out.push_back(static_cast<std::byte>(ch));
  }
  return out;
}

std::vector<std::byte> repeated(std::size_t count, std::uint8_t value) {
  return std::vector<std::byte>(count, static_cast<std::byte>(value));
}

}  // namespace

TEST(Ba2Hash, Crc32MatchesKnownValues) {
  EXPECT_EQ(fo4::archive::ba2_crc32(bytes_of("leather")), 0xB465C424u);
  EXPECT_EQ(fo4::archive::ba2_crc32(bytes_of("test")), 0xF93BA110u);
  EXPECT_EQ(fo4::archive::ba2_crc32(bytes_of("textures\\armor")), 0x055583E3u);
}

TEST(Ba2Hash, ComputeMatchesAuthoringTool) {
  const auto hashes = fo4::archive::compute_ba2_hashes("Textures\\Armor\\Leather.dds");
  EXPECT_EQ(hashes.name, 0xB465C424u);
  EXPECT_EQ(hashes.directory, 0x055583E3u);
  // "dds" packed little-endian: 'd' | 'd'<<8 | 's'<<16
  EXPECT_EQ(hashes.extension, 0x00736464u);
}

TEST(Ba2Hash, NormalizesSeparatorsCaseAndTrims) {
  // The C# oracle replaces '/' with '\\' and lowercases without collapsing
  // consecutive separators, then trims leading/trailing backslashes.
  EXPECT_EQ(fo4::archive::normalize_ba2_path("Textures//Armor/Leather.DDS"),
            "textures\\\\armor\\leather.dds");
  EXPECT_EQ(fo4::archive::normalize_ba2_path("\\Textures\\Armor\\"), "textures\\armor");
  EXPECT_EQ(fo4::archive::normalize_ba2_path(""), ".");
  EXPECT_EQ(fo4::archive::normalize_ba2_path(std::string(300, 'a')), ".");
}

TEST(Ba2Texture, RoundTripStoredAndCompressed) {
  Ba2TextureArchive archive;
  archive.version = 8;

  Ba2TextureEntry stored;
  stored.name_hash = 0xB465C424u;
  stored.extension_hash = 0x00736464u;
  stored.directory_hash = 0x055583E3u;
  stored.path = "textures\\armor\\leather.dds";
  stored.texture = {64, 256, 9, 71, 0, 8};
  stored.chunks.push_back(fo4::archive::make_stored_ba2_texture_chunk(bytes_of("stored-data")));

  Ba2TextureEntry compressed;
  compressed.name_hash = 0x9E6C1278u;
  compressed.extension_hash = 0x00736464u;
  compressed.directory_hash = 0x055583E3u;
  compressed.path = "textures\\armor\\main.dds";
  compressed.texture = {16, 16, 5, 98, 0, 8};
  const auto payload = repeated(4096, 0xAB);
  auto chunk = fo4::archive::make_compressed_ba2_texture_chunk(payload);
  chunk.mip_first = 0;
  chunk.mip_last = 2;
  compressed.chunks.push_back(std::move(chunk));

  archive.entries.push_back(std::move(stored));
  archive.entries.push_back(std::move(compressed));

  const auto serialized = fo4::archive::write_ba2_texture(archive);

  const auto read = fo4::archive::read_ba2_texture(serialized);
  EXPECT_EQ(read.version, 8u);
  EXPECT_TRUE(read.has_string_table);
  ASSERT_EQ(read.entries.size(), 2u);

  EXPECT_EQ(read.entries[0].path, "textures\\armor\\leather.dds");
  EXPECT_EQ(read.entries[0].name_hash, 0xB465C424u);
  EXPECT_EQ(read.entries[0].texture.height, 64u);
  EXPECT_EQ(read.entries[0].texture.width, 256u);
  EXPECT_EQ(read.entries[0].texture.mip_count, 9u);
  EXPECT_EQ(read.entries[0].texture.dxgi_format, 71u);
  EXPECT_EQ(read.entries[0].texture.tile_mode, 8u);
  ASSERT_EQ(read.entries[0].chunks.size(), 1u);
  EXPECT_FALSE(read.entries[0].chunks[0].compressed);
  EXPECT_EQ(read.entries[0].chunks[0].data, bytes_of("stored-data"));

  ASSERT_EQ(read.entries[1].chunks.size(), 1u);
  EXPECT_TRUE(read.entries[1].chunks[0].compressed);
  EXPECT_EQ(read.entries[1].chunks[0].mip_first, 0u);
  EXPECT_EQ(read.entries[1].chunks[0].mip_last, 2u);
  EXPECT_EQ(read.entries[1].chunks[0].decompressed_size, 4096u);
}

TEST(Ba2Texture, ExtractConcatenatesChunks) {
  Ba2TextureEntry entry;
  entry.path = "textures\\x.dds";
  entry.texture = {4, 4, 1, 71, 0, 8};
  entry.chunks.push_back(fo4::archive::make_stored_ba2_texture_chunk(bytes_of("abc")));
  const auto payload = repeated(512, 0x11);
  entry.chunks.push_back(fo4::archive::make_compressed_ba2_texture_chunk(payload));

  const auto extracted = fo4::archive::extract_ba2_texture_entry(entry);
  ASSERT_EQ(extracted.size(), 3u + 512u);
  EXPECT_EQ(std::string(reinterpret_cast<const char*>(extracted.data()), 3), "abc");
  EXPECT_EQ(extracted[3], static_cast<std::byte>(0x11));
  EXPECT_EQ(extracted.back(), static_cast<std::byte>(0x11));
}

TEST(Ba2Texture, FindIsCaseAndSeparatorInsensitive) {
  Ba2TextureArchive archive;
  Ba2TextureEntry entry;
  entry.path = "Textures\\Armor\\Leather.dds";
  archive.entries.push_back(std::move(entry));

  EXPECT_NE(fo4::archive::find_ba2_texture_entry(archive, "textures/armor/leather.dds"), nullptr);
  EXPECT_NE(fo4::archive::find_ba2_texture_entry(archive, "TEXTURES\\ARMOR\\LEATHER.DDS"), nullptr);
  EXPECT_EQ(fo4::archive::find_ba2_texture_entry(archive, "textures\\armor\\iron.dds"), nullptr);
}

TEST(Ba2Texture, RejectsMissingMagic) {
  std::vector<std::byte> data(64, std::byte{0});
  EXPECT_THROW(fo4::archive::read_ba2_texture(data), Ba2Error);
}

TEST(Ba2Texture, RejectsUnsupportedVersion) {
  Ba2TextureArchive archive;
  archive.version = 2;
  Ba2TextureEntry entry;
  entry.path = "textures\\x.dds";
  entry.texture = {4, 4, 1, 71, 0, 8};
  entry.chunks.push_back(fo4::archive::make_stored_ba2_texture_chunk(bytes_of("data")));
  archive.entries.push_back(std::move(entry));

  // The writer refuses to emit unproven versions.
  EXPECT_THROW(fo4::archive::write_ba2_texture(archive), Ba2Error);

  // The reader must reject a hand-built version-2 header as well.
  std::vector<std::byte> header;
  auto u32 = [&](std::uint32_t value) {
    header.push_back(static_cast<std::byte>(value & 0xFFu));
    header.push_back(static_cast<std::byte>((value >> 8u) & 0xFFu));
    header.push_back(static_cast<std::byte>((value >> 16u) & 0xFFu));
    header.push_back(static_cast<std::byte>((value >> 24u) & 0xFFu));
  };
  u32(0x58445442u);  // BTDX
  u32(2);
  u32(0x30315844u);  // DX10
  u32(0);
  u32(0);
  u32(0);
  EXPECT_THROW(fo4::archive::read_ba2_texture(header), Ba2Error);
}

TEST(Ba2Texture, RejectsGeneralArchiveContent) {
  // Reuse a GNRL archive writer by hand: correct header but wrong tag.
  std::vector<std::byte> data;
  auto u32 = [&](std::uint32_t value) {
    data.push_back(static_cast<std::byte>(value & 0xFFu));
    data.push_back(static_cast<std::byte>((value >> 8u) & 0xFFu));
    data.push_back(static_cast<std::byte>((value >> 16u) & 0xFFu));
    data.push_back(static_cast<std::byte>((value >> 24u) & 0xFFu));
  };
  auto u64 = [&](std::uint64_t value) {
    u32(static_cast<std::uint32_t>(value & 0xFFFFFFFFull));
    u32(static_cast<std::uint32_t>(value >> 32u));
  };
  u32(0x58445442u);  // BTDX
  u32(1);
  u32(0x4C524E47u);  // GNRL
  u32(0);
  u64(0);
  EXPECT_THROW(fo4::archive::read_ba2_texture(data), Ba2Error);
}

TEST(Ba2Texture, RejectsBadSentinel) {
  Ba2TextureArchive archive;
  Ba2TextureEntry entry;
  entry.path = "textures\\x.dds";
  entry.texture = {4, 4, 1, 71, 0, 8};
  entry.chunks.push_back(fo4::archive::make_stored_ba2_texture_chunk(bytes_of("data")));
  archive.entries.push_back(std::move(entry));

  auto serialized = fo4::archive::write_ba2_texture(archive);
  // Corrupt the sentinel of the only chunk: it sits at 24 (header) + 24 (entry) + 20 (chunk
  // record minus sentinel) .. 24 + 24 + 24.
  ASSERT_GE(serialized.size(), 72u);
  serialized[68] = std::byte{0x00};
  serialized[69] = std::byte{0x00};
  serialized[70] = std::byte{0x00};
  serialized[71] = std::byte{0x00};
  EXPECT_THROW(fo4::archive::read_ba2_texture(serialized), Ba2Error);
}
