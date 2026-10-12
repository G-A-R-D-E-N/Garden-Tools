#include "fo4/archive/ba2.hpp"
#include "plugin_bytes.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace {

std::vector<std::byte> bytes(std::string_view text) {
  std::vector<std::byte> out;
  out.reserve(text.size());
  for (const char ch : text) {
    out.push_back(static_cast<std::byte>(static_cast<unsigned char>(ch)));
  }
  return out;
}

void u64(fo4::test::ByteWriter& out, std::uint64_t value) {
  out.u32(static_cast<std::uint32_t>(value & 0xFFFFFFFFull));
  out.u32(static_cast<std::uint32_t>(value >> 32u));
}

fo4::test::ByteWriter header(std::uint32_t version, std::uint32_t tag,
                             std::uint32_t count, std::uint64_t strings) {
  fo4::test::ByteWriter out;
  out.u32(0x58445442u);  // BTDX
  out.u32(version);
  out.u32(tag);
  out.u32(count);
  u64(out, strings);
  return out;
}

}  // namespace

TEST(Ba2General, RoundTripsStoredCompressedAndMultiChunkEntries) {
  const auto first = bytes("mesh payload");
  std::string repeated(4096, 'A');
  repeated.replace(1000, 16, "compressed-data!");
  const auto second = bytes(repeated);
  const auto third_a = bytes("first chunk/");
  const auto third_b = bytes("second chunk");

  fo4::archive::Ba2GeneralArchive archive;
  archive.version = 1;
  archive.has_string_table = true;
  archive.entries = {
      {0x11111111u, 0x22222222u, 0x33333333u, 0x7Fu,
       "Meshes\\Weapons\\One.nif",
       {fo4::archive::make_stored_ba2_chunk(first)}},
      {0x44444444u, 0x55555555u, 0x66666666u, 0x12u,
       "Scripts\\Compressed.pex",
       {fo4::archive::make_compressed_ba2_chunk(second)}},
      {0x77777777u, 0x88888888u, 0x99999999u, 0x34u,
       "Interface\\Combined.bin",
       {fo4::archive::make_stored_ba2_chunk(third_a),
        fo4::archive::make_compressed_ba2_chunk(third_b)}}};

  ASSERT_TRUE(archive.entries[1].chunks[0].compressed);

  const auto encoded = fo4::archive::write_ba2_general(archive);
  const auto reread = fo4::archive::read_ba2_general(encoded);

  ASSERT_EQ(reread.version, 1u);
  ASSERT_TRUE(reread.has_string_table);
  ASSERT_EQ(reread.entries.size(), 3u);

  EXPECT_EQ(reread.entries[0].name_hash, 0x11111111u);
  EXPECT_EQ(reread.entries[0].extension_hash, 0x22222222u);
  EXPECT_EQ(reread.entries[0].directory_hash, 0x33333333u);
  EXPECT_EQ(reread.entries[0].unknown, 0x7Fu);
  EXPECT_EQ(reread.entries[0].path, "Meshes\\Weapons\\One.nif");
  EXPECT_EQ(fo4::archive::extract_ba2_general_entry(reread.entries[0]), first);

  EXPECT_TRUE(reread.entries[1].chunks[0].compressed);
  EXPECT_EQ(fo4::archive::extract_ba2_general_entry(reread.entries[1]), second);

  auto combined = third_a;
  combined.insert(combined.end(), third_b.begin(), third_b.end());
  EXPECT_EQ(reread.entries[2].chunks.size(), 2u);
  EXPECT_EQ(fo4::archive::extract_ba2_general_entry(reread.entries[2]), combined);

  const auto* found = fo4::archive::find_ba2_general_entry(
      reread, "meshes/weapons/ONE.NIF");
  ASSERT_NE(found, nullptr);
  EXPECT_EQ(found->name_hash, 0x11111111u);
}

TEST(Ba2General, StoredChunkSizeMismatchIsRejectedOnExtraction) {
  fo4::archive::Ba2GeneralEntry entry;
  entry.path = "bad.bin";
  entry.chunks.push_back({bytes("three"), 99u, false});
  EXPECT_THROW(fo4::archive::extract_ba2_general_entry(entry), fo4::archive::Ba2Error);
}

TEST(Ba2General, ArchiveWithoutStringTablePreservesHashesAndLeavesPathsUnknown) {
  fo4::archive::Ba2GeneralArchive archive;
  archive.has_string_table = false;
  archive.entries = {
      {1u, 2u, 3u, 4u, "not-serialized.txt",
       {fo4::archive::make_stored_ba2_chunk(bytes("payload"))}}};

  const auto reread = fo4::archive::read_ba2_general(
      fo4::archive::write_ba2_general(archive));
  ASSERT_EQ(reread.entries.size(), 1u);
  EXPECT_FALSE(reread.has_string_table);
  EXPECT_TRUE(reread.entries[0].path.empty());
  EXPECT_EQ(reread.entries[0].name_hash, 1u);
  EXPECT_EQ(reread.entries[0].extension_hash, 2u);
  EXPECT_EQ(reread.entries[0].directory_hash, 3u);
  EXPECT_EQ(reread.entries[0].unknown, 4u);
  EXPECT_EQ(fo4::archive::extract_ba2_general_entry(reread.entries[0]), bytes("payload"));
}

TEST(Ba2General, RoundTripsNextGenVersions7And8) {
  const std::string payload(2048, 'B');
  for (const std::uint32_t version : {7u, 8u}) {
    fo4::archive::Ba2GeneralArchive archive;
    archive.version = version;
    archive.entries = {
        {0x11111111u, 0x22222222u, 0x33333333u, 0x7Fu,
         "Meshes\\Weapons\\NextGen.nif",
         {fo4::archive::make_compressed_ba2_chunk(bytes(payload))}},
    };

    const auto reread = fo4::archive::read_ba2_general(
        fo4::archive::write_ba2_general(archive));
    EXPECT_EQ(reread.version, version);
    ASSERT_EQ(reread.entries.size(), 1u);
    EXPECT_EQ(reread.entries[0].path, "Meshes\\Weapons\\NextGen.nif");
    ASSERT_TRUE(reread.entries[0].chunks[0].compressed);
    EXPECT_EQ(reread.entries[0].chunks[0].decompressed_size, 2048u);
    EXPECT_EQ(fo4::archive::extract_ba2_general_entry(reread.entries[0]), bytes(payload));
  }
}

class Ba2GeneralUnprovenVersion : public testing::TestWithParam<std::uint32_t> {};

TEST_P(Ba2GeneralUnprovenVersion, RejectsOnReadAndWrite) {
  auto unproven = header(GetParam(), 0x4C524E47u, 0u, 0u);
  EXPECT_THROW(fo4::archive::read_ba2_general(unproven.bytes), fo4::archive::Ba2Error);

  fo4::archive::Ba2GeneralArchive archive;
  archive.version = GetParam();
  EXPECT_THROW(fo4::archive::write_ba2_general(archive), fo4::archive::Ba2Error);
}

INSTANTIATE_TEST_SUITE_P(UnprovenVersions, Ba2GeneralUnprovenVersion,
                         testing::Values(0u, 2u, 3u, 9u, 0xFFFFFFFFu));

TEST(Ba2General, RejectsDx10TagInGeneralReader) {
  auto dx10 = header(1u, 0x30315844u, 0u, 0u);
  EXPECT_THROW(fo4::archive::read_ba2_general(dx10.bytes), fo4::archive::Ba2Error);
}

TEST(Ba2General, RejectsBadChunkSentinelBeforeReadingPayload) {
  constexpr std::uint64_t data_offset = 24u + 16u + 20u;
  constexpr std::uint64_t strings_offset = data_offset + 3u;
  auto out = header(1u, 0x4C524E47u, 1u, strings_offset);

  out.u32(0x10u);
  out.u32(0x20u);
  out.u32(0x30u);
  out.u8(0);
  out.u8(1);
  out.u16(0x10u);
  u64(out, data_offset);
  out.u32(0u);
  out.u32(3u);
  out.u32(0xDEADBEEFu);
  out.raw("abc");
  out.u16(5u);
  out.raw("a.bin");

  EXPECT_THROW(fo4::archive::read_ba2_general(out.bytes), fo4::archive::Ba2Error);
}

TEST(Ba2General, RejectsChunkRangesOutsideArchive) {
  constexpr std::uint64_t data_offset = 0x1000u;
  auto out = header(1u, 0x4C524E47u, 1u, 0u);

  out.u32(1u);
  out.u32(2u);
  out.u32(3u);
  out.u8(0);
  out.u8(1);
  out.u16(0x10u);
  u64(out, data_offset);
  out.u32(0u);
  out.u32(4u);
  out.u32(0xBAADF00Du);

  EXPECT_THROW(fo4::archive::read_ba2_general(out.bytes), fo4::archive::Ba2Error);
}
