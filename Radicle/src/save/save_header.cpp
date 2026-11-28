#include "fo4/save/save_header.hpp"

#include "fo4/binary/cursor.hpp"
#include "fo4/error.hpp"
#include "fo4/text/encoding.hpp"

#include <cstring>
#include <string>
#include <string_view>

namespace fo4::save {

namespace {

constexpr char k_magic[] = "FO4_SAVEGAME";
constexpr std::size_t k_magic_size = sizeof(k_magic) - 1;

std::string read_wstring(binary::Cursor& in) {
  const auto bytes = in.read_bytes(in.read_u16());
  return text::windows1252_to_utf8(std::string_view(reinterpret_cast<const char*>(bytes.data()), bytes.size()));
}

SaveHeader read_header(std::span<const std::byte> bytes) {
  if (bytes.size() < k_magic_size || std::memcmp(bytes.data(), k_magic, k_magic_size) != 0) {
    throw parse_error(0, "not a Fallout 4 save: bad magic");
  }
  binary::Cursor in(bytes);
  in.read_bytes(k_magic_size);
  const auto header_size = in.read_u32();
  const auto header_offset = in.absolute_offset();
  binary::Cursor header(in.read_bytes(header_size), header_offset);

  SaveHeader out;
  out.version = header.read_u32();
  if (out.version != k_save_version) {
    throw parse_error(header_offset, "unsupported save version " + std::to_string(out.version));
  }
  out.save_number = header.read_u32();
  out.player_name = read_wstring(header);
  out.level = header.read_u32();
  out.location = read_wstring(header);
  out.playtime = read_wstring(header);
  out.race = read_wstring(header);
  out.sex = header.read_u16();
  out.current_xp = header.read_f32();
  out.required_xp = header.read_f32();
  const auto filetime_low = header.read_u32();
  out.filetime = (static_cast<std::uint64_t>(header.read_u32()) << 32) | filetime_low;
  out.screenshot_width = header.read_u32();
  out.screenshot_height = header.read_u32();
  if (!header.at_end()) {
    throw parse_error(header.absolute_offset(), "save header size does not match its fields");
  }
  if (out.screenshot_width > k_max_screenshot_dimension || out.screenshot_height > k_max_screenshot_dimension) {
    throw parse_error(in.absolute_offset(), "save screenshot dimensions are too large: " +
                                                std::to_string(out.screenshot_width) + "x" +
                                                std::to_string(out.screenshot_height));
  }
  out.screenshot = in.read_bytes(static_cast<std::size_t>(std::uint64_t{out.screenshot_width} *
                                                          out.screenshot_height * 4U));

  out.form_version = in.read_u8();
  out.game_version = read_wstring(in);
  const auto plugin_size = in.read_u32();
  const auto plugin_offset = in.absolute_offset();
  binary::Cursor plugins(in.read_bytes(plugin_size), plugin_offset);
  for (std::size_t left = plugins.read_u8(); left > 0; --left) {
    out.plugins.push_back(read_wstring(plugins));
  }
  if (!plugins.at_end()) {
    for (std::size_t left = plugins.read_u16(); left > 0; --left) {
      out.light_plugins.push_back(read_wstring(plugins));
    }
    if (!plugins.at_end()) {
      throw parse_error(plugins.absolute_offset(), "save plugin list size does not match its entries");
    }
  }
  return out;
}

}  // namespace

bool parse_save_header(std::span<const std::byte> bytes, SaveHeader& out, std::string& error) {
  try {
    out = read_header(bytes);
    error.clear();
    return true;
  } catch (const parse_error& failure) {
    error = failure.parse_message() + " (offset " + std::to_string(failure.offset()) + ")";
    return false;
  }
}

}  // namespace fo4::save
