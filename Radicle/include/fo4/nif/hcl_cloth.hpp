#pragma once

#include "fo4/nif/hknp_decode.hpp"
#include "fo4/nif/hknp_packfile.hpp"

#include <array>
#include <cstddef>
#include <string>
#include <vector>

namespace fo4::nif {

struct HclCollidable {
  std::size_t object{k_no_object};
  std::string name;
  // Constraint: transform holds the three rotation rows and then the translation, each as the four words stored.
  std::array<HknpVec4, 4> transform{};
  float pinch_detection_radius{0.0f};
  // Invariant: shape_object is the global fixup of the shape pointer, or k_no_object when it has none.
  std::size_t shape_object{k_no_object};
};

struct HclCapsuleShape {
  std::size_t object{k_no_object};
  HknpVec3 start{};
  HknpVec3 end{};
  HknpVec3 dir{};
  float radius{0.0f};
  float cap_len_sqr_inv{0.0f};
};

struct HclTaperedCapsuleShape {
  std::size_t object{k_no_object};
  float small_radius{0.0f};
  float big_radius{0.0f};
  float length{0.0f};
  float depth{0.0f};
};

struct HclCloth {
  std::vector<HclCollidable> collidables;
  std::vector<HclCapsuleShape> capsules;
  std::vector<HclTaperedCapsuleShape> tapered_capsules;
  // Invariant: bone_names has one entry per bone of the first hkaSkeleton, and an empty string where the bone has no name pointer.
  std::vector<std::string> bone_names;
};

// Requires each typed object to hold its class size; a truncated object or a name running past its object raises fo4::parse_error.
HclCloth decode_hcl_cloth(const HknpPackfile& pack);

}  // namespace fo4::nif
