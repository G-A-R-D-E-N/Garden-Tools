#pragma once

#include "fo4/environment/environment.hpp"
#include "fo4/forms/placed_reference.hpp"

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace fo4::forms {

enum class CellOverrideStatus {
  inserted,
  already_exists,
  source_missing,
  not_a_cell,
};

CellOverrideStatus copy_cell_as_override(Plugin& patch, const Plugin& source, const FormKey& cell);

enum class EnsureOverrideStatus {
  present,
  copied,
  reference_missing,
  reference_deleted,
};

struct EnsureOverrideResult {
  EnsureOverrideStatus status{EnsureOverrideStatus::reference_missing};
  std::string copied_from;
  std::string message;
};

EnsureOverrideResult ensure_reference_override(Plugin& patch,
                                               const fo4::env::Environment& env,
                                               const FormKey& reference);

struct OwnershipEdit {
  std::optional<FormKey> owner;
  bool no_crime{false};
  std::optional<std::int32_t> faction_rank;
};

struct EnableParentEdit {
  std::optional<FormKey> parent;
  std::uint8_t flags{0};
};

struct TeleportEdit {
  FormKey door;
  PositionRotation destination{};
};

struct PrimitiveEdit {
  std::array<float, 3> bounds{};
  std::uint32_t type{0};
};

struct PlacedReferenceEdit {
  std::optional<PositionRotation> transform;
  std::optional<std::optional<float>> scale;
  std::optional<std::string> editor_id;
  std::optional<std::uint32_t> record_flags;
  std::optional<std::optional<std::string>> comments;
  std::optional<std::optional<FormKey>> location_reference;
  std::optional<OwnershipEdit> ownership;
  std::optional<EnableParentEdit> enable_parent;
  std::optional<std::vector<LinkedReferenceEdit>> linked_references;
  std::optional<TeleportEdit> teleport;
  std::optional<PrimitiveEdit> primitive;
};

enum class PlacedReferenceEditStatus {
  edited,
  reference_missing,
  reference_deleted,
  target_cell_missing,
  field_absent,
};

struct PlacedReferenceEditResult {
  PlacedReferenceEditStatus status{PlacedReferenceEditStatus::reference_missing};
  std::string copied_from;
  bool relocated{false};
  std::string message;
};

PlacedReferenceEditResult edit_placed_reference(Plugin& patch,
                                                const fo4::env::Environment& env,
                                                const FormKey& reference,
                                                const PlacedReferenceEdit& edit);

enum class DuplicateStatus {
  duplicated,
  reference_missing,
  reference_deleted,
  parent_cell_missing,
  unsupported_signature,
};

struct DuplicateOptions {
  PositionRotation offset{};
  bool keep_editor_id{false};
};

struct DuplicateResult {
  DuplicateStatus status{DuplicateStatus::reference_missing};
  FormKey new_reference{};
};

DuplicateResult duplicate_placed_reference(Plugin& patch,
                                           const fo4::env::Environment& env,
                                           const FormKey& reference,
                                           const DuplicateOptions& options);

enum class DeleteStatus {
  deleted,
  marked_deleted,
  referenced,
  reference_missing,
  already_deleted,
};

struct DeleteResult {
  DeleteStatus status{DeleteStatus::reference_missing};
};

DeleteResult delete_placed_reference(Plugin& patch,
                                     const fo4::env::Environment& env,
                                     const FormKey& reference);

}  // namespace fo4::forms
