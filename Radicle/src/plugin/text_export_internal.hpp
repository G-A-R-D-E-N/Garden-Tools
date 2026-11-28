#pragma once

#include "fo4/io/path_text.hpp"
#include "fo4/plugin/container.hpp"
#include "fo4/signature.hpp"
#include "fo4/text/encoding.hpp"

#include <nlohmann/json.hpp>

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <limits>
#include <span>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace fo4::text_export {

using Json = nlohmann::json;

inline constexpr std::string_view k_format = "fo4txt/1";
inline constexpr std::string_view k_header_file = "header.json";
inline constexpr std::string_view k_group_file = "_group.json";

inline bool is_plain_signature(std::string_view text) {
  if (text.size() != 4 || text.back() == ' ') return false;
  for (const char ch : text) {
    const bool letter = (ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z');
    const bool digit = ch >= '0' && ch <= '9';
    if (!letter && !digit && ch != '_' && ch != ' ') return false;
  }
  return true;
}

inline bool is_group_folder_name(std::string_view name) {
  const auto tilde = name.find('~');
  if (tilde == std::string_view::npos) return is_plain_signature(name);
  const auto suffix = name.substr(tilde + 1);
  return is_plain_signature(name.substr(0, tilde)) && !suffix.empty() &&
         suffix.find_first_not_of("0123456789") == std::string_view::npos;
}

inline bool is_printable_ascii(std::string_view text) {
  for (const char ch : text) {
    const auto code = static_cast<unsigned char>(ch);
    if (code < 0x20 || code > 0x7E) return false;
  }
  return true;
}

inline bool is_safe_name(std::string_view name) {
  if (name.empty() || name.back() == '.' || name.back() == ' ') return false;
  for (const char ch : name) {
    const auto code = static_cast<unsigned char>(ch);
    if (code < 0x20 || code == 0x7F) return false;
    if (std::string_view{"<>:\"/\\|?*"}.find(ch) != std::string_view::npos) return false;
  }
  return true;
}

inline void require_windows1252(std::string_view utf8, std::string_view what) {
  if (!text::utf8_to_windows1252(utf8)) {
    throw std::invalid_argument(std::string(what) + " is not representable in Windows-1252: " +
                                std::string(utf8));
  }
}

inline std::string hex_encode(std::span<const std::byte> data) {
  constexpr char digits[] = "0123456789abcdef";
  std::string out;
  out.reserve(data.size() * 2);
  for (const auto byte : data) {
    const auto value = std::to_integer<unsigned>(byte);
    out += digits[value >> 4];
    out += digits[value & 15u];
  }
  return out;
}

inline int hex_nibble(char ch) {
  if (ch >= '0' && ch <= '9') return ch - '0';
  if (ch >= 'a' && ch <= 'f') return ch - 'a' + 10;
  if (ch >= 'A' && ch <= 'F') return ch - 'A' + 10;
  return -1;
}

inline std::vector<std::byte> hex_decode(std::string_view text) {
  if (text.size() % 2 != 0) {
    throw std::invalid_argument("fo4txt: hex string has an odd number of digits");
  }
  std::vector<std::byte> out;
  out.reserve(text.size() / 2);
  for (std::size_t at = 0; at < text.size(); at += 2) {
    const int high = hex_nibble(text[at]);
    const int low = hex_nibble(text[at + 1]);
    if (high < 0 || low < 0) {
      throw std::invalid_argument("fo4txt: hex string holds a non-hex digit");
    }
    out.push_back(std::byte{static_cast<std::uint8_t>(high * 16 + low)});
  }
  return out;
}

inline std::string u32_hex(std::uint32_t value) {
  constexpr char digits[] = "0123456789abcdef";
  std::string out = "0x00000000";
  for (std::size_t digit = 0; digit < 8; ++digit) {
    out[9 - digit] = digits[(value >> (4 * digit)) & 15u];
  }
  return out;
}

inline std::uint32_t parse_u32_hex(std::string_view text) {
  if (text.size() < 3 || text.size() > 10 || text.substr(0, 2) != "0x") {
    throw std::invalid_argument("fo4txt: expected a 0x-prefixed hex value, got " + std::string(text));
  }
  std::uint32_t value = 0;
  for (const char ch : text.substr(2)) {
    const int nibble = hex_nibble(ch);
    if (nibble < 0) {
      throw std::invalid_argument("fo4txt: expected a 0x-prefixed hex value, got " + std::string(text));
    }
    value = value * 16u + static_cast<std::uint32_t>(nibble);
  }
  return value;
}

inline std::string read_file_text(const std::filesystem::path& path) {
  std::ifstream in{path, std::ios::binary};
  if (!in) throw std::runtime_error("fo4txt: cannot open " + io::utf8(path));
  std::ostringstream text;
  text << in.rdbuf();
  return text.str();
}

inline void write_file_text(const std::filesystem::path& path, std::string_view text) {
  std::ofstream out{path, std::ios::binary | std::ios::trunc};
  out.write(text.data(), static_cast<std::streamsize>(text.size()));
  out.close();
  if (!out) throw std::runtime_error("fo4txt: cannot write " + io::utf8(path));
}

inline Json parse_document(const std::string& text, const std::filesystem::path& file) {
  try {
    return Json::parse(text);
  } catch (const Json::exception& error) {
    throw std::invalid_argument("fo4txt: " + io::utf8(file) + " is not valid JSON: " + error.what());
  }
}

inline const Json& field(const Json& object, const char* key) {
  if (!object.is_object() || !object.contains(key)) {
    throw std::invalid_argument(std::string("fo4txt: missing field ") + key);
  }
  return object.at(key);
}

inline std::string string_field(const Json& object, const char* key) {
  const auto& value = field(object, key);
  if (!value.is_string()) {
    throw std::invalid_argument(std::string("fo4txt: field is not a string: ") + key);
  }
  return value.get<std::string>();
}

inline bool bool_field(const Json& object, const char* key) {
  const auto& value = field(object, key);
  if (!value.is_boolean()) {
    throw std::invalid_argument(std::string("fo4txt: field is not a boolean: ") + key);
  }
  return value.get<bool>();
}

inline std::uint32_t unsigned_field(const Json& object, const char* key, std::uint32_t max) {
  const auto& value = field(object, key);
  if (!value.is_number_unsigned() || value.get<std::uint64_t>() > max) {
    throw std::invalid_argument(std::string("fo4txt: field is not an unsigned integer in range: ") + key);
  }
  return static_cast<std::uint32_t>(value.get<std::uint64_t>());
}

inline std::int32_t int32_field(const Json& object, const char* key) {
  const auto& value = field(object, key);
  if (!value.is_number_integer()) {
    throw std::invalid_argument(std::string("fo4txt: field is not an integer: ") + key);
  }
  const auto number = value.get<std::int64_t>();
  if (number < std::numeric_limits<std::int32_t>::min() ||
      number > std::numeric_limits<std::int32_t>::max()) {
    throw std::invalid_argument(std::string("fo4txt: field is out of int32 range: ") + key);
  }
  return static_cast<std::int32_t>(number);
}

inline const Json& array_field(const Json& object, const char* key) {
  const auto& value = field(object, key);
  if (!value.is_array()) {
    throw std::invalid_argument(std::string("fo4txt: field is not an array: ") + key);
  }
  return value;
}

Json schema_labels_json(const Record& record, bool localized);

}  // namespace fo4::text_export
