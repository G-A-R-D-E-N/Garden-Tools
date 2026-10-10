#pragma once

#include "fo4/plugin/container.hpp"

#include <cstddef>
#include <cstdint>
#include <variant>

namespace fo4 {

inline bool is_owned_child_group_type(std::int32_t type) {
  return type == static_cast<std::int32_t>(GroupType::world_children) ||
         type == static_cast<std::int32_t>(GroupType::cell_children) ||
         type == static_cast<std::int32_t>(GroupType::topic_children);
}

inline const Group* owned_child_group(const Group& parent, std::size_t record_index) {
  if (record_index + 1 >= parent.children.size()) return nullptr;
  const auto* next = std::get_if<Group>(&parent.children[record_index + 1]);
  return next != nullptr && is_owned_child_group_type(next->header.group_type) ? next : nullptr;
}

}  // namespace fo4
