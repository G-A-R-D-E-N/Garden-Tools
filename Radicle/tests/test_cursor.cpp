#include "fo4/binary/checked.hpp"
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

TEST(CheckedReaders, ReadLittleEndianAtOffset) {
  const std::vector<std::byte> data{
      std::byte{0xFF}, std::byte{0x78}, std::byte{0x56}, std::byte{0x34}, std::byte{0x12},
      std::byte{0xFE}, std::byte{0xFF},
  };
  EXPECT_EQ(fo4::binary::checked::read_u32(data, 1), 0x12345678u);
  EXPECT_EQ(fo4::binary::checked::read_u16(data, 1), 0x5678u);
  EXPECT_EQ(fo4::binary::checked::read_i16(data, 5), -2);
  EXPECT_EQ(fo4::binary::checked::read_u16(data), 0x78FFu);
}

TEST(CheckedReaders, ShortBufferOrOffsetThrowsParseError) {
  const std::vector<std::byte> data(3, std::byte{0});
  EXPECT_THROW(fo4::binary::checked::read_u32(data), fo4::parse_error);
  EXPECT_THROW(fo4::binary::checked::read_u16(data, 2), fo4::parse_error);
  EXPECT_THROW(fo4::binary::checked::read_i32(data, 3), fo4::parse_error);
  EXPECT_THROW(fo4::binary::checked::read_f32(data, 4), fo4::parse_error);
  EXPECT_THROW(fo4::binary::checked::read_u16({}, 0), fo4::parse_error);
  EXPECT_NO_THROW(fo4::binary::checked::read_u16(data, 1));
}

TEST(CheckedReaders, ReadFloatAndSignedAtOffsetAndExactFit) {
  const std::vector<std::byte> data{
      std::byte{0xAA}, std::byte{0x00}, std::byte{0x00}, std::byte{0xC0}, std::byte{0x3F},
      std::byte{0xFE}, std::byte{0xFF}, std::byte{0xFF}, std::byte{0xFF},
  };
  EXPECT_EQ(fo4::binary::checked::read_f32(data, 1), 1.5f);
  EXPECT_EQ(fo4::binary::checked::read_i32(data, 5), -2);
  EXPECT_THROW(fo4::binary::checked::read_f32(data, 6), fo4::parse_error);
}
