#pragma once

#include "fo4/environment/world.hpp"
#include "fo4/schema/ctda.hpp"

#include <span>
#include <vector>

namespace fo4::env {

struct PackageData {
  std::uint32_t flags{0};
  std::uint8_t type{0}, interrupt_override{0}, preferred_speed{0}, unknown{0};
  std::uint16_t interrupt_flags{0};
  std::uint16_t trailing_value{0};
};
struct PackageSchedule {
  std::int8_t month{0}, weekday{0}, date{0}, hour{0}, minute{0};
  std::uint32_t duration_minutes{0};
};
struct PackageCounter { std::uint32_t input_count{0}, version{0}; FormKey template_package; };
struct PackageLocation {
  std::size_t subrecord_index{0};
  std::int32_t type{0};
  std::uint32_t value{0};
  std::optional<std::int32_t> radius;
  std::optional<std::uint32_t> item_index;
  std::optional<FormKey> form;
};
struct PackageTarget {
  std::size_t subrecord_index{0};
  std::int32_t type{0};
  std::uint32_t value{0};
  std::int32_t count_or_distance{0};
  std::optional<FormKey> form;
};
struct Package {
  std::optional<PackageData> data;
  std::optional<PackageSchedule> schedule;
  std::optional<PackageCounter> counter;
  std::optional<FormKey> combat_style, owner_quest, begin_idle, end_idle, change_idle;
  std::vector<CtdaCondition> conditions;
  std::vector<PackageLocation> locations;
  std::vector<PackageTarget> targets;
  std::vector<FormKey> idle_animations;
  std::span<const Subrecord> opaque;
};
struct CombatCloseRange { std::array<float, 9> values{}; std::uint32_t throw_max_targets{0}; float flank_variance{0}; };
struct CombatStyle {
  std::optional<std::array<float, 12>> general;
  std::vector<float> melee, long_range;
  std::optional<CombatCloseRange> close_range;
  std::optional<std::array<float, 8>> flight;
  std::optional<float> ranged_accuracy, cover_search;
  std::optional<std::uint32_t> flags;
};
struct IdleData { std::uint8_t loop_min{0}, loop_max{0}, flags{0}, group_section{0}; std::uint16_t replay_delay{0}; };
struct Idle {
  std::optional<IdleData> data;
  std::optional<FormKey> parent, previous;
  std::optional<std::string_view> behavior_graph, animation_event, animation_file;
  std::vector<CtdaCondition> conditions;
};

Package decode_package(const Environment& env, const RecordRef& ref);
CombatStyle decode_combat_style(const Environment& env, const RecordRef& ref);
Idle decode_idle(const Environment& env, const RecordRef& ref);

}
