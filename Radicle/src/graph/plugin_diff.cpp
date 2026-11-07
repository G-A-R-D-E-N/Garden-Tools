#include "fo4/graph/plugin_diff.hpp"

#include "fo4/plugin/placement.hpp"
#include "fo4/plugin/reader.hpp"

#include <algorithm>
#include <map>

namespace fo4 {
namespace {

std::map<FormKey, const Record*> index_records(const Plugin& plugin) {
  std::map<FormKey, const Record*> out;
  for (const auto* record : collect_records(plugin)) out.try_emplace(record->form_key, record);
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

bool same_subrecord(const Subrecord& a, const Subrecord& b) {
  return a.signature == b.signature && a.data == b.data;
}

void add_reordered(const Record& before, const Record& after, std::vector<Signature>& out) {
  const auto& a = before.subrecords;
  const auto& b = after.subrecords;
  std::size_t head = 0;
  while (head < a.size() && head < b.size() && same_subrecord(a[head], b[head])) ++head;
  std::size_t tail = 0;
  while (tail < a.size() - head && tail < b.size() - head &&
         same_subrecord(a[a.size() - 1 - tail], b[b.size() - 1 - tail])) {
    ++tail;
  }
  for (std::size_t i = head; i < a.size() - tail; ++i) out.push_back(a[i].signature);
  for (std::size_t i = head; i < b.size() - tail; ++i) out.push_back(b[i].signature);
}

RecordDiff make_diff(const Record& record, RecordChange change) {
  return RecordDiff{.key = record.form_key,
                    .signature = record.header.signature,
                    .change = change,
                    .header_changed = false,
                    .placement_changed = false,
                    .changed_subrecords = {}};
}

RecordDiff compare(const Record& before, const Record& after) {
  auto diff = make_diff(after, RecordChange::changed);
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
  if (diff.changed_subrecords.empty()) add_reordered(before, after, diff.changed_subrecords);
  std::sort(diff.changed_subrecords.begin(), diff.changed_subrecords.end());
  diff.changed_subrecords.erase(
      std::unique(diff.changed_subrecords.begin(), diff.changed_subrecords.end()),
      diff.changed_subrecords.end());
  return diff;
}

}  // namespace

std::vector<RecordDiff> diff_plugins(const Plugin& before, const Plugin& after) {
  const auto old_records = index_records(before);
  const auto new_records = index_records(after);
  const auto old_places = record_placements(before);
  const auto new_places = record_placements(after);
  std::vector<RecordDiff> out;
  for (const auto& [key, record] : new_records) {
    const auto old = old_records.find(key);
    if (old == old_records.end()) {
      out.push_back(make_diff(*record, RecordChange::added));
      continue;
    }
    auto diff = compare(*old->second, *record);
    const auto old_place = old_places.find(key);
    const auto new_place = new_places.find(key);
    diff.placement_changed = (old_place == old_places.end()) != (new_place == new_places.end()) ||
                             (old_place != old_places.end() && new_place != new_places.end() &&
                              old_place->second != new_place->second);
    if (diff.header_changed || diff.placement_changed || !diff.changed_subrecords.empty()) {
      out.push_back(std::move(diff));
    }
  }
  for (const auto& [key, record] : old_records) {
    if (!new_records.contains(key)) {
      out.push_back(make_diff(*record, RecordChange::removed));
    }
  }
  return out;
}

}  // namespace fo4
