#include "fo4/nif/skin_create.hpp"

#include "fo4/binary/cursor.hpp"
#include "fo4/error.hpp"
#include "fo4/nif/structure.hpp"
#include "nif_internal.hpp"
#include "shape_ops_internal.hpp"
#include "write_model.hpp"

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <map>
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

constexpr std::size_t k_skin_record_bytes = 12;
constexpr std::size_t k_bone_transform_bytes = 68;
constexpr std::size_t k_max_bones = 256;
// Invariant: attribute N's offset field sits at bit 4N+4 of the vertex descriptor; skinning is attribute 6.
constexpr unsigned k_skin_offset_shift = 28;
constexpr std::string_view k_shape_type = "BSTriShape";
constexpr std::string_view k_skin_type = "BSSkin::Instance";
constexpr std::string_view k_bonedata_type = "BSSkin::BoneData";
constexpr std::string_view k_dismember_type = "BSDismemberSkinInstance";

std::uint16_t type_index(Model& model, std::string_view type) {
  for (std::size_t i = 0; i < model.block_types.size(); ++i) {
    if (model.block_types[i] == type) return static_cast<std::uint16_t>(i);
  }
  model.block_types.emplace_back(type);
  return static_cast<std::uint16_t>(model.block_types.size() - 1);
}

// Constraint: the instance cursor reads the FO4 tail only to skip it.
// Invariant: the scales are preserved verbatim by the splice, never interpreted.
struct InstanceParts {
  std::int32_t skeleton_root{-1};
  std::int32_t data{-1};
  std::vector<std::int32_t> bone_refs;
  Bytes scales_trailer;
};

InstanceParts parse_instance(const Bytes& bytes, std::uint32_t shape) {
  if (bytes.size() < 12) throw parse_error(0, "block " + std::to_string(shape) + " has a truncated skin instance");
  InstanceParts parts;
  parts.skeleton_root = read_int<std::int32_t>(bytes, 0);
  parts.data = read_int<std::int32_t>(bytes, 4);
  // Constraint: counts are widened before they are scaled, so a huge count cannot wrap past the size check.
  const std::size_t count = read_int<std::uint32_t>(bytes, 8);
  if (count > (bytes.size() - 12) / 4) {
    throw parse_error(0, "block " + std::to_string(shape) + " has a truncated bone list");
  }
  for (std::size_t i = 0; i < count; ++i) parts.bone_refs.push_back(read_int<std::int32_t>(bytes, 12 + 4 * i));
  const std::size_t trailer_at = 12 + count * 4;
  if (trailer_at + 4 > bytes.size()) {
    throw parse_error(0, "block " + std::to_string(shape) + " has a truncated scale count");
  }
  const std::size_t scales = read_int<std::uint32_t>(bytes, trailer_at);
  if (scales > (bytes.size() - trailer_at - 4) / 12) {
    throw parse_error(0, "block " + std::to_string(shape) + " has truncated bone scales");
  }
  parts.scales_trailer.assign(bytes.begin() + static_cast<std::ptrdiff_t>(trailer_at), bytes.end());
  return parts;
}

Bytes bonedata_transforms(const Model& model, std::int32_t data, std::uint32_t shape, std::size_t bones) {
  if (data < 0 || static_cast<std::size_t>(data) >= model.blocks.size()) {
    throw std::invalid_argument("block " + std::to_string(shape) + " has no bone data block");
  }
  const Bytes& bytes = model.blocks[static_cast<std::uint32_t>(data)];
  if (bytes.size() < 4 || read_int<std::uint32_t>(bytes, 0) != bones ||
      bytes.size() != 4 + bones * k_bone_transform_bytes) {
    throw std::invalid_argument("block " + std::to_string(shape) + " has bone data of an unexpected size");
  }
  return Bytes(bytes.begin() + 4, bytes.end());
}

void append_bone_transform(Bytes& out, const Bone& bone) {
  for (const float value : bone.center) append_int(out, std::bit_cast<std::uint32_t>(value));
  append_int(out, std::bit_cast<std::uint32_t>(bone.radius));
  for (const float value : bone.rotation) append_int(out, std::bit_cast<std::uint32_t>(value));
  for (const float value : bone.translation) append_int(out, std::bit_cast<std::uint32_t>(value));
  append_int(out, std::bit_cast<std::uint32_t>(bone.scale));
}

bool uses_instance(const NifFile& nif, std::int32_t instance, std::uint32_t except_shape) {
  std::size_t users = 0;
  for (const auto& shape : read_shapes(nif)) {
    if (shape.skin_ref == instance && shape.block != except_shape) users++;
  }
  return users > 0;
}

std::string node_name(const NifFile& nif, std::int32_t block) {
  if (block < 0 || static_cast<std::size_t>(block) >= nif.blocks().size()) return {};
  const auto& info = nif.blocks()[static_cast<std::uint32_t>(block)];
  if (info.size < 4) return {};
  auto cursor = detail::block_cursor(nif, static_cast<std::uint32_t>(block));
  const auto index = cursor.read_i32();
  if (index < 0 || static_cast<std::size_t>(index) >= nif.strings().size()) return {};
  return nif.strings()[static_cast<std::size_t>(index)];
}

}  // namespace

DismemberSkin read_dismember_skin(const NifFile& nif, std::uint32_t instance_block) {
  if (instance_block >= nif.blocks().size()) throw std::invalid_argument("dismember block is past the file");
  const auto& info = nif.blocks()[instance_block];
  if (info.type != k_dismember_type) {
    throw std::invalid_argument("block " + std::to_string(instance_block) + " is " + info.type + ", not " +
                                std::string(k_dismember_type));
  }
  if (info.size < 20) throw parse_error(info.offset, "dismember block is shorter than its six header fields");
  auto cursor = detail::block_cursor(nif, instance_block);
  DismemberSkin skin;
  const auto data = cursor.read_i32();
  skin.skin_partition_ref = cursor.read_i32();
  skin.skeleton_root = cursor.read_i32();
  const auto bones = cursor.read_u32();
  if (static_cast<std::size_t>(bones) > cursor.remaining() / 4) {
    throw parse_error(cursor.absolute_offset(), "dismember bone list runs past the end of its block");
  }
  for (std::uint32_t i = 0; i < bones; ++i) skin.bone_refs.push_back(cursor.read_i32());
  if (cursor.remaining() < 4) throw parse_error(cursor.absolute_offset(), "dismember block has no partition count");
  const auto partitions = cursor.read_u32();
  if (static_cast<std::size_t>(partitions) > cursor.remaining() / 4) {
    throw parse_error(cursor.absolute_offset(), "dismember partition list runs past the end of its block");
  }
  for (std::uint32_t i = 0; i < partitions; ++i) {
    DismemberPartition part;
    part.flags = cursor.read_u16();
    part.body_part = cursor.read_u16();
    skin.partitions.push_back(part);
  }
  for (const auto ref : skin.bone_refs) {
    const auto name = node_name(nif, ref);
    if (name.empty() && (ref < 0 || static_cast<std::size_t>(ref) >= nif.blocks().size())) {
      throw parse_error(info.offset, "dismember block names a bone past the file");
    }
    skin.bone_names.push_back(name);
  }
  if (data < 0 || static_cast<std::size_t>(data) >= nif.blocks().size()) {
    throw parse_error(info.offset, "dismember block has no bone data block");
  }
  if (nif.blocks()[static_cast<std::uint32_t>(data)].type != k_bonedata_type) {
    throw parse_error(nif.blocks()[static_cast<std::uint32_t>(data)].offset,
                      "dismember bone data is not BSSkin::BoneData");
  }
  skin.bonedata_ref = data;
  const auto& data_info = nif.blocks()[static_cast<std::uint32_t>(data)];
  if (data_info.size != 4 + bones * k_bone_transform_bytes) {
    throw parse_error(data_info.offset, "dismember bone data does not hold one transform per bone");
  }
  return skin;
}

std::vector<std::byte> create_skin(const NifFile& nif, std::uint32_t shape, std::int32_t skeleton_root,
                                   const std::vector<Bone>& bones) {
  if (shape >= nif.blocks().size()) throw std::invalid_argument("create_skin names a shape past the file");
  if (nif.blocks()[shape].type != k_shape_type) {
    throw std::invalid_argument("create_skin supports only BSTriShape");
  }
  if (bones.empty() || bones.size() > k_max_bones - 1) {
    throw std::invalid_argument("create_skin needs between 1 and 255 bones");
  }
  if (skeleton_root < 0 || static_cast<std::size_t>(skeleton_root) >= nif.blocks().size() ||
      nif.node_for_block(static_cast<std::uint32_t>(skeleton_root)) < 0) {
    throw std::invalid_argument("create_skin needs a skeleton root node");
  }
  std::set<std::string> seen;
  for (const auto& bone : bones) {
    if (bone.name.empty()) throw std::invalid_argument("create_skin needs a name for every bone");
    if (!seen.insert(bone.name).second) throw std::invalid_argument("create_skin names bone \"" + bone.name + "\" twice");
    if (bone.block < 0 || static_cast<std::size_t>(bone.block) >= nif.blocks().size() ||
        nif.node_for_block(static_cast<std::uint32_t>(bone.block)) < 0) {
      throw std::invalid_argument("bone \"" + bone.name + "\" does not name a node block");
    }
  }
  const auto decoded = read_shape(nif, shape);
  if (decoded.verts.empty()) throw std::invalid_argument("create_skin needs a shape with vertex data");
  const auto layout = shape_detail::read_layout(nif, shape);
  if (layout.skinned || decoded.skin_ref >= 0) throw std::invalid_argument("the shape is already skinned");

  auto model = write_detail::load_model(nif);
  // Invariant: the 12-byte skin record goes after the colour quad; the tail slot stays last.
  const std::size_t skin_at = layout.skin_at;
  const std::size_t new_stride = layout.stride + k_skin_record_bytes;
  if (new_stride / 4 > 0xF) throw std::runtime_error("internal error: the skinned stride past the descriptor nibble");
  if (skin_at / 4 > 0xF) throw std::runtime_error("internal error: the skin offset past the descriptor nibble");
  if (new_stride % 4 != 0) throw std::runtime_error("internal error: the skinned stride is not a whole word");
  Bytes& bytes = model.blocks[shape];
  Bytes grown;
  grown.reserve(bytes.size() + layout.vertex_count * k_skin_record_bytes);
  grown.insert(grown.end(), bytes.begin(), bytes.begin() + static_cast<std::ptrdiff_t>(layout.vertices_at));
  for (std::size_t v = 0; v < layout.vertex_count; ++v) {
    const auto from = bytes.begin() + static_cast<std::ptrdiff_t>(layout.vertices_at + v * layout.stride);
    grown.insert(grown.end(), from, from + static_cast<std::ptrdiff_t>(skin_at));
    grown.insert(grown.end(), k_skin_record_bytes, std::byte{0});
    grown.insert(grown.end(), from + static_cast<std::ptrdiff_t>(skin_at),
                 from + static_cast<std::ptrdiff_t>(layout.stride));
  }
  // Invariant: the grown vertex block keeps the original triangle list and everything after it.
  grown.insert(grown.end(), bytes.begin() + static_cast<std::ptrdiff_t>(layout.triangles_at), bytes.end());
  // Invariant: the descriptor sets the skin flag and the widened stride nibble.
  // Invariant: the skin offset field sits at bit 28 (attribute 6) and holds the offset in words.
  // Invariant: every other attribute offset field is preserved, the colour offset at bit 24 included.
  const auto descriptor = read_int<std::uint64_t>(grown, layout.vertices_at - 18);
  const auto flags = static_cast<std::uint64_t>(static_cast<std::uint32_t>(descriptor >> 44U) | (1U << 6));
  const std::uint64_t low44 = descriptor & ((std::uint64_t{1} << 44U) - 1);
  const std::uint64_t stride_nibble = new_stride / 4;
  const std::uint64_t skin_nibble = skin_at / 4;
  const std::uint64_t kept = low44 & ~(std::uint64_t{0xF} | (std::uint64_t{0xF} << k_skin_offset_shift));
  const std::uint64_t rebuilt = kept | stride_nibble | (skin_nibble << k_skin_offset_shift) | (flags << 44U);
  write_int(grown, layout.vertices_at - 18, rebuilt);
  write_int(grown, layout.vertices_at - 4,
            static_cast<std::uint32_t>(layout.vertex_count * new_stride + layout.triangle_count * 6));
  bytes = std::move(grown);

  Bytes bonedata;
  append_int(bonedata, static_cast<std::uint32_t>(bones.size()));
  for (const auto& bone : bones) append_bone_transform(bonedata, bone);
  const auto bonedata_block = static_cast<std::int32_t>(model.blocks.size());
  model.blocks.push_back(std::move(bonedata));
  model.block_type_index.push_back(type_index(model, k_bonedata_type));
  Bytes instance;
  append_int(instance, skeleton_root);
  append_int(instance, bonedata_block);
  append_int(instance, static_cast<std::uint32_t>(bones.size()));
  for (const auto& bone : bones) append_int(instance, bone.block);
  append_int(instance, std::uint32_t{0});
  const auto instance_block = static_cast<std::int32_t>(model.blocks.size());
  model.blocks.push_back(std::move(instance));
  model.block_type_index.push_back(type_index(model, k_skin_type));
  // Invariant: the skin reference follows the bounding sphere, 52 bytes of transform plus 4 of collision first.
  // Constraint: the block vector grew, so the shape is addressed by index rather than the stale reference.
  write_int(model.blocks[shape], layout.transform_at + 52 + 4 + 16, instance_block);
  auto out = write_detail::serialize(model);
  const auto check = read_shape(read_nif(out), shape);
  if (!shape_detail::read_layout(read_nif(out), shape).skinned || check.bones.size() != bones.size()) {
    throw std::runtime_error("internal error: the created skin does not read back");
  }
  return out;
}

std::vector<std::byte> attach_partitions(const NifFile& nif, std::uint32_t shape,
                                         const std::vector<DismemberPartition>& partitions) {
  if (shape >= nif.blocks().size()) throw std::invalid_argument("attach_partitions names a shape past the file");
  if (partitions.empty()) throw std::invalid_argument("attach_partitions needs at least one partition");
  if (partitions.size() > 0xFFFFFFFFU) throw std::invalid_argument("attach_partitions holds too many partitions");
  const auto decoded = read_shape(nif, shape);
  if (decoded.skin_ref < 0) throw std::invalid_argument("the shape has no skin to partition");
  const auto instance = static_cast<std::uint32_t>(decoded.skin_ref);
  if (nif.blocks()[instance].type != k_skin_type) {
    throw std::invalid_argument("the shape is not skinned through BSSkin::Instance");
  }
  if (uses_instance(nif, decoded.skin_ref, shape)) {
    throw std::invalid_argument("the skin instance is shared, so partitioning it would move other shapes");
  }
  auto model = write_detail::load_model(nif);
  const auto parts = parse_instance(model.blocks[instance], shape);
  Bytes dismember;
  append_int(dismember, parts.data);
  append_int(dismember, std::int32_t{-1});
  append_int(dismember, parts.skeleton_root);
  append_int(dismember, static_cast<std::uint32_t>(parts.bone_refs.size()));
  for (const auto ref : parts.bone_refs) append_int(dismember, ref);
  append_int(dismember, static_cast<std::uint32_t>(partitions.size()));
  for (const auto& part : partitions) {
    append_int(dismember, part.flags);
    append_int(dismember, part.body_part);
  }
  model.blocks.push_back(std::move(dismember));
  model.block_type_index.push_back(type_index(model, k_dismember_type));
  const auto dismember_block = static_cast<std::int32_t>(model.blocks.size() - 1);
  const auto layout = shape_detail::read_layout(nif, shape);
  write_int(model.blocks[shape], layout.transform_at + 52 + 4 + 16, dismember_block);
  auto bytes = write_detail::serialize(model);
  const auto dropped = remove_block(read_nif(bytes), instance);
  const auto relocated = read_nif(dropped.bytes);
  bool found = false;
  for (std::uint32_t b = 0; b < relocated.blocks().size(); ++b) {
    if (relocated.blocks()[b].type != k_dismember_type) continue;
    const auto check = read_dismember_skin(relocated, b);
    if (check.partitions.size() == partitions.size()) found = true;
  }
  if (!found) throw std::runtime_error("internal error: the partitions do not read back");
  return dropped.bytes;
}

std::vector<std::byte> adopt_donor_bones(const NifFile& nif, std::uint32_t target_shape, std::uint32_t donor_shape,
                                         std::uint32_t* adopted) {
  if (target_shape >= nif.blocks().size() || donor_shape >= nif.blocks().size()) {
    throw std::invalid_argument("adopt_donor_bones names a shape past the file");
  }
  if (target_shape == donor_shape) throw std::invalid_argument("a shape cannot adopt its own bones");
  const auto target = read_shape(nif, target_shape);
  if (target.skin_ref < 0) throw std::invalid_argument("the target shape has no skin to extend");
  if (nif.blocks()[static_cast<std::uint32_t>(target.skin_ref)].type != k_skin_type) {
    throw std::invalid_argument("adopt_donor_bones extends only BSSkin::Instance skins");
  }
  if (uses_instance(nif, target.skin_ref, target_shape)) {
    throw std::invalid_argument("the target skin instance is shared, so extending it would move other shapes");
  }
  const auto donor = read_shape(nif, donor_shape);
  if (donor.skin_ref < 0) throw std::invalid_argument("the donor shape has no skin");
  auto model = write_detail::load_model(nif);
  // Invariant: donor bones carry bind transforms and node refs from either skin flavour.
  std::vector<std::string> donor_names;
  std::vector<std::int32_t> donor_refs;
  Bytes donor_transforms;
  if (nif.blocks()[static_cast<std::uint32_t>(donor.skin_ref)].type == k_skin_type) {
    if (donor.bones.empty()) throw std::invalid_argument("the donor skin has no resolvable bones");
    const auto parts = parse_instance(model.blocks[static_cast<std::uint32_t>(donor.skin_ref)], donor_shape);
    donor_transforms = bonedata_transforms(model, parts.data, donor_shape, parts.bone_refs.size());
    if (donor.bones.size() != parts.bone_refs.size()) {
      throw std::invalid_argument("the donor decoded a different bone count than its instance holds");
    }
    donor_refs = parts.bone_refs;
    for (const auto& bone : donor.bones) donor_names.push_back(bone.name);
  } else if (nif.blocks()[static_cast<std::uint32_t>(donor.skin_ref)].type == k_dismember_type) {
    const auto skin = read_dismember_skin(nif, static_cast<std::uint32_t>(donor.skin_ref));
    if (skin.bone_names.empty()) throw std::invalid_argument("the donor skin has no resolvable bones");
    donor_transforms = bonedata_transforms(model, skin.bonedata_ref, donor_shape, skin.bone_refs.size());
    donor_refs = skin.bone_refs;
    donor_names = skin.bone_names;
  } else {
    throw std::invalid_argument("the donor skin is of an unsupported type");
  }
  std::set<std::string> seen;
  for (const auto& name : donor_names) {
    if (name.empty()) throw std::invalid_argument("the donor has a bone with no name");
    if (!seen.insert(name).second) throw std::invalid_argument("the donor names bone \"" + name + "\" twice");
  }
  const auto target_parts = parse_instance(model.blocks[static_cast<std::uint32_t>(target.skin_ref)], target_shape);
  if (target.bones.size() != target_parts.bone_refs.size()) {
    throw std::invalid_argument("the target decoded a different bone count than its instance holds");
  }
  std::set<std::string> target_names;
  for (const auto& bone : target.bones) target_names.insert(bone.name);
  Bytes merged = bonedata_transforms(model, target_parts.data, target_shape, target_parts.bone_refs.size());
  std::vector<std::int32_t> refs = target_parts.bone_refs;
  std::uint32_t count = 0;
  for (std::size_t b = 0; b < donor_names.size(); ++b) {
    if (target_names.count(donor_names[b]) != 0) continue;
    if (refs.size() >= k_max_bones) throw std::invalid_argument("the extended skin needs past 256 bones");
    if (donor_refs[b] < 0 || static_cast<std::size_t>(donor_refs[b]) >= nif.blocks().size()) {
      throw std::invalid_argument("the donor names a bone past the file");
    }
    merged.insert(merged.end(), donor_transforms.begin() + b * k_bone_transform_bytes,
                  donor_transforms.begin() + (b + 1) * k_bone_transform_bytes);
    refs.push_back(donor_refs[b]);
    count++;
  }
  Bytes bonedata;
  append_int(bonedata, static_cast<std::uint32_t>(refs.size()));
  bonedata.insert(bonedata.end(), merged.begin(), merged.end());
  model.blocks[static_cast<std::uint32_t>(target_parts.data)] = std::move(bonedata);
  Bytes instance;
  append_int(instance, target_parts.skeleton_root);
  append_int(instance, target_parts.data);
  append_int(instance, static_cast<std::uint32_t>(refs.size()));
  for (const auto ref : refs) append_int(instance, ref);
  instance.insert(instance.end(), target_parts.scales_trailer.begin(), target_parts.scales_trailer.end());
  model.blocks[static_cast<std::uint32_t>(target.skin_ref)] = std::move(instance);
  auto bytes = write_detail::serialize(model);
  const auto check = read_shape(read_nif(bytes), target_shape);
  if (check.bones.size() != refs.size()) {
    throw std::runtime_error("internal error: the extended skin does not read back");
  }
  if (adopted != nullptr) *adopted = count;
  return bytes;
}

std::vector<std::byte> paint_dab(const NifFile& nif, std::uint32_t shape, const PaintDab& dab,
                                 std::uint32_t* changed) {
  if (shape >= nif.blocks().size()) throw std::invalid_argument("paint_dab names a shape past the file");
  if (!(dab.radius > 0.0f) || !std::isfinite(dab.radius)) {
    throw std::invalid_argument("the brush radius must be positive and finite");
  }
  if (!std::isfinite(dab.center[0]) || !std::isfinite(dab.center[1]) || !std::isfinite(dab.center[2])) {
    throw std::invalid_argument("the brush center must be finite");
  }
  if (!(dab.weight >= 0.0f && dab.weight <= 1.0f) || !std::isfinite(dab.weight)) {
    throw std::invalid_argument("the brush weight must be between 0 and 1");
  }
  if (!(dab.strength >= 0.0f) || !std::isfinite(dab.strength)) {
    throw std::invalid_argument("the brush strength must not be negative");
  }
  const auto decoded = read_shape(nif, shape);
  if (decoded.verts.empty()) throw std::invalid_argument("paint_dab needs a shape with vertex data");
  const auto layout = shape_detail::read_layout(nif, shape);
  if (!layout.skinned || decoded.skin_ref < 0) throw std::invalid_argument("paint_dab needs a skinned shape");
  if (decoded.bones.empty()) throw std::invalid_argument("the skin has no resolvable bones");
  if (dab.bone >= decoded.bones.size()) throw std::invalid_argument("the brush names a bone past the skin");
  if (decoded.bone_indices.size() != decoded.verts.size() / 3 * 4) {
    throw std::invalid_argument("the skin weights do not cover the vertices");
  }
  const auto xf = shape_to_global(nif, shape);
  std::uint32_t painted = 0;
  auto model = write_detail::load_model(nif);
  Bytes& bytes = model.blocks[shape];
  const float radius_sq = dab.radius * dab.radius;
  for (std::size_t v = 0; v < decoded.verts.size() / 3; ++v) {
    const std::array<float, 3> local{decoded.verts[3 * v], decoded.verts[3 * v + 1], decoded.verts[3 * v + 2]};
    const auto r0 = xf.r[0] * local[0] + xf.r[1] * local[1] + xf.r[2] * local[2];
    const auto r1 = xf.r[3] * local[0] + xf.r[4] * local[1] + xf.r[5] * local[2];
    const auto r2 = xf.r[6] * local[0] + xf.r[7] * local[1] + xf.r[8] * local[2];
    const std::array<float, 3> world{xf.s * r0 + xf.t[0], xf.s * r1 + xf.t[1], xf.s * r2 + xf.t[2]};
    const float dx = world[0] - dab.center[0];
    const float dy = world[1] - dab.center[1];
    const float dz = world[2] - dab.center[2];
    const float dist_sq = dx * dx + dy * dy + dz * dz;
    if (!(dist_sq < radius_sq)) continue;
    // Invariant: the cubic smoothstep matches Outfit Studio's brush falloff, full at the center, none at the rim.
    const float t = 1.0f - std::sqrt(dist_sq) / dab.radius;
    const float falloff = t * t * (3.0f - 2.0f * t);
    const float effect = std::min(dab.strength * falloff, 1.0f);
    if (!(effect > 0.0f)) continue;
    std::array<float, 4> weights{};
    std::array<std::uint8_t, 4> bones{};
    for (std::size_t k = 0; k < 4; ++k) {
      weights[k] = decoded.weights[4 * v + k];
      bones[k] = decoded.bone_indices[4 * v + k];
      if (!std::isfinite(weights[k]) || weights[k] < 0.0f) {
        throw std::invalid_argument("vertex " + std::to_string(v) + " has an invalid weight");
      }
    }
    bool found = false;
    for (auto& bone : bones) {
      if (bone == dab.bone) {
        found = true;
        break;
      }
    }
    const float target = dab.add ? 1.0f : dab.weight;
    if (dab.add) {
      for (std::size_t k = 0; k < 4; ++k) {
        if (bones[k] == dab.bone) weights[k] = std::min(weights[k] + dab.weight * effect, 1.0f);
      }
      if (!found) {
        std::size_t smallest = 0;
        for (std::size_t k = 1; k < 4; ++k) {
          if (weights[k] < weights[smallest]) smallest = k;
        }
        if (dab.weight * effect > weights[smallest]) {
          bones[smallest] = static_cast<std::uint8_t>(dab.bone);
          weights[smallest] = dab.weight * effect;
        } else {
          continue;
        }
      }
    } else {
      for (std::size_t k = 0; k < 4; ++k) {
        if (bones[k] == dab.bone) weights[k] += (target - weights[k]) * effect;
      }
      if (!found) {
        std::size_t smallest = 0;
        for (std::size_t k = 1; k < 4; ++k) {
          if (weights[k] < weights[smallest]) smallest = k;
        }
        if (target * effect > weights[smallest]) {
          bones[smallest] = static_cast<std::uint8_t>(dab.bone);
          weights[smallest] = target * effect;
        } else {
          continue;
        }
      }
    }
    float total = weights[0] + weights[1] + weights[2] + weights[3];
    if (!(total > 0.0f)) continue;
    for (auto& weight : weights) weight /= total;
    const auto at = layout.vertices_at + v * layout.stride + layout.skin_at;
    bool differs = false;
    for (std::size_t k = 0; k < 4; ++k) {
      if (bytes[at + 8 + k] != std::byte{bones[k]}) differs = true;
    }
    for (std::size_t k = 0; k < 4; ++k) {
      const auto bits = write_detail::half_bits(weights[k]);
      if (read_int<std::uint16_t>(bytes, at + 2 * k) != bits) differs = true;
    }
    if (!differs) continue;
    for (std::size_t k = 0; k < 4; ++k) {
      write_int(bytes, at + 2 * k, write_detail::half_bits(weights[k]));
      bytes[at + 8 + k] = std::byte{bones[k]};
    }
    painted++;
  }
  if (changed != nullptr) *changed = painted;
  if (painted == 0) return nif.bytes();
  return write_detail::serialize(model);
}

}  // namespace fo4::nif
