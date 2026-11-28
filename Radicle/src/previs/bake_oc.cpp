#include "fo4/previs/bake_oc.hpp"

#include "fo4/error.hpp"
#include "fo4/nif/nif.hpp"
#include "fo4/nif/structure.hpp"
#include "fo4/previs/oc_nif_rewrite.hpp"
#include "fo4/previs/shared_geom_extra.hpp"

#include <cmath>
#include <fstream>
#include <optional>

namespace fo4::previs {
namespace {

// Constraint: a retail _OC carries one Shared extra per LOD shape, and every extra but the one this bake rewrites would keep naming the vanilla container.
std::optional<std::vector<std::byte>> keep_single_shared_extra(std::vector<std::byte> bytes) {
  const auto extra_count = [](const std::vector<std::byte>& image) {
    const auto nif = fo4::nif::read_nif(image);
    std::size_t count = 0;
    for (const auto& block : nif.blocks()) {
      if (block.type == "BSPackedCombinedSharedGeomDataExtra") ++count;
    }
    return count;
  };
  for (int guard = 0; guard < 64; ++guard) {
    const auto before = extra_count(bytes);
    if (before <= 1) return bytes;
    const auto nif = fo4::nif::read_nif(bytes);
    bool pruned = false;
    for (std::size_t node = 0; node < nif.blocks().size() && !pruned; ++node) {
      for (const auto child : nif.children(static_cast<std::uint32_t>(node))) {
        if (nif.blocks()[child].type == "BSPackedCombinedSharedGeomDataExtra") continue;
        std::vector<std::byte> staged;
        try {
          staged = fo4::nif::remove_block(nif, child).bytes;
        } catch (const fo4::parse_error&) {
          continue;
        }
        const auto after = extra_count(staged);
        if (after >= before) continue;
        bytes = std::move(staged);
        pruned = true;
        break;
      }
    }
    if (!pruned) return std::nullopt;
  }
  return std::nullopt;
}

SharedCombinedRecordXform placement_to_record(const OcPlacement& placement) {
  SharedCombinedRecordXform out{};
  for (std::size_t i = 0; i < 3; ++i) out.translation[i] = placement.xform.translation[i];
  for (std::size_t i = 0; i < 9; ++i) out.rotation[i] = placement.xform.rotation[i];
  out.scale = placement.xform.scale;
  return out;
}

BakeOcStatus patch_template_placements(std::vector<std::byte>& nif_bytes,
                                       const std::vector<OcPlacement>& placements) {
  std::size_t placement_index = 0;
  bool patched = false;
  const auto nif = fo4::nif::read_nif(nif_bytes);
  for (const auto& block : nif.blocks()) {
    if (block.type != "BSPackedCombinedSharedGeomDataExtra") continue;
    const auto extra = std::span<const std::byte>(nif_bytes).subspan(block.offset, block.size);
    const auto detected = detect_shared_geom_layout(extra);
    if (!detected.has_value() || detected->combined_per_object != 1) continue;
    if (placement_index + detected->object_count > placements.size()) return BakeOcStatus::unsupported;
    auto mutable_extra = std::span<std::byte>(nif_bytes).subspan(block.offset, block.size);
    for (std::size_t slot = 0; slot < detected->object_count; ++slot) {
      const auto record = placement_to_record(placements[placement_index++]);
      if (!write_shared_combined_record_xform(mutable_extra, slot, 0, record)) return BakeOcStatus::unsupported;
    }
    patched = true;
  }
  if (!patched || placement_index != placements.size()) return BakeOcStatus::unsupported;
  return BakeOcStatus::ok;
}

BakeOcStatus write_bytes(const std::filesystem::path& path, std::span<const std::byte> bytes) {
  std::ofstream out(path, std::ios::binary);
  if (!out) return BakeOcStatus::io_error;
  out.write(reinterpret_cast<const char*>(bytes.data()),
            static_cast<std::streamsize>(bytes.size()));
  return out ? BakeOcStatus::ok : BakeOcStatus::io_error;
}

}

// Constraint: a patched object keeps the template's mesh counts and takes the caller's container hash, offset and sphere.
// Constraint: only the first readable Shared extra is rebuilt; its mesh is the one the request's offset addresses, so every other extra stays byte for byte.
BakeOcStatus rewrite_placement_object(std::vector<std::byte>& nif_bytes, const BakeOcPlacementsRequest& request) {
  const auto nif = fo4::nif::read_nif(nif_bytes);
  for (std::size_t index = 0; index < nif.blocks().size(); ++index) {
    if (nif.blocks()[index].type != "BSPackedCombinedSharedGeomDataExtra") continue;
    const auto extra = std::span<const std::byte>(nif_bytes).subspan(nif.blocks()[index].offset, nif.blocks()[index].size);
    const auto header = read_shared_geom_header(extra);
    if (!header.has_value() || header->objects.empty()) continue;
    const auto& shipped = header->objects.front();
    SharedGeomObjectWrite object;
    object.data_offset = request.csg_data_offset;
    object.vertex_desc = shipped.vertex_desc;
    object.vertex_count = shipped.vertex_count;
    for (std::size_t level = 0; level < k_previs_lod_levels; ++level) {
      object.triangle_count[level] = shipped.triangle_count[level];
    }
    object.lod_levels = shipped.lod_levels == 0 ? static_cast<std::uint32_t>(k_previs_lod_levels) : shipped.lod_levels;
    object.records.reserve(request.placements.size());
    for (const auto& placement : request.placements) {
      object.records.push_back(record_for_placement(placement, request.local_center, request.local_radius));
    }
    const auto built = build_shared_geom_extra(extra, request.csg_file_hash,
                                               std::span<const SharedGeomObjectWrite>(&object, 1));
    if (!built.has_value()) return BakeOcStatus::unsupported;
    const auto replaced = replace_nif_block_at(nif_bytes, index, *built);
    if (!replaced.has_value()) return BakeOcStatus::unsupported;
    nif_bytes = std::move(*replaced);
    return BakeOcStatus::ok;
  }
  return BakeOcStatus::unsupported;
}

OcPlacementTransform placement_transform_from_euler(float x, float y, float z, float rx, float ry, float rz,
                                                    float scale) {
  OcPlacementTransform out{};
  out.translation[0] = x;
  out.translation[1] = y;
  out.translation[2] = z;
  out.scale = scale;
  const float cx = std::cos(rx);
  const float sx = std::sin(rx);
  const float cy = std::cos(ry);
  const float sy = std::sin(ry);
  const float cz = std::cos(rz);
  const float sz = std::sin(rz);
  out.rotation[0] = cy * cz;
  out.rotation[1] = cy * sz;
  out.rotation[2] = -sy;
  out.rotation[3] = sx * sy * cz - cx * sz;
  out.rotation[4] = sx * sy * sz + cx * cz;
  out.rotation[5] = sx * cy;
  out.rotation[6] = cx * sy * cz + sx * sz;
  out.rotation[7] = cx * sy * sz - sx * cz;
  out.rotation[8] = cx * cy;
  return out;
}

std::optional<std::vector<std::byte>> read_file_bytes(const std::filesystem::path& path) {
  std::ifstream in(path, std::ios::binary);
  if (!in) return std::nullopt;
  in.seekg(0, std::ios::end);
  const auto size = in.tellg();
  if (size <= 0) return std::nullopt;
  in.seekg(0, std::ios::beg);
  std::vector<std::byte> bytes(static_cast<std::size_t>(size));
  in.read(reinterpret_cast<char*>(bytes.data()), size);
  if (!in) return std::nullopt;
  return bytes;
}

struct PatchableSharedExtra {
  std::size_t block_index{};
  std::size_t object_count{};
};

std::vector<PatchableSharedExtra> patchable_shared_extras(const fo4::nif::NifFile& nif,
                                                          std::span<const std::byte> bytes) {
  std::vector<PatchableSharedExtra> out;
  for (std::size_t block_index = 0; block_index < nif.blocks().size(); ++block_index) {
    const auto& block = nif.blocks()[block_index];
    if (block.type != "BSPackedCombinedSharedGeomDataExtra") continue;
    const auto span = bytes.subspan(block.offset, block.size);
    const auto layout = detect_shared_geom_layout(span);
    if (!layout.has_value() || layout->combined_per_object != 1) continue;
    out.push_back(PatchableSharedExtra{.block_index = block_index, .object_count = layout->object_count});
  }
  return out;
}

std::optional<std::vector<std::byte>> encode_shared_extra_placements(std::span<const std::byte> extra,
                                                                      std::size_t object_count,
                                                                      std::span<const SharedCombinedRecordXform> records) {
  if (records.size() != object_count) return std::nullopt;
  const auto layout = detect_shared_geom_layout(extra);
  if (!layout.has_value() || layout->combined_per_object != 1) return std::nullopt;
  if (object_count == layout->object_count) {
    auto copy = std::vector<std::byte>(extra.begin(), extra.end());
    auto mutable_extra = std::span<std::byte>(copy.data(), copy.size());
    for (std::size_t slot = 0; slot < object_count; ++slot) {
      if (!write_shared_combined_record_xform(mutable_extra, slot, 0, records[slot])) return std::nullopt;
    }
    return copy;
  }
  return rebuild_compact_shared_geom_extra(extra, object_count, records);
}

BakeOcStatus apply_placements_to_oc_bytes(std::vector<std::byte>& template_bytes,
                                          const std::vector<OcPlacement>& placements) {
  if (template_bytes.empty() || placements.empty()) return BakeOcStatus::invalid_input;
  if (!oc_nif_has_shared_combined_geom(template_bytes)) return BakeOcStatus::unsupported;

  const auto template_nif = fo4::nif::read_nif(template_bytes);
  const auto patchables = patchable_shared_extras(template_nif, template_bytes);
  if (patchables.empty()) return BakeOcStatus::unsupported;

  std::size_t object_slots = 0;
  for (const auto& patchable : patchables) object_slots += patchable.object_count;

  if (object_slots == placements.size()) return patch_template_placements(template_bytes, placements);
  if (placements.size() < object_slots) return BakeOcStatus::unsupported;

  const std::size_t extra_slots = placements.size() - object_slots;
  for (std::size_t resize_block = 0; resize_block < patchables.size(); ++resize_block) {
    std::vector<std::byte> work = template_bytes;
    std::size_t placement_index = 0;
    bool ok = true;
    for (std::size_t block_pos = 0; block_pos < patchables.size(); ++block_pos) {
      const auto& patchable = patchables[block_pos];
      const std::size_t object_count = patchable.object_count + (block_pos == resize_block ? extra_slots : 0);
      if (placement_index + object_count > placements.size()) {
        ok = false;
        break;
      }
      std::vector<SharedCombinedRecordXform> records;
      records.reserve(object_count);
      for (std::size_t slot = 0; slot < object_count; ++slot) {
        records.push_back(placement_to_record(placements[placement_index++]));
      }
      const auto work_nif = fo4::nif::read_nif(work);
      const auto& block = work_nif.blocks()[patchable.block_index];
      const auto extra = std::span<const std::byte>(work).subspan(block.offset, block.size);
      auto encoded_extra = encode_shared_extra_placements(extra, object_count, records);
      if (!encoded_extra.has_value()) {
        ok = false;
        break;
      }
      auto encoded = replace_nif_block_at(work, patchable.block_index, *encoded_extra);
      if (!encoded.has_value()) {
        ok = false;
        break;
      }
      work = std::move(*encoded);
    }
    if (ok && placement_index == placements.size()) {
      template_bytes = std::move(work);
      return BakeOcStatus::ok;
    }
  }
  return BakeOcStatus::unsupported;
}

BakeOcStatus apply_placements_to_first_patchable_extra(std::vector<std::byte>& template_bytes,
                                                       const std::vector<OcPlacement>& placements) {
  if (template_bytes.empty() || placements.empty()) return BakeOcStatus::invalid_input;
  const auto template_nif = fo4::nif::read_nif(template_bytes);
  for (std::size_t block_index = 0; block_index < template_nif.blocks().size(); ++block_index) {
    const auto& block = template_nif.blocks()[block_index];
    if (block.type != "BSPackedCombinedSharedGeomDataExtra") continue;
    const auto extra = std::span<const std::byte>(template_bytes).subspan(block.offset, block.size);
    const auto layout = detect_shared_geom_layout(extra);
    if (!layout.has_value() || layout->combined_per_object != 1) continue;

    std::vector<SharedCombinedRecordXform> records;
    records.reserve(placements.size());
    for (const auto& placement : placements) {
      records.push_back(placement_to_record(placement));
    }

    std::optional<std::vector<std::byte>> new_extra;
    if (placements.size() == layout->object_count) {
      new_extra.emplace(extra.begin(), extra.end());
      auto mutable_extra = std::span<std::byte>(new_extra->data(), new_extra->size());
      for (std::size_t slot = 0; slot < placements.size(); ++slot) {
        if (!write_shared_combined_record_xform(mutable_extra, slot, 0, records[slot])) {
          return BakeOcStatus::unsupported;
        }
      }
    } else {
      new_extra = rebuild_compact_shared_geom_extra(extra, placements.size(),
                                                    std::span<const SharedCombinedRecordXform>(records));
    }
    if (!new_extra.has_value()) return BakeOcStatus::unsupported;
    const auto encoded = replace_nif_block_at(template_bytes, block_index, *new_extra);
    if (!encoded.has_value()) return BakeOcStatus::unsupported;
    template_bytes = std::move(*encoded);
    return BakeOcStatus::ok;
  }
  return BakeOcStatus::unsupported;
}

bool stat_mesh_nif(std::span<const std::byte> nif_bytes) {
  if (nif_bytes.empty()) return false;
  try {
    const auto nif = fo4::nif::read_nif(nif_bytes);
    bool shape = false;
    for (const auto& block : nif.blocks()) {
      if (fo4::nif::is_shape_block_type(block.type)) shape = true;
      if (block.type == "BSPackedCombinedSharedGeomDataExtra") return false;
    }
    return shape;
  } catch (const std::exception&) {
    return false;
  }
}

bool oc_nif_has_shared_combined_geom(std::span<const std::byte> nif_bytes) {
  if (nif_bytes.empty()) return false;
  try {
    const auto nif = fo4::nif::read_nif(nif_bytes);
    for (const auto& block : nif.blocks()) {
      if (block.type == "BSPackedCombinedSharedGeomDataExtra") return true;
    }
  } catch (const std::exception&) {
    return false;
  }
  return false;
}

BakeOcStatus bake_oc_single_source_bytes(std::span<const std::byte> source,
                                         const std::filesystem::path& output_oc_nif) {
  if (source.empty()) return BakeOcStatus::invalid_input;
  if (!oc_nif_has_shared_combined_geom(source)) return BakeOcStatus::unsupported;
  if (output_oc_nif.empty()) return BakeOcStatus::invalid_input;
  std::error_code ec;
  std::filesystem::create_directories(output_oc_nif.parent_path(), ec);
  if (ec) return BakeOcStatus::io_error;
  return write_bytes(output_oc_nif, source);
}

BakeOcStatus bake_oc_single_source(const std::filesystem::path& source_nif,
                                   const std::filesystem::path& output_oc_nif) {
  if (source_nif.empty() || output_oc_nif.empty()) return BakeOcStatus::invalid_input;
  std::ifstream in(source_nif, std::ios::binary);
  if (!in) return BakeOcStatus::io_error;
  in.seekg(0, std::ios::end);
  const auto size = in.tellg();
  if (size <= 0) return BakeOcStatus::invalid_input;
  in.seekg(0, std::ios::beg);
  std::vector<std::byte> bytes(static_cast<std::size_t>(size));
  in.read(reinterpret_cast<char*>(bytes.data()), size);
  if (!in) return BakeOcStatus::io_error;
  return bake_oc_single_source_bytes(bytes, output_oc_nif);
}

BakeOcStatus bake_oc_from_placements(const BakeOcPlacementsRequest& request,
                                     const std::filesystem::path& output_oc_nif) {
  if (request.template_oc_nif.empty() || output_oc_nif.empty() || request.placements.empty()) {
    return BakeOcStatus::invalid_input;
  }
  if (!std::filesystem::exists(request.template_oc_nif)) return BakeOcStatus::io_error;
  auto template_bytes = read_file_bytes(request.template_oc_nif);
  if (!template_bytes.has_value()) return BakeOcStatus::io_error;
  // Constraint: a request that names a container rewrites the table and records, so the template's own address is not reused.
  const bool addresses_container = request.csg_file_hash != 0;
  if (addresses_container && !(request.local_radius > 0.0f)) return BakeOcStatus::invalid_input;
  const auto status = addresses_container ? rewrite_placement_object(*template_bytes, request)
                                          : apply_placements_to_oc_bytes(*template_bytes, request.placements);
  if (status != BakeOcStatus::ok) return status;
  std::error_code ec;
  std::filesystem::create_directories(output_oc_nif.parent_path(), ec);
  if (ec) return BakeOcStatus::io_error;
  return write_bytes(output_oc_nif, *template_bytes);
}

BakeOcStatus bake_oc_author_from_stat_shell(const std::filesystem::path& stat_mesh,
                                            const std::filesystem::path& oc_shell,
                                            std::span<const OcPlacement> placements,
                                            const std::filesystem::path& output_oc_nif) {
  if (stat_mesh.empty() || oc_shell.empty() || output_oc_nif.empty() || placements.empty()) {
    return BakeOcStatus::invalid_input;
  }
  const auto stat_bytes = read_file_bytes(stat_mesh);
  const auto shell_bytes = read_file_bytes(oc_shell);
  if (!stat_bytes.has_value() || !shell_bytes.has_value()) return BakeOcStatus::io_error;
  if (!stat_mesh_nif(*stat_bytes) || !oc_nif_has_shared_combined_geom(*shell_bytes)) {
    return BakeOcStatus::unsupported;
  }
  auto grafted = graft_first_shape_mesh_into_shell(*shell_bytes, *stat_bytes);
  if (!grafted.has_value()) return BakeOcStatus::unsupported;
  std::vector<OcPlacement> local(placements.begin(), placements.end());
  const auto status = apply_placements_to_first_patchable_extra(*grafted, local);
  if (status != BakeOcStatus::ok) return status;
  std::error_code ec;
  std::filesystem::create_directories(output_oc_nif.parent_path(), ec);
  if (ec) return BakeOcStatus::io_error;
  return write_bytes(output_oc_nif, *grafted);
}

BakeOcStatus bake_oc_shared_object(const std::filesystem::path& stat_mesh,
                                   const std::filesystem::path& oc_shell,
                                   const OcObjectMesh& mesh,
                                   std::uint32_t csg_file_hash,
                                   std::span<const OcPlacement> placements,
                                   const std::filesystem::path& output_oc_nif) {
  if (placements.empty()) return BakeOcStatus::invalid_input;
  SharedGeomObjectWrite object;
  object.data_offset = mesh.data_offset;
  object.vertex_desc = mesh.vertex_desc;
  object.vertex_count = mesh.vertex_count;
  object.triangle_count = mesh.triangle_count;
  object.records.reserve(placements.size());
  for (const auto& placement : placements) {
    object.records.push_back(record_for_placement(placement, mesh.local_center, mesh.local_radius));
  }
  return bake_oc_shared_objects(stat_mesh, oc_shell, csg_file_hash,
                                std::span<const SharedGeomObjectWrite>(&object, 1), output_oc_nif);
}

SharedCombinedRecordXform record_for_placement(const OcPlacement& placement,
                                               const float local_center[3],
                                               float local_radius) {
  auto record = placement_to_record(placement);
  for (std::size_t i = 0; i < 3; ++i) {
    const float rotated = record.rotation[3 * i] * local_center[0] +
                          record.rotation[3 * i + 1] * local_center[1] +
                          record.rotation[3 * i + 2] * local_center[2];
    record.bound_center[i] = record.translation[i] + record.scale * rotated;
  }
  record.bound_radius = local_radius * record.scale;
  return record;
}

BakeOcStatus bake_oc_shared_objects(const std::filesystem::path& stat_mesh,
                                    const std::filesystem::path& oc_shell,
                                    std::uint32_t csg_file_hash,
                                    std::span<const SharedGeomObjectWrite> objects,
                                    const std::filesystem::path& output_oc_nif) {
  if (stat_mesh.empty() || oc_shell.empty() || output_oc_nif.empty() || objects.empty()) {
    return BakeOcStatus::invalid_input;
  }
  const auto stat_bytes = read_file_bytes(stat_mesh);
  if (!stat_bytes.has_value()) return BakeOcStatus::io_error;
  return bake_oc_shared_objects_bytes(*stat_bytes, oc_shell, csg_file_hash, objects, output_oc_nif);
}

std::optional<std::vector<std::byte>> bake_oc_shared_objects_image(std::span<const std::byte> stat_mesh,
                                                                  std::span<const std::byte> shell_bytes,
                                                                  std::uint32_t csg_file_hash,
                                                                  std::span<const SharedGeomObjectWrite> objects) {
  if (stat_mesh.empty() || shell_bytes.empty() || objects.empty()) return std::nullopt;
  if (!stat_mesh_nif(stat_mesh) || !oc_nif_has_shared_combined_geom(shell_bytes)) return std::nullopt;
  const auto single = keep_single_shared_extra(std::vector<std::byte>(shell_bytes.begin(), shell_bytes.end()));
  if (!single.has_value()) return std::nullopt;
  auto grafted = graft_first_shape_mesh_into_shell(*single, stat_mesh);
  if (!grafted.has_value()) return std::nullopt;

  const auto nif = fo4::nif::read_nif(*grafted);
  for (std::size_t block_index = 0; block_index < nif.blocks().size(); ++block_index) {
    if (nif.blocks()[block_index].type != "BSPackedCombinedSharedGeomDataExtra") continue;
    const auto extra = std::span<const std::byte>(*grafted)
                           .subspan(nif.blocks()[block_index].offset, nif.blocks()[block_index].size);
    const auto built = build_shared_geom_extra(extra, csg_file_hash, objects);
    if (!built.has_value()) return std::nullopt;
    const auto replaced = replace_nif_block_at(*grafted, block_index, *built);
    if (!replaced.has_value()) return std::nullopt;
    return *replaced;
  }
  return std::nullopt;
}

BakeOcStatus bake_oc_shared_objects_bytes(std::span<const std::byte> stat_mesh,
                                          const std::filesystem::path& oc_shell,
                                          std::uint32_t csg_file_hash,
                                          std::span<const SharedGeomObjectWrite> objects,
                                          const std::filesystem::path& output_oc_nif) {
  if (stat_mesh.empty() || oc_shell.empty() || output_oc_nif.empty() || objects.empty()) {
    return BakeOcStatus::invalid_input;
  }
  for (const auto& object : objects) {
    if (object.vertex_count == 0 || object.vertex_count > k_shared_geom_max_object_vertices ||
        object.triangle_count[0] == 0 || object.records.empty()) {
      return BakeOcStatus::invalid_input;
    }
  }
  const auto shell_bytes = read_file_bytes(oc_shell);
  if (!shell_bytes.has_value()) return BakeOcStatus::io_error;
  const auto image = bake_oc_shared_objects_image(stat_mesh, *shell_bytes, csg_file_hash, objects);
  if (!image.has_value()) return BakeOcStatus::unsupported;
  std::error_code ec;
  std::filesystem::create_directories(output_oc_nif.parent_path(), ec);
  if (ec) return BakeOcStatus::io_error;
  return write_bytes(output_oc_nif, *image);
}

}

