#pragma once

#include "fo4/plugin/container.hpp"

#include <cstdint>
#include <map>

namespace fo4 {

struct RecordPlacement {
  FormKey parent;
  std::int32_t group_type{0};

  friend bool operator==(const RecordPlacement&, const RecordPlacement&) = default;
};

std::map<FormKey, RecordPlacement> record_placements(const Plugin& plugin);

}  // namespace fo4
