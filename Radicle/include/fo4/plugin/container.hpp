#pragma once

#include "fo4/flags.hpp"
#include "fo4/headers.hpp"
#include "fo4/plugin_key.hpp"
#include "fo4/signature.hpp"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <variant>
#include <vector>

namespace fo4 {

struct Subrecord {
  Signature signature{};
  std::vector<std::byte> data{};
  bool extended_size{false};
};

struct Record {
  RecordHeader header{};
  FormKey form_key{};
  std::vector<Subrecord> subrecords{};
  std::vector<std::byte> original_bytes{};
  std::vector<std::byte> decompressed_payload{};
  bool compressed{false};
  std::uint32_t uncompressed_size{0};
};

struct Group;

using GroupChild = std::variant<Record, Group>;

struct Group {
  GroupHeader header{};
  std::vector<GroupChild> children{};
  std::vector<std::byte> original_bytes{};
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
  std::vector<std::byte> original_bytes;
};

}  // namespace fo4
