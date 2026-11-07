#pragma once

#include "fo4/plugin/container.hpp"
#include "fo4/plugin/visit.hpp"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace fo4::test {

inline std::vector<std::byte> original_bytes(const fo4::Plugin& plugin) {
  const auto bytes = fo4::plugin_original_bytes(plugin);
  return {bytes.begin(), bytes.end()};
}

inline std::vector<std::byte> record_original_bytes(const fo4::Plugin& plugin,
                                                     const fo4::Record& record) {
  const auto bytes = fo4::record_original_bytes(plugin, record);
  return {bytes.begin(), bytes.end()};
}

struct RecordState {
  std::vector<std::pair<fo4::Signature, std::vector<std::byte>>> subrecords{};
  fo4::RecordFlags flags{};
  bool original_bytes{false};

  bool operator==(const RecordState&) const = default;
};

struct PluginState {
  bool original_bytes{false};
  std::vector<RecordState> records{};

  bool operator==(const PluginState&) const = default;
};

inline PluginState plugin_state(const fo4::Plugin& plugin) {
  PluginState state{.original_bytes = !plugin.original_range.empty()};
  fo4::visit_records(plugin, [&state](const fo4::Record& record) {
    RecordState captured{.flags = record.header.flags,
                         .original_bytes = !record.original_range.empty()};
    for (const auto& sub : record.subrecords) captured.subrecords.emplace_back(sub.signature, sub.data);
    state.records.push_back(std::move(captured));
  });
  return state;
}

class ByteWriter {
public:
  std::vector<std::byte> bytes;

  void u8(std::uint8_t value) { bytes.push_back(std::byte{value}); }

  void u16(std::uint16_t value) {
    u8(static_cast<std::uint8_t>(value));
    u8(static_cast<std::uint8_t>(value >> 8));
  }

  void u32(std::uint32_t value) {
    u8(static_cast<std::uint8_t>(value));
    u8(static_cast<std::uint8_t>(value >> 8));
    u8(static_cast<std::uint8_t>(value >> 16));
    u8(static_cast<std::uint8_t>(value >> 24));
  }

  void i32(std::int32_t value) { u32(static_cast<std::uint32_t>(value)); }

  void f32(float value) {
    std::uint32_t bits = 0;
    static_assert(sizeof(float) == 4);
    std::memcpy(&bits, &value, 4);
    u32(bits);
  }

  void sig(std::string_view text) {
    for (std::size_t i = 0; i < 4; ++i) {
      u8(i < text.size() ? static_cast<std::uint8_t>(text[i]) : 0);
    }
  }

  void raw(std::span<const std::byte> data) {
    bytes.insert(bytes.end(), data.begin(), data.end());
  }

  void raw(std::string_view data) {
    for (char ch : data) {
      u8(static_cast<std::uint8_t>(ch));
    }
  }

  std::size_t size() const { return bytes.size(); }

  void patch_u32(std::size_t offset, std::uint32_t value) {
    bytes[offset] = std::byte{static_cast<std::uint8_t>(value)};
    bytes[offset + 1] = std::byte{static_cast<std::uint8_t>(value >> 8)};
    bytes[offset + 2] = std::byte{static_cast<std::uint8_t>(value >> 16)};
    bytes[offset + 3] = std::byte{static_cast<std::uint8_t>(value >> 24)};
  }
};

void write_subrecord(ByteWriter& out, std::string_view signature, std::span<const std::byte> data);
void write_subrecord(ByteWriter& out, std::string_view signature, std::string_view data);
void write_xxxx_subrecord(ByteWriter& out, std::string_view signature,
                          std::span<const std::byte> data);

std::size_t begin_record(ByteWriter& out, std::string_view signature, std::uint32_t flags,
                         std::uint32_t form_id, std::uint16_t form_version = 131);
void end_record(ByteWriter& out, std::size_t header_offset);

std::size_t begin_group(ByteWriter& out, std::string_view label, std::int32_t group_type);
void end_group(ByteWriter& out, std::size_t header_offset);

std::vector<std::byte> make_minimal_plugin();
std::vector<std::byte> make_tes4_plugin(std::uint32_t flags, float version,
                                        const std::vector<std::string>& masters);
std::vector<std::byte> zlib_compress(std::span<const std::byte> data);

}  // namespace fo4::test
