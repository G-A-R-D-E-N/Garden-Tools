#include "swf_internal.hpp"

#include <bit>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace fo4::swf::detail {

namespace {

bool read_color_transform(BitReader& bits, bool alpha, SwfColorTransform& out) noexcept {
  std::uint32_t has_add = 0;
  std::uint32_t has_mult = 0;
  std::uint32_t nbits = 0;
  if (!bits.read_unsigned(1, has_add) || !bits.read_unsigned(1, has_mult) ||
      !bits.read_unsigned(4, nbits)) {
    return false;
  }
  SwfColorTransform transform;
  transform.has_alpha = alpha;
  const std::size_t channels = alpha ? 4u : 3u;
  if (has_mult != 0) {
    for (std::size_t channel = 0; channel < channels; ++channel) {
      if (!bits.read_signed(nbits, transform.multiply[channel])) {
        return false;
      }
    }
  }
  if (has_add != 0) {
    for (std::size_t channel = 0; channel < channels; ++channel) {
      if (!bits.read_signed(nbits, transform.add[channel])) {
        return false;
      }
    }
  }
  bits.align();
  out = transform;
  return true;
}

bool read_rgba(SwfReader& reader, std::uint32_t& out) {
  std::uint8_t red = 0;
  std::uint8_t green = 0;
  std::uint8_t blue = 0;
  std::uint8_t alpha = 0;
  if (!reader.read_u8(red) || !reader.read_u8(green) || !reader.read_u8(blue) ||
      !reader.read_u8(alpha)) {
    return false;
  }
  out = (static_cast<std::uint32_t>(alpha) << 24) | (static_cast<std::uint32_t>(red) << 16) |
        (static_cast<std::uint32_t>(green) << 8) | static_cast<std::uint32_t>(blue);
  return true;
}

bool read_fixed16(SwfReader& reader, double& out) {
  std::uint32_t raw = 0;
  if (!reader.read_u32(raw)) {
    return false;
  }
  out = static_cast<std::int32_t>(raw) / 65536.0;
  return true;
}

bool read_fixed8(SwfReader& reader, double& out) {
  std::int16_t raw = 0;
  if (!reader.read_s16(raw)) {
    return false;
  }
  out = raw / 256.0;
  return true;
}

bool read_float(SwfReader& reader, float& out) {
  std::uint32_t raw = 0;
  if (!reader.read_u32(raw)) {
    return false;
  }
  out = std::bit_cast<float>(raw);
  return true;
}

bool read_floats(SwfReader& reader, std::size_t count, std::vector<float>& out) {
  float value = 0.0f;
  for (std::size_t index = 0; index < count; ++index) {
    if (!read_float(reader, value)) {
      return false;
    }
    out.push_back(value);
  }
  return true;
}

bool read_blur(SwfReader& reader, SwfFilter& filter) {
  return read_fixed16(reader, filter.blur_x) && read_fixed16(reader, filter.blur_y);
}

bool read_filter_flags(SwfReader& reader, SwfFilter& filter, bool has_on_top) {
  std::uint8_t flags = 0;
  if (!reader.read_u8(flags)) {
    return false;
  }
  filter.inner = (flags & 0x80u) != 0;
  filter.knockout = (flags & 0x40u) != 0;
  filter.composite_source = (flags & 0x20u) != 0;
  filter.on_top = has_on_top && (flags & 0x10u) != 0;
  filter.passes = static_cast<std::uint8_t>(flags & (has_on_top ? 0x0Fu : 0x1Fu));
  return true;
}

bool read_gradient(SwfReader& reader, SwfFilter& filter) {
  std::uint8_t stops = 0;
  if (!reader.read_u8(stops)) {
    return false;
  }
  for (std::uint8_t index = 0; index < stops; ++index) {
    std::uint32_t color = 0;
    if (!read_rgba(reader, color)) {
      return false;
    }
    filter.gradient_colors.push_back(color);
  }
  for (std::uint8_t index = 0; index < stops; ++index) {
    std::uint8_t ratio = 0;
    if (!reader.read_u8(ratio)) {
      return false;
    }
    filter.gradient_ratios.push_back(ratio);
  }
  return read_blur(reader, filter) && read_fixed16(reader, filter.angle) &&
         read_fixed16(reader, filter.distance) && read_fixed8(reader, filter.strength) &&
         read_filter_flags(reader, filter, true);
}

bool read_convolution(SwfReader& reader, SwfFilter& filter) {
  if (!reader.read_u8(filter.matrix_columns) || !reader.read_u8(filter.matrix_rows) ||
      !read_float(reader, filter.divisor) || !read_float(reader, filter.bias)) {
    return false;
  }
  const std::size_t cells = std::size_t{filter.matrix_columns} * filter.matrix_rows;
  if (!read_floats(reader, cells, filter.matrix)) {
    return false;
  }
  std::uint8_t flags = 0;
  if (!read_rgba(reader, filter.default_color) || !reader.read_u8(flags)) {
    return false;
  }
  filter.clamp = (flags & 0x02u) != 0;
  filter.preserve_alpha = (flags & 0x01u) != 0;
  return true;
}

bool read_filter(SwfReader& reader, SwfFilter& filter) {
  if (!reader.read_u8(filter.id)) {
    return false;
  }
  switch (filter.id) {
    case 0:
      return read_rgba(reader, filter.color) && read_blur(reader, filter) &&
             read_fixed16(reader, filter.angle) && read_fixed16(reader, filter.distance) &&
             read_fixed8(reader, filter.strength) && read_filter_flags(reader, filter, false);
    case 1: {
      std::uint8_t flags = 0;
      if (!read_blur(reader, filter) || !reader.read_u8(flags)) {
        return false;
      }
      filter.passes = static_cast<std::uint8_t>(flags >> 3);
      return true;
    }
    case 2:
      return read_rgba(reader, filter.color) && read_blur(reader, filter) &&
             read_fixed8(reader, filter.strength) && read_filter_flags(reader, filter, false);
    case 3:
      return read_rgba(reader, filter.color) && read_rgba(reader, filter.highlight_color) &&
             read_blur(reader, filter) && read_fixed16(reader, filter.angle) &&
             read_fixed16(reader, filter.distance) && read_fixed8(reader, filter.strength) &&
             read_filter_flags(reader, filter, true);
    case 4:
    case 7:
      return read_gradient(reader, filter);
    case 5:
      return read_convolution(reader, filter);
    case 6:
      return read_floats(reader, 20, filter.matrix);
    default:
      return false;
  }
}

bool decode_filter_list(SwfReader& reader, std::vector<SwfFilter>& filters) {
  std::uint8_t count = 0;
  if (!reader.read_u8(count)) {
    return false;
  }
  for (std::uint8_t index = 0; index < count; ++index) {
    SwfFilter filter;
    if (!read_filter(reader, filter)) {
      return false;
    }
    filters.push_back(std::move(filter));
  }
  return true;
}

SwfStatus decode_place_object(std::span<const std::byte> data, SwfPlacement& place) {
  SwfReader reader(data, 0);
  std::uint16_t character = 0;
  if (!reader.read_u16(character) || !reader.read_u16(place.depth)) {
    return SwfStatus::malformed_tag;
  }
  place.character_id = character;
  BitReader bits(data, reader.position() * 8);
  SwfMatrix matrix;
  if (!read_matrix(bits, matrix)) {
    return SwfStatus::malformed_tag;
  }
  place.matrix = matrix;
  if (bits.byte_position() < data.size()) {
    SwfColorTransform transform;
    if (!read_color_transform(bits, false, transform)) {
      return SwfStatus::malformed_tag;
    }
    place.color_transform = transform;
  }
  return SwfStatus::ok;
}

SwfStatus decode_place_object2(std::span<const std::byte> data, bool version3,
                               SwfPlacement& place) {
  SwfReader reader(data, 0);
  std::uint8_t flags = 0;
  std::uint8_t flags3 = 0;
  if (!reader.read_u8(flags) || (version3 && !reader.read_u8(flags3))) {
    return SwfStatus::malformed_tag;
  }
  const bool has_character = (flags & 0x02u) != 0;
  const bool has_matrix = (flags & 0x04u) != 0;
  const bool has_color = (flags & 0x08u) != 0;
  const bool has_ratio = (flags & 0x10u) != 0;
  const bool has_name = (flags & 0x20u) != 0;
  const bool has_clip_depth = (flags & 0x40u) != 0;
  place.move = (flags & 0x01u) != 0;
  place.has_clip_actions = (flags & 0x80u) != 0;
  const bool has_image = (flags3 & 0x10u) != 0;
  const bool has_class_name = (flags3 & 0x08u) != 0 || (has_image && !has_character);
  const bool has_cache = (flags3 & 0x04u) != 0;
  const bool has_blend = (flags3 & 0x02u) != 0;
  const bool has_filters = (flags3 & 0x01u) != 0;
  const bool has_visible = (flags3 & 0x20u) != 0;
  const bool has_opaque_background = (flags3 & 0x40u) != 0;

  if (!reader.read_u16(place.depth)) {
    return SwfStatus::malformed_tag;
  }
  if (has_class_name) {
    std::string class_name;
    if (!reader.read_zstring(class_name)) {
      return SwfStatus::malformed_tag;
    }
    place.class_name = std::move(class_name);
  }
  if (has_character) {
    std::uint16_t character = 0;
    if (!reader.read_u16(character)) {
      return SwfStatus::malformed_tag;
    }
    place.character_id = character;
  }
  if (has_matrix) {
    BitReader bits(data, reader.position() * 8);
    SwfMatrix matrix;
    if (!read_matrix(bits, matrix)) {
      return SwfStatus::malformed_tag;
    }
    place.matrix = matrix;
    reader = SwfReader(data, bits.byte_position());
  }
  if (has_color) {
    BitReader bits(data, reader.position() * 8);
    SwfColorTransform transform;
    if (!read_color_transform(bits, true, transform)) {
      return SwfStatus::malformed_tag;
    }
    place.color_transform = transform;
    reader = SwfReader(data, bits.byte_position());
  }
  if (has_ratio) {
    std::uint16_t ratio = 0;
    if (!reader.read_u16(ratio)) {
      return SwfStatus::malformed_tag;
    }
    place.ratio = ratio;
  }
  if (has_name) {
    std::string name;
    if (!reader.read_zstring(name)) {
      return SwfStatus::malformed_tag;
    }
    place.name = std::move(name);
  }
  if (has_clip_depth) {
    std::uint16_t clip_depth = 0;
    if (!reader.read_u16(clip_depth)) {
      return SwfStatus::malformed_tag;
    }
    place.clip_depth = clip_depth;
  }
  if (!version3) {
    return SwfStatus::ok;
  }
  if (has_filters && !decode_filter_list(reader, place.filters)) {
    return SwfStatus::malformed_tag;
  }
  if (has_blend) {
    std::uint8_t blend = 0;
    if (!reader.read_u8(blend)) {
      return SwfStatus::malformed_tag;
    }
    place.blend_mode = blend;
  }
  if (has_cache) {
    std::uint8_t cache = 0;
    if (!reader.read_u8(cache)) {
      return SwfStatus::malformed_tag;
    }
    place.bitmap_cache = cache;
  }
  if (has_visible) {
    std::uint8_t visible = 0;
    if (!reader.read_u8(visible)) {
      return SwfStatus::malformed_tag;
    }
    place.visible = visible != 0;
  }
  if (has_opaque_background) {
    std::uint32_t color = 0;
    if (!read_rgba(reader, color)) {
      return SwfStatus::malformed_tag;
    }
    place.background_color = color;
  }
  return SwfStatus::ok;
}

SwfStatus decode_remove_object(std::span<const std::byte> data, SwfTag& out_tag) {
  SwfReader reader(data, 0);
  SwfRemoval removal;
  if (out_tag.code == tag::kRemoveObject) {
    std::uint16_t character = 0;
    if (!reader.read_u16(character)) {
      return SwfStatus::malformed_tag;
    }
    removal.character_id = character;
  }
  if (!reader.read_u16(removal.depth)) {
    return SwfStatus::malformed_tag;
  }
  out_tag.removal = removal;
  return SwfStatus::ok;
}

}  // namespace

SwfStatus decode_placement_tag(std::span<const std::byte> data, SwfTag& out_tag) {
  if (out_tag.code == tag::kRemoveObject || out_tag.code == tag::kRemoveObject2) {
    return decode_remove_object(data, out_tag);
  }
  SwfPlacement place;
  SwfStatus status = SwfStatus::malformed_tag;
  if (out_tag.code == tag::kPlaceObject) {
    status = decode_place_object(data, place);
  } else {
    status = decode_place_object2(data, out_tag.code == tag::kPlaceObject3, place);
  }
  if (status == SwfStatus::ok) {
    out_tag.placement = std::move(place);
  }
  return status;
}

}  // namespace fo4::swf::detail
