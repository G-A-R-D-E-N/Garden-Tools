#pragma once

#include "fo4/swf/swf.hpp"

#include <cstdint>
#include <span>

namespace fo4::swf {

enum class SwfBitmapEditStatus : std::uint8_t {
  ok,
  unknown_character,
  not_a_lossless_bitmap,
  bad_dimensions,
  pixel_size_mismatch,
  malformed_file,
  encode_failed,
};

/* Requires a DefineBitsLossless or DefineBitsLossless2 character; rgba is row-major, unpremultiplied, 4 bytes per pixel. */
SwfBitmapEditStatus replace_bitmap_lossless(SwfFile& file, std::uint16_t character_id,
                                            std::uint16_t width, std::uint16_t height,
                                            std::span<const std::uint8_t> rgba, bool has_alpha);

}  // namespace fo4::swf
