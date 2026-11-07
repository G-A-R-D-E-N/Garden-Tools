#include "fo4/binary/cursor.hpp"
#include "fo4/error.hpp"

#include <gtest/gtest.h>

TEST(Cursor, ReadsLittleEndianIntegers) {
  const std::vector<std::byte> data{
      std::byte{0x78}, std::byte{0x56}, std::byte{0x34}, std::byte{0x12},
      std::byte{0x54}, std::byte{0x45}, std::byte{0x53}, std::byte{0x34},
  };
  fo4::binary::Cursor cursor{data};
  EXPECT_EQ(cursor.read_u32(), 0x12345678u);
  EXPECT_EQ(cursor.read_signature().to_string(), "TES4");
}

TEST(Cursor, ThrowsOnTruncation) {
  const std::vector<std::byte> data{std::byte{0x01}};
  fo4::binary::Cursor cursor{data};
  EXPECT_THROW(cursor.read_u32(), fo4::parse_error);
}
