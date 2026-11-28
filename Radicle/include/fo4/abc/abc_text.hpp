#pragma once

#include <fo4/abc/abc.hpp>

#include <string>

namespace fo4::abc {

/* Invariant: equal models must render equal text and every pool entry is printed by value. */
std::string render_abc_text(const AbcFile& file);

}  // namespace fo4::abc
