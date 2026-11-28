#include "fo4/hkx/gltf_animation_save.hpp"

#include "fo4/hkx/animation.hpp"
#include "fo4/hkx/annotation_write.hpp"
#include "fo4/hkx/layout.hpp"
#include "fo4/hkx/packfile.hpp"
#include "fo4/hkx/write.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <exception>
#include <limits>
#include <numeric>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_set>
#include <utility>
#include <vector>

namespace fo4::hkx {
namespace {

constexpr std::size_t k_object_align = 16;
constexpr std::uint32_t k_array_flag = 0x80000000u;
constexpr std::size_t k_motion_sample_size = 16;

struct Refusal {
  HkxGltfSaveStatus status;
  std::string message;
};

[[noreturn]] void refuse(HkxGltfSaveStatus status, std::string message) { throw Refusal{status, std::move(message)}; }

const char* hkx_status_text(HkxStatus status) {
  static constexpr std::array<const char*, 10> names{
      "Ok",         "Truncated",       "BadMagic",    "UnsupportedLayout", "MissingSection",
      "BadSection", "FixupOutOfRange", "DuplicateFixup", "BadClassName",    "MissingObject",
  };
  const auto index = static_cast<std::size_t>(status);
  return index < names.size() ? names[index] : "Unknown";
}

std::string join(const std::vector<std::string>& items) {
  std::string out;
  for (std::size_t i = 0; i < items.size(); ++i) out += (i == 0 ? "" : ", ") + items[i];
  return out;
}

void put_int(std::vector<std::byte>& bytes, std::size_t at, std::int32_t value) {
  std::memcpy(bytes.data() + at, &value, sizeof value);
}

void put_float(std::vector<std::byte>& bytes, std::size_t at, float value) {
  const auto bits = std::bit_cast<std::uint32_t>(value);
  std::memcpy(bytes.data() + at, &bits, sizeof bits);
}

bool is_clip_class(std::string_view name) {
  return name == "hkaSplineCompressedAnimation" || name == "hkaInterleavedUncompressedAnimation" ||
         name == "hkaLosslessCompressedAnimation" || name == "hkaPredictiveCompressedAnimation" ||
         name == "hkaQuantizedAnimation";
}

struct ObjectInfo {
  std::uint32_t offset{0};
  std::uint32_t end{0};
  std::string class_name;
  std::uint32_t signature{0};
};

// Invariant: copies every name out of the views, because a resize of the section data would leave a view dangling.
std::vector<ObjectInfo> objects_of(const HkxPackfile& packfile) {
  const auto data_size = static_cast<std::uint32_t>(packfile.sections[packfile.data_section].data.size());
  std::vector<ObjectInfo> out;
  out.reserve(packfile.objects.size());
  for (std::size_t i = 0; i < packfile.objects.size(); ++i) {
    const HkxObjectView& object = packfile.objects[i];
    const std::uint32_t end = i + 1 < packfile.objects.size() ? packfile.objects[i + 1].offset : data_size;
    out.push_back({object.offset, end, std::string(object.class_name), object.signature});
  }
  return out;
}

std::uint32_t member_of(const HkxClassLayout& layout, std::string_view member) {
  const auto offset = find_member_offset(layout, member);
  if (!offset) {
    refuse(HkxGltfSaveStatus::WriteFailed, "the layout table has no " + std::string(member) + " member for " +
                                               std::string(layout.name) + ".");
  }
  return *offset;
}

const HkxClassLayout& layout_of(std::string_view name) {
  const HkxClassLayout* layout = find_layout(name);
  if (layout == nullptr) {
    refuse(HkxGltfSaveStatus::WriteFailed, "the layout table has no " + std::string(name) + " class.");
  }
  return *layout;
}

std::optional<std::uint32_t> pointer_from(const HkxPackfile& packfile, std::uint32_t source) {
  const HkxSection& data = packfile.sections[packfile.data_section];
  for (const HkxLocalFixup& fixup : data.local_fixups) {
    if (fixup.source == source) return fixup.destination;
  }
  for (const HkxRemoteFixup& fixup : data.global_fixups) {
    if (fixup.source == source && fixup.section == packfile.data_section) return fixup.destination;
  }
  return std::nullopt;
}

struct Span {
  std::uint32_t begin{0};
  std::uint32_t end{0};
};

bool inside(const std::vector<Span>& spans, std::uint32_t at) {
  return std::any_of(spans.begin(), spans.end(), [at](const Span& span) { return at >= span.begin && at < span.end; });
}

// Requires every pointer into and out of the span to have been checked, so only whole objects leave the data section.
void cut(HkxPackfile& packfile, const Span& span) {
  const std::uint32_t length = span.end - span.begin;
  const auto shift = [&span, length](std::uint32_t& value) {
    if (value >= span.end) value -= length;
  };
  HkxSection& data = packfile.sections[packfile.data_section];
  data.data.erase(data.data.begin() + span.begin, data.data.begin() + span.end);

  const auto leaves = [&span](std::uint32_t source) { return source >= span.begin && source < span.end; };
  std::erase_if(data.local_fixups, [&](const HkxLocalFixup& fixup) { return leaves(fixup.source); });
  for (HkxLocalFixup& fixup : data.local_fixups) {
    shift(fixup.source);
    shift(fixup.destination);
  }
  std::erase_if(data.virtual_fixups, [&](const HkxRemoteFixup& fixup) { return leaves(fixup.source); });
  for (HkxRemoteFixup& fixup : data.virtual_fixups) shift(fixup.source);

  for (std::size_t i = 0; i < packfile.sections.size(); ++i) {
    HkxSection& section = packfile.sections[i];
    if (i == packfile.data_section) {
      std::erase_if(section.global_fixups, [&](const HkxRemoteFixup& fixup) { return leaves(fixup.source); });
      for (HkxRemoteFixup& fixup : section.global_fixups) shift(fixup.source);
    }
    for (HkxRemoteFixup& fixup : section.global_fixups) {
      if (fixup.section == packfile.data_section) shift(fixup.destination);
    }
  }
}

struct Projection {
  HkxInterleavedAnimation animation;
  std::vector<std::int16_t> map;
};

double dot4(const std::array<float, 4>& a, const std::array<float, 4>& b) {
  return static_cast<double>(a[0]) * b[0] + static_cast<double>(a[1]) * b[1] + static_cast<double>(a[2]) * b[2] +
         static_cast<double>(a[3]) * b[3];
}

double angle_between(const std::array<float, 4>& a, const std::array<float, 4>& b) {
  const double la = std::sqrt(dot4(a, a));
  const double lb = std::sqrt(dot4(b, b));
  if (!(la > 0.0) || !(lb > 0.0)) return 0.0;
  const double d = std::min(1.0, std::fabs(dot4(a, b)) / (la * lb));
  return 2.0 * std::acos(d);
}

double gap3(const std::array<float, 3>& a, const std::array<float, 3>& b) {
  const double x = static_cast<double>(a[0]) - b[0];
  const double y = static_cast<double>(a[1]) - b[1];
  const double z = static_cast<double>(a[2]) - b[2];
  return std::sqrt(x * x + y * y + z * z);
}

bool moves(const std::array<float, 4>& sample) {
  return sample[0] != 0.0f || sample[1] != 0.0f || sample[2] != 0.0f || sample[3] != 0.0f;
}

void check_import(const HkxSkeleton& skeleton, const HkxGltfImport& imported) {
  const HkxInterleavedAnimation& source = imported.animation;
  const std::size_t bones = skeleton.bone_names.size();
  if (bones == 0 || source.track_count != bones) {
    refuse(HkxGltfSaveStatus::BadImport, "the import holds " + std::to_string(source.track_count) +
                                             " track(s) but the skeleton has " + std::to_string(bones) +
                                             " bone(s), so it was not made against this skeleton.");
  }
  const std::size_t samples = source.frame_count * bones;
  if (source.frame_count < 2 || source.translations.size() != samples || source.rotations.size() != samples ||
      source.scales.size() != samples || !std::isfinite(source.duration) || !(source.duration > 0.0f)) {
    refuse(HkxGltfSaveStatus::BadImport, "the import holds no usable frames, so there is no clip to write.");
  }
  if (imported.motion.samples.size() != source.frame_count) {
    refuse(HkxGltfSaveStatus::BadImport, "the import's root motion has " + std::to_string(imported.motion.samples.size()) +
                                             " sample(s) for " + std::to_string(source.frame_count) + " frame(s).");
  }
}

void project(const HkxSkeleton& skeleton, const HkxGltfImport& imported, const HkxAnimation& old_clip,
             const HkxAnimationBinding& binding, HkxGltfSaveReport& report, Projection& out) {
  const std::size_t bones = skeleton.bone_names.size();
  const std::size_t template_tracks = old_clip.tracks.size();
  if (template_tracks == 0) refuse(HkxGltfSaveStatus::TemplateTrackMap, "the template clip drives no tracks.");

  out.map = binding.transform_track_to_bone_indices;
  if (out.map.empty()) {
    if (template_tracks != bones) {
      refuse(HkxGltfSaveStatus::TemplateTrackMap,
             "the template clip drives " + std::to_string(template_tracks) + " track(s) and its binding names no bone for " +
                 "them, while the skeleton has " + std::to_string(bones) + " bone(s), so which bone each track drives cannot be read.");
    }
    out.map.resize(bones);
    std::iota(out.map.begin(), out.map.end(), std::int16_t{0});
  }
  if (out.map.size() != template_tracks) {
    refuse(HkxGltfSaveStatus::TemplateTrackMap, "the template binding maps " + std::to_string(out.map.size()) +
                                                    " track(s) but the clip has " + std::to_string(template_tracks) + ".");
  }
  std::vector<char> tracked(bones, 0);
  for (const std::int16_t bone : out.map) {
    if (bone < 0 || static_cast<std::size_t>(bone) >= bones) {
      refuse(HkxGltfSaveStatus::TemplateTrackMap,
             "the template drives bone index " + std::to_string(bone) + ", which the skeleton does not have.");
    }
    if (tracked[static_cast<std::size_t>(bone)] != 0) {
      refuse(HkxGltfSaveStatus::TemplateTrackMap,
             "the template drives bone '" + skeleton.bone_names[static_cast<std::size_t>(bone)] + "' with two tracks.");
    }
    tracked[static_cast<std::size_t>(bone)] = 1;
  }

  const std::unordered_set<std::string> animated(imported.animated.begin(), imported.animated.end());
  std::vector<std::string> untracked_animated;
  for (std::size_t bone = 0; bone < bones; ++bone) {
    if (tracked[bone] != 0) continue;
    report.untracked_bones.push_back(skeleton.bone_names[bone]);
    if (animated.count(skeleton.bone_names[bone]) != 0) untracked_animated.push_back(skeleton.bone_names[bone]);
  }
  if (!untracked_animated.empty()) {
    refuse(HkxGltfSaveStatus::UnmappedBone, "the glTF animates bone(s) the template has no track for: " +
                                                join(untracked_animated) +
                                                ". Refused rather than writing them onto no track.");
  }
  for (const std::int16_t bone : out.map) {
    const std::string& name = skeleton.bone_names[static_cast<std::size_t>(bone)];
    (animated.count(name) != 0 ? report.animated : report.reference_tracks).push_back(name);
  }

  const HkxInterleavedAnimation& source = imported.animation;
  const std::size_t tracks = out.map.size();
  out.animation.duration = source.duration;
  out.animation.frame_count = source.frame_count;
  out.animation.track_count = tracks;
  out.animation.translations.reserve(source.frame_count * tracks);
  out.animation.rotations.reserve(source.frame_count * tracks);
  out.animation.scales.reserve(source.frame_count * tracks);
  for (std::size_t frame = 0; frame < source.frame_count; ++frame) {
    for (const std::int16_t bone : out.map) {
      const std::size_t at = frame * bones + static_cast<std::size_t>(bone);
      out.animation.translations.push_back(source.translations[at]);
      out.animation.rotations.push_back(source.rotations[at]);
      out.animation.scales.push_back(source.scales[at]);
    }
  }
}

struct TemplateFrame {
  bool present{false};
  std::size_t index{0};
  std::vector<std::byte> bytes;
};

void write_frame(HkxPackfile& packfile, const ObjectInfo& clip, const ObjectInfo& frame, const HkxExtractedMotion& motion) {
  HkxSection& data = packfile.sections[packfile.data_section];
  const HkxClassLayout* layout = find_layout(frame.class_name, frame.signature);
  if (layout == nullptr) refuse(HkxGltfSaveStatus::UnsupportedMotion, "the frame class has no layout.");
  const std::uint32_t duration_member = member_of(*layout, "duration");
  const std::uint32_t samples_member = member_of(*layout, "referenceFrameSamples");
  const std::uint32_t extracted_member = member_of(layout_of("hkaAnimation"), "extractedMotion");

  put_float(data.data, frame.offset + duration_member, motion.duration);
  std::memset(data.data.data() + frame.offset + samples_member, 0, 8);
  while (data.data.size() % k_object_align != 0) data.data.push_back(std::byte{0xFF});
  const auto run = static_cast<std::uint32_t>(data.data.size());
  data.data.resize(data.data.size() + motion.samples.size() * k_motion_sample_size, std::byte{0});
  for (std::size_t i = 0; i < motion.samples.size(); ++i) {
    for (std::size_t axis = 0; axis < 4; ++axis) {
      put_float(data.data, run + i * k_motion_sample_size + axis * sizeof(float), motion.samples[i][axis]);
    }
  }
  put_int(data.data, frame.offset + samples_member + 8, static_cast<std::int32_t>(motion.samples.size()));
  put_int(data.data, frame.offset + samples_member + 12,
          static_cast<std::int32_t>(k_array_flag | static_cast<std::uint32_t>(motion.samples.size())));
  data.local_fixups.push_back({frame.offset + samples_member, run});
  data.global_fixups.push_back({clip.offset + extracted_member, static_cast<std::uint32_t>(packfile.data_section),
                                frame.offset});
}

// Requires the new clip and frame to be the last objects; the cut spans are the template clip and its frame.
void replace_template(HkxPackfile& packfile, const std::vector<ObjectInfo>& objects, std::size_t clip_index,
                      const TemplateFrame& frame, std::uint32_t fresh_at) {
  std::vector<Span> spans{{objects[clip_index].offset, objects[clip_index].end}};
  if (frame.present) spans.push_back({objects[frame.index].offset, objects[frame.index].end});
  const std::uint32_t old_at = objects[clip_index].offset;
  const auto data_index = static_cast<std::uint32_t>(packfile.data_section);

  std::size_t repointed = 0;
  HkxSection& data = packfile.sections[packfile.data_section];
  for (HkxLocalFixup& fixup : data.local_fixups) {
    if (fixup.destination != old_at || inside(spans, fixup.source)) continue;
    fixup.destination = fresh_at;
    ++repointed;
  }
  for (std::size_t i = 0; i < packfile.sections.size(); ++i) {
    for (HkxRemoteFixup& fixup : packfile.sections[i].global_fixups) {
      if (fixup.section != data_index || fixup.destination != old_at) continue;
      if (i == packfile.data_section && inside(spans, fixup.source)) continue;
      fixup.destination = fresh_at;
      ++repointed;
    }
  }
  if (repointed == 0) {
    refuse(HkxGltfSaveStatus::TemplateNoBinding,
           "nothing in the template points at its clip, so there is nothing to aim at the new one.");
  }

  for (const HkxLocalFixup& fixup : data.local_fixups) {
    if (inside(spans, fixup.source) != inside(spans, fixup.destination)) {
      refuse(HkxGltfSaveStatus::SharedData,
             "the template's clip shares data with another object (the pointer at data offset " +
                 std::to_string(fixup.source) + " crosses its edge), so it cannot be cut out.");
    }
  }
  for (std::size_t i = 0; i < packfile.sections.size(); ++i) {
    for (const HkxRemoteFixup& fixup : packfile.sections[i].global_fixups) {
      const bool from = i == packfile.data_section && inside(spans, fixup.source);
      const bool to = fixup.section == data_index && inside(spans, fixup.destination);
      if (from != to) {
        refuse(HkxGltfSaveStatus::SharedData,
               "the template's clip or root-motion frame is still pointed at from outside, or points outside, so it "
               "cannot be cut out.");
      }
    }
  }

  std::vector<Span> ordered = spans;
  std::sort(ordered.begin(), ordered.end(), [](const Span& a, const Span& b) { return a.begin > b.begin; });
  for (const Span& span : ordered) cut(packfile, span);

  std::sort(data.local_fixups.begin(), data.local_fixups.end(),
            [](const HkxLocalFixup& a, const HkxLocalFixup& b) { return a.source < b.source; });
  for (HkxSection& section : packfile.sections) {
    std::sort(section.global_fixups.begin(), section.global_fixups.end(),
              [](const HkxRemoteFixup& a, const HkxRemoteFixup& b) { return a.source < b.source; });
  }
}

void verify(const HkxPackfile& before, const std::vector<ObjectInfo>& old_objects, std::size_t clip_index,
            const TemplateFrame& frame, const std::vector<std::byte>& finished, const Projection& projection,
            const HkxGltfImport& imported, const HkxAnimationBinding& binding, const HkxExtractedMotion& old_motion,
            const HkxGltfSaveOptions& options, HkxGltfSaveReport& report) {
  const auto fail = [](const std::string& why) { refuse(HkxGltfSaveStatus::ReadBack, "the written file does not read back: " + why); };

  HkxPackfile after;
  if (const auto status = read_packfile(finished, after); status != HkxStatus::Ok) {
    fail(std::string("it is not a packfile (") + hkx_status_text(status) + ").");
  }
  const std::vector<ObjectInfo> now = objects_of(after);
  const std::size_t removed = frame.present ? 2 : 1;
  const std::size_t added = frame.present ? 2 : 1;
  if (now.size() != old_objects.size() - removed + added) fail("the object count is wrong.");

  std::size_t clips = 0;
  std::uint32_t clip_at = 0;
  for (const ObjectInfo& object : now) {
    if (!is_clip_class(object.class_name)) continue;
    ++clips;
    clip_at = object.offset;
  }
  if (clips != 1) fail("it holds " + std::to_string(clips) + " clips.");

  std::size_t kept = 0;
  for (std::size_t i = 0; i < old_objects.size(); ++i) {
    if (i == clip_index || (frame.present && i == frame.index)) continue;
    if (now[kept].class_name != old_objects[i].class_name) fail("object " + std::to_string(i) + " changed class.");
    const auto old_data = before.objects[i].data;
    const auto new_data = after.objects[kept].data;
    const std::size_t common = std::min(old_data.size(), new_data.size());
    if (!std::equal(old_data.begin(), old_data.begin() + static_cast<std::ptrdiff_t>(common), new_data.begin())) {
      fail("object " + std::to_string(i) + " (" + old_objects[i].class_name + ") changed.");
    }
    const auto tail_ok = [](std::span<const std::byte> bytes, std::size_t from) {
      return std::all_of(bytes.begin() + static_cast<std::ptrdiff_t>(from), bytes.end(),
                         [](std::byte b) { return b == std::byte{0} || b == std::byte{0xFF}; });
    };
    if (!tail_ok(old_data, common) || !tail_ok(new_data, common)) fail("object " + std::to_string(i) + " changed size.");
    ++kept;
  }

  HkxAnimation decoded;
  if (const auto status = read_animation(after, decoded); status != HkxStatus::Ok) {
    fail(std::string("the clip does not decode (") + hkx_status_text(status) + ").");
  }
  const std::size_t tracks = projection.map.size();
  const std::size_t frames = projection.animation.frame_count;
  if (decoded.kind != HkxAnimationKind::Spline || decoded.frame_count != frames || decoded.tracks.size() != tracks) {
    fail("the clip's kind, frame count or track count changed.");
  }

  HkxAnimationBinding rebound;
  if (const auto status = read_binding(after, rebound); status != HkxStatus::Ok) {
    fail(std::string("the binding does not read (") + hkx_status_text(status) + ").");
  }
  if (!rebound.animation_offset || *rebound.animation_offset != clip_at) fail("the binding does not name the new clip.");
  if (rebound.original_skeleton_name != binding.original_skeleton_name ||
      rebound.transform_track_to_bone_indices != binding.transform_track_to_bone_indices ||
      rebound.float_track_to_float_slot_indices != binding.float_track_to_float_slot_indices ||
      rebound.partition_indices != binding.partition_indices || rebound.blend_hint != binding.blend_hint) {
    fail("the binding changed beyond its animation pointer.");
  }
  report.binding_linked = true;

  std::vector<HkxAnimationAnnotation> notes;
  for (const HkxAnnotationTrack& track : decoded.annotation_tracks) {
    notes.insert(notes.end(), track.annotations.begin(), track.annotations.end());
  }
  if (notes.size() != imported.annotations.size()) fail("the annotation count changed.");
  for (std::size_t i = 0; i < notes.size(); ++i) {
    if (notes[i].time != imported.annotations[i].time || notes[i].text != imported.annotations[i].text) {
      fail("annotation " + std::to_string(i) + " changed.");
    }
  }

  HkxExtractedMotion motion;
  if (const auto status = read_extracted_motion(after, motion); status != HkxStatus::Ok) {
    fail(std::string("the root-motion frame does not read (") + hkx_status_text(status) + ").");
  }
  if (frame.present) {
    if (motion.kind != HkxMotionKind::Default || motion.up != old_motion.up || motion.forward != old_motion.forward ||
        motion.duration != imported.motion.duration || motion.samples != imported.motion.samples) {
      fail("the root motion changed.");
    }
  } else if (motion.kind != HkxMotionKind::None) {
    fail("a root-motion frame appeared.");
  }

  double position = 0.0, rotation = 0.0, scale = 0.0;
  for (std::size_t t = 0; t < tracks; ++t) {
    const HkxAnimationTrack& track = decoded.tracks[t];
    if (track.translations.size() != frames || track.rotations.size() != frames || track.scales.size() != frames) {
      fail("track " + std::to_string(t) + " has a different frame count.");
    }
    for (std::size_t f = 0; f < frames; ++f) {
      const std::size_t at = f * tracks + t;
      position = std::max(position, gap3(projection.animation.translations[at], track.translations[f]));
      rotation = std::max(rotation, angle_between(projection.animation.rotations[at], track.rotations[f]));
      scale = std::max(scale, gap3(projection.animation.scales[at], track.scales[f]));
    }
  }
  report.readback_position = static_cast<float>(position);
  report.readback_rotation = static_cast<float>(rotation);
  report.readback_scale = static_cast<float>(scale);

  // Constraint: quantise16 in spline_write.cpp rounds each position axis to one of 65535 steps over its own min..max, so a 3D gap below one step is expected.
  double widest = 0.0;
  for (std::size_t t = 0; t < tracks; ++t) {
    for (std::size_t axis = 0; axis < 3; ++axis) {
      double low = std::numeric_limits<double>::infinity();
      double high = -low;
      for (std::size_t f = 0; f < frames; ++f) {
        const double value = projection.animation.translations[f * tracks + t][axis];
        low = std::min(low, value);
        high = std::max(high, value);
      }
      widest = std::max(widest, high - low);
    }
  }
  const double position_budget = std::max(static_cast<double>(options.max_position_error), widest / 65535.0);
  if (!(position <= position_budget && rotation <= options.max_rotation_error && scale <= options.max_scale_error)) {
    refuse(HkxGltfSaveStatus::OverBudget,
           "the written clip reads back " + std::to_string(position) + " unit(s), " + std::to_string(rotation) +
               " radian(s) and " + std::to_string(scale) + " of scale away from the sampled pose, over the budget of " +
               std::to_string(position_budget) + ", " + std::to_string(options.max_rotation_error) + " and " +
               std::to_string(options.max_scale_error) + ".");
  }
}

void build(std::span<const std::byte> bytes, const HkxSkeleton& skeleton, const HkxGltfImport& imported,
           const HkxGltfSaveOptions& options, HkxGltfSave& out) {
  HkxGltfSaveReport& report = out.report;
  check_import(skeleton, imported);
  report.frames = imported.animation.frame_count;
  report.frame_rate = imported.frame_rate;
  report.duration = imported.animation.duration;
  report.unmapped_joints = imported.unmapped;
  report.rest_mismatch = imported.rest_mismatch;
  report.weight_channels = imported.weight_channels;
  report.non_bone_channels = imported.non_bone_channels;
  report.sampling_position = imported.sampling_position;
  report.sampling_rotation = imported.sampling_rotation;
  report.sampling_scale = imported.sampling_scale;

  HkxPackfile pack;
  if (const auto status = read_packfile(bytes, pack); status != HkxStatus::Ok) {
    refuse(HkxGltfSaveStatus::BadTemplate, std::string("the template is not a readable packfile (") +
                                               hkx_status_text(status) + ").");
  }
  const std::vector<ObjectInfo> objects = objects_of(pack);
  std::size_t clip_index = objects.size();
  std::size_t clip_count = 0;
  for (std::size_t i = 0; i < objects.size(); ++i) {
    if (!is_clip_class(objects[i].class_name)) continue;
    ++clip_count;
    clip_index = i;
  }
  if (clip_count != 1) {
    refuse(HkxGltfSaveStatus::TemplateNoClip, "the template holds " + std::to_string(clip_count) +
                                                  " clips, not one, so which one to replace would be a guess.");
  }
  HkxAnimation old_clip;
  if (const auto status = read_animation(pack, old_clip); status != HkxStatus::Ok) {
    refuse(HkxGltfSaveStatus::TemplateNoClip,
           std::string("the template clip does not decode (") + hkx_status_text(status) + ").");
  }
  if (old_clip.float_track_count != 0 || !old_clip.float_tracks.empty()) {
    refuse(HkxGltfSaveStatus::TemplateFloatTracks,
           "the template clip drives float tracks, which would be dropped. Refused rather than written short.");
  }
  HkxAnimationBinding binding;
  if (const auto status = read_binding(pack, binding); status != HkxStatus::Ok) {
    refuse(HkxGltfSaveStatus::TemplateNoBinding,
           std::string("the template needs exactly one binding (") + hkx_status_text(status) + ").");
  }
  if (!binding.float_track_to_float_slot_indices.empty()) {
    refuse(HkxGltfSaveStatus::TemplateFloatTracks,
           "the template binding maps float tracks, which would be dropped. Refused rather than written short.");
  }
  if (!binding.animation_offset || *binding.animation_offset != objects[clip_index].offset) {
    refuse(HkxGltfSaveStatus::TemplateNoBinding, "the template binding does not point at the template clip.");
  }
  report.binding_skeleton_name = binding.original_skeleton_name;
  report.skeleton_match = skeleton.name.empty() || binding.original_skeleton_name.empty()
                              ? "unverified"
                              : (skeleton.name == binding.original_skeleton_name ? "match" : "mismatch");
  if (report.skeleton_match == "mismatch") {
    refuse(HkxGltfSaveStatus::SkeletonMismatch, "the template binding is for skeleton '" + binding.original_skeleton_name +
                                                    "', not '" + skeleton.name + "'.");
  }

  Projection projection;
  project(skeleton, imported, old_clip, binding, report, projection);
  report.tracks = projection.map.size();

  std::size_t old_notes = 0;
  for (const HkxAnnotationTrack& track : old_clip.annotation_tracks) old_notes += track.annotations.size();
  report.annotations_replaced = old_notes;
  report.annotations_written = imported.annotations.size();
  if (old_notes > 0 && !options.replace_annotations) {
    refuse(HkxGltfSaveStatus::AnnotationsReplaced,
           "the template clip holds " + std::to_string(old_notes) +
               " annotation(s) that the glTF's would replace. Replace them on purpose or pick another template.");
  }

  HkxExtractedMotion old_motion;
  if (const auto status = read_extracted_motion(pack, old_motion); status != HkxStatus::Ok) {
    refuse(HkxGltfSaveStatus::UnsupportedMotion, std::string("the template's root-motion frame does not read (") +
                                                     hkx_status_text(status) + ").");
  }
  const bool imported_moves = std::any_of(imported.motion.samples.begin(), imported.motion.samples.end(), moves);
  TemplateFrame frame;
  if (old_motion.kind == HkxMotionKind::Unsupported) {
    refuse(HkxGltfSaveStatus::UnsupportedMotion,
           "the template's root-motion frame is " + old_motion.class_name +
               ", which has no verified conversion into a default frame. Refused rather than copying its bytes.");
  }
  if (old_motion.kind == HkxMotionKind::None) {
    if (imported_moves) {
      refuse(HkxGltfSaveStatus::UnsupportedMotion,
             "the template has no root-motion frame to carry the glTF's root travel. Pick a template with one.");
    }
  } else {
    if (old_motion.up != std::array<float, 3>{0.0f, 0.0f, 1.0f}) {
      refuse(HkxGltfSaveStatus::UnsupportedMotion,
             "the template's root-motion frame does not point up along Z, and the glTF's root turn is built around Z. "
             "Refused rather than turning the root about the template's axis.");
    }
    const bool old_moves = std::any_of(old_motion.samples.begin(), old_motion.samples.end(), moves);
    if (old_moves && !options.replace_motion) {
      refuse(HkxGltfSaveStatus::MotionReplaced,
             "the template's root-motion frame holds motion that the glTF's would replace. Replace it on purpose or "
             "pick another template.");
    }
    const std::uint32_t extracted_member = member_of(layout_of("hkaAnimation"), "extractedMotion");
    const auto target = pointer_from(pack, objects[clip_index].offset + extracted_member);
    if (!target) refuse(HkxGltfSaveStatus::UnsupportedMotion, "the template's root-motion pointer does not resolve.");
    for (std::size_t i = 0; i < objects.size(); ++i) {
      if (objects[i].offset != *target) continue;
      frame.present = true;
      frame.index = i;
    }
    if (!frame.present) refuse(HkxGltfSaveStatus::UnsupportedMotion, "the template's root-motion pointer names no object.");
    const HkxClassLayout* layout = find_layout(objects[frame.index].class_name, objects[frame.index].signature);
    if (layout == nullptr || pack.objects[frame.index].data.size() < layout->size) {
      refuse(HkxGltfSaveStatus::UnsupportedMotion, "the template's root-motion frame is cut short or has no layout.");
    }
    frame.bytes.assign(pack.objects[frame.index].data.begin(),
                       pack.objects[frame.index].data.begin() + static_cast<std::ptrdiff_t>(layout->size));
  }
  report.motion_replaced = frame.present;
  report.motion_samples = frame.present ? imported.motion.samples.size() : 0;

  HkxAppend written;
  if (const auto status = append_spline_animation(bytes, projection.animation, options.spline, written);
      status != HkxStatus::Ok) {
    refuse(HkxGltfSaveStatus::WriteFailed, std::string("the clip writer refused (") + hkx_status_text(status) + ").");
  }
  std::vector<std::byte> current = std::move(written.bytes);
  if (!imported.annotations.empty()) {
    const HkxAnnotationTrack track{"", imported.annotations};
    HkxAppend noted;
    if (const auto status = set_animation_annotations(current, written.object_index, std::span(&track, 1), noted);
        status != HkxStatus::Ok) {
      refuse(HkxGltfSaveStatus::WriteFailed, std::string("the annotation writer refused (") + hkx_status_text(status) +
                                                 "): a note time must be finite and a note text at most 255 bytes with no NUL.");
    }
    current = std::move(noted.bytes);
  }
  if (frame.present) {
    HkxAppend with_frame;
    if (const auto status = append_object(current, objects[frame.index].class_name, objects[frame.index].signature,
                                          frame.bytes, with_frame);
        status != HkxStatus::Ok) {
      refuse(HkxGltfSaveStatus::WriteFailed,
             std::string("the root-motion frame writer refused (") + hkx_status_text(status) + ").");
    }
    current = std::move(with_frame.bytes);
  }

  HkxPackfile edited;
  if (const auto status = read_packfile(current, edited); status != HkxStatus::Ok) {
    refuse(HkxGltfSaveStatus::WriteFailed, std::string("the intermediate file does not read (") +
                                               hkx_status_text(status) + ").");
  }
  const std::vector<ObjectInfo> grown = objects_of(edited);
  const std::size_t added = frame.present ? 2 : 1;
  if (grown.size() != objects.size() + added) {
    refuse(HkxGltfSaveStatus::WriteFailed, "the intermediate file holds an unexpected number of objects.");
  }
  for (std::size_t i = 0; i < objects.size(); ++i) {
    if (grown[i].offset != objects[i].offset || grown[i].class_name != objects[i].class_name) {
      refuse(HkxGltfSaveStatus::WriteFailed, "the intermediate file moved an existing object.");
    }
  }
  const ObjectInfo fresh = grown[objects.size()];
  if (fresh.class_name != "hkaSplineCompressedAnimation") {
    refuse(HkxGltfSaveStatus::WriteFailed, "the appended clip is not the last object.");
  }
  const ObjectInfo fresh_frame = frame.present ? grown[objects.size() + 1] : ObjectInfo{};

  if (frame.present) write_frame(edited, fresh, fresh_frame, imported.motion);
  replace_template(edited, objects, clip_index, frame, fresh.offset);

  std::vector<std::byte> finished;
  if (const auto status = rewrite_packfile(current, edited, finished); status != HkxStatus::Ok) {
    refuse(HkxGltfSaveStatus::WriteFailed, std::string("the packfile could not be laid out again (") +
                                               hkx_status_text(status) + ").");
  }

  verify(pack, objects, clip_index, frame, finished, projection, imported, binding, old_motion, options, report);
  out.bytes = std::move(finished);
}

}  // namespace

const char* hkx_gltf_save_status_name(HkxGltfSaveStatus status) {
  switch (status) {
    case HkxGltfSaveStatus::Ok: return "Ok";
    case HkxGltfSaveStatus::BadImport: return "BadImport";
    case HkxGltfSaveStatus::BadTemplate: return "BadTemplate";
    case HkxGltfSaveStatus::TemplateNoClip: return "TemplateNoClip";
    case HkxGltfSaveStatus::TemplateNoBinding: return "TemplateNoBinding";
    case HkxGltfSaveStatus::TemplateFloatTracks: return "TemplateFloatTracks";
    case HkxGltfSaveStatus::TemplateTrackMap: return "TemplateTrackMap";
    case HkxGltfSaveStatus::UnmappedBone: return "UnmappedBone";
    case HkxGltfSaveStatus::AnnotationsReplaced: return "AnnotationsReplaced";
    case HkxGltfSaveStatus::MotionReplaced: return "MotionReplaced";
    case HkxGltfSaveStatus::UnsupportedMotion: return "UnsupportedMotion";
    case HkxGltfSaveStatus::SharedData: return "SharedData";
    case HkxGltfSaveStatus::WriteFailed: return "WriteFailed";
    case HkxGltfSaveStatus::ReadBack: return "ReadBack";
    case HkxGltfSaveStatus::OverBudget: return "OverBudget";
    case HkxGltfSaveStatus::SkeletonMismatch: return "SkeletonMismatch";
  }
  return "Unknown";
}

HkxGltfSaveStatus save_gltf_animation(std::span<const std::byte> template_bytes, const HkxSkeleton& skeleton,
                                      const HkxGltfImport& imported, const HkxGltfSaveOptions& options,
                                      HkxGltfSave& out) {
  out = HkxGltfSave{};
  try {
    build(template_bytes, skeleton, imported, options, out);
    return HkxGltfSaveStatus::Ok;
  } catch (const Refusal& refusal) {
    out.bytes.clear();
    out.report.message = refusal.message;
    return refusal.status;
  } catch (const std::exception& error) {
    out.bytes.clear();
    out.report.message = error.what();
    return HkxGltfSaveStatus::WriteFailed;
  }
}

}  // namespace fo4::hkx
