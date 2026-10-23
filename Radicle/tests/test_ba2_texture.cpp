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
using fo4::archive::Ba2TextureInfo;

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

std::uint32_t read_u32(const std::vector<std::byte>& data, std::size_t offset) {
  return std::to_integer<std::uint32_t>(data[offset]) |
         (std::to_integer<std::uint32_t>(data[offset + 1]) << 8u) |
         (std::to_integer<std::uint32_t>(data[offset + 2]) << 16u) |
         (std::to_integer<std::uint32_t>(data[offset + 3]) << 24u);
}

}  // namespace

TEST(Ba2Hash, Crc32MatchesKnownValues) {
  EXPECT_EQ(fo4::archive::ba2_crc32(bytes_of("leather")), 0xB465C424u);
  EXPECT_EQ(fo4::archive::ba2_crc32(bytes_of("test")), 0xF93BA110u);
  EXPECT_EQ(fo4::archive::ba2_crc32(bytes_of("textures\\armor")), 0x055583E3u);

  const auto hashes = fo4::archive::compute_ba2_hashes("Textures\\Armor\\Leather.dds");
  EXPECT_EQ(hashes.name, 0xB465C424u);
  EXPECT_EQ(hashes.directory, 0x055583E3u);
  EXPECT_EQ(hashes.extension, 0x00736464u);
}

TEST(Ba2Hash, NormalizesSeparatorsCaseAndTrims) {
  EXPECT_EQ(fo4::archive::normalize_ba2_path("Textures//Armor/Leather.DDS"),
            "textures\\\\armor\\leather.dds");
  EXPECT_EQ(fo4::archive::normalize_ba2_path("\\Textures\\Armor\\"), "textures\\armor");
  EXPECT_EQ(fo4::archive::normalize_ba2_path(""), ".");
  EXPECT_EQ(fo4::archive::normalize_ba2_path(std::string(300, 'a')), ".");
}

TEST(Ba2Texture, RoundTripStoredAndCompressed) {
  for (const std::uint32_t version : {1u, 7u, 8u}) {
    SCOPED_TRACE(version);
    Ba2TextureArchive archive;
    archive.version = version;

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
    EXPECT_EQ(read.version, version);
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
}

TEST(Ba2Texture, CubemapFlagSurvivesRebuildAndMarksDdsHeader) {
  for (const std::uint32_t version : {1u, 7u, 8u}) {
    SCOPED_TRACE(version);
    Ba2TextureArchive archive;
    archive.version = version;

    Ba2TextureEntry cube;
    cube.name_hash = 0xB465C424u;
    cube.extension_hash = 0x00736464u;
    cube.directory_hash = 0x055583E3u;
    cube.path = "textures\\armor\\sky.dds";
    cube.texture = {4, 4, 1, 98, 0x1, 8};
    const auto payload = repeated(48, 0x5A);
    cube.chunks.push_back(fo4::archive::make_stored_ba2_texture_chunk(payload));
    archive.entries.push_back(std::move(cube));

    const auto read = fo4::archive::read_ba2_texture(fo4::archive::write_ba2_texture(archive));
    ASSERT_EQ(read.entries.size(), 1u);
    EXPECT_EQ(read.entries[0].texture.flags & 0x1u, 0x1u);

    const auto extracted = fo4::archive::extract_ba2_texture_entry(read.entries[0]);
    ASSERT_EQ(extracted.size(), 148u + payload.size());
    EXPECT_EQ(read_u32(extracted, 112), 0xFE00u);
    EXPECT_EQ(read_u32(extracted, 136), 0x4u);
    EXPECT_EQ(std::vector<std::byte>(extracted.begin() + 148, extracted.end()), payload);

    const auto reread = fo4::archive::read_ba2_texture(fo4::archive::write_ba2_texture(read));
    ASSERT_EQ(reread.entries.size(), 1u);
    EXPECT_EQ(reread.entries[0].texture.flags, read.entries[0].texture.flags);
    EXPECT_EQ(fo4::archive::extract_ba2_texture_entry(reread.entries[0]), extracted);
  }
}

TEST(Ba2Texture, ExtractConcatenatesNonTextureChunks) {
  Ba2TextureEntry entry;
  entry.path = "data\\x.bin";
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

struct DdsHeaderCase {
  const char* name;
  const char* path;
  Ba2TextureInfo texture;
  std::size_t header_size;
  std::vector<std::pair<std::size_t, std::uint32_t>> fields;
};

class Ba2TextureDdsHeader : public testing::TestWithParam<DdsHeaderCase> {};

TEST_P(Ba2TextureDdsHeader, RebuildsHeaderFromTextureInfo) {
  const auto& param = GetParam();
  SCOPED_TRACE(param.name);
  Ba2TextureEntry entry;
  entry.path = param.path;
  entry.texture = param.texture;
  const auto payload = repeated(8, 0x5A);
  entry.chunks.push_back(fo4::archive::make_stored_ba2_texture_chunk(payload));

  const auto extracted = fo4::archive::extract_ba2_texture_entry(entry);
  ASSERT_EQ(extracted.size(), param.header_size + payload.size());
  for (const auto& [offset, value] : param.fields) {
    EXPECT_EQ(read_u32(extracted, offset), value);
  }
  EXPECT_EQ(std::vector<std::byte>(extracted.begin() + static_cast<std::ptrdiff_t>(param.header_size),
                                   extracted.end()),
            payload);
}

INSTANTIATE_TEST_SUITE_P(
    Formats, Ba2TextureDdsHeader,
    testing::Values(
        DdsHeaderCase{"Dx10Metadata", "textures\\x.dds", {4, 4, 1, 98, 0, 8}, 148,
                      {{0, 0x20534444u}, {4, 124u}, {12, 4u}, {16, 4u}, {28, 1u}, {84, 0x30315844u},
                       {128, 98u}}},
        DdsHeaderCase{"Bc7LinearSizeNonMultipleOf4", "textures\\x.dds", {10, 6, 1, 98, 0, 8}, 148,
                      {{8, 0xA1007u}, {20, 60u}}},
        DdsHeaderCase{"Bc1SrgbHalfArea", "textures\\x.dds", {10, 6, 1, 72, 0, 8}, 148,
                      {{8, 0xA1007u}, {20, 30u}}},
        DdsHeaderCase{"UnlistedDx10BitsPerPixel", "textures\\x.dds", {10, 6, 1, 10, 0, 8}, 148,
                      {{8, 0x2100Fu}, {20, 48u}, {128, 10u}}},
        DdsHeaderCase{"ClassicBc1", "textures\\x.dds", {4, 8, 1, 71, 0, 8}, 128,
                      {{8, 0xA1007u}, {20, 16u}, {76, 32u}, {80, 0x4u}, {84, 0x31545844u},
                       {108, 0x1000u}}},
        DdsHeaderCase{"Dxt5FourCcForBc3", "textures\\x.dds", {4, 8, 1, 77, 0, 8}, 128,
                      {{20, 32u}, {84, 0x35545844u}}},
        DdsHeaderCase{"BgraPixelMasksDx9", "textures\\x.dds", {4, 8, 1, 87, 0, 8}, 128,
                      {{8, 0x2100Fu}, {20, 32u}, {80, 0x41u}, {88, 32u}, {92, 0xFF0000u},
                       {104, 0xFF000000u}}},
        DdsHeaderCase{"SingleMipDx9CubemapComplex", "textures\\sky.dds", {4, 4, 1, 71, 0x1, 8}, 128,
                      {{108, 0x1008u}, {112, 0xFE00u}}}),
    [](const testing::TestParamInfo<DdsHeaderCase>& info) { return std::string(info.param.name); });

TEST(Ba2Texture, ExtractLeavesAlreadyCompleteDdsPayloadAlone) {
  Ba2TextureEntry entry;
  entry.path = "textures\\x.dds";
  entry.texture = {4, 4, 1, 71, 0, 8};
  auto payload = repeated(148, 0);
  payload[0] = std::byte{'D'};
  payload[1] = std::byte{'D'};
  payload[2] = std::byte{'S'};
  payload[3] = std::byte{' '};
  entry.chunks.push_back(fo4::archive::make_stored_ba2_texture_chunk(payload));

  EXPECT_EQ(fo4::archive::extract_ba2_texture_entry(entry), payload);
}

TEST(Ba2Texture, ExtractReturnsNonDdsPayloadUnchanged) {
  Ba2TextureEntry entry;
  entry.path = "textures\\x.bin";
  entry.texture = {4, 4, 1, 98, 0, 8};
  const auto payload = repeated(8, 0x5A);
  entry.chunks.push_back(fo4::archive::make_stored_ba2_texture_chunk(payload));

  EXPECT_EQ(fo4::archive::extract_ba2_texture_entry(entry), payload);
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

class Ba2TextureUnprovenVersion : public testing::TestWithParam<std::uint32_t> {};

TEST_P(Ba2TextureUnprovenVersion, RejectsOnWriteAndRead) {
  Ba2TextureArchive archive;
  archive.version = GetParam();
  Ba2TextureEntry entry;
  entry.path = "textures\\x.dds";
  entry.texture = {4, 4, 1, 71, 0, 8};
  entry.chunks.push_back(fo4::archive::make_stored_ba2_texture_chunk(bytes_of("data")));
  archive.entries.push_back(std::move(entry));

  EXPECT_THROW(fo4::archive::write_ba2_texture(archive), Ba2Error);

  std::vector<std::byte> header;
  auto u32 = [&](std::uint32_t value) {
    header.push_back(static_cast<std::byte>(value & 0xFFu));
    header.push_back(static_cast<std::byte>((value >> 8u) & 0xFFu));
    header.push_back(static_cast<std::byte>((value >> 16u) & 0xFFu));
    header.push_back(static_cast<std::byte>((value >> 24u) & 0xFFu));
  };
  u32(0x58445442u);
  u32(GetParam());
  u32(0x30315844u);
  u32(0);
  u32(0);
  u32(0);
  EXPECT_THROW(fo4::archive::read_ba2_texture(header), Ba2Error);
}

INSTANTIATE_TEST_SUITE_P(UnprovenVersions, Ba2TextureUnprovenVersion,
                         testing::Values(0u, 2u, 3u, 9u, 0xFFFFFFFFu));

TEST(Ba2Texture, RejectsGeneralArchiveContent) {
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
  u32(0x58445442u);
  u32(1);
  u32(0x4C524E47u);
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
  ASSERT_GE(serialized.size(), 72u);
  serialized[68] = std::byte{0x00};
  serialized[69] = std::byte{0x00};
  serialized[70] = std::byte{0x00};
  serialized[71] = std::byte{0x00};
  EXPECT_THROW(fo4::archive::read_ba2_texture(serialized), Ba2Error);
}

TEST(Ba2Texture, ExtractRejectsCompressedChunkBeyondZlibBound) {
  Ba2TextureEntry entry;
  entry.path = "textures\\x.dds";
  Ba2TextureChunk chunk;
  chunk.data = repeated(8, 0);
  chunk.decompressed_size = 1u << 30;
  chunk.compressed = true;
  entry.chunks.push_back(std::move(chunk));
  try {
    static_cast<void>(fo4::archive::extract_ba2_texture_entry(entry));
    FAIL() << "expected 1 GiB claim from 8 packed bytes to fail";
  } catch (const Ba2Error& error) {
    EXPECT_NE(std::string(error.what()).find("expansion bound"), std::string::npos);
  }
}
