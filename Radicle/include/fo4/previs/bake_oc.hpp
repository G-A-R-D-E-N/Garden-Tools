#pragma once

#include <fo4/previs/shared_geom_extra.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <span>
#include <string>
#include <vector>

namespace fo4::previs {

enum class BakeOcStatus {
  ok,
  io_error,
  invalid_input,
  unsupported,
};

bool oc_nif_has_shared_combined_geom(std::span<const std::byte> nif_bytes);

bool stat_mesh_nif(std::span<const std::byte> nif_bytes);

BakeOcStatus bake_oc_single_source(const std::filesystem::path& source_nif,
                                   const std::filesystem::path& output_oc_nif);

BakeOcStatus bake_oc_single_source_bytes(std::span<const std::byte> source,
                                         const std::filesystem::path& output_oc_nif);

struct OcPlacementTransform {
  float translation[3]{};
  float rotation[9]{1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f};
  float scale{1.0f};
};

OcPlacementTransform placement_transform_from_euler(float x, float y, float z, float rx, float ry, float rz,
                                                    float scale = 1.0f);

struct OcPlacement {
  std::filesystem::path source_nif;
  OcPlacementTransform xform{};
};

struct BakeOcPlacementsRequest {
  std::filesystem::path template_oc_nif;
  std::vector<OcPlacement> placements;
  // Constraint: a patched object addresses the patch's own container, so the hash and the object's offset come from the caller.
  std::uint32_t csg_file_hash{0};
  std::uint32_t csg_data_offset{0};
  // Constraint: each record's bound is the source STAT's own sphere, measured on the mesh rather than copied from the template.
  float local_center[3]{};
  float local_radius{0.0f};
};

BakeOcStatus bake_oc_from_placements(const BakeOcPlacementsRequest& request,
                                     const std::filesystem::path& output_oc_nif);

BakeOcStatus bake_oc_author_from_stat_shell(const std::filesystem::path& stat_mesh,
                                            const std::filesystem::path& oc_shell,
                                            std::span<const OcPlacement> placements,
                                            const std::filesystem::path& output_oc_nif);

// Constraint: the object addresses the patch's own CSG, so the _OC is self-contained once it ships.
struct OcObjectMesh {
  std::uint64_t vertex_desc{0};
  std::uint32_t vertex_count{0};
  // Constraint: the _OC object carries one triangle list per LOD level, so all three counts are written.
  std::array<std::uint32_t, k_previs_lod_levels> triangle_count{};
  std::uint32_t data_offset{0};
  // Constraint: the bound is the source mesh's own sphere, in the mesh's local space.
  float local_center[3]{};
  float local_radius{0.0f};
};

BakeOcStatus bake_oc_shared_object(const std::filesystem::path& stat_mesh,
                                   const std::filesystem::path& oc_shell,
                                   const OcObjectMesh& mesh,
                                   std::uint32_t csg_file_hash,
                                   std::span<const OcPlacement> placements,
                                   const std::filesystem::path& output_oc_nif);

// Constraint: the bound is the mesh's own sphere moved into cell space by the placement.
SharedCombinedRecordXform record_for_placement(const OcPlacement& placement,
                                                const float local_center[3],
                                                float local_radius);

// Constraint: a shipped STAT mesh arrives from an archive, so the same writer takes its bytes instead of a loose path.
BakeOcStatus bake_oc_shared_objects_bytes(std::span<const std::byte> stat_mesh,
                                          const std::filesystem::path& oc_shell,
                                          std::uint32_t csg_file_hash,
                                          std::span<const SharedGeomObjectWrite> objects,
                                          const std::filesystem::path& output_oc_nif);

// Constraint: a caller that must not leave a half-written cell takes the image instead of a file path.
std::optional<std::vector<std::byte>> bake_oc_shared_objects_image(std::span<const std::byte> stat_mesh,
                                                                  std::span<const std::byte> shell_bytes,
                                                                  std::uint32_t csg_file_hash,
                                                                  std::span<const SharedGeomObjectWrite> objects);

// Constraint: one _OC holds every object of a material, as the shipped multi-object extras do.
BakeOcStatus bake_oc_shared_objects(const std::filesystem::path& stat_mesh,
                                    const std::filesystem::path& oc_shell,
                                    std::uint32_t csg_file_hash,
                                    std::span<const SharedGeomObjectWrite> objects,
                                    const std::filesystem::path& output_oc_nif);

}  // namespace fo4::previs
