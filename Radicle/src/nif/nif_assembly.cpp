#include "fo4/nif/nif_assembly.hpp"

#include "fo4/binary/cursor.hpp"
#include "fo4/error.hpp"
#include "fo4/nif/block_ops.hpp"
#include "fo4/nif/shape_ops.hpp"
#include "fo4/nif/skin_create.hpp"
#include "nif_internal.hpp"
#include "write_model.hpp"
#include "write_refs.hpp"

#include <nlohmann/json.hpp>

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

using write_detail::Bytes;
using write_detail::Model;
using write_detail::read_int;
using write_detail::write_int;

constexpr std::size_t k_bone_transform_bytes = 68;

std::int32_t remap_block(std::int32_t ref, std::uint32_t donor_blocks, std::uint32_t base_blocks,
                         std::uint32_t donor_block) {
  if (ref < 0) return -1;
  if (static_cast<std::uint32_t>(ref) >= donor_blocks) {
    throw parse_error(0, "donor block " + std::to_string(donor_block) + " names a block past the donor file");
  }
  return static_cast<std::int32_t>(base_blocks + static_cast<std::uint32_t>(ref));
}

void remap_ref_at(Bytes& bytes, std::size_t at, std::uint32_t donor_blocks, std::uint32_t base_blocks,
                  std::uint32_t donor_block) {
  write_int(bytes, at, remap_block(read_int<std::int32_t>(bytes, at), donor_blocks, base_blocks, donor_block));
}

// Invariant: the NiAVObject prefix order matches read_av_header.
// Constraint: counts are widened before they are scaled, so a huge count cannot wrap past the size check.
// Invariant: a donor NiAVObject keeps its name through string_remap and carries no collision object.
void remap_av_name(const NifFile& donor, std::uint32_t donor_block, Bytes& bytes,
                   const std::vector<std::int32_t>& string_remap) {
  const auto& info = donor.blocks()[donor_block];
  if (bytes.size() < 16) {
    throw parse_error(0, "donor block " + std::to_string(donor_block) + " is shorter than its object prefix");
  }
  const std::size_t extras = read_int<std::uint32_t>(bytes, 4);
  if (8 + extras * 4 + 4 + 4 + 52 > bytes.size()) {
    throw parse_error(0, "donor block " + std::to_string(donor_block) + " is shorter than its object prefix");
  }
  const auto name = read_int<std::int32_t>(bytes, 0);
  if (name == -1) {
    // Invariant: a nameless donor block stays nameless.
  } else if (name < 0 || static_cast<std::size_t>(name) >= string_remap.size()) {
    throw parse_error(info.offset, "donor block names a string past the donor file");
  } else {
    write_int(bytes, 0, string_remap[static_cast<std::size_t>(name)]);
  }
  const std::size_t at = 8 + extras * 4 + 4 + 4 + 52;
  if (at + 4 > bytes.size()) {
    throw parse_error(info.offset, "donor block is shorter than its collision reference");
  }
  if (read_int<std::int32_t>(bytes, at) >= 0) {
    throw std::invalid_argument("donor block " + std::to_string(donor_block) + " carries collision");
  }
}

}  // namespace

std::vector<std::byte> merge_nif_into(const NifFile& base, const NifFile& donor, std::uint32_t parent) {
  if (parent >= base.blocks().size() || base.node_for_block(parent) < 0) {
    throw std::invalid_argument("merge_nif_into needs a base node to parent the donor roots");
  }
  if (donor.blocks().empty()) return base.bytes();
  auto model = write_detail::load_model(base);
  const auto donor_model = write_detail::load_model(donor);
  const std::uint32_t base_blocks = static_cast<std::uint32_t>(model.blocks.size());
  const std::uint32_t donor_blocks = static_cast<std::uint32_t>(donor.blocks().size());

  std::map<std::string, std::int32_t> string_index;
  for (std::size_t i = 0; i < model.strings.size(); ++i) string_index[model.strings[i]] = static_cast<std::int32_t>(i);
  std::vector<std::int32_t> string_remap(donor.strings().size(), -1);
  for (std::size_t i = 0; i < donor.strings().size(); ++i) {
    const auto found = string_index.find(donor.strings()[i]);
    if (found != string_index.end()) {
      string_remap[i] = found->second;
      continue;
    }
    string_remap[i] = static_cast<std::int32_t>(model.strings.size());
    string_index[donor.strings()[i]] = string_remap[i];
    model.strings.push_back(donor.strings()[i]);
  }
  const auto remap_string_at = [&](Bytes& bytes, std::size_t at, std::uint32_t donor_block) {
    const auto ref = read_int<std::int32_t>(bytes, at);
    if (ref < 0) return;
    if (static_cast<std::size_t>(ref) >= string_remap.size()) {
      throw parse_error(0, "donor block " + std::to_string(donor_block) + " names a string past the donor file");
    }
    write_int(bytes, at, string_remap[static_cast<std::size_t>(ref)]);
  };

  std::map<std::string, std::uint16_t> type_index;
  for (std::size_t i = 0; i < model.block_types.size(); ++i) type_index[model.block_types[i]] = static_cast<std::uint16_t>(i);

  for (std::uint32_t d = 0; d < donor_blocks; ++d) {
    const auto& info = donor.blocks()[d];
    Bytes bytes(donor.bytes().begin() + static_cast<std::ptrdiff_t>(info.offset),
                donor.bytes().begin() + static_cast<std::ptrdiff_t>(info.offset + info.size));
    const std::string type = info.type;
    bool follow_refs = true;
    if (type == "BSSkin::BoneData") {
      // Invariant: bone transforms carry no references.
      follow_refs = false;
    } else if (type == "NiAlphaProperty") {
      // Invariant: the name, extra-data list and controller follow the NiObjectNET prefix.
      if (bytes.size() < 12) throw parse_error(info.offset, "donor alpha property is truncated");
      const std::size_t extras = read_int<std::uint32_t>(bytes, 4);
      if (12 + extras * 4 > bytes.size()) throw parse_error(info.offset, "donor alpha property is truncated");
      remap_string_at(bytes, 0, d);
    } else if (type == "NiStringExtraData") {
      // Invariant: NiExtraData is the name alone (no extra-data list), then the value as a second string index.
      if (bytes.size() < 8) throw parse_error(info.offset, "donor string extra data is truncated");
      remap_string_at(bytes, 0, d);
      remap_string_at(bytes, 4, d);
      follow_refs = false;
    } else if (type == "BSShaderTextureSet") {
      // Invariant: inline sized strings only.
      follow_refs = false;
    } else if (type == "NiTransformData" || type == "NiKeyframeData" || type == "NiPosData" ||
               type == "NiFloatData" || type == "NiBoolData") {
      // Invariant: key records carry no references.
      follow_refs = false;
    } else if (type == "NiTextKeyExtraData") {
      // Invariant: the name, a count, then one (time, string index) pair per key; every index is donor relative.
      if (bytes.size() < 8) throw parse_error(info.offset, "donor text key block is truncated");
      const std::size_t keys = read_int<std::uint32_t>(bytes, 4);
      if (bytes.size() != 8 + keys * 8) throw parse_error(info.offset, "donor text key block is truncated");
      remap_string_at(bytes, 0, d);
      for (std::size_t i = 0; i < keys; ++i) remap_string_at(bytes, 8 + 8 * i + 4, d);
      follow_refs = false;
    } else if (type == "BSSkin::Instance" || type == "BSDismemberSkinInstance") {
      // Invariant: skin instances hold no strings; ref_offsets checks their layout.
    } else if (type == "BSLightingShaderProperty") {
      if (bytes.size() < 12) throw parse_error(info.offset, "donor shader block is truncated");
      remap_string_at(bytes, 4, d);
      const std::size_t extras = read_int<std::uint32_t>(bytes, 8);
      if (12 + extras * 4 + 4 + 8 + 16 + 4 + 12 + 4 + 4 > bytes.size()) {
        throw parse_error(info.offset, "donor shader block is truncated");
      }
      remap_string_at(bytes, 12 + extras * 4 + 4 + 8 + 16 + 4 + 12 + 4, d);
    } else if (type == "BSXFlags" || type == "NiIntegerExtraData" || type == "NiFloatExtraData" ||
               type == "NiBooleanExtraData" || type == "BSConnectPoint::Parents" ||
               type == "BSConnectPoint::Children") {
      // Invariant: NiExtraData is the name alone; the payload after it holds values and inline sized strings,
      // never a block reference or a string index, so it is copied verbatim.
      if (bytes.size() < 4) throw parse_error(info.offset, "donor extra-data block is truncated");
      remap_string_at(bytes, 0, d);
      follow_refs = false;
    } else if (donor.node_for_block(d) >= 0 || is_shape_block_type(type)) {
      remap_av_name(donor, d, bytes, string_remap);
    } else {
      throw std::invalid_argument("merge_nif_into refuses donor block type " + type);
    }
    if (follow_refs) {
      // Invariant: every reference offset comes from ref_offsets, so a new block type is added there only.
      for (const auto at : write_detail::ref_offsets(donor_model, d, true)) {
        remap_ref_at(bytes, at, donor_blocks, base_blocks, d);
      }
    }
    const auto found = type_index.find(type);
    if (found != type_index.end()) {
      model.block_type_index.push_back(found->second);
    } else {
      const auto index = static_cast<std::uint16_t>(model.block_types.size());
      model.block_types.push_back(type);
      type_index[type] = index;
      model.block_type_index.push_back(index);
    }
    model.blocks.push_back(std::move(bytes));
  }
  auto bytes = write_detail::serialize(model);
  for (const auto root : donor.roots()) {
    const auto live = read_nif(bytes);
    bytes = append_child(live, parent, base_blocks + root);
  }
  const auto check = read_nif(bytes);
  if (check.blocks().size() != base.blocks().size() + donor_blocks) {
    throw std::runtime_error("internal error: the merged file lost blocks");
  }
  return bytes;
}

std::vector<std::byte> bake_shape_transforms(const NifFile& nif, const std::vector<std::uint32_t>& blocks,
                                             BakeReport* report) {
  BakeReport result;
  auto bytes = nif.bytes();
  for (const auto block : blocks) {
    if (block >= read_nif(bytes).blocks().size()) {
      result.skipped.push_back("block " + std::to_string(block) + ": no such block");
      continue;
    }
    ShapeOpRequest request;
    request.op = ShapeOp::apply_transform;
    request.blocks = {block};
    ShapeOpReport op_report;
    try {
      bytes = apply_shape_op(read_nif(bytes), request, &op_report);
    } catch (const std::exception& error) {
      result.skipped.push_back("block " + std::to_string(block) + ": " + error.what());
      continue;
    }
    if (!op_report.changed.empty()) {
      result.baked.push_back(block);
    } else {
      const std::string reason = op_report.skipped.empty() ? "no change" : op_report.skipped.front();
      result.skipped.push_back(reason);
    }
  }
  if (report != nullptr) *report = result;
  return bytes;
}

std::string write_pose_library(const std::vector<Pose>& poses) {
  nlohmann::json root = nlohmann::json::object();
  nlohmann::json list = nlohmann::json::array();
  std::set<std::string> seen;
  for (const auto& pose : poses) {
    if (pose.name.empty()) throw std::invalid_argument("pose library needs a name for every pose");
    if (!seen.insert(pose.name).second) {
      throw std::invalid_argument("pose library names pose \"" + pose.name + "\" twice");
    }
    nlohmann::json entry = nlohmann::json::object();
    entry["name"] = pose.name;
    nlohmann::json bones = nlohmann::json::object();
    for (const auto& [name, bone] : pose.bones) {
      for (const auto value : bone.translation) {
        if (!std::isfinite(value)) throw std::invalid_argument("pose \"" + pose.name + "\" has a bad translation");
      }
      for (const auto value : bone.rotation) {
        if (!std::isfinite(value)) throw std::invalid_argument("pose \"" + pose.name + "\" has a bad rotation");
      }
      if (!std::isfinite(bone.scale)) throw std::invalid_argument("pose \"" + pose.name + "\" has a bad scale");
      nlohmann::json record = nlohmann::json::object();
      record["t"] = bone.translation;
      record["r"] = bone.rotation;
      record["s"] = bone.scale;
      bones[name] = std::move(record);
    }
    entry["bones"] = std::move(bones);
    list.push_back(std::move(entry));
  }
  root["poses"] = std::move(list);
  return root.dump(2);
}

std::vector<Pose> read_pose_library(std::string_view text) {
  nlohmann::json root;
  try {
    root = nlohmann::json::parse(std::string(text));
  } catch (const nlohmann::json::exception& error) {
    throw parse_error(0, error.what());
  }
  if (!root.is_object() || !root.contains("poses") || !root.at("poses").is_array()) {
    throw parse_error(0, "pose library needs a poses array");
  }
  std::vector<Pose> poses;
  for (const auto& entry : root.at("poses")) {
    if (!entry.is_object() || !entry.contains("name") || !entry.at("name").is_string()) {
      throw parse_error(0, "pose library needs a name for every pose");
    }
    Pose pose;
    pose.name = entry.at("name").get<std::string>();
    if (!entry.contains("bones") || !entry.at("bones").is_object()) {
      throw parse_error(0, "pose \"" + pose.name + "\" needs a bones object");
    }
    for (const auto& [name, record] : entry.at("bones").items()) {
      if (!record.is_object() || !record.contains("t") || !record.contains("r") || !record.contains("s")) {
        throw parse_error(0, "pose \"" + pose.name + "\" bone \"" + name + "\" needs t, r and s");
      }
      PoseBone bone;
      bone.translation = record.at("t").get<std::array<float, 3>>();
      bone.rotation = record.at("r").get<std::array<float, 4>>();
      bone.scale = record.at("s").get<float>();
      pose.bones[name] = bone;
    }
    poses.push_back(std::move(pose));
  }
  return poses;
}

std::vector<std::byte> apply_pose_to_skin(const NifFile& nif, std::uint32_t instance_block, const Pose& pose,
                                          PoseApplyReport* report) {
  if (instance_block >= nif.blocks().size()) throw std::invalid_argument("pose target is past the file");
  const auto& info = nif.blocks()[instance_block];
  std::vector<std::string> names;
  std::int32_t bonedata_ref = -1;
  if (info.type == "BSSkin::Instance") {
    for (const auto& bone : read_bones(nif, instance_block)) names.push_back(bone.name);
    auto cursor = detail::block_cursor(nif, instance_block);
    cursor.read_i32();
    bonedata_ref = cursor.read_i32();
  } else if (info.type == "BSDismemberSkinInstance") {
    const auto skin = read_dismember_skin(nif, instance_block);
    names = skin.bone_names;
    bonedata_ref = skin.bonedata_ref;
  } else {
    throw std::invalid_argument("pose target is " + info.type + ", not a skin instance");
  }
  if (bonedata_ref < 0 || static_cast<std::size_t>(bonedata_ref) >= nif.blocks().size()) {
    throw std::invalid_argument("pose target has no bone data block");
  }
  auto model = write_detail::load_model(nif);
  Bytes& bonedata = model.blocks[static_cast<std::uint32_t>(bonedata_ref)];
  if (bonedata.size() != 4 + names.size() * k_bone_transform_bytes) {
    throw std::invalid_argument("pose target bone data does not hold one transform per bone");
  }
  PoseApplyReport result;
  std::set<std::string> posed;
  for (std::size_t b = 0; b < names.size(); ++b) {
    const auto found = pose.bones.find(names[b]);
    if (found == pose.bones.end()) {
      result.unposed_bones.push_back(names[b]);
      continue;
    }
    const auto& bone = found->second;
    for (const auto value : bone.translation) {
      if (!std::isfinite(value)) throw std::invalid_argument("pose \"" + pose.name + "\" has a bad translation");
    }
    float norm_sq = 0.0f;
    for (const auto value : bone.rotation) {
      if (!std::isfinite(value)) throw std::invalid_argument("pose \"" + pose.name + "\" has a bad rotation");
      norm_sq += value * value;
    }
    if (!(norm_sq > 0.0f) || !std::isfinite(norm_sq)) {
      throw std::invalid_argument("pose \"" + pose.name + "\" bone \"" + names[b] + "\" has no usable rotation");
    }
    if (!std::isfinite(bone.scale)) throw std::invalid_argument("pose \"" + pose.name + "\" has a bad scale");
    const std::size_t at = 4 + b * k_bone_transform_bytes;
    // Invariant: the bind rotation stores a row-major 3x3 matrix, so the quaternion is converted first.
    const float inv = 1.0f / std::sqrt(norm_sq);
    const float x = bone.rotation[0] * inv;
    const float y = bone.rotation[1] * inv;
    const float z = bone.rotation[2] * inv;
    const float w = bone.rotation[3] * inv;
    const std::array<float, 9> matrix{1 - 2 * (y * y + z * z), 2 * (x * y - z * w),     2 * (x * z + y * w),
                                      2 * (x * y + z * w),     1 - 2 * (x * x + z * z), 2 * (y * z - x * w),
                                      2 * (x * z - y * w),     2 * (y * z + x * w),     1 - 2 * (x * x + y * y)};
    for (std::size_t k = 0; k < 9; ++k) {
      write_int(bonedata, at + 16 + 4 * k, std::bit_cast<std::uint32_t>(matrix[k]));
    }
    for (std::size_t k = 0; k < 3; ++k) {
      write_int(bonedata, at + 52 + 4 * k, std::bit_cast<std::uint32_t>(bone.translation[k]));
    }
    write_int(bonedata, at + 64, std::bit_cast<std::uint32_t>(bone.scale));
    result.applied++;
    posed.insert(names[b]);
  }
  for (const auto& [name, bone] : pose.bones) {
    (void)bone;
    if (posed.count(name) == 0) result.unknown_pose_bones.push_back(name);
  }
  if (report != nullptr) *report = result;
  auto bytes = write_detail::serialize(model);
  read_nif(bytes);
  return bytes;
}

}  // namespace fo4::nif
