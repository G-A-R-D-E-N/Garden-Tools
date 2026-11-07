#pragma once

#include "fo4/plugin/container.hpp"
#include "fo4/signature.hpp"

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

inline const Group* quest_child_group(const Group& parent, std::size_t record_index) {
  if (record_index + 1 >= parent.children.size()) return nullptr;
  const auto* next = std::get_if<Group>(&parent.children[record_index + 1]);
  return next != nullptr && next->header.group_type == static_cast<std::int32_t>(GroupType::quest_children)
             ? next
             : nullptr;
}

inline const Group* record_child_group(const Group& parent, std::size_t record_index) {
  const auto& record = std::get<Record>(parent.children[record_index]);
  const auto* child = owned_child_group(parent, record_index);
  if (child == nullptr && record.header.signature == sig_qust) child = quest_child_group(parent, record_index);
  return child != nullptr && child->header.label_as_u32() == record.header.form_id.value() ? child : nullptr;
}

}  // namespace fo4
