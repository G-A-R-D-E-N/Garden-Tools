#pragma once

#include "fo4/plugin/container.hpp"
#include "fo4/signature.hpp"

#include <cstddef>
#include <span>
#include <vector>

namespace fo4 {

struct UnreachableRecord {
  FormKey key;
  Signature signature{};
};

std::vector<UnreachableRecord> find_unreachable(std::span<const Plugin* const> load_order,
                                                std::size_t plugin_index);

}  // namespace fo4
