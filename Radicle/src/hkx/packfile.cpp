#include "fo4/hkx/packfile.hpp"

#include <algorithm>
#include <array>
#include <cstring>
#include <utility>

namespace fo4::hkx {
namespace {

constexpr std::uint32_t kMagic0 = 0x57E0E057u;
constexpr std::uint32_t kMagic1 = 0x10C0C010u;
constexpr std::int32_t kFileVersion = 11;
constexpr std::array<std::uint8_t, 4> kLayoutRules{8, 1, 0, 1};
constexpr std::uint64_t kPointerSize = 8;
constexpr std::size_t kHeaderSize = 0x40;
constexpr std::size_t kSectionHeaderSize = 0x40;
constexpr std::size_t kTagSize = 20;
constexpr std::size_t kLocalEntrySize = 8;
constexpr std::size_t kRemoteEntrySize = 12;
constexpr std::uint32_t kFiller = 0xFFFFFFFFu;
constexpr std::size_t kClassNamePrefix = 5;
constexpr std::int32_t kMaxSectionCount = 16;
constexpr std::size_t kMaxCStringScan = 256;

struct SectionRange {
  std::size_t begin{0};
  std::size_t end{0};
};

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

std::int32_t read_i32(std::span<const std::byte> bytes, std::size_t at) {
  return static_cast<std::int32_t>(read_u32(bytes, at));
}

std::string read_tag(std::span<const std::byte> bytes, std::size_t at) {
  std::string tag;
  for (std::size_t i = 0; i < kTagSize; ++i) {
    const auto ch = std::to_integer<unsigned char>(bytes[at + i]);
    if (ch == 0) break;
    tag.push_back(static_cast<char>(ch));
  }
  return tag;
}

HkxStatus read_local_table(std::span<const std::byte> table, std::vector<HkxLocalFixup>& out) {
  if (table.size() % kLocalEntrySize != 0) return HkxStatus::BadSection;
  for (std::size_t at = 0; at < table.size(); at += kLocalEntrySize) {
    const std::uint32_t source = read_u32(table, at);
    if (source == kFiller) continue;
    out.push_back({source, read_u32(table, at + 4)});
  }
  return HkxStatus::Ok;
}

HkxStatus read_remote_table(std::span<const std::byte> table, std::vector<HkxRemoteFixup>& out) {
  if (table.size() % kRemoteEntrySize != 0) return HkxStatus::BadSection;
  for (std::size_t at = 0; at < table.size(); at += kRemoteEntrySize) {
    const std::uint32_t source = read_u32(table, at);
    if (source == kFiller) continue;
    out.push_back({source, read_u32(table, at + 4), read_u32(table, at + 8)});
  }
  return HkxStatus::Ok;
}

HkxStatus read_section(std::span<const std::byte> bytes, std::size_t header, std::vector<SectionRange>& ranges,
                       HkxSection& section) {
  const std::int32_t start = read_i32(bytes, header + 0x14);
  std::array<std::int32_t, 6> marks{};
  for (std::size_t i = 0; i < marks.size(); ++i) marks[i] = read_i32(bytes, header + 0x18 + 4 * i);
  if (start < 0 || marks[0] < 0) return HkxStatus::BadSection;
  if (!std::is_sorted(marks.begin(), marks.end())) return HkxStatus::BadSection;

  const auto base = static_cast<std::size_t>(start);
  const auto mark = [&](std::size_t i) { return static_cast<std::size_t>(marks[i]); };
  if (base > bytes.size() || mark(5) > bytes.size() - base) return HkxStatus::Truncated;

  const SectionRange range{base, base + mark(5)};
  for (const SectionRange& other : ranges) {
    if (std::max(range.begin, other.begin) < std::min(range.end, other.end)) return HkxStatus::BadSection;
  }
  ranges.push_back(range);

  section.tag = read_tag(bytes, header);
  const auto data = bytes.subspan(base, mark(0));
  section.data.assign(data.begin(), data.end());
  if (const auto status = read_local_table(bytes.subspan(base + mark(0), mark(1) - mark(0)), section.local_fixups);
      status != HkxStatus::Ok) {
    return status;
  }
  if (const auto status = read_remote_table(bytes.subspan(base + mark(1), mark(2) - mark(1)), section.global_fixups);
      status != HkxStatus::Ok) {
    return status;
  }
  return read_remote_table(bytes.subspan(base + mark(2), mark(3) - mark(2)), section.virtual_fixups);
}

std::optional<std::size_t> find_section(const std::vector<HkxSection>& sections, std::string_view tag) {
  for (std::size_t i = 0; i < sections.size(); ++i) {
    if (sections[i].tag == tag) return i;
  }
  return std::nullopt;
}

HkxStatus validate_fixups(HkxPackfile& packfile) {
  for (HkxSection& section : packfile.sections) {
    const auto size = static_cast<std::uint64_t>(section.data.size());
    std::sort(section.local_fixups.begin(), section.local_fixups.end(),
              [](const HkxLocalFixup& a, const HkxLocalFixup& b) { return a.source < b.source; });
    for (std::size_t i = 0; i < section.local_fixups.size(); ++i) {
      const HkxLocalFixup& fixup = section.local_fixups[i];
      if (static_cast<std::uint64_t>(fixup.source) + kPointerSize > size || fixup.destination >= size) {
        return HkxStatus::FixupOutOfRange;
      }
      if (i > 0 && section.local_fixups[i - 1].source == fixup.source) return HkxStatus::DuplicateFixup;
    }
    for (const HkxRemoteFixup& fixup : section.global_fixups) {
      if (static_cast<std::uint64_t>(fixup.source) + kPointerSize > size) return HkxStatus::FixupOutOfRange;
      if (fixup.section >= packfile.sections.size()) return HkxStatus::FixupOutOfRange;
      if (fixup.destination >= packfile.sections[fixup.section].data.size()) return HkxStatus::FixupOutOfRange;
    }
  }
  return HkxStatus::Ok;
}

HkxStatus build_objects(HkxPackfile& packfile) {
  const HkxSection& names = packfile.sections[packfile.class_names_section];
  HkxSection& data = packfile.sections[packfile.data_section];
  auto& virtuals = data.virtual_fixups;
  std::sort(virtuals.begin(), virtuals.end(),
            [](const HkxRemoteFixup& a, const HkxRemoteFixup& b) { return a.source < b.source; });

  std::vector<HkxObjectView> objects;
  objects.reserve(virtuals.size());
  for (std::size_t i = 0; i < virtuals.size(); ++i) {
    const HkxRemoteFixup& fixup = virtuals[i];
    if (fixup.section != packfile.class_names_section || fixup.source >= data.data.size()) {
      return HkxStatus::FixupOutOfRange;
    }
    if (i > 0 && virtuals[i - 1].source == fixup.source) return HkxStatus::DuplicateFixup;
    if (fixup.destination >= names.data.size()) return HkxStatus::FixupOutOfRange;
    if (fixup.destination < kClassNamePrefix) return HkxStatus::BadClassName;
    const auto name = cstring_at(names.data, fixup.destination);
    if (!name || name->empty()) return HkxStatus::BadClassName;
    objects.push_back({fixup.source, read_u32(names.data, fixup.destination - kClassNamePrefix), *name, {}});
  }

  for (std::size_t i = 0; i < objects.size(); ++i) {
    const std::size_t begin = objects[i].offset;
    const std::size_t end = i + 1 < objects.size() ? objects[i + 1].offset : data.data.size();
    objects[i].data = std::span<const std::byte>(data.data).subspan(begin, end - begin);
  }
  packfile.objects = std::move(objects);
  return HkxStatus::Ok;
}

}  // namespace

HkxStatus read_packfile(std::span<const std::byte> bytes, HkxPackfile& out) {
  out = HkxPackfile{};
  if (bytes.size() < kHeaderSize) return HkxStatus::Truncated;
  if (read_u32(bytes, 0) != kMagic0 || read_u32(bytes, 4) != kMagic1) return HkxStatus::BadMagic;
  for (std::size_t i = 0; i < kLayoutRules.size(); ++i) {
    if (std::to_integer<std::uint8_t>(bytes[0x10 + i]) != kLayoutRules[i]) return HkxStatus::UnsupportedLayout;
  }
  const std::int32_t file_version = read_i32(bytes, 0x0C);
  if (file_version != kFileVersion) return HkxStatus::UnsupportedLayout;

  const std::int32_t section_count = read_i32(bytes, 0x14);
  if (section_count < 0 || section_count > kMaxSectionCount) return HkxStatus::BadSection;
  const auto count = static_cast<std::size_t>(section_count);
  const std::size_t table_at = kHeaderSize + read_u16(bytes, 0x3E);
  if (table_at > bytes.size() || (bytes.size() - table_at) / kSectionHeaderSize < count) {
    return HkxStatus::Truncated;
  }

  out.user_tag = read_i32(bytes, 0x08);
  out.file_version = file_version;
  out.pointer_size = static_cast<std::uint8_t>(kPointerSize);
  std::vector<SectionRange> ranges;
  for (std::size_t i = 0; i < count; ++i) {
    HkxSection section;
    if (const auto status = read_section(bytes, table_at + i * kSectionHeaderSize, ranges, section);
        status != HkxStatus::Ok) {
      return status;
    }
    out.sections.push_back(std::move(section));
  }

  const auto class_names = find_section(out.sections, "__classnames__");
  const auto data = find_section(out.sections, "__data__");
  if (!class_names || !data) return HkxStatus::MissingSection;
  out.class_names_section = *class_names;
  out.data_section = *data;

  if (const auto status = validate_fixups(out); status != HkxStatus::Ok) return status;
  return build_objects(out);
}

std::optional<std::string_view> cstring_at(std::span<const std::byte> bytes, std::size_t at) {
  const std::size_t limit = std::min(bytes.size(), at + kMaxCStringScan);
  for (std::size_t end = at; end < limit; ++end) {
    if (bytes[end] == std::byte{0}) {
      return std::string_view(reinterpret_cast<const char*>(bytes.data() + at), end - at);
    }
  }
  return std::nullopt;
}

}  // namespace fo4::hkx
