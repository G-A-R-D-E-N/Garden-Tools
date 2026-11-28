#pragma once

#include "fo4/plugin/container.hpp"

#include <array>
#include <cstdint>
#include <cstring>
#include <optional>
#include <stdexcept>
#include <string_view>
#include <vector>

namespace fo4 {

class RecordShapeError : public std::runtime_error {
public:
  using std::runtime_error::runtime_error;
};

struct SoundRateFile {
  std::uint32_t rpm{0};
  std::string_view file;
};

struct SoundRepeat {
  float min_time{0};
  float max_time{0};
  std::optional<std::uint8_t> stackable;
};

inline std::optional<std::string_view> zstring_view(const Subrecord& subrecord) noexcept {
  const auto& data = subrecord.data;
  if (data.empty() || data.back() != std::byte{0}) return std::nullopt;
  const auto* begin = reinterpret_cast<const char*>(data.data());
  const auto length = std::strlen(begin);
  if (length != data.size() - 1) return std::nullopt;
  return std::string_view{begin, length};
}

std::vector<SoundRateFile> parse_sound_rate_files(const Record& record);
std::optional<SoundRepeat> parse_sound_repeat(const Record& record);
std::array<std::uint32_t, 5> parse_footstep_counts(const Record& record);

}
