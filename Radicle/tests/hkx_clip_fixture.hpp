#pragma once

#include "fo4/hkx/layout.hpp"
#include "hkx_animation_fixture.hpp"
#include "hkx_test_fixture.hpp"

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace fo4::test::hkx {

inline void put_i16(std::vector<std::byte>& data, std::int16_t value) {
  const auto bits = static_cast<std::uint16_t>(value);
  data.push_back(static_cast<std::byte>(bits & 0xFF));
  data.push_back(static_cast<std::byte>(bits >> 8));
}

inline std::array<float, 4> turn() {
  const float norm = std::sqrt(0.95f);
  return {0.1f / norm, 0.2f / norm, 0.3f / norm, 0.9f / norm};
}

inline void put_pose(std::vector<std::byte>& data, float tx, float ty, float tz, float scale,
                      const std::array<float, 4>& rotation = {0.0f, 0.0f, 0.0f, 1.0f}) {
  for (float value : {tx, ty, tz, 0.0f}) put_float(data, value);
  for (float value : rotation) put_float(data, value);
  for (float value : {scale, scale, scale, 0.0f}) put_float(data, value);
}

inline std::vector<std::byte> clip_packfile(bool with_skeleton, const std::vector<std::int16_t>& parents = {-1, 0, 1},
                                            bool link_animation = true) {
  using fo4::hkx::find_layout;
  std::vector<std::byte> names;
  std::vector<std::byte> data;
  std::vector<std::uint32_t> virtuals;
  std::vector<std::uint32_t> local;
  std::vector<std::uint32_t> global;

  const auto place = [&](const char* class_name) {
    const auto name_at = add_class_name(names, find_layout(class_name)->signature, class_name);
    const std::size_t at = data.size();
    data.resize(at + find_layout(class_name)->size, std::byte{0});
    virtuals.insert(virtuals.end(), {static_cast<std::uint32_t>(at), 0u, name_at});
    return at;
  };
  const auto text = [&](const std::string& value) {
    const std::size_t at = data.size();
    for (char ch : value) data.push_back(static_cast<std::byte>(ch));
    data.push_back(std::byte{0});
    return at;
  };
  const auto array = [&](std::size_t object, const char* layout, const char* member, std::size_t at,
                         std::size_t count) {
    link(local, object + member_at(layout, member), at);
    set_count(data, object + member_at(layout, member), count);
  };

  const std::size_t skeleton_at = with_skeleton ? place("hkaSkeleton") : 0;
  const std::size_t binding_at = place("hkaAnimationBinding");
  const std::size_t animation_at = place("hkaInterleavedUncompressedAnimation");

  if (with_skeleton) {
    const std::size_t bone_size = find_layout("hkaBone")->size;
    const std::vector<std::string> bones = {"Root", "Spine", "Head"};
    const std::size_t bones_at = data.size();
    data.resize(bones_at + bones.size() * bone_size, std::byte{0});
    for (std::size_t i = 0; i < bones.size(); ++i) {
      link(local, bones_at + i * bone_size + member_at("hkaBone", "name"), text(bones[i]));
    }
    const std::size_t parents_at = data.size();
    for (std::int16_t parent : parents) put_i16(data, parent);
    const std::size_t pose_at = data.size();
    for (std::size_t i = 0; i < bones.size(); ++i) put_pose(data, static_cast<float>(i + 1), 0.0f, 0.0f, 1.0f);
    link(local, skeleton_at + member_at("hkaSkeleton", "name"), text("Rig"));
    array(skeleton_at, "hkaSkeleton", "parentIndices", parents_at, parents.size());
    array(skeleton_at, "hkaSkeleton", "bones", bones_at, bones.size());
    array(skeleton_at, "hkaSkeleton", "referencePose", pose_at, bones.size());
    // Constraint: only bone 1 is locked, so the flag is read per bone.
    data[bones_at + bone_size + member_at("hkaBone", "lockTranslation")] = std::byte{1};

    const std::size_t floats_at = data.size();
    for (float value : {0.5f, 1.25f}) put_float(data, value);
    array(skeleton_at, "hkaSkeleton", "referenceFloats", floats_at, 2);

    const std::size_t slots_at = data.size();
    data.resize(slots_at + 2 * 8, std::byte{0});
    link(local, slots_at, text("speed"));
    // Constraint: slot 1 is a null pointer, so an unnamed slot keeps its index.
    array(skeleton_at, "hkaSkeleton", "floatSlots", slots_at, 2);

    const std::size_t frame_size = find_layout("hkaSkeletonLocalFrameOnBone")->size;
    const std::size_t frames_at = data.size();
    data.resize(frames_at + frame_size, std::byte{0});
    data[frames_at + member_at("hkaSkeletonLocalFrameOnBone", "boneIndex")] = std::byte{2};
    link(local, frames_at + member_at("hkaSkeletonLocalFrameOnBone", "localFrame"), text("frame"));
    array(skeleton_at, "hkaSkeleton", "localFrames", frames_at, 1);

    const std::size_t partition_size = find_layout("hkaSkeletonPartition")->size;
    const std::size_t partitions_at = data.size();
    data.resize(partitions_at + partition_size, std::byte{0});
    link(local, partitions_at + member_at("hkaSkeletonPartition", "name"), text("upper"));
    data[partitions_at + member_at("hkaSkeletonPartition", "startBoneIndex")] = std::byte{1};
    data[partitions_at + member_at("hkaSkeletonPartition", "numBones")] = std::byte{2};
    array(skeleton_at, "hkaSkeleton", "partitions", partitions_at, 1);
  }

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
  array(binding_at, "hkaAnimationBinding", "transformTrackToBoneIndices", track_map_at, 2);
  array(binding_at, "hkaAnimationBinding", "floatTrackToFloatSlotIndices", float_map_at, 1);
  array(binding_at, "hkaAnimationBinding", "partitionIndices", partitions_at, 2);
  if (link_animation) {
    global.push_back(static_cast<std::uint32_t>(binding_at + member_at("hkaAnimationBinding", "animation")));
    global.push_back(2);
    global.push_back(static_cast<std::uint32_t>(animation_at));
  }

  const std::array<float, 4> identity = {0.0f, 0.0f, 0.0f, 1.0f};
  const std::size_t transforms_at = data.size();
  for (std::size_t frame = 0; frame < 2; ++frame) {
    for (std::size_t track = 0; track < 2; ++track) {
      put_pose(data, static_cast<float>(track * 100 + frame), static_cast<float>(track), 0.0f,
               static_cast<float>(track + 1), frame == 1 && track == 1 ? turn() : identity);
    }
  }
  const std::size_t floats_at = data.size();
  put_float(data, 5.5f);
  put_float(data, 6.5f);

  const std::size_t track_size = find_layout("hkaAnnotationTrack")->size;
  const std::size_t event_size = find_layout("hkaAnnotationTrackAnnotation")->size;
  const std::size_t annotation_tracks_at = data.size();
  data.resize(annotation_tracks_at + track_size, std::byte{0});
  const std::size_t events_at = data.size();
  data.resize(events_at + 2 * event_size, std::byte{0});
  link(local, annotation_tracks_at + member_at("hkaAnnotationTrack", "trackName"), text("Root"));
  array(annotation_tracks_at, "hkaAnnotationTrack", "annotations", events_at, 2);
  const std::vector<std::pair<float, std::string>> events = {{0.25f, "start"}, {0.75f, "end"}};
  for (std::size_t i = 0; i < events.size(); ++i) {
    const std::size_t event_at = events_at + i * event_size;
    patch_float(data, event_at + member_at("hkaAnnotationTrackAnnotation", "time"), events[i].first);
    link(local, event_at + member_at("hkaAnnotationTrackAnnotation", "text"), text(events[i].second));
  }

  patch_u32(data, animation_at + member_at("hkaAnimation", "type"), 1);
  patch_float(data, animation_at + member_at("hkaAnimation", "duration"), 1.0f);
  patch_u32(data, animation_at + member_at("hkaAnimation", "numberOfTransformTracks"), 2);
  patch_u32(data, animation_at + member_at("hkaAnimation", "numberOfFloatTracks"), 1);
  array(animation_at, "hkaInterleavedUncompressedAnimation", "transforms", transforms_at, 4);
  array(animation_at, "hkaInterleavedUncompressedAnimation", "floats", floats_at, 2);
  array(animation_at, "hkaAnimation", "annotationTracks", annotation_tracks_at, 1);

  return build_packfile({
      TestSection{"__classnames__", names, {}, {}, {}},
      TestSection{"__types__", {}, {}, {}, {}},
      TestSection{"__data__", std::move(data), std::move(local), std::move(global), std::move(virtuals)},
  });
}

inline std::vector<std::byte> clip_generator_packfile(const std::vector<std::array<std::string, 2>>& clips,
                                                      const std::vector<std::int8_t>& modes, float speed) {
  using fo4::hkx::find_layout;
  const auto* layout = find_layout("hkbClipGenerator");
  std::vector<std::byte> names;
  std::vector<std::byte> data;
  std::vector<std::uint32_t> virtuals;
  std::vector<std::uint32_t> local;
  const auto class_name = add_class_name(names, layout->signature, "hkbClipGenerator");
  std::vector<std::size_t> objects;
  for (std::size_t i = 0; i < clips.size(); ++i) {
    objects.push_back(data.size());
    virtuals.insert(virtuals.end(), {static_cast<std::uint32_t>(data.size()), 0u, class_name});
    data.resize(data.size() + layout->size, std::byte{0});
  }
  const auto text = [&](const std::string& value) {
    const std::size_t at = data.size();
    for (const char ch : value) data.push_back(static_cast<std::byte>(ch));
    data.push_back(std::byte{0});
    return at;
  };
  for (std::size_t i = 0; i < clips.size(); ++i) {
    link(local, objects[i] + member_at("hkbNode", "name"), text(clips[i][0]));
    link(local, objects[i] + member_at("hkbClipGenerator", "animationName"), text(clips[i][1]));
    data[objects[i] + member_at("hkbClipGenerator", "mode")] = static_cast<std::byte>(modes[i]);
    patch_float(data, objects[i] + member_at("hkbClipGenerator", "playbackSpeed"), speed);
  }
  return build_packfile({
      TestSection{"__classnames__", names, {}, {}, {}},
      TestSection{"__types__", {}, {}, {}, {}},
      TestSection{"__data__", std::move(data), std::move(local), {}, std::move(virtuals)},
  });
}

}  // namespace fo4::test::hkx
