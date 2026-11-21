#pragma once

#include "fo4/hkx/skeleton.hpp"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace fo4::hkx {

struct HkxBoneInfo {
  std::size_t index{0};
  std::string name;
  std::int16_t parent{-1};
  std::size_t depth{0};
  std::vector<std::size_t> children;
  // Constraint: the partner is another bone in this skeleton, never a name guessed from the left/right prefix alone.
  std::string opposite;
};

struct HkxSkeletonDocument {
  std::string name;
  std::vector<HkxBoneInfo> bones;
  std::vector<std::string> roots;
  bool has_reference_pose{false};
  std::vector<std::string> diagnostics;
};

HkxSkeletonDocument skeleton_document(const HkxSkeleton& skeleton);

enum class HkxSkeletonEditKind : std::uint8_t {
  Rename = 0,
  Reparent = 1,
  Remove = 2,
};

struct HkxSkeletonEdit {
  HkxSkeletonEditKind kind{HkxSkeletonEditKind::Rename};
  std::size_t bone{0};
  std::string name;
  std::int16_t parent{-1};
};

struct HkxSkeletonEditReport {
  bool changed{false};
  std::string refusal;
};

// Requires the edit to keep names unique, every parent earlier than its child, and every removed bone childless.
// Invariant: a refused edit leaves `out` untouched, so a caller cannot save a half-applied skeleton.
HkxSkeletonEditReport apply_skeleton_edit(const HkxSkeleton& skeleton, const HkxSkeletonEdit& edit, HkxSkeleton& out);

}  // namespace fo4::hkx
