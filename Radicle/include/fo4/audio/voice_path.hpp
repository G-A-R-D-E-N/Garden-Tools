#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace fo4::audio {

struct VoicePath {
  std::string plugin;
  std::string voice_type;
  std::string language;
  std::string stem;
  std::string extension;
  std::optional<std::uint32_t> info_form_id;
  std::optional<std::uint32_t> response_index;
  std::string variant;

  friend bool operator==(const VoicePath&, const VoicePath&) = default;
};

std::optional<VoicePath> parse_voice_path(std::string_view data_relative_path);

std::string format_voice_path(std::string_view plugin,
                              std::string_view voice_type,
                              std::uint32_t info_form_id,
                              std::uint32_t response_index,
                              std::string_view language = {},
                              std::string_view variant = {},
                              std::string_view extension = "fuz");

std::string voice_companion_path(const VoicePath& path);

}  // namespace fo4::audio
