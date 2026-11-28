#pragma once

#include "fo4/plugin/container.hpp"

#include <optional>
#include <vector>

namespace fo4 {

struct NestedCopyPlan {
  std::vector<const Group*> path;
  const Record* cell{nullptr};
  const Record* world{nullptr};
  const Record* quest{nullptr};
};

// A CELL that owns its enclosing block groups must be copied with them or the saved plugin loses the worldspace chain.
struct CellContainmentPlan {
  std::vector<const Group*> chain{};
  const Record* world{nullptr};
};

std::optional<NestedCopyPlan> plan_nested_copy(const Plugin& source, const Record* record);

// Edits the caller's copy only; on false the caller must discard that copy.
bool insert_nested_copy(Plugin& destination,
                        const Plugin& source,
                        const NestedCopyPlan& plan,
                        Record copied);

std::optional<CellContainmentPlan> plan_cell_containment(const Plugin& source,
                                                         const Record* record);

// Edits the caller's copy only; on false the caller must discard that copy.
bool insert_cell_containment(Plugin& destination,
                             const Plugin& source,
                             const CellContainmentPlan& plan,
                             Record copied);

}  // namespace fo4
