#include "fo4/hkx/write.hpp"

#include "fo4/hkx/layout.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <optional>
#include <span>
#include <string_view>
#include <utility>
#include <vector>

namespace fo4::hkx {
namespace {

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
constexpr std::uint32_t kArrayFlag = 0x80000000u;

std::uint16_t read_u16(std::span<const std::byte> bytes, std::size_t at) {
  std::uint16_t value = 0;
  std::memcpy(&value, bytes.data() + at, sizeof value);
  return value;
}

std::uint32_t read_u32(std::span<const std::byte> bytes, std::size_t at) {
  std::uint32_t value = 0;
  std::memcpy(&value, bytes.data() + at, sizeof value);
  return value;
}

void write_u32(std::vector<std::byte>& bytes, std::size_t at, std::uint32_t value) {
  std::memcpy(bytes.data() + at, &value, sizeof value);
}

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

// Invariant: a stored rotation is a unit quaternion, so a non-unit input is normalised and a zero one becomes identity.
std::array<float, 4> normalised_quaternion(const std::array<float, 4>& rotation) {
  const double length = std::sqrt(static_cast<double>(rotation[0]) * rotation[0] +
                                  static_cast<double>(rotation[1]) * rotation[1] +
                                  static_cast<double>(rotation[2]) * rotation[2] +
                                  static_cast<double>(rotation[3]) * rotation[3]);
  if (!(length > 0.0)) return {0.0f, 0.0f, 0.0f, 1.0f};
  const auto scale = static_cast<float>(1.0 / length);
  return {rotation[0] * scale, rotation[1] * scale, rotation[2] * scale, rotation[3] * scale};
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

std::vector<HkxRemoteFixup> sorted_global(const HkxSection& section) {
  std::vector<HkxRemoteFixup> fixups = section.global_fixups;
  std::sort(fixups.begin(), fixups.end(), [](const HkxRemoteFixup& a, const HkxRemoteFixup& b) {
    return a.source < b.source;
  });
  return fixups;
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
  if (!std::isfinite(animation.duration) || animation.frame_count == 0 || animation.track_count == 0) {
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
    for (std::size_t axis = 0; axis < 4; ++axis) {
      put_float(data.data, at + kQsRotationOffset + 4 * axis, rotation[axis]);
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

HkxStatus verify_append(std::span<const std::byte> source, std::span<const std::byte> written,
                        std::span<const std::byte> expected_instance) {
  HkxPackfile before;
  if (const auto status = read_packfile(source, before); status != HkxStatus::Ok) return status;
  HkxPackfile after;
  if (const auto status = read_packfile(written, after); status != HkxStatus::Ok) return status;
  if (after.objects.size() != before.objects.size() + 1) return HkxStatus::MissingObject;
  if (before.sections.size() != after.sections.size()) return HkxStatus::BadSection;

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
    if (i == before.class_names_section) {
      // Constraint: the writer trims the old 0xFF tail before appending a name, so compare the trimmed prefix only.
      const auto keep = names_end(before.sections[i]);
      const auto& old_names = before.sections[i].data;
      if (after.sections[i].data.size() < keep) return HkxStatus::BadSection;
      if (!std::equal(old_names.begin(), old_names.begin() + static_cast<std::ptrdiff_t>(keep),
                      after.sections[i].data.begin())) {
        return HkxStatus::BadSection;
      }
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

  // Constraint: the data section gains one virtual fixup for the new object and at most one local array fixup.
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
  if (new_locals.size() > old_locals.size() + kLocalEntry) return HkxStatus::BadSection;
  const auto old_globals = sorted_global(old_data_section);
  const auto new_globals = sorted_global(new_data_section);
  if (old_globals.size() != new_globals.size()) return HkxStatus::BadSection;
  for (std::size_t i = 0; i < old_globals.size(); ++i) {
    if (old_globals[i].source != new_globals[i].source || old_globals[i].section != new_globals[i].section ||
        old_globals[i].destination != new_globals[i].destination) {
      return HkxStatus::BadSection;
    }
  }
  return HkxStatus::Ok;
}

}  // namespace fo4::hkx
