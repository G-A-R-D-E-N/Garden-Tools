#include "fo4/swf/swf.hpp"
#include "swf_movie_builder.hpp"

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <span>
#include <stdexcept>
#include <vector>

namespace {

using fo4::swf::read_swf;
using fo4::swf::SwfCompression;
using fo4::swf::SwfFile;
using fo4::swf::SwfStatus;
using fo4::swf::write_swf;
using swf_test::SwfBuilder;
using swf_test::wrap_body;

constexpr std::size_t kZwsStreamOffset = 12;

std::vector<std::byte> repetitive_body() {
  SwfBuilder builder;
  builder.rect(0, 4000, 0, 3000, 14);
  builder.u16(24u * 256u);
  builder.u16(300);
  for (int index = 0; index < 300; ++index) {
    builder.tag(1);
  }
  return builder.body;
}

SwfFile read_file(std::span<const std::byte> bytes) {
  const auto result = read_swf(bytes);
  EXPECT_EQ(result.status, SwfStatus::ok) << result.message;
  return result.file;
}

std::uint32_t u32_at(const std::vector<std::byte>& bytes, std::size_t offset) {
  std::uint32_t value = 0;
  for (std::size_t index = 0; index < 4; ++index) {
    value |= static_cast<std::uint32_t>(std::to_integer<std::uint8_t>(bytes[offset + index]))
             << (8u * index);
  }
  return value;
}

std::vector<std::byte> zws_with(std::uint8_t properties, std::span<const std::byte> stream) {
  std::vector<std::byte> out;
  for (const char ch : {'Z', 'W', 'S'}) {
    out.push_back(static_cast<std::byte>(ch));
  }
  out.push_back(std::byte{10});
  for (const std::uint32_t value : {13u, 5u + static_cast<std::uint32_t>(stream.size())}) {
    for (unsigned index = 0; index < 4; ++index) {
      out.push_back(static_cast<std::byte>((value >> (8u * index)) & 0xFFu));
    }
  }
  out.push_back(static_cast<std::byte>(properties));
  for (const std::byte value : {std::byte{0}, std::byte{0}, std::byte{0}, std::byte{1}}) {
    out.push_back(value);
  }
  out.insert(out.end(), stream.begin(), stream.end());
  return out;
}

TEST(SwfLzmaTest, WritesZwsHeaderWithHandCheckedFields) {
  const auto fws = wrap_body(repetitive_body(), 10, false, false);
  const auto zws = write_swf(read_file(fws), SwfCompression::lzma);

  ASSERT_GT(zws.size(), kZwsStreamOffset + 5u);
  EXPECT_EQ(std::to_integer<char>(zws[0]), 'Z');
  EXPECT_EQ(std::to_integer<char>(zws[1]), 'W');
  EXPECT_EQ(std::to_integer<char>(zws[2]), 'S');
  EXPECT_EQ(std::to_integer<std::uint8_t>(zws[3]), 10u);
  EXPECT_EQ(u32_at(zws, 4), fws.size());
  EXPECT_EQ(u32_at(zws, 8), zws.size() - kZwsStreamOffset);
  EXPECT_EQ(std::to_integer<std::uint8_t>(zws[12]), 0x5Du);
  EXPECT_EQ(u32_at(zws, 13), 1u << 24);
  EXPECT_EQ(std::to_integer<std::uint8_t>(zws[17]), 0u);
}

TEST(SwfLzmaTest, RoundTripsAMovieThroughZwsByteExactly) {
  const auto fws = wrap_body(repetitive_body(), 10, false, false);
  const auto original = read_file(fws);
  const auto zws = write_swf(original, SwfCompression::lzma);
  EXPECT_LT(zws.size(), fws.size());

  const auto back = read_swf(zws);
  ASSERT_EQ(back.status, SwfStatus::ok) << back.message;
  EXPECT_EQ(back.file.header.compression, SwfCompression::lzma);
  EXPECT_TRUE(back.declared_length_matches);
  EXPECT_EQ(back.file.source_body, original.source_body);
  ASSERT_EQ(back.file.tags.size(), original.tags.size());
  EXPECT_EQ(back.file.tags.size(), 300u);
  EXPECT_EQ(write_swf(back.file), zws);
}

TEST(SwfLzmaTest, EncodesAChangedBodyAsFreshZwsThatReadsBack) {
  const auto fws = wrap_body(repetitive_body(), 10, false, false);
  auto file = read_swf(write_swf(read_file(fws), SwfCompression::lzma)).file;
  file.header.frame_count = 7;
  const auto zws = write_swf(file, SwfCompression::lzma);

  const auto back = read_swf(zws);
  ASSERT_EQ(back.status, SwfStatus::ok) << back.message;
  EXPECT_EQ(back.file.header.frame_count, 7u);
  EXPECT_EQ(back.file.tags.size(), 300u);
}

TEST(SwfLzmaTest, RefusesAStreamWithNoBytesAfterTheProperties) {
  const auto zws = zws_with(0x5D, std::span<const std::byte>{});
  const auto result = read_swf(zws);
  EXPECT_EQ(result.status, SwfStatus::decompress_failed);
  EXPECT_EQ(result.file.header.compression, SwfCompression::lzma);
}

TEST(SwfLzmaTest, RefusesInvalidPropertiesByte) {
  const std::vector<std::byte> stream(5, std::byte{0});
  const auto result = read_swf(zws_with(0xE1, stream));
  EXPECT_EQ(result.status, SwfStatus::decompress_failed);
}

TEST(SwfLzmaTest, RefusesNonZeroLeadingRangeCoderByte) {
  const std::vector<std::byte> stream{std::byte{1}, std::byte{0}, std::byte{0}, std::byte{0},
                                      std::byte{0}};
  const auto result = read_swf(zws_with(0x5D, stream));
  EXPECT_EQ(result.status, SwfStatus::decompress_failed);
}

TEST(SwfLzmaTest, RefusesGfxMovieWithLzmaCompression) {
  SwfBuilder builder;
  builder.rect(0, 0, 0, 0, 0);
  builder.u16(0);
  builder.u16(0);
  const auto gfx = wrap_body(builder.body, 10, true, false);
  const auto file = read_file(gfx);
  EXPECT_THROW(write_swf(file, SwfCompression::lzma), std::runtime_error);
}

}  // namespace
