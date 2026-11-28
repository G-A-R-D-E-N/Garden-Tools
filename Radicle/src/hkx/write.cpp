#include "fo4/hkx/write.hpp"
#include "fo4/binary/raw.hpp"

#include "fo4/hkx/layout.hpp"
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

using fo4::binary::raw::read_i32;
using fo4::binary::raw::read_u16;
using fo4::binary::raw::read_u32;
using fo4::binary::raw::write_u32;

using detail::normalised_quaternion;

constexpr std::size_t kHeaderSize = 0x40;
constexpr std::size_t kSectionHeaderSize = 0x40;
constexpr std::size_t kObjectAlign = 16;
constexpr std::size_t kSectionAlign = 16;
constexpr std::size_t kLocalEntry = 8;
constexpr std::size_t kSectionStartAt = 0x14;
constexpr std::size_t kSectionMarksAt = 0x18;
constexpr std::size_t kSectionTableOffsetAt = 0x3E;
constexpr std::size_t kMaxAppendGrowth = 15;
constexpr std::size_t kQsTransformSize = 48;
constexpr std::size_t kQsRotationOffset = 16;
constexpr std::size_t kQsScaleOffset = 32;
constexpr std::size_t kMaxAnimationSamples = 1u << 20;
constexpr std::int32_t kInterleavedType = 1;
constexpr std::int32_t kSplineType = 3;
constexpr std::uint32_t kArrayFlag = 0x80000000u;

void put_int(std::vector<std::byte>& bytes, std::size_t at, std::int32_t value) {
  std::memcpy(bytes.data() + at, &value, sizeof value);
}

void put_float(std::vector<std::byte>& bytes, std::size_t at, float value) {
  const auto bits = std::bit_cast<std::uint32_t>(value);
  std::memcpy(bytes.data() + at, &bits, sizeof bits);
}

std::size_t align_to(std::size_t value, std::size_t alignment) {
  return alignment <= 1 ? value : (value + alignment - 1) / alignment * alignment;
}

void append_u32(std::vector<std::byte>& out, std::uint32_t value) {
  const auto at = out.size();
  out.resize(at + sizeof value);
  write_u32(out, at, value);
}

// Invariant: every table gap is 0xFF filler, because the engine skips filler words instead of counting entries.
void pad_filler(std::vector<std::byte>& out, std::size_t target) {
  if (target > out.size()) out.resize(target, std::byte{0xFF});
}

// Requires a class-names section; the trimmed length ends where its 0xFF filler begins.
std::size_t names_end(const HkxSection& names) {
  std::size_t end = names.data.size();
  while (end > 0 && names.data[end - 1] == std::byte{0xFF}) --end;
  return end;
}

// Invariant: a class-name entry is [u32 signature][0x09][NUL-terminated name], so the scan matches on 0x09 and NUL.
std::optional<std::size_t> find_class_name(const HkxSection& names, std::string_view name) {
  const auto end = names_end(names);
  // Constraint: an entry's name starts five bytes past its signature, so no earlier position can be one.
  for (std::size_t at = 5; at + name.size() + 1 <= end; ++at) {
    if (names.data[at - 1] != std::byte{0x09}) continue;
    if (names.data[at + name.size()] != std::byte{0}) continue;
    bool same = true;
    for (std::size_t i = 0; i < name.size() && same; ++i) {
      same = names.data[at + i] == static_cast<std::byte>(name[i]);
    }
    if (same) return at;
  }
  return std::nullopt;
}

// Requires a class-names section the caller may grow; returns the offset of the name string it now holds.
std::optional<std::size_t> ensure_class_name(HkxSection& names, std::string_view class_name, std::uint32_t signature) {
  if (const auto found = find_class_name(names, class_name)) {
    if (read_u32(names.data, *found - 5) != signature) return std::nullopt;
    return found;
  }
  if (class_name.empty() || class_name.size() > 0xFFU) return std::nullopt;
  names.data.resize(names_end(names));
  append_u32(names.data, signature);
  names.data.push_back(std::byte{0x09});
  const auto name_at = names.data.size();
  for (const char c : class_name) names.data.push_back(static_cast<std::byte>(c));
  names.data.push_back(std::byte{0});
  return name_at;
}

struct Marks {
  std::array<std::size_t, 6> value{};
};

// Requires a source whose section table the reader accepted; the six marks are relative to the section start.
std::optional<Marks> section_marks(std::span<const std::byte> source, std::size_t index) {
  const auto table_at = kHeaderSize + read_u16(source, kSectionTableOffsetAt);
  const auto header = table_at + index * kSectionHeaderSize;
  if (header + kSectionHeaderSize > source.size()) return std::nullopt;
  Marks marks;
  for (std::size_t i = 0; i < marks.value.size(); ++i) {
    marks.value[i] = read_u32(source, header + kSectionMarksAt + 4 * i);
  }
  return marks;
}

// Invariant: the source carries no exports or imports, which this writer cannot relocate, so marks 3 to 5 are equal.
bool has_exports_or_imports(std::span<const std::byte> source, std::size_t index) {
  const auto marks = section_marks(source, index);
  if (!marks) return true;
  return marks->value[3] != marks->value[4] || marks->value[4] != marks->value[5];
}

struct Payload {
  std::vector<std::byte> bytes;
  Marks marks;
};

// Invariant: data, local, global and virtual tables follow, each padded to 16 with 0xFF, and no exports or imports.
Payload payload_of(const HkxSection& section) {
  Payload payload;
  payload.bytes = section.data;
  pad_filler(payload.bytes, align_to(payload.bytes.size(), kSectionAlign));
  payload.marks.value[0] = payload.bytes.size();
  for (const auto& fixup : section.local_fixups) {
    append_u32(payload.bytes, fixup.source);
    append_u32(payload.bytes, fixup.destination);
  }
  pad_filler(payload.bytes, align_to(payload.bytes.size(), kSectionAlign));
  payload.marks.value[1] = payload.bytes.size();
  for (const auto& fixup : section.global_fixups) {
    append_u32(payload.bytes, fixup.source);
    append_u32(payload.bytes, fixup.section);
    append_u32(payload.bytes, fixup.destination);
  }
  pad_filler(payload.bytes, align_to(payload.bytes.size(), kSectionAlign));
  payload.marks.value[2] = payload.bytes.size();
  std::vector<HkxRemoteFixup> virtuals = section.virtual_fixups;
  std::sort(virtuals.begin(), virtuals.end(),
            [](const HkxRemoteFixup& a, const HkxRemoteFixup& b) { return a.source < b.source; });
  for (const auto& fixup : virtuals) {
    append_u32(payload.bytes, fixup.source);
    append_u32(payload.bytes, fixup.section);
    append_u32(payload.bytes, fixup.destination);
  }
  pad_filler(payload.bytes, align_to(payload.bytes.size(), kSectionAlign));
  payload.marks.value[3] = payload.bytes.size();
  payload.marks.value[4] = payload.bytes.size();
  payload.marks.value[5] = payload.bytes.size();
  return payload;
}

// Invariant: a written container copies each source section header and patches only its start and its six marks.
HkxStatus finish_rebuild(std::span<const std::byte> source, const HkxPackfile& packfile, std::size_t table_at,
                         HkxAppend& out) {
  for (std::size_t i = 0; i < packfile.sections.size(); ++i) {
    if (has_exports_or_imports(source, i)) return HkxStatus::UnsupportedLayout;
  }

  std::vector<Payload> payloads(packfile.sections.size());
  for (std::size_t i = 0; i < packfile.sections.size(); ++i) payloads[i] = payload_of(packfile.sections[i]);

  std::vector<std::byte> bytes(source.begin(), source.begin() + static_cast<std::ptrdiff_t>(table_at));
  bytes.resize(table_at + payloads.size() * kSectionHeaderSize, std::byte{0});
  for (std::size_t i = 0; i < packfile.sections.size(); ++i) {
    const auto header = table_at + i * kSectionHeaderSize;
    std::copy(source.begin() + static_cast<std::ptrdiff_t>(header),
              source.begin() + static_cast<std::ptrdiff_t>(header + kSectionHeaderSize),
              bytes.begin() + static_cast<std::ptrdiff_t>(header));
  }

  std::size_t cursor = align_to(bytes.size(), kSectionAlign);
  for (auto& payload : payloads) {
    cursor += payload.bytes.size();
    cursor = align_to(cursor, kSectionAlign);
  }
  bytes.resize(cursor, std::byte{0});

  std::size_t at = align_to(table_at + payloads.size() * kSectionHeaderSize, kSectionAlign);
  for (std::size_t i = 0; i < payloads.size(); ++i) {
    const auto header = table_at + i * kSectionHeaderSize;
    write_u32(bytes, header + kSectionStartAt, static_cast<std::uint32_t>(at));
    for (std::size_t mark = 0; mark < payloads[i].marks.value.size(); ++mark) {
      write_u32(bytes, header + kSectionMarksAt + 4 * mark, static_cast<std::uint32_t>(payloads[i].marks.value[mark]));
    }
    std::copy(payloads[i].bytes.begin(), payloads[i].bytes.end(), bytes.begin() + static_cast<std::ptrdiff_t>(at));
    at = align_to(at + payloads[i].bytes.size(), kSectionAlign);
  }

  out.bytes = std::move(bytes);
  return HkxStatus::Ok;
}

std::vector<std::byte> sorted_local(const HkxSection& section) {
  std::vector<HkxLocalFixup> fixups = section.local_fixups;
  std::sort(fixups.begin(), fixups.end(), [](const HkxLocalFixup& a, const HkxLocalFixup& b) {
    return a.source < b.source;
  });
  std::vector<std::byte> words;
  for (const auto& fixup : fixups) {
    append_u32(words, fixup.source);
    append_u32(words, fixup.destination);
  }
  return words;
}

std::vector<std::byte> sorted_global(const HkxSection& section) {
  std::vector<HkxRemoteFixup> fixups = section.global_fixups;
  std::sort(fixups.begin(), fixups.end(), [](const HkxRemoteFixup& a, const HkxRemoteFixup& b) {
    return a.source < b.source;
  });
  std::vector<std::byte> words;
  for (const auto& fixup : fixups) {
    append_u32(words, fixup.source);
    append_u32(words, fixup.section);
    append_u32(words, fixup.destination);
  }
  return words;
}

std::vector<std::byte> sorted_virtual(const HkxSection& section) {
  std::vector<HkxRemoteFixup> fixups = section.virtual_fixups;
  std::sort(fixups.begin(), fixups.end(), [](const HkxRemoteFixup& a, const HkxRemoteFixup& b) {
    return a.source < b.source;
  });
  std::vector<std::byte> words;
  for (const auto& fixup : fixups) {
    append_u32(words, fixup.source);
    append_u32(words, fixup.section);
    append_u32(words, fixup.destination);
  }
  return words;
}

}  // namespace

HkxStatus rewrite_packfile(std::span<const std::byte> source, const HkxPackfile& packfile,
                           std::vector<std::byte>& out) {
  if (source.size() < kHeaderSize) return HkxStatus::Truncated;
  const auto table_at = kHeaderSize + read_u16(source, kSectionTableOffsetAt);
  if (table_at > source.size() || (source.size() - table_at) / kSectionHeaderSize < packfile.sections.size()) {
    return HkxStatus::Truncated;
  }
  HkxAppend rebuilt;
  if (const auto status = finish_rebuild(source, packfile, table_at, rebuilt); status != HkxStatus::Ok) return status;
  out = std::move(rebuilt.bytes);
  return HkxStatus::Ok;
}

HkxStatus append_object(std::span<const std::byte> source, std::string_view class_name, std::uint32_t signature,
                        std::span<const std::byte> instance_bytes, HkxAppend& out) {
  HkxPackfile packfile;
  if (const auto status = read_packfile(source, packfile); status != HkxStatus::Ok) return status;
  const auto table_at = kHeaderSize + read_u16(source, kSectionTableOffsetAt);
  if (table_at > source.size() || (source.size() - table_at) / kSectionHeaderSize < packfile.sections.size()) {
    return HkxStatus::Truncated;
  }
  const HkxClassLayout* layout = find_layout(class_name, signature);
  if (layout == nullptr) return HkxStatus::UnsupportedLayout;
  if (instance_bytes.size() != layout->size) return HkxStatus::BadSection;

  HkxSection& names = packfile.sections[packfile.class_names_section];
  HkxSection& data = packfile.sections[packfile.data_section];
  const auto name_at = ensure_class_name(names, class_name, signature);
  if (!name_at) return HkxStatus::BadClassName;

  pad_filler(data.data, align_to(data.data.size(), kObjectAlign));
  const auto object_at = data.data.size();
  data.data.insert(data.data.end(), instance_bytes.begin(), instance_bytes.end());
  data.virtual_fixups.push_back({static_cast<std::uint32_t>(object_at),
                                 static_cast<std::uint32_t>(packfile.class_names_section),
                                 static_cast<std::uint32_t>(*name_at)});
  out.object_index = packfile.objects.size();
  return finish_rebuild(source, packfile, table_at, out);
}

HkxStatus append_interleaved_animation(std::span<const std::byte> source, const HkxInterleavedAnimation& animation,
                                       HkxAppend& out) {
  HkxPackfile packfile;
  if (const auto status = read_packfile(source, packfile); status != HkxStatus::Ok) return status;
  const auto table_at = kHeaderSize + read_u16(source, kSectionTableOffsetAt);
  if (table_at > source.size() || (source.size() - table_at) / kSectionHeaderSize < packfile.sections.size()) {
    return HkxStatus::Truncated;
  }
  const HkxClassLayout* base = find_layout("hkaAnimation");
  const HkxClassLayout* interleaved = find_layout("hkaInterleavedUncompressedAnimation");
  if (base == nullptr || interleaved == nullptr) return HkxStatus::UnsupportedLayout;
  const auto type_at = find_member_offset(*base, "type");
  const auto duration_at = find_member_offset(*base, "duration");
  const auto tracks_at = find_member_offset(*base, "numberOfTransformTracks");
  const auto float_tracks_at = find_member_offset(*base, "numberOfFloatTracks");
  const auto annotations_at = find_member_offset(*base, "annotationTracks");
  const auto transforms_at = find_member_offset(*interleaved, "transforms");
  const auto floats_at = find_member_offset(*interleaved, "floats");
  if (!type_at || !duration_at || !tracks_at || !float_tracks_at || !annotations_at || !transforms_at || !floats_at) {
    return HkxStatus::UnsupportedLayout;
  }
  if (!std::isfinite(animation.duration) || animation.frame_count == 0 || animation.track_count == 0 ||
      (animation.frame_count > 1 && !(animation.duration > 0.0f))) {
    return HkxStatus::BadSection;
  }
  if (animation.frame_count > kMaxAnimationSamples / animation.track_count) return HkxStatus::BadSection;
  const auto samples = animation.frame_count * animation.track_count;
  if (animation.translations.size() != samples || animation.rotations.size() != samples ||
      animation.scales.size() != samples) {
    return HkxStatus::BadSection;
  }
  // Constraint: a non-finite sample cannot be stored, because the reader would hand it to the engine unchanged.
  for (std::size_t sample = 0; sample < samples; ++sample) {
    for (const float value : animation.translations[sample]) {
      if (!std::isfinite(value)) return HkxStatus::BadSection;
    }
    for (const float value : animation.rotations[sample]) {
      if (!std::isfinite(value)) return HkxStatus::BadSection;
    }
    for (const float value : animation.scales[sample]) {
      if (!std::isfinite(value)) return HkxStatus::BadSection;
    }
  }

  HkxSection& names = packfile.sections[packfile.class_names_section];
  HkxSection& data = packfile.sections[packfile.data_section];
  const auto name_at = ensure_class_name(names, interleaved->name, interleaved->signature);
  if (!name_at) return HkxStatus::BadClassName;

  // Constraint: the object comes first and its transforms array after it, so the array fixup points forwards.
  pad_filler(data.data, align_to(data.data.size(), kObjectAlign));
  const auto object_at = data.data.size();
  data.data.resize(object_at + interleaved->size, std::byte{0});
  put_int(data.data, object_at + *type_at, kInterleavedType);
  put_float(data.data, object_at + *duration_at, animation.duration);
  put_int(data.data, object_at + *tracks_at, static_cast<std::int32_t>(animation.track_count));
  put_int(data.data, object_at + *float_tracks_at, 0);
  // Invariant: an array header is pointer, count and a flagged capacity, and an empty array has no local fixup.
  put_int(data.data, object_at + *annotations_at + 8, 0);
  put_int(data.data, object_at + *annotations_at + 12, static_cast<std::int32_t>(kArrayFlag));
  put_int(data.data, object_at + *floats_at + 8, 0);
  put_int(data.data, object_at + *floats_at + 12, static_cast<std::int32_t>(kArrayFlag));
  put_int(data.data, object_at + *transforms_at + 8, static_cast<std::int32_t>(samples));
  put_int(data.data, object_at + *transforms_at + 12, static_cast<std::int32_t>(kArrayFlag | samples));

  // Constraint: the transforms array is frame-major and 16-aligned, because the reader slices it by track count.
  pad_filler(data.data, align_to(data.data.size(), kObjectAlign));
  const auto transforms_run = data.data.size();
  data.data.resize(transforms_run + samples * kQsTransformSize, std::byte{0});
  for (std::size_t sample = 0; sample < samples; ++sample) {
    const auto at = transforms_run + sample * kQsTransformSize;
    for (std::size_t axis = 0; axis < 3; ++axis) {
      put_float(data.data, at + 4 * axis, animation.translations[sample][axis]);
    }
    const auto rotation = normalised_quaternion(animation.rotations[sample]);
    if (!rotation) return HkxStatus::BadSection;
    for (std::size_t axis = 0; axis < 4; ++axis) {
      put_float(data.data, at + kQsRotationOffset + 4 * axis, (*rotation)[axis]);
    }
    for (std::size_t axis = 0; axis < 3; ++axis) {
      put_float(data.data, at + kQsScaleOffset + 4 * axis, animation.scales[sample][axis]);
    }
  }

  data.local_fixups.push_back({static_cast<std::uint32_t>(object_at + *transforms_at),
                               static_cast<std::uint32_t>(transforms_run)});
  data.virtual_fixups.push_back({static_cast<std::uint32_t>(object_at),
                                 static_cast<std::uint32_t>(packfile.class_names_section),
                                 static_cast<std::uint32_t>(*name_at)});
  out.object_index = packfile.objects.size();
  return finish_rebuild(source, packfile, table_at, out);
}

HkxStatus append_spline_animation(std::span<const std::byte> source, const HkxInterleavedAnimation& animation,
                                  const HkxSplineOptions& options, HkxAppend& out) {
  HkxSplineBlob blob;
  if (const auto status = encode_spline_blob(animation, options, blob); status != HkxStatus::Ok) return status;
  HkxPackfile packfile;
  if (const auto status = read_packfile(source, packfile); status != HkxStatus::Ok) return status;
  const auto table_at = kHeaderSize + read_u16(source, kSectionTableOffsetAt);
  if (table_at > source.size() || (source.size() - table_at) / kSectionHeaderSize < packfile.sections.size()) {
    return HkxStatus::Truncated;
  }
  const HkxClassLayout* base = find_layout("hkaAnimation");
  const HkxClassLayout* spline = find_layout("hkaSplineCompressedAnimation");
  if (base == nullptr || spline == nullptr) return HkxStatus::UnsupportedLayout;
  const auto type_at = find_member_offset(*base, "type");
  const auto duration_at = find_member_offset(*base, "duration");
  const auto tracks_at = find_member_offset(*base, "numberOfTransformTracks");
  const auto float_tracks_at = find_member_offset(*base, "numberOfFloatTracks");
  const auto annotations_at = find_member_offset(*base, "annotationTracks");
  const auto frames_at = find_member_offset(*spline, "numFrames");
  const auto blocks_at = find_member_offset(*spline, "numBlocks");
  const auto block_frames_at = find_member_offset(*spline, "maxFramesPerBlock");
  const auto mask_size_at = find_member_offset(*spline, "maskAndQuantizationSize");
  const auto block_duration_at = find_member_offset(*spline, "blockDuration");
  const auto block_inverse_duration_at = find_member_offset(*spline, "blockInverseDuration");
  const auto frame_duration_at = find_member_offset(*spline, "frameDuration");
  const auto block_offsets_at = find_member_offset(*spline, "blockOffsets");
  const auto float_block_offsets_at = find_member_offset(*spline, "floatBlockOffsets");
  const auto transform_offsets_at = find_member_offset(*spline, "transformOffsets");
  const auto float_offsets_at = find_member_offset(*spline, "floatOffsets");
  const auto data_at = find_member_offset(*spline, "data");
  if (!type_at || !duration_at || !tracks_at || !float_tracks_at || !annotations_at || !frames_at || !blocks_at ||
      !block_frames_at || !mask_size_at || !block_duration_at || !block_inverse_duration_at || !frame_duration_at ||
      !block_offsets_at || !float_block_offsets_at || !transform_offsets_at || !float_offsets_at || !data_at) {
    return HkxStatus::UnsupportedLayout;
  }

  HkxSection& names = packfile.sections[packfile.class_names_section];
  HkxSection& data = packfile.sections[packfile.data_section];
  const auto name_at = ensure_class_name(names, spline->name, spline->signature);
  if (!name_at) return HkxStatus::BadClassName;

  // Constraint: the object comes first and its arrays after it, so both array fixups point forwards.
  pad_filler(data.data, align_to(data.data.size(), kObjectAlign));
  const auto object_at = data.data.size();
  data.data.resize(object_at + spline->size, std::byte{0});
  put_int(data.data, object_at + *type_at, static_cast<std::int32_t>(kSplineType));
  put_float(data.data, object_at + *duration_at, blob.duration);
  put_int(data.data, object_at + *tracks_at, static_cast<std::int32_t>(blob.tracks));
  put_int(data.data, object_at + *float_tracks_at, 0);
  put_int(data.data, object_at + *frames_at, static_cast<std::int32_t>(blob.frames));
  put_int(data.data, object_at + *blocks_at, static_cast<std::int32_t>(blob.blocks));
  put_int(data.data, object_at + *block_frames_at, static_cast<std::int32_t>(blob.max_frames_per_block));
  put_int(data.data, object_at + *mask_size_at, static_cast<std::int32_t>(blob.mask_size));
  put_float(data.data, object_at + *block_duration_at, blob.block_duration);
  put_float(data.data, object_at + *block_inverse_duration_at, blob.block_inverse_duration);
  put_float(data.data, object_at + *frame_duration_at, blob.frame_duration);
  for (const auto empty_at : {float_block_offsets_at, transform_offsets_at, float_offsets_at}) {
    put_int(data.data, object_at + *empty_at + 8, 0);
    put_int(data.data, object_at + *empty_at + 12, static_cast<std::int32_t>(kArrayFlag));
  }
  put_int(data.data, object_at + *annotations_at + 8, 0);
  put_int(data.data, object_at + *annotations_at + 12, static_cast<std::int32_t>(kArrayFlag));

  pad_filler(data.data, align_to(data.data.size(), kObjectAlign));
  const auto offsets_run = data.data.size();
  data.data.resize(offsets_run + blob.block_offsets.size() * sizeof(std::uint32_t), std::byte{0});
  for (std::size_t block = 0; block < blob.block_offsets.size(); ++block) {
    const auto at = offsets_run + block * sizeof(std::uint32_t);
    put_int(data.data, at, static_cast<std::int32_t>(blob.block_offsets[block]));
  }
  put_int(data.data, object_at + *block_offsets_at + 8, static_cast<std::int32_t>(blob.block_offsets.size()));
  put_int(data.data, object_at + *block_offsets_at + 12,
          static_cast<std::int32_t>(kArrayFlag | static_cast<std::uint32_t>(blob.block_offsets.size())));
  data.local_fixups.push_back({static_cast<std::uint32_t>(object_at + *block_offsets_at),
                               static_cast<std::uint32_t>(offsets_run)});

  pad_filler(data.data, align_to(data.data.size(), kObjectAlign));
  const auto blob_run = data.data.size();
  data.data.insert(data.data.end(), blob.data.begin(), blob.data.end());
  put_int(data.data, object_at + *data_at + 8, static_cast<std::int32_t>(blob.data.size()));
  put_int(data.data, object_at + *data_at + 12,
          static_cast<std::int32_t>(kArrayFlag | static_cast<std::uint32_t>(blob.data.size())));
  data.local_fixups.push_back({static_cast<std::uint32_t>(object_at + *data_at),
                               static_cast<std::uint32_t>(blob_run)});

  data.virtual_fixups.push_back({static_cast<std::uint32_t>(object_at),
                                 static_cast<std::uint32_t>(packfile.class_names_section),
                                 static_cast<std::uint32_t>(*name_at)});
  pad_filler(data.data, align_to(data.data.size(), kObjectAlign));
  out.object_index = packfile.objects.size();
  return finish_rebuild(source, packfile, table_at, out);
}

HkxStatus verify_append(std::span<const std::byte> source, std::span<const std::byte> written,
                        std::span<const std::byte> expected_instance) {
  HkxPackfile before;
  if (const auto status = read_packfile(source, before); status != HkxStatus::Ok) return status;
  HkxPackfile after;
  if (const auto status = read_packfile(written, after); status != HkxStatus::Ok) return status;
  if (after.objects.size() != before.objects.size() + 1) return HkxStatus::MissingObject;
  if (before.sections.size() != after.sections.size()) return HkxStatus::BadSection;
  // Constraint: the writer copies the header, so the user tag and file version must come back unchanged.
  if (before.user_tag != after.user_tag || before.file_version != after.file_version) return HkxStatus::BadSection;

  // Constraint: the append may only add bytes to the data section, so every old byte must survive in place.
  const auto& before_data = before.sections[before.data_section].data;
  const auto& after_data = after.sections[after.data_section].data;
  if (after_data.size() < before_data.size()) return HkxStatus::BadSection;
  if (!std::equal(before_data.begin(), before_data.end(), after_data.begin())) return HkxStatus::BadSection;

  for (std::size_t i = 0; i < before.objects.size(); ++i) {
    const auto& old_object = before.objects[i];
    const auto& new_object = after.objects[i];
    if (old_object.class_name != new_object.class_name) return HkxStatus::BadClassName;
    if (old_object.signature != new_object.signature) return HkxStatus::BadClassName;
    if (old_object.offset != new_object.offset) return HkxStatus::BadSection;
  }

  if (!before.objects.empty()) {
    const auto& grew = after.objects[before.objects.size() - 1];
    const auto& grew_from = before.objects[before.objects.size() - 1];
    if (grew.data.size() < grew_from.data.size()) return HkxStatus::BadSection;
    if (grew.data.size() - grew_from.data.size() > kMaxAppendGrowth) return HkxStatus::BadSection;
    for (std::size_t i = grew_from.data.size(); i < grew.data.size(); ++i) {
      if (grew.data[i] != std::byte{0} && grew.data[i] != std::byte{0xFF}) return HkxStatus::BadSection;
    }
  }

  // Constraint: the new object starts on an alignment boundary past every old one, and carries what was written.
  const auto& added = after.objects.back();
  if (added.offset % kObjectAlign != 0) return HkxStatus::BadSection;
  if (added.class_name.empty()) return HkxStatus::BadClassName;
  if (!before.objects.empty() && added.offset <= before.objects.back().offset) return HkxStatus::BadSection;
  if (!expected_instance.empty() && (added.data.size() < expected_instance.size() ||
                                     !std::equal(expected_instance.begin(), expected_instance.end(),
                                                 added.data.begin()))) {
    return HkxStatus::BadSection;
  }

  for (std::size_t i = 0; i < before.sections.size(); ++i) {
    if (before.sections[i].tag != after.sections[i].tag) return HkxStatus::BadSection;
    if (sorted_global(before.sections[i]) != sorted_global(after.sections[i])) return HkxStatus::BadSection;
    if (i == before.class_names_section) {
      // Constraint: the writer trims the old 0xFF tail before appending a name, so compare the trimmed prefix only.
      const auto keep = names_end(before.sections[i]);
      const auto& old_names = before.sections[i].data;
      if (after.sections[i].data.size() < keep) return HkxStatus::BadSection;
      if (!std::equal(old_names.begin(), old_names.begin() + static_cast<std::ptrdiff_t>(keep),
                      after.sections[i].data.begin())) {
        return HkxStatus::BadSection;
      }
      if (sorted_local(before.sections[i]) != sorted_local(after.sections[i])) return HkxStatus::BadSection;
      if (sorted_virtual(before.sections[i]) != sorted_virtual(after.sections[i])) return HkxStatus::BadSection;
      // Constraint: the name table grows, so its marks must be the canonical layout of the rewritten section.
      const auto new_marks = section_marks(written, i);
      if (!new_marks || new_marks->value != payload_of(after.sections[i]).marks.value) return HkxStatus::BadSection;
      continue;
    }
    if (i == before.data_section) continue;
    if (before.sections[i].data != after.sections[i].data) return HkxStatus::BadSection;
    if (sorted_local(before.sections[i]) != sorted_local(after.sections[i])) return HkxStatus::BadSection;
    if (sorted_virtual(before.sections[i]) != sorted_virtual(after.sections[i])) return HkxStatus::BadSection;
    const auto old_marks = section_marks(source, i);
    const auto new_marks = section_marks(written, i);
    if (!old_marks || !new_marks || old_marks->value != new_marks->value) return HkxStatus::BadSection;
  }

  // Constraint: the data section gains one virtual fixup and up to two local ones, the spline block-offs and data.
  const auto& old_data_section = before.sections[before.data_section];
  const auto& new_data_section = after.sections[before.data_section];
  const auto old_virtuals = sorted_virtual(old_data_section);
  const auto new_virtuals = sorted_virtual(new_data_section);
  if (new_virtuals.size() != old_virtuals.size() + 3 * sizeof(std::uint32_t)) return HkxStatus::BadSection;
  if (!std::equal(old_virtuals.begin(), old_virtuals.end(), new_virtuals.begin())) return HkxStatus::BadSection;
  const auto old_locals = sorted_local(old_data_section);
  const auto new_locals = sorted_local(new_data_section);
  if (new_locals.size() < old_locals.size()) return HkxStatus::BadSection;
  if (!std::equal(old_locals.begin(), old_locals.end(), new_locals.begin())) return HkxStatus::BadSection;
  // Constraint: a spline append adds two local fixups, the block-offsets array and the block data array.
  if (new_locals.size() > old_locals.size() + 2 * kLocalEntry) return HkxStatus::BadSection;
  return HkxStatus::Ok;
}

namespace {

bool same_fixups(const std::vector<HkxLocalFixup>& a, const std::vector<HkxLocalFixup>& b) {
  return std::equal(a.begin(), a.end(), b.begin(), b.end(), [](const HkxLocalFixup& x, const HkxLocalFixup& y) {
    return x.source == y.source && x.destination == y.destination;
  });
}

bool same_fixups(const std::vector<HkxRemoteFixup>& a, const std::vector<HkxRemoteFixup>& b) {
  return std::equal(a.begin(), a.end(), b.begin(), b.end(), [](const HkxRemoteFixup& x, const HkxRemoteFixup& y) {
    return x.source == y.source && x.section == y.section && x.destination == y.destination;
  });
}

// Constraint: the rewrite pads section data to 16 with 0xFF, so a longer reread tail must be filler.
bool same_section_data(const std::vector<std::byte>& before, const std::vector<std::byte>& after) {
  if (after.size() < before.size() || !std::equal(before.begin(), before.end(), after.begin())) return false;
  return std::all_of(after.begin() + static_cast<std::ptrdiff_t>(before.size()), after.end(),
                     [](std::byte value) { return value == std::byte{0xFF}; });
}

}  // namespace

std::string first_packfile_difference(const HkxPackfile& a, const HkxPackfile& b) {
  if (a.user_tag != b.user_tag) return "user tag differs";
  if (a.file_version != b.file_version) return "file version differs";
  if (a.pointer_size != b.pointer_size) return "pointer size differs";
  if (a.class_names_section != b.class_names_section || a.data_section != b.data_section) {
    return "class-name or data section index differs";
  }
  if (a.sections.size() != b.sections.size()) return "section count differs";
  for (std::size_t i = 0; i < a.sections.size(); ++i) {
    const auto at = "section " + std::to_string(i) + " ";
    if (a.sections[i].tag != b.sections[i].tag) return at + "tag differs";
    if (!same_section_data(a.sections[i].data, b.sections[i].data)) return at + "data differs";
    if (!same_fixups(a.sections[i].local_fixups, b.sections[i].local_fixups)) return at + "local fixups differ";
    if (!same_fixups(a.sections[i].global_fixups, b.sections[i].global_fixups)) return at + "global fixups differ";
    if (!same_fixups(a.sections[i].virtual_fixups, b.sections[i].virtual_fixups)) return at + "virtual fixups differ";
  }
  if (a.objects.size() != b.objects.size()) return "object count differs";
  for (std::size_t i = 0; i < a.objects.size(); ++i) {
    const auto& x = a.objects[i];
    const auto& y = b.objects[i];
    if (x.offset != y.offset || x.signature != y.signature || x.class_name != y.class_name) {
      return "object " + std::to_string(i) + " differs";
    }
  }
  return {};
}

namespace {

constexpr std::string_view kRootContainer = "hkRootLevelContainer";

struct Extent {
  std::size_t begin;
  std::size_t end;
  bool removable;
};

struct Pointer {
  std::size_t section;
  std::size_t source;
  std::size_t destination;
};

// Invariant: a loose region runs from a pointed destination outside every object to the next object start or pointed destination.
class Ownership {
 public:
  explicit Ownership(const HkxPackfile& packfile) : data_section_(packfile.data_section) {
    const HkxSection& data = packfile.sections[data_section_];
    const std::size_t count = packfile.objects.size();
    for (std::size_t i = 0; i < count; ++i) {
      const HkxObjectView& object = packfile.objects[i];
      const std::size_t span_end = i + 1 < count ? packfile.objects[i + 1].offset : data.data.size();
      const HkxClassLayout* layout = find_layout(object.class_name, object.signature);
      const bool fits = layout != nullptr && object.offset + layout->size <= span_end;
      extents_.push_back({object.offset, fits ? object.offset + layout->size : span_end, fits});
    }
    for (const HkxLocalFixup& fixup : data.local_fixups) {
      pointers_.push_back({data_section_, fixup.source, fixup.destination});
    }
    for (std::size_t s = 0; s < packfile.sections.size(); ++s) {
      for (const HkxRemoteFixup& fixup : packfile.sections[s].global_fixups) {
        if (fixup.section == data_section_) pointers_.push_back({s, fixup.source, fixup.destination});
      }
    }
    for (const Extent& extent : extents_) boundaries_.push_back(extent.begin);
    for (const Pointer& pointer : pointers_) boundaries_.push_back(pointer.destination);
    boundaries_.push_back(data.data.size());
    std::sort(boundaries_.begin(), boundaries_.end());
    boundaries_.erase(std::unique(boundaries_.begin(), boundaries_.end()), boundaries_.end());
    for (const Pointer& pointer : pointers_) {
      if (!object_of(pointer.destination)) loose_.push_back(pointer.destination);
    }
    std::sort(loose_.begin(), loose_.end());
    loose_.erase(std::unique(loose_.begin(), loose_.end()), loose_.end());
    dead_object_.assign(extents_.size(), false);
    dead_region_.assign(loose_.size(), false);
  }

  const Extent& extent(std::size_t object) const { return extents_[object]; }
  bool dead(std::size_t object) const { return dead_object_[object]; }
  void kill(std::size_t object) { dead_object_[object] = true; }
  std::size_t objects() const { return extents_.size(); }
  std::size_t regions() const { return loose_.size(); }
  bool region_dead(std::size_t region) const { return dead_region_[region]; }
  std::size_t region_begin(std::size_t region) const { return loose_[region]; }
  std::size_t region_end(std::size_t region) const {
    return *std::upper_bound(boundaries_.begin(), boundaries_.end(), loose_[region]);
  }

  std::optional<std::size_t> object_of(std::size_t offset) const {
    const auto after = std::upper_bound(extents_.begin(), extents_.end(), offset,
                                        [](std::size_t value, const Extent& extent) { return value < extent.begin; });
    if (after == extents_.begin()) return std::nullopt;
    const auto index = static_cast<std::size_t>((after - 1) - extents_.begin());
    if (offset >= extents_[index].end) return std::nullopt;
    return index;
  }

  bool source_dead(const Pointer& pointer) const {
    if (pointer.section != data_section_) return false;
    return offset_dead(pointer.source);
  }

  bool offset_dead(std::size_t offset) const {
    if (const auto object = object_of(offset)) return dead_object_[*object];
    const auto after = std::upper_bound(loose_.begin(), loose_.end(), offset);
    if (after == loose_.begin()) return false;
    const auto region = static_cast<std::size_t>((after - 1) - loose_.begin());
    return offset < region_end(region) && dead_region_[region];
  }

  const std::vector<Pointer>& pointers() const { return pointers_; }

  // Constraint: only strings, pointer arrays and struct arrays held by a live object member are measured.
  std::size_t live_region_bytes(const HkxPackfile& packfile, const Pointer& pointer, const HkxObjectView& object,
                                const HkxClassLayout& layout) const {
    const HkxSection& data = packfile.sections[data_section_];
    for (const HkxMemberLayout& member : all_members(layout)) {
      if (member.offset != pointer.source - object.offset) continue;
      if (member.type == "stringptr") {
        const auto text = cstring_at(data.data, pointer.destination);
        return text ? text->size() + 1 : 0;
      }
      if (!member.type.starts_with("array of ") || pointer.source + 12 > data.data.size()) return 0;
      const std::int32_t count = read_i32(data.data, pointer.source + 8);
      std::size_t element = member.type == "array of pointer" ? 8 : 0;
      if (!member.type_class.empty()) {
        if (const HkxClassLayout* row = find_layout(member.type_class)) element = row->size;
      }
      return count > 0 ? static_cast<std::size_t>(count) * element : 0;
    }
    return 0;
  }

  // Constraint: refuses a dead pointer inside a measured live region, since the dead sub-region would be zeroed.
  std::optional<std::size_t> split_destination(const HkxPackfile& packfile) const {
    std::vector<std::size_t> dead_destinations;
    dead_destinations.reserve(loose_.size());
    for (std::size_t region = 0; region < loose_.size(); ++region) {
      if (dead_region_[region]) dead_destinations.push_back(loose_[region]);
    }
    for (const Pointer& live : pointers_) {
      if (live.section != data_section_ || source_dead(live) || object_of(live.destination)) continue;
      const auto holder = object_of(live.source);
      if (!holder) continue;
      const HkxObjectView& object = packfile.objects[*holder];
      const HkxClassLayout* layout = find_layout(object.class_name, object.signature);
      if (layout == nullptr) continue;
      const std::size_t bytes = live_region_bytes(packfile, live, object, *layout);
      if (bytes == 0) continue;
      const auto first = std::upper_bound(dead_destinations.begin(), dead_destinations.end(), live.destination);
      if (first != dead_destinations.end() && *first < live.destination + bytes) return *first;
    }
    return std::nullopt;
  }

  // Invariant: a region dies when every pointer into it comes from dead bytes; with cascade, so does an object that has referrers and no live one.
  void settle(bool cascade, std::string_view root_class, const HkxPackfile& packfile) {
    for (bool changed = true; changed;) {
      changed = false;
      std::vector<std::size_t> total_objects(extents_.size(), 0);
      std::vector<std::size_t> live_objects(extents_.size(), 0);
      std::vector<std::size_t> live_regions(loose_.size(), 0);
      for (const Pointer& pointer : pointers_) {
        const std::size_t live = source_dead(pointer) ? 0 : 1;
        if (const auto object = object_of(pointer.destination)) {
          total_objects[*object] += 1;
          live_objects[*object] += live;
        } else {
          const auto after = std::upper_bound(loose_.begin(), loose_.end(), pointer.destination);
          live_regions[static_cast<std::size_t>((after - 1) - loose_.begin())] += live;
        }
      }
      for (std::size_t r = 0; r < loose_.size(); ++r) {
        if (!dead_region_[r] && live_regions[r] == 0) {
          dead_region_[r] = true;
          changed = true;
        }
      }
      if (!cascade) continue;
      for (std::size_t i = 0; i < extents_.size(); ++i) {
        if (dead_object_[i] || !extents_[i].removable || packfile.objects[i].class_name == root_class) continue;
        if (total_objects[i] > 0 && live_objects[i] == 0) {
          dead_object_[i] = true;
          changed = true;
        }
      }
    }
  }

 private:
  std::size_t data_section_;
  std::vector<Extent> extents_;
  std::vector<Pointer> pointers_;
  std::vector<std::size_t> boundaries_;
  std::vector<std::size_t> loose_;
  std::vector<bool> dead_object_;
  std::vector<bool> dead_region_;
};

void refuse_removal(HkxRemoval& out, std::string text) { out.refusal = std::move(text); }

}  // namespace

HkxStatus remove_objects(HkxPackfile& packfile, std::span<const std::size_t> object_indices, bool cascade,
                         HkxRemoval& out) {
  out = HkxRemoval{};
  if (packfile.data_section >= packfile.sections.size() ||
      packfile.objects.size() != packfile.sections[packfile.data_section].virtual_fixups.size()) {
    refuse_removal(out, "the object list does not match the data section's object table");
    return HkxStatus::BadSection;
  }
  if (object_indices.empty()) {
    refuse_removal(out, "there are no objects to remove");
    return HkxStatus::MissingObject;
  }
  Ownership owner(packfile);
  for (const std::size_t index : object_indices) {
    if (index >= owner.objects()) {
      refuse_removal(out, "object " + std::to_string(index) + " is outside the object list");
      return HkxStatus::MissingObject;
    }
    const HkxObjectView& object = packfile.objects[index];
    // Constraint: only hkRootLevelContainer is protected here; other callers check their own roots.
    if (object.class_name == kRootContainer) {
      refuse_removal(out, "the root level container is never removed");
      return HkxStatus::UnsupportedLayout;
    }
    if (!owner.extent(index).removable) {
      refuse_removal(out, "object " + std::to_string(index) + " is a " + detail::printable_text(object.class_name) +
                              ", which the layout table does not size");
      return HkxStatus::UnsupportedLayout;
    }
    owner.kill(index);
  }
  owner.settle(cascade, kRootContainer, packfile);
  for (const std::size_t index : object_indices) {
    for (const Pointer& pointer : owner.pointers()) {
      const auto target = owner.object_of(pointer.destination);
      if (target && *target == index && !owner.source_dead(pointer)) {
        refuse_removal(out, "object " + std::to_string(index) + " (" + detail::printable_text(packfile.objects[index].class_name) +
                                ") is still pointed at from section " + std::to_string(pointer.section) + " offset " +
                                std::to_string(pointer.source));
        return HkxStatus::BadSection;
      }
    }
  }
  if (const auto split = owner.split_destination(packfile)) {
    refuse_removal(out, "a pointer from the removed bytes lands at offset " + std::to_string(*split) +
                            ", inside an array or string a live pointer still reaches, so zeroing it would cut that region short");
    return HkxStatus::BadSection;
  }

  HkxSection& data = packfile.sections[packfile.data_section];
  const auto zero = [&](std::size_t begin, std::size_t end) {
    std::fill(data.data.begin() + static_cast<std::ptrdiff_t>(begin), data.data.begin() + static_cast<std::ptrdiff_t>(end),
              std::byte{0});
  };
  for (std::size_t i = 0; i < owner.objects(); ++i) {
    if (!owner.dead(i)) continue;
    zero(owner.extent(i).begin, owner.extent(i).end);
    out.removed.push_back(i);
  }
  for (std::size_t r = 0; r < owner.regions(); ++r) {
    if (owner.region_dead(r)) zero(owner.region_begin(r), owner.region_end(r));
  }
  std::erase_if(data.virtual_fixups, [&](const HkxRemoteFixup& fixup) { return owner.offset_dead(fixup.source); });
  std::erase_if(data.local_fixups, [&](const HkxLocalFixup& fixup) { return owner.offset_dead(fixup.source); });
  std::erase_if(data.global_fixups, [&](const HkxRemoteFixup& fixup) { return owner.offset_dead(fixup.source); });
  return HkxStatus::Ok;
}

HkxRepackReport repack_check(std::span<const std::byte> bytes) {
  HkxRepackReport report;
  report.source_size = bytes.size();
  HkxPackfile before;
  report.status = read_packfile(bytes, before);
  if (report.status != HkxStatus::Ok) return report;

  std::vector<std::byte> rewritten;
  report.status = rewrite_packfile(bytes, before, rewritten);
  if (report.status != HkxStatus::Ok) {
    report.verdict = report.status == HkxStatus::UnsupportedLayout ? HkxRepackVerdict::Refused
                                                                    : HkxRepackVerdict::RewriteFailed;
    return report;
  }
  report.rewritten_size = rewritten.size();
  report.bytes_identical = rewritten.size() == bytes.size() && std::equal(bytes.begin(), bytes.end(), rewritten.begin());

  HkxPackfile after;
  report.status = read_packfile(rewritten, after);
  if (report.status != HkxStatus::Ok) {
    report.verdict = HkxRepackVerdict::RereadFailed;
    return report;
  }
  report.difference = first_packfile_difference(before, after);
  report.verdict = report.difference.empty() ? HkxRepackVerdict::Equal : HkxRepackVerdict::Differs;
  return report;
}

}  // namespace fo4::hkx
