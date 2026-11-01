#include "fo4/nif/nif_audit.hpp"

#include "fo4/binary/cursor.hpp"
#include "fo4/error.hpp"
#include "nif_internal.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace fo4::nif {
namespace {

constexpr std::string_view k_blanks = " \t\r\n";
constexpr std::size_t k_texture_slots = 10;

constexpr std::array<std::string_view, 16> k_fo4_node_types{
    "NiNode", "BSFadeNode", "BSMultiBoundNode", "BSOrderedNode", "BSLeafAnimNode",
    "BSTreeNode", "NiBillboardNode", "NiSwitchNode", "NiLODNode", "BSValueNode",
    "BSRangeNode", "BSDebrisNode", "BSBlastNode", "BSDamageStage", "NiSortAdjustNode",
    "BSMasterParticleSystem"};

constexpr std::array<std::string_view, 4> k_fo4_shader_types{
    "BSLightingShaderProperty", "BSEffectShaderProperty", "BSSkyShaderProperty",
    "BSWaterShaderProperty"};

constexpr std::array<std::string_view, 4> k_fo4_geometry_types{
    "BSTriShape", "BSSubIndexTriShape", "BSMeshLODTriShape", "BSDynamicTriShape"};

constexpr std::array<std::string_view, 11> k_older_geometry_types{
    "NiTriShape", "NiTriStrips", "NiTriShapeData", "NiTriStripsData", "NiTriShapeSkinController",
    "NiTriStripsSkinController", "NiSkinInstance", "NiSkinData", "NiSkinPartition", "NiTriBasedGeom",
    "NiTriBasedGeomData"};

bool listed(std::span<const std::string_view> names, std::string_view value) {
  for (const auto name : names) {
    if (name == value) return true;
  }
  return false;
}

bool is_shape_type(std::string_view type) {
  return listed(std::span<const std::string_view>{k_fo4_geometry_types}, type);
}

bool is_node_type(std::string_view type) {
  return listed(std::span<const std::string_view>{k_fo4_node_types}, type);
}

struct AvHeader {
  std::string name;
  std::vector<std::int32_t> extras;
};

bool read_av(binary::Cursor& cursor, const NifFile& nif, AvHeader& header) {
  const auto name = cursor.read_i32();
  const auto extra_count = cursor.read_u32();
  if (static_cast<std::size_t>(extra_count) > cursor.remaining() / 4) return false;
  header.extras.resize(extra_count);
  for (auto& ref : header.extras) ref = cursor.read_i32();
  cursor.read_i32();
  cursor.read_u32();
  cursor.read_bytes(13 * 4);
  if (name >= 0) {
    if (static_cast<std::size_t>(name) >= nif.strings().size()) return false;
    header.name = nif.strings()[static_cast<std::size_t>(name)];
  }
  return true;
}

bool read_node(const NifFile& nif, std::uint32_t block, AvHeader& header,
               std::vector<std::int32_t>& children) {
  try {
    auto cursor = detail::block_cursor(nif, block);
    if (!read_av(cursor, nif, header)) return false;
    cursor.read_i32();
    const auto count = cursor.read_u32();
    if (static_cast<std::size_t>(count) > cursor.remaining() / 4) return false;
    children.resize(count);
    for (auto& child : children) child = cursor.read_i32();
    return true;
  } catch (const parse_error&) {
    return false;
  }
}

bool read_skin_instance(const NifFile& nif, std::uint32_t block, std::int32_t& data,
                        std::vector<std::int32_t>& bones) {
  try {
    auto cursor = detail::block_cursor(nif, block);
    cursor.read_i32();
    data = cursor.read_i32();
    const auto count = cursor.read_u32();
    if (static_cast<std::size_t>(count) > cursor.remaining() / 4) return false;
    bones.resize(count);
    for (auto& bone : bones) bone = cursor.read_i32();
    return true;
  } catch (const parse_error&) {
    return false;
  }
}

std::optional<std::int32_t> read_node_bound_reference(const NifFile& nif, std::uint32_t block) {
  try {
    auto cursor = detail::block_cursor(nif, block);
    AvHeader header;
    if (!read_av(cursor, nif, header)) return std::nullopt;
    cursor.read_i32();
    const auto count = cursor.read_u32();
    if (static_cast<std::size_t>(count) > cursor.remaining() / 4) return std::nullopt;
    cursor.read_bytes(static_cast<std::size_t>(count) * 4);
    return cursor.read_i32();
  } catch (const parse_error&) {
    return std::nullopt;
  }
}

std::optional<std::int32_t> read_multi_bound_data_reference(const NifFile& nif, std::uint32_t block) {
  try {
    auto cursor = detail::block_cursor(nif, block);
    return cursor.read_i32();
  } catch (const parse_error&) {
    return std::nullopt;
  }
}

struct ShapeHeader {
  AvHeader av;
  std::int32_t collision{-1};
  std::int32_t skin{-1};
  std::int32_t shader{-1};
  std::int32_t alpha{-1};
};

bool read_shape_header(const NifFile& nif, std::uint32_t block, ShapeHeader& header) {
  try {
    auto cursor = detail::block_cursor(nif, block);
    if (!read_av(cursor, nif, header.av)) return false;
    header.collision = cursor.read_i32();
    cursor.read_bytes(16);
    header.skin = cursor.read_i32();
    header.shader = cursor.read_i32();
    header.alpha = cursor.read_i32();
    return true;
  } catch (const parse_error&) {
    return false;
  }
}

bool read_lighting_shader(const NifFile& nif, std::uint32_t block, std::string& name) {
  try {
    auto cursor = detail::block_cursor(nif, block);
    cursor.read_u32();
    const auto name_index = cursor.read_i32();
    if (name_index < 0) return true;
    if (static_cast<std::size_t>(name_index) >= nif.strings().size()) return false;
    name = nif.strings()[static_cast<std::size_t>(name_index)];
    return true;
  } catch (const parse_error&) {
    return false;
  }
}

bool read_texture_set(const NifFile& nif, std::uint32_t block, std::vector<std::string>& paths) {
  try {
    auto cursor = detail::block_cursor(nif, block);
    const auto count = cursor.read_u32();
    if (static_cast<std::size_t>(count) > cursor.remaining() / 4) return false;
    paths.clear();
    paths.reserve(count);
    for (std::size_t slot = 0; slot < count; ++slot) {
      const auto length = cursor.read_u32();
      if (static_cast<std::size_t>(length) > cursor.remaining()) return false;
      const auto text = cursor.read_bytes(length);
      paths.emplace_back(reinterpret_cast<const char*>(text.data()), text.size());
    }
    return true;
  } catch (const parse_error&) {
    return false;
  }
}

std::string_view trimmed(std::string_view text) {
  const auto first = text.find_first_not_of(k_blanks);
  if (first == std::string_view::npos) return {};
  return text.substr(first, text.find_last_not_of(k_blanks) - first + 1);
}

void add(std::vector<NifAuditFinding>& findings, NifAuditSeverity severity,
         std::string_view rule, std::uint32_t block, std::string message) {
  findings.push_back(NifAuditFinding{severity, rule, block, std::move(message)});
}

std::string block_text(const NifFile& nif, std::uint32_t block) {
  return std::string{nif.blocks().at(block).type};
}

}  // namespace

std::vector<NifAuditFinding> audit_nif(const NifFile& nif) {
  std::vector<NifAuditFinding> findings;
  const auto block_count = nif.blocks().size();
  const auto block_limit = static_cast<std::int32_t>(block_count);

  if (nif.roots().size() != 1) {
    add(findings, NifAuditSeverity::error, k_audit_root_count,
        nif.roots().empty() ? k_nif_audit_no_block : nif.roots().front(),
        "a Fallout 4 NIF must have exactly one root block, found " +
            std::to_string(nif.roots().size()));
  } else if (!is_node_type(nif.blocks()[nif.roots().front()].type)) {
    add(findings, NifAuditSeverity::error, k_audit_root_not_node, nif.roots().front(),
        "the root block is a " + block_text(nif, nif.roots().front()) + ", not a node");
  }

  std::vector<bool> child(block_count, false);
  std::vector<std::uint8_t> parent_count(block_count, 0);
  std::vector<std::vector<std::uint32_t>> adjacency(block_count);
  for (std::uint32_t block = 0; block < block_count; ++block) {
    if (!is_node_type(nif.blocks()[block].type)) continue;
    AvHeader header;
    std::vector<std::int32_t> children;
    if (!read_node(nif, block, header, children)) continue;
    std::vector<std::int32_t> seen;
    for (const auto entry : children) {
      if (entry < -1) {
        add(findings, NifAuditSeverity::error, k_audit_child_below_null, block,
            "child reference " + std::to_string(entry) + " is below the null sentinel -1");
        continue;
      }
      if (entry < 0) continue;
      const auto index = static_cast<std::size_t>(entry);
      if (std::find(seen.begin(), seen.end(), entry) != seen.end()) {
        add(findings, NifAuditSeverity::error, k_audit_child_duplicate, block,
            "child block " + std::to_string(entry) + " is listed more than once");
        continue;
      }
      seen.push_back(entry);
      child[index] = true;
      if (parent_count[index] < 2) ++parent_count[index];
      if (is_node_type(nif.blocks()[index].type)) {
        adjacency[block].push_back(static_cast<std::uint32_t>(entry));
      }
    }
  }
  for (std::uint32_t block = 0; block < block_count; ++block) {
    if (parent_count[block] > 1) {
      add(findings, NifAuditSeverity::error, k_audit_child_multiple_parents, block,
          "block is a child of more than one node; a Fallout 4 block must have one parent");
    }
  }
  {
    std::vector<std::uint8_t> state(block_count, 0);
    std::vector<std::size_t> position(block_count, 0);
    bool reported = false;
    for (std::uint32_t start = 0; start < block_count && !reported; ++start) {
      if (!is_node_type(nif.blocks()[start].type) || state[start] != 0) continue;
      std::vector<std::uint32_t> stack{start};
      state[start] = 1;
      while (!stack.empty() && !reported) {
        const auto node = stack.back();
        if (position[node] >= adjacency[node].size()) {
          state[node] = 2;
          stack.pop_back();
          continue;
        }
        const auto next = adjacency[node][position[node]++];
        if (state[next] == 1) {
          add(findings, NifAuditSeverity::error, k_audit_child_cycle, next,
              "node is part of a child cycle; the node graph must be a tree");
          reported = true;
          break;
        }
        if (state[next] == 0) {
          state[next] = 1;
          stack.push_back(next);
        }
      }
    }
  }

  for (std::uint32_t block = 0; block < block_count; ++block) {
    if (nif.blocks()[block].type != "BSSkin::Instance") continue;
    std::int32_t data = -1;
    std::vector<std::int32_t> bones;
    if (!read_skin_instance(nif, block, data, bones)) continue;
    const bool in_range = data >= 0 && data < block_limit;
    const bool is_bone_data =
        in_range && nif.blocks()[static_cast<std::size_t>(data)].type == "BSSkin::BoneData";
    if (data < -1) {
      add(findings, NifAuditSeverity::error, k_audit_skin_data, block,
          "bone data reference " + std::to_string(data) + " is below the null sentinel -1");
    } else if (data >= 0 && !in_range) {
      add(findings, NifAuditSeverity::error, k_audit_skin_data, block,
          "bone data reference " + std::to_string(data) + " is outside the block table");
    } else if (in_range && !is_bone_data) {
      add(findings, NifAuditSeverity::error, k_audit_skin_data, block,
          "bone data reference " + std::to_string(data) + " is not a BSSkin::BoneData block");
    }
    bool bone_reported = false;
    for (const auto bone : bones) {
      if (bone >= 0 && bone < block_limit &&
          is_node_type(nif.blocks()[static_cast<std::size_t>(bone)].type)) {
        continue;
      }
      if (bone_reported) continue;
      add(findings, NifAuditSeverity::error, k_audit_skin_bone, block,
          "bone reference " + std::to_string(bone) + " does not point at a node block");
      bone_reported = true;
    }
    if (!is_bone_data) continue;
    try {
      auto cursor = detail::block_cursor(nif, static_cast<std::uint32_t>(data));
      const auto transforms = cursor.read_u32();
      if (transforms != static_cast<std::uint32_t>(bones.size())) {
        add(findings, NifAuditSeverity::error, k_audit_skin_bone_count, block,
            "skin holds " + std::to_string(bones.size()) +
                " bone references but its bone data holds " + std::to_string(transforms) +
                " transforms");
      }
    } catch (const parse_error&) {
    }
  }

  for (std::uint32_t block = 0; block < block_count; ++block) {
    const auto& type = nif.blocks()[block].type;
    if (listed(std::span<const std::string_view>{k_older_geometry_types}, type)) {
      add(findings, NifAuditSeverity::error, k_audit_geometry_not_fo4, block,
          "geometry block is a " + type + "; Fallout 4 geometry must be a BSTriShape family block");
    }

    AvHeader header;
    std::int32_t shader = -1;
    const bool node = is_node_type(type);
    const bool shape = is_shape_type(type);
    if (node) {
      std::vector<std::int32_t> children;
      if (!read_node(nif, block, header, children)) continue;
    } else if (shape) {
      if (!child[block]) {
        add(findings, NifAuditSeverity::error, k_audit_shape_outside_node, block,
            "shape block is not a child of any node");
      }
      ShapeHeader shape_header;
      if (!read_shape_header(nif, block, shape_header)) continue;
      header = std::move(shape_header.av);
      shader = shape_header.shader;
      const std::pair<std::string_view, std::int32_t> references[] = {
          {"collision", shape_header.collision},
          {"skin", shape_header.skin},
          {"shader", shape_header.shader},
          {"alpha", shape_header.alpha},
      };
      for (const auto& [field, ref] : references) {
        if (ref < -1 || ref >= block_limit) {
          add(findings, NifAuditSeverity::error, k_audit_shape_reference, block,
              std::string{field} + " reference " + std::to_string(ref) +
                  " is outside the block table");
        }
      }
      if (shape_header.skin >= 0 && shape_header.skin < block_limit) {
        const auto index = static_cast<std::size_t>(shape_header.skin);
        if (nif.blocks()[index].type != "BSSkin::Instance") {
          add(findings, NifAuditSeverity::error, k_audit_skin_reference, block,
              "skin reference " + std::to_string(shape_header.skin) + " is a " +
                  nif.blocks()[index].type + ", not a BSSkin::Instance block");
        }
      }
    } else {
      continue;
    }

    for (const auto ref : header.extras) {
      if (ref >= block_limit) {
        add(findings, NifAuditSeverity::error, k_audit_extra_ref, block,
            "extra-data reference " + std::to_string(ref) + " is outside the block table");
        break;
      }
    }
    if (!shape) continue;
    if (shader < 0 || shader >= block_limit) continue;
    const auto& shader_type = nif.blocks()[static_cast<std::size_t>(shader)].type;
    if (!listed(std::span<const std::string_view>{k_fo4_shader_types}, shader_type)) {
      add(findings, NifAuditSeverity::error, k_audit_shader_not_fo4, block,
          "shader property is a " + shader_type + "; Fallout 4 uses a BS*ShaderProperty block");
    }
  }

  for (std::uint32_t block = 0; block < block_count; ++block) {
    const auto& type = nif.blocks()[block].type;
    if (type == "BSMultiBoundNode") {
      const auto bound = read_node_bound_reference(nif, block);
      if (!bound.has_value() || *bound < 0 || *bound >= block_limit ||
          nif.blocks()[static_cast<std::size_t>(*bound)].type != "BSMultiBound") {
        add(findings, NifAuditSeverity::error, k_audit_multi_bound, block,
            "bound reference is not a BSMultiBound block");
      }
      continue;
    }
    if (type != "BSMultiBound") continue;
    const auto data = read_multi_bound_data_reference(nif, block);
    if (!data.has_value() || *data < 0 || *data >= block_limit) {
      add(findings, NifAuditSeverity::error, k_audit_multi_bound, block,
          "bound data reference is outside the block table");
      continue;
    }
    const auto& data_type = nif.blocks()[static_cast<std::size_t>(*data)].type;
    if (data_type != "BSMultiBoundAABB" && data_type != "BSMultiBoundOBB") {
      add(findings, NifAuditSeverity::error, k_audit_multi_bound, block,
          "bound data reference is a " + data_type + ", not a BSMultiBoundAABB or BSMultiBoundOBB");
    }
  }

  for (std::uint32_t block = 0; block < block_count; ++block) {
    const auto& type = nif.blocks()[block].type;
    if (type == "BSLightingShaderProperty") {
      std::string name;
      if (!read_lighting_shader(nif, block, name)) continue;
      const auto kept = trimmed(name);
      if (kept.size() != name.size()) {
        add(findings, NifAuditSeverity::warning, k_audit_shader_name_space, block,
            "shader material name has leading or trailing whitespace");
      }
      continue;
    }
    if (type != "BSShaderTextureSet") continue;
    std::vector<std::string> paths;
    if (!read_texture_set(nif, block, paths)) continue;
    if (paths.size() != k_texture_slots) {
      add(findings, NifAuditSeverity::error, k_audit_texture_slots, block,
          "texture set has " + std::to_string(paths.size()) +
              " slots; Fallout 4 BSLightingShaderProperty texture sets carry 10");
    }
    for (std::size_t slot = 0; slot < paths.size(); ++slot) {
      const auto& path = paths[slot];
      if (path.empty()) continue;
      const auto kept = trimmed(path);
      if (kept.size() != path.size()) {
        add(findings, NifAuditSeverity::warning, k_audit_texture_path_space, block,
            "texture slot " + std::to_string(slot) + " path has leading or trailing whitespace");
      }
      if (path.front() == '/' || path.front() == '\\' || path.find(':') != std::string::npos) {
        add(findings, NifAuditSeverity::error, k_audit_texture_path_absolute, block,
            "texture slot " + std::to_string(slot) + " path is absolute; it must be a relative data path");
      }
      if (path.find("..") != std::string::npos) {
        add(findings, NifAuditSeverity::error, k_audit_texture_path_parent, block,
            "texture slot " + std::to_string(slot) + " path walks up with '..'");
      }
    }
  }
  return findings;
}

}  // namespace fo4::nif
