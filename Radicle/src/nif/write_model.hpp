#pragma once

#include "fo4/nif/nif.hpp"

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

namespace fo4::nif::write_detail {

using Bytes = std::vector<std::byte>;

struct Model {
  std::string banner;
  std::uint32_t version{0};
  std::uint32_t user_version{0};
  std::uint32_t bs_version{0};
  std::vector<std::string> export_strings;
  std::vector<std::string> block_types;
  std::vector<std::uint16_t> block_type_index;
  std::vector<Bytes> blocks;
  std::uint32_t max_string_length{0};
  std::vector<std::string> strings;
  std::vector<std::uint32_t> groups;
  std::vector<std::int32_t> roots;
};

Model load_model(const NifFile& nif);
Bytes serialize(const Model& model);
std::string_view block_type(const Model& model, std::uint32_t block);
std::int32_t intern_string(Model& model, const std::string& text);

template <typename T>
T read_int(const Bytes& bytes, std::size_t offset) {
  using Unsigned = std::make_unsigned_t<T>;
  Unsigned value = 0;
  for (std::size_t i = 0; i < sizeof(Unsigned); ++i) {
    const auto byte = static_cast<Unsigned>(std::to_integer<std::uint8_t>(bytes[offset + i]));
    value = static_cast<Unsigned>(value | static_cast<Unsigned>(byte << (8 * i)));
  }
  return static_cast<T>(value);
}

template <typename T>
void write_int(Bytes& bytes, std::size_t offset, T value) {
  using Unsigned = std::make_unsigned_t<T>;
  const auto bits = static_cast<Unsigned>(value);
  for (std::size_t i = 0; i < sizeof(Unsigned); ++i) {
    bytes[offset + i] = static_cast<std::byte>(static_cast<std::uint8_t>(bits >> (8 * i)));
  }
}

template <typename T>
void append_int(Bytes& bytes, T value) {
  const auto offset = bytes.size();
  bytes.resize(offset + sizeof(T));
  write_int(bytes, offset, value);
}

inline void append_text(Bytes& bytes, std::string_view text) {
  const auto* first = reinterpret_cast<const std::byte*>(text.data());
  bytes.insert(bytes.end(), first, first + text.size());
}

inline void append_sized(Bytes& bytes, std::string_view text) {
  append_int(bytes, static_cast<std::uint32_t>(text.size()));
  append_text(bytes, text);
}

}  // namespace fo4::nif::write_detail
