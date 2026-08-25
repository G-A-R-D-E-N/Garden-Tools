#pragma once

#include "fo4/formid.hpp"
#include "fo4/plugin/container.hpp"
#include "fo4/signature.hpp"

#include <cstddef>
#include <optional>
#include <span>
#include <stdexcept>
#include <string_view>

namespace fo4::schema {

struct FormLinkListDescriptor {
  Signature record{};
  std::string_view path;
  std::string_view label;
  // Mutagen property name the AI/list tools address this list by (e.g. "Items",
  // "Keywords"); empty when the list is only reachable through its indexed path.
  std::string_view field_alias;
  Signature subrecord{};
  bool nullable{false};
  bool can_insert{false};
  // Accepts any major-record target (FLST\Items). When set, target-type
  // validation admits every loaded record signature and no target list is emitted.
  bool wildcard_target{false};
  // Empty-list insertion appends at the end of the record rather than anchoring
  // against a sibling list (FLST holds only EDID before its LNAM entries).
  bool append_when_empty{false};
  std::size_t first_target{0};
  std::size_t target_count{0};
  // Sibling lists used to anchor the first element of an otherwise-empty list,
  // indexing k_generated_formlink_list_anchors: predecessors (nearest first)
  // then successors (nearest first).
  std::size_t first_anchor{0};
  std::size_t predecessor_count{0};
  std::size_t successor_count{0};
};

struct NestedFormLinkListDescriptor {
  Signature record{};
  std::string_view container_path;
  std::string_view member;
  std::string_view label;
  Signature subrecord{};
  Signature terminator{};
  std::size_t terminator_size{0};
  bool nullable{false};
  bool can_insert{false};
  std::size_t first_target{0};
  std::size_t target_count{0};
};

struct IndexedFormLinkListPath {
  FormLinkListDescriptor descriptor;
  std::size_t index{0};
};

struct ScopedNestedFormLinkListPath {
  NestedFormLinkListDescriptor descriptor;
  std::size_t container_index{0};
};

struct IndexedNestedFormLinkListPath {
  NestedFormLinkListDescriptor descriptor;
  std::size_t container_index{0};
  std::size_t index{0};
};

enum class FormLinkListMutationStatus {
  updated,
  unchanged,
  record_missing,
  record_mismatch,
  container_index_out_of_range,
  index_out_of_range,
  target_type_mismatch,
  insertion_boundary_unavailable,
};

class FormLinkListError : public std::runtime_error {
public:
  using std::runtime_error::runtime_error;
};

#include "fo4/schema/generated_formlink_lists.inc"

std::optional<FormLinkListDescriptor> formlink_list_descriptor(
    Signature record,
    std::string_view base_path) noexcept;
// Resolves a list by the Mutagen property name the AI list tools address it by
// (record signature + field alias, e.g. FLST + "Items"). Empty alias never matches.
std::optional<FormLinkListDescriptor> formlink_list_descriptor_by_field(
    Signature record,
    std::string_view field_alias) noexcept;
std::optional<IndexedFormLinkListPath> indexed_formlink_list_path(
    std::string_view path) noexcept;
std::span<const Signature> formlink_list_targets(
    const FormLinkListDescriptor& descriptor) noexcept;
bool formlink_list_target_allowed(const FormLinkListDescriptor& descriptor,
                                  Signature signature) noexcept;

std::optional<std::size_t> record_formlink_list_size(
    const Plugin& plugin,
    const FormKey& record_key,
    const FormLinkListDescriptor& descriptor);

FormLinkListMutationStatus set_record_formlink_list_element(
    Plugin& plugin,
    const FormKey& record_key,
    const FormLinkListDescriptor& descriptor,
    std::size_t index,
    const FormKey& target);

FormLinkListMutationStatus insert_record_formlink_list_element(
    Plugin& plugin,
    const FormKey& record_key,
    const FormLinkListDescriptor& descriptor,
    std::size_t index,
    const FormKey& target);

// Inserts a zero FormID placeholder only for schema-declared nullable lists.
// Non-null lists throw rather than manufacturing an invalid temporary record.
FormLinkListMutationStatus insert_record_formlink_list_null_element(
    Plugin& plugin,
    const FormKey& record_key,
    const FormLinkListDescriptor& descriptor,
    std::size_t index);

FormLinkListMutationStatus remove_record_formlink_list_element(
    Plugin& plugin,
    const FormKey& record_key,
    const FormLinkListDescriptor& descriptor,
    std::size_t index);

// Removes every element whose FormLink resolves to `target` and returns how many
// were removed (0 leaves the plugin untouched). Mirrors the value-based removal
// of WriteService.RemoveListItem, which drops all entries matching the value.
std::size_t remove_record_formlink_list_elements_matching(
    Plugin& plugin,
    const FormKey& record_key,
    const FormLinkListDescriptor& descriptor,
    const FormKey& target);

FormLinkListMutationStatus move_record_formlink_list_element(
    Plugin& plugin,
    const FormKey& record_key,
    const FormLinkListDescriptor& descriptor,
    std::size_t from,
    std::size_t to);

FormLinkListMutationStatus clear_record_formlink_list(
    Plugin& plugin,
    const FormKey& record_key,
    const FormLinkListDescriptor& descriptor);

std::optional<ScopedNestedFormLinkListPath> scoped_nested_formlink_list_path(
    std::string_view path) noexcept;
std::optional<IndexedNestedFormLinkListPath> indexed_nested_formlink_list_path(
    std::string_view path) noexcept;
std::span<const Signature> nested_formlink_list_targets(
    const NestedFormLinkListDescriptor& descriptor) noexcept;
bool nested_formlink_list_target_allowed(
    const NestedFormLinkListDescriptor& descriptor,
    Signature signature) noexcept;

std::optional<std::size_t> record_nested_formlink_list_size(
    const Plugin& plugin,
    const FormKey& record_key,
    const NestedFormLinkListDescriptor& descriptor,
    std::size_t container_index);

FormLinkListMutationStatus set_record_nested_formlink_list_element(
    Plugin& plugin,
    const FormKey& record_key,
    const NestedFormLinkListDescriptor& descriptor,
    std::size_t container_index,
    std::size_t index,
    const FormKey& target);

FormLinkListMutationStatus insert_record_nested_formlink_list_element(
    Plugin& plugin,
    const FormKey& record_key,
    const NestedFormLinkListDescriptor& descriptor,
    std::size_t container_index,
    std::size_t index,
    const FormKey& target);

FormLinkListMutationStatus insert_record_nested_formlink_list_null_element(
    Plugin& plugin,
    const FormKey& record_key,
    const NestedFormLinkListDescriptor& descriptor,
    std::size_t container_index,
    std::size_t index);

FormLinkListMutationStatus remove_record_nested_formlink_list_element(
    Plugin& plugin,
    const FormKey& record_key,
    const NestedFormLinkListDescriptor& descriptor,
    std::size_t container_index,
    std::size_t index);

FormLinkListMutationStatus move_record_nested_formlink_list_element(
    Plugin& plugin,
    const FormKey& record_key,
    const NestedFormLinkListDescriptor& descriptor,
    std::size_t container_index,
    std::size_t from,
    std::size_t to);

FormLinkListMutationStatus clear_record_nested_formlink_list(
    Plugin& plugin,
    const FormKey& record_key,
    const NestedFormLinkListDescriptor& descriptor,
    std::size_t container_index);

}  // namespace fo4::schema
