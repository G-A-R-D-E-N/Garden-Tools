#pragma once

#include "fo4/nif/nif.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace fo4::nif {

// Invariant: controller key layouts follow nif.xml KeyGroup order.
// Invariant: quaternion keys follow Num Rotation Keys inline, sharing one layout with anim.hpp readers.
// Invariant: tangent bytes are preserved verbatim here; only anim_keys.cpp decodes them, for sampling.
// Invariant: a plain key is the value, plus forward and backward tangents (quadratic) or 12 TBC bytes (TBC).
// Invariant: a quaternion key never has tangents: 16 bytes, or 28 with its TBC tail.
// Invariant: a text key value is the 4-byte header string-table index (NifFile::strings), never inline text.
// Invariant: exactly one rotation representation is set per block.
inline constexpr std::uint32_t k_key_linear = 1;
inline constexpr std::uint32_t k_key_quadratic = 2;
inline constexpr std::uint32_t k_key_tbc = 3;
inline constexpr std::uint32_t k_key_xyz = 4;
inline constexpr std::uint32_t k_key_const = 5;

struct KeyRecord {
  float time{0.0f};
  // Invariant: the raw value bytes for this key, exactly as stored.
  // Invariant: times are decoded for ordering; values are never interpreted.
  std::vector<std::byte> value;
};

struct KeyChannel {
  // Invariant: 0 means the group is empty; otherwise a KeyType word (1-5).
  std::uint32_t interpolation{0};
  std::vector<KeyRecord> keys;
};

struct NifKeys {
  std::string block_type;  // Invariant: exactly one rotation representation is set.
  // Invariant: quaternions cover types 1, 2, 3 and 5; the XYZ trio covers type 4.
  std::uint32_t rotation_type{0};
  KeyChannel rotation_quat;
  std::array<KeyChannel, 3> rotation_xyz;
  KeyChannel translations;
  KeyChannel scales;
  // Invariant: only one scalar channel below is set, matching the block type.
  KeyChannel data;
};

NifKeys read_controller_keys(const NifFile& nif, std::uint32_t block);

// Constraint: the replacement keeps the block type and every channel layout.
// Constraint: value sizes must match the read shape exactly.
// Constraint: times must be finite and non-decreasing.
// Constraint: text values must be string-table indices the file already holds (or -1).
// Invariant: a text block keeps its own name reference.
// Invariant: interpolator and controller links are untouched.
std::vector<std::byte> write_controller_keys(const NifFile& nif, std::uint32_t block, const NifKeys& keys);

}  // namespace fo4::nif
