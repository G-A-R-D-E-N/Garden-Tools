#include "ba2_fixture.hpp"
#include "fo4/archive/ba2_hash.hpp"
#include "fo4/archive/ba2_reader.hpp"
#include "fo4/archive/ba2_texture.hpp"
#include "fo4/c_api_env.h"
#include "fo4/texture/dds.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace {

using ba2_fixture::Ba2ReaderTest;
using ba2_fixture::bytes_of;
using ba2_fixture::general;
using ba2_fixture::text_of;

using fo4::archive::Ba2GeneralArchive;
using fo4::archive::Ba2Reader;

}  // namespace

TEST_F(Ba2ReaderTest, CApiWritesGeneralArchivesThatReadBack) {
  const std::string big(4096, 'm');
  const radicle_ba2_file files[] = {
      {"Meshes\\Gun.nif", reinterpret_cast<const uint8_t*>("gun"), 3},
      {"Materials\\Gun.bgsm", reinterpret_cast<const uint8_t*>(big.data()), big.size()},
      {"Empty.txt", nullptr, 0}};
  uint8_t* bytes = nullptr;
  size_t size = 0;
  ASSERT_EQ(radicle_ba2_write_general(files, 3, 1, 1, &bytes, &size), RADICLE_STATUS_OK);
  const std::vector<std::byte> written(reinterpret_cast<const std::byte*>(bytes),
                                       reinterpret_cast<const std::byte*>(bytes) + size);
  radicle_free_bytes(bytes);

  const auto path = write_file(data() / "Written.ba2", written);
  const auto reader = Ba2Reader::open(path);
  ASSERT_EQ(reader.entries().size(), 3u);
  EXPECT_EQ(text_of(reader.extract(*reader.find("meshes/gun.nif"))), "gun");
  EXPECT_EQ(reader.extract(*reader.find("materials/gun.bgsm")).size(), 4096u);
  EXPECT_EQ(reader.entries()[1].name_hash, fo4::archive::compute_ba2_hashes("Materials\\Gun.bgsm").name);

  ASSERT_EQ(radicle_ba2_write_general(files, 3, 8, 0, &bytes, &size), RADICLE_STATUS_OK);
  radicle_free_bytes(bytes);
  EXPECT_EQ(radicle_ba2_write_general(files, 3, 5, 0, &bytes, &size), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(bytes, nullptr);
  const radicle_ba2_file no_path[] = {{nullptr, nullptr, 0}};
  EXPECT_EQ(radicle_ba2_write_general(no_path, 1, 1, 0, &bytes, &size), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_ba2_write_general(nullptr, 1, 1, 0, &bytes, &size), RADICLE_STATUS_INVALID_ARGUMENT);
}

TEST_F(Ba2ReaderTest, CApiWritesPinnedBytesForOneGeneralEntry) {
  const char payload[] = "x";
  const radicle_ba2_file files[] = {{"a.txt", reinterpret_cast<const uint8_t*>(payload), 1}};
  uint8_t* bytes = nullptr;
  size_t size = 0;
  ASSERT_EQ(radicle_ba2_write_general(files, 1, 1, 0, &bytes, &size), RADICLE_STATUS_OK);
  const std::vector<uint8_t> got(bytes, bytes + size);
  const std::vector<std::byte> written(reinterpret_cast<const std::byte*>(bytes),
                                       reinterpret_cast<const std::byte*>(bytes) + size);
  radicle_free_bytes(bytes);

  const std::vector<uint8_t> expected = {
      0x42, 0x54, 0x44, 0x58, 0x01, 0x00, 0x00, 0x00, 0x47, 0x4E, 0x52, 0x4C,
      0x01, 0x00, 0x00, 0x00, 0x3D, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
      0xCE, 0x51, 0xB5, 0x3A, 0x74, 0x78, 0x74, 0x00, 0x00, 0x00, 0x00, 0x00,
      0x00, 0x01, 0x10, 0x00, 0x3C, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
      0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x0D, 0xF0, 0xAD, 0xBA,
      0x78, 0x05, 0x00, 0x61, 0x2E, 0x74, 0x78, 0x74,
  };
  EXPECT_EQ(got, expected);

  const auto reader = Ba2Reader::open(write_file(data() / "Pinned.ba2", written));
  ASSERT_EQ(reader.entries().size(), 1u);
  EXPECT_EQ(reader.entries()[0].name_hash, 0x3AB551CEu);
  EXPECT_EQ(text_of(reader.extract(*reader.find("a.txt"))), "x");
}

TEST_F(Ba2ReaderTest, CApiWritesVersionSevenGeneralArchives) {
  const char payload[] = "x";
  const radicle_ba2_file files[] = {{"a.txt", reinterpret_cast<const uint8_t*>(payload), 1}};
  uint8_t* bytes = nullptr;
  size_t size = 0;
  ASSERT_EQ(radicle_ba2_write_general(files, 1, 7, 0, &bytes, &size), RADICLE_STATUS_OK);
  const std::vector<std::byte> written(reinterpret_cast<const std::byte*>(bytes),
                                       reinterpret_cast<const std::byte*>(bytes) + size);
  radicle_free_bytes(bytes);

  const auto reader = Ba2Reader::open(write_file(data() / "Seven.ba2", written));
  EXPECT_EQ(reader.version(), 7u);
  EXPECT_EQ(text_of(reader.extract(*reader.find("a.txt"))), "x");
}

TEST_F(Ba2ReaderTest, CApiWritesHeaderOnlyArchiveForZeroEntries) {
  uint8_t* bytes = nullptr;
  size_t size = 0;
  ASSERT_EQ(radicle_ba2_write_general(nullptr, 0, 1, 0, &bytes, &size), RADICLE_STATUS_OK);
  const std::vector<uint8_t> got(bytes, bytes + size);
  const std::vector<std::byte> written(reinterpret_cast<const std::byte*>(bytes),
                                       reinterpret_cast<const std::byte*>(bytes) + size);
  radicle_free_bytes(bytes);

  const std::vector<uint8_t> expected = {0x42, 0x54, 0x44, 0x58, 0x01, 0x00, 0x00, 0x00,
                                         0x47, 0x4E, 0x52, 0x4C, 0x00, 0x00, 0x00, 0x00,
                                         0x18, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
  EXPECT_EQ(got, expected);

  const auto reader = Ba2Reader::open(write_file(data() / "Empty.ba2", written));
  EXPECT_EQ(reader.version(), 1u);
  EXPECT_TRUE(reader.entries().empty());
}

TEST_F(Ba2ReaderTest, CApiWriteGeneralReportsCallerErrorsAsInvalidArgument) {
  const char payload[] = "x";
  const auto buffer = reinterpret_cast<const uint8_t*>(payload);
  uint8_t* bytes = nullptr;
  size_t size = 0;

  const radicle_ba2_file files[] = {{"Meshes\\Gun.nif", buffer, 1}};
  const auto refuse = [&](const radicle_ba2_file* entries, size_t count, uint32_t version) {
    bytes = reinterpret_cast<uint8_t*>(static_cast<uintptr_t>(1));
    size = 7;
    EXPECT_EQ(radicle_ba2_write_general(entries, count, version, 0, &bytes, &size),
              RADICLE_STATUS_INVALID_ARGUMENT);
    EXPECT_EQ(bytes, nullptr);
    EXPECT_EQ(size, 0u);
  };

  refuse(files, 1, 5);
  EXPECT_STREQ(radicle_last_error(), "unsupported BA2 general version 5; supported GNRL versions are 1, 7 and 8");
  refuse(files, 1, 0);

  const radicle_ba2_file trailing[] = {{"Meshes\\", buffer, 1}};
  const radicle_ba2_file separator[] = {{"\\", buffer, 1},
                                        {"/", buffer, 1},
                                        {"Meshes/", buffer, 1}};
  const radicle_ba2_file non_1252[] = {{"Meshes\\\xC4\x80.nif", buffer, 1},
                                       {"Meshes\\\xFF.nif", buffer, 1}};
  refuse(trailing, 1, 1);
  for (const auto& entry : separator) refuse(&entry, 1, 1);
  for (const auto& entry : non_1252) refuse(&entry, 1, 1);
  const radicle_ba2_file dot_paths[] = {{".", buffer, 1}, {"\\.", buffer, 1}};
  for (const auto& entry : dot_paths) refuse(&entry, 1, 1);

  // Constraint: the 259-byte fixture must carry a directory so both hashes stay non-zero.
  const std::string at_hash_limit(260, 'a');
  const std::string under_hash_limit = "Meshes\\" + std::string(252, 'a');
  ASSERT_EQ(under_hash_limit.size(), 259u);
  const radicle_ba2_file too_long[] = {{at_hash_limit.c_str(), buffer, 1}};
  const radicle_ba2_file longest[] = {{under_hash_limit.c_str(), buffer, 1}};
  refuse(too_long, 1, 1);
  ASSERT_EQ(radicle_ba2_write_general(longest, 1, 1, 0, &bytes, &size), RADICLE_STATUS_OK);
  const std::vector<std::byte> written(reinterpret_cast<const std::byte*>(bytes),
                                       reinterpret_cast<const std::byte*>(bytes) + size);
  radicle_free_bytes(bytes);
  bytes = nullptr;
  size = 0;

  const auto hashes = fo4::archive::compute_ba2_hashes(under_hash_limit);
  EXPECT_NE(hashes.name, 0u);
  EXPECT_NE(hashes.directory, 0u);
  const auto longest_reader = Ba2Reader::open(write_file(data() / "Longest.ba2", written));
  ASSERT_EQ(longest_reader.entries().size(), 1u);
  EXPECT_EQ(longest_reader.entries()[0].name_hash, hashes.name);
  EXPECT_EQ(longest_reader.entries()[0].directory_hash, hashes.directory);

#if SIZE_MAX > 0xFFFFFFFFu
  const radicle_ba2_file oversized[] = {{"Meshes\\Gun.nif", buffer, size_t{1} << 32}};
  refuse(oversized, 1, 1);
  refuse(files, size_t{1} << 32, 1);
#endif

  ASSERT_EQ(radicle_ba2_write_general(files, 1, 1, 0, &bytes, &size), RADICLE_STATUS_OK);
  EXPECT_NE(bytes, nullptr);
  radicle_free_bytes(bytes);
}

TEST_F(Ba2ReaderTest, CApiWrittenNonAsciiPathsHashTheirWindows1252Bytes) {
  const char payload[] = "x";
  const radicle_ba2_file files[] = {{"Meshes\\Caf\xC3\xA9.nif", reinterpret_cast<const uint8_t*>(payload), 1}};
  uint8_t* bytes = nullptr;
  size_t size = 0;
  ASSERT_EQ(radicle_ba2_write_general(files, 1, 1, 0, &bytes, &size), RADICLE_STATUS_OK);
  const std::vector<std::byte> written(reinterpret_cast<const std::byte*>(bytes),
                                       reinterpret_cast<const std::byte*>(bytes) + size);
  radicle_free_bytes(bytes);

  const auto reader = Ba2Reader::open(write_file(data() / "Cafe.ba2", written));
  EXPECT_EQ(text_of(reader.extract(*reader.find("meshes/caf\xC3\xA9.nif"))), "x");
  EXPECT_EQ(reader.entries()[0].name_hash, 0x8AF76F07u);
  EXPECT_EQ(reader.entries()[0].directory_hash, 0x0B70CE37u);
  EXPECT_EQ(reader.entries()[0].extension_hash, 0x0066696Eu);
  EXPECT_EQ(reader.entries()[0].name_hash, fo4::archive::compute_ba2_hashes("Meshes\\Caf\xE9.nif").name);
  EXPECT_NE(reader.entries()[0].name_hash, fo4::archive::compute_ba2_hashes("Meshes\\Caf\xC3\xA9.nif").name);
}

TEST_F(Ba2ReaderTest, CApiWritesTheCppGeneralBytesAndReadsThemBack) {
  const std::string mesh = "high";
  const std::string material(4096, 'm');
  const radicle_ba2_file files[] = {
      {"Meshes\\Shared.nif", reinterpret_cast<const uint8_t*>(mesh.data()), mesh.size()},
      {"Materials\\Gun.bgsm", reinterpret_cast<const uint8_t*>(material.data()), material.size()}};

  Ba2GeneralArchive expected;
  expected.entries = {general("Meshes\\Shared.nif", mesh), general("Materials\\Gun.bgsm", material)};

  uint8_t* out = nullptr;
  size_t size = 0;
  ASSERT_EQ(radicle_ba2_write_general(files, 2, 1, 0, &out, &size), RADICLE_STATUS_OK);
  const auto* begin = reinterpret_cast<const std::byte*>(out);
  const std::vector<std::byte> written(begin, begin + size);
  radicle_free_bytes(out);
  EXPECT_EQ(written, fo4::archive::write_ba2_general(expected));

  const auto path = write_file(data() / "Abi - Main.ba2", written);
  radicle_ba2* ba2 = nullptr;
  ASSERT_EQ(radicle_ba2_open(path.string().c_str(), &ba2), RADICLE_STATUS_OK);
  radicle_ba2_info info{};
  ASSERT_EQ(radicle_ba2_get_info(ba2, &info), RADICLE_STATUS_OK);
  EXPECT_EQ(info.kind, RADICLE_BA2_GENERAL);
  EXPECT_EQ(info.version, 1u);
  EXPECT_EQ(info.has_name_table, 1);
  EXPECT_EQ(info.entry_count, 2u);
  radicle_ba2_entry_info entry{};
  ASSERT_EQ(radicle_ba2_get_entry(ba2, 1, &entry), RADICLE_STATUS_OK);
  EXPECT_STREQ(entry.path, "Materials\\Gun.bgsm");
  EXPECT_EQ(entry.unpacked_size, 4096u);
  EXPECT_EQ(entry.name_hash, 0xB5DA1C49u);
  EXPECT_EQ(entry.extension_hash, 0x6D736762u);
  EXPECT_EQ(entry.directory_hash, 0x7D1E021Bu);
  size_t index = 9;
  ASSERT_EQ(radicle_ba2_find(ba2, "materials/GUN.bgsm", &index), RADICLE_STATUS_OK);
  EXPECT_EQ(index, 1u);
  ASSERT_EQ(radicle_ba2_extract(ba2, 0, &out, &size), RADICLE_STATUS_OK);
  EXPECT_EQ(std::string(reinterpret_cast<const char*>(out), size), mesh);
  radicle_free_bytes(out);
  radicle_ba2_close(ba2);

  ASSERT_EQ(radicle_ba2_write_general(files, 2, 1, 1, &out, &size), RADICLE_STATUS_OK);
  const auto* packed_begin = reinterpret_cast<const std::byte*>(out);
  const std::vector<std::byte> packed(packed_begin, packed_begin + size);
  radicle_free_bytes(out);
  Ba2GeneralArchive packed_expected;
  packed_expected.entries = {general("Meshes\\Shared.nif", mesh, true), general("Materials\\Gun.bgsm", material, true)};
  EXPECT_EQ(packed, fo4::archive::write_ba2_general(packed_expected));
  EXPECT_LT(packed.size(), written.size());

  const auto packed_path = write_file(data() / "Abi - Packed.ba2", packed);
  ASSERT_EQ(radicle_ba2_open(packed_path.string().c_str(), &ba2), RADICLE_STATUS_OK);
  ASSERT_EQ(radicle_ba2_extract(ba2, 1, &out, &size), RADICLE_STATUS_OK);
  EXPECT_EQ(std::string(reinterpret_cast<const char*>(out), size), material);
  radicle_free_bytes(out);
  radicle_ba2_close(ba2);
}

TEST_F(Ba2ReaderTest, CApiWriteRefusesBadEntriesAndClearsOutputs) {
  const std::string payload = "x";
  const auto* bytes = reinterpret_cast<const uint8_t*>(payload.data());
  const std::string longest(259, 'a');
  const std::string too_long(260, 'a');
  const radicle_ba2_file longest_ok[] = {{longest.c_str(), bytes, 1}};
  const radicle_ba2_file null_path[] = {{nullptr, bytes, 1}};
  const radicle_ba2_file empty_path[] = {{"", bytes, 1}};
  const radicle_ba2_file too_long_path[] = {{too_long.c_str(), bytes, 1}};
  const radicle_ba2_file null_bytes[] = {{"Meshes\\A.nif", nullptr, 1}};
  const radicle_ba2_file duplicate[] = {{"Meshes\\A.nif", bytes, 1}, {"meshes/a.NIF", bytes, 1}};
  const radicle_ba2_file unmappable[] = {{"Meshes\\\xE4\xB8\xAD.nif", bytes, 1}};
  const radicle_ba2_file backslash[] = {{"\\", bytes, 1}};
  const radicle_ba2_file slashes[] = {{"///", bytes, 1}};
  const radicle_ba2_file trailing_separator[] = {{"Meshes\\", bytes, 1}};

  uint8_t sentinel = 0;
  uint8_t* out = nullptr;
  size_t size = 0;
  const auto refused = [&](const radicle_ba2_file* files, size_t count) {
    out = &sentinel;
    size = 7;
    EXPECT_EQ(radicle_ba2_write_general(files, count, 1, 0, &out, &size), RADICLE_STATUS_INVALID_ARGUMENT);
    EXPECT_EQ(out, nullptr);
    EXPECT_EQ(size, 0u);
  };
  refused(null_path, 1);
  refused(empty_path, 1);
  refused(too_long_path, 1);
  refused(null_bytes, 1);
  refused(duplicate, 2);
  refused(unmappable, 1);
  refused(backslash, 1);
  refused(slashes, 1);
  refused(trailing_separator, 1);
  refused(nullptr, 1);
  out = &sentinel;
  size = 7;
  EXPECT_EQ(radicle_ba2_write_general(longest_ok, 1, 2, 0, &out, &size), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(out, nullptr);
  EXPECT_EQ(size, 0u);
  size = 7;
  EXPECT_EQ(radicle_ba2_write_general(longest_ok, 1, 1, 0, nullptr, &size), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(size, 0u);

  ASSERT_EQ(radicle_ba2_write_general(longest_ok, 1, 1, 0, &out, &size), RADICLE_STATUS_OK);
  EXPECT_GT(size, 0u);
  radicle_free_bytes(out);
}

TEST_F(Ba2ReaderTest, CApiWriteEmptyFileListMatchesTheCppEmptyArchive) {
  uint8_t* out = nullptr;
  size_t size = 0;
  ASSERT_EQ(radicle_ba2_write_general(nullptr, 0, 1, 0, &out, &size), RADICLE_STATUS_OK);
  const auto* begin = reinterpret_cast<const std::byte*>(out);
  const std::vector<std::byte> written(begin, begin + size);
  radicle_free_bytes(out);
  const Ba2GeneralArchive empty{};
  EXPECT_EQ(written, fo4::archive::write_ba2_general(empty));
}

namespace {

std::uint32_t bc_surface_bytes(std::uint32_t width, std::uint32_t height, std::uint32_t block_bytes) {
  return ((width + 3) / 4) * ((height + 3) / 4) * block_bytes;
}

std::vector<std::uint8_t> patterned(std::size_t count, std::uint8_t seed) {
  std::vector<std::uint8_t> out(count);
  for (std::size_t i = 0; i < count; ++i) out[i] = static_cast<std::uint8_t>(seed + i / 64);
  return out;
}

std::vector<std::uint8_t> dx10_dds(std::uint8_t dxgi,
                                   int width,
                                   int height,
                                   int mip_count,
                                   bool cube,
                                   const std::vector<std::uint8_t>& payload) {
  const auto format = fo4::texture::lookup_format(dxgi);
  const fo4::texture::DdsInfo info{width, height, mip_count, cube, cube ? 6 : 1, 1, 148, *format};
  auto bytes = fo4::texture::build_dds_header(info);
  bytes.insert(bytes.end(), payload.begin(), payload.end());
  return bytes;
}

fo4::archive::Ba2TextureArchive write_textures_and_read(const std::vector<radicle_ba2_file>& files,
                                                        uint32_t version,
                                                        int32_t compress) {
  uint8_t* bytes = nullptr;
  size_t size = 0;
  EXPECT_EQ(radicle_ba2_write_textures(files.data(), files.size(), version, compress, &bytes, &size),
            RADICLE_STATUS_OK);
  const auto* begin = reinterpret_cast<const std::byte*>(bytes);
  const std::vector<std::byte> written(begin, begin + size);
  radicle_free_bytes(bytes);
  return fo4::archive::read_ba2_texture(written);
}

std::pair<std::uint16_t, std::uint16_t> mips_of(const fo4::archive::Ba2TextureChunk& chunk) {
  return {chunk.mip_first, chunk.mip_last};
}

}  // namespace

TEST(Ba2TextureWrite, CApiWritesDx10ArchivesWithMipChunksThatExtractToTheSourcePayload) {
  std::uint32_t chain = 0;
  for (std::uint32_t mip = 0; mip < 11; ++mip) chain += bc_surface_bytes(1024u >> mip, 1024u >> mip, 8);
  const auto payload = patterned(chain, 3);
  const auto dds = dx10_dds(71, 1024, 1024, 11, false, payload);
  const auto small = dx10_dds(71, 4, 4, 1, false, patterned(8, 9));

  for (const int32_t compress : {0, 1}) {
    const std::vector<radicle_ba2_file> files = {{"Textures\\Big_d.dds", dds.data(), dds.size()},
                                                 {"textures/Small_d.DDS", small.data(), small.size()}};
    const auto archive = write_textures_and_read(files, 8, compress);
    ASSERT_EQ(archive.version, 8u);
    ASSERT_EQ(archive.entries.size(), 2u);

    const auto& big = archive.entries[0];
    const auto hashes = fo4::archive::compute_ba2_hashes("Textures\\Big_d.dds");
    EXPECT_EQ(big.name_hash, hashes.name);
    EXPECT_EQ(big.directory_hash, hashes.directory);
    EXPECT_EQ(big.texture.width, 1024u);
    EXPECT_EQ(big.texture.height, 1024u);
    EXPECT_EQ(big.texture.mip_count, 11u);
    EXPECT_EQ(big.texture.dxgi_format, 71u);
    EXPECT_EQ(big.texture.flags, 0u);
    EXPECT_EQ(big.texture.tile_mode, 8u);
    ASSERT_EQ(big.chunks.size(), 3u);
    EXPECT_EQ(mips_of(big.chunks[0]), std::make_pair(std::uint16_t{0}, std::uint16_t{0}));
    EXPECT_EQ(mips_of(big.chunks[1]), std::make_pair(std::uint16_t{1}, std::uint16_t{1}));
    EXPECT_EQ(mips_of(big.chunks[2]), std::make_pair(std::uint16_t{2}, std::uint16_t{10}));
    EXPECT_EQ(big.chunks[0].decompressed_size, bc_surface_bytes(1024, 1024, 8));
    EXPECT_EQ(big.chunks[0].compressed, compress != 0);

    const auto extracted = fo4::archive::extract_ba2_texture_entry(big);
    ASSERT_EQ(extracted.size(), 128u + payload.size());
    EXPECT_TRUE(std::equal(payload.begin(), payload.end(),
                           reinterpret_cast<const std::uint8_t*>(extracted.data()) + 128));

    const auto& tiny = archive.entries[1];
    ASSERT_EQ(tiny.chunks.size(), 1u);
    EXPECT_EQ(tiny.chunks[0].decompressed_size, 8u);
    EXPECT_EQ(tiny.texture.mip_count, 1u);
  }
}

TEST(Ba2TextureWrite, CApiKeepsEveryCubemapFaceInOneChunk) {
  std::uint32_t chain = 0;
  for (std::uint32_t mip = 0; mip < 3; ++mip) chain += bc_surface_bytes(8u >> mip, 8u >> mip, 16);
  const auto payload = patterned(static_cast<std::size_t>(chain) * 6, 5);
  const auto dds = dx10_dds(77, 8, 8, 3, true, payload);

  const std::vector<radicle_ba2_file> files = {{"Textures\\Sky_e.dds", dds.data(), dds.size()}};
  const auto archive = write_textures_and_read(files, 1, 0);
  ASSERT_EQ(archive.entries.size(), 1u);
  const auto& cube = archive.entries[0];
  EXPECT_EQ(cube.texture.flags, 1u);
  ASSERT_EQ(cube.chunks.size(), 1u);
  EXPECT_EQ(mips_of(cube.chunks[0]), std::make_pair(std::uint16_t{0}, std::uint16_t{2}));
  EXPECT_EQ(cube.chunks[0].decompressed_size, payload.size());
  EXPECT_EQ(cube.chunks[0].data.size(), payload.size());
}

TEST(Ba2TextureWrite, CApiRefusesInputsThatCannotBecomeDx10Entries) {
  const auto good = dx10_dds(71, 8, 8, 1, false, patterned(32, 1));
  const auto truncated = dx10_dds(71, 8, 8, 1, false, patterned(31, 1));
  auto padded = dx10_dds(28, 4, 4, 1, false, patterned(80, 1));
  padded[8] = 0x0F;
  padded[20] = 20;
  std::vector<std::uint8_t> rgb24(128 + 48, 0);
  const auto put = [&](std::size_t at, std::uint32_t value) {
    for (int i = 0; i < 4; ++i) rgb24[at + i] = static_cast<std::uint8_t>(value >> (8 * i));
  };
  put(0, 0x20534444u);
  put(4, 124u);
  put(12, 4);
  put(16, 4);
  put(28, 1);
  put(80, 0x40u);
  put(88, 24u);
  put(92, 0xFF0000u);
  put(96, 0xFF00u);
  put(100, 0xFFu);

  uint8_t* bytes = nullptr;
  size_t size = 0;
  const auto write_one = [&](const char* path, const std::vector<std::uint8_t>& data, uint32_t version) {
    const radicle_ba2_file file{path, data.data(), data.size()};
    return radicle_ba2_write_textures(&file, 1, version, 0, &bytes, &size);
  };
  ASSERT_EQ(write_one("Textures\\a.dds", good, 1), RADICLE_STATUS_OK);
  radicle_free_bytes(bytes);
  EXPECT_EQ(write_one("Textures\\a.png", good, 1), RADICLE_STATUS_PARSE_ERROR);
  EXPECT_EQ(write_one("Textures\\a.dds", truncated, 1), RADICLE_STATUS_PARSE_ERROR);
  EXPECT_EQ(write_one("Textures\\a.dds", padded, 1), RADICLE_STATUS_PARSE_ERROR);
  EXPECT_EQ(write_one("Textures\\a.dds", rgb24, 1), RADICLE_STATUS_PARSE_ERROR);
  EXPECT_EQ(write_one("Textures\\a.dds", good, 5), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(bytes, nullptr);

  const radicle_ba2_file twice[] = {{"Textures\\a.dds", good.data(), good.size()},
                                    {"textures/A.DDS", good.data(), good.size()}};
  EXPECT_EQ(radicle_ba2_write_textures(twice, 2, 1, 0, &bytes, &size), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_ba2_write_textures(nullptr, 1, 1, 0, &bytes, &size), RADICLE_STATUS_INVALID_ARGUMENT);
}
