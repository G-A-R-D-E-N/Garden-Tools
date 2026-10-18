#include "fo4/graph/conflict_kind.hpp"

#include "fo4/plugin/reader.hpp"

#include <algorithm>
#include <cstdint>
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

ConflictPriority rule_for(std::span<const ConflictPriorityRule> rules,
                          Signature record,
                          Signature subrecord) {
  for (const auto& rule : rules) {
    if (rule.record == record && rule.subrecord == subrecord) return rule.priority;
  }
  return ConflictPriority::normal;
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
          index + 1 == count || same_value(nodes[index].value, nodes[index - 1].value);
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

  std::size_t last = count - 1;
  while (last > 0 && !nodes[last].value.has_value() && nodes[last].partial) --last;

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

  if (out.category < ConflictCategory::conflict) {
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
      out.rows[row][index].value = subrecord_value(subrecord);
    }
  }
  return out;
}

void append_subrecord_rows(const std::vector<VersionView>& versions,
                           Signature record_signature,
                           std::span<const ConflictPriorityRule> rules,
                           std::vector<SubrecordConflict>& out) {
  auto built = build_subrecord_rows(versions, record_signature, rules);
  const auto& keys = built.keys;
  const auto& rows = built.rows;

  for (std::size_t row = 0; row < rows.size(); ++row) {
    const auto result =
        classify_nodes(rows[row], rule_for(rules, record_signature, keys[row].signature));
    SubrecordConflict conflict;
    conflict.signature = keys[row].signature;
    conflict.ordinal = keys[row].ordinal;
    conflict.category = result.category;
    conflict.roles = result.roles;
    out.push_back(std::move(conflict));
  }
}

}  // namespace

RecordConflict classify_record_conflict(const ConflictGraph& graph, const ConflictQuery& query) {
  RecordConflict out;
  out.key = query.key;
  const auto versions = resolve_versions(graph, query.load_order, query.key);
  if (versions.empty()) return out;

  std::vector<Node> record_nodes = fresh_row(versions);
  for (std::size_t index = 0; index < versions.size(); ++index) {
    if (versions[index].record == nullptr || versions[index].partial) continue;
    record_nodes[index].value = record_value(*versions[index].record, query.priorities);
  }

  const auto result = classify_nodes(record_nodes, query.record_priority);
  out.category = result.category;
  out.roles = result.roles;

  for (const auto& version : versions) {
    if (version.record == nullptr || version.partial) continue;
    append_subrecord_rows(versions, version.record->header.signature, query.priorities,
                          out.subrecords);
    break;
  }
  return out;
}

std::vector<RecordConflict> classify_conflicts(const ConflictGraph& graph,
                                               std::span<const Plugin* const> load_order,
                                               std::span<const ConflictPriorityRule> priorities) {
  std::vector<RecordConflict> out;
  for (const auto& key : graph.conflicts()) {
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

}  // namespace fo4
