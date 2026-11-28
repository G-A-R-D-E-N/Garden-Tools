#pragma once

#include <fo4/avm2/display.hpp>

#include <cstdint>
#include <string>
#include <string_view>

namespace fo4::avm2 {

/* Requires a display root; returns one line per node, depth-first, indented by nesting level. */
std::string display_snapshot_text(const DisplayObject& root);

std::uint64_t fnv1a64(std::string_view bytes);

/* Requires the same root as display_snapshot_text; the hash is fnv1a64 of that text. */
std::uint64_t display_snapshot_hash(const DisplayObject& root);

}  // namespace fo4::avm2
