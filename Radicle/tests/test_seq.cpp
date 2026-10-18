#include "fo4/error.hpp"
#include "fo4/seq/seq.hpp"

#include <gtest/gtest.h>

#include <cstdint>
#include <vector>

using fo4::RawFormId;
using fo4::parse_error;
using fo4::seq::SeqFile;

namespace {

std::vector<std::uint8_t> bytes(const std::vector<std::uint32_t>& values) {
  std::vector<std::uint8_t> out;
  for (const auto value : values) {
    out.push_back(static_cast<std::uint8_t>(value & 0xFFu));
    out.push_back(static_cast<std::uint8_t>((value >> 8) & 0xFFu));
    out.push_back(static_cast<std::uint8_t>((value >> 16) & 0xFFu));
    out.push_back(static_cast<std::uint8_t>((value >> 24) & 0xFFu));
  }
  return out;
}

}

TEST(Seq, EntriesRoundTripInFileOrder) {
  const std::vector<std::vector<std::uint32_t>> rows{
      {0x07000800u},
      {0x07000800u, 0x07000802u, 0x07000803u},
      {0x00000010u, 0x0000000Fu, 0x00000001u},
      {0x07000800u, 0x07000800u},
      {0x00000000u},
  };
  for (const auto& values : rows) {
    SCOPED_TRACE(values.front());
    const auto input = bytes(values);
    const SeqFile file = SeqFile::fromBytes(input);
    ASSERT_EQ(file.entries.size(), values.size());
    for (std::size_t i = 0; i < values.size(); ++i) {
      EXPECT_EQ(file.entries[i].value(), values[i]);
      EXPECT_EQ(file.entries[i].local_24(), values[i] & 0x00FFFFFFu);
      EXPECT_EQ(file.entries[i].is_null(), values[i] == 0u);
    }
    EXPECT_EQ(file.toBytes(), input);
  }
}

TEST(Seq, EntriesAreRawRuntimeFormIds) {
  const SeqFile file = SeqFile::fromBytes(bytes({0x01001733u, 0xFE000123u, 0xFF000456u, 0xFFFFFFFFu}));
  ASSERT_EQ(file.entries.size(), 4u);
  EXPECT_EQ(file.entries[0].index_byte(), 1);
  EXPECT_EQ(file.entries[0].local_24(), 0x001733u);
  EXPECT_EQ(file.entries[1].index_byte(), 0xFE);
  EXPECT_TRUE(file.entries[1].is_runtime_light());
  EXPECT_EQ(file.entries[2].index_byte(), 0xFF);
  EXPECT_TRUE(file.entries[2].is_created());
  EXPECT_TRUE(file.entries[3].is_none());
}

TEST(Seq, EmptyInputIsAnEmptyList) {
  const SeqFile file = SeqFile::fromBytes({});
  EXPECT_TRUE(file.entries.empty());
  EXPECT_TRUE(file.toBytes().empty());
}

TEST(Seq, PartialEntryFailsClosed) {
  for (const std::size_t size : {1u, 2u, 3u, 5u, 7u, 255u}) {
    const std::vector<std::uint8_t> input(size, 0x41);
    try {
      static_cast<void>(SeqFile::fromBytes(input));
      FAIL() << "expected a parse error for " << size << " bytes";
    } catch (const parse_error& error) {
      EXPECT_EQ(error.offset(), size);
    }
  }
}
