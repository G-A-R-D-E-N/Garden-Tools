#pragma once

#include "fo4/plugin/container.hpp"
#include "fo4/plugin_core/subrecord_ops.hpp"

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

std::vector<SoundRateFile> parse_sound_rate_files(const Record& record);
std::optional<SoundRepeat> parse_sound_repeat(const Record& record);
std::array<std::uint32_t, 5> parse_footstep_counts(const Record& record);

}
