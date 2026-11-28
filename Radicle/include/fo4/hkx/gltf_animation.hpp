#pragma once

#include "fo4/hkx/animation.hpp"
#include "fo4/hkx/skeleton.hpp"
#include "fo4/hkx/spline_write.hpp"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace fo4::hkx {

inline constexpr float k_gltf_min_frame_rate = 30.0f;
inline constexpr std::size_t k_gltf_max_frames = 100000;
inline constexpr std::size_t k_gltf_max_frame_tracks = std::size_t{1} << 22;

enum class HkxGltfStatus : std::uint8_t {
  Ok = 0,
  Malformed,            // container, JSON, buffer, accessor or index is broken
  SparseAccessor,       // an animation accessor uses sparse storage
  UnsupportedAccessor,  // not plain 32-bit float SCALAR, VEC3 or VEC4
  UnsupportedChannel,   // a path other than translation, rotation, scale or weights; or an unknown interpolation on a bone
  InvalidSkeleton,      // no bones, duplicate bone names, or parent and pose counts that do not match the bones
  NoSkin,
  AmbiguousSkin,        // several skins and no choice, no match, or more than one match
  NoAnimation,
  AmbiguousAnimation,   // several animations and no choice, no match, or more than one match
  BadJoint,             // a joint of the chosen skin has no name or shares one
  MissingBone,          // an animated joint names no bone of the skeleton
  HierarchyMismatch,    // a joint's parent differs from its bone's parent
  AnimatedNonBone,      // an animated non-bone node has bones below it, or is a joint of another skin
  DuplicateChannel,     // a bone has two channels for one path
  NoAnimatedBone,
  BadRotation,          // a zero, tiny or non-finite rotation
  BadKeyTimes,          // key times that do not strictly increase
  BadAnnotation,
  ClipTooLarge,         // too long, too short, or too many frame-tracks
  DegenerateTransform,  // a pose that cannot be decomposed
  UnsupportedRoot,      // not exactly one root bone, or a root half turn about a horizontal axis
};

const char* hkx_gltf_status_name(HkxGltfStatus status);

using HkxGltfUriLoader = std::function<std::vector<std::byte>(std::string_view uri)>;

struct HkxGltfOptions {
  // Requires a name when the file holds several animations.
  std::optional<std::string> animation;
  // Requires a skin index, skin name or root joint name when the file holds several skins.
  std::optional<std::string> skin;
  // Requires a loader for an external buffer; it resolves a relative, percent-decoded path and must stay in the folder.
  HkxGltfUriLoader load_uri;
};

struct HkxGltfImport {
  // Invariant: frame-major, one track per skeleton bone in skeleton order, positions in game units, Havok axes.
  HkxInterleavedAnimation animation;
  // Invariant: the root's planar travel and turn about Z, rebased to frame 0; all zero when the root is not animated.
  HkxExtractedMotion motion;
  std::vector<HkxAnimationAnnotation> annotations;
  float frame_rate{0.0f};
  std::vector<std::string> animated;
  std::vector<std::string> reference_only;
  std::vector<std::string> unmapped;
  std::vector<std::string> rest_mismatch;
  std::size_t weight_channels{0};
  std::size_t non_bone_channels{0};
  // Invariant: the worst gap between the glTF curves and the baked frames, in units, radians and scale.
  float sampling_position{0.0f};
  float sampling_rotation{0.0f};
  float sampling_scale{0.0f};
  // Invariant: set to the reason whenever the status is not Ok.
  std::string message;
};

// Requires a .glb, or a .gltf whose buffers are data: URIs or readable through options.load_uri.
// Requires skeleton.bone_names, parent_indices and reference_pose to be the same length.
// Constraint: fail closed; any input the baker cannot place exactly is refused and `out` is left cleared.
HkxGltfStatus import_gltf_animation(std::span<const std::byte> bytes, const HkxSkeleton& skeleton,
                                    const HkxGltfOptions& options, HkxGltfImport& out);

}  // namespace fo4::hkx
