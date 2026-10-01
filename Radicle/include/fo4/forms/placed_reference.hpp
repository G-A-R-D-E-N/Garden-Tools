#pragma once

#include "fo4/formid.hpp"
#include "fo4/plugin/container.hpp"

#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

namespace fo4::forms {

class PlacedReferenceError : public std::runtime_error {
public:
  using std::runtime_error::runtime_error;
};

enum class PlacedReferenceKind {
  object,
  npc,
};

enum class PlacedReferenceCopyStatus {
  inserted,
  replaced,
  already_exists,
  source_missing,
};

struct PositionRotation {
  float x{0.0f};
  float y{0.0f};
  float z{0.0f};
  float rx{0.0f};
  float ry{0.0f};
  float rz{0.0f};
};

struct PlacedReferenceView {
  const Record* record{nullptr};
  PlacedReferenceKind kind{PlacedReferenceKind::object};
  std::optional<FormKey> base;
  std::string editor_id;
  PositionRotation transform{};
  float scale{1.0f};
  bool persistent{false};
  bool deleted{false};
};

// Decodes the stable shared REFR/ACHR surfaces used by the cell viewer:
// NAME (base form), DATA (P3 position + P3 rotation), optional XSCL, EDID and
// the major-record deleted flag. Other placed-record families are intentionally
// excluded until their on-disk contract is proven independently.
std::optional<PlacedReferenceView> decode_placed_reference(
    const Plugin& plugin,
    const Record& record,
    bool persistent = false);

// Returns REFR/ACHR records from the CELL's persistent and temporary child
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

bool set_placed_reference_scale(Plugin& plugin, const FormKey& reference, std::optional<float> scale);
}  // namespace fo4::forms
