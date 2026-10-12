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
};

struct PackageDataInput {
  std::string type_name;
  std::size_t subrecord_begin{0}, subrecord_end{0};
  std::uint8_t index{0};
};

struct PackageProcedureTree {
  bool decoded{false};
  std::size_t region_begin{0}, region_end{0};
  std::vector<PackageDataInput> inputs;
  std::optional<std::uint8_t> marker;
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

const char* package_location_type_name(PackageLocationType type);
const char* package_target_type_name(PackageTargetType type);

PackageProcedureTree decode_package_procedure(const Record& record);
std::optional<std::uint32_t> expected_input_count(std::string_view leaf_name);

}
