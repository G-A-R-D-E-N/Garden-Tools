#include "RadiclePluginPrevisPrep.hpp"

#include <fo4/nif/nif.hpp>
#include <fo4/nif/shape_mesh.hpp>
#include <fo4/previs/csg.hpp>
#include <fo4/previs/uvd.hpp>
#include <fo4/previs/uvd_tile_build.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <expected>
#include <format>
#include <fstream>
#include <system_error>
#include <map>
#include <string>
#include <utility>
#include <vector>

#include "RadiclePluginPrevisIo.hpp"

namespace fo4ide::plugin::previs_prep {
namespace {

// Constraint: retail tomes list a precombined object as 0xFD plus a small index, and never by the REFRs it absorbed.
constexpr std::uint32_t k_combined_id_prefix = 0xFD000000u;
constexpr std::size_t k_combined_id_mask = 0x00FFFFFFu;

void measureSphere(Model& model) {
  const auto vertexCount = model.positions.size() / 3u;
  if (vertexCount == 0) return;
  double center[3]{};
  for (std::size_t v = 0; v < vertexCount; ++v) {
    for (std::size_t axis = 0; axis < 3; ++axis) {
      center[axis] += static_cast<double>(model.positions[3 * v + axis]) / static_cast<double>(vertexCount);
    }
  }
  double radiusSq = 0.0;
  for (std::size_t v = 0; v < vertexCount; ++v) {
    double distanceSq = 0.0;
    for (std::size_t axis = 0; axis < 3; ++axis) {
      const double delta = static_cast<double>(model.positions[3 * v + axis]) - center[axis];
      distanceSq += delta * delta;
    }
    radiusSq = std::max(radiusSq, distanceSq);
  }
  for (std::size_t axis = 0; axis < 3; ++axis) model.local_center[axis] = static_cast<float>(center[axis]);
  model.local_radius = static_cast<float>(std::sqrt(radiusSq));
}

std::expected<Model, std::string> loadModel(const std::vector<std::byte>& bytes, const std::filesystem::path& path, const std::string& modelPath) {
  if (bytes.empty()) return std::unexpected("STAT mesh could not be read: " + modelPath);
  Model model;
  model.path = path;
  model.nif_bytes = bytes;
  std::string material;
  try {
    const auto nif = fo4::nif::read_nif(bytes);
    const auto shapeMesh = fo4::nif::read_first_shape_mesh(nif);
    if (!shapeMesh.has_value()) {
      return std::unexpected("STAT mesh carries no vertex data to combine: " + modelPath);
    }
    // Constraint: the container holds only shipped vertex layouts, so a shape in any other one fails here by name.
    model.mesh = fo4::previs::csg_mesh_from_shape(*shapeMesh);
    // Invariant: positions are the written half bytes read back, so the LOD and physics inputs match the container.
    model.positions = fo4::previs::csg_mesh_positions(model.mesh);
    for (const auto& shape : fo4::nif::read_shapes(nif)) {
      if (material.empty()) material = shape.material;
      if (!shape.verts.empty()) break;
    }
  } catch (const std::exception& error) {
    return std::unexpected("STAT mesh could not be parsed: " + modelPath + ": " + error.what());
  }
  model.material = material;
  measureSphere(model);
  return model;
}

}  // namespace

std::vector<fo4::nif::PhysicsMeshInput> placed_physics_meshes(const fo4::previs::PrecombinePlan& plan,
                                                              const std::vector<Model>& models,
                                                              std::size_t& triangles,
                                                              std::vector<std::array<float, 9>>& occluders) {
  std::vector<fo4::nif::PhysicsMeshInput> meshes;
  triangles = 0;
  occluders.clear();
  if (models.size() != plan.groups.size()) return meshes;
  for (std::size_t i = 0; i < plan.groups.size(); ++i) {
    if (models[i].positions.empty() || models[i].mesh.indices.empty()) continue;
    for (const auto& instance : plan.groups[i].instances) {
      const auto xform = fo4::previs::placement_transform_from_euler(
          instance.transform.x, instance.transform.y, instance.transform.z, instance.transform.rx,
          instance.transform.ry, instance.transform.rz, instance.scale);
      auto mesh = placed_physics_mesh(models[i], xform);
      triangles += mesh.indices.size() / 3u;
      // Constraint: an occluder triangle comes from the placed geometry of an instance whose base object may occlude.
      if (instance.occludes) {
        const std::size_t vertex_count = mesh.positions.size() / 3u;
        for (std::size_t corner = 0; corner + 2 < mesh.indices.size(); corner += 3) {
          std::array<float, 9> triangle{};
          bool usable = true;
          for (std::size_t k = 0; k < 3 && usable; ++k) {
            const std::size_t vertex = static_cast<std::size_t>(mesh.indices[corner + k]);
            if (vertex >= vertex_count) {
              usable = false;
              break;
            }
            for (std::size_t axis = 0; axis < 3; ++axis) {
              triangle[3 * k + axis] = mesh.positions[3 * vertex + axis];
            }
          }
          if (usable) occluders.push_back(triangle);
        }
      }
      meshes.push_back(std::move(mesh));
    }
  }
  return meshes;
}

namespace {

// Constraint: the mesh's shipped vertex descriptor stays as csg_mesh_from_shape set it, so the packed blocks arrive as bytes plus counts.
bool pack_levels_into_mesh(const fo4::previs::LodLevels& levels, Model& model) {
  const auto packed = fo4::previs::pack_lod_levels(levels, model.mesh.vertices, model.mesh.vertex_bytes());
  if (!packed.has_value()) return false;
  model.mesh.indices = packed->indices;
  model.mesh.higher_lod_indices = packed->higher_lod_indices;
  model.mesh.vertices = packed->vertices;
  model.mesh.vertex_count = packed->vertex_count;
  model.mesh.level_vertex_count = packed->level_vertex_count;
  return true;
}

}  // namespace

std::expected<Prepared, std::string> prepare(const std::filesystem::path& data_directory,
                                             fo4::previs::PrecombinePlan plan,
                                             const ReadModelBytes& read_model,
                                             std::span<const std::byte> existing_csg) {
  Prepared out;
  out.plan = std::move(plan);
  std::map<std::string, std::string, std::less<>> materialByModel;
  out.models.reserve(out.plan.groups.size());
  for (const auto& group : out.plan.groups) {
    const std::filesystem::path relative(group.model_path);
    const auto path = relative.is_absolute() ? relative : data_directory / relative;
    std::error_code error;
    // Constraint: a STAT model path is written without the Meshes prefix, so both spellings are tried.
    const auto meshes_path = data_directory / "Meshes" / relative;
    std::vector<std::byte> bytes;
    if (std::filesystem::is_regular_file(path, error)) {
      bytes = readBinaryFile(path);
    } else if (!relative.is_absolute() && std::filesystem::is_regular_file(meshes_path, error)) {
      bytes = readBinaryFile(meshes_path);
    } else if (read_model) {
      // Constraint: a shipped STAT mesh sits under Meshes in a general archive, so the reader gets the data-relative path.
      const auto asset_path = relative.is_absolute() ? group.model_path
                                                    : (std::filesystem::path{"Meshes"} / relative).string();
      bytes = read_model(asset_path);
    }
    if (bytes.empty()) {
      return std::unexpected("STAT mesh is missing under the Data folder: " + group.model_path);
    }
    auto model = loadModel(bytes, path, group.model_path);
    if (!model) return std::unexpected(model.error());
    // Constraint: every authored object carries the whole LOD ladder, so the welded levels are packed with this mesh's own bytes.
    if (!model->positions.empty() && !model->mesh.indices.empty()) {
      try {
        // Constraint: a pinned target is the object's own shipped ratio, so it replaces the edge rule only when the plan names one.
        const auto levels = group.lod_target.has_value()
                                ? fo4::previs::reduce_mesh_levels_to_target(
                                      model->positions, model->mesh.indices,
                                      fo4::previs::LodLevelTarget{group.lod_target->level1, group.lod_target->level2})
                                : fo4::previs::reduce_mesh_levels(model->positions, model->mesh.indices);
        if (!pack_levels_into_mesh(levels, *model)) {
          return std::unexpected("the STAT mesh cannot carry LOD levels: " + group.model_path);
        }
      } catch (const std::exception& error) {
        return std::unexpected(std::string("the STAT mesh cannot carry LOD levels: ") + error.what());
      }
    }
    materialByModel[group.model_path] = model->material;
    out.models.push_back(std::move(*model));
  }

  std::map<std::string, std::size_t, std::less<>> indexByPath;
  for (std::size_t index = 0; index < out.plan.groups.size(); ++index) {
    indexByPath[out.plan.groups[index].model_path] = index;
  }
  auto ordered = fo4::previs::order_plan_by_material(out.plan, materialByModel);
  out.plan = std::move(ordered.plan);
  out.materials = std::move(ordered.materials);
  std::vector<Model> reordered;
  reordered.reserve(out.plan.groups.size());
  for (const auto& group : out.plan.groups) {
    const auto found = indexByPath.find(group.model_path);
    if (found == indexByPath.end()) return std::unexpected("the material order lost a planned STAT mesh");
    reordered.push_back(std::move(out.models[found->second]));
  }
  out.models = std::move(reordered);

  std::vector<fo4::previs::CsgMeshObject> meshes;
  meshes.reserve(out.models.size());
  for (const auto& model : out.models) {
    // Constraint: an empty shape cannot address a stream, so the failure must name the mesh that lacks it.
    if (model.mesh.vertex_count == 0 || model.mesh.vertices.empty() || model.mesh.vertex_bytes() == 0) {
      return std::unexpected(std::format(
          "the STAT mesh has no usable vertex buffer (desc 0x{:x}, stride {}, {} vertices): {}",
          model.mesh.vertex_desc, model.mesh.vertex_bytes(), model.mesh.vertex_count, model.path.string()));
    }
    meshes.push_back(model.mesh);
  }
  try {
    out.csg = fo4::previs::build_csg(meshes, existing_csg);
  } catch (const std::exception& error) {
    return std::unexpected(std::string("the patch geometry container could not be written: ") + error.what());
  }
  if (out.csg.rows.size() != out.plan.groups.size()) {
    return std::unexpected("the geometry container lost a planned mesh");
  }
  return out;
}

fo4::nif::PhysicsMeshInput placed_physics_mesh(const Model& model,
                                                   const fo4::previs::OcPlacementTransform& xform) {
  fo4::nif::PhysicsMeshInput physics;
  physics.positions.resize(model.positions.size());
  const auto vertices = model.positions.size() / 3u;
  for (std::size_t v = 0; v < vertices; ++v) {
    const float local[3] = {model.positions[3 * v], model.positions[3 * v + 1], model.positions[3 * v + 2]};
    for (std::size_t axis = 0; axis < 3; ++axis) {
      const float rotated = xform.rotation[3 * axis] * local[0] + xform.rotation[3 * axis + 1] * local[1] +
                            xform.rotation[3 * axis + 2] * local[2];
      physics.positions[3 * v + axis] = xform.translation[axis] + xform.scale * rotated;
    }
  }
  physics.indices = model.mesh.indices;
  return physics;
}

std::expected<TomeBuild, std::string> build_tome(const TomeRequest& tome_request) {
  const auto& bodies = tome_request.bodies;
  TomeBuild build;
  auto& result = build.result;
  // Constraint: the vanilla copy is an explicit opt-in fallback, so it logs its own use through the returned result.
  if (tome_request.copy_vanilla) {
    auto source = readBinaryFile(tome_request.vanilla_tome);
    if (source.empty()) return std::unexpected("copy_vanilla_tome: no vanilla tome at " + tome_request.vanilla_tome.string());
    if (const auto issues = fo4::previs::validate_uvd(source); !issues.empty()) {
      return std::unexpected("copy_vanilla_tome: source tome failed validation: " + issues.front());
    }
    result.bytes = source.size();
    result.copied_vanilla = true;
    build.bytes = std::move(source);
    return build;
  }
  // Constraint: the id's low 24 bits carry the body index, so more bodies than that cannot all get distinct ids.
  if (bodies.size() > k_combined_id_mask) return std::unexpected("too many cell bodies for a tome: " + std::to_string(bodies.size()));
  fo4::previs::UvdBuildInput input;
  for (std::size_t index = 0; index < bodies.size(); ++index) {
    const auto box = fo4::previs::uvd_positions_box(bodies[index].positions);
    if (!box.has_value()) {
      result.warnings.push_back(std::format("tome: body {} has no finite positions and is left out of the tome", index));
      continue;
    }
    input.objects.push_back(fo4::previs::UvdBuildObject{
        .box = *box, .id = k_combined_id_prefix | static_cast<std::uint32_t>(index)});
  }
  // Constraint: a cell with no collision geometry has nothing for an object to cover, so no tome is written for it.
  input.occluders = tome_request.occluders;
  // Constraint: a culled body must not be listed or counted, so the survivors decide the cell box and the object total.
  auto visible = fo4::previs::uvd_cull_occluded(input);
  result.culled = input.objects.size() - visible.size();
  if (input.objects.empty() || visible.empty()) {
    result.warnings.push_back("tome: no body is left to list, so no tome was written for this run");
    return build;
  }
  if (tome_request.cell_xy.has_value()) {
    std::array<float, 6> cell{(*tome_request.cell_xy)[0], (*tome_request.cell_xy)[1], visible.front().box[2],
                              (*tome_request.cell_xy)[2], (*tome_request.cell_xy)[3], visible.front().box[5]};
    for (const auto& object : visible) {
      cell[2] = std::min(cell[2], object.box[2]);
      cell[5] = std::max(cell[5], object.box[5]);
    }
    input.cell_box = cell;
  }
  // Constraint: the survivors are already culled, so the builder gets no occluders and does not cull them a second time.
  input.objects = std::move(visible);
  input.occluders.clear();
  std::vector<std::byte> bytes;
  try {
    bytes = fo4::previs::write_uvd_tome_tiled(input);
  } catch (const std::exception& error) {
    return std::unexpected(std::string("generated tome could not be built: ") + error.what());
  }
  // Constraint: a tome the tile model rejects must never reach the Data folder, even on a fresh generation.
  if (const auto issues = fo4::previs::validate_uvd_tome_tiled(bytes); !issues.empty()) {
    return std::unexpected("generated tome failed validation: " + issues.front());
  }
  result.bytes = bytes.size();
  result.objects = input.objects.size();
  build.bytes = std::move(bytes);
  return build;
}

std::expected<TomeResult, std::string> write_tome(const TomeRequest& tome_request) {
  auto build = build_tome(tome_request);
  if (!build) return std::unexpected(std::move(build.error()));
  FileTransaction files;
  if (!build->bytes.empty()) {
    files.write(tome_request.path, std::move(build->bytes));
  } else if (!build->result.copied_vanilla) {
    // Constraint: a tome left by an earlier run must not outlive a run that lists nothing.
    files.remove(tome_request.path);
  }
  if (const auto failed = files.commit()) {
    return std::unexpected("could not write " + tome_request.path.string() + ": " + *failed);
  }
  return std::move(build->result);
}

}  // namespace fo4ide::plugin::previs_prep
