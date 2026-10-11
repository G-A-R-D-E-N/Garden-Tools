#include "fo4/quest/structure.hpp"

#include "fo4/plugin/reader.hpp"

#include <algorithm>
#include <exception>
#include <new>
#include <span>
#include <stdexcept>
#include <string_view>

namespace fo4::quest {
namespace {

bool is(const Subrecord& member, std::string_view signature) {
  return member.signature == Signature::from_string(signature);
}

bool condition_member(const Subrecord& member) {
  return is(member, "CTDA") || is(member, "CIS1") || is(member, "CIS2");
}

void unsupported(QuestStructure& out, QuestScope scope, std::size_t primary,
                 std::size_t secondary, std::size_t member, Signature signature,
                 std::string reason) {
  out.unsupported.push_back({scope, primary, secondary, member, signature, std::move(reason)});
}

void conditions(QuestStructure& out, QuestScope scope, std::size_t primary,
                std::size_t secondary, std::span<const Subrecord> members,
                bool prefix_only = false, std::size_t prefix_size = 0) {
  QuestConditionList list{scope, primary, secondary, true, {}};
  std::size_t runs = 0;
  for (std::size_t i = 0; i < members.size();) {
    if (!condition_member(members[i])) {
      ++i;
      continue;
    }
    const auto begin = i;
    while (i < members.size() && condition_member(members[i])) ++i;
    ++runs;
    std::vector<CtdaCondition> parsed;
    std::size_t consumed = 0;
    const auto run = members.subspan(begin, i - begin);
    if ((prefix_only && begin != prefix_size) || !is(run.front(), "CTDA") ||
        !forms::lossless_quest_log_entry_conditions(run, parsed, consumed) ||
        consumed != run.size()) {
      list.supported = false;
      unsupported(out, scope, primary, secondary, begin, run.front().signature,
                  "condition run is out of scope order or is not losslessly decodable");
    } else {
      list.conditions = std::move(parsed);
    }
  }
  if (runs > 1) {
    list.supported = false;
    unsupported(out, scope, primary, secondary, 0, Signature::from_string("CTDA"),
                "scope contains more than one condition run");
  }
  if (!list.supported) list.conditions.clear();
  out.condition_lists.push_back(std::move(list));
}

void quest_conditions(QuestStructure& out, const Record& record) {
  const auto end = std::find_if(record.subrecords.begin(), record.subrecords.end(),
                               [](const Subrecord& member) {
                                 return is(member, "INDX") || is(member, "QOBJ") ||
                                        is(member, "ANAM");
                               });
  const auto prefix = std::span<const Subrecord>(record.subrecords.data(),
      static_cast<std::size_t>(end - record.subrecords.begin()));
  std::size_t boundary = prefix.size();
  std::size_t markers = 0;
  bool marker_empty = true;
  for (std::size_t i = 0; i < prefix.size(); ++i) {
    if (!is(prefix[i], "NEXT")) continue;
    boundary = i;
    ++markers;
    marker_empty = marker_empty && prefix[i].data.empty();
  }
  const bool has_conditions = std::any_of(prefix.begin(), prefix.end(), condition_member);
  if (markers > 1 || !marker_empty || (has_conditions && markers != 1)) {
    out.condition_lists.push_back({QuestScope::dialogue, 0, 0, false, {}});
    out.condition_lists.push_back({QuestScope::story_manager, 0, 0, false, {}});
    unsupported(out, QuestScope::dialogue, 0, 0, boundary, Signature::from_string("NEXT"),
                "quest condition ownership requires one empty NEXT boundary");
    return;
  }
  conditions(out, QuestScope::dialogue, 0, 0, prefix.first(boundary));
  conditions(out, QuestScope::story_manager, 0, 0,
             markers == 1 ? prefix.subspan(boundary + 1) : std::span<const Subrecord>{});
}

void validate_objective_flags(const Record& record) {
  bool objective = false;
  bool target = false;
  std::size_t flags = 0;
  for (const auto& member : record.subrecords) {
    if (is(member, "QOBJ") || is(member, "ANAM")) {
      if (objective && flags > 1) {
        throw forms::QuestError("objective allows at most one FNAM Flags member");
      }
      if (is(member, "ANAM")) return;
      objective = true;
      target = false;
      flags = 0;
    } else if (objective && is(member, "QSTA")) {
      target = true;
    } else if (objective && is(member, "FNAM")) {
      if (target) throw forms::QuestError("objective FNAM Flags follows a QSTA target");
      ++flags;
    }
  }
  if (objective && flags > 1) {
    throw forms::QuestError("objective allows at most one FNAM Flags member");
  }
}

void validate_alias_names(const forms::QuestAliases& aliases) {
  for (const auto& alias : aliases.aliases) {
    for (const auto& member : alias.subrecords) {
      if (!is(member, "ALID")) continue;
      if (!alias.name || member.data.size() != alias.name->size() + 1 ||
          member.data.back() != std::byte{0}) {
        throw forms::QuestError("alias name is not one exact NUL-terminated string");
      }
    }
  }
}

}

QuestStructure read_quest_structure(const Plugin& plugin, const FormKey& quest) {
  const auto* record = find_record(plugin, quest);
  if (!record) throw std::out_of_range("QUST record not found: " + quest.to_string());
  if (record->header.signature != Signature::from_string("QUST")) {
    throw std::invalid_argument("record is not QUST: " + quest.to_string());
  }
  QuestStructure out;
  const auto family = [&](QuestScope scope, auto&& decode) {
    try {
      decode();
      return true;
    } catch (const std::bad_alloc&) {
      throw;
    } catch (const std::exception& error) {
      unsupported(out, scope, 0, 0, 0, Signature{}, error.what());
      return false;
    }
  };
  out.stages_supported = family(QuestScope::stages, [&] {
    out.stages = forms::decode_quest_stages(plugin, quest);
  });
  out.objectives_supported = family(QuestScope::objectives, [&] {
    validate_objective_flags(*record);
    out.objectives = forms::decode_quest_objectives(plugin, quest);
  });
  out.aliases_supported = family(QuestScope::aliases, [&] {
    auto aliases = forms::decode_quest_aliases(plugin, quest);
    validate_alias_names(aliases);
    out.aliases = std::move(aliases);
  });
  quest_conditions(out, *record);
  for (std::size_t s = 0; s < out.stages.stages.size(); ++s) {
    const auto& stage = out.stages.stages[s];
    for (std::size_t m = 0; m < stage.leading_members.size(); ++m) {
      unsupported(out, QuestScope::stages, s, 0, m, stage.leading_members[m].signature,
                  "stage leading member has no typed projection");
    }
    for (std::size_t e = 0; e < stage.log_entries.size(); ++e) {
      const auto& entry = stage.log_entries[e];
      conditions(out, QuestScope::log_entry, s, e, entry.members, true);
      for (std::size_t m = 0; m < entry.members.size(); ++m) {
        const auto& member = entry.members[m];
        if (!condition_member(member) && !is(member, "CNAM") && !is(member, "NAM2")) {
          unsupported(out, QuestScope::log_entry, s, e, m, member.signature,
                      "log entry member has no typed projection");
        }
      }
    }
  }
  for (std::size_t o = 0; o < out.objectives.objectives.size(); ++o) {
    const auto& objective = out.objectives.objectives[o];
    for (std::size_t t = 0; t < objective.targets.size(); ++t) {
      const auto& members = objective.targets[t].subrecords;
      conditions(out, QuestScope::objective_target, o, t, members, true);
      if (objective.targets[t].keyword_unresolved) {
        unsupported(out, QuestScope::objective_target, o, t, 0, Signature::from_string("QSTA"),
                    "objective target link is unresolved");
      }
      for (std::size_t m = 0; m < members.size(); ++m) {
        if (!condition_member(members[m])) {
          unsupported(out, QuestScope::objective_target, o, t, m, members[m].signature,
                      "objective target member has no typed projection");
        }
      }
    }
  }
  constexpr std::string_view known_alias_members[] = {
      "ALST", "ALLS", "ALCS", "ALMI", "ALID", "FNAM", "ALFI", "ALFR", "ALUA",
      "ALFA", "ALRT", "ALEQ", "ALEA", "ALCO", "ALCA", "ALCL", "ALNA", "ALNT",
      "ALFE", "ALFD", "ALCC", "ALFL", "KNAM", "ALED", "KSIZ", "KWDA", "COCT",
      "CNTO", "COED", "SPOR", "OCOR", "GWOR", "ECOR", "ALLA", "ALDN", "ALFV",
      "ALDI", "ALSP", "ALFC", "ALPC", "VTCK"};
  for (std::size_t a = 0; a < out.aliases.aliases.size(); ++a) {
    const auto& alias = out.aliases.aliases[a];
    constexpr std::string_view prefix_members[] = {
        "ALST", "ALLS", "ALID", "FNAM", "ALFI", "ALFR", "ALUA", "ALFA", "ALRT",
        "ALEQ", "ALEA", "ALCO", "ALCA", "ALCL", "ALNA", "ALNT", "ALFE", "ALFD",
        "ALFL", "KNAM"};
    const auto is_prefix = [&](const Subrecord& member) {
      return (alias.kind == forms::QuestAliasKind::reference && is(member, "ALCC")) ||
          std::any_of(std::begin(prefix_members), std::end(prefix_members),
                      [&](std::string_view name) { return is(member, name); });
    };
    const auto prefix_end = std::find_if(alias.subrecords.begin(), alias.subrecords.end(),
                                         [&](const auto& member) { return !is_prefix(member); });
    const auto prefix_size = static_cast<std::size_t>(prefix_end - alias.subrecords.begin());
    const bool prefix_closed = std::none_of(prefix_end, alias.subrecords.end(), is_prefix);
    conditions(out, QuestScope::alias, a, 0, alias.subrecords, true,
               prefix_closed ? prefix_size : alias.subrecords.size());
    for (const auto signature : alias.unresolved_links) {
      unsupported(out, QuestScope::alias, a, 0, 0, signature, "alias link is unresolved");
    }
    for (std::size_t m = 0; m < alias.subrecords.size(); ++m) {
      const auto& member = alias.subrecords[m];
      if (condition_member(member)) continue;
      if (std::none_of(std::begin(known_alias_members), std::end(known_alias_members),
                       [&](std::string_view name) { return is(member, name); })) {
        unsupported(out, QuestScope::alias, a, 0, m, member.signature,
                    "alias member has no typed projection");
      }
    }
  }
  return out;
}

}
