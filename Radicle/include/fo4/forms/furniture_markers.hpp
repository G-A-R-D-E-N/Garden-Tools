#pragma once

#include "fo4/plugin/container.hpp"

#include <cstdint>
#include <span>

namespace fo4::forms {

struct FurnitureMarkerEdit {
  bool enabled{true};
  float offset_x{0.0f};
  float offset_y{0.0f};
  float offset_z{0.0f};
  float rotation_z{0.0f};
  std::uint8_t entry_types{255};
};

enum class FurnitureMarkersWrite {
  written,
  record_missing,
  unsupported_record,
  too_many_markers,
  unexpected_layout,
  no_insertion_point,
};

// Constraint: replaces the whole SNAM list and rebuilds the low 22 MNAM bits from the enabled flags, keeping the upper MNAM bits; at most 22 markers, an empty list removes the SNAM, the keyword link is written null, and a refusal changes nothing.
FurnitureMarkersWrite set_furniture_markers(Plugin& plugin,
                                            const FormKey& record_key,
                                            std::span<const FurnitureMarkerEdit> markers);

}  // namespace fo4::forms
