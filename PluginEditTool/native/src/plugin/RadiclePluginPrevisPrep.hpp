#pragma once

#include <fo4/nif/physics_nif.hpp>
#include <fo4/previs/bake_oc.hpp>
#include <fo4/previs/csg.hpp>
#include <fo4/previs/precombine_plan.hpp>

#include <array>
#include <cstddef>
#include <expected>
#include <filesystem>
#include <functional>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace fo4ide::plugin::previs_prep {

// Constraint: one planned STAT mesh as the geometry writer consumes it, with its own sphere for the instance bounds.
struct Model {
  std::filesystem::path path;
  // Constraint: a shipped STAT mesh arrives as archive bytes, so the baker takes the bytes it was measured from.
  std::vector<std::byte> nif_bytes;
  std::string material;
  fo4::previs::CsgMeshObject mesh;
  std::vector<float> positions;
  float local_center[3]{};
  float local_radius{0.0f};
};

struct Prepared {
  fo4::previs::PrecombinePlan plan;
  std::vector<std::string> materials;
  std::vector<Model> models;
  fo4::previs::CsgBuilt csg;
};

// Constraint: a placed mesh's triangles in cell space are what the cell physics NIF stores.
fo4::nif::PhysicsMeshInput placed_physics_mesh(const Model& model, const fo4::previs::OcPlacementTransform& xform);

// Constraint: the cell body is every planned instance's triangles in cell space, split by the physics writer, and occluders collects the placed triangles of every instance that may occlude.
std::vector<fo4::nif::PhysicsMeshInput> placed_physics_meshes(const fo4::previs::PrecombinePlan& plan,
                                                              const std::vector<Model>& models,
                                                              std::size_t& triangles,
                                                              std::vector<std::array<float, 9>>& occluders);

struct TomeResult {
  std::size_t bytes{0};
  std::size_t objects{0};
  // Constraint: a culled body is absent from the tome, so the reported total must say how many were dropped.
  std::size_t culled{0};
  bool copied_vanilla{false};
  std::vector<std::string> warnings;
};

struct TomeRequest {
  std::filesystem::path path;
  std::vector<fo4::nif::PhysicsMeshInput> bodies;
  // Constraint: an occluder is a placed triangle, so a shell of separate wall bodies can enclose and hide a body.
  std::vector<std::array<float, 9>> occluders;
  // Constraint: the vanilla copy is opt-in only; it must never run unless the caller asked for it.
  bool copy_vanilla{false};
  std::filesystem::path vanilla_tome;
  // Constraint: an exterior cell's footprint (min x, min y, max x, max y); z comes from the bodies, an interior leaves it empty.
  std::optional<std::array<float, 4>> cell_xy;
};

struct TomeBuild {
  TomeResult result;
  // Constraint: empty bytes mean no tome was built, so the caller must not write a file for this run.
  std::vector<std::byte> bytes;
};

// Constraint: builds and validates the tome in memory only, so a refusal must happen before any file is written.
std::expected<TomeBuild, std::string> build_tome(const TomeRequest& request);

// Constraint: one tome object per visible cell-space body with id 0xFD000000 | body index and the body's own box, in one open tile, minus any body an occluder encloses; never read from a vanilla tome.
std::expected<TomeResult, std::string> write_tome(const TomeRequest& request);

// Constraint: vanilla STAT meshes live inside the general archives, so the caller supplies the reader that finds them.
using ReadModelBytes = std::function<std::vector<std::byte>(const std::string& model_path)>;

// Constraint: every planned STAT mesh is loaded, the plan is reordered by shader material, and the CSG is built.
std::expected<Prepared, std::string> prepare(const std::filesystem::path& data_directory,
                                             fo4::previs::PrecombinePlan plan,
                                             const ReadModelBytes& read_model = {},
                                             std::span<const std::byte> existing_csg = {});

}  // namespace fo4ide::plugin::previs_prep
