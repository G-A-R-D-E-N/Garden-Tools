#pragma once

#include "fo4/swf/swf.hpp"

#include <string>

namespace fo4::swf {

// Requires a decoded shape; the output is one complete <svg> document with a trailing newline.
std::string shape_to_svg(const SwfShape& shape);

// Requires a finite value.
std::string svg_number(double value);

// Requires bounds in twips; the output ends with a newline.
std::string svg_open(const SwfRect& bounds);

// Requires a decoded shape and an id_prefix unique within the document.
std::string shape_defs_to_svg(const SwfShape& shape, const std::string& id_prefix);

// Requires a decoded shape; the output has no <svg> wrapper.
std::string shape_paths_to_svg(const SwfShape& shape, const std::string& id_prefix);

// Requires a decoded shape and an id_prefix unique within the document; no stroke paths.
std::string shape_fill_paths_to_svg(const SwfShape& shape, const std::string& id_prefix);

}  // namespace fo4::swf
