#include "fo4/schema/condition_scopes.hpp"

#include "fo4/plugin/reader.hpp"
#include "fo4/plugin/visit.hpp"
#include "fo4/signature.hpp"

#include <charconv>
#include <cstddef>
#include <optional>
#include <string_view>
#include <system_error>
#include <vector>

namespace fo4::schema {
namespace {

bool parse_index(std::string_view text, std::size_t& value) noexcept {
  if (text.empty()) return false;
  const auto* begin = text.data();
  const auto* end = begin + text.size();
  const auto [ptr, error] = std::from_chars(begin, end, value, 10);
  return error == std::errc{} && ptr == end;
}

std::optional<ScopedConditionPath> parse_scoped_path(
    Signature record, std::string_view path) noexcept {
  for (const auto& descriptor : k_generated_condition_scopes) {
    if (descriptor.record != record) continue;
    if (!path.starts_with(descriptor.container)) continue;
    auto cursor = descriptor.container.size();
    if (cursor >= path.size() || path[cursor] != '[') continue;
    const auto close = path.find(']', cursor + 1);
    if (close == std::string_view::npos) continue;

    std::size_t container_index = 0;
    if (!parse_index(path.substr(cursor + 1, close - cursor - 1), container_index)) {
      continue;
    }
    if (close + 1 >= path.size() || path[close + 1] != '.') continue;
    if (path.substr(close + 2) != descriptor.member) continue;
    return ScopedConditionPath{descriptor, container_index};
  }
  return std::nullopt;
}

struct ScopeRange {
  std::size_t begin{0};
  std::size_t end{0};
};

std::optional<ScopeRange> marker_scope_range(const Record& record,
                                             Signature marker,
                                             std::size_t index) {
  std::vector<std::size_t> markers;
  for (std::size_t i = 0; i < record.subrecords.size(); ++i) {
    if (record.subrecords[i].signature == marker) markers.push_back(i);
  }
  if (index >= markers.size()) return std::nullopt;
  const std::size_t begin = markers[index];
  const std::size_t end =
      index + 1 < markers.size() ? markers[index + 1] : record.subrecords.size();
  return ScopeRange{begin, end};
}

Record* find_mutable_record(Plugin& plugin, const FormKey& key) {
  Record* found = nullptr;
  visit_records(plugin, [&](Record& record) {
    if (found == nullptr && record.header.signature != sig_tes4 &&
        record.form_key == key) {
      found = &record;
    }
  });
  return found;
}

}  // namespace

std::optional<ScopedConditionPath> scoped_condition_path(
    Signature record, std::string_view path) noexcept {
  return parse_scoped_path(record, path);
}

std::optional<std::vector<CtdaCondition>> read_scoped_conditions(
    const Record& record,
    const ConditionScopeDescriptor& descriptor,
    std::size_t container_index) {
  const auto range = marker_scope_range(record, descriptor.marker, container_index);
  if (!range.has_value()) return std::nullopt;
  return read_ctda_conditions_in(record, range->begin, range->end);
}

ScopedConditionStatus set_record_scoped_conditions(
    Plugin& plugin,
    const FormKey& record_key,
    const ConditionScopeDescriptor& descriptor,
    std::size_t container_index,
    std::span<const CtdaCondition> conditions) {
  assign_record_form_keys(plugin);
  Record* record = find_mutable_record(plugin, record_key);
  if (record == nullptr) return ScopedConditionStatus::record_missing;
  if (record->header.signature != descriptor.record) {
    return ScopedConditionStatus::record_mismatch;
  }
  const auto range = marker_scope_range(*record, descriptor.marker, container_index);
  if (!range.has_value()) {
    return ScopedConditionStatus::container_index_out_of_range;
  }
  const auto result = write_ctda_conditions_in(*record, range->begin, range->end,
                                               conditions, range->end);
  if (result == CtdaScopeWrite::replaced || result == CtdaScopeWrite::inserted) {
    record->original_range.clear();
    record->decompressed_payload.clear();
    record->header.data_size = 0;
    record->uncompressed_size = 0;
    plugin.original_range.clear();
    return ScopedConditionStatus::updated;
  }
  return ScopedConditionStatus::unchanged;
}

}  // namespace fo4::schema
