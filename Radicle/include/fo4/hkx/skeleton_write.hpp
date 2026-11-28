#pragma once

#include "fo4/hkx/packfile.hpp"
#include "fo4/hkx/skeleton.hpp"
#include "fo4/hkx/skeleton_document.hpp"

#include <cstddef>
#include <span>
#include <string>
#include <vector>

namespace fo4::hkx {

struct HkxSkeletonWrite {
  std::vector<std::byte> bytes;
  bool changed{false};
  std::string refusal;
};

// Invariant: a refused list leaves `out` untouched and names the failing edit by its 1-based position.
HkxSkeletonEditReport apply_skeleton_edits(const HkxSkeleton& skeleton, std::span<const HkxSkeletonEdit> edits,
                                           HkxSkeleton& out, std::vector<std::size_t>* origin = nullptr);

// Invariant: any status other than Ok leaves out.bytes empty; Ok with changed false means the edits changed nothing.
HkxStatus write_skeleton(std::span<const std::byte> source, std::span<const HkxSkeletonEdit> edits,
                         HkxSkeletonWrite& out);

}  // namespace fo4::hkx
