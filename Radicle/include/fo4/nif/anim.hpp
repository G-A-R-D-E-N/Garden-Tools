#pragma once

#include "fo4/nif/nif.hpp"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace fo4::nif {

// Invariant: KeyType values are nif.xml KeyType (1 linear, 2 quadratic, 3 TBC, 4 XYZ-rot, 5 const).
enum class AnimKeyType : std::uint32_t {
  Linear = 1,
  Quadratic = 2,
  Tbc = 3,
  XyzRot = 4,
  Const = 5,
};

// Invariant: CycleType values are nif.xml CycleType (0 loop, 1 reverse, 2 clamp).
enum class AnimCycleType : std::uint32_t {
  Loop = 0,
  Reverse = 1,
  Clamp = 2,
};

// Invariant: forward and backward are filled only for quadratic keys and tbc (tension, continuity, bias in file order) only for TBC keys.
struct AnimKeyVec3 {
  float time{0.0f};
  float value[3]{};
  float forward[3]{};
  float backward[3]{};
  float tbc[3]{};
};

struct AnimKeyFloat {
  float time{0.0f};
  float value{0.0f};
  float forward{0.0f};
  float backward{0.0f};
  float tbc[3]{};
};

struct AnimKeyQuat {
  float time{0.0f};
  float value[4]{1.0f, 0.0f, 0.0f, 0.0f};
};

struct AnimControlledBlock {
  std::string node_name;
  std::string property_type;
  std::string controller_type;
  std::string controller_id;
  std::string interpolator_id;
  std::int32_t interpolator_ref{-1};
  std::int32_t controller_ref{-1};
  std::uint8_t priority{0};
};

struct AnimSequence {
  std::uint32_t block{0};
  std::string name;
  float weight{1.0f};
  std::int32_t text_keys_ref{-1};
  AnimCycleType cycle{AnimCycleType::Clamp};
  float frequency{1.0f};
  float start_time{0.0f};
  float stop_time{0.0f};
  std::int32_t manager_ref{-1};
  std::string accum_root_name;
  std::vector<std::int32_t> anim_note_refs;
  std::vector<AnimControlledBlock> controlled;
};

struct AnimManager {
  std::uint32_t block{0};
  bool cumulative{false};
  std::vector<std::uint32_t> sequences;
  std::int32_t object_palette_ref{-1};
};

struct AnimTextKey {
  float time{0.0f};
  std::string text;
};

struct AnimBehaviourGraph {
  std::uint32_t block{0};
  std::string graph_file;
  bool controls_base_skeleton{false};
};

struct AnimTransformPose {
  float translation[3]{};
  float rotation[4]{1.0f, 0.0f, 0.0f, 0.0f};
  float scale{1.0f};
};

struct AnimNodePose {
  std::string node_name;
  AnimTransformPose pose;
};

struct TransformKeys {
  AnimKeyType rotation_type{AnimKeyType::Linear};
  std::vector<AnimKeyQuat> rotation_quat;
  std::vector<AnimKeyFloat> rotation_x;
  std::vector<AnimKeyFloat> rotation_y;
  std::vector<AnimKeyFloat> rotation_z;
  AnimKeyType translation_type{AnimKeyType::Linear};
  std::vector<AnimKeyVec3> translation;
  AnimKeyType scale_type{AnimKeyType::Linear};
  std::vector<AnimKeyFloat> scale;
  AnimTransformPose pose{};
  bool has_data{false};
};

struct AnimValueKey {
  float time{0.0f};
  float value[3]{};
  float forward[3]{};
  float backward[3]{};
  float tbc[3]{};
};

// Invariant: components is 1 for NiFloatInterpolator and 3 for NiPoint3Interpolator; a float key fills value[0] only.
// Invariant: interpolation is the nif.xml KeyType word, 0 when there are no keys; forward, backward and tbc stay zero unless that type carries them.
struct ValueKeys {
  std::uint32_t components{0};
  std::uint32_t interpolation{0};
  std::int32_t data_ref{-1};
  float pose[3]{};
  std::vector<AnimValueKey> keys;
};

// Requires FO4 20.2.0.7 NIF; throws parse_error when a block layout does not match nif.xml.
std::vector<AnimManager> read_anim_managers(const NifFile& nif);
// Requires FO4 20.2.0.7 NIF; throws parse_error when a block layout does not match nif.xml.
std::vector<AnimSequence> read_anim_sequences(const NifFile& nif);
// Requires a NiTextKeyExtraData block; throws parse_error for any other block.
std::vector<AnimTextKey> read_text_keys(const NifFile& nif, std::uint32_t block);
// Requires a BSBehaviorGraphExtraData block; throws parse_error for any other block.
AnimBehaviourGraph read_behaviour_graph(const NifFile& nif, std::uint32_t block);
// Requires a NiTransformInterpolator or NiFloatInterpolator block with its data block present.
TransformKeys read_transform_keys(const NifFile& nif, std::uint32_t interpolator);
// Requires a NiFloatInterpolator or NiPoint3Interpolator block; its data block must be NiFloatData or NiPosData when present.
ValueKeys read_value_keys(const NifFile& nif, std::uint32_t interpolator);
// Requires a NiControllerSequence block; samples every controlled block that resolves to transform keys.
std::vector<AnimNodePose> evaluate_sequence(const NifFile& nif, std::uint32_t sequence_block, float time);

}  // namespace fo4::nif
