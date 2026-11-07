#include "fo4/plugin/master_usage.hpp"

#include "fo4/plugin/visit.hpp"
#include "fo4/schema/form_links.hpp"

namespace fo4 {

MasterUsage scan_master_usage(const Plugin& plugin) {
  const auto& masters = plugin.header.masters;
  MasterUsage usage;
  usage.references.assign(masters.size(), 0);

  visit_records(plugin, [&](const Record& record) {
    if (record.header.signature == sig_tes4) return;
    if (record.undecodable) ++usage.undecodable_records;
    for (std::size_t i = 0; i < masters.size(); ++i) {
      if (record.form_key.owner() == masters[i].key) ++usage.references[i];
    }
  });

  const auto links = collect_form_links_detailed(plugin);
  usage.records_with_incomplete_vmad = links.records_with_incomplete_vmad;
  usage.unclaimed_link_payloads = links.unclaimed_link_payloads.size();
  for (const auto& hit : links.links) {
    const auto index = hit.raw.index_byte();
    if (!hit.raw.is_null() && index < masters.size()) ++usage.references[index];
  }
  return usage;
}

std::vector<MasterDescriptor> used_masters(const Plugin& plugin, const MasterUsage& usage) {
  std::vector<MasterDescriptor> kept;
  for (std::size_t i = 0; i < plugin.header.masters.size(); ++i) {
    if (!usage.complete() || usage.references[i] != 0) {
      kept.push_back({plugin.header.masters[i].key, plugin.header.masters[i].kind});
    }
  }
  return kept;
}

}  // namespace fo4
