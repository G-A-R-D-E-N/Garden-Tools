#include "fo4/schema/condition_scopes.hpp"

#include "fo4/plugin/reader.hpp"
#include "fo4/plugin/visit.hpp"
#include "fo4/signature.hpp"

#include <algorithm>
#include <charconv>
#include <cstddef>
#include <optional>
#include <string_view>
#include <system_error>
#include <utility>
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
constexpr Signature sig_alcc = Signature::from_string("ALCC");
constexpr Signature sig_citc = Signature::from_string("CITC");
constexpr Signature sig_qsta = Signature::from_string("QSTA");
constexpr Signature sig_pack = Signature::from_string("PACK");
constexpr Signature sig_info = Signature::from_string("INFO");
constexpr Signature sig_perk = Signature::from_string("PERK");
constexpr Signature sig_qust = Signature::from_string("QUST");
constexpr Signature sig_indx = Signature::from_string("INDX");
constexpr Signature sig_prkc = Signature::from_string("PRKC");

constexpr Signature k_package_branch_members[] = {
    sig_citc, sig_ctda, sig_cis1, sig_cis2, Signature::from_string("PRCB"),
    Signature::from_string("PNAM"), Signature::from_string("FNAM"),
    Signature::from_string("PKC2"), Signature::from_string("PFO2"),
    Signature::from_string("PFOR"),
};

constexpr Signature k_quest_target_members[] = {
    sig_citc, sig_ctda, sig_cis1, sig_cis2,
};

constexpr Signature k_pack_root_successors[] = {
    Signature::from_string("IDLA"), Signature::from_string("CNAM"),
    Signature::from_string("QNAM"), Signature::from_string("PKCU"),
};

constexpr Signature k_info_root_successors[] = {
    Signature::from_string("RNAM"), Signature::from_string("ANAM"),
    Signature::from_string("TSCE"), Signature::from_string("INTV"),
    Signature::from_string("ALFA"), Signature::from_string("ONAM"),
    Signature::from_string("GREE"), Signature::from_string("TIQS"),
    Signature::from_string("NAM0"), Signature::from_string("INCC"),
    Signature::from_string("MODQ"), Signature::from_string("INAM"),
};

constexpr Signature k_perk_root_successors[] = {
    Signature::from_string("DATA"), Signature::from_string("SNAM"),
    Signature::from_string("NNAM"), Signature::from_string("FNAM"),
    Signature::from_string("PRKE"),
};

constexpr Signature k_perk_tab_members[] = {
    sig_ctda, sig_cis1, sig_cis2,
};

constexpr Signature k_reference_condition_successors[] = {
    Signature::from_string("KSIZ"), Signature::from_string("KWDA"),
    Signature::from_string("COCT"), Signature::from_string("CNTO"),
    Signature::from_string("COED"), Signature::from_string("SPOR"),
    Signature::from_string("OCOR"), Signature::from_string("GWOR"),
    Signature::from_string("ECOR"), Signature::from_string("ALLA"),
    Signature::from_string("ALDN"), Signature::from_string("ALFV"),
    Signature::from_string("ALDI"), Signature::from_string("ALSP"),
    Signature::from_string("ALFC"), Signature::from_string("ALPC"),
    Signature::from_string("VTCK"),
};

bool is_reference_condition_successor(Signature signature) {
  for (const auto successor : k_reference_condition_successors) {
    if (signature == successor) return true;
  }
  return false;
}

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

bool is_whitespace(char ch) {
  return ch == ' ' || ch == '\t' || ch == '\n' || ch == '\r' ||
         ch == '\f' || ch == '\v';
}

std::string_view trim_whitespace(std::string_view text) noexcept {
  while (!text.empty() && is_whitespace(text.front())) text.remove_prefix(1);
  while (!text.empty() && is_whitespace(text.back())) text.remove_suffix(1);
  return text;
}

bool parse_index(std::string_view text, std::size_t& value) noexcept {
  text = trim_whitespace(text);
  if (text.empty()) return false;
  if (text.empty()) return false;
  const auto* begin = text.data();
  const auto* end = begin + text.size();
  const auto [ptr, error] = std::from_chars(begin, end, value, 10);
  return error == std::errc{} && ptr == end;
}

std::optional<ScopedConditionPath> parse_scoped_path(
    Signature record, std::string_view path) noexcept {
  const auto separator = path.find('.');
  if (separator == std::string_view::npos ||
      path.find('.', separator + 1) != std::string_view::npos) {
    return std::nullopt;
  }
  const auto container_path = trim_whitespace(path.substr(0, separator));
  const auto member_path = trim_whitespace(path.substr(separator + 1));
  const auto open = container_path.find('[');
  if (open == std::string_view::npos) return std::nullopt;
  const auto close = container_path.find(']', open + 1);
  if (close == std::string_view::npos || close + 1 != container_path.size()) {
    return std::nullopt;
  }

  for (const auto& descriptor : k_generated_condition_scopes) {
    if (descriptor.record != record) continue;
    if (!iequals(container_path.substr(0, open), descriptor.container)) continue;

    std::size_t container_index = 0;
    if (!parse_index(container_path.substr(open + 1, close - open - 1),
                     container_index)) {
      continue;
    }
    if (!iequals(member_path, descriptor.member)) continue;
    return ScopedConditionPath{descriptor, container_index};
  }
  return std::nullopt;
}

struct ScopeRange {
  std::size_t begin{0};
  std::size_t end{0};
  std::size_t insert_anchor{0};
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
    ranges.push_back(ScopeRange{begin, end, end, true});
  }
  return ranges;
}

bool is_alias_start(Signature signature) {
  return signature == sig_alst || signature == sig_alls || signature == sig_alcs;
}

std::size_t alias_condition_insert_anchor(const std::vector<Subrecord>& subs,
                                          std::size_t start, std::size_t aled,
                                          bool location) {
  for (std::size_t i = start + 1; i < aled; ++i) {
    const auto signature = subs[i].signature;
    const bool successor =
        location ? signature == sig_alcc : is_reference_condition_successor(signature);
    if (successor) return i;
  }
  return aled;
}

std::optional<std::vector<ScopeRange>> alias_element_ranges(const Record& record) {
  const auto& subs = record.subrecords;
  std::optional<std::size_t> anam;
  for (std::size_t i = 0; i < subs.size(); ++i) {
    if (subs[i].signature != sig_anam) continue;
    if (anam.has_value()) return std::nullopt;
    if (subs[i].data.size() != 4) return std::nullopt;
    anam = i;
  }

  std::vector<ScopeRange> ranges;
  if (!anam.has_value()) {
    for (const auto& subrecord : subs) {
      if (is_alias_start(subrecord.signature)) return std::nullopt;
    }
    return ranges;
  }

  std::size_t i = *anam + 1;
  while (i < subs.size() && is_alias_start(subs[i].signature)) {
    const std::size_t start = i;
    if (subs[start].signature == sig_alcs) {
      std::size_t j = start;
      while (j < subs.size() && subs[j].signature == sig_alcs) {
        if (subs[j].data.size() != 4) return std::nullopt;
        ++j;
        if (j < subs.size() && subs[j].signature == sig_almi) {
          if (subs[j].data.size() != 1) return std::nullopt;
          ++j;
        }
      }
      ranges.push_back(ScopeRange{start, j, j, false});
      i = j;
      continue;
    }
    if (subs[start].data.size() != 4) return std::nullopt;
    const bool location = subs[start].signature == sig_alls;
    std::size_t j = start + 1;
    while (j < subs.size() && subs[j].signature != sig_aled &&
           !is_alias_start(subs[j].signature)) {
      ++j;
    }
    if (j >= subs.size() || subs[j].signature != sig_aled) return std::nullopt;
    const std::size_t anchor = alias_condition_insert_anchor(subs, start, j, location);
    ranges.push_back(ScopeRange{start, j, anchor, true});
    i = j + 1;
  }

  for (; i < subs.size(); ++i) {
    const auto signature = subs[i].signature;
    if (is_alias_start(signature) || signature == sig_aled || signature == sig_almi) {
      return std::nullopt;
    }
  }
  return ranges;
}

template <std::size_t N>
bool contains_signature(const Signature (&set)[N], Signature signature) {
  for (const auto member : set) {
    if (signature == member) return true;
  }
  return false;
}

std::vector<ScopeRange> package_branch_ranges(const Record& record) {
  const auto& subs = record.subrecords;
  std::vector<ScopeRange> ranges;
  for (std::size_t i = 0; i + 1 < subs.size(); ++i) {
    if (subs[i].signature != sig_anam || subs[i + 1].signature != sig_citc) continue;
    std::size_t end = i + 2;
    while (end < subs.size() &&
           contains_signature(k_package_branch_members, subs[end].signature)) {
      ++end;
    }
    ranges.push_back(ScopeRange{i, end, i + 2, true});
  }
  return ranges;
}

std::vector<ScopeRange> quest_target_ranges(const Record& record) {
  const auto& subs = record.subrecords;
  std::vector<ScopeRange> ranges;
  for (std::size_t i = 0; i < subs.size(); ++i) {
    if (subs[i].signature != sig_qsta) continue;
    std::size_t end = i + 1;
    while (end < subs.size() &&
           contains_signature(k_quest_target_members, subs[end].signature)) {
      ++end;
    }
    const std::size_t anchor =
        (i + 1 < subs.size() && subs[i + 1].signature == sig_citc) ? i + 2 : i + 1;
    ranges.push_back(ScopeRange{i, end, anchor, true});
  }
  return ranges;
}

std::vector<ScopeRange> perk_effect_tab_ranges(const Record& record) {
  const auto& subs = record.subrecords;
  std::vector<ScopeRange> ranges;
  for (std::size_t i = 0; i < subs.size(); ++i) {
    if (subs[i].signature != sig_prkc) continue;
    std::size_t end = i + 1;
    while (end < subs.size() &&
           contains_signature(k_perk_tab_members, subs[end].signature)) {
      ++end;
    }
    ranges.push_back(ScopeRange{i, end, i + 1, true});
  }
  return ranges;
}

enum class ScopeStatus { ok, out_of_range, not_conditioned, structure_invalid };

struct ScopeResolution {
  ScopeStatus status{ScopeStatus::out_of_range};
  ScopeRange range{};
};

std::optional<std::vector<ScopeRange>> scope_ranges(const Record& record,
                                                    ConditionScopeKind kind) {
  switch (kind) {
    case ConditionScopeKind::effect:
      return effect_element_ranges(record);
    case ConditionScopeKind::alias:
      return alias_element_ranges(record);
    case ConditionScopeKind::package_branch:
      return package_branch_ranges(record);
    case ConditionScopeKind::quest_target:
      return quest_target_ranges(record);
    case ConditionScopeKind::perk_effect_tab:
      return perk_effect_tab_ranges(record);
  }
  return std::nullopt;
}

ScopeResolution resolve_scope(const Record& record,
                              const ConditionScopeDescriptor& descriptor,
                              std::size_t container_index) {
  const auto ranges = scope_ranges(record, descriptor.kind);
  if (!ranges.has_value()) {
    return ScopeResolution{ScopeStatus::structure_invalid, {}};
  }
  if (container_index >= ranges->size()) {
    return ScopeResolution{ScopeStatus::out_of_range, {}};
  }
  const auto& range = (*ranges)[container_index];
  if (!range.conditioned) {
    return ScopeResolution{ScopeStatus::not_conditioned, range};
  }
  return ScopeResolution{ScopeStatus::ok, range};
}

bool has_scope_of_kind(Signature record, ConditionScopeKind kind) {
  for (const auto& descriptor : k_generated_condition_scopes) {
    if (descriptor.record == record && descriptor.kind == kind) return true;
  }
  return false;
}

std::optional<std::vector<ScopeRange>> nested_scope_ranges(const Record& record) {
  constexpr ConditionScopeKind kinds[] = {
      ConditionScopeKind::effect,
      ConditionScopeKind::alias,
      ConditionScopeKind::package_branch,
      ConditionScopeKind::quest_target,
      ConditionScopeKind::perk_effect_tab,
  };
  std::vector<ScopeRange> ranges;
  for (const auto kind : kinds) {
    if (!has_scope_of_kind(record.header.signature, kind)) continue;
    auto kind_ranges = scope_ranges(record, kind);
    if (!kind_ranges.has_value()) return std::nullopt;
    ranges.insert(ranges.end(), kind_ranges->begin(), kind_ranges->end());
  }
  std::sort(ranges.begin(), ranges.end(),
            [](const ScopeRange& a, const ScopeRange& b) { return a.begin < b.begin; });
  return ranges;
}

template <std::size_t N>
std::optional<std::size_t> first_member_index(const std::vector<Subrecord>& subs,
                                              const Signature (&set)[N]) {
  for (std::size_t i = 0; i < subs.size(); ++i) {
    if (contains_signature(set, subs[i].signature)) return i;
  }
  return std::nullopt;
}

std::optional<std::size_t> root_insert_anchor(const Record& record) {
  const auto& subs = record.subrecords;
  if (record.header.signature == sig_pack) return first_member_index(subs, k_pack_root_successors);
  if (record.header.signature == sig_info) return first_member_index(subs, k_info_root_successors);
  if (record.header.signature == sig_perk) return first_member_index(subs, k_perk_root_successors);
  return std::nullopt;
}

std::size_t root_window_limit(const Record& record) {
  if (record.header.signature != sig_qust) return record.subrecords.size();
  for (std::size_t i = 0; i < record.subrecords.size(); ++i) {
    if (record.subrecords[i].signature == sig_indx) return i;
  }
  return record.subrecords.size();
}

bool has_ctda_in(const std::vector<Subrecord>& subs, std::size_t begin, std::size_t end) {
  for (std::size_t i = begin; i < end; ++i) {
    if (subs[i].signature == sig_ctda) return true;
  }
  return false;
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

RootConditionWindow root_condition_window(const Record& record) {
  const auto& subs = record.subrecords;
  const auto nested = nested_scope_ranges(record);
  if (!nested.has_value()) {
    return RootConditionWindow{RootConditionStatus::alias_structure_invalid, 0, 0, std::nullopt};
  }

  std::size_t lists = 0;
  RootConditionWindow found{RootConditionStatus::ok, 0, subs.size(), std::nullopt};
  const auto consider = [&](std::size_t begin, std::size_t end) {
    if (begin < end && has_ctda_in(subs, begin, end)) {
      ++lists;
      found.begin = begin;
      found.end = end;
    }
  };
  const auto limit = root_window_limit(record);
  std::size_t cursor = 0;
  for (const auto& range : *nested) {
    if (cursor < range.begin) consider(cursor, std::min(range.begin, limit));
    cursor = std::max(cursor, range.end);
  }
  consider(cursor, limit);

  if (lists > 1) return RootConditionWindow{RootConditionStatus::multiple_lists, 0, 0, std::nullopt};
  if (lists == 1) return found;

  const auto anchor = root_insert_anchor(record);
  if (!anchor.has_value()) {
    return RootConditionWindow{RootConditionStatus::no_list, 0, subs.size(), std::nullopt};
  }
  std::size_t begin = 0;
  std::size_t end = subs.size();
  for (const auto& range : *nested) {
    if (range.end <= *anchor) begin = std::max(begin, range.end);
    if (range.begin >= *anchor) end = std::min(end, range.begin);
  }
  return RootConditionWindow{RootConditionStatus::no_list, begin, end, anchor};
}

ScopedConditionsRead read_root_conditions(const Record& record) {
  const auto window = root_condition_window(record);
  switch (window.status) {
    case RootConditionStatus::no_list:
      return ScopedConditionsRead{true, ScopedConditionStatus::updated, {}};
    case RootConditionStatus::multiple_lists:
      return ScopedConditionsRead{false, ScopedConditionStatus::multiple_lists, {}};
    case RootConditionStatus::alias_structure_invalid:
      return ScopedConditionsRead{false, ScopedConditionStatus::alias_structure_invalid, {}};
    case RootConditionStatus::ok:
      break;
  }
  try {
    return ScopedConditionsRead{
        true, ScopedConditionStatus::updated,
        read_ctda_conditions_in(record, window.begin, window.end)};
  } catch (const CtdaMultipleListsError&) {
    return ScopedConditionsRead{false, ScopedConditionStatus::multiple_lists, {}};
  } catch (const CtdaNotLosslessError&) {
    return ScopedConditionsRead{false, ScopedConditionStatus::unrepresentable, {}};
  }
}

ScopedConditionsRead read_scoped_conditions(
    const Record& record,
    const ConditionScopeDescriptor& descriptor,
    std::size_t container_index) {
  const auto resolution = resolve_scope(record, descriptor, container_index);
  switch (resolution.status) {
    case ScopeStatus::ok:
      try {
        return ScopedConditionsRead{
            true, ScopedConditionStatus::updated,
            read_ctda_conditions_in(record, resolution.range.begin, resolution.range.end)};
      } catch (const CtdaMultipleListsError&) {
        return ScopedConditionsRead{false, ScopedConditionStatus::multiple_lists, {}};
      } catch (const CtdaNotLosslessError&) {
        return ScopedConditionsRead{false, ScopedConditionStatus::unrepresentable, {}};
      }
    case ScopeStatus::out_of_range:
      return ScopedConditionsRead{false,
                                  ScopedConditionStatus::container_index_out_of_range, {}};
    case ScopeStatus::not_conditioned:
      return ScopedConditionsRead{false, ScopedConditionStatus::element_not_conditioned, {}};
    case ScopeStatus::structure_invalid:
      return ScopedConditionsRead{false, ScopedConditionStatus::alias_structure_invalid, {}};
  }
  return ScopedConditionsRead{false, ScopedConditionStatus::container_index_out_of_range, {}};
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
  const auto resolution = resolve_scope(*record, descriptor, container_index);
  switch (resolution.status) {
    case ScopeStatus::out_of_range:
      return ScopedConditionStatus::container_index_out_of_range;
    case ScopeStatus::structure_invalid:
      return ScopedConditionStatus::alias_structure_invalid;
    case ScopeStatus::not_conditioned:
      return ScopedConditionStatus::element_not_conditioned;
    case ScopeStatus::ok:
      break;
  }
  const auto& range = resolution.range;
  const auto result = write_ctda_conditions_in(*record, range.begin, range.end,
                                               conditions, range.insert_anchor);
  if (result == CtdaScopeWrite::replaced || result == CtdaScopeWrite::inserted) {
    record->original_range.clear();
    record->decompressed_payload.clear();
    record->header.data_size = 0;
    record->uncompressed_size = 0;
    plugin.original_range.clear();
    return ScopedConditionStatus::updated;
  }
  if (result == CtdaScopeWrite::multiple_lists) return ScopedConditionStatus::multiple_lists;
  return ScopedConditionStatus::unchanged;
}

}  // namespace fo4::schema
