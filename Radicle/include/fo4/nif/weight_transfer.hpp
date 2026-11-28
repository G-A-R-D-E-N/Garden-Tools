#pragma once

#include "fo4/nif/nif.hpp"
#include "fo4/nif/weight_brush.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace fo4::nif {

// Invariant: triangle is -1 when no triangle lies within max_distance, and every other field is then zero.
struct SurfaceHit {
  std::int32_t triangle{-1};
  std::array<float, 3> bary{};
  Point point{};
  Point normal{};
  float distance{0.0f};
  float signed_distance{0.0f};
};

// Requires triangles that index the positions; max_distance 0 means no limit.
std::vector<SurfaceHit> project_onto_surface(std::span<const Point> targets, std::span<const Point> positions,
                                             std::span<const std::uint16_t> triangles, float max_distance = 0.0f);

enum class TransferMethod : std::uint8_t { nearest_vertex, barycentric };

struct WeightTransferOptions {
  TransferMethod method{TransferMethod::barycentric};
  float max_distance{0.0f};
};

struct WeightTransferReport {
  std::size_t transferred{0};
  std::size_t out_of_reach{0};
  std::size_t unmapped{0};
};

// Invariant: an entry is -1 when the target names the bone zero times or twice, or past index 255.
std::vector<std::int32_t> map_bones_by_name(std::span<const std::string> source, std::span<const std::string> target);

// Requires bone_map to give the target bone of each source bone, and triangles for the barycentric method.
WeightTransferReport transfer_weights(std::span<const Point> targets, std::span<VertexWeights> target_weights,
                                      std::span<const Point> reference, std::span<const std::uint16_t> triangles,
                                      std::span<const VertexWeights> reference_weights,
                                      std::span<const std::int32_t> bone_map, const WeightTransferOptions& options);

enum class ConformMode : std::uint8_t { snap, push_out };

struct ConformOptions {
  ConformMode mode{ConformMode::push_out};
  float offset{0.0f};
  float max_distance{0.0f};
};

struct ConformReport {
  std::size_t moved{0};
  std::size_t out_of_reach{0};
  float max_move{0.0f};
};

// Invariant: the reference normal points out of the body, and the offset is measured along it.
ConformReport conform_to_surface(std::span<Point> positions, std::span<const Point> reference,
                                 std::span<const std::uint16_t> triangles, const ConformOptions& options);

// Requires two skinned tri-shape blocks; a source bone that carries weight must have one same-name target bone.
std::vector<std::byte> transfer_shape_weights(const NifFile& nif, std::uint32_t source_shape,
                                              std::uint32_t target_shape, const WeightTransferOptions& options,
                                              WeightTransferReport* report = nullptr);

// Requires two NIF buffers whose shapes sit in one world frame, because each shape is placed by its own file's chain.
std::vector<std::byte> transfer_shape_weights(std::span<const std::byte> source_nif, std::uint32_t source_shape,
                                              std::span<const std::byte> target_nif, std::uint32_t target_shape,
                                              const WeightTransferOptions& options,
                                              WeightTransferReport* report = nullptr);

// Requires a full-precision target that is not BSDynamicTriShape, and a reference shape in the same world frame.
// Invariant: only moved vertices are rewritten, through the inverse of the global placement, and the vertex count is kept.
// Invariant: a shape with stored normals gets recalc_normals over the whole shape, with tangents where it has UVs.
std::vector<std::byte> conform_shape_to_surface(const NifFile& nif, std::uint32_t target_shape,
                                                std::uint32_t reference_shape, const ConformOptions& options,
                                                ConformReport* report = nullptr);

std::vector<std::byte> conform_shape_to_surface(std::span<const std::byte> reference_nif,
                                                std::uint32_t reference_shape, std::span<const std::byte> target_nif,
                                                std::uint32_t target_shape, const ConformOptions& options,
                                                ConformReport* report = nullptr);

}  // namespace fo4::nif
