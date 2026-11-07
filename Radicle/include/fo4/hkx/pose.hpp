#pragma once

#include "fo4/hkx/animation.hpp"
#include "fo4/hkx/packfile.hpp"
#include "fo4/hkx/skeleton.hpp"

#include <span>
#include <vector>

namespace fo4::hkx {

// Requires a decoded clip whose every track holds frame_count samples, and a binding that maps each track to a bone.
HkxStatus sample_pose(const HkxSkeleton& skeleton, const HkxAnimation& animation, const HkxAnimationBinding& binding,
                      float time, std::vector<HkxQsTransform>& out);

// Requires one local transform per bone, and every parent index to be -1 or an earlier bone.
HkxStatus model_space_pose(const HkxSkeleton& skeleton, std::span<const HkxQsTransform> local,
                           std::vector<HkxQsTransform>& out);

}  // namespace fo4::hkx
