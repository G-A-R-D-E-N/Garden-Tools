#include "fo4/nif/skin_binding.hpp"

#include "fo4/error.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace fo4::nif {
namespace {

constexpr std::size_t k_slots = 4;
constexpr std::size_t k_names_listed = 6;
constexpr std::string_view k_skin_suffix = "_skin";

std::string lower(std::string_view text) {
  std::string out(text);
  for (char& ch : out) {
    if (ch >= 'A' && ch <= 'Z') ch = static_cast<char>(ch - 'A' + 'a');
  }
  return out;
}

struct SkeletonNames {
  std::unordered_map<std::string, std::size_t> first;
  std::unordered_set<std::string> repeated;
};

SkeletonNames index_skeleton(std::span<const std::string> names) {
  SkeletonNames out;
  for (std::size_t bone = 0; bone < names.size(); ++bone) {
    const auto key = lower(names[bone]);
    if (!out.first.emplace(key, bone).second) out.repeated.insert(key);
  }
  return out;
}

struct NodeCandidate {
  std::size_t bone{0};
  std::string via;
  std::string source;
  bool repeated{false};
};

// Requires key already lower case; each walk follows node parents until a node is named like a skeleton bone.
std::vector<NodeCandidate> node_candidates(const std::string& key, const SkeletonNames& skeleton,
                                           std::span<const SkinNodeSource> sources) {
  std::vector<NodeCandidate> found;
  for (const auto& source : sources) {
    if (source.nif == nullptr) continue;
    const auto& nodes = source.nif->nodes();
    for (const auto& start : nodes) {
      if (lower(start.name) != key) continue;
      const Node* node = &start;
      for (std::size_t steps = 0; node != nullptr && steps < nodes.size(); ++steps) {
        const auto name = lower(node->name);
        if (const auto hit = skeleton.first.find(name); hit != skeleton.first.end()) {
          found.push_back({hit->second, node->name, source.label, skeleton.repeated.contains(name)});
          break;
        }
        const auto parent = node->parent;
        node = parent >= 0 && static_cast<std::size_t>(parent) < nodes.size()
                   ? &nodes[static_cast<std::size_t>(parent)]
                   : nullptr;
      }
    }
  }
  return found;
}

bool is_ambiguous(const std::vector<NodeCandidate>& found) {
  if (found.empty()) return false;
  if (std::any_of(found.begin(), found.end(), [](const NodeCandidate& hit) { return hit.repeated; })) return true;
  return std::any_of(found.begin(), found.end(),
                     [&](const NodeCandidate& hit) { return hit.bone != found.front().bone; });
}

void check_shape(const Shape& shape) {
  const auto label = "shape '" + shape.name + "' (block " + std::to_string(shape.block) + ")";
  if (shape.skin_lost) {
    throw std::invalid_argument(label + " lost its skin, so there is nothing to bind" +
                                (shape.warning.empty() ? "" : ": " + shape.warning));
  }
  if (shape.bones.empty()) throw std::invalid_argument(label + " is not skinned: it lists no skin bones");
  const auto vertices = shape.verts.size() / 3;
  if (shape.weights.size() != vertices * k_slots || shape.bone_indices.size() != vertices * k_slots) {
    throw std::invalid_argument(label + " has no four-slot weights for each of its vertices");
  }
  for (const auto index : shape.bone_indices) {
    if (static_cast<std::size_t>(index) >= shape.bones.size()) {
      throw std::invalid_argument(label + " names a bone index past its bone list");
    }
  }
}

bool weighted(const Shape& shape, std::size_t vertex, std::size_t slot) {
  return !(shape.weights[vertex * k_slots + slot] <= 0.0f);
}

std::string name_list(const std::vector<std::string>& names) {
  std::string text;
  for (std::size_t i = 0; i < names.size() && i < k_names_listed; ++i) text += (i == 0 ? "" : ", ") + names[i];
  if (names.size() > k_names_listed) text += ", and " + std::to_string(names.size() - k_names_listed) + " more";
  return text;
}

std::string plural(std::size_t count, std::string_view noun) {
  return std::to_string(count) + " " + std::string(noun) + (count == 1 ? "" : "s");
}

std::string warning_text(const SkinBindingReport& report) {
  std::vector<std::string> missing;
  std::vector<std::string> approximated;
  std::vector<std::string> ambiguous;
  for (const auto& bone : report.bones) {
    if (bone.match == SkinBoneMatch::unmatched) missing.push_back(bone.name);
    if (bone.match == SkinBoneMatch::skin_parent) approximated.push_back(bone.name);
    if (bone.ambiguous) ambiguous.push_back(bone.name);
  }
  std::vector<std::string> parts;
  if (!missing.empty()) {
    parts.push_back(plural(missing.size(), "bone") + (missing.size() == 1 ? " matches" : " match") +
                    " no skeleton bone and no helper node: " + name_list(missing) +
                    ". A vertex weighted only to them has no bone to follow.");
  }
  if (!approximated.empty()) {
    parts.push_back(plural(approximated.size(), "bone") + (approximated.size() == 1 ? " matches" : " match") +
                    " only through the skeleton bone of the same name without the _skin suffix: " +
                    name_list(approximated) + ". That is a placement guess, not a binding.");
  }
  if (!ambiguous.empty()) {
    parts.push_back(name_list(ambiguous) + (ambiguous.size() == 1 ? " matches" : " match") +
                    " more than one skeleton bone or helper node, so the first match is used.");
  }
  std::string text;
  for (const auto& part : parts) text += (text.empty() ? "" : " ") + part;
  return text;
}

}  // namespace

const char* skin_bone_match_name(SkinBoneMatch match) noexcept {
  switch (match) {
    case SkinBoneMatch::exact: return "exact";
    case SkinBoneMatch::node_chain: return "node_chain";
    case SkinBoneMatch::skin_parent: return "skin_parent";
    case SkinBoneMatch::unmatched: return "unmatched";
  }
  return "unmatched";
}

SkinBindingReport bind_skin_to_skeleton(const Shape& shape, std::span<const std::string> skeleton_bones,
                                        std::span<const SkinNodeSource> node_sources) {
  check_shape(shape);
  const auto skeleton = index_skeleton(skeleton_bones);

  SkinBindingReport report;
  report.block = shape.block;
  report.name = shape.name;
  report.vertices = shape.verts.size() / 3;
  report.bones.resize(shape.bones.size());

  for (std::size_t vertex = 0; vertex < report.vertices; ++vertex) {
    for (std::size_t slot = 0; slot < k_slots; ++slot) {
      if (!weighted(shape, vertex, slot)) continue;
      bool repeat = false;
      const auto bone = shape.bone_indices[vertex * k_slots + slot];
      for (std::size_t earlier = 0; earlier < slot; ++earlier) {
        if (weighted(shape, vertex, earlier) && shape.bone_indices[vertex * k_slots + earlier] == bone) repeat = true;
      }
      if (!repeat) ++report.bones[bone].weighted_vertices;
    }
  }

  for (std::size_t index = 0; index < shape.bones.size(); ++index) {
    auto& bone = report.bones[index];
    bone.name = shape.bones[index].name;
    const auto key = lower(bone.name);

    if (const auto hit = skeleton.first.find(key); hit != skeleton.first.end()) {
      bone.match = SkinBoneMatch::exact;
      bone.skeleton_bone = static_cast<std::int32_t>(hit->second);
      bone.ambiguous = skeleton.repeated.contains(key);
    } else {
      const auto found = node_candidates(key, skeleton, node_sources);
      bone.ambiguous = is_ambiguous(found);
      if (!found.empty()) {
        bone.match = SkinBoneMatch::node_chain;
        bone.skeleton_bone = static_cast<std::int32_t>(found.front().bone);
        bone.via = found.front().via;
        bone.source = found.front().source;
      } else if (key.size() > k_skin_suffix.size() && key.ends_with(k_skin_suffix) &&
                 skeleton.first.contains(key.substr(0, key.size() - k_skin_suffix.size()))) {
        bone.match = SkinBoneMatch::skin_parent;
        bone.skeleton_bone = static_cast<std::int32_t>(
            skeleton.first.at(key.substr(0, key.size() - k_skin_suffix.size())));
      }
    }

    switch (bone.match) {
      case SkinBoneMatch::exact: ++report.exact; break;
      case SkinBoneMatch::node_chain: ++report.node_chain; break;
      case SkinBoneMatch::skin_parent: ++report.skin_parent; break;
      case SkinBoneMatch::unmatched: ++report.unmatched; break;
    }
    if (bone.ambiguous) ++report.ambiguous;
  }

  for (std::size_t vertex = 0; vertex < report.vertices; ++vertex) {
    bool bound = true;
    for (std::size_t slot = 0; slot < k_slots; ++slot) {
      if (!weighted(shape, vertex, slot)) continue;
      if (report.bones[shape.bone_indices[vertex * k_slots + slot]].match != SkinBoneMatch::exact) bound = false;
    }
    if (bound) ++report.fully_bound_vertices;
  }

  report.warning = warning_text(report);
  return report;
}

const char* skin_shapes_status_name(SkinShapesStatus status) noexcept {
  switch (status) {
    case SkinShapesStatus::ok: return "ok";
    case SkinShapesStatus::invalid_block: return "invalid_block";
    case SkinShapesStatus::not_a_shape: return "not_a_shape";
    case SkinShapesStatus::refused: return "refused";
    case SkinShapesStatus::parse_error: return "parse_error";
    case SkinShapesStatus::no_skinned_shape: return "no_skinned_shape";
  }
  return "refused";
}

SkinShapesBinding bind_skin_shapes(const NifFile& nif, std::optional<std::uint32_t> block,
                                   std::span<const std::string> skeleton_bones,
                                   std::span<const SkinNodeSource> node_sources) {
  SkinShapesBinding out;
  std::vector<std::uint32_t> blocks;
  if (block) {
    if (*block >= nif.blocks().size()) {
      out.status = SkinShapesStatus::invalid_block;
      out.message = "block " + std::to_string(*block) + " is not in the NIF";
      return out;
    }
    if (!is_shape_block_type(nif.blocks()[*block].type)) {
      out.status = SkinShapesStatus::not_a_shape;
      out.message = "block " + std::to_string(*block) + " is not a shape";
      return out;
    }
    blocks.push_back(*block);
  } else {
    for (std::size_t index = 0; index < nif.blocks().size(); ++index) {
      if (is_shape_block_type(nif.blocks()[index].type)) blocks.push_back(static_cast<std::uint32_t>(index));
    }
  }

  for (const std::uint32_t index : blocks) {
    Shape shape;
    try {
      shape = read_shape(nif, index);
    } catch (const parse_error& error) {
      if (block) {
        out.status = SkinShapesStatus::parse_error;
        out.message = error.parse_message();
        return out;
      }
      out.refused.push_back({index, std::string(), error.parse_message()});
      continue;
    }
    if (!block && shape.bones.empty() && !shape.skin_lost) {
      ++out.unskinned;
      continue;
    }
    try {
      out.shapes.push_back(bind_skin_to_skeleton(shape, skeleton_bones, node_sources));
    } catch (const std::invalid_argument& error) {
      std::string reason = error.what();
      if (!shape.warning.empty() && reason.find(shape.warning) == std::string::npos) reason += ": " + shape.warning;
      if (block) {
        out.status = SkinShapesStatus::refused;
        out.message = std::move(reason);
        out.shapes.clear();
        return out;
      }
      out.refused.push_back({shape.block, shape.name, std::move(reason)});
    }
  }
  if (out.shapes.empty() && out.refused.empty()) {
    out.status = SkinShapesStatus::no_skinned_shape;
    out.message = "The NIF has no skinned shape to bind";
  }
  return out;
}

}  // namespace fo4::nif
