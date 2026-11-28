#pragma once

#include "state.hpp"

#include <cstddef>
#include <string>

namespace fo4::avm1::timeline_ops {

Avm1RunStatus execute(Avm1Run& run, const Avm1Block& block, const Avm1Action& action,
                      std::size_t& next, std::string& message);

}  // namespace fo4::avm1::timeline_ops
