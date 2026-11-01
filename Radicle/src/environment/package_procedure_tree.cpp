#include "fo4/environment/package_procedure.hpp"

#include "../plugin/record_edit.hpp"
#include "fo4/schema/scalar.hpp"
#include "record_reader.hpp"

#include <algorithm>
#include <array>
#include <cstring>
#include <optional>
#include <span>
#include <utility>
#include <vector>

namespace fo4::env {
namespace {

using detail::Reader;
constexpr Signature sig(const char (&s)[5]) { return Signature{s[0], s[1], s[2], s[3]}; }

constexpr std::uint8_t max_input_id = 0xFE;
constexpr std::size_t input_id_count = 0xFF;

std::vector<std::byte> tree_zstring(std::string_view text) {
  std::vector<std::byte> out;
  out.reserve(text.size() + 1);
  for (const char ch : text) out.push_back(std::byte{static_cast<std::uint8_t>(ch)});
  out.push_back(std::byte{0});
  return out;
}

std::vector<std::byte> tree_dword(std::uint32_t value) {
  std::vector<std::byte> out(4, std::byte{0});
  std::memcpy(out.data(), &value, 4);
  return out;
}

void write_dword(std::vector<std::byte>& data, std::size_t offset, std::uint32_t value) {
  if (offset + 4 > data.size()) return;
  for (std::size_t i = 0; i < 4; ++i) {
    data[offset + i] = std::byte{static_cast<std::uint8_t>(value >> (8 * i))};
  }
}

std::uint32_t read_dword(std::span<const std::byte> data, std::size_t offset) {
  std::uint32_t value = 0;
  std::memcpy(&value, data.data() + offset, sizeof(value));
  return value;
}

std::optional<std::size_t> pkcu_index(const Record& record) {
  for (std::size_t index = 0; index < record.subrecords.size(); ++index) {
    if (record.subrecords[index].signature == sig("PKCU") && record.subrecords[index].data.size() == 12) {
      return index;
    }
  }
  return std::nullopt;
}

std::optional<std::size_t> parent_of(const PackageProcedureTree& tree, std::size_t node) {
  for (std::size_t index = 0; index < tree.nodes.size(); ++index) {
    for (const auto child : tree.nodes[index].children) {
      if (child == node) return index;
    }
  }
  return std::nullopt;
}

std::size_t node_depth(const PackageProcedureTree& tree, std::size_t node) {
  std::size_t depth = 0;
  auto current = parent_of(tree, node);
  while (current) {
    ++depth;
    current = parent_of(tree, *current);
  }
  return depth;
}

bool read_leaf_input_ids(const Record& record, const PackageProcedureNode& node, std::vector<std::uint8_t>& ids) {
  ids.clear();
  if (node.opaque_begin > node.opaque_end || node.opaque_end > record.subrecords.size()) return false;
  for (std::size_t at = node.opaque_begin; at < node.opaque_end; ++at) {
    const auto& sub = record.subrecords[at];
    if (sub.signature == sig("FNAM") && sub.data.size() == 4) continue;
    if (sub.signature == sig("PKC2") && sub.data.size() == 1) {
      ids.push_back(std::to_integer<std::uint8_t>(sub.data[0]));
      continue;
    }
    return false;
  }
  return true;
}

std::optional<std::uint32_t> location_input_target(const Record& record, const PackageDataInput& input) {
  if (input.value_tag != sig("PLDT") || input.value_index >= record.subrecords.size()) return std::nullopt;
  const auto& data = record.subrecords[input.value_index].data;
  if (data.size() < 8) return std::nullopt;
  if (static_cast<std::int32_t>(read_dword(data, 0)) != 11) return std::nullopt;
  return read_dword(data, 4);
}

struct Rebuild {
  const Record& record;
  const PackageProcedureTree& tree;
  std::size_t synthetic;
  std::optional<std::size_t> skip;
  std::string_view procedure;
  std::vector<Subrecord> out;
  std::vector<std::uint8_t> synthetic_inputs;
};

std::size_t emit_node(Rebuild& rebuild, std::size_t index) {
  const auto& node = rebuild.tree.nodes[index];
  std::size_t emitted = 0;
  if (index == rebuild.synthetic) {
    rebuild.out.push_back(Subrecord{sig("ANAM"), tree_zstring("Procedure"), false});
    rebuild.out.push_back(Subrecord{sig("CITC"), tree_dword(0), false});
    rebuild.out.push_back(Subrecord{sig("PNAM"), tree_zstring(rebuild.procedure), false});
    for (const auto id : rebuild.synthetic_inputs) {
      rebuild.out.push_back(Subrecord{sig("PKC2"), std::vector<std::byte>{std::byte{id}}, false});
    }
    return 1;
  }
  rebuild.out.push_back(rebuild.record.subrecords[node.begin]);
  for (std::size_t at = node.condition_begin; at < node.condition_end; ++at) {
    rebuild.out.push_back(rebuild.record.subrecords[at]);
  }
  std::size_t flags_begin = node.end;
  if (node.kind == PackageProcedureKind::leaf) {
    rebuild.out.push_back(rebuild.record.subrecords[node.condition_end]);
    for (std::size_t at = node.opaque_begin; at < node.opaque_end; ++at) {
      rebuild.out.push_back(rebuild.record.subrecords[at]);
    }
    flags_begin = node.opaque_end;
  } else {
    Subrecord prcb = rebuild.record.subrecords[node.prcb_index];
    std::size_t children = 0;
    for (const auto child : node.children) {
      if (rebuild.skip && *rebuild.skip == child) continue;
      ++children;
    }
    write_dword(prcb.data, 0, static_cast<std::uint32_t>(children));
    rebuild.out.push_back(std::move(prcb));
    flags_begin = node.children_anchor;
    for (const auto child : node.children) {
      // Invariant: flags_begin follows every original child, including a skipped (removed) one.
      if (child != rebuild.synthetic) flags_begin = rebuild.tree.nodes[child].end;
      if (rebuild.skip && *rebuild.skip == child) continue;
      emitted += emit_node(rebuild, child);
    }
  }
  for (std::size_t at = flags_begin; at < node.end; ++at) {
    rebuild.out.push_back(rebuild.record.subrecords[at]);
  }
  return emitted + 1;
}

void rebuild_into(Record& target, const PackageProcedureTree& tree, Rebuild& rebuild) {
  const auto tree_begin = tree.nodes.front().begin;
  const auto tree_end = tree.nodes.front().end;
  if (tree_begin >= tree_end || tree_end > target.subrecords.size()) {
    throw schema::ScalarError("package procedure tree region is malformed");
  }
  rebuild.out.clear();
  emit_node(rebuild, 0);
  target.subrecords.erase(target.subrecords.begin() + static_cast<std::ptrdiff_t>(tree_begin),
                          target.subrecords.begin() + static_cast<std::ptrdiff_t>(tree_end));
  target.subrecords.insert(target.subrecords.begin() + static_cast<std::ptrdiff_t>(tree_begin),
                           std::make_move_iterator(rebuild.out.begin()),
                           std::make_move_iterator(rebuild.out.end()));
}

void commit_rebuilt(Plugin& plugin, Record& record, Record candidate) {
  if (!decode_package_procedure(candidate).decoded) {
    throw schema::ScalarError("the rebuilt package procedure tree does not decode");
  }
  record = std::move(candidate);
  fo4::detail::mark_record_modified(plugin, record);
}

std::optional<std::size_t> input_block_counter(const Record& record, std::size_t pkcu) {
  for (std::size_t index = pkcu + 1; index < record.subrecords.size(); ++index) {
    if (record.subrecords[index].signature == sig("XNAM") && record.subrecords[index].data.size() == 1) {
      return index;
    }
  }
  return std::nullopt;
}

void erase_dropped_inputs(Record& record, const PackageProcedureTree& tree,
                          const std::array<bool, input_id_count>& drop) {
  std::vector<std::pair<std::size_t, std::size_t>> ranges;
  for (const auto& input : tree.inputs) {
    if (input.index > max_input_id || !drop[input.index]) continue;
    ranges.emplace_back(input.subrecord_begin, input.subrecord_end);
    ranges.emplace_back(input.id_index, input.id_end);
  }
  std::sort(ranges.begin(), ranges.end(),
            [](const std::pair<std::size_t, std::size_t>& left,
               const std::pair<std::size_t, std::size_t>& right) { return left.first > right.first; });
  for (const auto& range : ranges) {
    record.subrecords.erase(record.subrecords.begin() + static_cast<std::ptrdiff_t>(range.first),
                            record.subrecords.begin() + static_cast<std::ptrdiff_t>(range.second));
  }
}

void reindex_kept_references(Record& record, const PackageProcedureTree& tree, std::size_t removed_node,
                             const std::array<bool, input_id_count>& present,
                             const std::array<bool, input_id_count>& drop,
                             const std::array<std::uint8_t, input_id_count>& mapping) {
  for (const auto& input : tree.inputs) {
    if (input.index > max_input_id || drop[input.index]) continue;
    record.subrecords[input.id_index].data[0] = std::byte{mapping[input.index]};
    const auto target = location_input_target(record, input);
    if (target && *target <= max_input_id && present[*target]) {
      write_dword(record.subrecords[input.value_index].data, 4, mapping[*target]);
    }
  }
  for (std::size_t index = 0; index < tree.nodes.size(); ++index) {
    if (index == removed_node || tree.nodes[index].kind != PackageProcedureKind::leaf) continue;
    const auto& node = tree.nodes[index];
    for (std::size_t at = node.opaque_begin; at < node.opaque_end; ++at) {
      auto& sub = record.subrecords[at];
      if (sub.signature != sig("PKC2") || sub.data.size() != 1) continue;
      const auto old = std::to_integer<std::uint8_t>(sub.data[0]);
      if (old <= max_input_id && present[old]) sub.data[0] = std::byte{mapping[old]};
    }
  }
}

bool ensure_default_inputs(Plugin& plugin, Record*& record, const FormKey& record_key,
                           PackageProcedureTree& tree, std::size_t needed,
                           PackageTreeMutationStatus& status) {
  const auto pkcu = pkcu_index(*record);
  if (!pkcu) {
    status = PackageTreeMutationStatus::undecodable;
    return false;
  }
  if (needed <= tree.inputs.size()) return true;
  if (Reader::u32(record->subrecords[*pkcu], 4) != 0) {
    status = PackageTreeMutationStatus::templated_package;
    return false;
  }
  const Record saved = *record;
  const auto saved_range = plugin.original_range;
  const auto missing = needed - tree.inputs.size();
  for (std::size_t slot = 0; slot < missing; ++slot) {
    PackageInputValue value;
    value.kind = PackageInputValueKind::boolean;
    value.boolean = false;
    const auto added = add_package_data_input(plugin, record_key, "Bool", value);
    if (added != PackageInputMutationStatus::updated) {
      *record = saved;
      plugin.original_range = saved_range;
      status = PackageTreeMutationStatus::input_creation_failed;
      return false;
    }
  }
  auto* refreshed = fo4::detail::locate_mutable_record(plugin, record_key);
  if (refreshed == nullptr) {
    *record = saved;
    plugin.original_range = saved_range;
    status = PackageTreeMutationStatus::input_creation_failed;
    return false;
  }
  record = refreshed;
  tree = decode_package_procedure(*record);
  if (!tree.decoded || tree.inputs.size() != needed) {
    *record = saved;
    plugin.original_range = saved_range;
    status = PackageTreeMutationStatus::input_creation_failed;
    return false;
  }
  return true;
}

}

PackageTreeMutationStatus add_package_procedure_node(Plugin& plugin,
                                                     const FormKey& record_key,
                                                     std::size_t parent_index,
                                                     std::string_view procedure,
                                                     std::size_t* out_node_index) {
  if (out_node_index != nullptr) *out_node_index = 0;
  auto* record = fo4::detail::locate_mutable_record(plugin, record_key);
  if (record == nullptr) return PackageTreeMutationStatus::record_missing;
  if (record->header.signature != sig("PACK")) return PackageTreeMutationStatus::not_a_package;
  auto tree = decode_package_procedure(*record);
  if (!tree.decoded) return PackageTreeMutationStatus::undecodable;
  if (parent_index >= tree.nodes.size()) return PackageTreeMutationStatus::index_out_of_range;
  if (tree.nodes[parent_index].kind != PackageProcedureKind::container) {
    return PackageTreeMutationStatus::not_a_container;
  }
  if (!is_known_procedure(procedure)) return PackageTreeMutationStatus::unknown_procedure;
  if (node_depth(tree, parent_index) + 1 >= package_tree_max_depth) {
    return PackageTreeMutationStatus::tree_full;
  }
  const auto defaults = expected_input_count(procedure);
  const std::size_t needed = defaults.value_or(0);
  const Record before_inputs = *record;
  const auto before_range = plugin.original_range;
  auto status = PackageTreeMutationStatus::updated;
  if (!ensure_default_inputs(plugin, record, record_key, tree, needed, status)) return status;
  std::vector<std::uint8_t> slot_ids;
  slot_ids.reserve(needed);
  for (std::size_t slot = 0; slot < needed; ++slot) slot_ids.push_back(tree.inputs[slot].index);

  auto model = tree;
  const auto added = model.nodes.size();
  PackageProcedureNode node;
  node.kind = PackageProcedureKind::leaf;
  node.name = std::string{procedure};
  model.nodes.push_back(std::move(node));
  model.nodes[parent_index].children.push_back(added);

  try {
    Record candidate = *record;
    Rebuild rebuild{*record, model, added, std::nullopt, procedure, {}, std::move(slot_ids)};
    rebuild_into(candidate, model, rebuild);
    commit_rebuilt(plugin, *record, std::move(candidate));
  } catch (...) {
    *record = before_inputs;
    plugin.original_range = before_range;
    throw;
  }
  if (out_node_index != nullptr) {
    const auto rebuilt = decode_package_procedure(*record);
    if (rebuilt.decoded && parent_index < rebuilt.nodes.size() && !rebuilt.nodes[parent_index].children.empty()) {
      *out_node_index = rebuilt.nodes[parent_index].children.back();
    }
  }
  return PackageTreeMutationStatus::updated;
}

PackageTreeMutationStatus remove_package_procedure_node(Plugin& plugin,
                                                        const FormKey& record_key,
                                                        std::size_t node_index) {
  auto* record = fo4::detail::locate_mutable_record(plugin, record_key);
  if (record == nullptr) return PackageTreeMutationStatus::record_missing;
  if (record->header.signature != sig("PACK")) return PackageTreeMutationStatus::not_a_package;
  const auto tree = decode_package_procedure(*record);
  if (!tree.decoded) return PackageTreeMutationStatus::undecodable;
  if (node_index >= tree.nodes.size()) return PackageTreeMutationStatus::index_out_of_range;
  const auto& node = tree.nodes[node_index];
  const auto parent = parent_of(tree, node_index);
  if (!parent) return PackageTreeMutationStatus::root_node;
  if (node.kind != PackageProcedureKind::leaf) return PackageTreeMutationStatus::not_a_leaf;
  const auto pkcu = pkcu_index(*record);
  if (!pkcu) return PackageTreeMutationStatus::undecodable;

  std::vector<std::uint8_t> removed_ids;
  if (!read_leaf_input_ids(*record, node, removed_ids)) {
    return PackageTreeMutationStatus::leaf_payload_undetermined;
  }
  std::vector<std::uint8_t> remaining_ids;
  for (std::size_t index = 0; index < tree.nodes.size(); ++index) {
    if (index == node_index || tree.nodes[index].kind != PackageProcedureKind::leaf) continue;
    std::vector<std::uint8_t> ids;
    if (!read_leaf_input_ids(*record, tree.nodes[index], ids)) {
      return PackageTreeMutationStatus::leaf_payload_undetermined;
    }
    remaining_ids.insert(remaining_ids.end(), ids.begin(), ids.end());
  }

  std::array<bool, input_id_count> present{};
  for (const auto& input : tree.inputs) {
    if (input.index > max_input_id) return PackageTreeMutationStatus::undecodable;
    if (present[input.index]) return PackageTreeMutationStatus::input_id_conflict;
    present[input.index] = true;
  }
  std::array<bool, input_id_count> drop{};
  for (const auto id : removed_ids) {
    if (id <= max_input_id && present[id]) drop[id] = true;
  }
  for (const auto id : remaining_ids) {
    if (id <= max_input_id) drop[id] = false;
  }
  for (const auto& input : tree.inputs) {
    if (input.index > max_input_id || drop[input.index]) continue;
    const auto target = location_input_target(*record, input);
    if (target && *target <= max_input_id && drop[*target]) {
      return PackageTreeMutationStatus::reference_orphaned;
    }
  }
  bool any_drop = false;
  for (const bool value : drop) any_drop = any_drop || value;

  std::array<std::uint8_t, input_id_count> mapping{};
  std::uint8_t next_id = 0;
  std::size_t kept = 0;
  for (const auto& input : tree.inputs) {
    if (input.index > max_input_id || drop[input.index]) continue;
    mapping[input.index] = next_id++;
    ++kept;
  }

  Record working = *record;
  if (any_drop) reindex_kept_references(working, tree, node_index, present, drop, mapping);

  Record candidate = working;
  Rebuild rebuild{working, tree, static_cast<std::size_t>(-1), node_index, {}, {}, {}};
  rebuild_into(candidate, tree, rebuild);
  if (any_drop) {
    erase_dropped_inputs(candidate, tree, drop);
    write_dword(candidate.subrecords[*pkcu].data, 0, static_cast<std::uint32_t>(kept));
    if (const auto counter = input_block_counter(candidate, *pkcu)) {
      candidate.subrecords[*counter].data[0] = std::byte{static_cast<std::uint8_t>(kept <= max_input_id ? kept : 0)};
    }
  }
  commit_rebuilt(plugin, *record, std::move(candidate));
  return PackageTreeMutationStatus::updated;
}

const char* package_tree_mutation_status_message(PackageTreeMutationStatus status) {
  switch (status) {
    case PackageTreeMutationStatus::updated:
      return "package procedure tree updated";
    case PackageTreeMutationStatus::unchanged:
      return "package procedure tree unchanged";
    case PackageTreeMutationStatus::record_missing:
      return "plugin has no such record";
    case PackageTreeMutationStatus::not_a_package:
      return "expected a PACK record";
    case PackageTreeMutationStatus::undecodable:
      return "the procedure tree does not decode";
    case PackageTreeMutationStatus::index_out_of_range:
      return "procedure node index is out of range";
    case PackageTreeMutationStatus::not_a_container:
      return "a procedure can only be added under a branch node";
    case PackageTreeMutationStatus::not_a_leaf:
      return "only leaf procedure nodes can be removed";
    case PackageTreeMutationStatus::root_node:
      return "the root procedure node cannot be removed";
    case PackageTreeMutationStatus::unknown_procedure:
      return "procedure type is not one of the stored names";
    case PackageTreeMutationStatus::leaf_payload_undetermined:
      return "leaf payload has a subrecord that is not a flag or a data input index";
    case PackageTreeMutationStatus::input_id_conflict:
      return "package stores duplicate data input ids";
    case PackageTreeMutationStatus::reference_orphaned:
      return "removal would orphan a data input referenced outside the removed leaf";
    case PackageTreeMutationStatus::templated_package:
      return "a templated package cannot have data inputs created for it";
    case PackageTreeMutationStatus::input_creation_failed:
      return "the default data inputs could not be created";
    case PackageTreeMutationStatus::tree_full:
      return "procedure tree depth limit reached";
  }
  return "unknown package tree status";
}

}
