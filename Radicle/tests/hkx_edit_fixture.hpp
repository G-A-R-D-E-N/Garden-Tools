#pragma once

#include "fo4/hkx/layout.hpp"
#include "hkx_clip_fixture.hpp"
#include "hkx_test_fixture.hpp"

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace fo4::test::hkx {

inline constexpr std::size_t kEditFrames = 5;

struct EditSpec {
  bool motion{true};
  bool float_track{false};
  bool link_binding{true};
  bool extra_reference{true};
  bool second_clip{false};
  bool short_track_map{false};
  bool motion_extra_fixup{false};
  bool motion_shared{false};
  std::string motion_class{"hkaDefaultAnimatedReferenceFrame"};
  std::uint8_t frame_type{0};
};

inline std::array<float, 4> z_turn(float angle) { return {0.0f, 0.0f, std::sin(angle / 2.0f), std::cos(angle / 2.0f)}; }

// Invariant: five frames over four seconds, two tracks bound to bones 0 and 2, and an extra pointer at the clip.
inline std::vector<std::byte> editable_packfile(const EditSpec& spec = {}) {
  using fo4::hkx::find_layout;
  std::vector<std::byte> names;
  std::vector<std::byte> data;
  std::vector<std::uint32_t> virtuals;
  std::vector<std::uint32_t> local;
  std::vector<std::uint32_t> global;
  const auto place = [&](const char* class_name) {
    const auto name_at = add_class_name(names, signature_of(class_name), class_name);
    const std::size_t at = data.size();
    data.resize(at + find_layout(class_name)->size, std::byte{0});
    virtuals.insert(virtuals.end(), {static_cast<std::uint32_t>(at), 0u, name_at});
    return at;
  };
  const auto text = [&](const std::string& value) {
    const std::size_t at = data.size();
    for (const char ch : value) data.push_back(static_cast<std::byte>(ch));
    data.push_back(std::byte{0});
    return at;
  };
  const auto array = [&](std::size_t object, const char* layout, const char* member, std::size_t at,
                         std::size_t count) {
    set_count(data, object + member_at(layout, member), count);
    if (count != 0) link(local, object + member_at(layout, member), at);
  };

  const std::size_t binding_at = place("hkaAnimationBinding");
  const std::size_t clip_at = place("hkaInterleavedUncompressedAnimation");
  const std::size_t motion_at = spec.motion ? place(spec.motion_class.c_str()) : 0;
  if (spec.second_clip) place("hkaInterleavedUncompressedAnimation");

  link(local, binding_at + member_at("hkaAnimationBinding", "originalSkeletonName"), text("Rig"));
  data[binding_at + member_at("hkaAnimationBinding", "blendHint")] = std::byte{1};
  const std::size_t track_map_at = data.size();
  put_i16(data, 0);
  put_i16(data, 2);
  const std::size_t float_map_at = data.size();
  put_i16(data, 0);
  const std::size_t partitions_at = data.size();
  put_i16(data, 5);
  put_i16(data, 6);
  array(binding_at, "hkaAnimationBinding", "transformTrackToBoneIndices", track_map_at, spec.short_track_map ? 1 : 2);
  array(binding_at, "hkaAnimationBinding", "floatTrackToFloatSlotIndices", float_map_at, spec.float_track ? 1 : 0);
  array(binding_at, "hkaAnimationBinding", "partitionIndices", partitions_at, 2);
  if (spec.link_binding) {
    global.insert(global.end(), {static_cast<std::uint32_t>(binding_at + member_at("hkaAnimationBinding", "animation")),
                                 2u, static_cast<std::uint32_t>(clip_at)});
  }
  if (spec.motion && spec.motion_extra_fixup) {
    global.insert(global.end(), {static_cast<std::uint32_t>(motion_at), 2u, static_cast<std::uint32_t>(clip_at)});
  }
  if (spec.extra_reference) {
    const std::size_t slot_at = data.size();
    data.resize(slot_at + 8, std::byte{0});
    global.insert(global.end(), {static_cast<std::uint32_t>(slot_at), 2u, static_cast<std::uint32_t>(clip_at)});
  }
  if (spec.motion && spec.motion_shared) {
    const std::size_t slot_at = data.size();
    data.resize(slot_at + 8, std::byte{0});
    global.insert(global.end(), {static_cast<std::uint32_t>(slot_at), 2u, static_cast<std::uint32_t>(motion_at)});
  }

  const std::size_t transforms_at = data.size();
  for (std::size_t frame = 0; frame < kEditFrames; ++frame) {
    for (std::size_t track = 0; track < 2; ++track) {
      put_pose(data, static_cast<float>(frame * 10 + track), static_cast<float>(track), -static_cast<float>(frame),
               static_cast<float>(track + 1), z_turn(0.2f * static_cast<float>(frame)));
    }
  }
  const std::size_t floats_at = data.size();
  if (spec.float_track) {
    for (std::size_t frame = 0; frame < kEditFrames; ++frame) put_float(data, static_cast<float>(frame));
  }

  const std::size_t track_size = find_layout("hkaAnnotationTrack")->size;
  const std::size_t event_size = find_layout("hkaAnnotationTrackAnnotation")->size;
  const std::size_t annotation_tracks_at = data.size();
  data.resize(annotation_tracks_at + track_size, std::byte{0});
  const std::size_t events_at = data.size();
  data.resize(events_at + 3 * event_size, std::byte{0});
  link(local, annotation_tracks_at + member_at("hkaAnnotationTrack", "trackName"), text("Root"));
  array(annotation_tracks_at, "hkaAnnotationTrack", "annotations", events_at, 3);
  const std::vector<std::pair<float, std::string>> events = {{0.2f, "a"}, {2.0f, "b"}, {3.8f, "c"}};
  for (std::size_t i = 0; i < events.size(); ++i) {
    const std::size_t event_at = events_at + i * event_size;
    patch_float(data, event_at + member_at("hkaAnnotationTrackAnnotation", "time"), events[i].first);
    link(local, event_at + member_at("hkaAnnotationTrackAnnotation", "text"), text(events[i].second));
  }

  if (spec.motion) {
    data[motion_at + member_at("hkaAnimatedReferenceFrame", "frameType")] = std::byte{spec.frame_type};
    link(local, clip_at + member_at("hkaAnimation", "extractedMotion"), motion_at);
    if (spec.motion_class == "hkaDefaultAnimatedReferenceFrame") {
      const char* frame = "hkaDefaultAnimatedReferenceFrame";
      const std::array<float, 3> up{0.0f, 0.0f, 1.0f};
      const std::array<float, 3> forward{0.0f, 1.0f, 0.0f};
      for (std::size_t axis = 0; axis < 3; ++axis) {
        patch_float(data, motion_at + member_at(frame, "up") + 4 * axis, up[axis]);
        patch_float(data, motion_at + member_at(frame, "forward") + 4 * axis, forward[axis]);
      }
      patch_float(data, motion_at + member_at(frame, "duration"), 4.0f);
      const std::size_t samples_at = data.size();
      for (std::size_t f = 0; f < kEditFrames; ++f) {
        const auto step = static_cast<float>(f);
        for (const float value : {step, 2.0f * step, 0.0f, 0.1f * step}) put_float(data, value);
      }
      const std::size_t samples_field = motion_at + member_at(frame, "referenceFrameSamples");
      link(local, samples_field, samples_at);
      set_count(data, samples_field, kEditFrames);
    }
  }

  patch_u32(data, clip_at + member_at("hkaAnimation", "type"), 1);
  patch_float(data, clip_at + member_at("hkaAnimation", "duration"), 4.0f);
  patch_u32(data, clip_at + member_at("hkaAnimation", "numberOfTransformTracks"), 2);
  patch_u32(data, clip_at + member_at("hkaAnimation", "numberOfFloatTracks"), spec.float_track ? 1 : 0);
  array(clip_at, "hkaInterleavedUncompressedAnimation", "transforms", transforms_at, kEditFrames * 2);
  array(clip_at, "hkaInterleavedUncompressedAnimation", "floats", floats_at, spec.float_track ? kEditFrames : 0);
  array(clip_at, "hkaAnimation", "annotationTracks", annotation_tracks_at, 1);

  return build_packfile({
      TestSection{"__classnames__", names, {}, {}, {}},
      TestSection{"__types__", {}, {}, {}, {}},
      TestSection{"__data__", std::move(data), std::move(local), std::move(global), std::move(virtuals)},
  });
}

}  // namespace fo4::test::hkx
