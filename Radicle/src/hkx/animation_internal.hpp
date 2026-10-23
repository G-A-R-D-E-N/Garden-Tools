#pragma once

#include "fo4/hkx/animation.hpp"
#include "fo4/hkx/layout.hpp"
#include "fo4/hkx/packfile.hpp"

namespace fo4::hkx::detail {

// Requires the hkaAnimation annotationTracks array that every animation class inherits.
HkxStatus decode_annotation_tracks(const HkxPackfile& packfile, const HkxObjectView& object,
                                   std::vector<HkxAnnotationTrack>& out);

// Requires a class the caller already recognised; offsets come from the measured layout.
HkxStatus decode_spline_animation(const HkxPackfile& packfile, const HkxObjectView& object,
                                  const HkxClassLayout& layout, HkxAnimation& out);
HkxStatus decode_interleaved_animation(const HkxPackfile& packfile, const HkxObjectView& object,
                                      const HkxClassLayout& layout, HkxAnimation& out);

}  // namespace fo4::hkx::detail
