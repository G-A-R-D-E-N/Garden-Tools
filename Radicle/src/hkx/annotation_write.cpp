#include "fo4/hkx/annotation_write.hpp"

#include "fo4/hkx/layout.hpp"

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace fo4::hkx {
namespace {

constexpr std::size_t kObjectAlign = 16;
constexpr std::size_t kMaxTracks = 1u << 10;
constexpr std::size_t kMaxAnnotations = 1u << 16;
// Invariant: read_animation reads at most 255 characters per string.
constexpr std::size_t kMaxStringLength = 255;
constexpr std::size_t kPointerBytes = 8;
constexpr std::uint32_t kArrayFlag = 0x80000000u;

void put_int(std::vector<std::byte>& bytes, std::size_t at, std::int32_t value) {
  std::memcpy(bytes.data() + at, &value, sizeof value);
}

void put_float(std::vector<std::byte>& bytes, std::size_t at, float value) {
  const auto bits = std::bit_cast<std::uint32_t>(value);
  std::memcpy(bytes.data() + at, &bits, sizeof bits);
}

void pad_to_object_align(std::vector<std::byte>& bytes) {
  const auto target = (bytes.size() + kObjectAlign - 1) / kObjectAlign * kObjectAlign;
  if (target > bytes.size()) bytes.resize(target, std::byte{0xFF});
}

std::size_t append_text(std::vector<std::byte>& bytes, const std::string& text) {
  const auto at = bytes.size();
  for (const char c : text) bytes.push_back(static_cast<std::byte>(c));
  bytes.push_back(std::byte{0});
  return at;
}

bool text_is_writable(const std::string& text) {
  return text.size() <= kMaxStringLength && text.find('\0') == std::string::npos;
}

}  // namespace

HkxStatus set_animation_annotations(std::span<const std::byte> source, std::size_t object_index,
                                    std::span<const HkxAnnotationTrack> tracks, HkxAppend& out) {
  out = HkxAppend{};
  HkxPackfile packfile;
  if (const auto status = read_packfile(source, packfile); status != HkxStatus::Ok) return status;
  if (object_index >= packfile.objects.size()) return HkxStatus::MissingObject;
  const HkxObjectView& object = packfile.objects[object_index];
  if (!is_animation_class(object.class_name)) return HkxStatus::UnsupportedLayout;

  const HkxClassLayout* animation_layout = find_layout(object.class_name, object.signature);
  const HkxClassLayout* base = find_layout("hkaAnimation");
  const HkxClassLayout* track_layout = find_layout("hkaAnnotationTrack");
  const HkxClassLayout* note_layout = find_layout("hkaAnnotationTrackAnnotation");
  if (animation_layout == nullptr || base == nullptr || track_layout == nullptr || note_layout == nullptr) {
    return HkxStatus::UnsupportedLayout;
  }
  const auto tracks_member = find_member_offset(*base, "annotationTracks");
  const auto name_member = find_member_offset(*track_layout, "trackName");
  const auto events_member = find_member_offset(*track_layout, "annotations");
  const auto time_member = find_member_offset(*note_layout, "time");
  const auto text_member = find_member_offset(*note_layout, "text");
  if (!tracks_member || !name_member || !events_member || !time_member || !text_member) {
    return HkxStatus::UnsupportedLayout;
  }
  if (object.data.size() < animation_layout->size) return HkxStatus::UnsupportedLayout;

  HkxSection& data = packfile.sections[packfile.data_section];
  const std::size_t field = object.offset + static_cast<std::size_t>(*tracks_member);
  if (field + kPointerBytes + 8 > data.data.size()) return HkxStatus::FixupOutOfRange;
  std::int32_t existing = 0;
  std::memcpy(&existing, data.data.data() + field + kPointerBytes, sizeof existing);
  if (existing < 0) return HkxStatus::BadSection;
  const auto has_fixup_at_field =
      std::any_of(data.local_fixups.begin(), data.local_fixups.end(),
                  [field](const HkxLocalFixup& fixup) { return fixup.source == field; }) ||
      std::any_of(data.global_fixups.begin(), data.global_fixups.end(),
                  [field](const HkxRemoteFixup& fixup) { return fixup.source == field; });
  if (existing != 0 || has_fixup_at_field) return HkxStatus::UnsupportedLayout;

  if (tracks.size() > kMaxTracks) return HkxStatus::BadSection;
  std::size_t total = 0;
  for (const HkxAnnotationTrack& track : tracks) {
    if (!text_is_writable(track.name)) return HkxStatus::BadSection;
    total += track.annotations.size();
    if (total > kMaxAnnotations) return HkxStatus::BadSection;
    for (const HkxAnimationAnnotation& note : track.annotations) {
      if (!std::isfinite(note.time) || !text_is_writable(note.text)) return HkxStatus::BadSection;
    }
  }
  out.object_index = object_index;
  if (tracks.empty()) {
    out.bytes.assign(source.begin(), source.end());
    return HkxStatus::Ok;
  }

  const std::size_t track_size = track_layout->size;
  const std::size_t note_size = note_layout->size;
  pad_to_object_align(data.data);
  const std::size_t tracks_run = data.data.size();
  data.data.resize(tracks_run + tracks.size() * track_size, std::byte{0});
  std::vector<std::size_t> note_runs(tracks.size(), 0);
  for (std::size_t i = 0; i < tracks.size(); ++i) {
    if (tracks[i].annotations.empty()) continue;
    pad_to_object_align(data.data);
    note_runs[i] = data.data.size();
    data.data.resize(note_runs[i] + tracks[i].annotations.size() * note_size, std::byte{0});
  }

  for (std::size_t i = 0; i < tracks.size(); ++i) {
    const std::size_t track_at = tracks_run + i * track_size;
    if (!tracks[i].name.empty()) {
      const auto name_at = append_text(data.data, tracks[i].name);
      data.local_fixups.push_back({static_cast<std::uint32_t>(track_at + *name_member),
                                   static_cast<std::uint32_t>(name_at)});
    }
    const std::size_t events_field = track_at + *events_member;
    const auto count = static_cast<std::uint32_t>(tracks[i].annotations.size());
    put_int(data.data, events_field + kPointerBytes, static_cast<std::int32_t>(count));
    put_int(data.data, events_field + kPointerBytes + 4, static_cast<std::int32_t>(kArrayFlag | count));
    if (count == 0) continue;
    data.local_fixups.push_back({static_cast<std::uint32_t>(events_field), static_cast<std::uint32_t>(note_runs[i])});
    for (std::size_t j = 0; j < tracks[i].annotations.size(); ++j) {
      const std::size_t note_at = note_runs[i] + j * note_size;
      put_float(data.data, note_at + *time_member, tracks[i].annotations[j].time);
      if (tracks[i].annotations[j].text.empty()) continue;
      const auto text_at = append_text(data.data, tracks[i].annotations[j].text);
      data.local_fixups.push_back({static_cast<std::uint32_t>(note_at + *text_member),
                                   static_cast<std::uint32_t>(text_at)});
    }
  }

  const auto track_count = static_cast<std::uint32_t>(tracks.size());
  put_int(data.data, field + kPointerBytes, static_cast<std::int32_t>(track_count));
  put_int(data.data, field + kPointerBytes + 4, static_cast<std::int32_t>(kArrayFlag | track_count));
  data.local_fixups.push_back({static_cast<std::uint32_t>(field), static_cast<std::uint32_t>(tracks_run)});
  std::sort(data.local_fixups.begin(), data.local_fixups.end(),
            [](const HkxLocalFixup& a, const HkxLocalFixup& b) { return a.source < b.source; });

  const std::size_t object_count = packfile.objects.size();
  if (const auto status = rewrite_packfile(source, packfile, out.bytes); status != HkxStatus::Ok) {
    out = HkxAppend{};
    return status;
  }
  HkxPackfile check;
  if (read_packfile(out.bytes, check) != HkxStatus::Ok || check.objects.size() != object_count) {
    out = HkxAppend{};
    return HkxStatus::BadSection;
  }
  out.object_index = object_index;
  return HkxStatus::Ok;
}

}  // namespace fo4::hkx
