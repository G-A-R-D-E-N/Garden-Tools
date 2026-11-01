#pragma once

#include "fo4/nif/hknp_decode.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

namespace fo4::nif::hknp_detail {

inline constexpr int k_shape_unvisited = -2;
inline constexpr int k_shape_in_progress = -3;
inline constexpr int k_shape_unsupported = -1;
// Constraint: the capsule radius is the stored convex radius divided by 0.9801.
inline constexpr float k_capsule_core_factor = 0.9801f;

struct ArrayRef {
  std::size_t offset{0};
  std::uint32_t count{0};
};

struct DecodeContext {
  const HknpPackfile& pack;
  HknpSystem& sys;
  // Invariant: one entry per pack object, holding k_shape_* or the index of the decoded shape in sys.shapes.
  std::vector<int> shape_of_object;
  int depth{0};
};

std::uint8_t u8_at(const HknpPackObject& object, std::size_t offset);
std::uint16_t u16_at(const HknpPackObject& object, std::size_t offset);
std::uint32_t u32_at(const HknpPackObject& object, std::size_t offset);
float f32_at(const HknpPackObject& object, std::size_t offset);
HknpVec3 vec3_at(const HknpPackObject& object, std::size_t offset);
HknpVec4 vec4_at(const HknpPackObject& object, std::size_t offset);
// Constraint: a packed vector is three int16 mantissas over 32768, scaled by 2 to the exponent in the u16 at offset + 6.
HknpVec3 packed_vec3_at(const HknpPackObject& object, std::size_t offset);

// Invariant: a local fixup target past the end of its object raises fo4::parse_error.
std::optional<std::size_t> local_target(const HknpPackObject& object, std::size_t source);
// Invariant: a global fixup naming no object of the packfile raises fo4::parse_error.
std::optional<std::size_t> global_target(const HknpPackfile& pack, const HknpPackObject& object, std::size_t source);
// Constraint: an hkArray descriptor keeps its payload pointer at field and its count at field + 8.
std::optional<ArrayRef> array_at(const HknpPackObject& object, std::size_t field);
// Constraint: a self-relative array keeps its count as u16 at field and its payload offset as u16 at field + 2.
ArrayRef rel_array_at(const HknpPackObject& object, std::size_t field);

int decode_shape(DecodeContext& ctx, std::size_t object);

}  // namespace fo4::nif::hknp_detail
