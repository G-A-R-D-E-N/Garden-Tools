#pragma once

#include "fo4/nif/nif.hpp"

#include <cstddef>
#include <vector>

namespace fo4::nif {

std::vector<std::byte> fix_nif(const NifFile& nif);

}  // namespace fo4::nif
