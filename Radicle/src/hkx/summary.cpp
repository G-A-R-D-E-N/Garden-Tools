#include "fo4/hkx/summary.hpp"

#include <algorithm>
#include <cstdio>
#include <utility>

namespace fo4::hkx {
namespace {

std::size_t keys_of(const HkxAnimationTrack& track) {
  return std::max({track.translations.size(), track.rotations.size(), track.scales.size()});
}

std::string fixed(float value) {
  char text[32] = {};
  std::snprintf(text, sizeof(text), "%.4f", static_cast<double>(value));
  return text;
}

}  // namespace

HkxSummary read_summary(const HkxPackfile& packfile) {
  HkxSummary summary;
  summary.object_count = packfile.objects.size();

  HkxSkeleton skeleton;
  if (read_skeleton(packfile, skeleton) == HkxStatus::Ok && !skeleton.bone_names.empty()) {
    summary.has_skeleton = true;
    summary.skeleton_name = skeleton.name;
    summary.bone_names = skeleton.bone_names;
    summary.parent_indices = skeleton.parent_indices;
  }

  HkxAnimation animation;
  if (read_animation(packfile, animation) == HkxStatus::Ok) {
    HkxSummaryClip clip;
    clip.class_name = animation.class_name;
    clip.duration = animation.duration;
    clip.frame_count = animation.frame_count;
    clip.track_count = animation.tracks.size();
    for (const auto& track : animation.tracks) {
      const auto keys = keys_of(track);
      clip.key_count += keys;
      clip.max_keys_per_track = std::max(clip.max_keys_per_track, keys);
    }
    for (const auto& track : animation.annotation_tracks) clip.annotation_count += track.annotations.size();
    if (!clip.class_name.empty() || clip.frame_count > 0) summary.clips.push_back(std::move(clip));
  }

  HkxAnimationBinding binding;
  if (read_binding(packfile, binding) == HkxStatus::Ok && !binding.original_skeleton_name.empty()) {
    summary.binding_skeletons.push_back(binding.original_skeleton_name);
  }
  return summary;
}

std::string render_summary(const HkxSummary& summary) {
  std::string text = "objects " + std::to_string(summary.object_count) + "\n";
  text += "skeleton " + (summary.has_skeleton ? summary.skeleton_name : std::string{"none"}) + " bones " +
          std::to_string(summary.bone_names.size()) + "\n";
  for (std::size_t bone = 0; bone < summary.bone_names.size(); ++bone) {
    const auto parent = bone < summary.parent_indices.size()
                            ? std::to_string(summary.parent_indices[bone])
                            : std::string{"-1"};
    text += "bone " + std::to_string(bone) + " " + summary.bone_names[bone] + " parent " + parent + "\n";
  }
  text += "clips " + std::to_string(summary.clips.size()) + "\n";
  for (const auto& clip : summary.clips) {
    text += "clip " + clip.class_name + " duration " + fixed(clip.duration) + " frames " +
            std::to_string(clip.frame_count) + " tracks " + std::to_string(clip.track_count) + " keys " +
            std::to_string(clip.key_count) + " maxkeys " + std::to_string(clip.max_keys_per_track) +
            " annotations " + std::to_string(clip.annotation_count) + "\n";
  }
  text += "bindings " + std::to_string(summary.binding_skeletons.size()) + "\n";
  for (const auto& name : summary.binding_skeletons) text += "binding " + name + "\n";
  return text;
}

}  // namespace fo4::hkx
