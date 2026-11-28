#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace fo4::nif {

struct SclpBone;

// Invariant: fit_sclp_scales computes per-bone armor-over-underarmor scales for .sclp files.
// Constraint: inputs are parent-relative bind offsets in the same units.
// Invariant: each axis scales as fitted over reference.
// Invariant: a near-zero reference axis stays 1.0.
// Invariant: all-1.0 bones are skipped because the file only needs scaled bones.
// Constraint: a non-positive or non-finite ratio is refused.
struct FitBone {
  std::string name;
  std::array<float, 3> reference{0.0f, 0.0f, 0.0f};
  std::array<float, 3> fitted{0.0f, 0.0f, 0.0f};
};

struct FitScales {
  std::vector<SclpBone> bones;
  // Invariant: every skipped entry reads "<bone>: <reason>".
  std::vector<std::string> skipped;
};

FitScales fit_sclp_scales(const std::vector<FitBone>& bones, float epsilon = 1e-6f);

}  // namespace fo4::nif
