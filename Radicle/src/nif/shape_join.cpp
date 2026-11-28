#include "fo4/nif/shape_join.hpp"

#include "fo4/error.hpp"
#include "fo4/nif/block_ops.hpp"
#include "fo4/nif/customization_remap.hpp"
#include "fo4/nif/structure.hpp"
#include "customization_remap_internal.hpp"
#include "import_mesh.hpp"
#include "shape_ops_internal.hpp"
#include "write_model.hpp"

#include <algorithm>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <set>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace fo4::nif {
namespace {

using shape_detail::Layout;
using write_detail::Bytes;
using write_detail::Model;
using write_detail::append_int;
using write_detail::read_int;
using write_detail::write_int;

constexpr std::size_t k_triangle_bytes = 6;
constexpr std::size_t k_bone_transform_bytes = 68;
constexpr std::size_t k_max_bones = 256;
constexpr std::string_view k_plain_type = "BSTriShape";
constexpr std::string_view k_skin_type = "BSSkin::Instance";
constexpr std::string_view k_bonedata_type = "BSSkin::BoneData";

// Invariant: the three count fields sit at fixed negative offsets from the vertex data.
// Invariant: read_layout reads descriptor, triangle count, vertex count, data size, then vertices.
std::uint64_t descriptor_of(const Bytes& bytes, const Layout& layout) {
  return read_int<std::uint64_t>(bytes, layout.vertices_at - 18);
}

bool same_transform(const Transform& a, const Transform& b) {
  for (std::size_t i = 0; i < 3; ++i) {
    if (std::bit_cast<std::uint32_t>(a.t[i]) != std::bit_cast<std::uint32_t>(b.t[i])) return false;
  }
  for (std::size_t i = 0; i < 9; ++i) {
    if (std::bit_cast<std::uint32_t>(a.r[i]) != std::bit_cast<std::uint32_t>(b.r[i])) return false;
  }
  return std::bit_cast<std::uint32_t>(a.s) == std::bit_cast<std::uint32_t>(b.s);
}

bool same_layout(const Bytes& a, const Layout& la, const Bytes& b, const Layout& lb) {
  return la.stride == lb.stride && la.full == lb.full && la.skinned == lb.skinned && la.has_uv == lb.has_uv &&
         la.has_normal == lb.has_normal && la.has_tangent == lb.has_tangent && la.normal_at == lb.normal_at &&
         la.skin_at == lb.skin_at && la.bitangent_x_at == lb.bitangent_x_at &&
         descriptor_of(a, la) == descriptor_of(b, lb);
}

struct SkinBlocks {
  std::uint32_t instance{0};
  std::uint32_t bonedata{0};
  std::int32_t skeleton_root{-1};
  std::vector<std::int32_t> bone_refs;
  std::vector<std::string> bone_names;
  Bytes transforms;
  Bytes scales_trailer;
};

std::optional<std::string> read_skin_blocks(const NifFile& nif, const Model& model, std::uint32_t shape,
                                            const Shape& decoded, SkinBlocks& skin) {
  if (decoded.skin_ref < 0) return std::nullopt;
  const auto instance = static_cast<std::uint32_t>(decoded.skin_ref);
  if (nif.blocks()[instance].type != k_skin_type) {
    return "block " + std::to_string(shape) + " uses " + nif.blocks()[instance].type +
           ", so its partitions cannot merge";
  }
  if (decoded.bones.empty()) return "block " + std::to_string(shape) + " has no resolvable bones";
  const Bytes& bytes = model.blocks[instance];
  if (bytes.size() < 12) return "block " + std::to_string(shape) + " has a truncated skin instance";
  skin.instance = instance;
  skin.skeleton_root = read_int<std::int32_t>(bytes, 0);
  const auto data = read_int<std::int32_t>(bytes, 4);
  if (data < 0 || static_cast<std::size_t>(data) >= model.blocks.size()) {
    return "block " + std::to_string(shape) + " has no bone data block";
  }
  if (nif.blocks()[static_cast<std::uint32_t>(data)].type != k_bonedata_type) {
    return "block " + std::to_string(shape) + " has bone data of an unexpected type";
  }
  skin.bonedata = static_cast<std::uint32_t>(data);
  // Constraint: counts are widened before they are scaled, so a huge count cannot wrap past the size check.
  const std::size_t count = read_int<std::uint32_t>(bytes, 8);
  if (count > (bytes.size() - 12) / 4) return "block " + std::to_string(shape) + " has a truncated bone list";
  skin.bone_refs.reserve(count);
  for (std::size_t i = 0; i < count; ++i) skin.bone_refs.push_back(read_int<std::int32_t>(bytes, 12 + 4 * i));
  const std::size_t trailer_at = 12 + count * 4;
  if (trailer_at + 4 > bytes.size()) return "block " + std::to_string(shape) + " has a truncated scale count";
  const std::size_t scales = read_int<std::uint32_t>(bytes, trailer_at);
  if (scales > (bytes.size() - trailer_at - 4) / 12) {
    return "block " + std::to_string(shape) + " has truncated bone scales";
  }
  skin.scales_trailer.assign(bytes.begin() + static_cast<std::ptrdiff_t>(trailer_at), bytes.end());
  const Bytes& data_bytes = model.blocks[skin.bonedata];
  if (data_bytes.size() < 4) return "block " + std::to_string(shape) + " has truncated bone data";
  if (read_int<std::uint32_t>(data_bytes, 0) != count) {
    return "block " + std::to_string(shape) + " names a different bone count than its bone data holds";
  }
  if (4 + count * k_bone_transform_bytes != data_bytes.size()) {
    return "block " + std::to_string(shape) + " has bone data of an unexpected size";
  }
  skin.transforms.assign(data_bytes.begin() + 4, data_bytes.end());
  if (decoded.bones.size() != count) {
    return "block " + std::to_string(shape) + " decoded a different bone count than its instance holds";
  }
  skin.bone_names.reserve(count);
  std::set<std::string> seen;
  for (const auto& bone : decoded.bones) {
    if (bone.name.empty()) return "block " + std::to_string(shape) + " has a bone with no name";
    if (!seen.insert(bone.name).second) {
      return "block " + std::to_string(shape) + " names bone \"" + bone.name + "\" twice";
    }
    skin.bone_names.push_back(bone.name);
  }
  return std::nullopt;
}

void write_counts(Bytes& bytes, const Layout& layout, std::uint32_t tris, std::uint32_t verts) {
  write_int(bytes, layout.vertices_at - 10, tris);
  write_int(bytes, layout.vertices_at - 6, static_cast<std::uint16_t>(verts));
  write_int(bytes, layout.vertices_at - 4,
            static_cast<std::uint32_t>(verts * layout.stride + tris * k_triangle_bytes));
}

// Invariant: the result lists the source vertex of each kept vertex, ascending, so it indexes a per-vertex side table.
std::vector<std::int32_t> filter_geometry(Model& model, std::uint32_t block, const Layout& layout,
                                          const std::vector<std::uint32_t>& keep_tris, bool refresh_sphere,
                                          const std::vector<float>& decoded_positions) {
  Bytes& bytes = model.blocks[block];
  std::vector<bool> used(layout.vertex_count, false);
  for (const auto tri : keep_tris) {
    for (std::size_t k = 0; k < 3; ++k) {
      used[read_int<std::uint16_t>(bytes, layout.triangles_at + (tri * 3 + k) * 2)] = true;
    }
  }
  std::vector<std::uint32_t> remap(layout.vertex_count, 0);
  Bytes out;
  out.insert(out.end(), bytes.begin(), bytes.begin() + static_cast<std::ptrdiff_t>(layout.vertices_at));
  std::uint32_t kept = 0;
  std::vector<std::int32_t> source_of_vertex;
  std::vector<float> kept_positions;
  kept_positions.reserve(layout.vertex_count * 3);
  for (std::size_t v = 0; v < layout.vertex_count; ++v) {
    if (!used[v]) continue;
    source_of_vertex.push_back(static_cast<std::int32_t>(v));
    remap[v] = kept++;
    const auto from = bytes.begin() + static_cast<std::ptrdiff_t>(layout.vertices_at + v * layout.stride);
    out.insert(out.end(), from, from + static_cast<std::ptrdiff_t>(layout.stride));
    if (refresh_sphere) {
      kept_positions.insert(kept_positions.end(),
                            {decoded_positions[3 * v], decoded_positions[3 * v + 1], decoded_positions[3 * v + 2]});
    }
  }
  for (const auto tri : keep_tris) {
    for (std::size_t k = 0; k < 3; ++k) {
      const auto index = remap[read_int<std::uint16_t>(bytes, layout.triangles_at + (tri * 3 + k) * 2)];
      append_int(out, static_cast<std::uint16_t>(index));
    }
  }
  const auto tail_at = layout.triangles_at + layout.triangle_count * k_triangle_bytes;
  out.insert(out.end(), bytes.begin() + static_cast<std::ptrdiff_t>(tail_at), bytes.end());
  bytes = std::move(out);
  write_counts(bytes, layout, static_cast<std::uint32_t>(keep_tris.size()), kept);
  if (refresh_sphere && !kept_positions.empty()) {
    auto points = shape_detail::to_vec3(kept_positions);
    shape_detail::write_sphere(bytes, layout, import_detail::bounding_sphere(points));
  }
  return source_of_vertex;
}

}  // namespace

std::vector<std::byte> join_shapes(const NifFile& nif, const JoinRequest& request, JoinReport* report) {
  if (request.blocks.size() < 2) throw std::invalid_argument("join_shapes needs at least two blocks");
  for (const auto block : request.blocks) {
    if (block >= nif.blocks().size()) throw std::invalid_argument("join_shapes names a block past the file");
    if (nif.blocks()[block].type != k_plain_type) {
      throw std::invalid_argument("join_shapes supports only BSTriShape; block " + std::to_string(block) + " is " +
                                  nif.blocks()[block].type);
    }
    CustomizationRemap owned;
    try {
      owned = read_customization_remap(nif, block);
    } catch (const parse_error& error) {
      throw std::invalid_argument("join_shapes cannot read the customization remap data of block " +
                                  std::to_string(block) + ": " + error.what());
    }
    if (owned.remap_block >= 0 || owned.new_bones_block >= 0) {
      throw std::invalid_argument("join_shapes does not merge customization remap data; block " +
                                  std::to_string(block) + " owns some");
    }
  }
  {
    std::set<std::uint32_t> unique(request.blocks.begin(), request.blocks.end());
    if (unique.size() != request.blocks.size()) throw std::invalid_argument("join_shapes repeats a block");
  }
  const std::uint32_t target = request.blocks[0];
  const auto target_shape = read_shape(nif, target);
  const auto target_layout = shape_detail::read_layout(nif, target);
  if (target_shape.verts.empty()) throw std::invalid_argument("the join target has no vertex data");
  const auto target_global = shape_to_global(nif, target);
  if (target_shape.material.empty()) throw std::invalid_argument("the join target has no material");

  struct Donor {
    std::uint32_t block;
    Shape shape;
    Layout layout;
    Transform global;
    std::vector<std::uint32_t> remap;
  };
  std::vector<Donor> donors;
  const bool skinned = target_layout.skinned;
  if (skinned && target_shape.skin_ref < 0) throw std::invalid_argument("the join target is skinned without a skin");
  auto model = write_detail::load_model(nif);
  SkinBlocks target_skin;
  if (skinned) {
    if (const auto reason = read_skin_blocks(nif, model, target, target_shape, target_skin)) {
      throw std::invalid_argument(*reason);
    }
  }
  std::vector<std::string> merged_names = target_skin.bone_names;
  std::map<std::string, std::uint32_t> merged_index;
  for (std::uint32_t i = 0; i < merged_names.size(); ++i) merged_index[merged_names[i]] = i;
  Bytes merged_transforms = target_skin.transforms;
  std::vector<std::int32_t> merged_refs = target_skin.bone_refs;

  std::size_t verts_before = target_shape.verts.size() / 3;
  std::size_t tris_before = target_shape.tris.size() / 3;
  for (std::size_t i = 1; i < request.blocks.size(); ++i) {
    const auto block = request.blocks[i];
    auto shape = read_shape(nif, block);
    if (shape.verts.empty()) throw std::invalid_argument("block " + std::to_string(block) + " has no vertex data");
    const auto layout = shape_detail::read_layout(nif, block);
    const Bytes& target_bytes = model.blocks[target];
    if (!same_layout(target_bytes, target_layout, model.blocks[block], layout)) {
      throw std::invalid_argument("block " + std::to_string(block) + " has a different vertex layout");
    }
    if (!same_transform(shape_to_global(nif, block), target_global)) {
      throw std::invalid_argument("block " + std::to_string(block) +
                                  " sits in a different transform; bake it first");
    }
    if (shape.material != target_shape.material) {
      throw std::invalid_argument("block " + std::to_string(block) + " has a different material");
    }
    if (shape.textures != target_shape.textures) {
      throw std::invalid_argument("block " + std::to_string(block) + " has different textures");
    }
    if (layout.skinned != skinned) {
      throw std::invalid_argument("block " + std::to_string(block) + " mixes skinned with unskinned");
    }
    Donor donor{block, std::move(shape), layout, shape_to_global(nif, block), {}};
    if (skinned) {
      SkinBlocks donor_skin;
      if (const auto reason = read_skin_blocks(nif, model, block, donor.shape, donor_skin)) {
        throw std::invalid_argument(*reason);
      }
      donor.remap.assign(donor_skin.bone_names.size(), 0);
      for (std::size_t b = 0; b < donor_skin.bone_names.size(); ++b) {
        const auto found = merged_index.find(donor_skin.bone_names[b]);
        if (found != merged_index.end()) {
          // Invariant: a shared bone keeps the target bind, so a different donor bind is a refusal, not a guess.
          const auto* want = merged_transforms.data() + found->second * k_bone_transform_bytes;
          const auto* have = donor_skin.transforms.data() + b * k_bone_transform_bytes;
          if (!std::equal(want, want + k_bone_transform_bytes, have)) {
            throw std::invalid_argument("bone \"" + donor_skin.bone_names[b] + "\" has a different bind transform");
          }
          donor.remap[b] = found->second;
          continue;
        }
        if (merged_names.size() >= k_max_bones) throw std::invalid_argument("the merged skin needs past 256 bones");
        donor.remap[b] = static_cast<std::uint32_t>(merged_names.size());
        merged_index[donor_skin.bone_names[b]] = static_cast<std::uint32_t>(merged_names.size());
        merged_names.push_back(donor_skin.bone_names[b]);
        merged_transforms.insert(merged_transforms.end(), donor_skin.transforms.begin() + b * k_bone_transform_bytes,
                                 donor_skin.transforms.begin() + (b + 1) * k_bone_transform_bytes);
        merged_refs.push_back(donor_skin.bone_refs[b]);
      }
    }
    verts_before += donor.shape.verts.size() / 3;
    tris_before += donor.shape.tris.size() / 3;
    donors.push_back(std::move(donor));
  }

  if (verts_before > 65535) throw std::invalid_argument("the joined shape needs past 65535 vertices");
  // Invariant: the splice concatenates target vertices, donor vertices, target triangles, donor triangles.
  // Invariant: donor skin bone indices are remapped to the merged palette.
  Bytes& target_bytes = model.blocks[target];
  std::uint32_t total_verts = static_cast<std::uint32_t>(target_layout.vertex_count);
  std::uint32_t total_tris = static_cast<std::uint32_t>(target_layout.triangle_count);
  {
    Bytes vertices(target_bytes.begin() + static_cast<std::ptrdiff_t>(target_layout.vertices_at),
                   target_bytes.begin() + static_cast<std::ptrdiff_t>(target_layout.vertices_at +
                                                                      target_layout.vertex_count *
                                                                          target_layout.stride));
    Bytes triangles(target_bytes.begin() + static_cast<std::ptrdiff_t>(target_layout.triangles_at),
                    target_bytes.begin() + static_cast<std::ptrdiff_t>(target_layout.triangles_at +
                                                                       target_layout.triangle_count *
                                                                           k_triangle_bytes));
    std::uint32_t base = static_cast<std::uint32_t>(target_layout.vertex_count);
    for (const auto& donor : donors) {
      const Bytes& donor_bytes = model.blocks[donor.block];
      for (std::size_t v = 0; v < donor.layout.vertex_count; ++v) {
        const auto from = donor_bytes.begin() +
                          static_cast<std::ptrdiff_t>(donor.layout.vertices_at + v * donor.layout.stride);
        Bytes vertex(from, from + static_cast<std::ptrdiff_t>(donor.layout.stride));
        if (skinned) {
          for (std::size_t k = 0; k < 4; ++k) {
            const auto old_index = std::to_integer<std::uint8_t>(vertex[donor.layout.skin_at + 8 + k]);
            // Constraint: a weighted slot that names a bone past its own skin is a refusal, not a read past the map.
            // Invariant: an unweighted slot carries no influence, so a stale index there falls back to bone 0.
            if (old_index >= donor.remap.size()) {
              if (read_int<std::uint16_t>(vertex, donor.layout.skin_at + 2 * k) == 0) {
                vertex[donor.layout.skin_at + 8 + k] = std::byte{0};
                continue;
              }
              throw std::invalid_argument("block " + std::to_string(donor.block) + " vertex " + std::to_string(v) +
                                          " names bone " + std::to_string(old_index) + ", past its skin");
            }
            vertex[donor.layout.skin_at + 8 + k] = std::byte{static_cast<std::uint8_t>(donor.remap[old_index])};
          }
        }
        vertices.insert(vertices.end(), vertex.begin(), vertex.end());
      }
      for (std::size_t t = 0; t < donor.layout.triangle_count * 3; ++t) {
        const auto index = read_int<std::uint16_t>(donor_bytes, donor.layout.triangles_at + t * 2);
        append_int(triangles, static_cast<std::uint16_t>(base + index));
      }
      base += static_cast<std::uint32_t>(donor.layout.vertex_count);
    }
    total_verts = base;
    total_tris = static_cast<std::uint32_t>(target_layout.triangle_count);
    for (const auto& donor : donors) total_tris += static_cast<std::uint32_t>(donor.layout.triangle_count);
    Bytes rebuilt(target_bytes.begin(), target_bytes.begin() + static_cast<std::ptrdiff_t>(target_layout.vertices_at));
    rebuilt.insert(rebuilt.end(), vertices.begin(), vertices.end());
    rebuilt.insert(rebuilt.end(), triangles.begin(), triangles.end());
    const auto tail_at = target_layout.triangles_at + target_layout.triangle_count * k_triangle_bytes;
    rebuilt.insert(rebuilt.end(), target_bytes.begin() + static_cast<std::ptrdiff_t>(tail_at), target_bytes.end());
    target_bytes = std::move(rebuilt);
    write_counts(target_bytes, target_layout, total_tris, total_verts);
  }
  if (skinned) {
    Bytes instance;
    append_int(instance, target_skin.skeleton_root);
    append_int(instance, static_cast<std::int32_t>(target_skin.bonedata));
    append_int(instance, static_cast<std::uint32_t>(merged_names.size()));
    for (const auto ref : merged_refs) append_int(instance, ref);
    instance.insert(instance.end(), target_skin.scales_trailer.begin(), target_skin.scales_trailer.end());
    model.blocks[target_skin.instance] = std::move(instance);
    Bytes bonedata;
    append_int(bonedata, static_cast<std::uint32_t>(merged_names.size()));
    bonedata.insert(bonedata.end(), merged_transforms.begin(), merged_transforms.end());
    model.blocks[target_skin.bonedata] = std::move(bonedata);
  }
  // Invariant: the sphere covers the merged decoded positions.
  {
    std::vector<float> positions = target_shape.verts;
    for (const auto& donor : donors) positions.insert(positions.end(), donor.shape.verts.begin(), donor.shape.verts.end());
    shape_detail::write_sphere(target_bytes, target_layout,
                               import_detail::bounding_sphere(shape_detail::to_vec3(positions)));
  }
  auto bytes = write_detail::serialize(model);
  // Invariant: donors are removed highest-first; the target index is tracked.
  std::vector<std::uint32_t> ordered;
  for (const auto& donor : donors) ordered.push_back(donor.block);
  std::sort(ordered.begin(), ordered.end(), std::greater<std::uint32_t>());
  std::uint32_t live_target = target;
  std::vector<std::uint32_t> removed;
  for (const auto donor : ordered) {
    const auto live = read_nif(bytes);
    bytes = remove_block(live, donor).bytes;
    if (donor < live_target) live_target--;
    removed.push_back(donor);
  }
  std::sort(removed.begin(), removed.end());
  const auto live = read_nif(bytes);
  const auto check = read_shape(live, live_target);
  if (check.verts.size() / 3 != verts_before || check.tris.size() / 3 != tris_before) {
    throw std::runtime_error("internal error: the joined shape does not hold every vertex");
  }
  if (report != nullptr) {
    // Invariant: before counts cover the target alone, after counts cover the joined shape.
    report->target = live_target;
    report->removed = removed;
    report->vertices_before = target_layout.vertex_count;
    report->vertices_after = verts_before;
    report->triangles_before = target_layout.triangle_count;
    report->triangles_after = tris_before;
  }
  return bytes;
}

std::vector<std::byte> separate_shape(const NifFile& nif, const SeparateRequest& request, SeparateReport* report) {
  if (request.groups.size() < 2) throw std::invalid_argument("separate_shape needs at least two groups");
  if (request.block >= nif.blocks().size()) throw std::invalid_argument("separate_shape names a block past the file");
  if (nif.blocks()[request.block].type != k_plain_type) {
    throw std::invalid_argument("separate_shape supports only BSTriShape");
  }
  const auto shape = read_shape(nif, request.block);
  const auto layout = shape_detail::read_layout(nif, request.block);
  const std::size_t faces = layout.triangle_count;
  std::vector<bool> covered(faces, false);
  for (std::size_t g = 0; g < request.groups.size(); ++g) {
    if (request.groups[g].empty()) throw std::invalid_argument("separate_shape group " + std::to_string(g) + " is empty");
    for (const auto tri : request.groups[g]) {
      if (tri >= faces) throw std::invalid_argument("separate_shape names triangle " + std::to_string(tri));
      if (covered[tri]) throw std::invalid_argument("separate_shape names triangle " + std::to_string(tri) + " twice");
      covered[tri] = true;
    }
  }
  if (!std::all_of(covered.begin(), covered.end(), [](bool v) { return v; })) {
    throw std::invalid_argument("separate_shape groups leave triangles uncovered");
  }
  CustomizationRemap remap;
  if (const auto reason = remap_detail::load_for_op(nif, request.block, remap)) {
    throw std::invalid_argument("separate_shape cannot follow the remap data: " + *reason);
  }
  const auto parent = nif.parent(request.block);
  if (parent < 0) throw std::invalid_argument("separate_shape needs a parented shape");
  const std::uint32_t parent_block = nif.nodes()[static_cast<std::size_t>(parent)].block;

  auto bytes = nif.bytes();
  const std::uint32_t copies = static_cast<std::uint32_t>(request.groups.size() - 1);
  for (std::uint32_t i = 0; i < copies; ++i) {
    const auto live = read_nif(bytes);
    bytes = duplicate_block(live, request.block);
  }
  for (std::uint32_t i = 0; i < copies; ++i) {
    const auto live = read_nif(bytes);
    bytes = append_child(live, parent_block, static_cast<std::uint32_t>(live.blocks().size() - copies + i));
  }
  const auto live = read_nif(bytes);
  auto model = write_detail::load_model(live);
  std::vector<std::uint32_t> blocks{request.block};
  for (std::uint32_t i = 0; i < copies; ++i) blocks.push_back(static_cast<std::uint32_t>(live.blocks().size() - copies + i));
  for (std::size_t g = 0; g < request.groups.size(); ++g) {
    auto group = request.groups[g];
    std::sort(group.begin(), group.end());
    const auto source_of_vertex = filter_geometry(model, blocks[g], layout, group, layout.full, shape.verts);
    if (remap.remap_block >= 0) {
      const auto records = reindex_remap_records(remap.records, source_of_vertex);
      // Invariant: each copy owns its own remap block, because the copies share the original's extra-data refs.
      const auto owned_block = g == 0 ? remap.remap_block : remap_detail::clone_extra(model, blocks[g], remap.remap_block);
      remap_detail::store_records(model, owned_block, records);
    }
  }
  bytes = write_detail::serialize(model);
  const auto check = read_nif(bytes);
  for (std::size_t g = 0; g < request.groups.size(); ++g) {
    if (read_shape(check, blocks[g]).tris.size() / 3 != request.groups[g].size()) {
      throw std::runtime_error("internal error: a separated shape lost triangles");
    }
  }
  if (report != nullptr) report->blocks = blocks;
  return bytes;
}

std::vector<std::byte> separate_by_bone(const NifFile& nif, std::uint32_t block, SeparateReport* report) {
  if (block >= nif.blocks().size()) throw std::invalid_argument("separate_by_bone names a block past the file");
  const auto shape = read_shape(nif, block);
  if (!shape_detail::read_layout(nif, block).skinned || shape.skin_ref < 0) {
    throw std::invalid_argument("separate_by_bone needs a skinned shape");
  }
  if (shape.bone_indices.size() != shape.verts.size() / 3 * 4) {
    throw std::invalid_argument("separate_by_bone needs per-vertex bone indices");
  }
  std::map<std::uint32_t, std::vector<std::uint32_t>> groups;
  const std::size_t faces = shape.tris.size() / 3;
  for (std::size_t t = 0; t < faces; ++t) {
    std::map<std::uint32_t, float> total;
    for (std::size_t k = 0; k < 3; ++k) {
      const auto v = shape.tris[3 * t + k];
      for (std::size_t s = 0; s < 4; ++s) {
        total[shape.bone_indices[4 * v + s]] += shape.weights[4 * v + s];
      }
    }
    std::uint32_t best = 0;
    float best_weight = 0.0f;
    for (const auto& [bone, weight] : total) {
      if (weight > best_weight) {
        best_weight = weight;
        best = bone;
      }
    }
    if (!(best_weight > 0.0f)) {
      throw std::invalid_argument("triangle " + std::to_string(t) + " has no positive bone weight");
    }
    groups[best].push_back(static_cast<std::uint32_t>(t));
  }
  if (groups.size() < 2) throw std::invalid_argument("separate_by_bone needs triangles on two bones at least");
  SeparateRequest request{block, {}};
  for (auto& [bone, tris] : groups) request.groups.push_back(std::move(tris));
  return separate_shape(nif, request, report);
}

}  // namespace fo4::nif
