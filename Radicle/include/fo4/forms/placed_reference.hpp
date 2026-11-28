#pragma once

#include "fo4/formid.hpp"
#include "fo4/plugin/container.hpp"
#include "fo4/plugin/placed_decode.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace fo4::forms {

using fo4::PlacedReferenceError;
using fo4::PlacedReferenceKind;
using fo4::PositionRotation;
using fo4::PlacedReferenceView;
using fo4::placed_decode::decode_placed_reference;

enum class PlacedReferenceCopyStatus {
  inserted,
  replaced,
  already_exists,
  source_missing,
};

// Returns REFR, ACHR and variant records from the CELL's persistent and temporary child
// groups. The CELL child-group label is resolved through the plugin's own
// FormID context, so master-owned cell overrides are handled correctly.
std::vector<PlacedReferenceView> placed_references_for_cell(
    const Plugin& plugin,
    const FormKey& cell);

// Copies a REFR/ACHR as an override while preserving its CELL containment.
// Missing CELL/group hierarchy is recreated from the source path. Group labels
// that store parent FormIDs are re-encoded through the destination master table;
// grid/block labels are preserved byte-for-byte. The operation is transactional
// and fails closed on malformed or ambiguous containment.
PlacedReferenceCopyStatus copy_placed_reference_as_override(
    Plugin& destination,
    const Plugin& source,
    const FormKey& reference,
    bool overwrite = false);

enum class PlacedReferenceCopyNewStatus {
  created,
  source_missing,
  unsupported_signature,
  parent_cell_missing,
};

struct PlacedReferenceCopyNewResult {
  PlacedReferenceCopyNewStatus status{PlacedReferenceCopyNewStatus::source_missing};
  FormKey new_reference{};
};

PlacedReferenceCopyNewResult copy_placed_reference_as_new(
    Plugin& destination,
    const Plugin& source,
    const FormKey& reference);

class InfoCopyError : public std::runtime_error {
public:
  using std::runtime_error::runtime_error;
};

enum class InfoCopyNewStatus {
  created,
  source_missing,
  unsupported_signature,
  parent_dial_missing,
};

struct InfoCopyNewResult {
  InfoCopyNewStatus status{InfoCopyNewStatus::source_missing};
  FormKey new_info{};
};

InfoCopyNewResult copy_info_as_new(
    Plugin& destination,
    const Plugin& source,
    const FormKey& info);

// Replaces only the 24-byte DATA position/rotation payload on an existing
// REFR/ACHR. Missing, duplicate or malformed DATA is rejected transactionally.
bool set_placed_reference_transform(
    Plugin& plugin,
    const FormKey& reference,
    const PositionRotation& transform);

enum class PlacedReferenceMoveStatus {
  moved,
  relocated,
  reference_missing,
  target_cell_missing,
};

PlacedReferenceMoveStatus move_placed_reference(Plugin& plugin,
                                                const FormKey& reference,
                                                const PositionRotation& transform);

struct PlacedReferenceCreation {
  FormKey cell;
  FormKey base;
  PositionRotation transform{};
  bool persistent{true};
  bool initially_disabled{false};
  PlacedReferenceKind kind{PlacedReferenceKind::object};
  // Must be set when the CELL is already an override copied into this plugin under its master key.
  bool cell_is_override{false};
};

FormKey create_placed_reference(Plugin& plugin, const PlacedReferenceCreation& creation);

struct RawCellChildCreation {
  FormKey cell;
  Signature signature{};
  bool persistent{false};
  // Requires: 0 takes the next free local id; any other value must be inside the plugin local range and unused, and raises HEDR nextFormID past it.
  std::uint32_t local_id{0};
  std::uint16_t form_version{0};
};

enum class RawCellChildStatus {
  created,
  cell_missing,
  not_a_cell,
  not_a_cell_child_type,
  persistent_land,
  id_out_of_range,
  id_taken,
};

struct RawCellChildResult {
  RawCellChildStatus status{RawCellChildStatus::cell_missing};
  FormKey key{};
};

// Requires: an empty REFR-family or LAND record in the persistent or temporary group of an existing CELL, with no subrecords, flags or position written; nothing is validated against the cell kind.
RawCellChildResult create_raw_cell_child(Plugin& plugin, const RawCellChildCreation& creation);


bool set_placed_reference_scale(Plugin& plugin, const FormKey& reference, std::optional<float> scale);
bool set_placed_reference_editor_id(Plugin& plugin, const FormKey& reference, std::string_view editor_id);
bool set_placed_reference_record_flags(Plugin& plugin, const FormKey& reference, std::uint32_t flags);
bool set_placed_reference_comments(Plugin& plugin, const FormKey& reference, std::optional<std::string_view> comments);
bool set_placed_reference_location_reference(Plugin& plugin,
                                             const FormKey& reference,
                                             const std::optional<FormKey>& location);
bool set_placed_reference_ownership(Plugin& plugin,
                                    const FormKey& reference,
                                    const std::optional<FormKey>& owner,
                                    bool no_crime,
                                    std::optional<std::int32_t> faction_rank);
bool set_placed_reference_enable_parent(Plugin& plugin,
                                        const FormKey& reference,
                                        const std::optional<FormKey>& parent,
                                        std::uint8_t flags);

struct LockDataEdit {
  std::uint8_t level{0};
  std::optional<FormKey> key;
  std::uint8_t flags{0};
};

bool set_placed_reference_lock(Plugin& plugin, const FormKey& reference, const std::optional<LockDataEdit>& lock);

struct LinkedReferenceEdit {
  std::optional<FormKey> keyword;
  FormKey reference;
};

bool set_placed_reference_linked_references(Plugin& plugin,
                                            const FormKey& reference,
                                            std::span<const LinkedReferenceEdit> links);

enum class PresentFieldEdit {
  updated,
  record_missing,
  field_absent,
};

PresentFieldEdit set_placed_reference_teleport(Plugin& plugin,
                                               const FormKey& reference,
                                               const FormKey& door,
                                               const PositionRotation& destination);
PresentFieldEdit set_placed_reference_primitive(Plugin& plugin,
                                                const FormKey& reference,
                                                const std::array<float, 3>& bounds,
                                                std::uint32_t type);
PresentFieldEdit set_placed_reference_primitive_values(Plugin& plugin,
                                                       const FormKey& reference,
                                                       const std::optional<std::array<float, 3>>& bounds,
                                                       const std::optional<std::array<float, 4>>& color);
bool set_placed_reference_open_by_default(Plugin& plugin, const FormKey& reference, bool open);
bool set_placed_reference_ignored_by_sandbox(Plugin& plugin, const FormKey& reference, bool ignored);
PresentFieldEdit set_placed_reference_activate_parent_delay(Plugin& plugin,
                                                            const FormKey& reference,
                                                            std::size_t index,
                                                            float delay);
}  // namespace fo4::forms
