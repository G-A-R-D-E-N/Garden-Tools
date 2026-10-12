#pragma once

#include "fo4/forms/placed_reference.hpp"
#include "fo4/signature.hpp"

#include <set>
#include <string_view>
#include <vector>

namespace fo4::forms::detail {

struct RecordLocation {
  const Record* record{nullptr};
  std::vector<const Group*> path;
};

RecordLocation locate_record(const Plugin& plugin, const FormKey& key);

GroupHeader remap_group_header(const GroupHeader& source,
                               const PluginFormIdContext& source_context,
                               const PluginFormIdContext& destination_context);

bool same_group_identity(const GroupHeader& left, const GroupHeader& right);

Group* unique_top_group(Plugin& plugin, const GroupHeader& wanted);

Group* unique_child_group(Group& parent, const GroupHeader& wanted);

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

}  // namespace fo4::forms::detail
