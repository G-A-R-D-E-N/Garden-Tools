#include "copy_hierarchy.hpp"

#include "child_groups.hpp"

#include "fo4/formid.hpp"
#include "fo4/plugin/reader.hpp"
#include "fo4/schema/form_links.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <utility>
#include <variant>
#include <vector>

namespace fo4 {
namespace {

// Nesting and kind lists are concept-level, TODO-verify against a Fallout4.esm sample; see docs/code-notes/radicle-copy-cell-children.md.
constexpr auto k_cell = Signature::from_string("CELL");
constexpr auto k_wrld = Signature::from_string("WRLD");

constexpr auto k_reference_kinds = std::array{
    Signature::from_string("REFR"), Signature::from_string("ACHR"),
    Signature::from_string("ACRE"), Signature::from_string("PGRE"),
    Signature::from_string("PMIS"), Signature::from_string("PARW"),
    Signature::from_string("PBEA"), Signature::from_string("PFLA"),
    Signature::from_string("PCON"), Signature::from_string("PBAR"),
    Signature::from_string("PHZD"),
};

constexpr auto k_temporary_only_kinds = std::array{
    Signature::from_string("PGRD"),
    Signature::from_string("LAND"),
    Signature::from_string("NAVM"),
};

bool contains(const auto& kinds, Signature signature) {
  return std::find(kinds.begin(), kinds.end(), signature) != kinds.end();
}

bool nested_kind_allowed(std::int32_t type, Signature signature) {
  if (contains(k_reference_kinds, signature)) return type == 8 || type == 9 || type == 10;
  return type == 9 && contains(k_temporary_only_kinds, signature);
}

bool find_group_path(const Group& group, const Record* record, std::vector<const Group*>& path) {
  path.push_back(&group);
  for (const auto& child : group.children) {
    if (const auto* sub = std::get_if<Group>(&child)) {
      if (find_group_path(*sub, record, path)) return true;
    } else if (&std::get<Record>(child) == record) {
      return true;
    }
  }
  path.pop_back();
  return false;
}

std::optional<FormKey> resolve_group_label(const Group& group, const PluginFormIdContext& context) {
  try {
    return resolve_plugin_form_id(RawFormId{group.header.label_as_u32()}, context);
  } catch (const FormIdError&) {
    return std::nullopt;
  }
}

const Record* record_before_group(const Group& container,
                                  const Group& group,
                                  const FormKey& key,
                                  Signature signature) {
  for (std::size_t index = 0; index < container.children.size(); ++index) {
    const auto* record = std::get_if<Record>(&container.children[index]);
    if (record == nullptr || owned_child_group(container, index) != &group) continue;
    return record->form_key == key && record->header.signature == signature ? record : nullptr;
  }
  return nullptr;
}

GroupHeader destination_header(const Group& source_group,
                               const PluginFormIdContext& source_context,
                               const PluginFormIdContext& destination_context) {
  auto header = source_group.header;
  if (!group_label_is_form_id(header.group_type)) return header;
  const auto key = resolve_plugin_form_id(RawFormId{header.label_as_u32()}, source_context);
  const auto value = encode_plugin_form_id(key, destination_context).value();
  for (std::size_t index = 0; index < header.label.size(); ++index) {
    header.label[index] = std::byte{static_cast<std::uint8_t>(value >> (8 * index))};
  }
  return header;
}

bool same_group(const GroupHeader& left, const GroupHeader& right) {
  return left.group_type == right.group_type && left.label == right.label;
}

Group* find_child_group(Group& parent, const GroupHeader& header) {
  for (auto& child : parent.children) {
    auto* group = std::get_if<Group>(&child);
    if (group != nullptr && same_group(group->header, header)) return group;
  }
  return nullptr;
}

Group& find_or_add_child_group(Group& parent, const GroupHeader& header) {
  if (auto* found = find_child_group(parent, header)) return *found;
  Group created;
  created.header = header;
  parent.children.emplace_back(std::move(created));
  return std::get<Group>(parent.children.back());
}

Group* adjacent_child_group(Group& parent, std::size_t position, const GroupHeader& header) {
  if (const auto* next = owned_child_group(parent, position); next != nullptr) {
    return same_group(next->header, header) ? &std::get<Group>(parent.children[position + 1]) : nullptr;
  }
  if (find_child_group(parent, header) != nullptr) return nullptr;
  Group created;
  created.header = header;
  parent.children.insert(parent.children.begin() + static_cast<std::ptrdiff_t>(position + 1),
                         std::move(created));
  return &std::get<Group>(parent.children[position + 1]);
}

std::optional<std::size_t> record_index(const Group& group, const FormKey& key) {
  for (std::size_t index = 0; index < group.children.size(); ++index) {
    const auto* record = std::get_if<Record>(&group.children[index]);
    if (record != nullptr && record->form_key == key) return index;
  }
  return std::nullopt;
}

Group* find_top_group(Plugin& plugin, Signature signature) {
  for (auto& group : plugin.groups) {
    if (group.header.group_type == 0 && group.header.top_label() == signature) return &group;
  }
  return nullptr;
}

Record clone_record(const Record& source,
                    const FormKey& key,
                    const PluginFormIdContext& source_context,
                    const PluginFormIdContext& destination_context) {
  auto out = source;
  out.header.form_id = encode_plugin_form_id(key, destination_context);
  remap_record_form_links(out, source_context, destination_context);
  out.original_range.clear();
  out.decompressed_payload.clear();
  out.header.data_size = 0;
  out.uncompressed_size = 0;
  return out;
}

}  // namespace

std::optional<NestedCopyPlan> plan_nested_copy(const Plugin& source, const Record* record) {
  NestedCopyPlan plan;
  for (const auto& group : source.groups) {
    if (group.header.group_type == 0 && find_group_path(group, record, plan.path)) break;
  }
  const auto& path = plan.path;
  if (path.size() < 4 || !nested_kind_allowed(path.back()->header.group_type, record->header.signature)) {
    return std::nullopt;
  }

  std::vector<std::int32_t> parents;
  for (std::size_t index = 0; index + 1 < path.size(); ++index) {
    parents.push_back(path[index]->header.group_type);
  }
  const auto top = path.front()->header.top_label();
  const bool interior = top == k_cell && parents == std::vector<std::int32_t>{0, 2, 3, 6};
  const bool exterior = top == k_wrld && parents == std::vector<std::int32_t>{0, 1, 4, 5, 6};
  const bool persistent = top == k_wrld && parents == std::vector<std::int32_t>{0, 1, 6};
  if (!interior && !exterior && !persistent) return std::nullopt;

  const auto& cell_group = *path[path.size() - 2];
  const auto cell_key = resolve_group_label(cell_group, plugin_form_id_context(source));
  if (!cell_key || path.back()->header.label != cell_group.header.label) return std::nullopt;
  plan.cell = record_before_group(*path[path.size() - 3], cell_group, *cell_key, k_cell);
  if (plan.cell == nullptr) return std::nullopt;

  if (exterior || persistent) {
    const auto world_key = resolve_group_label(*path[1], plugin_form_id_context(source));
    if (!world_key) return std::nullopt;
    plan.world = record_before_group(*path.front(), *path[1], *world_key, k_wrld);
    if (plan.world == nullptr) return std::nullopt;
  }
  return plan;
}

bool insert_nested_copy(Plugin& destination,
                        const Plugin& source,
                        const NestedCopyPlan& plan,
                        Record copied) {
  const auto& path = plan.path;
  const auto source_context = plugin_form_id_context(source);
  const auto destination_context = plugin_form_id_context(destination);

  auto* top = find_top_group(destination, path.front()->header.top_label());
  if (top == nullptr) {
    destination.groups.emplace_back();
    top = &destination.groups.back();
    top->header = path.front()->header;
  }

  Group* current = top;
  std::size_t first = 1;
  if (plan.world != nullptr) {
    const auto world_key = plan.world->form_key;
    auto index = record_index(*top, world_key);
    if (index) {
      if (std::get<Record>(top->children[*index]).header.signature != k_wrld) return false;
    } else {
      if (find_record(destination, world_key) != nullptr) return false;
      top->children.emplace_back(
          clone_record(*plan.world, world_key, source_context, destination_context));
      index = top->children.size() - 1;
    }
    current = adjacent_child_group(
        *top, *index, destination_header(*path[1], source_context, destination_context));
    if (current == nullptr) return false;
    first = 2;
  }

  for (std::size_t index = first; index + 2 < path.size(); ++index) {
    current = &find_or_add_child_group(
        *current, destination_header(*path[index], source_context, destination_context));
  }

  const auto cell_key = plan.cell->form_key;
  const auto* existing = find_record(destination, cell_key);
  auto cell_index = record_index(*current, cell_key);
  if (existing != nullptr && (!cell_index || existing->header.signature != k_cell)) return false;
  if (!cell_index) {
    current->children.emplace_back(
        clone_record(*plan.cell, cell_key, source_context, destination_context));
    cell_index = current->children.size() - 1;
  }

  auto* cell_children = adjacent_child_group(
      *current, *cell_index,
      destination_header(*path[path.size() - 2], source_context, destination_context));
  if (cell_children == nullptr) return false;
  auto& leaf = find_or_add_child_group(
      *cell_children, destination_header(*path.back(), source_context, destination_context));
  leaf.children.emplace_back(std::move(copied));
  return true;
}

}  // namespace fo4
