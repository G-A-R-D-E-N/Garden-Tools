#include "fo4/graph/conflict_kind.hpp"

#include "fo4/plugin/reader.hpp"
#include "fo4/schema/field_address.hpp"
#include "fo4/schema/scalar.hpp"

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

ConflictPriority element_priority(ConflictPriority priority, bool injected) {
  if (injected && priority >= ConflictPriority::normal) return ConflictPriority::critical;
  return priority;
}

constexpr Signature k_gmst = Signature::from_string("GMST");
constexpr Signature k_dfob = Signature::from_string("DFOB");

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
                           bool injected,
                           std::vector<SubrecordConflict>& out) {
  auto built = build_subrecord_rows(versions, record_signature, rules);
  const auto& keys = built.keys;
  const auto& rows = built.rows;

  for (std::size_t row = 0; row < rows.size(); ++row) {
    const auto result = classify_nodes(
        rows[row], element_priority(rule_for(rules, record_signature, keys[row].signature),
                                    injected));
    SubrecordConflict conflict;
    conflict.signature = keys[row].signature;
    conflict.ordinal = keys[row].ordinal;
    conflict.category = result.category;
    conflict.roles = result.roles;
    out.push_back(std::move(conflict));
  }
}

std::optional<std::string> field_value(const Record& record,
                                      const schema::ScalarFieldDescriptor& field) {
  const auto resolved = schema::resolve_field_slots(
      record, schema::scalar_field_shape(field.scalar), field.scalar.address);
  if (resolved.duplicate || resolved.malformed || resolved.misaligned) return std::nullopt;
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
      return std::nullopt;
    }
  }
  if (!any) return std::nullopt;
  return out;
}

void append_field_rows(const std::vector<VersionView>& versions,
                       Signature record_signature,
                       std::span<const ConflictPriorityRule> rules,
                       bool injected,
                       std::vector<FieldConflict>& out) {
  for (const auto& field : schema::scalar_fields(record_signature)) {
    const auto priority = rule_for(rules, record_signature, field.scalar.subrecord);
    if (priority == ConflictPriority::ignore) continue;
    std::vector<Node> row = fresh_row(versions);
    bool any = false;
    for (std::size_t index = 0; index < versions.size(); ++index) {
      if (versions[index].record == nullptr || versions[index].partial) continue;
      auto value = field_value(*versions[index].record, field);
      if (!value.has_value()) continue;
      row[index].value = std::move(*value);
      any = true;
    }
    if (!any) continue;
    const auto result = classify_nodes(row, element_priority(priority, injected));
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
  const bool injected = signature != k_gmst && signature != k_dfob &&
                        record_is_injected(versions, query.load_order, query.key);

  std::vector<Node> record_nodes = fresh_row(versions);
  for (std::size_t index = 0; index < versions.size(); ++index) {
    if (versions[index].record == nullptr || versions[index].partial) continue;
    record_nodes[index].value = record_value(*versions[index].record, query.priorities);
  }

  const auto result =
      classify_nodes(record_nodes, element_priority(query.record_priority, injected));
  out.category = result.category;
  out.roles = result.roles;

  for (const auto& version : versions) {
    if (version.record == nullptr || version.partial) continue;
    append_subrecord_rows(versions, signature, query.priorities, injected, out.subrecords);
    append_field_rows(versions, signature, query.priorities, injected, out.fields);
    break;
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
