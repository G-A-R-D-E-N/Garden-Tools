#pragma once

#include "fo4/signature.hpp"

#include <cstddef>
#include <span>
#include <vector>

namespace fo4 {

std::vector<std::size_t> vmad_formid_offsets(std::span<const std::byte> data,
                                             Signature record_signature);

}  // namespace fo4
