#pragma once

#include "fo4/nif/nif.hpp"

#include <cstddef>
#include <string_view>
#include <vector>

namespace fo4::nif {

std::vector<std::byte> fix_nif(const NifFile& nif);
std::vector<std::byte> fix_nif(const NifFile& nif, std::string_view check);

}  // namespace fo4::nif
