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
constexpr Signature sig_anam = Signature::from_string("ANAM");
constexpr Signature sig_alst = Signature::from_string("ALST");
constexpr Signature sig_alls = Signature::from_string("ALLS");
constexpr Signature sig_alcs = Signature::from_string("ALCS");
constexpr Signature sig_aled = Signature::from_string("ALED");
constexpr Signature sig_almi = Signature::from_string("ALMI");

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
  bool conditioned{true};
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

bool is_alias_start(Signature signature) {
  return signature == sig_alst || signature == sig_alls || signature == sig_alcs;
}

std::vector<ScopeRange> alias_element_ranges(const Record& record) {
  std::vector<ScopeRange> ranges;
  const auto& subs = record.subrecords;
  std::size_t anam = subs.size();
  for (std::size_t i = 0; i < subs.size(); ++i) {
    if (subs[i].signature == sig_anam) {
      anam = i;
      break;
    }
  }
  if (anam == subs.size()) return ranges;

  std::size_t i = anam + 1;
  while (i < subs.size() && is_alias_start(subs[i].signature)) {
    const std::size_t start = i;
    if (subs[start].signature == sig_alcs) {
      std::size_t end = start + 1;
      if (end < subs.size() && subs[end].signature == sig_almi) ++end;
      ranges.push_back(ScopeRange{start, end, false});
      i = end;
      continue;
    }
    std::size_t j = start + 1;
    while (j < subs.size() && subs[j].signature != sig_aled &&
           !is_alias_start(subs[j].signature)) {
      ++j;
    }
    ranges.push_back(ScopeRange{start, j, true});
    i = (j < subs.size() && subs[j].signature == sig_aled) ? j + 1 : j;
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
    case ConditionScopeKind::alias:
      ranges = alias_element_ranges(record);
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
  if (!range->conditioned) return std::vector<CtdaCondition>{};
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
  if (!range->conditioned) {
    return conditions.empty() ? ScopedConditionStatus::unchanged
                              : ScopedConditionStatus::element_not_conditioned;
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
