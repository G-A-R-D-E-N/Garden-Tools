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
  Signature subrecord{};
  bool nullable{false};
  bool can_insert{false};
  std::size_t first_target{0};
  std::size_t target_count{0};
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
std::optional<IndexedFormLinkListPath> indexed_formlink_list_path(
    std::string_view path) noexcept;
std::span<const Signature> formlink_list_targets(
    const FormLinkListDescriptor& descriptor) noexcept;
bool formlink_list_target_allowed(const FormLinkListDescriptor& descriptor,
                                  Signature signature) noexcept;

std::optional<ScopedNestedFormLinkListPath> scoped_nested_formlink_list_path(
    std::string_view path) noexcept;
std::optional<IndexedNestedFormLinkListPath> indexed_nested_formlink_list_path(
    std::string_view path) noexcept;
std::span<const Signature> nested_formlink_list_targets(
    const NestedFormLinkListDescriptor& descriptor) noexcept;
bool nested_formlink_list_target_allowed(
    const NestedFormLinkListDescriptor& descriptor,
    Signature signature) noexcept;

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

FormLinkListMutationStatus remove_record_formlink_list_element(
    Plugin& plugin,
    const FormKey& record_key,
    const FormLinkListDescriptor& descriptor,
    std::size_t index);

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

FormLinkListMutationStatus remove_record_nested_formlink_list_element(
    Plugin& plugin,
    const FormKey& record_key,
    const NestedFormLinkListDescriptor& descriptor,
    std::size_t container_index,
    std::size_t index);

}  // namespace fo4::schema
