#include "fo4/archive/ba2.hpp"
#include "plugin_bytes.hpp"

#include <gtest/gtest.h>

#include <cstdint>
#include <limits>
#include <string>

namespace {

constexpr std::uint32_t k_magic = 0x58445442u;  // BTDX
constexpr std::uint32_t k_gnrl = 0x4C524E47u;   // GNRL
constexpr std::uint32_t k_chunk_sentinel = 0xBAADF00Du;

void u64(fo4::test::ByteWriter& out, std::uint64_t value) {
  out.u32(static_cast<std::uint32_t>(value));
  out.u32(static_cast<std::uint32_t>(value >> 32));
}

fo4::test::ByteWriter header(std::uint32_t file_count,
                             std::uint64_t string_table_offset) {
  fo4::test::ByteWriter out;
  out.u32(k_magic);
  out.u32(1);
  out.u32(k_gnrl);
  out.u32(file_count);
  u64(out, string_table_offset);
  return out;
}

void entry_header(fo4::test::ByteWriter& out, std::uint8_t chunk_count) {
  out.u32(0);  // name hash
  out.u32(0);  // extension hash
  out.u32(0);  // directory hash
  out.u8(0);   // unknown
  out.u8(chunk_count);
  out.u16(0x10);
}

}

TEST(Ba2Hardening, ForgedHugeFileCountFailsBeforeEntryAllocation) {
  const auto out = header(std::numeric_limits<std::uint32_t>::max(), 0);
  EXPECT_THROW(
      static_cast<void>(fo4::archive::read_ba2_general(out.bytes)),
      fo4::archive::Ba2Error);
}

TEST(Ba2Hardening, TruncatedChunkTableUsesBa2ErrorContract) {
  auto out = header(1, 0);
  entry_header(out, 255);
  EXPECT_THROW(
      static_cast<void>(fo4::archive::read_ba2_general(out.bytes)),
      fo4::archive::Ba2Error);
}

TEST(Ba2Hardening, TruncatedStringTableUsesBa2ErrorContract) {
  auto out = header(1, 40);
  entry_header(out, 0);
  ASSERT_EQ(out.bytes.size(), 40u);

  try {
    static_cast<void>(fo4::archive::read_ba2_general(out.bytes));
    FAIL() << "expected truncated BA2 string table to fail";
  } catch (const fo4::archive::Ba2Error& error) {
    EXPECT_NE(std::string(error.what()).find("BA2 parse error"), std::string::npos);
  } catch (...) {
    FAIL() << "truncated BA2 escaped the Ba2Error API contract";
  }
}

TEST(Ba2Hardening, CompressedChunkBeyondZlibBoundFailsBeforeAllocation) {
  auto out = header(1, 0);
  entry_header(out, 1);
  u64(out, 60);
  out.u32(8);
  out.u32(1u << 30);
  out.u32(k_chunk_sentinel);
  out.u32(0);
  out.u32(0);
  const auto archive = fo4::archive::read_ba2_general(out.bytes);
  try {
    static_cast<void>(fo4::archive::extract_ba2_general_entry(archive.entries.at(0)));
    FAIL() << "expected 1 GiB claim from 8 packed bytes to fail";
  } catch (const fo4::archive::Ba2Error& error) {
    EXPECT_NE(std::string(error.what()).find("expansion bound"), std::string::npos);
  }
}
