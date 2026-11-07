#pragma once

#include "fo4/graph/check_errors.hpp"

#include <span>
#include <vector>

namespace fo4 {

std::vector<CheckFinding> check_record_levels(const Plugin& plugin);
std::vector<CheckFinding> check_record_levels(const Plugin& plugin,
                                              std::span<const Record* const> records);

}  // namespace fo4
