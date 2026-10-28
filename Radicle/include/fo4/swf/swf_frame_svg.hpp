#pragma once

#include "fo4/swf/swf.hpp"
#include "fo4/swf/swf_display.hpp"

#include <cstdint>
#include <string>

namespace fo4::swf {

// Requires display built from the same file by build_display_list; a frame past the root timeline draws no shapes.
std::string frame_to_svg(const SwfFile& file, const SwfDisplayList& display, std::uint32_t frame);

}  // namespace fo4::swf
