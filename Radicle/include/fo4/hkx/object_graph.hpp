#pragma once

#include "fo4/hkx/object_view.hpp"
#include "fo4/hkx/packfile.hpp"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace fo4::hkx {

// Invariant: target_object is the object STARTING at the destination, or -1; inner marks a destination inside an object's span.
struct HkxObjectLink {
  std::int32_t source_object{-1};
  std::uint32_t source_offset{0};
  std::string member;
  std::uint32_t target_section{0};
  std::uint32_t target_offset{0};
  std::int32_t target_object{-1};
  std::string target_class;
  bool inner{false};
};

// Invariant: detail.references stays empty; outgoing carries the reference sites.
struct HkxObjectLookup {
  HkxObjectDetail detail;
  std::vector<HkxObjectLink> outgoing;
  std::vector<HkxObjectLink> incoming;
};

// Requires an object index; MissingObject when the index is past the object list.
HkxStatus lookup_object(const HkxPackfile& packfile, std::size_t index, HkxObjectLookup& out);

struct HkxTreeQuery {
  // Invariant: depth is clamped to 256 so a hostile file cannot exhaust the stack.
  std::size_t max_depth{32};
  std::size_t max_nodes{2000};
};

struct HkxTreeNode {
  std::size_t object{0};
  std::int32_t parent{-1};
  std::size_t depth{0};
  std::string member;
  // Invariant: empty when the class has no layout or no decoded string member; see HkxFieldDecoder::summary.
  std::string summary;
  bool repeated{false};
  bool cut{false};
};

struct HkxObjectTree {
  std::size_t object_count{0};
  std::size_t unreachable{0};
  bool truncated{false};
  std::vector<std::size_t> roots;
  std::vector<HkxTreeNode> nodes;
};

HkxStatus build_object_tree(const HkxPackfile& packfile, const HkxTreeQuery& query, HkxObjectTree& out);

struct HkxSectionDiagnostic {
  std::string tag;
  std::size_t size{0};
  std::size_t local_fixups{0};
  std::size_t global_fixups{0};
  std::size_t virtual_fixups{0};
};

enum class HkxClassLayoutStatus : std::uint8_t { Match = 0, SignatureMismatch, NoLayout };

struct HkxClassDiagnostic {
  std::string name;
  std::uint32_t signature{0};
  std::size_t count{0};
  HkxClassLayoutStatus status{HkxClassLayoutStatus::NoLayout};
  std::uint32_t layout_size{0};
  std::size_t shortest_span{0};
};

struct HkxPackfileDiagnostics {
  std::int32_t file_version{0};
  std::int32_t user_tag{0};
  std::uint8_t pointer_size{0};
  std::size_t object_count{0};
  std::size_t object_links{0};
  std::size_t inner_links{0};
  std::size_t external_links{0};
  std::size_t stray_links{0};
  std::size_t unreferenced_objects{0};
  std::vector<HkxSectionDiagnostic> sections;
  std::vector<HkxClassDiagnostic> classes;
  std::vector<std::string> issues;
};

HkxStatus diagnose_packfile(const HkxPackfile& packfile, HkxPackfileDiagnostics& out);

}  // namespace fo4::hkx
