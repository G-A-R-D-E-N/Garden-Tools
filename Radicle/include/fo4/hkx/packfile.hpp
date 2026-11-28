#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace fo4::hkx {

enum class HkxStatus : std::uint8_t {
  Ok = 0,
  Truncated,
  BadMagic,
  UnsupportedLayout,
  MissingSection,
  BadSection,
  FixupOutOfRange,
  DuplicateFixup,
  BadClassName,
  MissingObject,
};

struct HkxLocalFixup {
  std::uint32_t source;
  std::uint32_t destination;
};

struct HkxRemoteFixup {
  std::uint32_t source;
  std::uint32_t section;
  std::uint32_t destination;
};

struct HkxSection {
  std::string tag;
  // Requires the section start: alignment rounds against the file offset, not the section data.
  std::size_t file_offset{0};
  std::vector<std::byte> data;
  std::vector<HkxLocalFixup> local_fixups;
  std::vector<HkxRemoteFixup> global_fixups;
  std::vector<HkxRemoteFixup> virtual_fixups;
  // Invariant: the source order of the virtual table as stored in the file, before read_packfile sorts it; true when it was ascending.
  bool virtual_fixups_ascending{true};
};

struct HkxObjectView {
  std::uint32_t offset;
  std::uint32_t signature;
  std::string_view class_name;
  std::span<const std::byte> data;
};

struct HkxPackfile {
  HkxPackfile() = default;
  HkxPackfile(const HkxPackfile&) = delete;
  HkxPackfile& operator=(const HkxPackfile&) = delete;
  HkxPackfile(HkxPackfile&&) = default;
  HkxPackfile& operator=(HkxPackfile&&) = default;

  std::int32_t user_tag{0};
  std::int32_t file_version{0};
  std::uint8_t pointer_size{0};
  std::vector<HkxSection> sections;
  std::size_t class_names_section{0};
  std::size_t data_section{0};
  std::vector<HkxObjectView> objects;
};

// Constraint: reads little-endian, 8-byte-pointer, layout rules 8 1 0 1, file version 11 only; anything else is UnsupportedLayout.
HkxStatus read_packfile(std::span<const std::byte> bytes, HkxPackfile& out);

// Invariant: empty when the header is one read_packfile accepts; else one sentence naming the refused field or a TAG0 tagfile.
std::string describe_packfile_refusal(std::span<const std::byte> bytes);

std::optional<std::string_view> cstring_at(std::span<const std::byte> bytes, std::size_t at);

}  // namespace fo4::hkx
