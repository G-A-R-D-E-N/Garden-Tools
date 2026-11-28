#include "fo4/texture/png.hpp"

#include <zlib.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <stdexcept>
#include <string_view>
#include <vector>

namespace fo4::texture {
namespace {

constexpr std::array<std::uint8_t, 8> k_signature{137, 80, 78, 71, 13, 10, 26, 10};
constexpr std::uint8_t k_bit_depth = 8;
constexpr std::uint8_t k_colour_type_rgba = 6;

void put_u32(std::vector<std::uint8_t>& out, std::uint32_t value) {
  for (int shift = 24; shift >= 0; shift -= 8) out.push_back(static_cast<std::uint8_t>(value >> shift));
}

void put_chunk(std::vector<std::uint8_t>& out, std::string_view type, const std::vector<std::uint8_t>& payload) {
  put_u32(out, static_cast<std::uint32_t>(payload.size()));
  const std::size_t start = out.size();
  out.insert(out.end(), type.begin(), type.end());
  out.insert(out.end(), payload.begin(), payload.end());
  const auto crc = crc32(0L, out.data() + start, static_cast<uInt>(out.size() - start));
  put_u32(out, static_cast<std::uint32_t>(crc));
}

}  // namespace

std::vector<std::uint8_t> encode_png_rgba(std::span<const std::uint8_t> rgba, std::uint32_t width, std::uint32_t height) {
  const std::size_t row_bytes = static_cast<std::size_t>(width) * 4;
  if (width == 0 || height == 0 || rgba.size() != row_bytes * height) {
    throw std::invalid_argument("PNG: pixel data does not match the image size");
  }

  std::vector<std::uint8_t> scanlines;
  scanlines.reserve((row_bytes + 1) * height);
  for (std::size_t y = 0; y < height; ++y) {
    scanlines.push_back(0);
    const auto row = rgba.subspan(y * row_bytes, row_bytes);
    scanlines.insert(scanlines.end(), row.begin(), row.end());
  }

  uLongf packed_size = compressBound(static_cast<uLong>(scanlines.size()));
  std::vector<std::uint8_t> packed(packed_size);
  if (compress2(packed.data(), &packed_size, scanlines.data(), static_cast<uLong>(scanlines.size()), Z_BEST_SPEED) !=
      Z_OK) {
    throw std::runtime_error("PNG: zlib could not compress the image");
  }
  packed.resize(packed_size);

  std::vector<std::uint8_t> header;
  put_u32(header, width);
  put_u32(header, height);
  header.insert(header.end(), {k_bit_depth, k_colour_type_rgba, 0, 0, 0});

  std::vector<std::uint8_t> png(k_signature.begin(), k_signature.end());
  put_chunk(png, "IHDR", header);
  put_chunk(png, "IDAT", packed);
  put_chunk(png, "IEND", {});
  return png;
}

}  // namespace fo4::texture
