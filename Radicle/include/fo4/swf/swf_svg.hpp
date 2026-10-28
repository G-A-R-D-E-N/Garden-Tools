#pragma once

#include "fo4/swf/swf.hpp"

#include <string>

namespace fo4::swf {

// Requires a decoded shape; the output is one complete <svg> document with a trailing newline.
std::string shape_to_svg(const SwfShape& shape);

}  // namespace fo4::swf
