#include "fo4/hkx/animation.hpp"

#include "animation_internal.hpp"
#include "hkx_read_internal.hpp"

#include <algorithm>
#include <array>
#include <cmath>
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

constexpr std::size_t kLosslessQuaternionStride = 16;
constexpr std::size_t kLosslessTranslationWordStride = 8;
constexpr std::size_t kLosslessRotationWordStride = 2;

constexpr int kLosslessTrackClear = 0;
constexpr int kLosslessTrackStatic = 1;
constexpr int kLosslessTrackDynamic = 2;

// Requires one 16-bit channel field of a lossless track word: bits 0-1 name the track type, bits 2-15 hold the offset.
int lossless_type(std::uint64_t word, std::size_t component) {
  return static_cast<int>((word >> (component * 16)) & 0x3u);
}

std::size_t lossless_offset(std::uint64_t word, std::size_t component) {
  return static_cast<std::size_t>(((word >> (component * 16)) & 0xFFFFu) >> 2);
}

// Constraint: BGS caps numFrames * numTracks at MaxFrameTracks (1 << 22) rather than the spline sample budget.
constexpr std::size_t kMaxLosslessFrameTracks = 1u << 22;

// Requires a channel that fits the array it selects: BGS refuses a static offset past the static array and a
// dynamic channel that runs past its array, so the clip must be refused instead of posed with neutral values.
bool lossless_channel_in_range(int type, std::size_t offset, std::size_t frames, std::size_t stride,
                               const detail::Elements& dynamic, const detail::Elements& constant) {
  if (type == kLosslessTrackStatic) return offset < constant.count;
  if (type == kLosslessTrackDynamic) return frames != 0 && offset + (frames - 1) * stride < dynamic.count;
  return true;
}

// Requires Elements::first to be a byte offset: the index is scaled before it is added, so a sample address is
// byte offset first + index * element_size and not (first + index) * element_size.
std::optional<std::size_t> lossless_sample_at(int type, std::size_t offset, std::size_t frame, std::size_t stride,
                                              std::size_t element_size, const detail::Elements& dynamic,
                                              const detail::Elements& constant) {
  if (type == kLosslessTrackStatic) {
    if (offset < constant.count) return constant.first + offset * element_size;
    return std::nullopt;
  }
  if (type == kLosslessTrackDynamic) {
    const std::size_t index = offset + frame * stride;
    if (index < dynamic.count) return dynamic.first + index * element_size;
  }
  return std::nullopt;
}

float lossless_scalar(const HkxSection& section, std::uint64_t word, std::size_t component, std::size_t frame,
                      std::size_t stride, const detail::Elements& dynamic, const detail::Elements& constant, float fallback) {
  const auto at = lossless_sample_at(lossless_type(word, component), lossless_offset(word, component), frame, stride,
                                     kFloatSize, dynamic, constant);
  return at ? detail::value_at<float>(section.data, *at) : fallback;
}

std::array<float, 4> lossless_rotation(const HkxSection& section, std::uint64_t word, std::size_t frame,
                                       std::size_t stride, const detail::Elements& dynamic, const detail::Elements& constant) {
  constexpr std::array<float, 4> identity{0.0f, 0.0f, 0.0f, 1.0f};
  const auto at = lossless_sample_at(lossless_type(word, 0), lossless_offset(word, 0), frame, stride,
                                     kLosslessQuaternionStride, dynamic, constant);
  if (!at) return identity;
  const std::size_t base = *at;
  return {detail::value_at<float>(section.data, base), detail::value_at<float>(section.data, base + kFloatSize),
          detail::value_at<float>(section.data, base + 2 * kFloatSize), detail::value_at<float>(section.data, base + 3 * kFloatSize)};
}

// Requires one supported animation object per packfile: a second clip cannot be tied to its binding or skeleton.
HkxStatus select_animation(const HkxPackfile& packfile, const HkxObjectView*& out) {
  out = nullptr;
  for (const HkxObjectView& object : packfile.objects) {
    if (object.class_name != "hkaSplineCompressedAnimation" &&
        object.class_name != "hkaInterleavedUncompressedAnimation" &&
        object.class_name != "hkaLosslessCompressedAnimation" &&
        object.class_name != "hkaPredictiveCompressedAnimation" &&
        object.class_name != "hkaQuantizedAnimation") {
      continue;
    }
    if (out != nullptr) return HkxStatus::UnsupportedLayout;
    out = &object;
  }
  return HkxStatus::Ok;
}

bool all_finite(const std::array<float, 3>& values) {
  return std::all_of(values.begin(), values.end(), [](float value) { return std::isfinite(value); });
}

bool all_finite(const std::array<float, 4>& values) {
  return std::isfinite(values[0]) && std::isfinite(values[1]) && std::isfinite(values[2]) &&
         std::isfinite(values[3]);
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
  if (tracks == 0) return out.float_track_count == 0 ? HkxStatus::Ok : HkxStatus::UnsupportedLayout;

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
      const HkxQsTransform sample{translation_of(section, at), rotation_of(section, at), scale_of(section, at)};
      if (!detail::transform_is_finite(sample)) return HkxStatus::BadSection;
      HkxAnimationTrack& into = out.tracks[track];
      into.translations.push_back(sample.translation);
      into.rotations.push_back(sample.rotation);
      into.scales.push_back(sample.scale);
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

HkxStatus decode_lossless_animation(const HkxPackfile& packfile, const HkxObjectView& object,
                                    const HkxClassLayout& layout, HkxAnimation& out) {
  const HkxSection& section = packfile.sections[packfile.data_section];
  const HkxClassLayout* base = find_layout("hkaAnimation");
  if (base == nullptr) return HkxStatus::UnsupportedLayout;

  const auto type_at = member_field(section, *base, object, "type");
  const auto duration_at = member_field(section, *base, object, "duration");
  const auto tracks_at = member_field(section, *base, object, "numberOfTransformTracks");
  const auto float_tracks_at = member_field(section, *base, object, "numberOfFloatTracks");
  const auto frames_at = member_field(section, layout, object, "numFrames");
  const auto dynamic_translations_at = member_field(section, layout, object, "dynamicTranslations");
  const auto static_translations_at = member_field(section, layout, object, "staticTranslations");
  const auto translation_words_at = member_field(section, layout, object, "translationTypeAndOffsets");
  const auto dynamic_rotations_at = member_field(section, layout, object, "dynamicRotations");
  const auto static_rotations_at = member_field(section, layout, object, "staticRotations");
  const auto rotation_words_at = member_field(section, layout, object, "rotationTypeAndOffsets");
  const auto dynamic_scales_at = member_field(section, layout, object, "dynamicScales");
  const auto static_scales_at = member_field(section, layout, object, "staticScales");
  const auto scale_words_at = member_field(section, layout, object, "scaleTypeAndOffsets");
  const auto floats_at = member_field(section, layout, object, "floats");
  if (!type_at || !duration_at || !tracks_at || !float_tracks_at || !frames_at || !dynamic_translations_at ||
      !static_translations_at || !translation_words_at || !dynamic_rotations_at || !static_rotations_at ||
      !rotation_words_at || !dynamic_scales_at || !static_scales_at || !scale_words_at || !floats_at) {
    return HkxStatus::UnsupportedLayout;
  }

  out.kind = HkxAnimationKind::Lossless;
  out.type = value_at<std::int32_t>(section.data, *type_at);
  out.duration = value_at<float>(section.data, *duration_at);
  out.float_track_count = value_at<std::int32_t>(section.data, *float_tracks_at);
  const auto declared_tracks = value_at<std::int32_t>(section.data, *tracks_at);
  const auto declared_frames = value_at<std::int32_t>(section.data, *frames_at);
  if (declared_tracks < 0 || declared_frames < 0 || out.float_track_count < 0) return HkxStatus::BadSection;

  const auto tracks = static_cast<std::size_t>(declared_tracks);
  const auto frames = static_cast<std::size_t>(declared_frames);
  // Constraint: declared frames and block span survive a zero track count, so a float-only clip keeps its metadata.
  out.frame_count = frames;
  out.blocks.count = 1;
  out.blocks.max_frames_per_block = frames;
  out.blocks.block_duration = out.duration;
  if (frames > 1 && out.duration > 0.0f) out.frame_duration = out.duration / static_cast<float>(frames - 1);
  // Constraint: BGS returns an empty clip for zero frames before it caps the frame-track product.
  if (frames == 0) return HkxStatus::Ok;
  // Constraint: BGS caps numFrames * numTracks at MaxFrameTracks before anything is sized; a larger clip is refused.
  // Constraint: a zero-track clip has no product to cap.
  if (tracks != 0 && (tracks > kMaxLosslessFrameTracks || frames > kMaxLosslessFrameTracks / tracks)) {
    return HkxStatus::BadSection;
  }

  Elements dynamic_translations;
  Elements static_translations;
  Elements translation_words;
  Elements dynamic_rotations;
  Elements static_rotations;
  Elements rotation_words;
  Elements dynamic_scales;
  Elements static_scales;
  Elements scale_words;
  const auto read_array = [&](const std::optional<std::size_t>& field, std::size_t element_size, Elements& into) {
    return elements_at(section, *field, packfile.pointer_size, element_size, into);
  };
  if (const auto status = read_array(dynamic_translations_at, kFloatSize, dynamic_translations);
      status != HkxStatus::Ok) {
    return status;
  }
  if (const auto status = read_array(static_translations_at, kFloatSize, static_translations);
      status != HkxStatus::Ok) {
    return status;
  }
  if (const auto status = read_array(translation_words_at, kLosslessTranslationWordStride, translation_words);
      status != HkxStatus::Ok) {
    return status;
  }
  if (const auto status = read_array(dynamic_rotations_at, kLosslessQuaternionStride, dynamic_rotations);
      status != HkxStatus::Ok) {
    return status;
  }
  if (const auto status = read_array(static_rotations_at, kLosslessQuaternionStride, static_rotations);
      status != HkxStatus::Ok) {
    return status;
  }
  if (const auto status = read_array(rotation_words_at, kLosslessRotationWordStride, rotation_words);
      status != HkxStatus::Ok) {
    return status;
  }
  if (const auto status = read_array(dynamic_scales_at, kFloatSize, dynamic_scales); status != HkxStatus::Ok) {
    return status;
  }
  if (const auto status = read_array(static_scales_at, kFloatSize, static_scales); status != HkxStatus::Ok) {
    return status;
  }
  if (const auto status = read_array(scale_words_at, kLosslessTranslationWordStride, scale_words);
      status != HkxStatus::Ok) {
    return status;
  }

  // Constraint: BGS divides the stride out and ignores a partial trailing frame, so the same clips decode here.

  // Requires one channel word per track: a missing word would silently decode that bone as a clear channel.
  if (translation_words.count < tracks || rotation_words.count < tracks || scale_words.count < tracks) {
    return HkxStatus::BadSection;
  }

  Elements float_values;
  std::size_t float_samples = 0;
  // Constraint: BGS never reads the lossless float array, so an unreadable or short payload leaves the float
  // tracks empty instead of refusing a clip whose bone channels decode.
  if (out.float_track_count > 0 && read_array(floats_at, kFloatSize, float_values) == HkxStatus::Ok) {
    float_samples = frames * static_cast<std::size_t>(out.float_track_count);
    if (float_values.count < float_samples) float_samples = 0;
  }

  const std::size_t translation_stride = dynamic_translations.count / frames;
  const std::size_t rotation_stride = dynamic_rotations.count / frames;
  const std::size_t scale_stride = dynamic_scales.count / frames;

  out.float_tracks.reserve(float_samples);
  for (std::size_t i = 0; i < float_samples; ++i) {
    out.float_tracks.push_back(value_at<float>(section.data, float_values.first + i * kFloatSize));
  }

  out.tracks.resize(tracks);
  for (std::size_t track = 0; track < tracks; ++track) {
    const auto translation_word =
        value_at<std::uint64_t>(section.data, translation_words.first + track * kLosslessTranslationWordStride);
    const auto rotation_word = static_cast<std::uint64_t>(
        value_at<std::uint16_t>(section.data, rotation_words.first + track * kLosslessRotationWordStride));
    const auto scale_word =
        value_at<std::uint64_t>(section.data, scale_words.first + track * kLosslessTranslationWordStride);
    // Constraint: BGS refuses a channel that points past its array, so a broken clip is reported, not posed neutral.
    for (std::size_t axis = 0; axis < 3; ++axis) {
      if (!lossless_channel_in_range(lossless_type(translation_word, axis), lossless_offset(translation_word, axis),
                                     frames, translation_stride, dynamic_translations, static_translations) ||
          !lossless_channel_in_range(lossless_type(scale_word, axis), lossless_offset(scale_word, axis), frames,
                                     scale_stride, dynamic_scales, static_scales)) {
        return HkxStatus::BadSection;
      }
    }
    if (!lossless_channel_in_range(lossless_type(rotation_word, 0), lossless_offset(rotation_word, 0), frames,
                                   rotation_stride, dynamic_rotations, static_rotations)) {
      return HkxStatus::BadSection;
    }
    HkxAnimationTrack& into = out.tracks[track];
    for (std::size_t axis = 0; axis < 3; ++axis) {
      into.translation_animated[axis] = lossless_type(translation_word, axis) != kLosslessTrackClear;
      into.scale_animated[axis] = lossless_type(scale_word, axis) != kLosslessTrackClear;
    }
    into.rotation_animated = lossless_type(rotation_word, 0) != kLosslessTrackClear;
    into.translations.reserve(frames);
    into.rotations.reserve(frames);
    into.scales.reserve(frames);
    for (std::size_t frame = 0; frame < frames; ++frame) {
      const std::array<float, 3> translation{
          lossless_scalar(section, translation_word, 0, frame, translation_stride, dynamic_translations,
                          static_translations, 0.0f),
          lossless_scalar(section, translation_word, 1, frame, translation_stride, dynamic_translations,
                          static_translations, 0.0f),
          lossless_scalar(section, translation_word, 2, frame, translation_stride, dynamic_translations,
                          static_translations, 0.0f)};
      const std::array<float, 4> rotation =
          lossless_rotation(section, rotation_word, frame, rotation_stride, dynamic_rotations, static_rotations);
      const std::array<float, 3> scale{
          lossless_scalar(section, scale_word, 0, frame, scale_stride, dynamic_scales, static_scales, 1.0f),
          lossless_scalar(section, scale_word, 1, frame, scale_stride, dynamic_scales, static_scales, 1.0f),
          lossless_scalar(section, scale_word, 2, frame, scale_stride, dynamic_scales, static_scales, 1.0f)};
      if (!all_finite(translation) || !all_finite(rotation) || !all_finite(scale)) return HkxStatus::BadSection;
      into.translations.push_back(translation);
      into.rotations.push_back(rotation);
      into.scales.push_back(scale);
    }
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
  if (const auto status = select_animation(packfile, animation); status != HkxStatus::Ok) return status;

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
  if (layout == nullptr) {
    out.refusal = {std::string(animation->class_name), "no verified layout for this class signature"};
    return HkxStatus::UnsupportedLayout;
  }
  out.class_name = animation->class_name;

  std::string feature;
  HkxStatus status = HkxStatus::UnsupportedLayout;
  if (animation->class_name == "hkaSplineCompressedAnimation") {
    status = detail::decode_spline_animation(packfile, *animation, *layout, out);
  } else if (animation->class_name == "hkaLosslessCompressedAnimation") {
    status = detail::decode_lossless_animation(packfile, *animation, *layout, out);
  } else if (animation->class_name == "hkaPredictiveCompressedAnimation") {
    status = detail::decode_predictive_animation(packfile, *animation, *layout, out, feature);
  } else if (animation->class_name == "hkaQuantizedAnimation") {
    status = detail::decode_quantized_animation(packfile, *animation, *layout, out);
  } else {
    status = detail::decode_interleaved_animation(packfile, *animation, *layout, out);
  }
  if (status != HkxStatus::Ok || !std::isfinite(out.duration)) {
    out = HkxAnimation{};
    if (status == HkxStatus::UnsupportedLayout) out.refusal = {std::string(animation->class_name), std::move(feature)};
    return status == HkxStatus::Ok ? HkxStatus::BadSection : status;
  }
  if (const auto annotations = detail::decode_annotation_tracks(packfile, *animation, out.annotation_tracks);
      annotations != HkxStatus::Ok) {
    out = HkxAnimation{};
    return annotations;
  }
  return HkxStatus::Ok;
}

const char* hkx_animation_kind_name(HkxAnimationKind kind) {
  switch (kind) {
    case HkxAnimationKind::Interleaved:
      return "interleaved";
    case HkxAnimationKind::Spline:
      return "spline";
    case HkxAnimationKind::Lossless:
      return "lossless";
    case HkxAnimationKind::Predictive:
      return "predictive";
    case HkxAnimationKind::Quantized:
      return "quantized";
    case HkxAnimationKind::View:
      return "view";
  }
  return "view";
}

bool is_animation_class(std::string_view class_name) noexcept { return ends_with_animation(class_name); }

namespace {

constexpr std::size_t kMotionSampleSize = 4 * kFloatSize;

// Requires a fixup at the field: a null pointer has none and reads as no extracted motion.
bool has_fixup(const HkxSection& section, std::size_t source) {
  const auto local = std::any_of(section.local_fixups.begin(), section.local_fixups.end(),
                                 [source](const HkxLocalFixup& fixup) { return fixup.source == source; });
  const auto remote = std::any_of(section.global_fixups.begin(), section.global_fixups.end(),
                                  [source](const HkxRemoteFixup& fixup) { return fixup.source == source; });
  return local || remote;
}

std::optional<std::array<float, 3>> vector3_at(const HkxSection& section, std::size_t at) {
  if (!detail::in_section(section, at, 3 * kFloatSize)) return std::nullopt;
  return translation_of(section, at);
}

float dot3(const std::array<float, 3>& a, const std::array<float, 3>& b) {
  return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}

}  // namespace

// Constraint: up and forward must be finite and nonzero; the class name alone selects the decoder.
HkxStatus read_extracted_motion(const HkxPackfile& packfile, HkxExtractedMotion& out) {
  out = HkxExtractedMotion{};
  const HkxObjectView* animation = nullptr;
  if (const auto status = select_animation(packfile, animation); status != HkxStatus::Ok) return status;
  if (animation == nullptr) return HkxStatus::MissingObject;

  const HkxSection& section = packfile.sections[packfile.data_section];
  const HkxClassLayout* base = find_layout("hkaAnimation");
  if (base == nullptr) return HkxStatus::UnsupportedLayout;
  const auto motion_at = detail::member_field(section, *base, *animation, "extractedMotion");
  if (!motion_at) return HkxStatus::UnsupportedLayout;
  if (!has_fixup(section, *motion_at)) return HkxStatus::Ok;

  const auto frame_offset = detail::pointer_target(packfile, section, *motion_at);
  if (!frame_offset) return HkxStatus::FixupOutOfRange;
  const HkxObjectView* frame = nullptr;
  for (const HkxObjectView& object : packfile.objects) {
    if (static_cast<std::size_t>(object.offset) == *frame_offset) {
      frame = &object;
      break;
    }
  }
  if (frame == nullptr) return HkxStatus::MissingObject;

  HkxExtractedMotion motion;
  motion.class_name = std::string(frame->class_name);
  if (frame->class_name != "hkaDefaultAnimatedReferenceFrame") {
    motion.kind = HkxMotionKind::Unsupported;
    out = std::move(motion);
    return HkxStatus::Ok;
  }

  const HkxClassLayout* reference = find_layout("hkaAnimatedReferenceFrame");
  if (reference == nullptr) return HkxStatus::UnsupportedLayout;
  const auto frame_type_at = detail::member_field(section, *reference, *frame, "frameType");
  if (!frame_type_at) return HkxStatus::UnsupportedLayout;
  const auto frame_type = detail::value_at<std::int8_t>(section.data, *frame_type_at);
  if (frame_type != 0 && frame_type != 1) {
    motion.kind = HkxMotionKind::Unsupported;
    out = std::move(motion);
    return HkxStatus::Ok;
  }

  const HkxClassLayout* layout = find_layout(frame->class_name, frame->signature);
  if (layout == nullptr) return HkxStatus::UnsupportedLayout;
  const auto up_at = detail::member_field(section, *layout, *frame, "up");
  const auto forward_at = detail::member_field(section, *layout, *frame, "forward");
  const auto duration_at = detail::member_field(section, *layout, *frame, "duration");
  const auto samples_at = detail::member_field(section, *layout, *frame, "referenceFrameSamples");
  if (!up_at || !forward_at || !duration_at || !samples_at) return HkxStatus::UnsupportedLayout;

  const auto up = vector3_at(section, *up_at);
  const auto forward = vector3_at(section, *forward_at);
  const auto duration = detail::value_at<float>(section.data, *duration_at);
  if (!up || !forward || !all_finite(*up) || !all_finite(*forward) || !std::isfinite(duration)) {
    return HkxStatus::BadSection;
  }
  if (*up == std::array<float, 3>{} || *forward == std::array<float, 3>{}) return HkxStatus::BadSection;

  detail::Elements elements;
  if (const auto status = detail::elements_at(section, *samples_at, packfile.pointer_size, kMotionSampleSize, elements);
      status != HkxStatus::Ok) {
    return status;
  }
  if (elements.count > kMaxDecodedSamples) return HkxStatus::BadSection;

  motion.kind = HkxMotionKind::Default;
  motion.up = *up;
  motion.forward = *forward;
  motion.duration = duration;
  motion.samples.reserve(elements.count);
  for (std::size_t i = 0; i < elements.count; ++i) {
    const std::size_t at = elements.first + i * kMotionSampleSize;
    const std::array<float, 4> sample{detail::value_at<float>(section.data, at),
                                      detail::value_at<float>(section.data, at + kFloatSize),
                                      detail::value_at<float>(section.data, at + 2 * kFloatSize),
                                      detail::value_at<float>(section.data, at + 3 * kFloatSize)};
    if (!all_finite(sample)) return HkxStatus::BadSection;
    motion.samples.push_back(sample);
  }
  out = std::move(motion);
  return HkxStatus::Ok;
}

// Constraint: mean_planar_speed sums step lengths orthogonal to up and divides by duration.
HkxMotionSummary summarize_motion(const HkxExtractedMotion& motion) {
  HkxMotionSummary summary{};
  if (motion.kind != HkxMotionKind::Default || motion.samples.empty()) return summary;

  const auto& first = motion.samples.front();
  const auto& last = motion.samples.back();
  summary.net_translation = {last[0] - first[0], last[1] - first[1], last[2] - first[2]};
  summary.net_turn_radians = last[3] - first[3];

  // Constraint: a zero up has no plane to project onto, so speed stays 0.
  const float length = std::sqrt(dot3(motion.up, motion.up));
  if (length == 0.0f) return summary;
  const std::array<float, 3> axis{motion.up[0] / length, motion.up[1] / length, motion.up[2] / length};

  float path = 0.0f;
  for (std::size_t i = 1; i < motion.samples.size(); ++i) {
    const auto& a = motion.samples[i - 1];
    const auto& b = motion.samples[i];
    std::array<float, 3> step{b[0] - a[0], b[1] - a[1], b[2] - a[2]};
    const float along = dot3(step, axis);
    for (std::size_t k = 0; k < 3; ++k) step[k] -= along * axis[k];
    path += std::sqrt(dot3(step, step));
  }

  if (motion.duration > 0.0f) {
    summary.mean_planar_speed = path / motion.duration;
    summary.mean_turn_rate = summary.net_turn_radians / motion.duration;
  }
  return summary;
}

}  // namespace fo4::hkx
