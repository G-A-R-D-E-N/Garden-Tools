#include "fo4/hkx/summary.hpp"

#include "fo4/hkx/skeleton.hpp"

#include <array>
#include <charconv>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>

namespace fo4::hkx {
namespace {

constexpr std::string_view k_placeholder = "(unnamed)";
constexpr char k_control_byte = '?';

std::string clean(std::string_view text) {
  std::string out;
  out.reserve(text.size());
  for (const char value : text) {
    const auto byte = static_cast<std::uint8_t>(value);
    out.push_back(byte < 0x20U ? k_control_byte : value);
  }
  return out;
}

std::string name_or_placeholder(std::string_view text) {
  return text.empty() ? std::string{k_placeholder} : clean(text);
}

// Invariant: names the decoder status in text, so a reported reason is never a bare enum value.
std::string_view status_reason(HkxStatus status) {
  switch (status) {
    case HkxStatus::Ok:
      return "view";
    case HkxStatus::Truncated:
      return "truncated";
    case HkxStatus::BadMagic:
      return "bad-magic";
    case HkxStatus::UnsupportedLayout:
      return "unsupported-layout";
    case HkxStatus::MissingSection:
      return "missing-section";
    case HkxStatus::BadSection:
      return "bad-section";
    case HkxStatus::FixupOutOfRange:
      return "fixup-out-of-range";
    case HkxStatus::DuplicateFixup:
      return "duplicate-fixup";
    case HkxStatus::BadClassName:
      return "bad-class-name";
    case HkxStatus::MissingObject:
      return "missing-object";
  }
  return "unknown";
}

std::string fixed(float value) {
  std::array<char, 64> buffer{};
  const auto result = std::to_chars(buffer.data(), buffer.data() + buffer.size(), value, std::chars_format::fixed, 4);
  if (result.ec != std::errc{}) return "0";
  return std::string(buffer.data(), result.ptr);
}

}  // namespace

HkxSummary read_summary(const HkxPackfile& packfile) {
  HkxSummary summary;
  summary.object_count = packfile.objects.size();

  HkxSkeleton skeleton;
  if (read_skeleton(packfile, skeleton) == HkxStatus::Ok && !skeleton.bone_names.empty()) {
    summary.has_skeleton = true;
    summary.skeleton_name = clean(skeleton.name);
    summary.bone_names.reserve(skeleton.bone_names.size());
    for (const auto& bone : skeleton.bone_names) summary.bone_names.push_back(clean(bone));
    summary.parent_indices = skeleton.parent_indices;
  }

  // Constraint: read_animation materializes one sample per frame per track, so a summary pays that decode for counts only.
  HkxAnimation animation;
  const auto status = read_animation(packfile, animation);
  if (status == HkxStatus::Ok && animation.kind != HkxAnimationKind::View) {
    HkxSummaryClip clip;
    clip.kind = animation.kind;
    clip.class_name = clean(animation.class_name);
    clip.duration = animation.duration;
    clip.frame_count = animation.frame_count;
    clip.track_count = animation.tracks.size();
    for (const auto& track : animation.annotation_tracks) clip.annotation_count += track.annotations.size();
    summary.clips.push_back(std::move(clip));
  }
  // Invariant: every animation object lands in clips or in undecoded, so a decode failure is never a file of no clips.
  if (status != HkxStatus::Ok || animation.kind == HkxAnimationKind::View) {
    std::vector<std::string> classes;
    for (const auto& object : packfile.objects) {
      if (is_animation_class(object.class_name)) classes.push_back(clean(object.class_name));
    }
    if (classes.size() > 1) {
      summary.undecoded.push_back(
          {classes.front(), "more than one animation object: " + std::to_string(classes.size()), {}});
    } else if (classes.size() == 1) {
      summary.undecoded.push_back({classes.front(), std::string{status_reason(status)},
                                   status == HkxStatus::UnsupportedLayout ? clean(animation.refusal.feature) : std::string{}});
    }
  }

  HkxAnimationBinding binding;
  if (read_binding(packfile, binding) == HkxStatus::Ok && !binding.original_skeleton_name.empty()) {
    summary.binding_skeletons.push_back(clean(binding.original_skeleton_name));
  }
  return summary;
}

std::string render_summary(const HkxSummary& summary) {
  std::string text = "objects " + std::to_string(summary.object_count) + "\n";
  text += "skeleton " + (summary.has_skeleton ? name_or_placeholder(summary.skeleton_name) : std::string{"none"}) +
          " bones " + std::to_string(summary.bone_names.size()) + "\n";
  for (std::size_t bone = 0; bone < summary.bone_names.size(); ++bone) {
    const auto parent = bone < summary.parent_indices.size() ? std::to_string(summary.parent_indices[bone])
                                                             : std::string{"-1"};
    text += "bone " + std::to_string(bone) + " " + name_or_placeholder(summary.bone_names[bone]) + " parent " +
            parent + "\n";
  }
  text += "clips " + std::to_string(summary.clips.size()) + "\n";
  for (const auto& clip : summary.clips) {
    text += "clip " + name_or_placeholder(clip.class_name) + " kind " + std::string{hkx_animation_kind_name(clip.kind)} +
            " duration " + fixed(clip.duration) + " frames " + std::to_string(clip.frame_count) + " tracks " +
            std::to_string(clip.track_count) + " annotations " + std::to_string(clip.annotation_count) + "\n";
  }
  text += "undecoded " + std::to_string(summary.undecoded.size()) + "\n";
  for (const auto& clip : summary.undecoded) {
    text += "undecoded " + name_or_placeholder(clip.class_name) + " " + clean(clip.reason) +
            (clip.detail.empty() ? std::string{} : " " + clean(clip.detail)) + "\n";
  }
  text += "bindings " + std::to_string(summary.binding_skeletons.size()) + "\n";
  for (const auto& name : summary.binding_skeletons) text += "binding " + name_or_placeholder(name) + "\n";
  return text;
}

}  // namespace fo4::hkx
