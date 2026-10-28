#pragma once

#include "fo4/swf/swf.hpp"

#include <cstdint>
#include <map>
#include <string>

namespace fo4::swf {

// Requires a file whose bitmap tags outlive the map.
using SvgBitmaps = std::map<std::uint16_t, const SwfBitmap*>;

// Requires a decoded shape; the output is one complete <svg> document with a trailing newline.
std::string shape_to_svg(const SwfShape& shape);

// Requires a decoded shape; bitmap fills take their image from the bitmap tags of file.
std::string shape_to_svg(const SwfShape& shape, const SwfFile& file);

// Requires a finite value.
std::string svg_number(double value);

// Requires bounds in twips; the output ends with a newline.
std::string svg_open(const SwfRect& bounds);

// Requires file to outlive the returned map.
SvgBitmaps index_bitmaps(const SwfFile& file);

// Requires a decoded shape and an id_prefix unique within the document.
std::string shape_defs_to_svg(const SwfShape& shape, const std::string& id_prefix,
                              const SvgBitmaps& bitmaps);

// Requires a decoded shape; the output has no <svg> wrapper.
std::string shape_paths_to_svg(const SwfShape& shape, const std::string& id_prefix,
                               const SvgBitmaps& bitmaps);

// Requires a decoded shape and an id_prefix unique within the document; no stroke paths.
std::string shape_fill_paths_to_svg(const SwfShape& shape, const std::string& id_prefix,
                                    const SvgBitmaps& bitmaps);

}  // namespace fo4::swf
