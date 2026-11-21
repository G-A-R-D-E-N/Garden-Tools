#pragma once

#include "fo4/hkx/packfile.hpp"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace fo4::hkx {

// Invariant: one field shows at most this many bytes, so a 4 KiB object cannot flood a reply.
inline constexpr std::uint32_t kObjectHexLimit = 16;

struct HkxObjectField {
  std::string name;
  std::uint32_t offset{0};
  std::uint32_t size{0};
  std::string hex;
};

// Constraint: target_object is -1 whenever the destination is not the start of an object this reader indexes.
struct HkxObjectReference {
  std::uint32_t source{0};
  std::string member;
  std::uint32_t target_section{0};
  std::uint32_t target_offset{0};
  std::int32_t target_object{-1};
  std::string target_class;
};

// Invariant: an object whose class has no generated layout still reports its offset, signature and bytes.
struct HkxObjectDetail {
  std::size_t index{0};
  std::uint32_t offset{0};
  std::uint32_t signature{0};
  std::string class_name;
  std::uint32_t layout_size{0};
  std::size_t size{0};
  bool layout_known{false};
  std::vector<HkxObjectField> fields;
  std::vector<HkxObjectReference> references;
};

struct HkxObjectQuery {
  // Requires a prefix of the class name; an empty prefix keeps every class.
  std::string class_prefix;
  bool known_layout_only{false};
  std::size_t limit{64};
};

struct HkxObjectReport {
  std::size_t object_count{0};
  std::size_t matched{0};
  std::vector<std::string> classes;
  std::vector<HkxObjectDetail> objects;
  std::vector<std::string> diagnostics;
};

HkxStatus inspect_objects(const HkxPackfile& packfile, const HkxObjectQuery& query, HkxObjectReport& out);

}  // namespace fo4::hkx
