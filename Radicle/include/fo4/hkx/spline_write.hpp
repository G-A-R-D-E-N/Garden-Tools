#pragma once

#include "fo4/hkx/packfile.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace fo4::hkx {

// Invariant: samples are frame-major, so sample t of frame f sits at index f * track_count + t.
struct HkxInterleavedAnimation {
  float duration{0.0f};
  std::size_t frame_count{0};
  std::size_t track_count{0};
  std::vector<std::array<float, 3>> translations;
  std::vector<std::array<float, 4>> rotations;
  std::vector<std::array<float, 3>> scales;
};

// Constraint: the channel tolerances are reserved; channel kind is decided by static_tolerance only, as BGS does.
struct HkxSplineOptions {
  float position_tolerance{0.01f};
  float rotation_tolerance{0.001f};
  float scale_tolerance{0.0005f};
  float static_tolerance{1e-6f};
};

struct HkxSplineBlob {
  std::vector<std::byte> data;
  std::vector<std::uint32_t> block_offsets;
  std::size_t tracks{0};
  std::size_t frames{0};
  std::size_t blocks{0};
  std::size_t max_frames_per_block{0};
  std::size_t mask_size{0};
  float duration{0.0f};
  float frame_duration{0.0f};
  float block_duration{0.0f};
  float block_inverse_duration{0.0f};
};

// Requires finite frame-major samples; encodes the per-block stream decode_spline_animation reads, degree-1 splines.
HkxStatus encode_spline_blob(const HkxInterleavedAnimation& animation, const HkxSplineOptions& options,
                             HkxSplineBlob& out);

}  // namespace fo4::hkx
