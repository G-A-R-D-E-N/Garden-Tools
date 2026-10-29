#include "fo4/swf/swf_bitmap_edit.hpp"

#include "fo4/swf/swf_characters.hpp"
#include "swf_internal.hpp"

#include <zlib.h>

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace fo4::swf {

namespace {

constexpr std::uint8_t kFormatTrueColour = 5;

constexpr std::size_t kBytesPerPixel = 4;

std::uint8_t premultiply(std::uint8_t channel, std::uint8_t alpha) {
  return static_cast<std::uint8_t>(std::nearbyint(channel * static_cast<double>(alpha) / 255.0));
}

void append_u16(std::vector<std::byte>& out, std::uint16_t value) {
  out.push_back(static_cast<std::byte>(value & 0xFFu));
  out.push_back(static_cast<std::byte>(value >> 8));
}

std::vector<std::byte> argb_bytes(std::span<const std::uint8_t> rgba, bool has_alpha) {
  std::vector<std::byte> raw;
  raw.reserve(rgba.size());
  for (std::size_t at = 0; at < rgba.size(); at += kBytesPerPixel) {
    const std::uint8_t alpha = rgba[at + 3];
    raw.push_back(static_cast<std::byte>(has_alpha ? alpha : 0));
    for (std::size_t channel = 0; channel < 3; ++channel) {
      const std::uint8_t value =
          has_alpha ? premultiply(rgba[at + channel], alpha) : rgba[at + channel];
      raw.push_back(static_cast<std::byte>(value));
    }
  }
  return raw;
}

bool zlib_pack(std::span<const std::byte> raw, std::vector<std::byte>& packed) {
  uLongf size = compressBound(static_cast<uLong>(raw.size()));
  packed.resize(static_cast<std::size_t>(size));
  if (compress2(reinterpret_cast<Bytef*>(packed.data()), &size,
                reinterpret_cast<const Bytef*>(raw.data()), static_cast<uLong>(raw.size()),
                Z_BEST_COMPRESSION) != Z_OK) {
    return false;
  }
  packed.resize(static_cast<std::size_t>(size));
  return true;
}

}  // namespace

SwfBitmapEditStatus replace_bitmap_lossless(SwfFile& file, std::uint16_t character_id,
                                            std::uint16_t width, std::uint16_t height,
                                            std::span<const std::uint8_t> rgba, bool has_alpha) {
  SwfCharacterModel model;
  std::string message;
  if (!build_character_model(file, model, message)) {
    return SwfBitmapEditStatus::malformed_file;
  }
  const SwfCharacter* character = find_character(model, character_id);
  if (character == nullptr) {
    return SwfBitmapEditStatus::unknown_character;
  }
  const auto code = file.tags[character->tag_index].code;
  if (code != tag::kDefineBitsLossless && code != tag::kDefineBitsLossless2) {
    return SwfBitmapEditStatus::not_a_lossless_bitmap;
  }
  if (width == 0 || height == 0 ||
      static_cast<std::size_t>(height) >
          detail::kMaxBitmapBytes / (static_cast<std::size_t>(width) * kBytesPerPixel)) {
    return SwfBitmapEditStatus::bad_dimensions;
  }
  const std::size_t pixel_bytes = static_cast<std::size_t>(width) * height * kBytesPerPixel;
  if (rgba.size() != pixel_bytes) {
    return SwfBitmapEditStatus::pixel_size_mismatch;
  }
  std::vector<std::byte> packed;
  if (!zlib_pack(argb_bytes(rgba, has_alpha), packed)) {
    return SwfBitmapEditStatus::encode_failed;
  }

  SwfTag replacement;
  replacement.code = has_alpha ? tag::kDefineBitsLossless2 : tag::kDefineBitsLossless;
  append_u16(replacement.payload, character_id);
  replacement.payload.push_back(static_cast<std::byte>(kFormatTrueColour));
  append_u16(replacement.payload, width);
  append_u16(replacement.payload, height);
  replacement.payload.insert(replacement.payload.end(), packed.begin(), packed.end());

  SwfTagPlan plan;
  plan.removals.push_back(SwfTagRemoval{character->tag_index});
  plan.insertions.push_back(SwfTagInsertion{character->tag_index, std::move(replacement)});
  SwfTagPlanResult applied;
  if (!apply_tag_plan(file, plan, applied)) {
    return SwfBitmapEditStatus::encode_failed;
  }
  return SwfBitmapEditStatus::ok;
}

}  // namespace fo4::swf
