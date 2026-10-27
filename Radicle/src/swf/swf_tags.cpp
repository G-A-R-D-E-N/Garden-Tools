#include "swf_internal.hpp"

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace fo4::swf::detail {

std::uint8_t byte_at(std::span<const std::byte> bytes, std::size_t index) noexcept {
  if (index >= bytes.size()) {
    return 0;
  }
  return std::to_integer<std::uint8_t>(bytes[index]);
}

std::string to_text(std::span<const std::byte> bytes) {
  if (bytes.empty()) {
    return std::string{};
  }
  return std::string(reinterpret_cast<const char*>(bytes.data()), bytes.size());
}

bool SwfReader::read_u8(std::uint8_t& out) noexcept {
  if (remaining() < 1) {
    return false;
  }
  out = byte_at(data_, position_);
  ++position_;
  return true;
}

bool SwfReader::read_s16(std::int16_t& out) noexcept {
  std::uint16_t value = 0;
  if (!read_u16(value)) {
    return false;
  }
  out = static_cast<std::int16_t>(value);
  return true;
}

bool SwfReader::read_u16(std::uint16_t& out) noexcept {
  if (remaining() < 2) {
    return false;
  }
  out = static_cast<std::uint16_t>(
      static_cast<std::uint16_t>(byte_at(data_, position_)) |
      static_cast<std::uint16_t>(static_cast<std::uint16_t>(byte_at(data_, position_ + 1)) << 8));
  position_ += 2;
  return true;
}

bool SwfReader::read_u32(std::uint32_t& out) noexcept {
  if (remaining() < 4) {
    return false;
  }
  out = static_cast<std::uint32_t>(byte_at(data_, position_)) |
        (static_cast<std::uint32_t>(byte_at(data_, position_ + 1)) << 8) |
        (static_cast<std::uint32_t>(byte_at(data_, position_ + 2)) << 16) |
        (static_cast<std::uint32_t>(byte_at(data_, position_ + 3)) << 24);
  position_ += 4;
  return true;
}

bool SwfReader::read_zstring(std::string& out) {
  std::size_t end = position_;
  while (end < data_.size() && byte_at(data_, end) != 0) {
    ++end;
  }
  if (end >= data_.size()) {
    return false;
  }
  out.assign(reinterpret_cast<const char*>(data_.data() + position_), end - position_);
  position_ = end + 1;
  return true;
}

bool SwfReader::read_sized_string(std::string& out) {
  std::uint8_t length = 0;
  if (!read_u8(length)) {
    return false;
  }
  if (remaining() < static_cast<std::size_t>(length)) {
    return false;
  }
  if (length == 0) {
    out.clear();
    return true;
  }
  out.assign(reinterpret_cast<const char*>(data_.data() + position_), length);
  position_ += length;
  return true;
}

bool SwfReader::skip(std::size_t count) noexcept {
  if (remaining() < count) {
    return false;
  }
  position_ += count;
  return true;
}

bool read_rect(std::span<const std::byte> data, std::size_t offset, SwfRect& out,
               std::size_t& next) noexcept {
  BitReader bits(data, offset * 8);
  std::uint32_t nbits = 0;
  std::int32_t x_min = 0;
  std::int32_t x_max = 0;
  std::int32_t y_min = 0;
  std::int32_t y_max = 0;
  if (!bits.read_unsigned(5, nbits) || !bits.read_signed(nbits, x_min) ||
      !bits.read_signed(nbits, x_max) || !bits.read_signed(nbits, y_min) ||
      !bits.read_signed(nbits, y_max)) {
    return false;
  }
  out.x_min = x_min;
  out.x_max = x_max;
  out.y_min = y_min;
  out.y_max = y_max;
  out.nbits = static_cast<std::uint8_t>(nbits);
  const auto pad_bits = static_cast<unsigned>((8u - bits.bit_position() % 8u) % 8u);
  out.padding = static_cast<std::uint8_t>(byte_at(data, bits.byte_position() - 1u) &
                                          ((1u << pad_bits) - 1u));
  next = bits.byte_position();
  return true;
}

bool BitReader::read_unsigned(unsigned count, std::uint32_t& out) noexcept {
  if (count > 32 || bit_position_ + count > data_.size() * 8) {
    return false;
  }
  std::uint32_t value = 0;
  for (unsigned index = 0; index < count; ++index) {
    const auto absolute = bit_position_ + index;
    const auto byte_value = byte_at(data_, absolute / 8);
    const auto bit = static_cast<std::uint32_t>(
        (byte_value >> (7u - static_cast<unsigned>(absolute % 8))) & 1u);
    value = (value << 1) | bit;
  }
  bit_position_ += count;
  out = value;
  return true;
}

bool BitReader::read_signed(unsigned count, std::int32_t& out) noexcept {
  std::uint32_t raw = 0;
  if (!read_unsigned(count, raw)) {
    return false;
  }
  if (count > 0 && count < 32 && (raw & (1u << (count - 1))) != 0) {
    raw |= ~((1u << count) - 1u);
  }
  out = static_cast<std::int32_t>(raw);
  return true;
}

bool read_matrix(BitReader& bits, SwfMatrix& out) noexcept {
  SwfMatrix matrix;
  std::uint32_t flag = 0;
  std::uint32_t nbits = 0;
  if (!bits.read_unsigned(1, flag)) {
    return false;
  }
  if (flag != 0) {
    if (!bits.read_unsigned(5, nbits) || !bits.read_signed(nbits, matrix.scale_x) ||
        !bits.read_signed(nbits, matrix.scale_y)) {
      return false;
    }
  }
  if (!bits.read_unsigned(1, flag)) {
    return false;
  }
  if (flag != 0) {
    if (!bits.read_unsigned(5, nbits) || !bits.read_signed(nbits, matrix.rotate_skew0) ||
        !bits.read_signed(nbits, matrix.rotate_skew1)) {
      return false;
    }
  }
  if (!bits.read_unsigned(5, nbits) || !bits.read_signed(nbits, matrix.translate_x) ||
      !bits.read_signed(nbits, matrix.translate_y)) {
    return false;
  }
  bits.align();
  out = matrix;
  return true;
}

namespace {

bool decode_symbol_list(std::span<const std::byte> payload, std::vector<SwfSymbol>& out) {
  SwfReader reader(payload, 0);
  std::uint16_t count = 0;
  if (!reader.read_u16(count)) {
    return false;
  }
  std::vector<SwfSymbol> symbols;
  symbols.reserve(count);
  for (std::uint16_t index = 0; index < count; ++index) {
    SwfSymbol symbol;
    if (!reader.read_u16(symbol.tag_id) || !reader.read_zstring(symbol.name)) {
      return false;
    }
    symbols.push_back(std::move(symbol));
  }
  out = std::move(symbols);
  return true;
}

}  // namespace

bool decode_edit_text(std::span<const std::byte> data, SwfEditText& out) {
  SwfReader head(data, 0);
  if (!head.read_u16(out.character_id)) {
    return false;
  }
  std::size_t position = 0;
  if (!read_rect(data, 2, out.bounds, position)) {
    return false;
  }
  SwfReader reader(data, position);
  if (!reader.read_u8(out.flags1) || !reader.read_u8(out.flags2)) {
    return false;
  }
  if ((out.flags1 & 0x01u) != 0) {
    std::uint16_t font_id = 0;
    if (!reader.read_u16(font_id)) {
      return false;
    }
    out.font_id = font_id;
  }
  if ((out.flags2 & 0x80u) != 0 && !reader.read_zstring(out.font_class)) {
    return false;
  }
  if ((out.flags1 & 0x01u) != 0 || (out.flags2 & 0x80u) != 0) {
    std::uint16_t height = 0;
    if (!reader.read_u16(height)) {
      return false;
    }
    out.font_height_twips = height;
  }
  if ((out.flags1 & 0x04u) != 0) {
    std::uint8_t red = 0;
    std::uint8_t green = 0;
    std::uint8_t blue = 0;
    std::uint8_t alpha = 0;
    if (!reader.read_u8(red) || !reader.read_u8(green) || !reader.read_u8(blue) ||
        !reader.read_u8(alpha)) {
      return false;
    }
    out.color = (static_cast<std::uint32_t>(alpha) << 24) |
                (static_cast<std::uint32_t>(red) << 16) |
                (static_cast<std::uint32_t>(green) << 8) | static_cast<std::uint32_t>(blue);
  }
  if ((out.flags1 & 0x02u) != 0) {
    std::uint16_t max_length = 0;
    if (!reader.read_u16(max_length)) {
      return false;
    }
    out.max_length = max_length;
  }
  if ((out.flags2 & 0x20u) != 0) {
    SwfEditTextLayout layout;
    std::uint16_t leading = 0;
    if (!reader.read_u8(layout.align) || !reader.read_u16(layout.left_margin) ||
        !reader.read_u16(layout.right_margin) || !reader.read_u16(layout.indent) ||
        !reader.read_u16(leading)) {
      return false;
    }
    layout.leading = static_cast<std::int16_t>(leading);
    out.layout = layout;
  }
  if (!reader.read_zstring(out.variable)) {
    return false;
  }
  if ((out.flags1 & 0x80u) != 0 && !reader.read_zstring(out.text)) {
    return false;
  }
  return true;
}

SwfStatus decode_tag_payload(const std::vector<std::byte>& payload, SwfTag& out_tag,
                             unsigned depth, std::size_t& tags_left) {
  const std::span<const std::byte> data(payload);
  switch (out_tag.code) {
    case tag::kFileAttributes: {
      if (payload.size() < 4) {
        return SwfStatus::malformed_tag;
      }
      const auto raw = static_cast<std::uint32_t>(byte_at(data, 0)) |
                       (static_cast<std::uint32_t>(byte_at(data, 1)) << 8) |
                       (static_cast<std::uint32_t>(byte_at(data, 2)) << 16) |
                       (static_cast<std::uint32_t>(byte_at(data, 3)) << 24);
      SwfFileAttributes attributes;
      attributes.raw = raw;
      attributes.use_network = (raw & 0x01u) != 0;
      attributes.action_script3 = (raw & 0x08u) != 0;
      attributes.has_metadata = (raw & 0x10u) != 0;
      attributes.use_gpu = (raw & 0x20u) != 0;
      attributes.use_direct_blit = (raw & 0x40u) != 0;
      out_tag.attributes = attributes;
      return SwfStatus::ok;
    }
    case tag::kSetBackgroundColor: {
      if (payload.size() < 3) {
        return SwfStatus::malformed_tag;
      }
      const auto rgb = (static_cast<std::uint32_t>(byte_at(data, 0)) << 16) |
                       (static_cast<std::uint32_t>(byte_at(data, 1)) << 8) |
                       static_cast<std::uint32_t>(byte_at(data, 2));
      const auto alpha =
          payload.size() >= 4 ? static_cast<std::uint32_t>(byte_at(data, 3)) : std::uint32_t{0};
      out_tag.background_color = (alpha << 24) | rgb;
      return SwfStatus::ok;
    }
    case tag::kMetadata: {
      std::size_t length = 0;
      while (length < payload.size() && byte_at(data, length) != 0) {
        ++length;
      }
      out_tag.metadata = to_text(data.first(length));
      return SwfStatus::ok;
    }
    case tag::kScriptLimits: {
      SwfReader reader(data, 0);
      SwfScriptLimits limits;
      if (!reader.read_u16(limits.max_recursion_depth) ||
          !reader.read_u16(limits.script_timeout_seconds)) {
        return SwfStatus::malformed_tag;
      }
      out_tag.script_limits = limits;
      return SwfStatus::ok;
    }
    case tag::kDefineSprite: {
      SwfReader reader(data, 0);
      std::uint16_t sprite_id = 0;
      std::uint16_t frame_count = 0;
      if (!reader.read_u16(sprite_id) || !reader.read_u16(frame_count)) {
        return SwfStatus::malformed_tag;
      }
      out_tag.sprite_id = sprite_id;
      out_tag.sprite_frame_count = frame_count;
      if (depth >= kMaxTagDepth) {
        return SwfStatus::ok;
      }
      std::size_t stop = 0;
      const auto nested = decode_tag_stream(data, reader.position(), data.size(), depth + 1,
                                            out_tag.children, stop, tags_left);
      if (nested == SwfStatus::too_many_tags) {
        return nested;
      }
      return nested == SwfStatus::ok ? SwfStatus::ok : SwfStatus::malformed_tag;
    }
    case tag::kImportAssets:
    case tag::kImportAssets2: {
      SwfReader reader(data, 0);
      SwfImportAssets imports;
      imports.version = out_tag.code == tag::kImportAssets2 ? 2 : 1;
      if (!reader.read_zstring(imports.source_movie)) {
        return SwfStatus::malformed_tag;
      }
      if (imports.version == 2) {
        std::uint8_t reserved = 0;
        std::uint8_t reserved_again = 0;
        if (!reader.read_u8(reserved) || !reader.read_u8(reserved_again)) {
          return SwfStatus::malformed_tag;
        }
      }
      if (!decode_symbol_list(data.subspan(reader.position()), imports.symbols)) {
        return SwfStatus::malformed_tag;
      }
      out_tag.imports = std::move(imports);
      return SwfStatus::ok;
    }
    case tag::kSymbolClass:
    case tag::kExportAssets: {
      std::vector<SwfSymbol> symbols;
      if (!decode_symbol_list(data, symbols)) {
        return SwfStatus::malformed_tag;
      }
      if (out_tag.code == tag::kSymbolClass) {
        out_tag.symbols = std::move(symbols);
      } else {
        out_tag.exports = std::move(symbols);
      }
      return SwfStatus::ok;
    }
    case tag::kDoAbc: {
      SwfAbc abc;
      abc.payload_size = payload.size();
      out_tag.abc = abc;
      return SwfStatus::ok;
    }
    case tag::kDoAbc2: {
      SwfReader reader(data, 0);
      SwfAbc abc;
      if (!reader.read_u32(abc.flags) || !reader.read_zstring(abc.name)) {
        return SwfStatus::malformed_tag;
      }
      abc.payload_offset = reader.position();
      abc.payload_size = reader.remaining();
      out_tag.abc = abc;
      return SwfStatus::ok;
    }
    case tag::kDoAction: {
      SwfAction action;
      action.offset = 0;
      action.size = payload.size();
      out_tag.action = action;
      return SwfStatus::ok;
    }
    case tag::kDoInitAction: {
      SwfReader reader(data, 0);
      SwfAction action;
      if (!reader.read_u16(action.sprite_id)) {
        return SwfStatus::malformed_tag;
      }
      action.offset = reader.position();
      action.size = reader.remaining();
      out_tag.action = action;
      return SwfStatus::ok;
    }
    case tag::kDefineBinaryData: {
      SwfReader reader(data, 0);
      SwfBinaryData binary;
      if (!reader.read_u16(binary.tag_id) || !reader.read_u32(binary.reserved)) {
        return SwfStatus::malformed_tag;
      }
      binary.offset = reader.position();
      binary.size = reader.remaining();
      out_tag.binary_data = binary;
      return SwfStatus::ok;
    }
    case tag::kPlaceObject:
    case tag::kPlaceObject2:
    case tag::kPlaceObject3:
    case tag::kRemoveObject:
    case tag::kRemoveObject2:
      return decode_placement_tag(data, out_tag);
    case tag::kDefineShape:
    case tag::kDefineShape2:
    case tag::kDefineShape3:
    case tag::kDefineShape4:
      return decode_shape_tag(data, out_tag);
    case tag::kDefineBitsLossless:
    case tag::kDefineBitsLossless2:
      return decode_bitmap_tag(data, out_tag);
    case tag::kDefineBits:
    case tag::kJpegTables:
    case tag::kDefineBitsJpeg2:
    case tag::kDefineBitsJpeg3:
    case tag::kDefineBitsJpeg4:
      return decode_jpeg_tag(data, out_tag);
    case tag::kDefineMorphShape:
    case tag::kDefineMorphShape2:
      return decode_morph_tag(data, out_tag);
    case tag::kDefineFont:
    case tag::kDefineFont2:
    case tag::kDefineFont3:
    case tag::kDefineText:
    case tag::kDefineText2:
      return decode_text_tag(data, out_tag);
    case tag::kDefineEditText: {
      SwfEditText edit;
      if (!decode_edit_text(data, edit)) {
        return SwfStatus::malformed_tag;
      }
      out_tag.edit_text = std::move(edit);
      return SwfStatus::ok;
    }
    case tag::kDefineSound: {
      SwfReader reader(data, 0);
      SwfSound sound;
      std::uint8_t flags = 0;
      if (!reader.read_u16(sound.sound_id) || !reader.read_u8(flags) ||
          !reader.read_u32(sound.sample_count)) {
        return SwfStatus::malformed_tag;
      }
      sound.format = static_cast<std::uint8_t>(flags >> 4);
      sound.rate = static_cast<std::uint8_t>((flags >> 2) & 0x03u);
      sound.sample_16bit = (flags & 0x02u) != 0;
      sound.stereo = (flags & 0x01u) != 0;
      sound.data_size = reader.remaining();
      out_tag.sound = sound;
      return SwfStatus::ok;
    }
    case tag::kExporterInfo: {
      SwfReader reader(data, 0);
      SwfExporterInfo info;
      std::uint16_t code_offsets = 0;
      if (!reader.read_u16(info.version)) {
        return SwfStatus::malformed_tag;
      }
      if (info.version >= 0x10A && !reader.read_u32(info.flags)) {
        return SwfStatus::malformed_tag;
      }
      if (!reader.read_u16(info.bitmap_format) || !reader.read_sized_string(info.prefix) ||
          !reader.read_sized_string(info.swf_name)) {
        return SwfStatus::malformed_tag;
      }
      if (info.version >= 0x401) {
        if (!reader.read_u16(code_offsets) ||
            !reader.skip(static_cast<std::size_t>(code_offsets) * 4u)) {
          return SwfStatus::malformed_tag;
        }
        info.code_offset_count = code_offsets;
      }
      out_tag.exporter_info = info;
      return SwfStatus::ok;
    }
    case tag::kDefineExternalImage:
    case tag::kDefineExternalImageV4: {
      SwfReader reader(data, 0);
      SwfExternalImage image;
      std::uint16_t character_id = 0;
      if (out_tag.code == tag::kDefineExternalImageV4) {
        if (!reader.read_u32(image.character_id)) {
          return SwfStatus::malformed_tag;
        }
      } else {
        if (!reader.read_u16(character_id)) {
          return SwfStatus::malformed_tag;
        }
        image.character_id = character_id;
      }
      if (!reader.read_u16(image.bitmap_format) || !reader.read_u16(image.target_width) ||
          !reader.read_u16(image.target_height) ||
          !reader.read_sized_string(image.export_name) ||
          !reader.read_sized_string(image.file_name)) {
        return SwfStatus::malformed_tag;
      }
      out_tag.external_image = image;
      return SwfStatus::ok;
    }
    case tag::kFontTextureInfo: {
      SwfReader reader(data, 0);
      SwfFontTextureInfo texture;
      std::uint16_t texture_glyphs = 0;
      std::uint16_t fonts = 0;
      if (!reader.read_u32(texture.texture_id) || !reader.read_u16(texture.texture_format) ||
          !reader.read_sized_string(texture.file_name) ||
          !reader.read_u16(texture.texture_width) ||
          !reader.read_u16(texture.texture_height) || !reader.read_u8(texture.pad_pixels) ||
          !reader.read_u16(texture.nominal_glyph_size) || !reader.read_u16(texture_glyphs) ||
          !reader.skip(static_cast<std::size_t>(texture_glyphs) * 24u) ||
          !reader.read_u16(fonts)) {
        return SwfStatus::malformed_tag;
      }
      for (std::uint16_t index = 0; index < fonts; ++index) {
        std::uint16_t font_id = 0;
        std::uint16_t glyph_count = 0;
        if (!reader.read_u16(font_id) || !reader.read_u16(glyph_count) ||
            !reader.skip(static_cast<std::size_t>(glyph_count) * 4u)) {
          return SwfStatus::malformed_tag;
        }
      }
      texture.texture_glyph_count = texture_glyphs;
      texture.font_count = fonts;
      out_tag.font_texture = texture;
      return SwfStatus::ok;
    }
    default:
      return SwfStatus::ok;
  }
}

SwfStatus decode_tag_stream(std::span<const std::byte> data, std::size_t begin, std::size_t end,
                            unsigned depth, std::vector<SwfTag>& out, std::size_t& stop,
                            std::size_t& tags_left) {
  std::size_t position = begin;
  bool malformed = false;
  while (position < end) {
    const auto tag_start = position;
    if (end - position < 2) {
      stop = position;
      return SwfStatus::truncated_tag;
    }
    const auto header = static_cast<std::uint16_t>(
        static_cast<std::uint16_t>(byte_at(data, position)) |
        static_cast<std::uint16_t>(static_cast<std::uint16_t>(byte_at(data, position + 1)) << 8));
    position += 2;

    SwfTag current;
    current.code = static_cast<std::uint16_t>(header >> 6);
    std::uint32_t length = static_cast<std::uint32_t>(header & 0x3Fu);
    if (length == 0x3Fu) {
      if (end - position < 4) {
        stop = position;
        return SwfStatus::truncated_tag;
      }
      length = static_cast<std::uint32_t>(byte_at(data, position)) |
               (static_cast<std::uint32_t>(byte_at(data, position + 1)) << 8) |
               (static_cast<std::uint32_t>(byte_at(data, position + 2)) << 16) |
               (static_cast<std::uint32_t>(byte_at(data, position + 3)) << 24);
      position += 4;
      current.long_header = true;
    }
    if (tags_left == 0) {
      stop = tag_start;
      return SwfStatus::too_many_tags;
    }
    tags_left -= 1;
    current.length = length;
    current.offset = position;

    if (static_cast<std::size_t>(length) > end - position) {
      current.payload.assign(data.begin() + static_cast<std::ptrdiff_t>(position),
                             data.begin() + static_cast<std::ptrdiff_t>(end));
      out.push_back(std::move(current));
      stop = end;
      return SwfStatus::truncated_tag;
    }
    current.payload.assign(data.begin() + static_cast<std::ptrdiff_t>(position),
                           data.begin() + static_cast<std::ptrdiff_t>(position + length));
    position += length;

    const auto payload_status = decode_tag_payload(current.payload, current, depth, tags_left);
    if (payload_status == SwfStatus::too_many_tags) {
      stop = tag_start;
      return payload_status;
    }
    if (payload_status != SwfStatus::ok) {
      malformed = true;
    }
    const bool is_end = current.code == tag::kEnd;
    out.push_back(std::move(current));
    if (is_end) {
      stop = position;
      return malformed ? SwfStatus::malformed_tag : SwfStatus::ok;
    }
  }
  stop = position;
  return malformed ? SwfStatus::malformed_tag : SwfStatus::ok;
}

}  // namespace fo4::swf::detail
