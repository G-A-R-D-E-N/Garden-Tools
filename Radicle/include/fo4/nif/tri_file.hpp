#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace fo4::nif {

struct TriMorph {
  std::string name;
  float scale{0.0f};
  std::vector<std::int16_t> deltas;
};

// Invariant: positions are 3 floats per vertex, each morph 3 int16 per vertex; delta = int16 * scale.
struct TriFile {
  std::uint32_t vertex_count{0};
  std::uint32_t triangle_count{0};
  std::vector<float> positions;
  std::vector<TriMorph> morphs;

  const TriMorph* find(std::string_view name) const;
};

// Constraint: only the shipped FRTRI003 shape is read; anything else is a parse_error.
TriFile parse_tri(std::span<const std::byte> bytes);

std::vector<float> tri_morph_deltas(const TriMorph& morph);

// Invariant: engine frame is (x, z, -y) / units_per_metre; 0 selects k_hknp_game_units_per_metre.
std::vector<float> tri_to_engine_frame(std::span<const float> xyz, float units_per_metre);

}  // namespace fo4::nif
