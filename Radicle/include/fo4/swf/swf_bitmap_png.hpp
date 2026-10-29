#pragma once

#include "fo4/swf/swf.hpp"

#include <cstdint>
#include <optional>
#include <vector>

namespace fo4::swf {

std::optional<std::vector<std::uint8_t>> bitmap_to_png(const SwfBitmap& bitmap);

}  // namespace fo4::swf
