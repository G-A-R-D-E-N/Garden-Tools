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

SwfStatus decode_tag_payload(const std::vector<std::byte>& payload, SwfTag& out_tag,
                             unsigned depth) {
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
      out_tag.background_color = (static_cast<std::uint32_t>(byte_at(data, 0)) << 16) |
                                 (static_cast<std::uint32_t>(byte_at(data, 1)) << 8) |
                                 static_cast<std::uint32_t>(byte_at(data, 2));
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
                                            out_tag.children, stop);
      return nested == SwfStatus::ok ? SwfStatus::ok : SwfStatus::malformed_tag;
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
    default:
      return SwfStatus::ok;
  }
}

SwfStatus decode_tag_stream(std::span<const std::byte> data, std::size_t begin, std::size_t end,
                            unsigned depth, std::vector<SwfTag>& out, std::size_t& stop) {
  std::size_t position = begin;
  bool malformed = false;
  while (position < end) {
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

    if (decode_tag_payload(current.payload, current, depth) != SwfStatus::ok) {
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
