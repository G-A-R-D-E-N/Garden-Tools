#pragma once

#include <fo4/swf/swf.hpp>

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace fo4::swf::detail {

inline constexpr unsigned kMaxTagDepth = 64;

inline constexpr std::size_t kMaxBodyBytes = 256u * 1024u * 1024u;

inline constexpr std::size_t kMaxTags = 1u << 18;

class SwfReader {
public:
  SwfReader(std::span<const std::byte> data, std::size_t start) noexcept
      : data_(data), position_(start) {}

  std::size_t position() const noexcept { return position_; }
  std::size_t remaining() const noexcept { return data_.size() - position_; }

  bool read_u8(std::uint8_t& out) noexcept;
  bool read_u16(std::uint16_t& out) noexcept;
  bool read_s16(std::int16_t& out) noexcept;
  bool read_u32(std::uint32_t& out) noexcept;
  bool read_zstring(std::string& out);
  bool read_sized_string(std::string& out);
  bool skip(std::size_t count) noexcept;

private:
  std::span<const std::byte> data_;
  std::size_t position_{0};
};

class BitReader {
public:
  BitReader(std::span<const std::byte> data, std::size_t start_bit) noexcept
      : data_(data), bit_position_(start_bit) {}

  bool read_unsigned(unsigned count, std::uint32_t& out) noexcept;
  bool read_signed(unsigned count, std::int32_t& out) noexcept;

  std::size_t bit_position() const noexcept { return bit_position_; }
  std::size_t byte_position() const noexcept { return (bit_position_ + 7) / 8; }
  void align() noexcept { bit_position_ = (bit_position_ + 7) / 8 * 8; }

private:
  std::span<const std::byte> data_;
  std::size_t bit_position_{0};
};

struct Cursor {
  std::span<const std::byte> data;
  std::size_t position{0};

  bool u8(std::uint8_t& out);
  bool u16(std::uint16_t& out);
  bool u32(std::uint32_t& out);
  bool matrix(SwfMatrix& out);
};

struct SwfShapeStyles {
  std::vector<SwfFillStyle> fills;
  std::vector<SwfLineStyle> lines;
  unsigned fill_bits{0};
  unsigned line_bits{0};
};

struct SwfMorphStyles {
  std::vector<SwfMorphFillStyle> fills;
  std::vector<SwfMorphLineStyle> lines;
  unsigned fill_bits{0};
  unsigned line_bits{0};
};

struct StyleBounds {
  std::size_t fills{0};
  std::size_t lines{0};
  std::size_t fill_base{0};
  std::size_t line_base{0};
};

struct ShapeRecordCounts {
  std::uint32_t records{0};
  std::uint32_t edges{0};
  std::uint32_t curved_edges{0};
  std::uint32_t style_changes{0};
};

// Each style entry requires its type byte; a line style requires its two UI16 widths (SWF 19).
constexpr std::size_t kMinFillStyleBytes = 1;
constexpr std::size_t kMinLineStyleBytes = 4;

// A declared style count must not exceed the bytes left that could encode it.
bool style_count_fits(const Cursor& cursor, std::size_t count, std::size_t minimum_bytes);

bool read_fill_style(Cursor& cursor, unsigned version, SwfFillStyle& out);

bool read_line_style(Cursor& cursor, unsigned version, SwfLineStyle& out);

bool read_shape_styles(Cursor& cursor, unsigned version, SwfShapeStyles& out);

bool read_morph_styles(Cursor& cursor, unsigned version, SwfMorphStyles& out);

bool read_shape_records(std::span<const std::byte> data, std::size_t start, unsigned version,
                        SwfShapeStyles& styles, StyleBounds& bounds, ShapeRecordCounts& counts,
                        std::vector<SwfShapeRecord>& records, std::size_t& next);

bool read_morph_records(std::span<const std::byte> data, std::size_t start, unsigned version,
                        SwfMorphStyles& styles, StyleBounds& bounds, ShapeRecordCounts& counts,
                        std::vector<SwfShapeRecord>& records, std::size_t& next);

/* Requires all-or-nothing: any unreadable glyph leaves font.glyphs empty so the font tag still decodes. */
void read_glyph_shapes(std::span<const std::byte> data, std::size_t table_start, SwfFont& font);

bool lzma_decode_swf(std::span<const std::byte> input, std::size_t out_size,
                     std::vector<std::byte>& out);

std::vector<std::byte> lzma_encode_swf(std::span<const std::byte> body);

std::string to_text(std::span<const std::byte> bytes);

std::uint8_t byte_at(std::span<const std::byte> bytes, std::size_t index) noexcept;

bool read_rect(std::span<const std::byte> data, std::size_t offset, SwfRect& out,
               std::size_t& next) noexcept;

bool read_matrix(BitReader& bits, SwfMatrix& out) noexcept;

SwfStatus decode_tag_payload(const std::vector<std::byte>& payload, SwfTag& out_tag,
                             unsigned depth, std::size_t& tags_left);

SwfStatus decode_placement_tag(std::span<const std::byte> data, SwfTag& out_tag);

SwfStatus decode_button_tag(std::span<const std::byte> data, SwfTag& out_tag);

SwfStatus decode_shape_tag(std::span<const std::byte> data, SwfTag& out_tag);

SwfStatus decode_bitmap_tag(std::span<const std::byte> data, SwfTag& out_tag);

bool inflate_to(std::span<const std::byte> input, std::size_t expected,
                std::vector<std::byte>& unpacked);

SwfStatus decode_morph_tag(std::span<const std::byte> data, SwfTag& out_tag);

SwfStatus decode_jpeg_tag(std::span<const std::byte> data, SwfTag& out_tag);

SwfStatus decode_text_tag(std::span<const std::byte> data, SwfTag& out_tag);

SwfStatus decode_tag_stream(std::span<const std::byte> data, std::size_t begin, std::size_t end,
                            unsigned depth, std::vector<SwfTag>& out, std::size_t& stop,
                            std::size_t& tags_left);

}  // namespace fo4::swf::detail
