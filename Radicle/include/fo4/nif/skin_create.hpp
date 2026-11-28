#pragma once

#include "fo4/nif/nif.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace fo4::nif {

// Constraint: create_skin authors a BSSkin::Instance plus BSSkin::BoneData for a shape.
// Constraint: the shape must be an unskinned BSTriShape.
// Invariant: the vertex layout gains the 12-byte skin record per vertex with zero weights.
// Constraint: Bone::block must name node blocks.
// Constraint: Bone::name must be unique and non-empty.
// Constraint: at most 255 bones fit the per-vertex indices.
std::vector<std::byte> create_skin(const NifFile& nif, std::uint32_t shape, std::int32_t skeleton_root,
                                   const std::vector<Bone>& bones);

// Invariant: dismember field order is Data, Skin Partition, Skeleton Root, bone refs, partitions.
// Constraint: BoneData must be BSSkin::BoneData.
// Constraint: any other bone data is a refusal.
struct DismemberPartition {
  std::uint16_t flags{0};
  std::uint16_t body_part{0};
};

struct DismemberSkin {
  std::int32_t skeleton_root{-1};
  std::int32_t skin_partition_ref{-1};
  std::int32_t bonedata_ref{-1};
  std::vector<std::int32_t> bone_refs;
  std::vector<std::string> bone_names;
  std::vector<DismemberPartition> partitions;
};

DismemberSkin read_dismember_skin(const NifFile& nif, std::uint32_t instance_block);

// Invariant: attach_partitions authors a BSDismemberSkinInstance sharing the bone data.
// Constraint: the skin must be a private BSSkin::Instance.
// Constraint: a shared instance is refused.
std::vector<std::byte> attach_partitions(const NifFile& nif, std::uint32_t shape,
                                         const std::vector<DismemberPartition>& partitions);

// Invariant: adopt_donor_bones extends a skin with the donor's missing bones by name.
// Invariant: adopted bones carry the donor bind transforms.
// Constraint: the donor may be skinned through BSSkin::Instance or BSDismemberSkinInstance.
// Constraint: a shared target instance is refused.
std::vector<std::byte> adopt_donor_bones(const NifFile& nif, std::uint32_t target_shape, std::uint32_t donor_shape,
                                         std::uint32_t* adopted = nullptr);

// Invariant: paint_dab compares vertices in global space.
// Invariant: influence falls off with the cubic smoothstep from the dab center to the radius.
// Constraint: the stroke loop and falloff preview are UI and live outside this call.
struct PaintDab {
  std::array<float, 3> center{0.0f, 0.0f, 0.0f};
  float radius{1.0f};
  std::uint32_t bone{0};
  float weight{1.0f};
  float strength{1.0f};
  bool add{false};
};

std::vector<std::byte> paint_dab(const NifFile& nif, std::uint32_t shape, const PaintDab& dab,
                                 std::uint32_t* changed = nullptr);

}  // namespace fo4::nif
