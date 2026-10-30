#pragma once

#include "fo4/hkx/layout.hpp"
#include "fo4/hkx/packfile.hpp"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
#include <utility>
#include <vector>

namespace fo4::test::hkx {

struct TestSection {
  std::string tag;
  std::vector<std::byte> data;
  std::vector<std::uint32_t> local;
  std::vector<std::uint32_t> global;
  std::vector<std::uint32_t> virtuals;
};

inline void put_u32(std::vector<std::byte>& out, std::uint32_t value) {
  for (int shift = 0; shift < 32; shift += 8) out.push_back(static_cast<std::byte>((value >> shift) & 0xFF));
}

inline void put_words(std::vector<std::byte>& out, const std::vector<std::uint32_t>& words) {
  for (std::uint32_t word : words) put_u32(out, word);
}

inline void patch_u32(std::vector<std::byte>& bytes, std::size_t at, std::uint32_t value) {
  for (std::size_t i = 0; i < 4; ++i) bytes[at + i] = static_cast<std::byte>((value >> (8 * i)) & 0xFF);
}

// Requires room for the pointer, count and capacity of one array member.
inline void reserve_array(std::vector<std::byte>& bytes, std::size_t field) {
  if (bytes.size() < field + 12) bytes.resize(field + 12, std::byte{0});
}

inline void set_count(std::vector<std::byte>& bytes, std::size_t field, std::size_t count) {
  reserve_array(bytes, field);
  patch_u32(bytes, field + 8, static_cast<std::uint32_t>(count));
}

inline std::vector<std::byte> build_packfile(const std::vector<TestSection>& sections) {
  std::vector<std::byte> header;
  put_u32(header, 0x57E0E057u);
  put_u32(header, 0x10C0C010u);
  put_u32(header, 0);
  put_u32(header, 11);
  const std::uint8_t rules[] = {8, 1, 0, 1};
  for (std::uint8_t rule : rules) header.push_back(static_cast<std::byte>(rule));
  put_u32(header, static_cast<std::uint32_t>(sections.size()));
  header.resize(0x3E, std::byte{0});
  header.push_back(std::byte{16});
  header.push_back(std::byte{0});
  header.resize(0x50, std::byte{0});

  std::vector<std::byte> table;
  std::vector<std::byte> bodies;
  const std::size_t bodies_at = 0x50 + sections.size() * 0x40;
  for (const TestSection& section : sections) {
    const auto start = static_cast<std::uint32_t>(bodies_at + bodies.size());
    const auto data_end = static_cast<std::uint32_t>(section.data.size());
    const auto local_end = data_end + static_cast<std::uint32_t>(section.local.size() * 4);
    const auto global_end = local_end + static_cast<std::uint32_t>(section.global.size() * 4);
    const auto virtual_end = global_end + static_cast<std::uint32_t>(section.virtuals.size() * 4);
    for (std::size_t i = 0; i < 20; ++i) {
      table.push_back(i < section.tag.size() ? static_cast<std::byte>(section.tag[i]) : std::byte{0});
    }
    put_u32(table, start);
    for (std::uint32_t mark : {data_end, local_end, global_end, virtual_end, virtual_end, virtual_end}) {
      put_u32(table, mark);
    }
    table.resize(table.size() + 16, std::byte{0});
    bodies.insert(bodies.end(), section.data.begin(), section.data.end());
    put_words(bodies, section.local);
    put_words(bodies, section.global);
    put_words(bodies, section.virtuals);
  }
  header.insert(header.end(), table.begin(), table.end());
  header.insert(header.end(), bodies.begin(), bodies.end());
  return header;
}

inline std::uint32_t add_class_name(std::vector<std::byte>& names, std::uint32_t signature, const std::string& name) {
  put_u32(names, signature);
  names.push_back(std::byte{0x09});
  const auto name_at = static_cast<std::uint32_t>(names.size());
  for (char ch : name) names.push_back(static_cast<std::byte>(ch));
  names.push_back(std::byte{0});
  return name_at;
}

inline std::vector<std::byte> single_object_packfile(const std::string& class_name, std::uint32_t signature,
                                                     std::vector<std::byte> data, std::vector<std::uint32_t> local) {
  std::vector<std::byte> names;
  const std::uint32_t name_at = add_class_name(names, signature, class_name);
  return build_packfile({
      TestSection{"__classnames__", names, {}, {}, {}},
      TestSection{"__types__", {}, {}, {}, {}},
      TestSection{"__data__", std::move(data), std::move(local), {}, {0, 0, name_at}},
  });
}

inline std::vector<std::byte> filled(std::size_t size, std::byte value) { return std::vector<std::byte>(size, value); }

inline void put_float(std::vector<std::byte>& out, float value) {
  std::uint32_t bits = 0;
  std::memcpy(&bits, &value, sizeof bits);
  put_u32(out, bits);
}

inline std::size_t member_at(const char* layout, const char* member) {
  return *fo4::hkx::find_member_offset(*fo4::hkx::find_layout(layout), member);
}

inline void link(std::vector<std::uint32_t>& local, std::size_t source, std::size_t destination) {
  local.push_back(static_cast<std::uint32_t>(source));
  local.push_back(static_cast<std::uint32_t>(destination));
}

}  // namespace fo4::test::hkx
