#pragma once

#include "fo4/hkx/packfile.hpp"

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace fo4::hkx {

// Invariant: object is -1 when the variant pointer does not land on the start of an indexed object.
struct HkxSceneVariant {
  std::string name;
  std::string declared_class;
  std::string class_name;
  std::int32_t object{-1};
  std::uint32_t signature{0};
  bool layout_known{false};
};

// Invariant: a scene-level class carries no decoded member, because none of these layouts is verified.
struct HkxSceneClass {
  std::string class_name;
  std::size_t object{0};
  std::uint32_t signature{0};
  std::size_t size{0};
  bool layout_known{false};
};

struct HkxSceneStructure {
  std::size_t object_count{0};
  bool has_root{false};
  std::vector<HkxSceneVariant> variants;
  std::vector<HkxSceneClass> scene_objects;
  std::vector<std::string> unknown_classes;
  std::vector<std::string> diagnostics;
};

bool is_scene_class(std::string_view class_name);

// Requires a reader-accepted packfile; the variant table is read only through the generated root-container layouts.
HkxStatus read_scene_structure(const HkxPackfile& packfile, HkxSceneStructure& out);

}  // namespace fo4::hkx
