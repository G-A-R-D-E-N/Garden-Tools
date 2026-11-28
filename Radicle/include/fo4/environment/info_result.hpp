#pragma once

#include "fo4/environment/world.hpp"
#include "fo4/schema/vmad.hpp"

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace fo4::env {

enum class InfoFragmentState : std::uint8_t { no_vmad = 0, no_section = 1, unreadable = 2, present = 3 };

enum class InfoFragmentKind : std::uint8_t { on_begin = 1, on_end = 2 };

struct InfoFragmentRow {
  InfoFragmentKind kind{InfoFragmentKind::on_begin};
  std::uint8_t unknown{0};
  std::string script, function;
};

struct InfoResult {
  std::optional<std::array<std::uint16_t, 2>> stages;
  std::optional<std::string_view> start_scene_phase;
  InfoFragmentState fragment_state{InfoFragmentState::no_vmad};
  std::string fragment_failure;
  std::int32_t fragment_version{0};
  std::uint8_t fragment_flags{0};
  VmadScript fragment_script;
  std::vector<InfoFragmentRow> rows;
};

InfoResult decode_info_result(const Environment& env, const RecordRef& ref);

}  // namespace fo4::env
