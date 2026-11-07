#pragma once

#include "fo4/environment/environment.hpp"
#include "fo4/forms/placed_reference.hpp"

#include <array>
#include <cstddef>
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

struct CellLightingEdit {
  std::optional<std::array<std::uint8_t, 3>> ambient;
  std::optional<std::array<std::uint8_t, 3>> directional;
  std::optional<std::array<std::uint8_t, 3>> fog_near_color;
  std::optional<float> fog_near;
  std::optional<float> fog_far;
  std::optional<std::int32_t> directional_rotation_xy;
  std::optional<std::int32_t> directional_rotation_z;
  std::optional<float> directional_fade;
  std::optional<float> fog_clip_distance;
  std::optional<float> fog_power;
};

struct CellEdit {
  std::optional<std::uint16_t> flags;
  std::optional<CellLightingEdit> lighting;
  std::optional<std::optional<FormKey>> lighting_template;
  std::optional<float> water_height;
  std::optional<std::optional<FormKey>> water;
  std::optional<std::optional<FormKey>> image_space;
  std::optional<std::optional<FormKey>> encounter_zone;
  std::optional<std::optional<FormKey>> location;
  std::optional<std::optional<FormKey>> acoustic_space;
  std::optional<std::optional<FormKey>> music_type;
  std::optional<std::optional<FormKey>> sky_weather_region;
  std::optional<std::optional<FormKey>> god_rays;
  std::optional<std::optional<FormKey>> lock_list;
  std::optional<std::optional<FormKey>> owner;
  std::optional<std::int32_t> owner_rank;
  std::optional<std::vector<FormKey>> regions;
};

enum class CellEditStatus {
  edited,
  cell_missing,
  cell_deleted,
  not_a_cell,
  target_missing,
  target_type_not_allowed,
  target_not_master,
  interior_flag_locked,
  invalid_value,
  malformed,
};

struct CellEditResult {
  CellEditStatus status{CellEditStatus::cell_missing};
  std::string copied_from;
};

CellEditResult edit_cell(Plugin& patch,
                         const fo4::env::Environment& env,
                         const FormKey& cell,
                         const CellEdit& edit);

enum class CellPropertyKind {
  scalar,
  link,
  colour,
  flags,
};

struct CellPropertyDescriptor {
  std::string name;
  std::string label;
  CellPropertyKind kind{CellPropertyKind::scalar};
  double minimum{0.0};
  double maximum{0.0};
  bool multiple{false};
  std::vector<std::string> targets;
};

std::vector<CellPropertyDescriptor> cell_property_descriptors();

enum class CleanupMode {
  dedupe,
  excess,
};

enum class CleanupPlanStatus {
  planned,
  cell_missing,
  cell_deleted,
  not_a_cell,
};

enum class CleanupReason {
  duplicate,
  duplicate_of_special,
  excess,
};

struct CleanupEntry {
  FormKey reference{};
  FormKey kept{};
  CleanupReason reason{CleanupReason::duplicate};
};

struct CleanupPlan {
  CleanupPlanStatus status{CleanupPlanStatus::cell_missing};
  FormKey cell{};
  std::vector<CleanupEntry> entries;
};

CleanupPlan plan_cell_cleanup(const fo4::env::Environment& env,
                              const FormKey& cell,
                              CleanupMode mode,
                              std::size_t max_count,
                              bool by_model);

enum class CleanupApplyStatus {
  applied,
  refused,
};

struct CleanupApplyResult {
  CleanupApplyStatus status{CleanupApplyStatus::refused};
  std::vector<DeleteStatus> entry_status;
};

CleanupApplyResult apply_cell_cleanup(Plugin& patch,
                                      const fo4::env::Environment& env,
                                      const CleanupPlan& plan);

}  // namespace fo4::forms
