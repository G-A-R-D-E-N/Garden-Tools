#pragma once

#include "fo4/nif/nif.hpp"
#include "fo4/nif/skin.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace fo4::nif {

using Point = std::array<float, 3>;

// Invariant: one VertexWeights is the four-slot skin record, and a slot of weight 0 is empty.
struct VertexWeights {
  std::array<std::uint8_t, k_max_influences_per_vertex> bone{};
  std::array<float, k_max_influences_per_vertex> weight{};
};

enum class BrushMode : std::uint8_t { add, subtract, replace, smooth };
enum class BrushFalloff : std::uint8_t { smoothstep, linear, constant };

struct WeightBrush {
  BrushMode mode{BrushMode::add};
  BrushFalloff falloff{BrushFalloff::smoothstep};
  Point center{0.0f, 0.0f, 0.0f};
  float radius{1.0f};
  float strength{1.0f};
  std::uint32_t bone{0};
  float target{1.0f};
};

struct WeightBrushReport {
  std::size_t in_reach{0};
  std::size_t changed{0};
  std::size_t skipped{0};
  float max_weight_change{0.0f};
};

// Invariant: locked bones keep their exact weights and the unlocked remainder fills what they leave.
VertexWeights pack_influences(std::span<const Influence> influences, std::span<const std::uint8_t> locked_bones = {});

// Requires one position per vertex; triangles are read only by smooth.
WeightBrushReport apply_weight_brush(std::span<const Point> positions, std::span<const std::uint16_t> triangles,
                                     std::span<VertexWeights> vertices, const WeightBrush& brush,
                                     std::span<const std::uint8_t> locked_bones = {});

// Requires a skinned tri-shape block.
std::vector<VertexWeights> read_shape_weights(const NifFile& nif, std::uint32_t shape);

// Requires one record per vertex of a skinned tri-shape block.
std::vector<std::byte> write_shape_weights(const NifFile& nif, std::uint32_t shape,
                                           std::span<const VertexWeights> weights);

// Requires a skinned tri-shape block, and a brush bone inside its skin unless the mode is smooth.
std::vector<std::byte> brush_shape_weights(const NifFile& nif, std::uint32_t shape, const WeightBrush& brush,
                                           std::span<const std::uint8_t> locked_bones = {},
                                           WeightBrushReport* report = nullptr);

}  // namespace fo4::nif
