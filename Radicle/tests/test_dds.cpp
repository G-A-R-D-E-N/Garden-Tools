#include "fo4/c_api_dds.h"
#include "fo4/texture/dds.hpp"

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
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

std::vector<std::uint8_t> padded_dds(std::uint8_t dxgi,
                                     int width,
                                     int height,
                                     std::uint32_t row_pitch,
                                     const std::vector<std::uint8_t>& surface,
                                     int mip_count = 1) {
  auto bytes = make_dds(dxgi, surface, mip_count, width, height);
  put_u32(bytes, 8, 0x1U | 0x2U | 0x4U | 0x1000U | 0x8U | 0x80000U);
  put_u32(bytes, 20, row_pitch);
  return bytes;
}

std::vector<std::uint8_t> legacy_rgb24_dds(std::uint32_t r_mask,
                                           std::uint32_t b_mask,
                                           int width,
                                           int height,
                                           const std::vector<std::uint8_t>& surface,
                                           std::uint32_t declared_pitch = 0) {
  std::vector<std::uint8_t> bytes(128, 0);
  put_u32(bytes, 0, 0x20534444U);
  put_u32(bytes, 4, 124);
  put_u32(bytes, 8, 0x1U | 0x2U | 0x4U | 0x1000U | (declared_pitch != 0 ? 0x8U : 0U));
  put_u32(bytes, 12, static_cast<std::uint32_t>(height));
  put_u32(bytes, 16, static_cast<std::uint32_t>(width));
  put_u32(bytes, 20, declared_pitch);
  put_u32(bytes, 28, 1);
  put_u32(bytes, 76, 32);
  put_u32(bytes, 80, 0x40U);
  put_u32(bytes, 88, 24);
  put_u32(bytes, 92, r_mask);
  put_u32(bytes, 96, 0x0000FF00U);
  put_u32(bytes, 100, b_mask);
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

TEST(BcDecoder, RejectsUnsupportedBlockFormats) {
  const std::vector<std::uint8_t> block(16, 0);
  for (const int dxgi : {0, 81, 84, 94, 95, 96, 100, 255}) {
    expect_dds_error([&] { fo4::texture::decode_block_format(block, static_cast<std::uint8_t>(dxgi), 4, 4); },
                     "UnsupportedFormat");
  }
  const std::vector<std::uint8_t> tiny{0, 0, 0, 0};
  expect_dds_error([&] { fo4::texture::decode_block_format(tiny, 95, 4, 4); }, "UnsupportedFormat");
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
  EXPECT_TRUE(fo4::texture::is_bc5(make_dds(82, bc5)));
  EXPECT_FALSE(fo4::texture::is_bc5(make_dds(84, bc5)));
  EXPECT_FALSE(fo4::texture::is_bc5(make_dds(71, k_bc1_white)));
  EXPECT_FALSE(fo4::texture::is_bc5(legacy_dds(0x31545844U)));

  const auto dds_info_bc5 = [](const std::vector<std::uint8_t>& bytes) {
    uint32_t format = 0;
    uint32_t mips = 0;
    uint32_t width = 0;
    uint32_t height = 0;
    uint64_t payload = 0;
    int32_t flag = 0;
    EXPECT_EQ(radicle_dds_info(bytes.data(), bytes.size(), &format, &mips, &width, &height, &payload, &flag),
              RADICLE_STATUS_OK);
    return flag;
  };
  EXPECT_EQ(dds_info_bc5(make_dds(82, bc5)), 1);
  EXPECT_EQ(dds_info_bc5(make_dds(83, bc5)), 1);
  EXPECT_EQ(dds_info_bc5(make_dds(84, bc5)), 0);
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

TEST(DdsDecoder, ReadsPaddedSingleChannelRowsAtTheDeclaredPitch) {
  const std::vector<std::uint8_t> surface{0x10, 0xBB, 0xCC, 0xDD, 0x20, 0xEE, 0xEF, 0xF0};
  const auto bytes = padded_dds(61, 1, 2, 4, surface);
  EXPECT_EQ(fo4::texture::read_dds(bytes).payload_size(), 8);

  std::uint32_t width = 0;
  std::uint32_t height = 0;
  const auto decoded = fo4::texture::decode_dds_rgba(bytes, 0, false, width, height);
  EXPECT_EQ(width, 1U);
  EXPECT_EQ(height, 2U);
  ASSERT_EQ(decoded.size(), 8U);
  expect_rgba(pixel_at(decoded, 1, 0, 0), 0x10, 0x10, 0x10, 255);
  expect_rgba(pixel_at(decoded, 1, 0, 1), 0x20, 0x20, 0x20, 255);
}

TEST(DdsDecoder, SkipsPaddingWithinAndBetweenUncompressedRows) {
  const std::vector<std::uint8_t> surface{1,  2,  3,  4,  5,  6,  7,  8,  0xEE, 0xEE, 0xEE, 0xEE,
                                          9, 10, 11, 12, 13, 14, 15, 16,  0xEE, 0xEE, 0xEE, 0xEE};
  const auto bytes = padded_dds(28, 2, 2, 12, surface);
  EXPECT_EQ(fo4::texture::read_dds(bytes).payload_size(), 24);

  std::uint32_t width = 0;
  std::uint32_t height = 0;
  const auto decoded = fo4::texture::decode_dds_rgba(bytes, 0, false, width, height);
  EXPECT_EQ(width, 2U);
  EXPECT_EQ(height, 2U);
  ASSERT_EQ(decoded.size(), 16U);
  expect_rgba(pixel_at(decoded, 2, 0, 0), 1, 2, 3, 4);
  expect_rgba(pixel_at(decoded, 2, 1, 0), 5, 6, 7, 8);
  expect_rgba(pixel_at(decoded, 2, 0, 1), 9, 10, 11, 12);
  expect_rgba(pixel_at(decoded, 2, 1, 1), 13, 14, 15, 16);
}

TEST(DdsDecoder, SizesPaddedMipChainsAtEachLevelsPitch) {
  const std::vector<std::uint8_t> surface{0x11, 0xEE, 0xEE, 0xEE, 0x22, 0xEE, 0xEE, 0xEE, 0x33, 0xEE, 0xEE, 0xEE};
  const auto bytes = padded_dds(61, 2, 2, 4, surface, 2);
  const auto info = fo4::texture::read_dds(bytes);
  EXPECT_EQ(info.payload_size(), 12);
  EXPECT_EQ(info.mip_size(1), 4);

  std::uint32_t width = 0;
  std::uint32_t height = 0;
  const auto second = fo4::texture::decode_dds_rgba(bytes, 1, false, width, height);
  EXPECT_EQ(width, 1U);
  EXPECT_EQ(height, 1U);
  ASSERT_EQ(second.size(), 4U);
  expect_rgba(pixel_at(second, 1, 0, 0), 0x33, 0x33, 0x33, 255);
}

TEST(DdsCodec, IgnoresARowPitchShorterThanOneRowAndReadsTightRows) {
  std::vector<std::uint8_t> surface;
  for (int i = 0; i < 16; ++i) surface.push_back(static_cast<std::uint8_t>(i + 1));
  const auto bytes = padded_dds(28, 4, 1, 8, surface);
  EXPECT_EQ(fo4::texture::read_dds(bytes).payload_size(), 16);

  std::uint32_t width = 0;
  std::uint32_t height = 0;
  const auto decoded = fo4::texture::decode_dds_rgba(bytes, 0, false, width, height);
  ASSERT_EQ(decoded.size(), 16U);
  expect_rgba(pixel_at(decoded, 4, 3, 0), 13, 14, 15, 16);
}

TEST(DdsDecoder, DecodesLegacy24BitBgrAndRgbWithOpaqueAlpha) {
  const std::vector<std::uint8_t> surface{10, 20, 30, 40, 50, 60};
  std::uint32_t width = 0;
  std::uint32_t height = 0;

  const auto bgr_bytes = legacy_rgb24_dds(0x00FF0000U, 0x000000FFU, 2, 1, surface);
  const auto bgr_info = fo4::texture::read_dds(bgr_bytes);
  EXPECT_EQ(bgr_info.data_offset, 128);
  EXPECT_EQ(bgr_info.payload_size(), 6);
  const auto bgr = fo4::texture::decode_dds_rgba(bgr_bytes, 0, false, width, height);
  ASSERT_EQ(bgr.size(), 8U);
  expect_rgba(pixel_at(bgr, 2, 0, 0), 30, 20, 10, 255);
  expect_rgba(pixel_at(bgr, 2, 1, 0), 60, 50, 40, 255);

  const auto rgb = fo4::texture::decode_dds_rgba(legacy_rgb24_dds(0x000000FFU, 0x00FF0000U, 2, 1, surface), 0, false,
                                                 width, height);
  ASSERT_EQ(rgb.size(), 8U);
  expect_rgba(pixel_at(rgb, 2, 0, 0), 10, 20, 30, 255);
  expect_rgba(pixel_at(rgb, 2, 1, 0), 40, 50, 60, 255);
}

TEST(DdsDecoder, Legacy24BitHonoursAPaddedPitchAndIgnoresAShortOne) {
  std::uint32_t width = 0;
  std::uint32_t height = 0;

  const std::vector<std::uint8_t> padded{1, 2, 3, 4, 5, 6, 0xEE, 0xEE, 7, 8, 9, 10, 11, 12, 0xEE, 0xEE};
  const auto padded_rgb =
      fo4::texture::decode_dds_rgba(legacy_rgb24_dds(0x00FF0000U, 0x000000FFU, 2, 2, padded, 8), 0, false, width, height);
  ASSERT_EQ(padded_rgb.size(), 16U);
  expect_rgba(pixel_at(padded_rgb, 2, 0, 1), 9, 8, 7, 255);
  expect_rgba(pixel_at(padded_rgb, 2, 1, 1), 12, 11, 10, 255);

  const std::vector<std::uint8_t> tight{1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12};
  const auto short_pitch =
      fo4::texture::decode_dds_rgba(legacy_rgb24_dds(0x00FF0000U, 0x000000FFU, 2, 2, tight, 3), 0, false, width, height);
  ASSERT_EQ(short_pitch.size(), 16U);
  expect_rgba(pixel_at(short_pitch, 2, 0, 1), 9, 8, 7, 255);
  expect_rgba(pixel_at(short_pitch, 2, 1, 1), 12, 11, 10, 255);
}

TEST(DdsCodec, KeepsTightRowsWhenTheDeclaredPitchMatchesTheRow) {
  const std::vector<std::uint8_t> surface{0x10, 0x20, 0x30, 0x40, 0x50, 0x60, 0x70, 0x80};
  const auto bytes = padded_dds(61, 4, 2, 4, surface);
  EXPECT_EQ(fo4::texture::read_dds(bytes).payload_size(), 8);

  std::uint32_t width = 0;
  std::uint32_t height = 0;
  const auto decoded = fo4::texture::decode_dds_rgba(bytes, 0, false, width, height);
  ASSERT_EQ(decoded.size(), 32U);
  expect_rgba(pixel_at(decoded, 4, 3, 0), 0x40, 0x40, 0x40, 255);
  expect_rgba(pixel_at(decoded, 4, 0, 1), 0x50, 0x50, 0x50, 255);
}

namespace {

std::vector<std::uint8_t> solid_rgba(int width, int height, std::uint8_t red, std::uint8_t green, std::uint8_t blue) {
  std::vector<std::uint8_t> rgba(static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4);
  for (std::size_t i = 0; i < rgba.size(); i += 4) {
    rgba[i] = red;
    rgba[i + 1] = green;
    rgba[i + 2] = blue;
    rgba[i + 3] = 255;
  }
  return rgba;
}

}  // namespace

// Detects (invariant): BC1 encoding must give an opaque block whose endpoints decode back to the input colour.
TEST(BcnEncode, Bc1SolidBlockDecodesOpaque) {
  const auto encoded = fo4::texture::encode_bc1(solid_rgba(4, 4, 200, 120, 40), 4, 4);
  ASSERT_EQ(encoded.size(), 8U);
  const auto decoded = fo4::texture::decode_block_format(encoded, 71, 4, 4);
  ASSERT_EQ(decoded.size(), 64U);
  for (std::size_t i = 0; i < decoded.size(); i += 4) {
    EXPECT_NEAR(static_cast<int>(decoded[i]), 200, 8);
    EXPECT_NEAR(static_cast<int>(decoded[i + 1]), 120, 8);
    EXPECT_NEAR(static_cast<int>(decoded[i + 2]), 40, 8);
    EXPECT_EQ(static_cast<int>(decoded[i + 3]), 255);
  }
}

// Detects (invariant): pure white must decode exactly, catching a palette or endpoint-order regression in BC1.
TEST(BcnEncode, Bc1WhiteIsExact) {
  const auto decoded = fo4::texture::decode_block_format(
      fo4::texture::encode_bc1(solid_rgba(4, 4, 255, 255, 255), 4, 4), 71, 4, 4);
  ASSERT_EQ(decoded.size(), 64U);
  for (std::size_t i = 0; i < decoded.size(); i += 4) {
    expect_rgba({decoded[i], decoded[i + 1], decoded[i + 2], decoded[i + 3]}, 255, 255, 255, 255);
  }
}

// Detects (invariant): a flat block and a 0/255 two-level block must decode exactly, catching BC4 endpoint or palette-mode errors.
TEST(BcnEncode, Bc4SolidAndTwoLevelBlocksAreExact) {
  const auto flat = fo4::texture::encode_bc4(solid_rgba(4, 4, 77, 0, 0), 4, 4);
  ASSERT_EQ(flat.size(), 8U);
  const auto flat_decoded = fo4::texture::decode_block_format(flat, 80, 4, 4);
  ASSERT_EQ(flat_decoded.size(), 64U);
  for (std::size_t p = 0; p < 16; ++p) EXPECT_EQ(static_cast<int>(flat_decoded[p * 4]), 77);

  auto two_level = solid_rgba(4, 4, 0, 0, 0);
  for (std::size_t p = 0; p < 16; ++p) two_level[p * 4] = static_cast<std::uint8_t>((p % 2 == 0) ? 0 : 255);
  const auto decoded = fo4::texture::decode_block_format(fo4::texture::encode_bc4(two_level, 4, 4), 80, 4, 4);
  ASSERT_EQ(decoded.size(), 64U);
  for (std::size_t p = 0; p < 16; ++p) EXPECT_EQ(decoded[p * 4], two_level[p * 4]);
}

// Detects (invariant): a gradient over partial edge blocks must stay within the 8-level palette step, catching edge-clamp or block-offset bugs.
TEST(BcnEncode, Bc4GradientWithinQuantisationError) {
  constexpr int width = 6;
  constexpr int height = 5;
  auto rgba = solid_rgba(width, height, 0, 0, 0);
  for (int y = 0; y < height; ++y) {
    for (int x = 0; x < width; ++x) {
      rgba[static_cast<std::size_t>(y * width + x) * 4] = static_cast<std::uint8_t>(x * 40 + y * 8);
    }
  }
  const auto encoded = fo4::texture::encode_bc4(rgba, width, height);
  ASSERT_EQ(encoded.size(), 2U * 2U * 8U);
  const auto decoded = fo4::texture::decode_block_format(encoded, 80, width, height);
  ASSERT_EQ(decoded.size(), static_cast<std::size_t>(width * height * 4));
  for (int y = 0; y < height; ++y) {
    for (int x = 0; x < width; ++x) {
      const auto p = static_cast<std::size_t>(y * width + x) * 4;
      EXPECT_LE(std::abs(static_cast<int>(decoded[p]) - static_cast<int>(rgba[p])), 20);
    }
  }
}

// Detects (invariant): BC5 must write red then green, so a swapped or misaligned block shows as a channel error.
TEST(BcnEncode, Bc5RoundTripsRedAndGreen) {
  constexpr int width = 8;
  constexpr int height = 8;
  auto rgba = solid_rgba(width, height, 0, 0, 0);
  for (int y = 0; y < height; ++y) {
    for (int x = 0; x < width; ++x) {
      const auto p = static_cast<std::size_t>(y * width + x) * 4;
      rgba[p] = static_cast<std::uint8_t>(x * 30);
      rgba[p + 1] = static_cast<std::uint8_t>(y * 30);
    }
  }
  const auto encoded = fo4::texture::encode_bc5(rgba, width, height);
  ASSERT_EQ(encoded.size(), 4U * 16U);
  const auto decoded = fo4::texture::decode_block_format(encoded, 83, width, height);
  ASSERT_EQ(decoded.size(), static_cast<std::size_t>(width * height * 4));
  for (int y = 0; y < height; ++y) {
    for (int x = 0; x < width; ++x) {
      const auto p = static_cast<std::size_t>(y * width + x) * 4;
      EXPECT_LE(std::abs(static_cast<int>(decoded[p]) - static_cast<int>(rgba[p])), 20);
      EXPECT_LE(std::abs(static_cast<int>(decoded[p + 1]) - static_cast<int>(rgba[p + 1])), 20);
    }
  }
}

// Detects (invariant): an input shorter than width*height*4 must be rejected before any block is read.
TEST(BcnEncode, RejectsInputWhoseSizeDoesNotMatchItsDimensions) {
  const std::vector<std::uint8_t> short_rgba(10U, std::uint8_t{0});
  expect_dds_error([&] { fo4::texture::encode_bc1(short_rgba, 4, 4); }, "InvalidImage");
  expect_dds_error([&] { fo4::texture::encode_bc4(short_rgba, 4, 4); }, "InvalidImage");
  expect_dds_error([&] { fo4::texture::encode_bc5(short_rgba, 4, 4); }, "InvalidImage");
}

TEST(DdsCodec, RejectsADx10HeaderNamingALegacyOnlyRgbId) {
  for (const std::uint32_t dxgi : {240U, 241U}) {
    auto bytes = make_dds(71, k_bc1_white);
    put_u32(bytes, 128, dxgi);
    expect_dds_error([&] { fo4::texture::read_dds(bytes); }, "UnsupportedFormat");
  }
}

namespace {

struct FaceColour {
  std::uint8_t red;
  std::uint8_t green;
  std::uint8_t blue;
};

const std::array<FaceColour, 6> k_face_colours{{{255, 0, 0},
                                                {0, 255, 0},
                                                {0, 0, 255},
                                                {255, 255, 0},
                                                {0, 255, 255},
                                                {255, 0, 255}}};

std::vector<std::uint8_t> solid_bgra(int width, int height, std::uint8_t red, std::uint8_t green,
                                     std::uint8_t blue) {
  std::vector<std::uint8_t> pixels(static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4);
  for (std::size_t i = 0; i < pixels.size(); i += 4) {
    pixels[i] = blue;
    pixels[i + 1] = green;
    pixels[i + 2] = red;
    pixels[i + 3] = 255;
  }
  return pixels;
}

std::vector<std::vector<std::uint8_t>> colour_faces(int width, int height) {
  std::vector<std::vector<std::uint8_t>> faces;
  for (const auto& colour : k_face_colours) {
    faces.push_back(solid_bgra(width, height, colour.red, colour.green, colour.blue));
  }
  return faces;
}

std::vector<std::uint8_t> dx10_cube_dds(std::uint8_t dxgi,
                                        int width,
                                        int height,
                                        int mip_count,
                                        const std::vector<std::vector<std::uint8_t>>& faces) {
  const auto format = fo4::texture::lookup_format(dxgi);
  if (!format) return {};
  const fo4::texture::DdsInfo info{width, height, mip_count, true, 6, 1, 148, *format, 0};
  auto bytes = fo4::texture::build_dds_header(info);
  for (const auto& face : faces) bytes.insert(bytes.end(), face.begin(), face.end());
  return bytes;
}

std::vector<std::uint8_t> legacy_cube_dds(int width, int height,
                                          const std::vector<std::vector<std::uint8_t>>& faces) {
  std::vector<std::uint8_t> bytes(128, 0);
  put_u32(bytes, 0, 0x20534444U);
  put_u32(bytes, 4, 124);
  put_u32(bytes, 8, 0x1U | 0x2U | 0x4U | 0x1000U | 0x80000U);
  put_u32(bytes, 12, static_cast<std::uint32_t>(height));
  put_u32(bytes, 16, static_cast<std::uint32_t>(width));
  put_u32(bytes, 28, 1);
  put_u32(bytes, 76, 32);
  put_u32(bytes, 80, 0x40U);
  put_u32(bytes, 88, 32);
  put_u32(bytes, 92, 0x00FF0000U);
  put_u32(bytes, 96, 0x0000FF00U);
  put_u32(bytes, 100, 0x000000FFU);
  put_u32(bytes, 104, 0xFF000000U);
  put_u32(bytes, 108, 0x1000U);
  put_u32(bytes, 112, 0x200U | 0xFC00U);
  for (const auto& face : faces) bytes.insert(bytes.end(), face.begin(), face.end());
  return bytes;
}

std::vector<std::uint8_t> face_blocks(const std::vector<std::uint8_t>& file,
                                      std::uint32_t face,
                                      std::uint32_t mips_per_face,
                                      std::uint32_t mip_count,
                                      std::uint8_t unsigned_rgtc,
                                      radicle_status expected = RADICLE_STATUS_OK) {
  uint8_t* out = nullptr;
  size_t out_size = 0;
  EXPECT_EQ(radicle_dds_face_blocks(file.data(), file.size(), face, mips_per_face, mip_count, unsigned_rgtc, &out,
                                    &out_size),
            expected)
      << radicle_last_error();
  std::vector<std::uint8_t> blocks;
  if (out) {
    blocks.assign(out, out + out_size);
    radicle_free_bytes(out);
  }
  return blocks;
}

}  // namespace

TEST(CapiDds, ReportsTheDataOffsetFaceCountAndCubeFlag) {
  uint32_t offset = 99;
  uint32_t faces = 99;
  int32_t cube = 9;

  const auto legacy = legacy_dds(0x31545844U);
  ASSERT_EQ(radicle_dds_layout(legacy.data(), legacy.size(), &offset, &faces, &cube), RADICLE_STATUS_OK)
      << radicle_last_error();
  EXPECT_EQ(offset, 128U);
  EXPECT_EQ(faces, 1U);
  EXPECT_EQ(cube, 0);

  const auto dx10 = make_dds(71, k_bc1_white);
  ASSERT_EQ(radicle_dds_layout(dx10.data(), dx10.size(), &offset, &faces, &cube), RADICLE_STATUS_OK)
      << radicle_last_error();
  EXPECT_EQ(offset, 148U);
  EXPECT_EQ(faces, 1U);
  EXPECT_EQ(cube, 0);

  const auto cube_bytes = dx10_cube_dds(87, 4, 4, 1, colour_faces(4, 4));
  ASSERT_EQ(radicle_dds_layout(cube_bytes.data(), cube_bytes.size(), &offset, &faces, &cube), RADICLE_STATUS_OK)
      << radicle_last_error();
  EXPECT_EQ(offset, 148U);
  EXPECT_EQ(faces, 6U);
  EXPECT_EQ(cube, 1);
}

TEST(CapiDds, ResetsTheLayoutOutputsAndRefusesJunk) {
  uint32_t offset = 9;
  uint32_t faces = 9;
  int32_t cube = 9;
  EXPECT_EQ(radicle_dds_layout(nullptr, 0, &offset, &faces, &cube), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(offset, 0U);
  EXPECT_EQ(faces, 0U);
  EXPECT_EQ(cube, 0);

  const std::vector<std::uint8_t> junk(64, 0);
  EXPECT_EQ(radicle_dds_layout(junk.data(), junk.size(), &offset, &faces, &cube), RADICLE_STATUS_PARSE_ERROR);
  EXPECT_EQ(faces, 0U);
}

TEST(DdsDecoder, DecodesEachFaceOfADx10BgraCube) {
  const auto bytes = dx10_cube_dds(87, 4, 4, 1, colour_faces(4, 4));
  std::uint32_t width = 0;
  std::uint32_t height = 0;
  for (std::uint32_t face = 0; face < 6; ++face) {
    const auto decoded = fo4::texture::decode_dds_rgba(bytes, face, 0, false, width, height);
    ASSERT_EQ(decoded.size(), 64U) << "face " << face;
    EXPECT_EQ(width, 4U);
    EXPECT_EQ(height, 4U);
    expect_rgba(pixel_at(decoded, 4, 0, 0), k_face_colours[face].red, k_face_colours[face].green,
                k_face_colours[face].blue, 255);
  }

  const auto base = fo4::texture::decode_dds_rgba(bytes, 0, false, width, height);
  expect_rgba(pixel_at(base, 4, 0, 0), 255, 0, 0, 255);
}

TEST(DdsDecoder, DecodesEachFaceOfALegacyCube) {
  const auto bytes = legacy_cube_dds(4, 4, colour_faces(4, 4));
  EXPECT_EQ(fo4::texture::read_dds(bytes).face_count(), 6);
  std::uint32_t width = 0;
  std::uint32_t height = 0;
  for (std::uint32_t face = 0; face < 6; ++face) {
    const auto decoded = fo4::texture::decode_dds_rgba(bytes, face, 0, false, width, height);
    ASSERT_EQ(decoded.size(), 64U) << "face " << face;
    expect_rgba(pixel_at(decoded, 4, 0, 0), k_face_colours[face].red, k_face_colours[face].green,
                k_face_colours[face].blue, 255);
  }
}

TEST(DdsDecoder, SelectsTheFaceAndMipOfACubeChain) {
  const auto format = fo4::texture::lookup_format(87);
  ASSERT_TRUE(format.has_value());
  const fo4::texture::DdsInfo info{4, 4, 2, true, 6, 1, 148, *format, 0};
  auto bytes = fo4::texture::build_dds_header(info);
  for (int face = 0; face < 6; ++face) {
    const auto base = solid_bgra(4, 4, static_cast<std::uint8_t>(40 * face), 0, 0);
    const auto smaller = solid_bgra(2, 2, 0, static_cast<std::uint8_t>(40 * face), 0);
    bytes.insert(bytes.end(), base.begin(), base.end());
    bytes.insert(bytes.end(), smaller.begin(), smaller.end());
  }

  std::uint32_t width = 0;
  std::uint32_t height = 0;
  const auto base = fo4::texture::decode_dds_rgba(bytes, 2, 0, false, width, height);
  EXPECT_EQ(width, 4U);
  EXPECT_EQ(height, 4U);
  expect_rgba(pixel_at(base, 4, 0, 0), 80, 0, 0, 255);

  const auto second = fo4::texture::decode_dds_rgba(bytes, 2, 1, false, width, height);
  EXPECT_EQ(width, 2U);
  EXPECT_EQ(height, 2U);
  ASSERT_EQ(second.size(), 16U);
  expect_rgba(pixel_at(second, 2, 0, 0), 0, 80, 0, 255);
}

TEST(DdsDecoder, DecodesEachSliceOfADx10NonCubeArray) {
  const auto format = fo4::texture::lookup_format(87);
  ASSERT_TRUE(format.has_value());
  const fo4::texture::DdsInfo info{4, 4, 1, false, 3, 1, 148, *format, 0};
  auto bytes = fo4::texture::build_dds_header(info);
  for (std::size_t slice = 0; slice < 3; ++slice) {
    const auto pixels = solid_bgra(4, 4, k_face_colours[slice].red, k_face_colours[slice].green,
                                   k_face_colours[slice].blue);
    bytes.insert(bytes.end(), pixels.begin(), pixels.end());
  }

  const auto read = fo4::texture::read_dds(bytes);
  EXPECT_FALSE(read.is_cube_map);
  EXPECT_EQ(read.face_count(), 3);
  std::uint32_t width = 0;
  std::uint32_t height = 0;
  for (std::uint32_t slice = 0; slice < 3; ++slice) {
    const auto decoded = fo4::texture::decode_dds_rgba(bytes, slice, 0, false, width, height);
    expect_rgba(pixel_at(decoded, 4, 0, 0), k_face_colours[slice].red, k_face_colours[slice].green,
                k_face_colours[slice].blue, 255);
  }
  expect_dds_error([&] { fo4::texture::decode_dds_rgba(bytes, 3, 0, false, width, height); }, "InvalidDds");
}

TEST(DdsDecoder, RefusesAFaceWhoseBytesAreTruncated) {
  auto bytes = dx10_cube_dds(87, 4, 4, 1, colour_faces(4, 4));
  bytes.resize(bytes.size() - 8);
  std::uint32_t width = 0;
  std::uint32_t height = 0;
  const auto fifth = fo4::texture::decode_dds_rgba(bytes, 4, 0, false, width, height);
  expect_rgba(pixel_at(fifth, 4, 0, 0), k_face_colours[4].red, k_face_colours[4].green,
              k_face_colours[4].blue, 255);
  expect_dds_error([&] { fo4::texture::decode_dds_rgba(bytes, 5, 0, false, width, height); }, "InvalidDds");
}

TEST(DdsDecoder, RefusesAHostileHugeArrayHeaderAsInvalidDdsWithoutAllocating) {
  const auto format = fo4::texture::lookup_format(87);
  ASSERT_TRUE(format.has_value());
  const fo4::texture::DdsInfo hostile{65535, 65535, 1, false, 65535, 1, 148, *format, 0};
  const auto bytes = fo4::texture::build_dds_header(hostile);
  std::uint32_t width = 0;
  std::uint32_t height = 0;
  expect_dds_error([&] { fo4::texture::decode_dds_rgba(bytes, 65534, 0, false, width, height); }, "InvalidDds");
  expect_dds_error([&] { fo4::texture::decode_dds_rgba(bytes, 0, 0, false, width, height); }, "InvalidDds");
}

TEST(CapiDds, DecodesARequestedFaceAndReportsAnOutOfRangeFaceAsAnError) {
  const auto bytes = dx10_cube_dds(87, 4, 4, 1, colour_faces(4, 4));
  uint8_t* rgba = nullptr;
  uint32_t width = 0;
  uint32_t height = 0;
  ASSERT_EQ(radicle_dds_decode_rgba_face(bytes.data(), bytes.size(), 3, 0, 0, &rgba, &width, &height),
            RADICLE_STATUS_OK)
      << radicle_last_error();
  ASSERT_NE(rgba, nullptr);
  EXPECT_EQ(rgba[0], k_face_colours[3].red);
  EXPECT_EQ(rgba[1], k_face_colours[3].green);
  EXPECT_EQ(rgba[2], k_face_colours[3].blue);
  EXPECT_EQ(rgba[3], 255);
  radicle_free_bytes(rgba);

  rgba = nullptr;
  EXPECT_EQ(radicle_dds_decode_rgba_face(bytes.data(), bytes.size(), 6, 0, 0, &rgba, &width, &height),
            RADICLE_STATUS_PARSE_ERROR);
  EXPECT_EQ(rgba, nullptr);

  EXPECT_EQ(radicle_dds_decode_rgba_face(bytes.data(), bytes.size(), 0, 1, 0, &rgba, &width, &height),
            RADICLE_STATUS_PARSE_ERROR);
  EXPECT_EQ(rgba, nullptr);

  const auto single = make_dds(71, k_bc1_white);
  EXPECT_EQ(radicle_dds_decode_rgba_face(single.data(), single.size(), 1, 0, 0, &rgba, &width, &height),
            RADICLE_STATUS_PARSE_ERROR);
  EXPECT_EQ(rgba, nullptr);
}

TEST(CapiDds, BlockChainReturnsTheStoredMipRunAndFlipsSignedRgtcOnRequest) {
  std::vector<std::uint8_t> surface(40);
  for (std::size_t i = 0; i < surface.size(); ++i) surface[i] = static_cast<std::uint8_t>(i);
  surface[0] = 0x80;
  surface[1] = 0x7F;
  surface[32] = 0x00;
  surface[33] = 0x82;
  const auto bytes = make_dds(81, surface, 2, 8, 8);
  ASSERT_FALSE(bytes.empty());

  uint8_t* out = nullptr;
  size_t out_size = 0;
  ASSERT_EQ(radicle_dds_block_chain(bytes.data(), bytes.size(), 0, 1, 1, 0, &out, &out_size), RADICLE_STATUS_OK)
      << radicle_last_error();
  ASSERT_EQ(out_size, 8U);
  EXPECT_EQ(std::vector<std::uint8_t>(out, out + out_size),
            std::vector<std::uint8_t>(surface.begin() + 32, surface.end()));
  radicle_free_bytes(out);

  ASSERT_EQ(radicle_dds_block_chain(bytes.data(), bytes.size(), 0, 0, 2, 1, &out, &out_size), RADICLE_STATUS_OK)
      << radicle_last_error();
  ASSERT_EQ(out_size, 40U);
  EXPECT_EQ(out[0], 0x01);
  EXPECT_EQ(out[1], 0xFF);
  EXPECT_EQ(out[2], surface[2]);
  EXPECT_EQ(out[32], 0x80);
  EXPECT_EQ(out[33], 0x02);
  radicle_free_bytes(out);

  const auto bc5 = make_dds(84, std::vector<std::uint8_t>(16, 0x10));
  ASSERT_EQ(radicle_dds_block_chain(bc5.data(), bc5.size(), 0, 0, 1, 1, &out, &out_size), RADICLE_STATUS_OK)
      << radicle_last_error();
  ASSERT_EQ(out_size, 16U);
  for (size_t i = 0; i < out_size; ++i) EXPECT_EQ(out[i], (i == 0 || i == 1 || i == 8 || i == 9) ? 0x90 : 0x10) << i;
  radicle_free_bytes(out);

  const auto unorm = make_dds(80, std::vector<std::uint8_t>(8, 0x80));
  ASSERT_EQ(radicle_dds_block_chain(unorm.data(), unorm.size(), 0, 0, 1, 1, &out, &out_size), RADICLE_STATUS_OK)
      << radicle_last_error();
  EXPECT_EQ(out[0], 0x80);
  radicle_free_bytes(out);
}

TEST(CapiDds, BlockChainSelectsTheFaceOfACubeAndRefusesBadRequests) {
  std::vector<std::vector<std::uint8_t>> faces;
  for (std::uint8_t face = 0; face < 6; ++face) faces.emplace_back(8, static_cast<std::uint8_t>(0x10 + face));
  const auto cube = dx10_cube_dds(71, 4, 4, 1, faces);
  uint8_t* out = nullptr;
  size_t out_size = 0;
  ASSERT_EQ(radicle_dds_block_chain(cube.data(), cube.size(), 4, 0, 1, 0, &out, &out_size), RADICLE_STATUS_OK)
      << radicle_last_error();
  ASSERT_EQ(out_size, 8U);
  EXPECT_EQ(out[0], 0x14);
  radicle_free_bytes(out);

  EXPECT_EQ(radicle_dds_block_chain(cube.data(), cube.size(), 6, 0, 1, 0, &out, &out_size), RADICLE_STATUS_PARSE_ERROR);
  EXPECT_EQ(out, nullptr);
  EXPECT_EQ(radicle_dds_block_chain(cube.data(), cube.size(), 0, 1, 1, 0, &out, &out_size), RADICLE_STATUS_PARSE_ERROR);
  EXPECT_EQ(radicle_dds_block_chain(cube.data(), cube.size(), 0, 0, 0, 0, &out, &out_size), RADICLE_STATUS_PARSE_ERROR);

  auto truncated = cube;
  truncated.resize(truncated.size() - 4);
  EXPECT_EQ(radicle_dds_block_chain(truncated.data(), truncated.size(), 5, 0, 1, 0, &out, &out_size),
            RADICLE_STATUS_PARSE_ERROR);
  EXPECT_EQ(out, nullptr);
  EXPECT_EQ(radicle_dds_block_chain(truncated.data(), truncated.size(), 4, 0, 1, 0, &out, &out_size), RADICLE_STATUS_OK);
  radicle_free_bytes(out);

  const auto plain = dx10_cube_dds(87, 4, 4, 1, colour_faces(4, 4));
  EXPECT_EQ(radicle_dds_block_chain(plain.data(), plain.size(), 0, 0, 1, 0, &out, &out_size), RADICLE_STATUS_PARSE_ERROR);
  EXPECT_EQ(radicle_dds_block_chain(nullptr, 0, 0, 0, 1, 0, &out, &out_size), RADICLE_STATUS_INVALID_ARGUMENT);
}

TEST(CapiDds, FaceBlocksSliceACubeWithoutTheCubeFlagAndStrideByTheCallersMipCount) {
  std::vector<std::vector<std::uint8_t>> faces;
  std::vector<std::uint8_t> all;
  for (std::uint8_t face = 0; face < 6; ++face) {
    faces.emplace_back(56, static_cast<std::uint8_t>(0x10 + face));
    all.insert(all.end(), faces.back().begin(), faces.back().end());
  }

  const auto unflagged = make_dds(71, all, 4, 8, 8);
  ASSERT_FALSE(unflagged.empty());
  uint32_t offset = 0;
  uint32_t face_count = 0;
  int32_t cube = 9;
  ASSERT_EQ(radicle_dds_layout(unflagged.data(), unflagged.size(), &offset, &face_count, &cube), RADICLE_STATUS_OK);
  EXPECT_EQ(face_count, 1U);
  EXPECT_EQ(cube, 0);
  uint8_t* out = nullptr;
  size_t out_size = 0;
  EXPECT_EQ(radicle_dds_block_chain(unflagged.data(), unflagged.size(), 3, 0, 4, 0, &out, &out_size),
            RADICLE_STATUS_PARSE_ERROR);

  EXPECT_EQ(face_blocks(unflagged, 3, 4, 4, 0), std::vector<std::uint8_t>(56, 0x13));
  EXPECT_EQ(face_blocks(unflagged, 5, 4, 1, 0), std::vector<std::uint8_t>(32, 0x15));
  EXPECT_EQ(face_blocks(unflagged, 0, 4, 4, 0), faces[0]);

  const auto over_declared = make_dds(71, all, 5, 8, 8);
  EXPECT_EQ(face_blocks(over_declared, 5, 4, 4, 0), faces[5]);
  const auto flagged = dx10_cube_dds(71, 8, 8, 5, faces);
  EXPECT_EQ(face_blocks(flagged, 5, 4, 4, 0), faces[5]);
  EXPECT_EQ(face_blocks(flagged, 2, 4, 1, 0), std::vector<std::uint8_t>(32, 0x12));
}

TEST(CapiDds, FaceBlocksFlipSignedRgtcOnAFaceAboveZeroAndRefuseBadRequests) {
  std::vector<std::uint8_t> surface(32, 0x10);
  surface[16] = 0x80;
  surface[17] = 0x7F;
  surface[24] = 0x00;
  surface[25] = 0x82;
  const auto two_faces = make_dds(84, surface, 1, 4, 4);
  const auto flipped = face_blocks(two_faces, 1, 1, 1, 1);
  ASSERT_EQ(flipped.size(), 16U);
  EXPECT_EQ(flipped[0], 0x01);
  EXPECT_EQ(flipped[1], 0xFF);
  EXPECT_EQ(flipped[8], 0x80);
  EXPECT_EQ(flipped[9], 0x02);
  EXPECT_EQ(flipped[2], 0x10);
  EXPECT_EQ(face_blocks(two_faces, 1, 1, 1, 0)[0], 0x80);

  std::vector<std::uint8_t> all;
  for (std::uint8_t face = 0; face < 6; ++face) all.insert(all.end(), 56, static_cast<std::uint8_t>(0x10 + face));
  const auto file = make_dds(71, all, 4, 8, 8);
  EXPECT_TRUE(face_blocks(file, 6, 4, 1, 0, RADICLE_STATUS_PARSE_ERROR).empty());
  EXPECT_TRUE(face_blocks(file, 0, 0, 1, 0, RADICLE_STATUS_PARSE_ERROR).empty());
  EXPECT_TRUE(face_blocks(file, 0, 4, 0, 0, RADICLE_STATUS_PARSE_ERROR).empty());
  EXPECT_TRUE(face_blocks(file, 0, 4, 5, 0, RADICLE_STATUS_PARSE_ERROR).empty());
  EXPECT_TRUE(face_blocks(file, 0, 5, 1, 0, RADICLE_STATUS_PARSE_ERROR).empty());

  auto truncated = file;
  truncated.resize(truncated.size() - 4);
  EXPECT_TRUE(face_blocks(truncated, 5, 4, 4, 0, RADICLE_STATUS_PARSE_ERROR).empty());
  EXPECT_EQ(face_blocks(truncated, 4, 4, 4, 0).size(), 56U);

  const auto raw = make_dds(87, std::vector<std::uint8_t>(64, 0), 1, 4, 4);
  EXPECT_TRUE(face_blocks(raw, 0, 1, 1, 0, RADICLE_STATUS_PARSE_ERROR).empty());
  uint8_t* out = nullptr;
  size_t out_size = 0;
  EXPECT_EQ(radicle_dds_face_blocks(nullptr, 0, 0, 1, 1, 0, &out, &out_size), RADICLE_STATUS_INVALID_ARGUMENT);
}

TEST(CApiPng, EncodesRgbaIntoAPngBufferTheCallerFrees) {
  const std::uint8_t rgba[8] = {255, 0, 0, 255, 0, 255, 0, 128};
  std::uint8_t* png = nullptr;
  std::size_t size = 0;
  ASSERT_EQ(radicle_png_encode_rgba(rgba, sizeof rgba, 2, 1, &png, &size), RADICLE_STATUS_OK) << radicle_last_error();
  ASSERT_NE(png, nullptr);
  ASSERT_GT(size, 8u);
  const std::uint8_t signature[8] = {0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A};
  EXPECT_EQ(std::memcmp(png, signature, 8), 0);
  radicle_free_bytes(png);
}

TEST(CApiPng, RejectsBadArgumentsAndZeroesTheOutputs) {
  const std::uint8_t rgba[8] = {};
  std::uint8_t* png = reinterpret_cast<std::uint8_t*>(1);
  std::size_t size = 9;
  EXPECT_EQ(radicle_png_encode_rgba(rgba, sizeof rgba, 3, 1, &png, &size), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(png, nullptr);
  EXPECT_EQ(size, 0u);
  EXPECT_EQ(radicle_png_encode_rgba(rgba, sizeof rgba, 0, 2, &png, &size), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_png_encode_rgba(nullptr, sizeof rgba, 2, 1, &png, &size), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_png_encode_rgba(rgba, sizeof rgba, 2, 1, nullptr, &size), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_png_encode_rgba(rgba, sizeof rgba, 2, 1, &png, nullptr), RADICLE_STATUS_INVALID_ARGUMENT);
}
