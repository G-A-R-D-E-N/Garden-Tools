#include "fo4/plugin/onam.hpp"

#include "fo4/plugin/reader.hpp"
#include "fo4/signature.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>
#include <vector>

namespace fo4 {
namespace {

bool participates_in_fo4_onam(Signature signature) {
  return signature == Signature::from_string("NAVM") ||
         signature == Signature::from_string("LAND") ||
         signature == Signature::from_string("REFR") ||
         signature == Signature::from_string("PGRE") ||
         signature == Signature::from_string("PMIS") ||
         signature == Signature::from_string("ACHR") ||
         signature == Signature::from_string("ACRE") ||
         signature == Signature::from_string("SCEN") ||
         signature == Signature::from_string("DLBR") ||
         signature == Signature::from_string("DIAL") ||
         signature == Signature::from_string("INFO");
}

void collect_onam_overrides(const Plugin& plugin,
                            const Group& group,
                            bool inside_persistent_children,
                            std::vector<RawFormId>& result) {
  const bool persistent =
      inside_persistent_children ||
      group.header.group_type == static_cast<std::int32_t>(GroupType::cell_persistent_children);

  for (const auto& child : group.children) {
    if (const auto* record = std::get_if<Record>(&child)) {
      if (persistent || !participates_in_fo4_onam(record->header.signature)) continue;
      if (record->form_key.owner().empty() || record->form_key.owner() == plugin.key) continue;
      result.push_back(record->header.form_id);
      continue;
    }
    collect_onam_overrides(plugin, std::get<Group>(child), persistent, result);
  }
}

Subrecord make_onam(std::span<const RawFormId> form_ids) {
  Subrecord result;
  result.signature = sig_onam;
  result.data.resize(form_ids.size() * sizeof(std::uint32_t));
  for (std::size_t index = 0; index < form_ids.size(); ++index) {
    const auto raw = form_ids[index].value();
    std::memcpy(result.data.data() + index * sizeof(raw), &raw, sizeof(raw));
  }
  return result;
}

}  // namespace

void rebuild_tes4_onam(Plugin& plugin) {
  assign_record_form_keys(plugin);

  std::vector<RawFormId> overrides;
  for (const auto& group : plugin.groups) {
    collect_onam_overrides(plugin, group, false, overrides);
  }

  std::ranges::sort(overrides);
  overrides.erase(std::unique(overrides.begin(), overrides.end()), overrides.end());

  std::vector<Subrecord> subrecords;
  subrecords.reserve(plugin.tes4.subrecords.size() + (overrides.empty() ? 0u : 1u));

  std::size_t insert_at = 0;
  for (const auto& subrecord : plugin.tes4.subrecords) {
    if (subrecord.signature == sig_onam) continue;
    subrecords.push_back(subrecord);
    if (subrecord.signature == sig_mast || subrecord.signature == sig_data) {
      insert_at = subrecords.size();
    }
  }

  if (!overrides.empty()) {
    subrecords.insert(subrecords.begin() + static_cast<std::ptrdiff_t>(insert_at),
                      make_onam(overrides));
  }

  plugin.tes4.subrecords = std::move(subrecords);
  plugin.tes4.original_bytes.clear();
  plugin.original_bytes.clear();
  plugin.header.overrides = overrides;
}

}  // namespace fo4
