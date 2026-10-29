#pragma once

#include "fo4/flags.hpp"
#include "fo4/formid.hpp"
#include "fo4/signature.hpp"

#include <array>
#include <cstddef>
#include <cstdint>

namespace fo4 {

inline constexpr std::size_t k_record_header_size = 24;
inline constexpr std::size_t k_group_header_size = 24;
inline constexpr std::size_t k_subrecord_header_size = 6;

struct RecordHeader {
  Signature signature{};
  std::uint32_t data_size{0};
  RecordFlags flags{};
  RawFormId form_id{};
  std::uint16_t timestamp{0};
  std::uint16_t version_control{0};
  std::uint16_t form_version{0};
  std::uint16_t vc_version{0};
};

struct GroupHeader {
  Signature signature{sig_grup};
  std::uint32_t group_size{0};
  std::array<std::byte, 4> label{};
  std::int32_t group_type{0};
  std::uint16_t timestamp{0};
  std::uint16_t version_control{0};
  std::uint16_t form_version{0};
  std::uint16_t unknown{0};

  Signature top_label() const {
    return Signature{
        static_cast<char>(label[0]),
        static_cast<char>(label[1]),
        static_cast<char>(label[2]),
        static_cast<char>(label[3]),
    };
  }

  std::uint32_t label_as_u32() const {
    return static_cast<std::uint32_t>(std::to_integer<std::uint8_t>(label[0])) |
           (static_cast<std::uint32_t>(std::to_integer<std::uint8_t>(label[1])) << 8) |
           (static_cast<std::uint32_t>(std::to_integer<std::uint8_t>(label[2])) << 16) |
           (static_cast<std::uint32_t>(std::to_integer<std::uint8_t>(label[3])) << 24);
  }

  void set_label_as_u32(std::uint32_t value) {
    for (std::size_t index = 0; index < label.size(); ++index) {
      label[index] = std::byte{static_cast<std::uint8_t>(value >> (8 * index))};
    }
  }
};

struct SubrecordHeader {
  Signature signature{};
  std::uint16_t data_size{0};
};

}  // namespace fo4
