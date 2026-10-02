#include "fo4/plugin/maintenance.hpp"

#include "fo4/plugin/reader.hpp"
#include "fo4/plugin/visit.hpp"
#include "fo4/schema/form_link_mixed.hpp"
#include "fo4/schema/form_links.hpp"

#include <exception>
#include <variant>

namespace fo4 {
namespace {

constexpr Signature k_sig_navm = Signature::from_string("NAVM");

bool same_record(const Record& left, const Record& right) {
  if (left.header.signature != right.header.signature || left.header.flags != right.header.flags ||
      left.subrecords.size() != right.subrecords.size()) {
    return false;
  }
  for (std::size_t i = 0; i < left.subrecords.size(); ++i) {
    if (left.subrecords[i].signature != right.subrecords[i].signature ||
        left.subrecords[i].data != right.subrecords[i].data) {
      return false;
    }
  }
  return true;
}

// source:PluginEditTool RadiclePluginMaintenance.cpp previousVersionNormalizesEqual; results must stay equivalent.
bool previous_normalizes_equal(const Record& current,
                               const Record& previous,
                               const Plugin& previous_plugin,
                               const PluginFormIdContext& previous_context,
                               const PluginFormIdContext& current_context) {
  auto normalized = previous;
  try {
    remap_record_form_links(normalized, previous_context, current_context, &previous_plugin);
  } catch (const std::exception&) {
    return false;
  }
  return same_record(current, normalized);
}

}  // namespace

std::vector<IdenticalToMaster> find_identical_to_master(std::span<const Plugin* const> load_order,
                                                        std::size_t plugin_index,
                                                        std::size_t limit) {
  std::vector<IdenticalToMaster> out;
  if (plugin_index >= load_order.size() || load_order[plugin_index] == nullptr) return out;
  const auto& plugin = *load_order[plugin_index];
  const auto current_context = plugin_form_id_context(plugin);

  for (const auto& group : plugin.groups) {
    if (group.header.group_type != 0) continue;
    for (const auto& child : group.children) {
      const auto* record = std::get_if<Record>(&child);
      if (record == nullptr || record->form_key.owner().empty() ||
          record->form_key.owner() == plugin.key) {
        continue;
      }
      for (std::size_t index = plugin_index; index-- > 0;) {
        const auto* earlier = load_order[index];
        if (earlier == nullptr) continue;
        const auto* previous = find_record(*earlier, record->form_key);
        if (previous == nullptr) continue;
        if (previous_normalizes_equal(*record, *previous, *earlier,
                                      plugin_form_id_context(*earlier), current_context)) {
          out.push_back({record->form_key, record->header.signature, earlier->key});
          if (limit != 0 && out.size() >= limit) return out;
        }
        break;
      }
    }
  }
  return out;
}

DeletedRecordCounts count_deleted_records(const Plugin& plugin) {
  DeletedRecordCounts counts;
  visit_records(plugin, [&](const Record& record) {
    if (record.header.signature == sig_tes4 || !record.header.flags.is_deleted()) return;
    ++counts.records;
    if (is_placed_reference_signature(record.header.signature)) ++counts.references;
    if (record.header.signature == k_sig_navm) ++counts.navmeshes;
  });
  return counts;
}

}  // namespace fo4
