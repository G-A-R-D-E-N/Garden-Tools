#include "fo4/schema/condition_scopes.hpp"

#include "fo4/plugin/reader.hpp"
#include "fo4/plugin/visit.hpp"
#include "fo4/signature.hpp"
#include "../plugin/record_edit.hpp"

#include <algorithm>
#include <cstddef>
#include <optional>
#include <span>
#include <string_view>
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
constexpr Signature sig_next = Signature::from_string("NEXT");
constexpr Signature sig_prkc = Signature::from_string("PRKC");
constexpr Signature sig_pnam = Signature::from_string("PNAM");
constexpr Signature sig_scen = Signature::from_string("SCEN");
constexpr Signature sig_itxt = Signature::from_string("ITXT");

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
    Signature::from_string("IDLF"), Signature::from_string("IDLC"),
    Signature::from_string("IDLT"), Signature::from_string("IDLA"),
    Signature::from_string("IDLB"), Signature::from_string("CNAM"),
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

constexpr Signature k_qust_root_successors[] = {
    sig_next,
};

constexpr Signature k_acti_root_successors[] = {
    Signature::from_string("NVNM"),
};

constexpr Signature k_cams_root_successors[] = {
    Signature::from_string("DATA"), Signature::from_string("MNAM"),
};

constexpr Signature k_cobj_root_successors[] = {
    Signature::from_string("CNAM"), Signature::from_string("BNAM"),
    Signature::from_string("NAM1"), Signature::from_string("NAM2"),
    Signature::from_string("NAM3"), Signature::from_string("ANAM"),
    Signature::from_string("FNAM"), Signature::from_string("INTV"),
};

constexpr Signature k_cpth_root_successors[] = {
    Signature::from_string("ANAM"), Signature::from_string("DATA"),
    Signature::from_string("SNAM"),
};

constexpr Signature k_furn_root_successors[] = {
    Signature::from_string("COCT"), Signature::from_string("CNTO"),
    Signature::from_string("MNAM"), Signature::from_string("WBDT"),
    Signature::from_string("NAM1"), Signature::from_string("ENAM"),
    Signature::from_string("FNPR"), Signature::from_string("XMRK"),
    Signature::from_string("SNAM"), Signature::from_string("APPR"),
    Signature::from_string("OBTE"), Signature::from_string("NVNM"),
};

constexpr Signature k_idle_root_successors[] = {
    Signature::from_string("DNAM"), Signature::from_string("ENAM"),
    Signature::from_string("ANAM"), Signature::from_string("DATA"),
    Signature::from_string("GNAM"),
};

constexpr Signature k_lscr_root_successors[] = {
    Signature::from_string("NNAM"), Signature::from_string("TNAM"),
    Signature::from_string("ONAM"), Signature::from_string("ZNAM"),
    Signature::from_string("MOD2"),
};

constexpr Signature k_must_root_successors[] = {
    Signature::from_string("SNAM"),
};

constexpr Signature k_scen_root_successors[] = {
    Signature::from_string("SCQS"), Signature::from_string("NNAM"),
    Signature::from_string("TNAM"), Signature::from_string("XNAM"),
};

constexpr Signature k_smbn_root_successors[] = {
    Signature::from_string("DNAM"), Signature::from_string("XNAM"),
};

constexpr Signature k_smen_root_successors[] = {
    Signature::from_string("DNAM"), Signature::from_string("XNAM"),
    Signature::from_string("ENAM"),
};

constexpr Signature k_smqn_root_successors[] = {
    Signature::from_string("DNAM"), Signature::from_string("XNAM"),
    Signature::from_string("MNAM"), Signature::from_string("HNAM"),
    Signature::from_string("QNAM"), Signature::from_string("NNAM"),
};

constexpr Signature k_sndr_root_successors[] = {
    Signature::from_string("LNAM"), Signature::from_string("BNAM"),
    Signature::from_string("DNAM"), Signature::from_string("ITMC"),
    Signature::from_string("ITMS"),
};

struct RootListAnchor {
  Signature record;
  std::span<const Signature> successors;
  bool end_when_absent;
};

constexpr RootListAnchor k_root_list_anchors[] = {
    {Signature::from_string("ACTI"), k_acti_root_successors, true},
    {Signature::from_string("CAMS"), k_cams_root_successors, true},
    {Signature::from_string("CLFM"), {}, true},
    {Signature::from_string("COBJ"), k_cobj_root_successors, true},
    {Signature::from_string("CPTH"), k_cpth_root_successors, true},
    {Signature::from_string("FACT"), {}, true},
    {Signature::from_string("FURN"), k_furn_root_successors, true},
    {Signature::from_string("HDPT"), {}, true},
    {Signature::from_string("IDLE"), k_idle_root_successors, true},
    {sig_info, k_info_root_successors, true},
    {Signature::from_string("LSCR"), k_lscr_root_successors, true},
    {Signature::from_string("MGEF"), {}, true},
    {Signature::from_string("MUST"), k_must_root_successors, true},
    {sig_pack, k_pack_root_successors, false},
    {sig_perk, k_perk_root_successors, true},
    {sig_qust, k_qust_root_successors, false},
    {Signature::from_string("SCEN"), k_scen_root_successors, true},
    {Signature::from_string("SMBN"), k_smbn_root_successors, true},
    {Signature::from_string("SMEN"), k_smen_root_successors, true},
    {Signature::from_string("SMQN"), k_smqn_root_successors, true},
    {Signature::from_string("SNDR"), k_sndr_root_successors, true},
};

constexpr Signature k_condition_members[] = {
    sig_ctda, sig_cis1, sig_cis2,
};

constexpr Signature sig_btxt = Signature::from_string("BTXT");
constexpr Signature sig_rnam = Signature::from_string("RNAM");
constexpr Signature sig_itid = Signature::from_string("ITID");
constexpr Signature sig_unam = Signature::from_string("UNAM");
constexpr Signature sig_vnam = Signature::from_string("VNAM");
constexpr Signature sig_tnam = Signature::from_string("TNAM");

constexpr Signature k_terminal_menu_members[] = {
    sig_rnam, sig_anam, sig_itid, sig_unam, sig_vnam, sig_tnam, sig_ctda, sig_cis1, sig_cis2,
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
           contains_signature(k_condition_members, subs[end].signature)) {
      ++end;
    }
    ranges.push_back(ScopeRange{i, end, i + 1, true});
  }
  return ranges;
}

std::vector<ScopeRange> message_button_ranges(const Record& record) {
  const auto& subs = record.subrecords;
  std::vector<ScopeRange> ranges;
  for (std::size_t i = 0; i < subs.size(); ++i) {
    if (subs[i].signature != sig_itxt) continue;
    std::size_t end = i + 1;
    while (end < subs.size() && subs[end].signature != sig_itxt) ++end;
    ranges.push_back(ScopeRange{i, end, i + 1, true});
  }
  return ranges;
}

std::vector<ScopeRange> terminal_body_text_ranges(const Record& record) {
  const auto& subs = record.subrecords;
  std::vector<ScopeRange> ranges;
  for (std::size_t i = 0; i < subs.size(); ++i) {
    if (subs[i].signature != sig_btxt) continue;
    std::size_t end = i + 1;
    while (end < subs.size() &&
           contains_signature(k_condition_members, subs[end].signature)) {
      ++end;
    }
    ranges.push_back(ScopeRange{i, end, i + 1, true});
  }
  return ranges;
}

std::vector<ScopeRange> terminal_menu_item_ranges(const Record& record) {
  const auto& subs = record.subrecords;
  std::vector<ScopeRange> ranges;
  for (std::size_t i = 0; i < subs.size(); ++i) {
    if (subs[i].signature != sig_itxt) continue;
    std::size_t end = i + 1;
    while (end < subs.size() &&
           contains_signature(k_terminal_menu_members, subs[end].signature)) {
      ++end;
    }
    ranges.push_back(ScopeRange{i, end, end, true});
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
    case ConditionScopeKind::message_button:
      return message_button_ranges(record);
    case ConditionScopeKind::terminal_body_text:
      return terminal_body_text_ranges(record);
    case ConditionScopeKind::terminal_menu_item:
      return terminal_menu_item_ranges(record);
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
      ConditionScopeKind::message_button,
      ConditionScopeKind::terminal_body_text,
      ConditionScopeKind::terminal_menu_item,
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

std::optional<std::size_t> first_member_index(const std::vector<Subrecord>& subs,
                                              std::size_t begin,
                                              std::span<const Signature> set) {
  for (std::size_t i = begin; i < subs.size(); ++i) {
    if (std::find(set.begin(), set.end(), subs[i].signature) != set.end()) return i;
  }
  return std::nullopt;
}

std::optional<std::size_t> root_insert_anchor(const Record& record, std::size_t begin) {
  const auto& subs = record.subrecords;
  for (const auto& entry : k_root_list_anchors) {
    if (entry.record != record.header.signature) continue;
    const auto anchor = first_member_index(subs, begin, entry.successors);
    if (anchor.has_value() || entry.end_when_absent) return anchor.value_or(subs.size());
    return std::nullopt;
  }
  return std::nullopt;
}

std::optional<std::size_t> root_window_begin(const Record& record) {
  if (record.header.signature != sig_scen) return std::size_t{0};
  const auto& subs = record.subrecords;
  for (std::size_t i = subs.size(); i > 0; --i) {
    if (subs[i - 1].signature == sig_pnam) return i;
  }
  return std::nullopt;
}

std::size_t root_window_limit(const Record& record) {
  if (record.header.signature != sig_qust) return record.subrecords.size();
  for (std::size_t i = 0; i < record.subrecords.size(); ++i) {
    const auto signature = record.subrecords[i].signature;
    if (signature == sig_next || signature == sig_indx) return i;
  }
  return record.subrecords.size();
}

bool has_ctda_in(const std::vector<Subrecord>& subs, std::size_t begin, std::size_t end) {
  for (std::size_t i = begin; i < end; ++i) {
    if (subs[i].signature == sig_ctda) return true;
  }
  return false;
}

using fo4::detail::find_mutable_record;

}  // namespace

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
  const auto window_begin = root_window_begin(record);
  if (!window_begin.has_value()) {
    return RootConditionWindow{RootConditionStatus::alias_structure_invalid, 0, 0, std::nullopt};
  }
  const auto start = *window_begin;
  const auto limit = root_window_limit(record);
  std::size_t cursor = start;
  for (const auto& range : *nested) {
    if (cursor < range.begin) consider(cursor, std::min(range.begin, limit));
    cursor = std::max(cursor, range.end);
  }
  consider(cursor, limit);

  if (lists > 1) return RootConditionWindow{RootConditionStatus::multiple_lists, 0, 0, std::nullopt};
  if (lists == 1) return found;

  const auto anchor = root_insert_anchor(record, start);
  if (!anchor.has_value()) {
    return RootConditionWindow{RootConditionStatus::no_list, 0, 0, std::nullopt};
  }
  std::size_t begin = start;
  std::size_t end = limit;
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
