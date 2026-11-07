#include "fo4/plugin/placement.hpp"

#include "fo4/plugin/reader.hpp"

#include <variant>

namespace fo4 {
namespace {

bool is_parent_group(std::int32_t type) {
  return type == static_cast<std::int32_t>(GroupType::world_children) ||
         type == static_cast<std::int32_t>(GroupType::cell_children) ||
         type == static_cast<std::int32_t>(GroupType::topic_children);
}

void place_group(const Group& group, const PluginFormIdContext& context, FormKey parent,
                 std::map<FormKey, RecordPlacement>& out) {
  if (is_parent_group(group.header.group_type)) {
    try {
      parent = resolve_plugin_link(RawFormId{group.header.label_as_u32()}, context);
    } catch (const FormIdError&) {
      parent = FormKey{};
    }
  }
  for (const auto& child : group.children) {
    if (const auto* sub = std::get_if<Group>(&child)) {
      place_group(*sub, context, parent, out);
      continue;
    }
    const auto& record = std::get<Record>(child);
    out.try_emplace(record.form_key, RecordPlacement{parent, group.header.group_type});
  }
}

}  // namespace

std::map<FormKey, RecordPlacement> record_placements(const Plugin& plugin) {
  std::map<FormKey, RecordPlacement> out;
  const auto context = plugin_form_id_context(plugin);
  for (const auto& group : plugin.groups) place_group(group, context, FormKey{}, out);
  return out;
}

}  // namespace fo4
