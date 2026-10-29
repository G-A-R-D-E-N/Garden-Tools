#include "ba2_fixture.hpp"
#include "fo4/archive/ba2_hash.hpp"
#include "fo4/archive/ba2_reader.hpp"
#include "fo4/c_api_env.h"

#include <gtest/gtest.h>

#include <cstdint>
#include <string>
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
