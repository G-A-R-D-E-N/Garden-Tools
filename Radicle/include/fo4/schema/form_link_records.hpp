#pragma once

#include "fo4/plugin_core/container.hpp"

#include <cstddef>
#include <optional>
#include <vector>

namespace fo4 {

std::optional<std::vector<std::size_t>> record_struct_formid_offsets(const Record& record,
                                                                    const Subrecord& sub,
                                                                    bool strict_remap);

}
