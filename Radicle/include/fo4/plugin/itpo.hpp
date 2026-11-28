#pragma once

#include "fo4/plugin/container.hpp"
#include "fo4/plugin_key.hpp"
#include "fo4/signature.hpp"

#include <cstddef>
#include <span>
#include <vector>

namespace fo4 {

struct IdenticalToPreviousOverride {
  FormKey key;
  Signature signature{};
  PluginKey previous;
};

std::vector<IdenticalToPreviousOverride> find_identical_to_previous_override(
    std::span<const Plugin* const> load_order,
    std::size_t plugin_index,
    std::size_t limit = 0,
    bool exact_only = false);

std::size_t remove_identical_to_previous_override(Plugin& plugin,
                                                  std::span<const Plugin* const> load_order,
                                                  std::size_t plugin_index);

}
