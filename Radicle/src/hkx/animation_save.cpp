#include "fo4/hkx/animation_edit.hpp"

#include "fo4/hkx/annotation_write.hpp"
#include "fo4/hkx/layout.hpp"
#include "fo4/hkx/write.hpp"
#include "hkx_read_internal.hpp"
#include "write_internal.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace fo4::hkx {
namespace {

using detail::pointer_target;

constexpr std::size_t kObjectAlign = 16;
constexpr std::size_t kPointerSize = 8;
constexpr std::size_t kArrayCountAt = 8;
constexpr std::size_t kArrayCapacityAt = 12;
constexpr std::uint32_t kArrayFlag = 0x80000000u;
constexpr float kDurationSlack = 1e-3f;
constexpr std::size_t kMaxAnnotationTracks = 1u << 10;
constexpr std::size_t kMaxAnnotations = 1u << 16;
constexpr std::size_t kMaxAnnotationText = 255;

// Invariant: these limits must equal the ones set_animation_annotations enforces, so an over-limit edit is BadValue.
bool writable_text(const std::string& text) {
  return text.size() <= kMaxAnnotationText && text.find('\0') == std::string::npos;
}

bool is_clip_class(std::string_view name) {
  return name == "hkaSplineCompressedAnimation" || name == "hkaInterleavedUncompressedAnimation" ||
         name == "hkaLosslessCompressedAnimation" || name == "hkaPredictiveCompressedAnimation" ||
         name == "hkaQuantizedAnimation";
}

struct Described {
  HkxClipEdit clip;
  const HkxObjectView* object{nullptr};
  HkxAnimationBinding binding;
  std::optional<std::size_t> motion_offset;
};

// Constraint: the motion copy must keep only the sample array's fixup, so any other fixup or reference is refused.
HkxEditStatus locate_motion(const HkxPackfile& packfile, const HkxObjectView& clip, std::size_t& motion_offset) {
  const HkxSection& section = packfile.sections[packfile.data_section];
  const HkxClassLayout* base = find_layout("hkaAnimation");
  if (base == nullptr) return HkxEditStatus::UnsupportedMotion;
  const auto field = detail::member_field(section, *base, clip, "extractedMotion");
  if (!field) return HkxEditStatus::UnsupportedMotion;
  const auto target = pointer_target(packfile, section, *field);
  if (!target) return HkxEditStatus::UnsupportedMotion;
  const HkxObjectView* frame = nullptr;
  for (const HkxObjectView& object : packfile.objects) {
    if (static_cast<std::size_t>(object.offset) == *target) frame = &object;
  }
  if (frame == nullptr || frame->class_name != "hkaDefaultAnimatedReferenceFrame") return HkxEditStatus::UnsupportedMotion;
  const HkxClassLayout* layout = find_layout(frame->class_name, frame->signature);
  if (layout == nullptr || !detail::holds_layout(*frame, *layout)) return HkxEditStatus::UnsupportedMotion;
  const auto samples = find_member_offset(*layout, "referenceFrameSamples");
  if (!samples) return HkxEditStatus::UnsupportedMotion;
  const std::size_t begin = *target;
  const std::size_t end = begin + layout->size;
  for (const HkxLocalFixup& fixup : section.local_fixups) {
    if (fixup.source >= begin && fixup.source < end && fixup.source != begin + *samples) {
      return HkxEditStatus::UnsupportedMotion;
    }
  }
  for (const HkxRemoteFixup& fixup : section.global_fixups) {
    if (fixup.source >= begin && fixup.source < end) return HkxEditStatus::UnsupportedMotion;
  }
  std::size_t references = 0;
  for (const HkxLocalFixup& fixup : section.local_fixups) references += fixup.destination == begin ? 1 : 0;
  for (const HkxRemoteFixup& fixup : section.global_fixups) references += fixup.destination == begin ? 1 : 0;
  if (references != 1) return HkxEditStatus::UnsupportedMotion;
  motion_offset = begin;
  return HkxEditStatus::Ok;
}

HkxEditStatus describe(const HkxPackfile& packfile, Described& out) {
  if (packfile.pointer_size != kPointerSize) return HkxEditStatus::UnsupportedClip;
  if (read_animation(packfile, out.clip.animation) != HkxStatus::Ok) return HkxEditStatus::UnsupportedClip;
  const HkxAnimation& animation = out.clip.animation;
  if (animation.kind == HkxAnimationKind::View || animation.tracks.empty() || animation.frame_count == 0 ||
      animation.float_track_count != 0 || !animation.float_tracks.empty()) {
    return HkxEditStatus::UnsupportedClip;
  }
  for (const HkxAnimationTrack& track : animation.tracks) {
    if (track.translations.size() != animation.frame_count || track.rotations.size() != animation.frame_count ||
        track.scales.size() != animation.frame_count) {
      return HkxEditStatus::UnsupportedClip;
    }
  }
  for (const HkxObjectView& object : packfile.objects) {
    if (!is_clip_class(object.class_name)) continue;
    if (out.object != nullptr) return HkxEditStatus::UnsupportedClip;
    out.object = &object;
  }
  if (out.object == nullptr) return HkxEditStatus::UnsupportedClip;

  if (read_extracted_motion(packfile, out.clip.motion) != HkxStatus::Ok ||
      out.clip.motion.kind == HkxMotionKind::Unsupported) {
    return HkxEditStatus::UnsupportedMotion;
  }
  if (out.clip.motion.kind == HkxMotionKind::Default) {
    std::size_t offset = 0;
    if (const auto status = locate_motion(packfile, *out.object, offset); status != HkxEditStatus::Ok) return status;
    out.motion_offset = offset;
  }

  if (read_binding(packfile, out.binding) != HkxStatus::Ok) return HkxEditStatus::BindingMismatch;
  if (out.binding.animation_offset != static_cast<std::size_t>(out.object->offset) ||
      out.binding.transform_track_to_bone_indices.size() != animation.tracks.size()) {
    return HkxEditStatus::BindingMismatch;
  }
  return HkxEditStatus::Ok;
}

float vector_gap(const std::array<float, 3>& a, const std::array<float, 3>& b) {
  const double x = static_cast<double>(a[0]) - b[0];
  const double y = static_cast<double>(a[1]) - b[1];
  const double z = static_cast<double>(a[2]) - b[2];
  return static_cast<float>(std::sqrt(x * x + y * y + z * z));
}

float rotation_gap(const std::array<float, 4>& a, const std::array<float, 4>& b) {
  const std::array<float, 4> identity{0.0f, 0.0f, 0.0f, 1.0f};
  const auto ua = detail::normalised_quaternion(a).value_or(identity);
  const auto ub = detail::normalised_quaternion(b).value_or(identity);
  double dot = 0.0;
  for (std::size_t i = 0; i < 4; ++i) dot += static_cast<double>(ua[i]) * ub[i];
  return static_cast<float>(2.0 * std::acos(std::min(1.0, std::fabs(dot))));
}

bool finite_clip(const HkxAnimation& animation) {
  for (const HkxAnimationTrack& track : animation.tracks) {
    for (const auto& value : track.translations) {
      if (!std::isfinite(value[0]) || !std::isfinite(value[1]) || !std::isfinite(value[2])) return false;
    }
    for (const auto& value : track.rotations) {
      if (!std::isfinite(value[0]) || !std::isfinite(value[1]) || !std::isfinite(value[2]) || !std::isfinite(value[3])) {
        return false;
      }
    }
    for (const auto& value : track.scales) {
      if (!std::isfinite(value[0]) || !std::isfinite(value[1]) || !std::isfinite(value[2])) return false;
    }
  }
  return true;
}

HkxEditStatus validate_edit(const Described& original, const HkxClipEdit& edited) {
  const HkxAnimation& animation = edited.animation;
  if (animation.tracks.size() != original.clip.animation.tracks.size()) return HkxEditStatus::BadValue;
  if (animation.frame_count == 0 || !std::isfinite(animation.duration) ||
      (animation.frame_count > 1 && !(animation.duration > 0.0f)) || animation.duration < 0.0f) {
    return HkxEditStatus::BadValue;
  }
  for (const HkxAnimationTrack& track : animation.tracks) {
    if (track.translations.size() != animation.frame_count || track.rotations.size() != animation.frame_count ||
        track.scales.size() != animation.frame_count) {
      return HkxEditStatus::BadValue;
    }
  }
  if (!finite_clip(animation)) return HkxEditStatus::BadValue;
  if (animation.annotation_tracks.size() > kMaxAnnotationTracks) return HkxEditStatus::BadValue;
  std::size_t annotation_total = 0;
  for (const HkxAnnotationTrack& track : animation.annotation_tracks) {
    if (!writable_text(track.name)) return HkxEditStatus::BadValue;
    annotation_total += track.annotations.size();
    if (annotation_total > kMaxAnnotations) return HkxEditStatus::BadValue;
    for (const HkxAnimationAnnotation& note : track.annotations) {
      if (!std::isfinite(note.time) || !writable_text(note.text)) return HkxEditStatus::BadValue;
    }
  }
  if (edited.motion.kind != original.clip.motion.kind) return HkxEditStatus::BadValue;
  if (edited.motion.kind == HkxMotionKind::Default) {
    const HkxExtractedMotion& motion = edited.motion;
    for (const float value : motion.up) {
      if (!std::isfinite(value)) return HkxEditStatus::BadValue;
    }
    for (const float value : motion.forward) {
      if (!std::isfinite(value)) return HkxEditStatus::BadValue;
    }
    if (!std::isfinite(motion.duration)) return HkxEditStatus::BadValue;
    for (const auto& sample : motion.samples) {
      for (const float value : sample) {
        if (!std::isfinite(value)) return HkxEditStatus::BadValue;
      }
    }
  }
  return HkxEditStatus::Ok;
}

struct DataWriter {
  HkxSection& section;

  std::size_t size() const { return section.data.size(); }

  void align() {
    const std::size_t aligned = (section.data.size() + kObjectAlign - 1) / kObjectAlign * kObjectAlign;
    section.data.resize(aligned, std::byte{0xFF});
  }

  std::size_t reserve(std::size_t bytes) {
    align();
    const std::size_t at = section.data.size();
    section.data.resize(at + bytes, std::byte{0});
    return at;
  }

  void put_int(std::size_t at, std::int32_t value) { std::memcpy(section.data.data() + at, &value, sizeof value); }

  void put_float(std::size_t at, float value) {
    const auto bits = std::bit_cast<std::uint32_t>(value);
    std::memcpy(section.data.data() + at, &bits, sizeof bits);
  }

  void link(std::size_t field, std::size_t target) {
    section.local_fixups.push_back({static_cast<std::uint32_t>(field), static_cast<std::uint32_t>(target)});
  }

  void array(std::size_t field, std::size_t target, std::size_t count) {
    put_int(field + kArrayCountAt, static_cast<std::int32_t>(count));
    put_int(field + kArrayCapacityAt, static_cast<std::int32_t>(kArrayFlag | static_cast<std::uint32_t>(count)));
    if (count != 0) link(field, target);
  }
};

HkxEditStatus write_motion(HkxPackfile& after, DataWriter& writer, std::size_t clip_at, const HkxClassLayout& base,
                           std::size_t old_motion, const HkxExtractedMotion& motion) {
  const HkxClassLayout* layout = find_layout("hkaDefaultAnimatedReferenceFrame");
  const auto field = find_member_offset(base, "extractedMotion");
  if (layout == nullptr || !field) return HkxEditStatus::WriteFailed;
  const auto up_at = find_member_offset(*layout, "up");
  const auto forward_at = find_member_offset(*layout, "forward");
  const auto duration_at = find_member_offset(*layout, "duration");
  const auto samples_at = find_member_offset(*layout, "referenceFrameSamples");
  if (!up_at || !forward_at || !duration_at || !samples_at) return HkxEditStatus::WriteFailed;

  const std::vector<HkxRemoteFixup>& virtuals = writer.section.virtual_fixups;
  const auto name = std::find_if(virtuals.begin(), virtuals.end(),
                                 [&](const HkxRemoteFixup& fixup) { return fixup.source == old_motion; });
  if (name == virtuals.end()) return HkxEditStatus::WriteFailed;
  const HkxRemoteFixup class_name = *name;

  std::vector<std::byte> copy(writer.section.data.begin() + static_cast<std::ptrdiff_t>(old_motion),
                              writer.section.data.begin() + static_cast<std::ptrdiff_t>(old_motion + layout->size));
  const std::size_t motion_at = writer.reserve(layout->size);
  std::copy(copy.begin(), copy.end(), writer.section.data.begin() + static_cast<std::ptrdiff_t>(motion_at));
  for (std::size_t axis = 0; axis < 3; ++axis) {
    writer.put_float(motion_at + *up_at + 4 * axis, motion.up[axis]);
    writer.put_float(motion_at + *forward_at + 4 * axis, motion.forward[axis]);
  }
  writer.put_float(motion_at + *duration_at, motion.duration);
  const std::size_t samples_run = motion.samples.empty() ? 0 : writer.reserve(motion.samples.size() * 16);
  writer.array(motion_at + *samples_at, samples_run, motion.samples.size());
  for (std::size_t i = 0; i < motion.samples.size(); ++i) {
    for (std::size_t lane = 0; lane < 4; ++lane) writer.put_float(samples_run + i * 16 + 4 * lane, motion.samples[i][lane]);
  }
  writer.section.virtual_fixups.push_back({static_cast<std::uint32_t>(motion_at), class_name.section, class_name.destination});
  writer.section.global_fixups.push_back({static_cast<std::uint32_t>(clip_at + *field),
                                          static_cast<std::uint32_t>(after.data_section),
                                          static_cast<std::uint32_t>(motion_at)});
  return HkxEditStatus::Ok;
}

HkxEditStatus verify_saved(const HkxPackfile& before, const Described& original, const HkxClipEdit& edited,
                           const HkxSaveOptions& options, std::span<const std::byte> written, HkxEditReport* report) {
  HkxPackfile check;
  if (read_packfile(written, check) != HkxStatus::Ok) return HkxEditStatus::WriteFailed;
  if (check.objects.size() != before.objects.size()) return HkxEditStatus::WriteFailed;
  const auto& old_data = before.sections[before.data_section].data;
  const auto& new_data = check.sections[check.data_section].data;
  if (new_data.size() < old_data.size() || !std::equal(old_data.begin(), old_data.end(), new_data.begin())) {
    return HkxEditStatus::WriteFailed;
  }
  for (const HkxObjectView& object : before.objects) {
    const std::size_t offset = object.offset;
    if (offset == original.object->offset || (original.motion_offset && offset == *original.motion_offset)) continue;
    const bool kept = std::any_of(check.objects.begin(), check.objects.end(), [&](const HkxObjectView& other) {
      return other.offset == object.offset && other.class_name == object.class_name && other.signature == object.signature;
    });
    if (!kept) return HkxEditStatus::WriteFailed;
  }

  HkxAnimation saved;
  if (read_animation(check, saved) != HkxStatus::Ok) return HkxEditStatus::WriteFailed;
  const HkxAnimation& wanted = edited.animation;
  if (saved.kind == HkxAnimationKind::View || saved.frame_count != wanted.frame_count ||
      saved.tracks.size() != wanted.tracks.size() || saved.float_track_count != 0 ||
      std::fabs(saved.duration - wanted.duration) > kDurationSlack * std::max(1.0f, wanted.duration)) {
    return HkxEditStatus::WriteFailed;
  }
  float position = 0.0f;
  float rotation = 0.0f;
  float scale = 0.0f;
  for (std::size_t t = 0; t < wanted.tracks.size(); ++t) {
    const HkxAnimationTrack& a = wanted.tracks[t];
    const HkxAnimationTrack& b = saved.tracks[t];
    if (b.translations.size() != wanted.frame_count || b.rotations.size() != wanted.frame_count ||
        b.scales.size() != wanted.frame_count) {
      return HkxEditStatus::WriteFailed;
    }
    for (std::size_t f = 0; f < wanted.frame_count; ++f) {
      position = std::max(position, vector_gap(a.translations[f], b.translations[f]));
      rotation = std::max(rotation, rotation_gap(a.rotations[f], b.rotations[f]));
      scale = std::max(scale, vector_gap(a.scales[f], b.scales[f]));
    }
  }
  if (report != nullptr) {
    report->position_error = position;
    report->rotation_error = rotation;
    report->scale_error = scale;
  }
  if (!(position <= options.max_position_error) || !(rotation <= options.max_rotation_error) ||
      !(scale <= options.max_scale_error)) {
    return HkxEditStatus::BudgetExceeded;
  }

  if (saved.annotation_tracks.size() != wanted.annotation_tracks.size()) return HkxEditStatus::WriteFailed;
  for (std::size_t i = 0; i < wanted.annotation_tracks.size(); ++i) {
    const HkxAnnotationTrack& a = wanted.annotation_tracks[i];
    const HkxAnnotationTrack& b = saved.annotation_tracks[i];
    if (a.name != b.name || a.annotations.size() != b.annotations.size()) return HkxEditStatus::WriteFailed;
    for (std::size_t j = 0; j < a.annotations.size(); ++j) {
      if (a.annotations[j].time != b.annotations[j].time || a.annotations[j].text != b.annotations[j].text) {
        return HkxEditStatus::WriteFailed;
      }
    }
  }

  HkxExtractedMotion motion;
  if (read_extracted_motion(check, motion) != HkxStatus::Ok || motion.kind != edited.motion.kind) {
    return HkxEditStatus::WriteFailed;
  }
  if (motion.kind == HkxMotionKind::Default &&
      (motion.up != edited.motion.up || motion.forward != edited.motion.forward ||
       motion.duration != edited.motion.duration || motion.samples != edited.motion.samples)) {
    return HkxEditStatus::WriteFailed;
  }

  HkxAnimationBinding binding;
  if (read_binding(check, binding) != HkxStatus::Ok) return HkxEditStatus::WriteFailed;
  const HkxAnimationBinding& was = original.binding;
  if (binding.original_skeleton_name != was.original_skeleton_name ||
      binding.transform_track_to_bone_indices != was.transform_track_to_bone_indices ||
      binding.float_track_to_float_slot_indices != was.float_track_to_float_slot_indices ||
      binding.partition_indices != was.partition_indices || binding.blend_hint != was.blend_hint ||
      !binding.animation_offset) {
    return HkxEditStatus::WriteFailed;
  }
  const auto clip_at = std::find_if(check.objects.begin(), check.objects.end(), [&](const HkxObjectView& object) {
    return is_clip_class(object.class_name);
  });
  if (clip_at == check.objects.end() || *binding.animation_offset != clip_at->offset) return HkxEditStatus::WriteFailed;
  return HkxEditStatus::Ok;
}

}  // namespace

HkxEditStatus open_clip_edit(std::span<const std::byte> source, HkxClipEdit& out) {
  HkxPackfile packfile;
  if (read_packfile(source, packfile) != HkxStatus::Ok) return HkxEditStatus::BadPackfile;
  Described described;
  if (const auto status = describe(packfile, described); status != HkxEditStatus::Ok) return status;
  out = std::move(described.clip);
  return HkxEditStatus::Ok;
}

HkxEditStatus save_clip_edit(std::span<const std::byte> source, const HkxClipEdit& edited,
                             const HkxSaveOptions& options, std::vector<std::byte>& out, HkxEditReport* report) {
  out.clear();
  HkxPackfile before;
  if (read_packfile(source, before) != HkxStatus::Ok) return HkxEditStatus::BadPackfile;
  Described original;
  if (const auto status = describe(before, original); status != HkxEditStatus::Ok) return status;
  if (const auto status = validate_edit(original, edited); status != HkxEditStatus::Ok) return status;
  const HkxClassLayout* base = find_layout("hkaAnimation");
  if (base == nullptr) return HkxEditStatus::WriteFailed;

  HkxInterleavedAnimation samples;
  samples.duration = edited.animation.duration;
  samples.frame_count = edited.animation.frame_count;
  samples.track_count = edited.animation.tracks.size();
  for (std::size_t f = 0; f < samples.frame_count; ++f) {
    for (const HkxAnimationTrack& track : edited.animation.tracks) {
      samples.translations.push_back(track.translations[f]);
      samples.rotations.push_back(track.rotations[f]);
      samples.scales.push_back(track.scales[f]);
    }
  }
  HkxAppend appended;
  const HkxStatus appended_status = options.spline
                                        ? append_spline_animation(source, samples, options.spline_options, appended)
                                        : append_interleaved_animation(source, samples, appended);
  if (appended_status != HkxStatus::Ok) return HkxEditStatus::WriteFailed;

  HkxAppend annotated;
  if (const auto status = set_animation_annotations(appended.bytes, appended.object_index,
                                                    edited.animation.annotation_tracks, annotated);
      status != HkxStatus::Ok) {
    return status == HkxStatus::BadSection ? HkxEditStatus::BadValue : HkxEditStatus::WriteFailed;
  }

  HkxPackfile after;
  if (read_packfile(annotated.bytes, after) != HkxStatus::Ok) return HkxEditStatus::WriteFailed;
  if (after.objects.size() != before.objects.size() + 1 || !is_clip_class(after.objects.back().class_name)) {
    return HkxEditStatus::WriteFailed;
  }
  const std::size_t new_clip = after.objects.back().offset;
  const std::size_t old_clip = original.object->offset;
  const std::size_t data_index = after.data_section;

  HkxSection& data = after.sections[data_index];
  DataWriter writer{data};
  if (original.motion_offset) {
    if (const auto status = write_motion(after, writer, new_clip, *base, *original.motion_offset, edited.motion);
        status != HkxEditStatus::Ok) {
      return status;
    }
  }

  std::size_t repointed = 0;
  for (HkxRemoteFixup& fixup : data.global_fixups) {
    if (fixup.section == data_index && fixup.destination == old_clip) {
      fixup.destination = static_cast<std::uint32_t>(new_clip);
      ++repointed;
    }
  }
  if (repointed == 0) return HkxEditStatus::BindingMismatch;
  std::erase_if(data.virtual_fixups, [&](const HkxRemoteFixup& fixup) {
    return fixup.source == old_clip || (original.motion_offset && fixup.source == *original.motion_offset);
  });
  std::sort(data.local_fixups.begin(), data.local_fixups.end(),
            [](const HkxLocalFixup& a, const HkxLocalFixup& b) { return a.source < b.source; });
  std::sort(data.global_fixups.begin(), data.global_fixups.end(),
            [](const HkxRemoteFixup& a, const HkxRemoteFixup& b) { return a.source < b.source; });

  std::vector<std::byte> bytes;
  if (rewrite_packfile(annotated.bytes, after, bytes) != HkxStatus::Ok) return HkxEditStatus::WriteFailed;
  if (const auto status = verify_saved(before, original, edited, options, bytes, report); status != HkxEditStatus::Ok) {
    return status;
  }
  out = std::move(bytes);
  return HkxEditStatus::Ok;
}

}  // namespace fo4::hkx
