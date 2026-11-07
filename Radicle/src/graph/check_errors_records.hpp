#pragma once

#include "fo4/graph/check_errors.hpp"

#include <vector>

namespace fo4 {

std::vector<CheckFinding> check_record_levels(const Plugin& plugin);

}  // namespace fo4
