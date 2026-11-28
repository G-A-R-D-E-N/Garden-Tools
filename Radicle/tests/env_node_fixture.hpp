#pragma once

#include "byte_helpers.hpp"
#include "fo4/c_api.h"
#include "fo4/environment/terrain.hpp"
#include "fo4/lod/lod.hpp"
#include "plugin_bytes.hpp"

#if __has_include(<gtest/gtest.h>)
#include <gtest/gtest.h>
#endif

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <initializer_list>
#include <iterator>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

namespace fo4::test::envnode {

struct Sub {
  std::string signature;
  std::vector<std::byte> data;
};

struct Node {
  bool group{false};
  std::string signature;
  std::uint32_t form_id{0};
  std::uint32_t flags{0};
  std::vector<Sub> subrecords;
  std::uint16_t form_version{131};
  std::uint32_t label{0};
  std::int32_t group_type{0};
  std::uint16_t timestamp{0};
  std::vector<Node> children;
};

inline Node rec(std::string signature, std::uint32_t form_id, std::vector<Sub> subrecords = {},
         std::uint32_t flags = 0, std::uint16_t form_version = 131,
         std::uint16_t timestamp = 0) {
  Node node;
  node.signature = std::move(signature);
  node.form_id = form_id;
  node.flags = flags;
  node.form_version = form_version;
  node.timestamp = timestamp;
  node.subrecords = std::move(subrecords);
  return node;
}

inline Node grp(std::uint32_t label, std::int32_t type, std::vector<Node> children) {
  Node node;
  node.group = true;
  node.label = label;
  node.group_type = type;
  node.children = std::move(children);
  return node;
}

inline Node top(std::string_view signature, std::vector<Node> children) {
  std::uint32_t label = 0;
  std::memcpy(&label, signature.data(), 4);
  return grp(label, 0, std::move(children));
}

inline std::vector<std::byte> bytes_of(std::initializer_list<std::uint32_t> words) {
  fo4::test::ByteWriter out;
  for (const auto word : words) out.u32(word);
  return out.bytes;
}

inline std::vector<std::byte> floats(std::initializer_list<float> values) {
  fo4::test::ByteWriter out;
  for (const auto value : values) out.f32(value);
  return out.bytes;
}

inline std::vector<std::byte> zstring(std::string_view text) { return fo4::test::zstr(text); }

inline void write_node(fo4::test::ByteWriter& out, const Node& node) {
  if (node.group) {
    std::string label(4, '\0');
    std::memcpy(label.data(), &node.label, 4);
    const auto header = fo4::test::begin_group(out, label, node.group_type);
    for (const auto& child : node.children) write_node(out, child);
    fo4::test::end_group(out, header);
    return;
  }
  const auto header = fo4::test::begin_record(out, node.signature, node.flags, node.form_id, node.form_version);
  for (const auto& sub : node.subrecords) fo4::test::write_subrecord(out, sub.signature, sub.data);
  // Timestamp invariant: fixtures that need staleness must be able to stamp the header date.
  if (node.timestamp != 0) out.patch_u32(header + 16, node.timestamp);
  fo4::test::end_record(out, header);
}

inline std::vector<std::byte> make_plugin(const std::vector<std::string>& masters,
                                   const std::vector<Node>& groups) {
  fo4::test::ByteWriter out;
  const auto tes4 = fo4::test::begin_record(out, "TES4", 0, 0);
  fo4::test::ByteWriter hedr;
  hedr.f32(1.0f);
  hedr.u32(0);
  hedr.u32(0x900);
  fo4::test::write_subrecord(out, "HEDR", hedr.bytes);
  for (const auto& master : masters) {
    fo4::test::write_subrecord(out, "MAST", zstring(master));
    fo4::test::ByteWriter data;
    data.u32(0);
    data.u32(0);
    fo4::test::write_subrecord(out, "DATA", data.bytes);
  }
  fo4::test::end_record(out, tes4);
  for (const auto& group : groups) write_node(out, group);
  return out.bytes;
}

}

namespace fo4::test::envnode::capi_lod {

inline std::vector<std::byte> xclc(std::int32_t x, std::int32_t y) {
  fo4::test::ByteWriter out;
  out.i32(x);
  out.i32(y);
  return out.bytes;
}

inline std::vector<std::byte> vhgt(float offset) {
  fo4::test::ByteWriter out;
  out.f32(offset);
  for (std::size_t index = 0; index < fo4::env::k_land_vertices; ++index) out.u8(0);
  out.u8(0);
  out.u8(0);
  out.u8(0);
  return out.bytes;
}

inline std::vector<std::byte> plugin_bytes() {
  std::vector<Node> cells{rec("CELL", 0x802, {{"XCLC", xclc(0, 0)}}),
                          grp(0x802, 6, {grp(0x802, 9, {rec("LAND", 0x812, {{"VHGT", vhgt(10.0f)}})})})};
  return make_plugin({}, {top("WRLD", {rec("WRLD", 0x800, {{"EDID", zstring("TestWorld")}}),
                                       grp(0x800, 1, {grp(0, 4, {grp(0, 5, std::move(cells))})})})});
}

inline void write_bytes(const std::filesystem::path& path, std::span<const std::byte> bytes) {
  std::error_code ignored;
  std::filesystem::create_directories(path.parent_path(), ignored);
  std::ofstream out(path, std::ios::binary | std::ios::trunc);
  out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
}

inline std::vector<std::byte> read_bytes(const std::filesystem::path& path) {
  std::ifstream input(path, std::ios::binary);
  std::vector<char> raw((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
  std::vector<std::byte> out(raw.size());
  if (!raw.empty()) std::memcpy(out.data(), raw.data(), raw.size());
  return out;
}

struct Fixture {
  std::filesystem::path root;
  std::string plugin_paths;
  std::string data_dir;
  Fixture() {
    const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    root = std::filesystem::temp_directory_path() / ("garden_c_api_lod_" + std::to_string(stamp));
    std::error_code ignored;
    std::filesystem::remove_all(root, ignored);
    std::filesystem::create_directories(root / "LODSettings");
    write_bytes(root / "Test.esp", plugin_bytes());
    write_bytes(root / "LODSettings" / "TestWorld.LOD",
                fo4::lod::serialize_lod_settings(fo4::lod::LodSettings{0, 0, 64, 4, 32}));
    plugin_paths = (root / "Test.esp").string();
    data_dir = root.string();
  }
  ~Fixture() {
    std::error_code ignored;
    std::filesystem::remove_all(root, ignored);
  }
};

inline void release(uint8_t* bytes) {
  if (bytes != nullptr) radicle_free_bytes(bytes);
}

inline std::string text_of(uint8_t* bytes, size_t size) {
  if (bytes == nullptr || size == 0) return std::string{};
  return std::string(reinterpret_cast<const char*>(bytes), size);
}

#if __has_include(<gtest/gtest.h>)
// Invariant: the generate summary lists only the counts that happened, so no counter may be serialised as zero.
// Detects: a reader that cannot tell an omitted count from a zero, and a summary that grew back its zero fields.
inline void expect_absent_count(const std::string& summary, std::string_view name) {
  EXPECT_EQ(summary.find("\"" + std::string{name} + "\":"), std::string::npos) << summary;
}
#endif

}  // namespace fo4::test::envnode::capi_lod
