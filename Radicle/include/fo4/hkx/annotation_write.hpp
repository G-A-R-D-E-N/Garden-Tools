#pragma once

#include "fo4/hkx/animation.hpp"
#include "fo4/hkx/packfile.hpp"
#include "fo4/hkx/write.hpp"

#include <cstddef>
#include <span>

namespace fo4::hkx {

// Requires an animation object `object_index` with no annotation tracks; an existing list is UnsupportedLayout.
// Constraint: a non-finite time, a NUL byte or a string over 255 bytes is BadSection.
// Invariant: only the annotationTracks array header is patched; existing __data__ bytes and fixups stay, new bytes follow.
HkxStatus set_animation_annotations(std::span<const std::byte> source, std::size_t object_index,
                                    std::span<const HkxAnnotationTrack> tracks, HkxAppend& out);

}  // namespace fo4::hkx
