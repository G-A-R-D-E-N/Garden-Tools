#include "fo4/c_api_dds.h"
#include "fo4/texture/dds.hpp"

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {

void put_u32(std::vector<std::uint8_t>& bytes, std::size_t offset, std::uint32_t value) {
  if (bytes.size() < offset + 4) bytes.resize(offset + 4, 0);
  bytes[offset] = static_cast<std::uint8_t>(value & 0xFFU);
  bytes[offset + 1] = static_cast<std::uint8_t>((value >> 8U) & 0xFFU);
  bytes[offset + 2] = static_cast<std::uint8_t>((value >> 16U) & 0xFFU);
  bytes[offset + 3] = static_cast<std::uint8_t>((value >> 24U) & 0xFFU);
}

std::vector<std::uint8_t> legacy_dds(std::uint32_t four_cc) {
  std::vector<std::uint8_t> bytes(128, 0);
  put_u32(bytes, 0, 0x20534444U);
  put_u32(bytes, 4, 124);
  put_u32(bytes, 12, 4);
  put_u32(bytes, 16, 4);
  put_u32(bytes, 28, 1);
  put_u32(bytes, 80, 0x4U);
  put_u32(bytes, 84, four_cc);
  return bytes;
}

std::vector<std::uint8_t> make_dds(std::uint8_t dxgi,
                                   const std::vector<std::uint8_t>& surface,
                                   int mip_count = 1,
                                   int width = 4,
                                   int height = 4) {
  const auto format = fo4::texture::lookup_format(dxgi);
  if (!format) return {};
  const fo4::texture::DdsInfo info{width, height, mip_count, false, 1, 1, 148, *format};
  auto bytes = fo4::texture::build_dds_header(info);
  bytes.insert(bytes.end(), surface.begin(), surface.end());
  return bytes;
}

template <typename Fn>
void expect_dds_error(Fn&& fn, std::string_view type) {
  try {
    fn();
    ADD_FAILURE() << "expected a dds_error of type " << type;
  } catch (const fo4::texture::dds_error& error) {
    EXPECT_EQ(error.type(), type) << error.what();
  }
}

struct BitWriter {
  std::uint64_t low{};
  std::uint64_t high{};
  int count{};

  void write(int value, int bits) {
    for (int i = 0; i < bits; ++i) {
      const auto bit = (value >> i) & 1;
      if (count < 64) {
        low |= static_cast<std::uint64_t>(bit) << count;
      } else {
        high |= static_cast<std::uint64_t>(bit) << (count - 64);
      }
      ++count;
    }
  }

  std::array<std::uint8_t, 16> bytes() const {
    std::array<std::uint8_t, 16> out{};
    for (int i = 0; i < 8; ++i) {
      out[i] = static_cast<std::uint8_t>((low >> (8 * i)) & 0xFFU);
      out[8 + i] = static_cast<std::uint8_t>((high >> (8 * i)) & 0xFFU);
    }
    return out;
  }
};

void expect_rgba(const std::vector<std::uint8_t>& pixel, std::uint8_t r, std::uint8_t g, std::uint8_t b,
                 std::uint8_t a) {
  ASSERT_EQ(pixel.size(), 4U);
  EXPECT_EQ(pixel[0], r);
  EXPECT_EQ(pixel[1], g);
  EXPECT_EQ(pixel[2], b);
  EXPECT_EQ(pixel[3], a);
}

std::vector<std::uint8_t> pixel_at(const std::vector<std::uint8_t>& rgba, int width, int x, int y) {
  const auto offset = (static_cast<std::size_t>(y) * width + x) * 4;
  return std::vector<std::uint8_t>(rgba.begin() + offset, rgba.begin() + offset + 4);
}

const std::vector<std::uint8_t> k_bc1_white{0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
const std::vector<std::uint8_t> k_bc1_transparent{0x00, 0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
const std::vector<std::uint8_t> k_bc3_opaque_white{255, 0, 0, 0, 0, 0, 0, 0,
                                                   0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};

}

TEST(DdsCodec, ReadsLegacyAndDx10Headers) {
  const auto legacy = fo4::texture::read_dds(legacy_dds(0x31545844U));
  EXPECT_EQ(legacy.width, 4);
  EXPECT_EQ(legacy.height, 4);
  EXPECT_EQ(legacy.mip_count, 1);
  EXPECT_EQ(legacy.data_offset, 128);
  EXPECT_EQ(legacy.dxgi_format(), 71);
  EXPECT_EQ(legacy.payload_size(), 8);

  const auto format = fo4::texture::lookup_format(71);
  ASSERT_TRUE(format.has_value());
  const fo4::texture::DdsInfo info{8, 8, 1, false, 1, 1, 148, *format};
  const auto dx10 = fo4::texture::read_dds(fo4::texture::build_dds_header(info));
  EXPECT_EQ(dx10.width, 8);
  EXPECT_EQ(dx10.height, 8);
  EXPECT_EQ(dx10.data_offset, 148);
  EXPECT_EQ(dx10.dxgi_format(), 71);
  EXPECT_TRUE(dx10.format.is_block_compressed());
  EXPECT_EQ(dx10.payload_size(), 32);
}

TEST(DdsCodec, RejectsJunkAndUnsupportedFormats) {
  const std::vector<std::uint8_t> junk(128, 0);
  expect_dds_error([&] { fo4::texture::read_dds(junk); }, "InvalidDds");
  expect_dds_error([&] { fo4::texture::read_dds(legacy_dds(0x20202020U)); }, "UnsupportedFormat");
}

TEST(DdsCodec, SizesEveryMipLevel) {
  const auto format = fo4::texture::lookup_format(71);
  ASSERT_TRUE(format.has_value());
  EXPECT_EQ(fo4::texture::mip_size(*format, 4, 4), 8);
  EXPECT_EQ(fo4::texture::mip_size(*format, 2, 2), 8);
  EXPECT_EQ(fo4::texture::mip_size(*format, 1, 1), 8);
  EXPECT_EQ(fo4::texture::mip_size(*format, 0, 0), 8);

  const fo4::texture::DdsInfo info{4, 4, 2, false, 1, 1, 148, *format};
  EXPECT_EQ(info.mip_size(0), 8);
  EXPECT_EQ(info.mip_size(1), 8);
  EXPECT_EQ(info.mip_offset(0), 0);
  EXPECT_EQ(info.mip_offset(1), 8);
}

TEST(BcDecoder, DecodesBc1WhiteAndTransparentBlocks) {
  const auto white = fo4::texture::decode_block_format(k_bc1_white, 71, 4, 4);
  ASSERT_EQ(white.size(), 64U);
  for (std::size_t i = 0; i < white.size(); i += 4) expect_rgba({white[i], white[i + 1], white[i + 2], white[i + 3]},
                                                               255, 255, 255, 255);

  const auto transparent = fo4::texture::decode_block_format(k_bc1_transparent, 71, 4, 4);
  ASSERT_EQ(transparent.size(), 64U);
  for (std::size_t i = 0; i < transparent.size(); i += 4) {
    expect_rgba({transparent[i], transparent[i + 1], transparent[i + 2], transparent[i + 3]}, 0, 0, 0, 0);
  }
}

TEST(BcDecoder, DecodesBc3Bc4AndBc5Blocks) {
  const auto bc3 = fo4::texture::decode_block_format(k_bc3_opaque_white, 77, 4, 4);
  ASSERT_EQ(bc3.size(), 64U);
  expect_rgba(pixel_at(bc3, 4, 0, 0), 255, 255, 255, 255);

  const std::vector<std::uint8_t> bc4{255, 0, 0, 0, 0, 0, 0, 0};
  const auto red = fo4::texture::decode_block_format(bc4, 80, 4, 4);
  ASSERT_EQ(red.size(), 64U);
  expect_rgba(pixel_at(red, 4, 0, 0), 255, 255, 255, 255);

  std::vector<std::uint8_t> bc5 = bc4;
  bc5.insert(bc5.end(), bc4.begin(), bc4.end());
  const auto normal = fo4::texture::decode_block_format(bc5, 83, 4, 4);
  ASSERT_EQ(normal.size(), 64U);
  expect_rgba(pixel_at(normal, 4, 0, 0), 255, 255, 0, 255);
}

TEST(BcDecoder, DecodesBc7ModeZeroBlackBlock) {
  BitWriter writer;
  writer.write(1, 1);
  writer.write(0, 4);
  for (int i = 0; i < 72; ++i) writer.write(0, 1);
  for (int i = 0; i < 6; ++i) writer.write(0, 1);
  for (int i = 0; i < 45; ++i) writer.write(0, 1);
  ASSERT_EQ(writer.count, 128);

  const auto block = writer.bytes();
  const auto decoded = fo4::texture::decode_block_format(block, 98, 4, 4);
  ASSERT_EQ(decoded.size(), 64U);
  for (std::size_t i = 0; i < decoded.size(); i += 4) {
    expect_rgba({decoded[i], decoded[i + 1], decoded[i + 2], decoded[i + 3]}, 0, 0, 0, 255);
  }
}

TEST(BcDecoder, RejectsTruncatedAndUnknownSurfaces) {
  const std::vector<std::uint8_t> tiny{0, 0, 0, 0};
  expect_dds_error([&] { fo4::texture::decode_block_format(tiny, 71, 4, 4); }, "InvalidData");
}

TEST(DdsDecoder, DecodesUncompressedLayouts) {
  std::vector<std::uint8_t> rgba_surface;
  for (int i = 0; i < 16; ++i) {
    rgba_surface.push_back(static_cast<std::uint8_t>(i));
    rgba_surface.push_back(static_cast<std::uint8_t>(i + 16));
    rgba_surface.push_back(static_cast<std::uint8_t>(i + 32));
    rgba_surface.push_back(static_cast<std::uint8_t>(i + 48));
  }
  std::uint32_t width = 0;
  std::uint32_t height = 0;
  const auto decoded = fo4::texture::decode_dds_rgba(make_dds(28, rgba_surface), 0, false, width, height);
  EXPECT_EQ(width, 4U);
  EXPECT_EQ(height, 4U);
  ASSERT_EQ(decoded.size(), 64U);
  expect_rgba(pixel_at(decoded, 4, 0, 0), 0, 16, 32, 48);

  const auto bgra = fo4::texture::decode_dds_rgba(make_dds(87, rgba_surface), 0, false, width, height);
  expect_rgba(pixel_at(bgra, 4, 0, 0), 32, 16, 0, 48);

  std::vector<std::uint8_t> x8_surface;
  for (int i = 0; i < 64; ++i) x8_surface.push_back(static_cast<std::uint8_t>(i + 1));
  const auto x8 = fo4::texture::decode_dds_rgba(make_dds(88, x8_surface), 0, false, width, height);
  expect_rgba(pixel_at(x8, 4, 0, 0), 3, 2, 1, 255);
}

TEST(DdsDecoder, DecodesBc1EndToEnd) {
  std::uint32_t width = 0;
  std::uint32_t height = 0;
  const auto decoded = fo4::texture::decode_dds_rgba(make_dds(71, k_bc1_white), 0, false, width, height);
  EXPECT_EQ(width, 4U);
  EXPECT_EQ(height, 4U);
  expect_rgba(pixel_at(decoded, 4, 0, 0), 255, 255, 255, 255);
}

TEST(DdsDecoder, ReconstructsZForUnsignedBc5NormalMaps) {
  std::vector<std::uint8_t> surface(16, 0);
  surface[0] = 255;
  std::uint32_t width = 0;
  std::uint32_t height = 0;
  const auto decoded = fo4::texture::decode_dds_rgba(make_dds(83, surface), 0, true, width, height);
  EXPECT_EQ(width, 4U);
  EXPECT_EQ(height, 4U);
  const auto pixel = pixel_at(decoded, 4, 0, 0);
  EXPECT_EQ(pixel[0], 255);
  EXPECT_EQ(pixel[1], 0);
  EXPECT_EQ(pixel[2], 128);
}

TEST(DdsDecoder, SelectsTheRequestedMip) {
  std::vector<std::uint8_t> surface(k_bc1_white);
  surface.resize(16, 0);
  surface[8] = 0x00;
  surface[9] = 0x00;
  surface[10] = 0xFF;
  surface[11] = 0xFF;
  surface[12] = 0xFF;
  surface[13] = 0xFF;
  surface[14] = 0xFF;
  surface[15] = 0xFF;

  const auto bytes = make_dds(71, surface, 2);
  std::uint32_t width = 0;
  std::uint32_t height = 0;
  const auto base = fo4::texture::decode_dds_rgba(bytes, 0, false, width, height);
  EXPECT_EQ(width, 4U);
  EXPECT_EQ(height, 4U);
  expect_rgba(pixel_at(base, 4, 0, 0), 255, 255, 255, 255);

  const auto second = fo4::texture::decode_dds_rgba(bytes, 1, false, width, height);
  EXPECT_EQ(width, 2U);
  EXPECT_EQ(height, 2U);
  expect_rgba(pixel_at(second, 2, 0, 0), 0, 0, 0, 0);

  expect_dds_error(
      [&] {
        std::uint32_t w = 0;
        std::uint32_t h = 0;
        fo4::texture::decode_dds_rgba(bytes, 2, false, w, h);
      },
      "InvalidDds");
}

TEST(DdsDecoder, ReportsBc5Containers) {
  std::vector<std::uint8_t> bc5(k_bc3_opaque_white);
  EXPECT_TRUE(fo4::texture::is_bc5(make_dds(83, bc5)));
  EXPECT_FALSE(fo4::texture::is_bc5(make_dds(71, k_bc1_white)));
  EXPECT_FALSE(fo4::texture::is_bc5(legacy_dds(0x31545844U)));
}

TEST(CapiDds, DecodesPixelsAndReportsTheHeader) {
  const auto bytes = make_dds(83, k_bc3_opaque_white);
  uint8_t* rgba = nullptr;
  uint32_t width = 0;
  uint32_t height = 0;
  ASSERT_EQ(radicle_dds_decode_rgba(bytes.data(), bytes.size(), 0, 0, &rgba, &width, &height), RADICLE_STATUS_OK)
      << radicle_last_error();
  EXPECT_EQ(width, 4U);
  EXPECT_EQ(height, 4U);
  ASSERT_NE(rgba, nullptr);
  EXPECT_EQ(rgba[0], 255);
  EXPECT_EQ(rgba[3], 255);
  radicle_free_bytes(rgba);

  uint32_t format = 0;
  uint32_t mips = 0;
  uint32_t info_width = 0;
  uint32_t info_height = 0;
  uint64_t payload = 0;
  int32_t bc5 = 0;
  ASSERT_EQ(radicle_dds_info(bytes.data(), bytes.size(), &format, &mips, &info_width, &info_height, &payload, &bc5),
            RADICLE_STATUS_OK);
  EXPECT_EQ(format, 83U);
  EXPECT_EQ(mips, 1U);
  EXPECT_EQ(info_width, 4U);
  EXPECT_EQ(info_height, 4U);
  EXPECT_EQ(payload, 16U);
  EXPECT_EQ(bc5, 1);

  EXPECT_EQ(radicle_dds_decode_rgba(nullptr, 0, 0, 0, &rgba, &width, &height), RADICLE_STATUS_INVALID_ARGUMENT);
  const std::vector<uint8_t> junk(64, 0);
  EXPECT_EQ(radicle_dds_decode_rgba(junk.data(), junk.size(), 0, 0, &rgba, &width, &height),
            RADICLE_STATUS_PARSE_ERROR);
  EXPECT_EQ(radicle_dds_info(junk.data(), junk.size(), &format, &mips, &info_width, &info_height, &payload, &bc5),
            RADICLE_STATUS_PARSE_ERROR);
}

TEST(DdsDecoder, RefusesReconstructZOnEveryFormatButUnsignedBc5) {
  std::vector<std::uint8_t> bc5(16, 0);
  bc5[0] = 255;
  std::vector<std::uint8_t> rgba_surface(64, 200);
  std::uint32_t width = 0;
  std::uint32_t height = 0;
  EXPECT_THROW(fo4::texture::decode_dds_rgba(make_dds(84, bc5), 0, true, width, height), std::invalid_argument);
  EXPECT_THROW(fo4::texture::decode_dds_rgba(make_dds(71, k_bc1_white), 0, true, width, height),
               std::invalid_argument);
  EXPECT_THROW(fo4::texture::decode_dds_rgba(make_dds(28, rgba_surface), 0, true, width, height),
               std::invalid_argument);
  EXPECT_NO_THROW(fo4::texture::decode_dds_rgba(make_dds(71, k_bc1_white), 0, false, width, height));
}

TEST(DdsCodec, RejectsArraySizeAndDepthAboveTheSixteenBitCount) {
  auto bytes = make_dds(71, k_bc1_white);
  put_u32(bytes, 140, 0x10000U);
  expect_dds_error([&] { fo4::texture::read_dds(bytes); }, "InvalidDds");

  const auto format = fo4::texture::lookup_format(71);
  ASSERT_TRUE(format.has_value());
  const fo4::texture::DdsInfo info{4, 4, 1, false, 1, 1, 148, *format};
  auto volume = fo4::texture::build_dds_header(info);
  put_u32(volume, 112, 0x200000U);
  put_u32(volume, 24, 0x10000U);
  expect_dds_error([&] { fo4::texture::read_dds(volume); }, "InvalidDds");
  put_u32(volume, 24, 0xFFFFU);
  EXPECT_EQ(fo4::texture::read_dds(volume).depth, 65535);
}

TEST(DdsCodec, RejectsAPayloadThatOverflowsASixtyFourBitByteCount) {
  const auto format = fo4::texture::lookup_format(28);
  ASSERT_TRUE(format.has_value());
  const fo4::texture::DdsInfo huge{65535, 65535, 255, false, 65535, 65535, 148, *format};
  expect_dds_error([&] { huge.payload_size(); }, "InvalidDds");
  const fo4::texture::DdsInfo modest{65535, 65535, 255, false, 4, 1, 148, *format};
  EXPECT_GT(modest.payload_size(), 0);
}

TEST(CapiDds, ReportsAReconstructZRefusalAsInvalidArgument) {
  const auto bc1 = make_dds(71, k_bc1_white);
  const auto snorm = make_dds(84, k_bc3_opaque_white);
  const auto unorm = make_dds(83, k_bc3_opaque_white);
  uint8_t* rgba = nullptr;
  uint32_t width = 0;
  uint32_t height = 0;
  EXPECT_EQ(radicle_dds_decode_rgba(bc1.data(), bc1.size(), 0, 1, &rgba, &width, &height),
            RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(rgba, nullptr);
  EXPECT_EQ(radicle_dds_decode_rgba(snorm.data(), snorm.size(), 0, 1, &rgba, &width, &height),
            RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(rgba, nullptr);
  ASSERT_EQ(radicle_dds_decode_rgba(unorm.data(), unorm.size(), 0, 1, &rgba, &width, &height), RADICLE_STATUS_OK)
      << radicle_last_error();
  EXPECT_NE(rgba, nullptr);
  radicle_free_bytes(rgba);
}

TEST(DdsCodec, SizesAHundredMipChainWithoutShiftingPastTheWidth) {
  const auto info = fo4::texture::read_dds(make_dds(71, {}, 100, 64, 64));
  EXPECT_EQ(info.mip_count, 100);
  EXPECT_EQ(info.payload_size(), 3488);
  EXPECT_EQ(info.mip_size(99), 8);
}

TEST(CapiDds, ReportsAHundredMipHeaderAndRejectsAMipPastTheFile) {
  const auto bytes = make_dds(71, {}, 100, 64, 64);
  uint32_t format = 0;
  uint32_t mips = 0;
  uint32_t width = 0;
  uint32_t height = 0;
  uint64_t payload = 0;
  int32_t bc5 = 0;
  ASSERT_EQ(radicle_dds_info(bytes.data(), bytes.size(), &format, &mips, &width, &height, &payload, &bc5),
            RADICLE_STATUS_OK)
      << radicle_last_error();
  EXPECT_EQ(mips, 100U);
  EXPECT_EQ(payload, 3488U);

  uint8_t* rgba = nullptr;
  uint32_t decoded_width = 0;
  uint32_t decoded_height = 0;
  EXPECT_EQ(radicle_dds_decode_rgba(bytes.data(), bytes.size(), 99, 0, &rgba, &decoded_width, &decoded_height),
            RADICLE_STATUS_PARSE_ERROR);
  EXPECT_EQ(rgba, nullptr);
}

TEST(CapiDds, ResetsEveryOutputOnInvalidArgument) {
  uint8_t sentinel = 0;
  uint8_t* rgba = &sentinel;
  uint32_t width = 9;
  uint32_t height = 9;
  EXPECT_EQ(radicle_dds_decode_rgba(nullptr, 0, 0, 0, &rgba, &width, &height), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(rgba, nullptr);
  EXPECT_EQ(width, 0U);
  EXPECT_EQ(height, 0U);

  uint32_t format = 9;
  uint32_t mips = 9;
  uint32_t info_width = 9;
  uint32_t info_height = 9;
  uint64_t payload = 9;
  int32_t bc5 = 9;
  EXPECT_EQ(radicle_dds_info(nullptr, 0, &format, &mips, &info_width, &info_height, &payload, &bc5),
            RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(format, 0U);
  EXPECT_EQ(mips, 0U);
  EXPECT_EQ(info_width, 0U);
  EXPECT_EQ(info_height, 0U);
  EXPECT_EQ(payload, 0U);
  EXPECT_EQ(bc5, 0);
}
