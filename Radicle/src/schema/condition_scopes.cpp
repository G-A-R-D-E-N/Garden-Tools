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

constexpr Signature sig_efid = Signature::from_string("EFID");
constexpr Signature sig_efit = Signature::from_string("EFIT");
constexpr Signature sig_cis1 = Signature::from_string("CIS1");
constexpr Signature sig_cis2 = Signature::from_string("CIS2");

char ascii_lower(char ch) {
  return (ch >= 'A' && ch <= 'Z') ? static_cast<char>(ch - 'A' + 'a') : ch;
}

bool iequals(std::string_view a, std::string_view b) {
  if (a.size() != b.size()) return false;
  for (std::size_t i = 0; i < a.size(); ++i) {
    if (ascii_lower(a[i]) != ascii_lower(b[i])) return false;
  }
  return true;
}

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
    if (path.size() < descriptor.container.size()) continue;
    if (!iequals(path.substr(0, descriptor.container.size()), descriptor.container)) {
      continue;
    }
    auto cursor = descriptor.container.size();
    if (cursor >= path.size() || path[cursor] != '[') continue;
    const auto close = path.find(']', cursor + 1);
    if (close == std::string_view::npos) continue;

    std::size_t container_index = 0;
    if (!parse_index(path.substr(cursor + 1, close - cursor - 1), container_index)) {
      continue;
    }
    if (close + 1 >= path.size() || path[close + 1] != '.') continue;
    if (!iequals(path.substr(close + 2), descriptor.member)) continue;
    return ScopedConditionPath{descriptor, container_index};
  }
  return std::nullopt;
}

struct ScopeRange {
  std::size_t begin{0};
  std::size_t end{0};
};

int effect_stage(Signature signature) {
  if (signature == sig_efid) return 0;
  if (signature == sig_efit) return 1;
  if (signature == sig_ctda) return 2;
  return -1;
}

std::vector<ScopeRange> effect_element_ranges(const Record& record) {
  std::vector<std::size_t> starts;
  std::size_t region_end = 0;
  int current_stage = -1;
  bool open = false;
  for (std::size_t i = 0; i < record.subrecords.size(); ++i) {
    const auto signature = record.subrecords[i].signature;
    if (signature == sig_efid || signature == sig_efit || signature == sig_ctda ||
        signature == sig_cis1 || signature == sig_cis2) {
      region_end = i + 1;
    }
    const int stage = effect_stage(signature);
    if (stage < 0) continue;
    const bool starts_new = !open || (stage != 2 && stage <= current_stage);
    if (starts_new) {
      starts.push_back(i);
      current_stage = stage;
      open = true;
    } else if (stage > current_stage) {
      current_stage = stage;
    }
  }

  std::vector<ScopeRange> ranges;
  ranges.reserve(starts.size());
  for (std::size_t k = 0; k < starts.size(); ++k) {
    const std::size_t begin = starts[k];
    const std::size_t end = (k + 1 < starts.size()) ? starts[k + 1] : region_end;
    ranges.push_back(ScopeRange{begin, end});
  }
  return ranges;
}

std::optional<ScopeRange> scope_range(const Record& record,
                                      const ConditionScopeDescriptor& descriptor,
                                      std::size_t container_index) {
  std::vector<ScopeRange> ranges;
  switch (descriptor.kind) {
    case ConditionScopeKind::effect:
      ranges = effect_element_ranges(record);
      break;
  }
  if (container_index >= ranges.size()) return std::nullopt;
  return ranges[container_index];
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
  const auto range = scope_range(record, descriptor, container_index);
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
  const auto range = scope_range(*record, descriptor, container_index);
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
