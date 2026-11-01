#include "fo4/audio/voice_path.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace fo4::audio {
namespace {

constexpr std::string_view k_prefix_root = "Sound";
constexpr std::string_view k_prefix_voice = "Voice";

char lower(char character) {
  return static_cast<char>(character >= 'A' && character <= 'Z' ? character - 'A' + 'a' : character);
}

bool equal_case_insensitive(std::string_view left, std::string_view right) {
  if (left.size() != right.size()) return false;
  for (std::size_t index = 0; index < left.size(); ++index) {
    if (lower(left[index]) != lower(right[index])) return false;
  }
  return true;
}

bool is_separator(char character) { return character == '\\' || character == '/'; }

std::vector<std::string_view> split_components(std::string_view path) {
  std::vector<std::string_view> components;
  std::size_t start = 0;
  for (std::size_t index = 0; index <= path.size(); ++index) {
    if (index == path.size() || is_separator(path[index])) {
      components.push_back(path.substr(start, index - start));
      start = index + 1;
    }
  }
  return components;
}

bool valid_component(std::string_view component) {
  return !component.empty() && component != "." && component != "..";
}

int hex_value(char character) {
  if (character >= '0' && character <= '9') return character - '0';
  if (character >= 'a' && character <= 'f') return character - 'a' + 10;
  if (character >= 'A' && character <= 'F') return character - 'A' + 10;
  return -1;
}

bool is_identifier_character(char character) {
  return (character >= '0' && character <= '9') || (character >= 'a' && character <= 'z') ||
         (character >= 'A' && character <= 'Z');
}

std::optional<std::uint32_t> parse_form_id(std::string_view text) {
  if (text.empty() || text.size() > 8) return std::nullopt;
  std::uint32_t value = 0;
  for (const char character : text) {
    const auto digit = hex_value(character);
    if (digit < 0) return std::nullopt;
    value = (value << 4u) | static_cast<std::uint32_t>(digit);
  }
  return value;
}

std::optional<std::uint32_t> parse_response_index(std::string_view text) {
  if (text.empty() || text.size() > 10) return std::nullopt;
  std::uint64_t value = 0;
  for (const char character : text) {
    if (character < '0' || character > '9') return std::nullopt;
    value = value * 10u + static_cast<std::uint64_t>(character - '0');
  }
  if (value > std::numeric_limits<std::uint32_t>::max()) return std::nullopt;
  return static_cast<std::uint32_t>(value);
}

std::string format_form_id(std::uint32_t value) {
  static constexpr std::array<char, 16> digits{'0', '1', '2', '3', '4', '5', '6', '7',
                                               '8', '9', 'A', 'B', 'C', 'D', 'E', 'F'};
  std::string out(8, '0');
  for (std::size_t index = 0; index < 8; ++index) {
    out[7 - index] = digits[(value >> (index * 4u)) & 0xFu];
  }
  return out;
}

void require_component(std::string_view component, const char* what) {
  if (!valid_component(component) || component.find('\\') != std::string_view::npos ||
      component.find('/') != std::string_view::npos) {
    throw std::invalid_argument(std::string{"voice path "} + what + " is not a single path component");
  }
}

std::string_view checked_extension(std::string_view extension) {
  if (extension != "fuz" && extension != "hkx") {
    throw std::invalid_argument("voice path extension must be fuz or hkx");
  }
  return extension;
}

}  // namespace

std::optional<VoicePath> parse_voice_path(std::string_view data_relative_path) {
  const auto components = split_components(data_relative_path);
  if (components.size() < 5 || components.size() > 6) return std::nullopt;
  if (!equal_case_insensitive(components[0], k_prefix_root) ||
      !equal_case_insensitive(components[1], k_prefix_voice)) {
    return std::nullopt;
  }
  for (const auto component : components) {
    if (!valid_component(component)) return std::nullopt;
  }

  VoicePath path;
  path.plugin = std::string{components[2]};
  path.voice_type = std::string{components[3]};
  std::size_t file_index = 4;
  if (components.size() == 6) {
    path.language = std::string{components[4]};
    file_index = 5;
  }

  const auto file = components[file_index];
  const auto dot = file.find_last_of('.');
  if (dot == std::string_view::npos || dot == 0) return std::nullopt;
  std::string extension{file.substr(dot + 1)};
  for (auto& character : extension) character = lower(character);
  if (extension != "fuz" && extension != "hkx") return std::nullopt;
  path.extension = extension;
  path.stem = std::string{file.substr(0, dot)};

  const auto separator = path.stem.find('_');
  if (separator != std::string::npos) {
    const auto form_id = parse_form_id(std::string_view{path.stem}.substr(0, separator));
    if (form_id.has_value()) {
      const auto tail = std::string_view{path.stem}.substr(separator + 1);
      const auto variant_separator = tail.find('_');
      const auto response_text = tail.substr(0, variant_separator);
      const auto response = parse_response_index(response_text);
      if (!response.has_value() || *response == 0) return std::nullopt;
      if (response_text.size() > 1 && response_text.front() == '0') return std::nullopt;
      if (variant_separator != std::string_view::npos) {
        const auto variant = tail.substr(variant_separator + 1);
        if (variant.empty()) return std::nullopt;
        for (const char character : variant) {
          if (!is_identifier_character(character)) return std::nullopt;
        }
        path.variant = std::string{variant};
      }
      path.info_form_id = form_id;
      path.response_index = response;
    }
  }
  return path;
}

std::string format_voice_path(std::string_view plugin,
                              std::string_view voice_type,
                              std::uint32_t info_form_id,
                              std::uint32_t response_index,
                              std::string_view language,
                              std::string_view variant,
                              std::string_view extension) {
  require_component(plugin, "plugin");
  require_component(voice_type, "voice type");
  if (!language.empty()) require_component(language, "language");
  if (!variant.empty()) {
    for (const char character : variant) {
      if (!is_identifier_character(character)) {
        throw std::invalid_argument("voice path variant is not alphanumeric");
      }
    }
  }
  if (response_index == 0) {
    throw std::invalid_argument("voice path response index must be at least one");
  }
  checked_extension(extension);

  std::string out{"Sound\\Voice\\"};
  out += plugin;
  out += '\\';
  out += voice_type;
  if (!language.empty()) {
    out += '\\';
    out += language;
  }
  out += '\\';
  out += format_form_id(info_form_id);
  out += '_';
  out += std::to_string(response_index);
  if (!variant.empty()) {
    out += '_';
    out += variant;
  }
  out += '.';
  out += extension;
  return out;
}

std::string voice_companion_path(const VoicePath& path) {
  if (path.info_form_id.has_value() && path.response_index.has_value()) {
    const auto extension = path.extension == "fuz" ? "hkx" : "fuz";
    return format_voice_path(path.plugin, path.voice_type, *path.info_form_id, *path.response_index,
                             path.language, path.variant, extension);
  }
  std::string out{"Sound\\Voice\\"};
  out += path.plugin;
  out += '\\';
  out += path.voice_type;
  if (!path.language.empty()) {
    out += '\\';
    out += path.language;
  }
  out += '\\';
  out += path.stem;
  out += '.';
  out += path.extension == "fuz" ? "hkx" : "fuz";
  return out;
}

}  // namespace fo4::audio
