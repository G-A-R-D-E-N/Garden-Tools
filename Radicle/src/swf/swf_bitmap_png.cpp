#include "fo4/swf/swf_bitmap_png.hpp"

#include <zlib.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

namespace fo4::swf {

namespace {

constexpr std::uint8_t kSignature[] = {137, 80, 78, 71, 13, 10, 26, 10};

constexpr std::uint8_t kOpaque = 255;

void append_u32(std::vector<std::uint8_t>& out, std::uint32_t value) {
  out.push_back(static_cast<std::uint8_t>(value >> 24));
  out.push_back(static_cast<std::uint8_t>(value >> 16));
  out.push_back(static_cast<std::uint8_t>(value >> 8));
  out.push_back(static_cast<std::uint8_t>(value));
}

void append_chunk(std::vector<std::uint8_t>& out, std::string_view type,
                  std::span<const std::uint8_t> payload) {
  auto crc = crc32(0L, reinterpret_cast<const Bytef*>(type.data()), static_cast<uInt>(type.size()));
  crc = crc32(crc, payload.data(), static_cast<uInt>(payload.size()));
  append_u32(out, static_cast<std::uint32_t>(payload.size()));
  out.insert(out.end(), type.begin(), type.end());
  out.insert(out.end(), payload.begin(), payload.end());
  append_u32(out, static_cast<std::uint32_t>(crc));
}

std::uint8_t straight(std::uint8_t channel, std::uint8_t alpha) {
  if (alpha == 0) {
    return channel;
  }
  return static_cast<std::uint8_t>(std::min(255.0, std::nearbyint(channel * 255.0 / alpha)));
}

void put_pixel(std::uint8_t* out, std::uint8_t red, std::uint8_t green, std::uint8_t blue,
               std::uint8_t alpha) {
  out[0] = straight(red, alpha);
  out[1] = straight(green, alpha);
  out[2] = straight(blue, alpha);
  out[3] = alpha;
}

}  // namespace

std::optional<std::vector<std::uint8_t>> bitmap_to_png(const SwfBitmap& bitmap) {
  if (bitmap.format != 3 && bitmap.format != 5) {
    return std::nullopt;
  }
  const std::size_t width = bitmap.width;
  const std::size_t height = bitmap.height;
  if (width == 0 || height == 0) {
    return std::nullopt;
  }
  const std::size_t entry_bytes = bitmap.has_alpha ? 4 : 3;
  const std::size_t pixel_bytes = bitmap.format == 3 ? 1 : 4;
  const std::size_t row = (width * pixel_bytes + 3) & ~std::size_t{3};
  std::size_t palette_entries = 0;
  if (bitmap.format == 3) {
    if (!bitmap.color_table_size) {
      return std::nullopt;
    }
    palette_entries = *bitmap.color_table_size + 1u;
  }
  const std::size_t palette_bytes = palette_entries * entry_bytes;
  if (bitmap.pixels.size() != palette_bytes + row * height) {
    return std::nullopt;
  }
  const auto at = [&bitmap](std::size_t offset) {
    return std::to_integer<std::uint8_t>(bitmap.pixels[offset]);
  };

  const std::size_t stride = 1 + width * 4;
  std::vector<std::uint8_t> scanlines(stride * height, 0);
  for (std::size_t y = 0; y < height; ++y) {
    const std::size_t source = palette_bytes + y * row;
    std::uint8_t* out = scanlines.data() + y * stride + 1;
    for (std::size_t x = 0; x < width; ++x) {
      std::uint8_t* pixel = out + x * 4;
      if (bitmap.format == 3) {
        const std::size_t index = at(source + x);
        if (index >= palette_entries) {
          return std::nullopt;
        }
        const std::size_t entry = index * entry_bytes;
        put_pixel(pixel, at(entry), at(entry + 1), at(entry + 2),
                  bitmap.has_alpha ? at(entry + 3) : kOpaque);
      } else {
        const std::size_t argb = source + x * 4;
        put_pixel(pixel, at(argb + 1), at(argb + 2), at(argb + 3),
                  bitmap.has_alpha ? at(argb) : kOpaque);
      }
    }
  }

  std::vector<std::uint8_t> header;
  append_u32(header, static_cast<std::uint32_t>(width));
  append_u32(header, static_cast<std::uint32_t>(height));
  header.insert(header.end(), {8, 6, 0, 0, 0});

  uLongf packed_size = compressBound(static_cast<uLong>(scanlines.size()));
  std::vector<std::uint8_t> packed(packed_size);
  if (compress2(packed.data(), &packed_size, scanlines.data(),
                static_cast<uLong>(scanlines.size()), Z_BEST_COMPRESSION) != Z_OK) {
    return std::nullopt;
  }
  packed.resize(packed_size);

  std::vector<std::uint8_t> png(std::begin(kSignature), std::end(kSignature));
  append_chunk(png, "IHDR", header);
  append_chunk(png, "IDAT", packed);
  append_chunk(png, "IEND", {});
  return png;
}

}  // namespace fo4::swf
