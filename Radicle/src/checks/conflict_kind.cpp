#include "fo4/checks/conflict_kind.hpp"

#include "fo4/plugin/reader.hpp"
#include "fo4/schema/field_address.hpp"
#include "fo4/schema/scalar.hpp"
#include "../forms/quest_aliases_internal.hpp"

#include <algorithm>
#include <cstdint>
#include <iterator>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace fo4 {
namespace {

using Value = std::optional<std::string>;

struct Node {
  Value value;
  bool partial{false};
};

struct ElementResult {
  ConflictCategory category{ConflictCategory::unknown};
  std::vector<ConflictRole> roles;
};

struct VersionView {
  const Record* record{nullptr};
  bool partial{false};
  PluginKey plugin;
};

struct SubrecordKey {
  Signature signature{};
  std::size_t ordinal{0};

  friend bool operator<(const SubrecordKey& left, const SubrecordKey& right) {
    if (left.signature != right.signature) return left.signature < right.signature;
    return left.ordinal < right.ordinal;
  }
};

bool same_value(const Value& left, const Value& right) {
  if (left.has_value() != right.has_value()) return false;
  return !left.has_value() || *left == *right;
}

ConflictPriority element_priority(ConflictPriority priority, bool injected) {
  if (injected && priority >= ConflictPriority::normal) return ConflictPriority::critical;
  return priority;
}

struct ConflictFieldPriorityRule {
  Signature record{};
  Signature subrecord{};
  std::string_view path{};
  ConflictPriority priority{ConflictPriority::normal};
};

#include "xedit_conflict_priorities.inc"

std::optional<ConflictPriority> generated_rule(Signature record, Signature subrecord) {
  for (const auto& rule : k_xedit_subrecord_priorities) {
    if (rule.record == record && rule.subrecord == subrecord) return rule.priority;
  }
  return std::nullopt;
}

std::optional<ConflictPriority> generated_field_rule(Signature record,
                                                     Signature subrecord,
                                                     std::string_view path) {
  for (const auto& rule : k_xedit_field_priorities) {
    if (rule.record == record && rule.subrecord == subrecord && rule.path == path) return rule.priority;
  }
  return std::nullopt;
}

ConflictPriority rule_for(std::span<const ConflictPriorityRule> rules,
                          Signature record,
                          Signature subrecord) {
  for (const auto& rule : rules) {
    if (rule.record == record && rule.subrecord == subrecord) return rule.priority;
  }
  if (const auto generated = generated_rule(record, subrecord)) return *generated;
  const auto r = record.view();
  const auto s = subrecord.view();
  static constexpr std::string_view keyword_records[] = {
      "ACTI", "TACT", "ALCH", "AMMO", "ARMO", "BOOK", "CONT", "DOOR", "FURN", "MSTT",
      "IDLM", "LCTN", "SCEN", "ARTO", "INGR", "KEYM", "LIGH", "MGEF", "MISC", "NPC_",
      "RACE", "SPEL", "FLOR", "TERM", "WEAP"};
  if (s == "KSIZ" && std::find(std::begin(keyword_records), std::end(keyword_records), r) !=
                         std::end(keyword_records)) return ConflictPriority::benign;
  if ((s == "LLCT" && (r == "LVLI" || r == "LVLN" || r == "LVSP")) ||
      (s == "SPCT" && (r == "NPC_" || r == "RACE")) ||
      (s == "EDID" && r == "FLST") ||
      (s == "SNAM" && (r == "SMBN" || r == "SMQN" || r == "SMEN")) ||
      (s == "QNAM" && r == "SMQN") || (s == "PNAM" && r == "INFO") ||
      (s == "PRKZ" && r == "NPC_") || (s == "ITMC" && r == "SNDR") ||
      (s == "CS2H" && r == "NPC_") ||
      (s == "LFSP" && r == "LENS") || (r == "TERM" && (s == "BSIZ" || s == "ISIZ")) ||
      (s == "COCT" && (r == "CONT" || r == "FURN" || r == "NPC_")) ||
      (s == "XWCN" && (r == "CELL" || r == "ACHR" || r == "REFR")) ||
      (s == "IDLC" && (r == "IDLM" || r == "PACK")) ||
      (s == "OBTE" && (r == "ARMO" || r == "FURN" || r == "NPC_" || r == "WEAP")) ||
      (s == "CITC" && (r == "ACTI" || r == "FACT" || r == "FURN" || r == "SMBN" ||
                       r == "SMQN" || r == "SMEN" || r == "MUST")))
    return ConflictPriority::benign;
  if (s == "XLRL" && (r == "ACHR" || r == "REFR" || r == "PARW" || r == "PBAR" ||
      r == "PBEA" || r == "PCON" || r == "PFLA" || r == "PGRE" || r == "PHZD" || r == "PMIS"))
    return ConflictPriority::benign_if_added;
  if (r == "GMST" && s == "EDID") return ConflictPriority::critical;
  if ((r == "TACT" && (s == "PNAM" || s == "FNAM")) || (r == "CSTY" && s == "CSMD") ||
      (r == "DIAL" && s == "TIFC") || (r == "PROJ" && s == "DATA") ||
      (r == "PERK" && s == "PRKF") || (r == "MESG" && s == "INAM") ||
      (r == "MATO" && s == "DNAM") || (r == "REFR" && s == "XMBP") ||
      (r == "WRLD" && s == "NNAM")) return ConflictPriority::ignore;
  return ConflictPriority::normal;
}

ConflictPriority field_priority(std::span<const ConflictPriorityRule> rules,
                                Signature record,
                                const schema::ScalarFieldDescriptor& field) {
  const auto subrecord = field.scalar.subrecord;
  const bool caller_rule = std::any_of(rules.begin(), rules.end(), [&](const ConflictPriorityRule& rule) {
    return rule.record == record && rule.subrecord == subrecord;
  });
  if (!caller_rule) {
    if (const auto generated = generated_field_rule(record, subrecord, std::string_view(field.path))) {
      return *generated;
    }
  }
  return rule_for(rules, record, subrecord);
}

void append_u32(std::string& out, std::uint32_t value) {
  for (int shift = 0; shift < 32; shift += 8) {
    out.push_back(static_cast<char>((value >> shift) & 0xFFu));
  }
}

std::string subrecord_value(const Subrecord& subrecord) {
  if (subrecord.data.empty()) return {};
  return std::string(reinterpret_cast<const char*>(subrecord.data.data()), subrecord.data.size());
}

std::vector<VersionView> resolve_versions(const ConflictGraph& graph,
                                          std::span<const Plugin* const> load_order,
                                          const FormKey& key) {
  std::vector<VersionView> out;
  for (const auto& version : graph.versions(key)) {
    const Plugin* owner = nullptr;
    for (const auto* plugin : load_order) {
      if (plugin != nullptr && plugin->key == version.plugin) {
        owner = plugin;
        break;
      }
    }
    if (owner == nullptr) continue;
    out.push_back(VersionView{find_record(*owner, key), version.partial_form, version.plugin});
  }
  return out;
}

std::string record_value(const Record& record, std::span<const ConflictPriorityRule> rules) {
  std::string out;
  const auto signature = record.header.signature;
  out.append(signature.view());
  append_u32(out, static_cast<std::uint32_t>(record.header.flags.value()));
  for (const auto& subrecord : record.subrecords) {
    if (rule_for(rules, signature, subrecord.signature) == ConflictPriority::ignore) continue;
    out.append(subrecord.signature.view());
    append_u32(out, static_cast<std::uint32_t>(subrecord.data.size()));
    out.append(subrecord_value(subrecord));
  }
  return out;
}

ElementResult classify_nodes(std::span<const Node> nodes, ConflictPriority element_priority) {
  ElementResult out;
  const std::size_t count = nodes.size();
  out.roles.assign(count, ConflictRole::unknown);
  if (count == 0) return out;
  if (count == 1) {
    out.category = ConflictCategory::only_one;
    out.roles[0] = ConflictRole::only_one;
    return out;
  }

  std::size_t last = count - 1;
  while (last > 0 && !nodes[last].value.has_value() && nodes[last].partial) --last;

  std::set<std::string> unique_values;
  ConflictRole overall = ConflictRole::unknown;
  ConflictPriority effective = element_priority;
  for (std::size_t index = 0; index < count; ++index) {
    ConflictPriority priority = nodes[index].partial ? ConflictPriority::ignore : element_priority;
    if (priority != ConflictPriority::ignore) {
      unique_values.insert(nodes[index].value.value_or(std::string{}));
    }
    if (priority == ConflictPriority::ignore) {
      out.roles[index] = ConflictRole::ignored;
    } else if (index == 0) {
      out.roles[index] =
          nodes[index].value.has_value() ? ConflictRole::master : ConflictRole::unknown;
    } else {
      bool same_as_first = same_value(nodes[index].value, nodes[0].value);
      const bool same_as_last =
          index + 1 == count || same_value(nodes[index].value, nodes[last].value);
      if (!same_as_first && priority == ConflictPriority::benign_if_added && same_as_last &&
          !nodes[0].value.has_value()) {
        priority = ConflictPriority::benign;
        effective = ConflictPriority::benign;
        same_as_first = true;
      }
      if (same_as_first) {
        out.roles[index] = ConflictRole::identical_to_master;
      } else if (same_as_last) {
        out.roles[index] = ConflictRole::wins;
      } else {
        out.roles[index] = ConflictRole::loses;
      }
    }
    if (priority == ConflictPriority::benign && out.roles[index] > ConflictRole::benign) {
      out.roles[index] = ConflictRole::benign;
    }
    if (priority == ConflictPriority::override_ && out.roles[index] > ConflictRole::override_) {
      out.roles[index] = ConflictRole::override_;
    }
    overall = std::max(overall, out.roles[index]);
  }

  if (unique_values.size() <= 1) {
    out.category = ConflictCategory::no_conflict;
  } else if (unique_values.size() == 2) {
    if (!same_value(nodes[0].value, nodes[last].value)) {
      out.category = ConflictCategory::override_;
    } else if (unique_values.contains(std::string{}) && nodes[last].value.has_value() &&
               !nodes[last].value->empty()) {
      out.category = ConflictCategory::override_;
    } else {
      out.category = ConflictCategory::conflict;
    }
  } else {
    out.category = ConflictCategory::conflict;
  }

  if (out.category > ConflictCategory::no_conflict) {
    if (effective == ConflictPriority::benign) {
      out.category = ConflictCategory::benign;
    } else if (effective == ConflictPriority::override_) {
      out.category = ConflictCategory::override_;
    } else if (effective == ConflictPriority::critical) {
      unique_values.erase(std::string{});
      if (unique_values.size() > 1) out.category = ConflictCategory::critical;
    }
  }

  if (effective > ConflictPriority::benign && overall > ConflictRole::override_) {
    auto& last_role = out.roles[count - 1];
    if (last_role < ConflictRole::override_) {
      last_role = last_role == ConflictRole::identical_to_master
                      ? ConflictRole::identical_to_master_wins
                      : ConflictRole::wins;
    }
  }

  if (out.category == ConflictCategory::no_conflict ||
      out.category == ConflictCategory::override_ ||
      out.category == ConflictCategory::conflict) {
    for (std::size_t index = 0; index < count; ++index) {
      if (out.roles[index] == ConflictRole::identical_to_master) {
        if (index + 1 == count && out.category != ConflictCategory::no_conflict) {
          out.roles[index] = ConflictRole::identical_to_master_wins;
        }
      } else if (out.roles[index] == ConflictRole::wins) {
        if (out.category == ConflictCategory::no_conflict) {
          out.roles[index] = ConflictRole::identical_to_master;
        } else if (out.category == ConflictCategory::override_) {
          out.roles[index] = ConflictRole::override_;
        }
      }
    }
  }

  if (effective > ConflictPriority::benign && out.category < ConflictCategory::conflict) {
    for (std::size_t index = 0; index < count; ++index) {
      if (out.roles[index] >= ConflictRole::identical_to_master_wins) {
        out.category = ConflictCategory::conflict;
        break;
      }
    }
  }
  return out;
}

std::vector<Node> fresh_row(const std::vector<VersionView>& versions) {
  std::vector<Node> row(versions.size());
  for (std::size_t index = 0; index < versions.size(); ++index) {
    row[index].partial = versions[index].partial;
  }
  return row;
}

std::vector<FieldValueGroup> group_identical_versions(const std::vector<Node>& row,
                                                      const std::vector<VersionView>& versions) {
  std::vector<FieldValueGroup> out;
  std::vector<std::size_t> representatives;
  for (std::size_t index = 0; index < row.size(); ++index) {
    if (!row[index].value.has_value()) continue;
    bool placed = false;
    for (std::size_t slot = 0; slot < representatives.size(); ++slot) {
      if (*row[representatives[slot]].value != *row[index].value) continue;
      out[slot].plugins.push_back(versions[index].plugin);
      placed = true;
      break;
    }
    if (placed) continue;
    representatives.push_back(index);
    out.push_back(FieldValueGroup{{versions[index].plugin}});
  }
  return out;
}

struct SubrecordRows {
  std::vector<SubrecordKey> keys;
  std::vector<std::vector<Node>> rows;
};

SubrecordRows build_subrecord_rows(const std::vector<VersionView>& versions,
                                   Signature record_signature,
                                   std::span<const ConflictPriorityRule> rules) {
  SubrecordRows out;
  std::map<SubrecordKey, std::size_t> row_of;
  for (std::size_t index = 0; index < versions.size(); ++index) {
    const auto& version = versions[index];
    if (version.record == nullptr || version.partial) continue;
    std::map<Signature, std::size_t> seen;
    for (const auto& subrecord : version.record->subrecords) {
      if (rule_for(rules, record_signature, subrecord.signature) == ConflictPriority::ignore) {
        continue;
      }
      const SubrecordKey key{subrecord.signature, seen[subrecord.signature]++};
      std::size_t row = 0;
      const auto found = row_of.find(key);
      if (found == row_of.end()) {
        row = out.rows.size();
        row_of.emplace(key, row);
        out.keys.push_back(key);
        out.rows.push_back(fresh_row(versions));
      } else {
        row = found->second;
      }
      // Invariant: a present subrecord never compares equal to an absent one, even when empty.
      out.rows[row][index].value = std::string(1, 'P') + subrecord_value(subrecord);
    }
  }
  return out;
}

bool stable_reference_alias_counts(const std::vector<VersionView>& versions, Signature wanted) {
  namespace quest = fo4::forms::detail;
  std::optional<std::string> expected;
  bool any = false;
  for (const auto& version : versions) {
    if (version.record == nullptr || version.partial) continue;
    const auto& record = *version.record;
    try {
      const auto section = quest::inspect_quest_alias_section(record);
      std::string shape;
      std::size_t inside = 0;
      for (const auto& range : section.ranges) {
        if (range.collection) return false;
        const auto kind = record.subrecords[range.begin].signature;
        shape.append(kind.view());
        append_u32(shape, range.id);
        std::size_t count = 0;
        for (std::size_t at = range.begin + 1; at < range.end; ++at) {
          const auto& sub = record.subrecords[at];
          if (sub.signature == sig_almi) return false;
          if (sub.signature != wanted) continue;
          if (kind != sig_alst || sub.data.size() != 4 || ++count > 1) return false;
          ++inside;
        }
        shape.push_back(static_cast<char>(count));
      }
      std::size_t total = 0;
      for (std::size_t at = 0; at < record.subrecords.size(); ++at) {
        const auto tag = record.subrecords[at].signature;
        if (tag == wanted) ++total;
        if (at < section.anam_index && (tag == sig_alst || tag == sig_alls ||
            tag == sig_alcs || tag == sig_almi || tag == sig_aled)) return false;
      }
      if (total != inside) return false;
      any = any || inside != 0;
      if (expected && *expected != shape) return false;
      expected = std::move(shape);
    } catch (const fo4::forms::QuestError&) {
      return false;
    }
  }
  return any;
}

struct FieldByteRange {
  std::size_t begin{0};
  std::size_t end{0};
  ConflictPriority priority{ConflictPriority::normal};
};

std::optional<std::size_t> occurrence_index(const Record& record, const SubrecordKey& key) {
  std::size_t seen = 0;
  for (std::size_t index = 0; index < record.subrecords.size(); ++index) {
    if (record.subrecords[index].signature != key.signature) continue;
    if (seen++ == key.ordinal) return index;
  }
  return std::nullopt;
}

std::vector<FieldByteRange> low_priority_ranges(const Record& record,
                                                Signature record_signature,
                                                Signature subrecord,
                                                std::size_t index,
                                                std::span<const ConflictPriorityRule> rules) {
  std::vector<FieldByteRange> out;
  for (const auto& field : schema::scalar_fields(record_signature)) {
    if (field.scalar.subrecord != subrecord || !field.scalar.embedded) continue;
    const auto storage = field.scalar.storage;
    if (storage == schema::ScalarStorage::zstring || storage == schema::ScalarStorage::fixed_string ||
        storage == schema::ScalarStorage::bytes) continue;
    const auto priority = field_priority(rules, record_signature, field);
    if (priority != ConflictPriority::ignore && priority != ConflictPriority::benign) continue;
    const auto width = schema::scalar_storage_size(storage);
    const auto resolved = schema::resolve_field_slots(
        record, schema::scalar_field_shape(field.scalar), field.scalar.address);
    if (resolved.duplicate || resolved.malformed || resolved.misaligned || resolved.wrong_size) continue;
    for (const auto& slot : resolved.slots) {
      if (slot.valid && slot.subrecord == index) {
        out.push_back(FieldByteRange{slot.offset, slot.offset + width, priority});
      }
    }
  }
  return out;
}

std::optional<ConflictPriority> covering_priority(const std::vector<FieldByteRange>& ranges,
                                                  std::size_t at) {
  std::optional<ConflictPriority> out;
  for (const auto& range : ranges) {
    if (at < range.begin || at >= range.end) continue;
    if (!out || range.priority == ConflictPriority::benign) out = range.priority;
  }
  return out;
}

std::optional<ConflictPriority> field_lowered_priority(const std::vector<VersionView>& versions,
                                                       Signature record_signature,
                                                       const SubrecordKey& key,
                                                       std::span<const ConflictPriorityRule> rules) {
  if (std::any_of(rules.begin(), rules.end(), [&](const ConflictPriorityRule& rule) {
        return rule.record == record_signature && rule.subrecord == key.signature;
      })) return std::nullopt;
  const auto fields = schema::scalar_fields(record_signature);
  if (std::none_of(fields.begin(), fields.end(), [&](const schema::ScalarFieldDescriptor& field) {
        return field.scalar.subrecord == key.signature &&
               generated_field_rule(record_signature, key.signature, std::string_view(field.path))
                   .has_value();
      })) return std::nullopt;

  struct Part {
    const Subrecord* subrecord{nullptr};
    std::vector<FieldByteRange> ranges;
  };
  std::vector<Part> parts;
  for (const auto& version : versions) {
    if (version.record == nullptr || version.partial) continue;
    const auto index = occurrence_index(*version.record, key);
    if (!index) return std::nullopt;
    Part part;
    part.subrecord = &version.record->subrecords[*index];
    part.ranges = low_priority_ranges(*version.record, record_signature, key.signature, *index, rules);
    parts.push_back(std::move(part));
  }

  bool any_difference = false;
  bool any_benign = false;
  for (std::size_t left = 0; left < parts.size(); ++left) {
    for (std::size_t right = left + 1; right < parts.size(); ++right) {
      const auto& a = parts[left].subrecord->data;
      const auto& b = parts[right].subrecord->data;
      const std::size_t length = std::max(a.size(), b.size());
      for (std::size_t at = 0; at < length; ++at) {
        if (at < a.size() && at < b.size() && a[at] == b[at]) continue;
        any_difference = true;
        const auto in_left = covering_priority(parts[left].ranges, at);
        const auto in_right = covering_priority(parts[right].ranges, at);
        if (!in_left || !in_right) return std::nullopt;
        if (*in_left == ConflictPriority::benign || *in_right == ConflictPriority::benign) {
          any_benign = true;
        }
      }
    }
  }
  if (!any_difference) return std::nullopt;
  return any_benign ? ConflictPriority::benign : ConflictPriority::ignore;
}

void append_subrecord_rows(const std::vector<VersionView>& versions,
                           Signature record_signature,
                           std::span<const ConflictPriorityRule> rules,
                           bool injected,
                           std::vector<SubrecordConflict>& out) {
  auto built = build_subrecord_rows(versions, record_signature, rules);
  const auto& keys = built.keys;
  const auto& rows = built.rows;
  const auto scoped = [&](Signature tag) {
    if (record_signature != Signature::from_string("QUST") ||
        (tag != Signature::from_string("KSIZ") && tag != Signature::from_string("COCT"))) return false;
    if (std::none_of(keys.begin(), keys.end(), [&](const auto& key) { return key.signature == tag; })) return false;
    for (const auto& rule : rules)
      if (rule.record == record_signature && rule.subrecord == tag) return false;
    return stable_reference_alias_counts(versions, tag);
  };
  const bool alias_keywords = scoped(Signature::from_string("KSIZ"));
  const bool alias_items = scoped(Signature::from_string("COCT"));

  for (std::size_t row = 0; row < rows.size(); ++row) {
    const auto tag = keys[row].signature;
    auto priority = (tag == Signature::from_string("KSIZ") && alias_keywords) ||
                                  (tag == Signature::from_string("COCT") && alias_items)
                              ? ConflictPriority::benign
                              : rule_for(rules, record_signature, tag);
    if (priority == ConflictPriority::normal) {
      if (const auto lowered = field_lowered_priority(versions, record_signature, keys[row], rules)) {
        priority = *lowered;
      }
    }
    const auto result = classify_nodes(
        rows[row], element_priority(priority, injected));
    SubrecordConflict conflict;
    conflict.signature = keys[row].signature;
    conflict.ordinal = keys[row].ordinal;
    conflict.category = result.category;
    conflict.roles = result.roles;
    out.push_back(std::move(conflict));
  }
}

struct FieldRead {
  Value value;
  bool unavailable{false};
};

FieldRead field_value(const Record& record, const schema::ScalarFieldDescriptor& field) {
  const auto resolved = schema::resolve_field_slots(
      record, schema::scalar_field_shape(field.scalar), field.scalar.address);
  if (resolved.duplicate || resolved.malformed || resolved.misaligned || resolved.wrong_size)
    return {std::nullopt, true};
  std::string out;
  bool any = false;
  for (const auto& slot : resolved.slots) {
    if (!slot.valid) continue;
    try {
      const auto bytes =
          schema::scalar_slot_bytes(record.subrecords[slot.subrecord].data, slot, field.scalar);
      out.append(reinterpret_cast<const char*>(bytes.data()), bytes.size());
      any = true;
    } catch (const schema::ScalarError&) {
      return {std::nullopt, true};
    }
  }
  if (!any) return {};
  return {std::move(out), false};
}

void append_field_rows(const std::vector<VersionView>& versions,
                       Signature record_signature,
                       std::span<const ConflictPriorityRule> rules,
                       bool injected,
                       std::vector<FieldConflict>& out,
                       std::vector<ElementResult>& parent_rows) {
  for (const auto& field : schema::scalar_fields(record_signature)) {
    const auto priority = field_priority(rules, record_signature, field);
    if (priority == ConflictPriority::ignore) continue;
    std::vector<Node> row = fresh_row(versions);
    std::vector<bool> unavailable(versions.size(), false);
    bool any = false;
    for (std::size_t index = 0; index < versions.size(); ++index) {
      if (versions[index].record == nullptr || versions[index].partial) continue;
      auto value = field_value(*versions[index].record, field);
      unavailable[index] = value.unavailable;
      if (!value.value.has_value()) continue;
      row[index].value = std::move(value.value);
      any = true;
    }
    if (!any) continue;
    std::vector<Node> available;
    for (std::size_t index = 0; index < row.size(); ++index)
      if (!unavailable[index]) available.push_back(row[index]);
    auto result = !unavailable.empty() && unavailable.front() && available.size() > 1
                      ? ElementResult{ConflictCategory::unknown,
                                      std::vector<ConflictRole>(available.size(), ConflictRole::unknown)}
                      : classify_nodes(available, element_priority(priority, injected));
    const auto available_roles = std::move(result.roles);
    result.roles.assign(row.size(), ConflictRole::ignored);
    for (std::size_t index = 0, next = 0; index < row.size(); ++index)
      if (!unavailable[index]) result.roles[index] = available_roles[next++];
    if (std::none_of(unavailable.begin(), unavailable.end(), [](bool value) { return value; }))
      parent_rows.push_back(result);
    FieldConflict conflict;
    conflict.path = std::string(field.path);
    conflict.category = result.category;
    conflict.roles = std::move(result.roles);
    out.push_back(std::move(conflict));
  }
}

bool record_is_injected(const std::vector<VersionView>& versions,
                        std::span<const Plugin* const> load_order,
                        const FormKey& key) {
  const Plugin* owner = nullptr;
  for (const auto* plugin : load_order) {
    if (plugin != nullptr && plugin->key == key.owner()) {
      owner = plugin;
      break;
    }
  }
  if (owner == nullptr) return false;
  if (find_record(*owner, key) != nullptr) return false;
  for (const auto& version : versions) {
    if (version.record != nullptr && version.record->form_key.owner() == key.owner()) return true;
  }
  return false;
}

}  // namespace

RecordConflict classify_record_conflict(const ConflictGraph& graph, const ConflictQuery& query) {
  RecordConflict out;
  out.key = query.key;
  const auto versions = resolve_versions(graph, query.load_order, query.key);
  if (versions.empty()) return out;

  Signature signature{};
  for (const auto& version : versions) {
    if (version.record == nullptr || version.partial) continue;
    signature = version.record->header.signature;
    break;
  }
  const bool injected = signature != sig_gmst && signature != sig_dfob &&
                        record_is_injected(versions, query.load_order, query.key);

  std::vector<Node> record_nodes = fresh_row(versions);
  std::vector<Node> order_nodes = fresh_row(versions);
  std::map<Signature, std::size_t> shared_counts;
  bool have_counts = false;
  for (std::size_t index = 0; index < versions.size(); ++index) {
    if (versions[index].record == nullptr || versions[index].partial) continue;
    const auto& record = *versions[index].record;
    if (query.record_priority != ConflictPriority::normal) {
      record_nodes[index].value = record_value(record, query.priorities);
      continue;
    }
    std::string header(record.header.signature.view());
    append_u32(header, static_cast<std::uint32_t>(record.header.flags.value()));
    record_nodes[index].value = std::move(header);
    std::map<Signature, std::size_t> counts;
    for (const auto& sub : record.subrecords) {
      if (rule_for(query.priorities, signature, sub.signature) != ConflictPriority::ignore)
        ++counts[sub.signature];
    }
    if (!have_counts) {
      shared_counts = std::move(counts);
      have_counts = true;
    } else {
      for (auto& [tag, count] : shared_counts) count = std::min(count, counts[tag]);
    }
  }
  if (query.record_priority == ConflictPriority::normal) {
    for (std::size_t index = 0; index < versions.size(); ++index) {
      if (versions[index].record == nullptr || versions[index].partial) continue;
      std::map<Signature, std::size_t> seen;
      std::string order;
      for (const auto& sub : versions[index].record->subrecords) {
        const auto found = shared_counts.find(sub.signature);
        if (found != shared_counts.end() && seen[sub.signature]++ < found->second)
          order.append(sub.signature.view());
      }
      order_nodes[index].value = std::move(order);
    }
  }

  const auto result =
      classify_nodes(record_nodes, element_priority(query.record_priority, injected));
  out.category = result.category;
  out.roles = result.roles;

  std::vector<ElementResult> field_parent_rows;
  for (const auto& version : versions) {
    if (version.record == nullptr || version.partial) continue;
    append_subrecord_rows(versions, signature, query.priorities, injected, out.subrecords);
    append_field_rows(versions, signature, query.priorities, injected, out.fields, field_parent_rows);
    break;
  }
  if (query.record_priority == ConflictPriority::normal && versions.size() > 1) {
    const auto order_result = classify_nodes(order_nodes, element_priority(ConflictPriority::normal, injected));
    out.category = std::max(out.category, order_result.category);
    for (std::size_t i = 0; i < out.roles.size(); ++i) out.roles[i] = std::max(out.roles[i], order_result.roles[i]);
    for (const auto& child : out.subrecords) {
      out.category = std::max(out.category, child.category);
      for (std::size_t i = 0; i < out.roles.size(); ++i) out.roles[i] = std::max(out.roles[i], child.roles[i]);
    }
    for (const auto& field : field_parent_rows) {
      out.category = std::max(out.category, field.category);
      for (std::size_t i = 0; i < out.roles.size(); ++i) out.roles[i] = std::max(out.roles[i], field.roles[i]);
    }
  }
  return out;
}

std::vector<RecordConflict> classify_conflicts(const ConflictGraph& graph,
                                               std::span<const Plugin* const> load_order,
                                               std::span<const ConflictPriorityRule> priorities) {
  std::vector<RecordConflict> out;
  for (const auto& key : graph.all_keys()) {
    out.push_back(classify_record_conflict(graph, ConflictQuery{load_order, key,
                                                                ConflictPriority::normal,
                                                                priorities}));
  }
  return out;
}

RecordComparison compare_record_across_plugins(const ConflictGraph& graph,
                                               std::span<const Plugin* const> load_order,
                                               const FormKey& key,
                                               std::span<const ConflictPriorityRule> priorities) {
  RecordComparison out;
  out.key = key;
  const auto versions = resolve_versions(graph, load_order, key);
  if (versions.empty()) return out;

  std::vector<Node> record_nodes = fresh_row(versions);
  Signature signature{};
  bool have_record = false;
  for (std::size_t index = 0; index < versions.size(); ++index) {
    if (versions[index].record == nullptr || versions[index].partial) continue;
    record_nodes[index].value = record_value(*versions[index].record, priorities);
    if (!have_record) {
      signature = versions[index].record->header.signature;
      have_record = true;
    }
  }
  out.record_groups = group_identical_versions(record_nodes, versions);
  if (!have_record) return out;

  const auto built = build_subrecord_rows(versions, signature, priorities);
  out.fields.reserve(built.rows.size());
  for (std::size_t row = 0; row < built.rows.size(); ++row) {
    FieldComparison field;
    field.signature = built.keys[row].signature;
    field.ordinal = built.keys[row].ordinal;
    field.groups = group_identical_versions(built.rows[row], versions);
    out.fields.push_back(std::move(field));
  }
  return out;
}

std::vector<RecordComparison> compare_conflicts_across_plugins(
    const ConflictGraph& graph,
    std::span<const Plugin* const> load_order,
    std::span<const ConflictPriorityRule> priorities) {
  std::vector<RecordComparison> out;
  for (const auto& key : graph.conflicts()) {
    out.push_back(compare_record_across_plugins(graph, load_order, key, priorities));
  }
  return out;
}

std::string_view conflict_category_name(ConflictCategory category) noexcept {
  switch (category) {
    case ConflictCategory::unknown: return "unknown";
    case ConflictCategory::only_one: return "only_one";
    case ConflictCategory::no_conflict: return "no_conflict";
    case ConflictCategory::benign: return "benign";
    case ConflictCategory::override_: return "override";
    case ConflictCategory::conflict: return "conflict";
    case ConflictCategory::critical: return "critical";
  }
  return "unknown";
}

std::string_view conflict_role_name(ConflictRole role) noexcept {
  switch (role) {
    case ConflictRole::unknown: return "unknown";
    case ConflictRole::ignored: return "ignored";
    case ConflictRole::identical_to_master: return "identical_to_master";
    case ConflictRole::only_one: return "only_one";
    case ConflictRole::master: return "master";
    case ConflictRole::benign: return "benign";
    case ConflictRole::override_: return "override";
    case ConflictRole::identical_to_master_wins: return "identical_to_master_wins";
    case ConflictRole::wins: return "wins";
    case ConflictRole::loses: return "loses";
  }
  return "unknown";
}

}  // namespace fo4
