#pragma once

#include "fo4/swf/swf.hpp"
#include "fo4/swf/swf_display.hpp"

#include <array>
#include <cstdint>
#include <string>

namespace fo4::swf {

struct NineSliceCell {
  SwfBounds source{};
  SwfBounds destination{};
};

// Requires a placement with positive scale and no rotation, and a placed box with positive size.
std::array<NineSliceCell, 9> nine_slice_regions(const SwfRect& splitter, const SwfBounds& placed,
                                                const SwfTransform& placement);

// Requires display built from the same file by build_display_list; a frame past the root timeline draws no shapes.
std::string frame_to_svg(const SwfFile& file, const SwfDisplayList& display, std::uint32_t frame);

}  // namespace fo4::swf
