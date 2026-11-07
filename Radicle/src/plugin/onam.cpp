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

std::uint64_t structure_fingerprint(const Plugin& plugin) {
  const auto group_count = collect_groups(plugin).size();
  const auto record_count = collect_records(plugin).size();
  return (static_cast<std::uint64_t>(group_count) << 32) |
         static_cast<std::uint64_t>(record_count);
}

bool same_tes4(const Record& a, const Record& b) {
  const auto& x = a.header;
  const auto& y = b.header;
  return x.signature == y.signature && x.data_size == y.data_size &&
         x.flags.value() == y.flags.value() && x.form_id.value() == y.form_id.value() &&
         x.timestamp == y.timestamp && x.version_control == y.version_control &&
         x.form_version == y.form_version && x.vc_version == y.vc_version &&
         a.compressed == b.compressed && a.undecodable == b.undecodable &&
         a.uncompressed_size == b.uncompressed_size &&
         std::ranges::equal(a.subrecords, b.subrecords, [](const Subrecord& left, const Subrecord& right) {
           return left.signature == right.signature && left.data == right.data &&
                  left.extended_size == right.extended_size;
         });
}

bool original_structure_is_intact(const Plugin& plugin) {
  if (plugin.original_structure_fingerprint == 0 ||
      structure_fingerprint(plugin) != plugin.original_structure_fingerprint) {
    return false;
  }
  return std::ranges::all_of(collect_records(plugin),
                             [&](const Record* record) {
                               return !record->original_range.empty() ||
                                      (record == &plugin.tes4 && tes4_matches_hedr_repair(plugin));
                             });
}

bool participates_in_fo4_onam(Signature signature) {
  return signature == Signature::from_string("NAVM") ||
         signature == Signature::from_string("LAND") ||
         signature == Signature::from_string("REFR") ||
         signature == Signature::from_string("PGRE") ||
         signature == Signature::from_string("PMIS") ||
         signature == Signature::from_string("ACHR") ||
         signature == Signature::from_string("PARW") ||
         signature == Signature::from_string("PBEA") ||
         signature == Signature::from_string("PFLA") ||
         signature == Signature::from_string("PCON") ||
         signature == Signature::from_string("PBAR") ||
         signature == Signature::from_string("PHZD") ||
         signature == Signature::from_string("SCEN") ||
         signature == Signature::from_string("DLBR") ||
         signature == Signature::from_string("DIAL") ||
         signature == Signature::from_string("INFO");
}

void collect_onam_overrides(const Plugin& plugin,
                            const Group& group,
                            std::vector<RawFormId>& result) {
  for (const auto& child : group.children) {
    if (const auto* record = std::get_if<Record>(&child)) {
      if (record->header.flags.is_persistent() ||
          !participates_in_fo4_onam(record->header.signature)) {
        continue;
      }
      if (record->form_key.owner().empty() || record->form_key.owner() == plugin.key) continue;
      result.push_back(record->header.form_id);
      continue;
    }
    collect_onam_overrides(plugin, std::get<Group>(child), result);
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

bool tes4_matches_hedr_repair(const Plugin& plugin) {
  return plugin.tes4_after_hedr_repair && same_tes4(plugin.tes4, *plugin.tes4_after_hedr_repair);
}

void rebuild_tes4_onam(Plugin& plugin, bool preserve_intact_structure) {
  if (preserve_intact_structure && original_structure_is_intact(plugin)) return;
  assign_record_form_keys(plugin);

  std::vector<RawFormId> overrides;
  for (const auto& group : plugin.groups) {
    collect_onam_overrides(plugin, group, overrides);
  }

  std::ranges::sort(overrides);
  overrides.erase(std::unique(overrides.begin(), overrides.end()), overrides.end());
  if (overrides == plugin.header.overrides) return;

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
  plugin.tes4.original_range.clear();
  plugin.original_range.clear();
  plugin.header.overrides = overrides;
}

}  // namespace fo4
