#include "fo4/hkx/animation.hpp"

#include "animation_internal.hpp"
#include "hkx_read_internal.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace fo4::hkx {
namespace {

constexpr std::size_t kMaxDecodedSamples = 1u << 20;
constexpr std::size_t kQsTransformSize = 48;
constexpr std::size_t kQsRotationOffset = 16;
constexpr std::size_t kQsScaleOffset = 32;
constexpr std::size_t kFloatSize = 4;

using detail::member_field;

std::array<float, 3> translation_of(const HkxSection& section, std::size_t at) {
  return {detail::value_at<float>(section.data, at), detail::value_at<float>(section.data, at + kFloatSize),
          detail::value_at<float>(section.data, at + 2 * kFloatSize)};
}

std::array<float, 4> rotation_of(const HkxSection& section, std::size_t at) {
  return {detail::value_at<float>(section.data, at + kQsRotationOffset),
          detail::value_at<float>(section.data, at + kQsRotationOffset + kFloatSize),
          detail::value_at<float>(section.data, at + kQsRotationOffset + 2 * kFloatSize),
          detail::value_at<float>(section.data, at + kQsRotationOffset + 3 * kFloatSize)};
}

std::array<float, 3> scale_of(const HkxSection& section, std::size_t at) {
  return {detail::value_at<float>(section.data, at + kQsScaleOffset),
          detail::value_at<float>(section.data, at + kQsScaleOffset + kFloatSize),
          detail::value_at<float>(section.data, at + kQsScaleOffset + 2 * kFloatSize)};
}

bool ends_with_animation(std::string_view class_name) {
  constexpr std::string_view suffix = "Animation";
  return class_name.size() >= suffix.size() && class_name.substr(class_name.size() - suffix.size()) == suffix;
}

}  // namespace

namespace detail {

HkxStatus decode_interleaved_animation(const HkxPackfile& packfile, const HkxObjectView& object,
                                      const HkxClassLayout& layout, HkxAnimation& out) {
  const HkxSection& section = packfile.sections[packfile.data_section];
  const HkxClassLayout* base = find_layout("hkaAnimation");
  if (base == nullptr) return HkxStatus::UnsupportedLayout;

  const auto type_at = member_field(section, *base, object, "type");
  const auto duration_at = member_field(section, *base, object, "duration");
  const auto tracks_at = member_field(section, *base, object, "numberOfTransformTracks");
  const auto float_tracks_at = member_field(section, *base, object, "numberOfFloatTracks");
  const auto transforms_at = member_field(section, layout, object, "transforms");
  const auto floats_at = member_field(section, layout, object, "floats");
  if (!type_at || !duration_at || !tracks_at || !float_tracks_at || !transforms_at || !floats_at) {
    return HkxStatus::UnsupportedLayout;
  }

  out.kind = HkxAnimationKind::Interleaved;
  out.type = value_at<std::int32_t>(section.data, *type_at);
  out.duration = value_at<float>(section.data, *duration_at);
  out.float_track_count = value_at<std::int32_t>(section.data, *float_tracks_at);
  const auto declared_tracks = value_at<std::int32_t>(section.data, *tracks_at);
  if (declared_tracks < 0 || out.float_track_count < 0) return HkxStatus::BadSection;

  const auto tracks = static_cast<std::size_t>(declared_tracks);
  if (tracks == 0) return HkxStatus::Ok;

  // Requires the array before any resize: a declared count the data cannot cover must not size the tracks.
  Elements transforms;
  if (const auto status = elements_at(section, *transforms_at, packfile.pointer_size, kQsTransformSize, transforms);
      status != HkxStatus::Ok) {
    return status;
  }
  if (tracks > transforms.count || transforms.count % tracks != 0) return HkxStatus::BadSection;
  const std::size_t frames = transforms.count / tracks;
  if (tracks > kMaxDecodedSamples || frames > kMaxDecodedSamples / tracks) return HkxStatus::BadSection;

  out.tracks.resize(tracks);
  out.frame_count = frames;
  out.blocks.count = 1;
  out.blocks.max_frames_per_block = frames;
  out.blocks.block_duration = out.duration;
  if (frames > 1 && out.duration > 0.0f) out.frame_duration = out.duration / static_cast<float>(frames - 1);

  for (std::size_t frame = 0; frame < frames; ++frame) {
    for (std::size_t track = 0; track < tracks; ++track) {
      const std::size_t at = transforms.first + (frame * tracks + track) * kQsTransformSize;
      HkxAnimationTrack& into = out.tracks[track];
      into.translations.push_back(translation_of(section, at));
      into.rotations.push_back(rotation_of(section, at));
      into.scales.push_back(scale_of(section, at));
    }
  }
  for (HkxAnimationTrack& track : out.tracks) {
    track.translation_animated = {true, true, true};
    track.rotation_animated = true;
    track.scale_animated = {true, true, true};
  }

  Elements floats;
  if (const auto status = elements_at(section, *floats_at, packfile.pointer_size, kFloatSize, floats);
      status != HkxStatus::Ok) {
    return status;
  }
  if (floats.count != frames * static_cast<std::size_t>(out.float_track_count)) return HkxStatus::BadSection;
  out.float_tracks.reserve(floats.count);
  for (std::size_t i = 0; i < floats.count; ++i) {
    out.float_tracks.push_back(value_at<float>(section.data, floats.first + i * kFloatSize));
  }
  return HkxStatus::Ok;
}

HkxStatus decode_annotation_tracks(const HkxPackfile& packfile, const HkxObjectView& object,
                                   std::vector<HkxAnnotationTrack>& out) {
  out.clear();
  const HkxClassLayout* base = find_layout("hkaAnimation");
  const HkxClassLayout* track_layout = find_layout("hkaAnnotationTrack");
  const HkxClassLayout* annotation_layout = find_layout("hkaAnnotationTrackAnnotation");
  if (base == nullptr || track_layout == nullptr || annotation_layout == nullptr) return HkxStatus::UnsupportedLayout;

  const HkxSection& section = packfile.sections[packfile.data_section];
  const auto tracks_at = member_field(section, *base, object, "annotationTracks");
  const auto track_name_at = find_member_offset(*track_layout, "trackName");
  const auto track_events_at = find_member_offset(*track_layout, "annotations");
  const auto event_time_at = find_member_offset(*annotation_layout, "time");
  const auto event_text_at = find_member_offset(*annotation_layout, "text");
  if (!tracks_at || !track_name_at || !track_events_at || !event_time_at || !event_text_at) {
    return HkxStatus::UnsupportedLayout;
  }

  Elements tracks;
  if (const auto status = elements_at(section, *tracks_at, packfile.pointer_size, track_layout->size, tracks);
      status != HkxStatus::Ok) {
    return status;
  }
  out.reserve(tracks.count);
  for (std::size_t i = 0; i < tracks.count; ++i) {
    const std::size_t track_at = tracks.first + i * track_layout->size;
    HkxAnnotationTrack track;
    // Requires a named or an unnamed track: a missing name pointer leaves the name empty, as BGS reads it.
    if (const auto name_target = local_target(section, track_at + *track_name_at)) {
      if (const auto status = read_string(section, *name_target, track.name); status != HkxStatus::Ok) return status;
    }

    Elements events;
    if (const auto status = elements_at(section, track_at + *track_events_at, packfile.pointer_size,
                                       annotation_layout->size, events);
        status != HkxStatus::Ok) {
      return status;
    }
    track.annotations.reserve(events.count);
    for (std::size_t j = 0; j < events.count; ++j) {
      const std::size_t event_at = events.first + j * annotation_layout->size;
      HkxAnimationAnnotation annotation;
      annotation.time = value_at<float>(section.data, event_at + *event_time_at);
      if (const auto text_target = local_target(section, event_at + *event_text_at)) {
        if (const auto status = read_string(section, *text_target, annotation.text); status != HkxStatus::Ok) {
          return status;
        }
      }
      track.annotations.push_back(std::move(annotation));
    }
    out.push_back(std::move(track));
  }
  return HkxStatus::Ok;
}

}  // namespace detail

HkxStatus read_animation(const HkxPackfile& packfile, HkxAnimation& out) {
  out = HkxAnimation{};

  const HkxObjectView* animation = nullptr;
  for (const HkxObjectView& object : packfile.objects) {
    if (object.class_name != "hkaSplineCompressedAnimation" &&
        object.class_name != "hkaInterleavedUncompressedAnimation") {
      continue;
    }
    animation = &object;
    break;
  }

  if (animation == nullptr) {
    for (const HkxObjectView& object : packfile.objects) {
      if (!ends_with_animation(object.class_name) || find_layout(object.class_name) == nullptr) continue;
      out.kind = HkxAnimationKind::View;
      out.class_name = object.class_name;
      return HkxStatus::Ok;
    }
    return HkxStatus::MissingObject;
  }

  const HkxClassLayout* layout = find_layout(animation->class_name, animation->signature);
  if (layout == nullptr) return HkxStatus::UnsupportedLayout;
  out.class_name = animation->class_name;

  const HkxStatus status = animation->class_name == "hkaSplineCompressedAnimation"
                               ? detail::decode_spline_animation(packfile, *animation, *layout, out)
                               : detail::decode_interleaved_animation(packfile, *animation, *layout, out);
  if (status != HkxStatus::Ok) {
    out = HkxAnimation{};
    return status;
  }
  if (const auto annotations = detail::decode_annotation_tracks(packfile, *animation, out.annotation_tracks);
      annotations != HkxStatus::Ok) {
    out = HkxAnimation{};
    return annotations;
  }
  return HkxStatus::Ok;
}

}  // namespace fo4::hkx
