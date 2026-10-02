#include "fo4/graph/plugin_diff.hpp"

#include "fo4/plugin/reader.hpp"

#include <algorithm>
#include <map>

namespace fo4 {
namespace {

std::map<FormKey, const Record*> index_records(const Plugin& plugin) {
  std::map<FormKey, const Record*> out;
  for (const auto* record : collect_records(plugin)) out[record->form_key] = record;
  return out;
}

std::map<Signature, std::vector<const std::vector<std::byte>*>> by_signature(const Record& record) {
  std::map<Signature, std::vector<const std::vector<std::byte>*>> out;
  for (const auto& sub : record.subrecords) out[sub.signature].push_back(&sub.data);
  return out;
}

bool same_payloads(const std::vector<const std::vector<std::byte>*>& a,
                   const std::vector<const std::vector<std::byte>*>& b) {
  return std::equal(a.begin(), a.end(), b.begin(), b.end(),
                    [](const auto* x, const auto* y) { return *x == *y; });
}

RecordDiff compare(const Record& before, const Record& after) {
  RecordDiff diff{after.form_key, after.header.signature, RecordChange::changed};
  constexpr auto ignored = RecordFlags::compressed;
  diff.header_changed = before.header.signature != after.header.signature ||
                        (before.header.flags.value() & ~ignored) !=
                            (after.header.flags.value() & ~ignored);
  const auto old_subs = by_signature(before);
  const auto new_subs = by_signature(after);
  for (const auto& [signature, payloads] : new_subs) {
    const auto old = old_subs.find(signature);
    if (old == old_subs.end() || !same_payloads(old->second, payloads)) {
      diff.changed_subrecords.push_back(signature);
    }
  }
  for (const auto& [signature, payloads] : old_subs) {
    if (!new_subs.contains(signature)) diff.changed_subrecords.push_back(signature);
  }
  std::sort(diff.changed_subrecords.begin(), diff.changed_subrecords.end());
  return diff;
}

}  // namespace

std::vector<RecordDiff> diff_plugins(const Plugin& before, const Plugin& after) {
  const auto old_records = index_records(before);
  const auto new_records = index_records(after);
  std::vector<RecordDiff> out;
  for (const auto& [key, record] : new_records) {
    const auto old = old_records.find(key);
    if (old == old_records.end()) {
      out.push_back({key, record->header.signature, RecordChange::added});
      continue;
    }
    auto diff = compare(*old->second, *record);
    if (diff.header_changed || !diff.changed_subrecords.empty()) out.push_back(std::move(diff));
  }
  for (const auto& [key, record] : old_records) {
    if (!new_records.contains(key)) {
      out.push_back({key, record->header.signature, RecordChange::removed});
    }
  }
  return out;
}

}  // namespace fo4
