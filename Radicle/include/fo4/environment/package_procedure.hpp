#pragma once

#include "fo4/plugin/container.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace fo4::env {

enum class PackageProcedureKind : std::uint8_t { container, leaf };

struct PackageProcedureNode {
  PackageProcedureKind kind{PackageProcedureKind::leaf};
  std::string name;
  std::uint32_t condition_count{0};
  std::uint32_t child_count{0};
  std::optional<std::uint32_t> flags;
  std::size_t opaque_begin{0}, opaque_end{0};
  std::vector<std::size_t> children;
  std::size_t begin{0}, end{0};
  std::size_t condition_begin{0}, condition_end{0};
  std::size_t children_anchor{0};
  std::size_t prcb_index{0};
};

enum class PackageInputValueKind : std::uint8_t {
  none, boolean, integer, real, reference, target, location, object_list, topic
};

struct PackageDataInput {
  std::string type_name;
  std::size_t subrecord_begin{0}, subrecord_end{0};
  std::size_t value_index{0};
  std::size_t id_index{0}, id_end{0};
  Signature value_tag{};
  PackageInputValueKind kind{PackageInputValueKind::none};
  std::uint8_t index{0};
  bool boolean{false};
  std::int32_t integer{0};
  std::uint32_t integer_raw{0};
  float real{0.0f};
};

struct PackageProcedureTree {
  bool decoded{false};
  std::size_t region_begin{0}, region_end{0};
  std::vector<PackageDataInput> inputs;
  std::optional<std::uint8_t> next_input_id;
  std::vector<PackageProcedureNode> nodes;
};

enum class PackageLocationType : std::int32_t {
  near_reference, in_cell, near_package_start, near_editor_location, search_result_4, search_result_5,
  near_linked_reference, package_location_7, quest_alias_8, location_handle_9, named_special_reference,
  data_input_id_match, near_self, editor_location_cell, alias_collection_item
};

enum class PackageTargetType : std::int32_t {
  specific_reference, object_id, object_type, linked_reference, quest_reference_alias,
  special_reference, self, keyword, alias_collection_item
};

enum class PackageInputMutationStatus {
  updated,
  unchanged,
  record_missing,
  not_a_package,
  undecodable,
  unknown_input_type,
  input_id_missing,
  duplicate_input_id,
  kind_mismatch,
  payload_size_invalid,
  payload_layout_undetermined,
  payload_tag_mismatch,
  procedure_tree_present,
  no_free_input_id,
};

struct PackageInputValue {
  PackageInputValueKind kind{PackageInputValueKind::boolean};
  bool boolean{false};
  std::int32_t integer{0};
  float real{0.0f};
  std::vector<std::byte> payload;
};

const char* package_location_type_name(PackageLocationType type);
const char* package_target_type_name(PackageTargetType type);

PackageProcedureTree decode_package_procedure(const Record& record);
std::optional<std::uint32_t> expected_input_count(std::string_view leaf_name);
bool is_known_procedure(std::string_view name);
std::optional<PackageInputValueKind> package_input_value_kind(std::string_view type_name);

PackageInputMutationStatus add_package_data_input(Plugin& plugin,
                                                  const FormKey& record_key,
                                                  std::string_view type_name,
                                                  const PackageInputValue& value);

PackageInputMutationStatus set_package_data_input(Plugin& plugin,
                                                  const FormKey& record_key,
                                                  std::uint8_t id,
                                                  const PackageInputValue& value);

PackageInputMutationStatus remove_package_data_input(Plugin& plugin,
                                                     const FormKey& record_key,
                                                     std::uint8_t id);

enum class PackageTreeMutationStatus {
  updated,
  unchanged,
  record_missing,
  not_a_package,
  undecodable,
  index_out_of_range,
  not_a_container,
  not_a_leaf,
  root_node,
  unknown_procedure,
  leaf_payload_undetermined,
  input_id_conflict,
  reference_orphaned,
  templated_package,
  input_creation_failed,
  tree_full,
};

inline constexpr std::size_t package_tree_max_depth = 64;

const char* package_tree_mutation_status_message(PackageTreeMutationStatus status);

// Constraint: the tree is rebuilt from the decoded model and every branch count is recomputed.
PackageTreeMutationStatus add_package_procedure_node(Plugin& plugin,
                                                     const FormKey& record_key,
                                                     std::size_t parent_index,
                                                     std::string_view procedure,
                                                     std::size_t* out_node_index = nullptr);

PackageTreeMutationStatus remove_package_procedure_node(Plugin& plugin,
                                                        const FormKey& record_key,
                                                        std::size_t node_index);

}
