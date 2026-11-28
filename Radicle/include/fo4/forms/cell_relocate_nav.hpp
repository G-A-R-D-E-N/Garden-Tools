#pragma once

#include "fo4/environment/environment.hpp"
#include "fo4/formid.hpp"
#include "fo4/plugin/container.hpp"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace fo4::forms {

enum class CellNavigationStatus {
  updated,
  unchanged,
  cell_missing,
  not_a_cell,
  not_exterior,
  not_relocated,
  malformed,
  neighbour_missing,
  copy_failed,
  navi_unsupported,
};

struct CellNavigationResult {
  CellNavigationStatus status{CellNavigationStatus::cell_missing};
  std::size_t links_removed{0};
  std::size_t back_links_removed{0};
  std::vector<FormKey> neighbour_overrides;
  std::size_t navi_entries_updated{0};
  std::size_t navi_entries_absent{0};
  std::size_t stale_preferred_paths{0};
  std::string message;
};

CellNavigationResult relocate_cell_navigation(Plugin& patch,
                                              const env::Environment& environment,
                                              const FormKey& cell,
                                              std::int32_t old_x,
                                              std::int32_t old_y,
                                              std::int32_t new_x,
                                              std::int32_t new_y);

}  // namespace fo4::forms
