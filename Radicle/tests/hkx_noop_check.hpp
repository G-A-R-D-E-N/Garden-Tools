#pragma once

#include "fo4/hkx/animation.hpp"
#include "fo4/hkx/behavior_edit.hpp"
#include "fo4/hkx/behavior_graph.hpp"
#include "fo4/hkx/gltf_animation.hpp"
#include "fo4/hkx/packfile.hpp"
#include "fo4/hkx/write.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace fo4::test::hkx {

struct BehaviorNoOp {
  // Invariant: empty when the writer's output reads as the same packfile and graph model as the source; else one sentence.
  std::string difference;
  bool bytes_identical{false};
  std::size_t edits{0};
};

// Requires a behaviour packfile with an event table or a start state to restate; the edits restate values the file already holds, so the model must not change.
inline BehaviorNoOp behavior_noop(std::span<const std::byte> bytes) {
  using namespace fo4::hkx;
  BehaviorNoOp out;
  HkxPackfile before;
  HkxGraphModel was;
  if (read_packfile(bytes, before) != HkxStatus::Ok || read_behavior_model(before, was) != HkxStatus::Ok) {
    out.difference = "the source does not read as a behaviour graph";
    return out;
  }
  std::vector<HkxBehaviorEdit> edits;
  if (!was.events.empty() && was.event_name_count == was.event_info_count) {
    HkxBehaviorEdit flags;
    flags.kind = HkxBehaviorEditKind::SetEventFlags;
    flags.index = 0;
    flags.flags = was.events[0].flags;
    edits.push_back(flags);
  }
  if (!was.state_machines.empty()) {
    const HkxGraphStateMachine& machine = was.state_machines[0];
    bool holds_start = false;
    for (std::size_t i = machine.first_state; i < machine.first_state + machine.state_count; ++i) {
      holds_start = holds_start || was.states[i].state_id == machine.start_state_id;
    }
    if (holds_start) {
      HkxBehaviorEdit start;
      start.kind = HkxBehaviorEditKind::SetStartState;
      start.state_machine = 0;
      start.value = machine.start_state_id;
      edits.push_back(start);
    }
  }
  out.edits = edits.size();
  if (edits.empty()) {
    out.difference = "the source holds no event table or start state to restate";
    return out;
  }

  std::vector<std::byte> written;
  const HkxBehaviorEditReport report = apply_behavior_edits(bytes, edits, written);
  if (!report.ok) {
    out.difference = "the writer refused a restated value: " + report.refusal;
    return out;
  }
  out.bytes_identical = written.size() == bytes.size() && std::equal(bytes.begin(), bytes.end(), written.begin());
  HkxPackfile after;
  HkxGraphModel now;
  if (read_packfile(written, after) != HkxStatus::Ok || read_behavior_model(after, now) != HkxStatus::Ok) {
    out.difference = "the written file does not read as a behaviour graph";
    return out;
  }
  for (HkxPackfile* pack : {&before, &after}) {
    for (HkxSection& section : pack->sections) {
      std::sort(section.global_fixups.begin(), section.global_fixups.end(),
                [](const HkxRemoteFixup& a, const HkxRemoteFixup& b) { return a.source < b.source; });
    }
  }
  out.difference = first_packfile_difference(before, after);
  if (!out.difference.empty()) return out;

  if (now.events.size() != was.events.size() || now.variables.size() != was.variables.size() ||
      now.nodes.size() != was.nodes.size() || now.state_machines.size() != was.state_machines.size() ||
      now.states.size() != was.states.size() || now.transitions.size() != was.transitions.size() ||
      now.clips.size() != was.clips.size() || now.bindings.size() != was.bindings.size() ||
      now.edges.size() != was.edges.size()) {
    out.difference = "a graph table changed its length";
    return out;
  }
  for (std::size_t i = 0; i < was.events.size(); ++i) {
    if (now.events[i].name != was.events[i].name || now.events[i].flags != was.events[i].flags) {
      out.difference = "event " + std::to_string(i) + " changed";
      return out;
    }
  }
  for (std::size_t i = 0; i < was.transitions.size(); ++i) {
    if (now.transitions[i].event_id != was.transitions[i].event_id ||
        now.transitions[i].to_state_id != was.transitions[i].to_state_id) {
      out.difference = "transition " + std::to_string(i) + " changed";
      return out;
    }
  }
  for (std::size_t i = 0; i < was.clips.size(); ++i) {
    if (now.clips[i].animation != was.clips[i].animation) {
      out.difference = "clip " + std::to_string(i) + " changed its animation";
      return out;
    }
  }
  if (report.after.errors != report.before.errors) out.difference = "the validation error count changed";
  return out;
}

struct ClipGap {
  float position{0.0f};
  float rotation{0.0f};
  float scale{0.0f};
};

// Invariant: the same measures save_clip_edit and save_gltf_animation judge by, so a budget there is a budget here; false when the clips have different shapes.
inline bool clip_gap(const fo4::hkx::HkxAnimation& a, const fo4::hkx::HkxAnimation& b, ClipGap& out) {
  if (a.frame_count != b.frame_count || a.tracks.size() != b.tracks.size()) return false;
  out = ClipGap{};
  const auto distance = [](const auto& x, const auto& y) {
    double sum = 0.0;
    for (std::size_t i = 0; i < 3; ++i) sum += (static_cast<double>(x[i]) - y[i]) * (static_cast<double>(x[i]) - y[i]);
    return static_cast<float>(std::sqrt(sum));
  };
  for (std::size_t t = 0; t < a.tracks.size(); ++t) {
    const auto& x = a.tracks[t];
    const auto& y = b.tracks[t];
    if (x.translations.size() != a.frame_count || y.translations.size() != a.frame_count ||
        x.rotations.size() != a.frame_count || y.rotations.size() != a.frame_count ||
        x.scales.size() != a.frame_count || y.scales.size() != a.frame_count) {
      return false;
    }
    for (std::size_t f = 0; f < a.frame_count; ++f) {
      out.position = std::max(out.position, distance(x.translations[f], y.translations[f]));
      out.scale = std::max(out.scale, distance(x.scales[f], y.scales[f]));
      double dot = 0.0;
      double la = 0.0;
      double lb = 0.0;
      for (std::size_t i = 0; i < 4; ++i) {
        dot += static_cast<double>(x.rotations[f][i]) * y.rotations[f][i];
        la += static_cast<double>(x.rotations[f][i]) * x.rotations[f][i];
        lb += static_cast<double>(y.rotations[f][i]) * y.rotations[f][i];
      }
      if (la > 0.0 && lb > 0.0) {
        const double cosine = std::min(1.0, std::fabs(dot) / std::sqrt(la * lb));
        out.rotation = std::max(out.rotation, static_cast<float>(2.0 * std::acos(cosine)));
      }
    }
  }
  return true;
}

// Requires a packfile with one clip and its binding; the import holds the clip's own decoded samples, root motion and notes, so saving it over that clip must change nothing.
// Constraint: an unmapped bone gets the identity pose; false when the clip's motion has samples that are not one per frame.
inline bool import_of_clip(std::span<const std::byte> bytes, std::size_t bones, fo4::hkx::HkxAnimation& clip,
                           fo4::hkx::HkxGltfImport& out) {
  using namespace fo4::hkx;
  HkxPackfile pack;
  HkxAnimationBinding binding;
  HkxExtractedMotion motion;
  if (read_packfile(bytes, pack) != HkxStatus::Ok || read_animation(pack, clip) != HkxStatus::Ok ||
      read_binding(pack, binding) != HkxStatus::Ok || read_extracted_motion(pack, motion) != HkxStatus::Ok) {
    return false;
  }
  const std::size_t frames = clip.frame_count;
  out = HkxGltfImport{};
  HkxInterleavedAnimation& baked = out.animation;
  baked.duration = clip.duration;
  baked.frame_count = frames;
  baked.track_count = bones;
  baked.translations.assign(frames * bones, {0.0f, 0.0f, 0.0f});
  baked.rotations.assign(frames * bones, {0.0f, 0.0f, 0.0f, 1.0f});
  baked.scales.assign(frames * bones, {1.0f, 1.0f, 1.0f});
  std::vector<std::int16_t> map = binding.transform_track_to_bone_indices;
  if (map.empty()) {
    for (std::size_t t = 0; t < clip.tracks.size(); ++t) map.push_back(static_cast<std::int16_t>(t));
  }
  if (map.size() != clip.tracks.size()) return false;
  for (std::size_t t = 0; t < clip.tracks.size(); ++t) {
    if (map[t] < 0 || static_cast<std::size_t>(map[t]) >= bones || clip.tracks[t].translations.size() != frames) return false;
    for (std::size_t f = 0; f < frames; ++f) {
      const std::size_t at = f * bones + static_cast<std::size_t>(map[t]);
      baked.translations[at] = clip.tracks[t].translations[f];
      baked.rotations[at] = clip.tracks[t].rotations[f];
      baked.scales[at] = clip.tracks[t].scales[f];
    }
  }
  if (motion.kind == HkxMotionKind::Default) {
    if (motion.samples.size() != frames) return false;
    out.motion = motion;
  } else {
    out.motion.samples.assign(frames, {0.0f, 0.0f, 0.0f, 0.0f});
  }
  for (const HkxAnnotationTrack& track : clip.annotation_tracks) {
    out.annotations.insert(out.annotations.end(), track.annotations.begin(), track.annotations.end());
  }
  out.frame_rate = clip.duration > 0.0f ? static_cast<float>(frames - 1) / clip.duration : 0.0f;
  return true;
}

}  // namespace fo4::test::hkx
