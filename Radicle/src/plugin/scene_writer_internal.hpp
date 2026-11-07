#pragma once

#include "fo4/plugin/container.hpp"

#include "fo4/environment/scene_layout.hpp"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace fo4::detail {

env::detail::SceneLayout checked_scene_layout(const Record& record);

std::size_t tail_anchor(const Record& record);

std::size_t groups_end(const env::detail::SceneLayout& layout, const Record& record);

std::size_t phase_anchor(const env::detail::SceneLayout& layout,
                         const Record& record,
                         std::size_t index);

std::size_t actor_anchor(const env::detail::SceneLayout& layout,
                         const Record& record,
                         std::size_t index);

std::size_t action_anchor(const env::detail::SceneLayout& layout,
                          const Record& record,
                          std::size_t index);

// Constraint: one stored phase reference of an action header, captured before the phase list moves.
struct PhaseReference {
  std::size_t subrecord{0};
  std::uint32_t value{0};
  std::size_t action{0};
};

std::vector<PhaseReference> action_phase_references(const Record& record,
                                                    const env::detail::SceneLayout& layout,
                                                    std::size_t phase_count);

void check_phase_mapping(const std::vector<PhaseReference>& references,
                         const std::vector<std::uint32_t>& old_to_new);

void remap_phase_references(Record& record,
                            const std::vector<PhaseReference>& references,
                            const std::vector<std::uint32_t>& old_to_new);

void remap_scene_phase_vmad(Plugin& plugin, const Record& record,
                            const std::vector<std::uint32_t>& old_to_new);

// Constraint: the action index convention is read from the record; a scene that mixes 0- and 1-based indices is refused.
std::uint32_t action_index_base(const Record& record, const env::detail::SceneLayout& layout);

void renumber_actions(Record& record, const env::detail::SceneLayout& layout, std::uint32_t base);

void move_span(Record& record, std::size_t open, std::size_t close, std::size_t anchor);

}  // namespace fo4::detail
