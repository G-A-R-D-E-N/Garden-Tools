#pragma once

#include "fo4/flags.hpp"
#include "fo4/headers.hpp"
#include "fo4/plugin_key.hpp"
#include "fo4/signature.hpp"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <variant>
#include <vector>

namespace fo4 {

struct OriginalByteRange {
  const std::vector<std::byte>* owner{nullptr};
  std::size_t offset{0};
  std::size_t size{0};

  [[nodiscard]] constexpr bool empty() const noexcept { return size == 0; }
  constexpr void clear() noexcept {
    owner = nullptr;
    offset = 0;
    size = 0;
  }
};

struct Subrecord {
  Signature signature{};
  std::vector<std::byte> data{};
  bool extended_size{false};
};

struct Record {
  RecordHeader header{};
  FormKey form_key{};
  std::vector<Subrecord> subrecords{};
  OriginalByteRange original_range{};
  std::vector<std::byte> decompressed_payload{};
  bool compressed{false};
  bool undecodable{false};
  std::uint32_t uncompressed_size{0};
};

inline bool is_partial_form(const Record& record) {
  const auto signature = record.header.signature.view();
  return record.header.flags.test(RecordFlags::partial_form) &&
         (signature == "CELL" || signature == "DIAL" || signature == "QUST" || signature == "WRLD");
}

struct Group;

using GroupChild = std::variant<Record, Group>;

struct Group {
  GroupHeader header{};
  std::vector<GroupChild> children{};
};

struct MasterReference {
  PluginKey key;
  PluginKind kind{PluginKind::full};
  std::uint64_t data_size{0};
  std::vector<std::byte> data_bytes{};
};

struct PluginHeader {
  float version{0.0f};
  std::uint32_t record_count{0};
  std::uint32_t next_form_id{0};
  std::string author;
  std::string description;
  std::vector<MasterReference> masters;
  std::vector<RawFormId> overrides;
  std::optional<std::uint32_t> intv;
  std::optional<std::uint32_t> incc;
  RecordFlags tes4_flags{};
  std::uint16_t form_version{0};
};

struct Plugin {
  std::filesystem::path path;
  PluginKey key;
  Record tes4;
  PluginHeader header;
  std::vector<Group> groups;
  std::shared_ptr<const std::vector<std::byte>> original_data;
  OriginalByteRange original_range;
  std::uint64_t original_structure_fingerprint{0};
};

[[nodiscard]] std::span<const std::byte> plugin_original_bytes(const Plugin& plugin);
[[nodiscard]] std::span<const std::byte> record_original_bytes(const Plugin& plugin,
                                                                const Record& record);

}  // namespace fo4
