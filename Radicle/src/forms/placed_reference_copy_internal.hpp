#pragma once

#include "fo4/forms/placed_reference.hpp"
#include "fo4/signature.hpp"

#include <array>
#include <cmath>
#include <cstdint>
#include <set>
#include <string_view>
#include <vector>

namespace fo4::forms::detail {

struct RecordLocation {
  const Record* record{nullptr};
  std::vector<const Group*> path;
};

std::array<std::byte, 4> u32_label(std::uint32_t value);

RecordLocation locate_record(const Plugin& plugin, const FormKey& key);

GroupHeader remap_group_header(const GroupHeader& source,
                               const PluginFormIdContext& source_context,
                               const PluginFormIdContext& destination_context);

bool same_group_identity(const GroupHeader& left, const GroupHeader& right);

Group* unique_top_group(Plugin& plugin, const GroupHeader& wanted);

Group* unique_child_group(Group& parent, const GroupHeader& wanted);

Group* ensure_cell_children_group(Group& parent,
                                  const GroupHeader& source_header,
                                  const FormKey& cell,
                                  const PluginFormIdContext& source_context,
                                  const PluginFormIdContext& destination_context);

Group* ensure_reference_bucket(Group& cell_children,
                               const GroupHeader& source_header,
                               const PluginFormIdContext& source_context,
                               const PluginFormIdContext& destination_context);

PluginKind owner_kind(const Plugin& source, const PluginKey& owner);

bool has_master(const Plugin& plugin, const PluginKey& owner);

void collect_group_label_owner(const Group& group,
                               const PluginFormIdContext& source_context,
                               std::set<PluginKey>& owners);

Record copy_record_for_destination(const Record& source_record,
                                   const FormKey& key,
                                   const PluginFormIdContext& source_context,
                                   const PluginFormIdContext& destination_context);

void validate_destination_record_path(
    const Plugin& destination,
    const FormKey& key,
    Signature signature,
    const std::vector<const Group*>& source_path,
    const PluginFormIdContext& source_context,
    const PluginFormIdContext& destination_context,
    std::string_view label);

inline constexpr float k_exterior_cell_span = 4096.0f;

inline GroupHeader group_header(GroupType type, std::uint32_t label) {
  GroupHeader header;
  header.group_type = static_cast<std::int32_t>(type);
  header.label = u32_label(label);
  return header;
}

inline std::int32_t grid_coordinate(float world) {
  const auto cells = std::floor(world / k_exterior_cell_span);
  if (!(std::fabs(cells) < 2147483648.0f)) {
    throw PlacedReferenceError("placed reference position is not a finite grid coordinate");
  }
  return static_cast<std::int32_t>(cells);
}

}  // namespace fo4::forms::detail
