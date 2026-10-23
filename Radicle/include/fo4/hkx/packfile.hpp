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
  std::vector<std::byte> data;
  std::vector<HkxLocalFixup> local_fixups;
  std::vector<HkxRemoteFixup> global_fixups;
  std::vector<HkxRemoteFixup> virtual_fixups;
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

HkxStatus read_packfile(std::span<const std::byte> bytes, HkxPackfile& out);

std::optional<std::string_view> cstring_at(std::span<const std::byte> bytes, std::size_t at);

}  // namespace fo4::hkx
