#pragma once

#include "fo4/hkx/gltf_animation.hpp"
#include "fo4/hkx/skeleton.hpp"
#include "fo4/hkx/spline_write.hpp"

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace fo4::hkx {

enum class HkxGltfSaveStatus : std::uint8_t {
  Ok = 0,
  BadImport,            // the import does not match the skeleton, or holds no frames
  BadTemplate,          // not a packfile this writer can lay out again
  TemplateNoClip,       // not exactly one decodable clip
  TemplateNoBinding,    // no single binding that names the template clip
  TemplateFloatTracks,  // float tracks would be dropped
  TemplateTrackMap,     // the track-to-bone map is out of range, repeated or missing
  UnmappedBone,         // glTF animates a bone the template has no track for
  AnnotationsReplaced,  // the template has events and replace_annotations is off
  MotionReplaced,       // the template frame has motion and replace_motion is off
  UnsupportedMotion,    // frame class, up axis or a moving root with no frame
  SharedData,           // the template clip cannot be cut out without touching other data
  WriteFailed,          // a writer refused the clip
  ReadBack,             // the reopened file disagrees with what was meant
  OverBudget,           // the stored curves drift past the error budget
  SkeletonMismatch,     // the binding names a different skeleton than the one given
};

const char* hkx_gltf_save_status_name(HkxGltfSaveStatus status);

struct HkxGltfSaveOptions {
  bool replace_annotations{false};
  bool replace_motion{false};
  HkxSplineOptions spline;
  float max_position_error{0.05f};
  float max_rotation_error{0.002f};
  float max_scale_error{0.002f};
};

struct HkxGltfSaveReport {
  std::size_t tracks{0};
  std::size_t frames{0};
  float frame_rate{0.0f};
  float duration{0.0f};
  // Invariant: one name per template track, in track order; a track is animated or held at the reference pose.
  std::vector<std::string> animated;
  std::vector<std::string> reference_tracks;
  // Invariant: skeleton bones the template has no track for; glTF did not animate them, so nothing was lost.
  std::vector<std::string> untracked_bones;
  std::vector<std::string> unmapped_joints;
  std::vector<std::string> rest_mismatch;
  std::size_t weight_channels{0};
  std::size_t non_bone_channels{0};
  std::size_t annotations_replaced{0};
  std::size_t annotations_written{0};
  bool motion_replaced{false};
  std::size_t motion_samples{0};
  bool binding_linked{false};
  std::string binding_skeleton_name;
  // Invariant: match, mismatch, or unverified when either name is empty.
  std::string skeleton_match;
  float sampling_position{0.0f};
  float sampling_rotation{0.0f};
  float sampling_scale{0.0f};
  float readback_position{0.0f};
  float readback_rotation{0.0f};
  float readback_scale{0.0f};
  // Invariant: set to the reason whenever the status is not Ok.
  std::string message;
};

struct HkxGltfSave {
  // Invariant: empty unless the status is Ok, so a refusal never hands back a half-written file.
  std::vector<std::byte> bytes;
  HkxGltfSaveReport report;
};

// Requires a template packfile that holds one clip and one binding naming it, and an import made against `skeleton`.
// Constraint: the template's binding and track map are kept; only its animation pointer moves to the new clip.
// Constraint: fail closed; any template or import this cannot place exactly is refused and `out.bytes` stays empty.
HkxGltfSaveStatus save_gltf_animation(std::span<const std::byte> template_bytes, const HkxSkeleton& skeleton,
                                      const HkxGltfImport& imported, const HkxGltfSaveOptions& options,
                                      HkxGltfSave& out);

}  // namespace fo4::hkx
