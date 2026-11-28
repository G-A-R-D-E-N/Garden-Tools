#include "fo4/hkx/character_workspace.hpp"

#include "fo4/error.hpp"
#include "fo4/hkx/skeleton_mapper.hpp"
#include "fo4/hkx/skeleton_write.hpp"
#include "fo4/nif/nif.hpp"
#include "fo4/nif/skin.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <numeric>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_set>
#include <utility>
#include <vector>

namespace fo4::hkx {
namespace {

constexpr std::size_t k_slots = 4;
constexpr std::size_t k_vertices_listed = 8;
constexpr std::size_t k_names_listed = 6;
constexpr float k_sum_tolerance = 0.001f;

constexpr std::array<std::string_view, 10> k_status_names{
    "Ok", "Truncated", "BadMagic", "UnsupportedLayout", "MissingSection",
    "BadSection", "FixupOutOfRange", "DuplicateFixup", "BadClassName", "MissingObject",
};

std::string lower(std::string_view text) {
  std::string out(text);
  for (char& ch : out) {
    if (ch >= 'A' && ch <= 'Z') ch = static_cast<char>(ch - 'A' + 'a');
  }
  return out;
}

std::string number(std::size_t value) { return std::to_string(value); }

std::string plural(std::size_t count, std::string_view noun) {
  return number(count) + " " + std::string(noun) + (count == 1 ? "" : "s");
}

CharacterFinding finding(CharacterLevel level, std::string area, std::string where, std::string message) {
  return CharacterFinding{level, std::move(area), std::move(where), std::move(message)};
}

std::string status_text(HkxStatus status, std::span<const std::byte> bytes, std::string_view step) {
  std::string text = std::string(step) + " failed: " + std::string(k_status_names[static_cast<std::size_t>(status)]);
  if (const auto reason = describe_packfile_refusal(bytes); !reason.empty()) text += " (" + reason + ")";
  return text;
}

std::string name_list(const std::vector<std::string>& names) {
  std::string text;
  for (std::size_t i = 0; i < names.size() && i < k_names_listed; ++i) text += (i == 0 ? "" : ", ") + names[i];
  if (names.size() > k_names_listed) text += ", and " + number(names.size() - k_names_listed) + " more";
  return text;
}

bool finite3(const std::array<float, 3>& value) {
  return std::isfinite(value[0]) && std::isfinite(value[1]) && std::isfinite(value[2]);
}

void parallel_length(std::vector<CharacterFinding>& out, std::string_view what, std::size_t count, std::size_t bones,
                     std::string_view against = "boneNames") {
  if (count == bones) return;
  out.push_back(finding(CharacterLevel::error, "skeleton", std::string(what),
                        "it holds " + number(count) + (count == 1 ? " entry" : " entries") + " against " +
                            number(bones) + " in " + std::string(against)));
}

void check_pose(std::vector<CharacterFinding>& out, const std::string& where, const HkxQsTransform& pose) {
  if (!finite3(pose.translation)) {
    out.push_back(finding(CharacterLevel::error, "skeleton", where, "its reference translation is not a finite number"));
  }
  if (!finite3(pose.scale)) {
    out.push_back(finding(CharacterLevel::error, "skeleton", where, "its reference scale is not a finite number"));
  } else if (pose.scale[0] == 0.0f || pose.scale[1] == 0.0f || pose.scale[2] == 0.0f) {
    out.push_back(finding(CharacterLevel::error, "skeleton", where, "its reference scale collapses an axis to zero"));
  }
  const auto& q = pose.rotation;
  if (!std::isfinite(q[0]) || !std::isfinite(q[1]) || !std::isfinite(q[2]) || !std::isfinite(q[3])) {
    out.push_back(finding(CharacterLevel::error, "skeleton", where, "its reference rotation is not a finite number"));
    return;
  }
  const float length = std::sqrt(q[0] * q[0] + q[1] * q[1] + q[2] * q[2] + q[3] * q[3]);
  if (length < 1e-6f) {
    out.push_back(finding(CharacterLevel::error, "skeleton", where, "its reference rotation has no length"));
  } else if (std::fabs(length - 1.0f) > 1e-3f) {
    std::string text = std::to_string(length);
    text.erase(text.find_last_not_of('0') + 1);
    if (!text.empty() && text.back() == '.') text.pop_back();
    out.push_back(finding(CharacterLevel::warning, "skeleton", where,
                          "its reference rotation is " + text + " long rather than 1"));
  }
}

bool same_names(const std::vector<std::string>& a, const std::vector<std::string>& b) {
  if (a.size() != b.size()) return false;
  for (std::size_t i = 0; i < a.size(); ++i) {
    if (lower(a[i]) != lower(b[i])) return false;
  }
  return true;
}

bool has_all_names(const std::vector<std::string>& wanted, const std::vector<std::string>& in) {
  std::unordered_set<std::string> have;
  for (const auto& name : in) have.insert(lower(name));
  return std::all_of(wanted.begin(), wanted.end(), [&](const std::string& name) { return have.contains(lower(name)); });
}

std::span<const std::byte> first_given(std::span<const std::byte> a, std::span<const std::byte> b,
                                       std::span<const std::byte> c = {}) {
  if (!a.empty()) return a;
  return b.empty() ? c : b;
}

std::vector<float> reflected_gaps(const std::vector<std::pair<std::size_t, std::size_t>>& pairs,
                                  const std::vector<HkxBoneFrame>& frames, std::size_t lane) {
  std::vector<float> apart;
  for (const auto& [left, right] : pairs) {
    if (left >= frames.size() || right >= frames.size()) continue;
    const auto& a = frames[left].position;
    auto b = frames[right].position;
    b[lane] = -b[lane];
    const float dx = a[0] - b[0];
    const float dy = a[1] - b[1];
    const float dz = a[2] - b[2];
    apart.push_back(std::sqrt(dx * dx + dy * dy + dz * dz));
  }
  std::sort(apart.begin(), apart.end());
  return apart;
}

CharacterLevel level_of(nif::SkinFindingLevel level) {
  return level == nif::SkinFindingLevel::error ? CharacterLevel::error : CharacterLevel::warning;
}

std::string shape_label(const nif::Shape& shape) {
  return "shape '" + shape.name + "' (block " + number(shape.block) + ")";
}

// Constraint: the shape was already accepted by bind_skin_shapes, so its four-slot arrays are complete.
void inspect_shape_weights(const nif::Shape& shape, CharacterSkinShape& out) {
  const std::size_t vertices = shape.verts.size() / 3;
  std::vector<std::uint32_t> off_sum;
  std::vector<std::uint32_t> broken;
  std::size_t unbound = 0;
  for (std::size_t vertex = 0; vertex < vertices; ++vertex) {
    float total = 0.0f;
    bool bad = false;
    bool hits_unmatched = false;
    for (std::size_t slot = 0; slot < k_slots; ++slot) {
      const float weight = shape.weights[vertex * k_slots + slot];
      const auto bone = static_cast<std::size_t>(shape.bone_indices[vertex * k_slots + slot]);
      if (!std::isfinite(weight)) continue;
      if (weight < 0.0f) {
        bad = true;
        continue;
      }
      if (weight == 0.0f) continue;
      total += weight;
      for (std::size_t earlier = 0; earlier < slot; ++earlier) {
        const float other = shape.weights[vertex * k_slots + earlier];
        if (std::isfinite(other) && other > 0.0f &&
            static_cast<std::size_t>(shape.bone_indices[vertex * k_slots + earlier]) == bone) {
          bad = true;
        }
      }
      if (bone < out.binding.bones.size() && out.binding.bones[bone].match == nif::SkinBoneMatch::unmatched) {
        hits_unmatched = true;
      }
    }
    if (total > 0.0f && std::fabs(total - 1.0f) > k_sum_tolerance) off_sum.push_back(static_cast<std::uint32_t>(vertex));
    if (bad) broken.push_back(static_cast<std::uint32_t>(vertex));
    if (hits_unmatched) ++unbound;
  }
  out.unbound_vertices = unbound;

  const auto listed = [](const std::vector<std::uint32_t>& all) {
    return std::vector<std::uint32_t>(all.begin(),
                                      all.begin() + static_cast<std::ptrdiff_t>(std::min(all.size(), k_vertices_listed)));
  };
  if (!broken.empty()) {
    out.repairs.push_back({CharacterRepairKind::set_influences, shape.block, listed(broken), broken.size(),
                           "a vertex puts one bone in two slots or carries a negative weight; rewrite its influences"});
  }
  if (!off_sum.empty()) {
    out.repairs.push_back({CharacterRepairKind::normalize_weights, shape.block, listed(off_sum), off_sum.size(),
                           "a weight sum other than one shrinks or stretches the vertex as the rig moves; normalise the shape"});
  }
}

void check_skin(const CharacterAssets& assets, const HkxSkeleton& skeleton, CharacterReport& report) {
  if (assets.body_nif.empty()) return;
  std::optional<nif::NifFile> body;
  try {
    body.emplace(nif::read_nif(assets.body_nif));
  } catch (const parse_error& error) {
    report.skin_status = "parse_error";
    report.findings.push_back(finding(CharacterLevel::error, "skin", "body NIF", error.parse_message()));
    return;
  } catch (const std::exception& error) {
    report.skin_status = "parse_error";
    report.findings.push_back(finding(CharacterLevel::error, "skin", "body NIF", error.what()));
    return;
  }

  std::optional<nif::NifFile> tree;
  if (!assets.skeleton_nif.empty()) {
    try {
      tree.emplace(nif::read_nif(assets.skeleton_nif));
    } catch (const std::exception& error) {
      report.findings.push_back(finding(CharacterLevel::error, "skin", "skeleton NIF",
                                        std::string("it was not read, so helper bones are not resolved: ") + error.what()));
    }
  }
  std::vector<nif::SkinNodeSource> sources;
  if (tree) sources.push_back({&*tree, assets.skeleton_nif_label});
  sources.push_back({&*body, "body NIF"});

  auto bound = nif::bind_skin_shapes(*body, std::nullopt, skeleton.bone_names, sources);
  report.unskinned_shapes = bound.unskinned;
  report.skin_refused = std::move(bound.refused);
  if (bound.status != nif::SkinShapesStatus::ok) {
    report.skin_status = nif::skin_shapes_status_name(bound.status);
    report.findings.push_back(finding(CharacterLevel::warning, "skin", "body NIF", bound.message));
  }
  for (const auto& refusal : report.skin_refused) {
    report.findings.push_back(finding(CharacterLevel::warning, "skin", "block " + number(refusal.block),
                                      "the shape is not checked: " + refusal.reason));
  }

  for (auto& binding : bound.shapes) {
    CharacterSkinShape shape;
    shape.binding = std::move(binding);
    const nif::Shape read = nif::read_shape(*body, shape.binding.block);
    shape.skin = nif::validate_skin(*body, read);
    const std::string label = shape_label(read);
    for (const auto& item : shape.skin.findings) {
      report.findings.push_back(finding(level_of(item.level), "skin", label + " " + item.where, item.what));
    }
    if (!shape.binding.warning.empty()) {
      report.findings.push_back(finding(CharacterLevel::warning, "skin", label, shape.binding.warning));
    }
    inspect_shape_weights(read, shape);
    if (shape.unbound_vertices > 0) {
      report.findings.push_back(finding(
          CharacterLevel::warning, "skin", label,
          plural(shape.unbound_vertices, "vertex") +
              " are weighted to a bone the skeleton does not have, so that share does nothing"));
    }
    report.shapes.push_back(std::move(shape));
  }
}

void check_ragdoll(const CharacterAssets& assets, const HkxSkeleton& skeleton, CharacterReport& report) {
  const bool given = !assets.ragdoll.empty();
  const auto bytes = first_given(assets.ragdoll, assets.skeleton);
  HkxPackfile packfile;
  if (const auto status = read_packfile(bytes, packfile); status != HkxStatus::Ok) {
    report.findings.push_back(finding(CharacterLevel::error, "ragdoll", "file", status_text(status, bytes, "packfile read")));
    return;
  }
  HkxRagdollModel model;
  HkxPhysicsLoadReport load;
  const auto status = read_ragdoll(packfile, model, load);
  report.ragdoll.status = load.status_slug();
  report.ragdoll.headline = load.headline();
  if (status != HkxStatus::Ok) {
    if (given) {
      report.findings.push_back(finding(CharacterLevel::warning, "ragdoll", "file",
                                        "the file holds no usable ragdoll: " + load.headline()));
    } else {
      report.findings.push_back(finding(CharacterLevel::info, "ragdoll", "file",
                                        "the skeleton file holds no usable ragdoll: " + load.headline()));
    }
    return;
  }
  auto& summary = report.ragdoll;
  summary.present = true;
  summary.bodies = model.bodies.size();
  summary.shapes = model.shapes.size();
  summary.constraints = model.constraints.size();
  summary.bone_bindings = model.bone_bindings.size();
  summary.physics_bones = model.skeleton ? model.skeleton->bone_names.size() : 0;
  summary.animation_bones = model.animation_skeleton ? model.animation_skeleton->bone_names.size() : 0;

  std::optional<std::int32_t> bone_count;
  if (model.skeleton) bone_count = static_cast<std::int32_t>(model.skeleton->bone_names.size());
  for (const auto& item : check_physics_model(model, bone_count)) {
    report.findings.push_back(finding(item.level == HkxPhysicsValidationLevel::Error ? CharacterLevel::error
                                                                                     : CharacterLevel::warning,
                                      "ragdoll", item.where, item.message));
  }
  if (model.animation_skeleton) {
    std::unordered_set<std::string> have;
    for (const auto& name : skeleton.bone_names) have.insert(lower(name));
    std::vector<std::string> missing;
    for (const auto& name : model.animation_skeleton->bone_names) {
      if (!have.contains(lower(name))) missing.push_back(name);
    }
    if (!missing.empty()) {
      report.findings.push_back(finding(
          CharacterLevel::warning, "ragdoll", "animation skeleton",
          plural(missing.size(), "bone") + " of its " + number(model.animation_skeleton->bone_names.size()) +
              " are not in the character skeleton: " + name_list(missing)));
    }
    const auto& names = model.animation_skeleton->bone_names;
    for (std::size_t i = 0; i < model.mappings.size(); ++i) {
      const auto bone = model.mappings[i].bone_a;
      if (bone < 0 || static_cast<std::size_t>(bone) >= names.size() || have.contains(lower(names[bone]))) continue;
      report.findings.push_back(finding(CharacterLevel::warning, "ragdoll", "mapping " + number(i),
                                        "animation bone " + std::to_string(bone) + " (" + names[bone] +
                                            ") is not in the character skeleton"));
    }
  }
}

void check_mappers(const CharacterAssets& assets, const HkxSkeleton& skeleton, CharacterReport& report) {
  const bool given = !assets.mapper.empty();
  const auto bytes = first_given(assets.mapper, assets.ragdoll, assets.skeleton);
  HkxPackfile packfile;
  if (const auto status = read_packfile(bytes, packfile); status != HkxStatus::Ok) {
    report.findings.push_back(finding(CharacterLevel::error, "mapper", "file", status_text(status, bytes, "packfile read")));
    return;
  }
  std::vector<HkxSkeletonMapper> found;
  const auto status = read_skeleton_mappers(packfile, found);
  if (status == HkxStatus::MissingObject) {
    if (given) report.findings.push_back(finding(CharacterLevel::warning, "mapper", "file", "the file holds no skeleton mapper"));
    return;
  }
  if (status != HkxStatus::Ok) {
    report.findings.push_back(finding(CharacterLevel::error, "mapper", "file", status_text(status, bytes, "skeleton mapper read")));
    return;
  }
  const auto in_range = [](std::int16_t bone, std::size_t count) {
    return bone >= 0 && static_cast<std::size_t>(bone) < count;
  };
  for (std::size_t index = 0; index < found.size(); ++index) {
    const auto& mapper = found[index];
    CharacterMapperSummary summary;
    summary.object_index = mapper.object_index;
    summary.skeleton_a = mapper.skeleton_a.name;
    summary.skeleton_b = mapper.skeleton_b.name;
    summary.bones_a = mapper.skeleton_a.bone_names.size();
    summary.bones_b = mapper.skeleton_b.bone_names.size();
    summary.rows = mapper.rows.size();
    summary.chains = mapper.chains.size();
    summary.a_has_character_bones = has_all_names(skeleton.bone_names, mapper.skeleton_a.bone_names);
    summary.b_has_character_bones = has_all_names(skeleton.bone_names, mapper.skeleton_b.bone_names);
    const std::string where = "mapper " + number(index);
    if (!summary.a_has_character_bones && !summary.b_has_character_bones) {
      report.findings.push_back(finding(CharacterLevel::warning, "mapper", where,
                                        "neither of its skeletons carries every bone name of the character skeleton"));
    }
    for (std::size_t row = 0; row < mapper.rows.size(); ++row) {
      const auto& item = mapper.rows[row];
      if (!in_range(item.bone_a, summary.bones_a) || !in_range(item.bone_b, summary.bones_b)) {
        report.findings.push_back(finding(CharacterLevel::error, "mapper", where + " row " + number(row),
                                          "it names a bone outside its skeleton"));
      }
    }
    for (std::size_t row = 0; row < mapper.chains.size(); ++row) {
      const auto& item = mapper.chains[row];
      if (!in_range(item.start_bone_a, summary.bones_a) || !in_range(item.end_bone_a, summary.bones_a) ||
          !in_range(item.start_bone_b, summary.bones_b) || !in_range(item.end_bone_b, summary.bones_b)) {
        report.findings.push_back(finding(CharacterLevel::error, "mapper", where + " chain " + number(row),
                                          "it names a bone outside its skeleton"));
      }
    }
    report.mappers.push_back(std::move(summary));
  }
}

}  // namespace

const char* character_level_name(CharacterLevel level) noexcept {
  switch (level) {
    case CharacterLevel::error: return "error";
    case CharacterLevel::warning: return "warning";
    case CharacterLevel::info: return "info";
  }
  return "error";
}

const char* character_repair_kind_name(CharacterRepairKind kind) noexcept {
  switch (kind) {
    case CharacterRepairKind::normalize_weights: return "normalize_weights";
    case CharacterRepairKind::set_influences: return "set_influences";
  }
  return "normalize_weights";
}

const char* character_document_kind_name(CharacterDocumentKind kind) noexcept {
  switch (kind) {
    case CharacterDocumentKind::skeleton: return "skeleton";
    case CharacterDocumentKind::ragdoll: return "ragdoll";
    case CharacterDocumentKind::mapper: return "mapper";
    case CharacterDocumentKind::skin: return "skin";
  }
  return "skeleton";
}

std::size_t CharacterReport::errors() const noexcept {
  return static_cast<std::size_t>(std::count_if(
      findings.begin(), findings.end(), [](const CharacterFinding& item) { return item.level == CharacterLevel::error; }));
}

std::size_t CharacterReport::warnings() const noexcept {
  return static_cast<std::size_t>(std::count_if(
      findings.begin(), findings.end(), [](const CharacterFinding& item) { return item.level == CharacterLevel::warning; }));
}

bool CharacterSavePlan::ok() const noexcept {
  return std::none_of(findings.begin(), findings.end(),
                      [](const CharacterFinding& item) { return item.level == CharacterLevel::error; });
}

std::vector<CharacterFinding> validate_skeleton(const HkxSkeleton& skeleton) {
  std::vector<CharacterFinding> out;
  const std::size_t bones = skeleton.bone_names.size();
  if (bones == 0) out.push_back(finding(CharacterLevel::error, "skeleton", "skeleton", "it has no bones"));

  parallel_length(out, "parentIndices", skeleton.parent_indices.size(), bones);
  parallel_length(out, "referencePose", skeleton.reference_pose.size(), bones);
  if (!skeleton.lock_translation.empty()) parallel_length(out, "lockTranslations", skeleton.lock_translation.size(), bones);
  if (!skeleton.reference_floats.empty() || !skeleton.float_slots.empty()) {
    parallel_length(out, "referenceFloats", skeleton.reference_floats.size(), skeleton.float_slots.size(), "floatSlots");
  }

  std::unordered_set<std::string> seen;
  std::vector<std::size_t> first_index;
  for (std::size_t i = 0; i < bones; ++i) {
    const std::string& name = skeleton.bone_names[i];
    const bool blank = std::all_of(name.begin(), name.end(), [](unsigned char ch) { return std::isspace(ch) != 0; });
    const std::string where = "bone " + number(i) + (name.empty() ? "" : " '" + name + "'");
    if (blank) {
      out.push_back(finding(CharacterLevel::error, "skeleton", where, "it has no name"));
    } else {
      const std::string key = lower(name);
      if (!seen.insert(key).second) {
        std::size_t first = 0;
        for (std::size_t j = 0; j < i; ++j) {
          if (lower(skeleton.bone_names[j]) == key) {
            first = j;
            break;
          }
        }
        out.push_back(finding(CharacterLevel::error, "skeleton", where,
                              "bone " + number(first) + " is already called '" + name + "'"));
      }
    }
    if (i >= skeleton.parent_indices.size()) continue;
    const int parent = skeleton.parent_indices[i];
    if (parent >= 0 && static_cast<std::size_t>(parent) >= bones) {
      out.push_back(finding(CharacterLevel::error, "skeleton", where,
                            "its parent is " + std::to_string(parent) + ", past the last bone (" + number(bones > 0 ? bones - 1 : 0) + ")"));
    } else if (parent == static_cast<int>(i)) {
      out.push_back(finding(CharacterLevel::error, "skeleton", where, "it is its own parent"));
    } else if (parent >= 0 && parent > static_cast<int>(i)) {
      out.push_back(finding(CharacterLevel::error, "skeleton", where,
                            "its parent is bone " + std::to_string(parent) +
                                ", stored after it; a bone's parent has to be stored before the bone itself"));
    }
    if (i < skeleton.reference_pose.size()) check_pose(out, where, skeleton.reference_pose[i]);
  }

  const std::size_t chain_bones = skeleton.parent_indices.size();
  for (std::size_t i = 0; i < chain_bones; ++i) {
    long at = static_cast<long>(i);
    for (std::size_t step = 0; step <= chain_bones; ++step) {
      if (at < 0 || at >= static_cast<long>(chain_bones)) break;
      at = skeleton.parent_indices[static_cast<std::size_t>(at)];
      if (at != static_cast<long>(i)) continue;
      const std::string name = i < skeleton.bone_names.size() ? skeleton.bone_names[i] : std::string();
      out.push_back(finding(CharacterLevel::error, "skeleton", "bone " + number(i) + (name.empty() ? "" : " '" + name + "'"),
                            "its parent chain comes back to it"));
      break;
    }
  }

  const auto roots = static_cast<std::size_t>(std::count_if(
      skeleton.parent_indices.begin(), skeleton.parent_indices.end(), [](std::int16_t parent) { return parent < 0; }));
  if (bones > 0 && roots == 0) {
    out.push_back(finding(CharacterLevel::error, "skeleton", "skeleton", "no bone is a root, so every bone has a parent"));
  } else if (roots > 1) {
    out.push_back(finding(CharacterLevel::warning, "skeleton", "skeleton",
                          number(roots) + " bones are roots; a character rig normally has one"));
  }
  return out;
}

SkeletonMirrorReport inspect_mirror_pairs(const HkxSkeleton& skeleton) {
  SkeletonMirrorReport out;
  const auto paired = nif::skeleton_pairs(skeleton.bone_names);
  out.ambiguous = paired.ambiguous;
  out.unconfirmed = paired.unconfirmed;
  std::vector<std::pair<std::size_t, std::size_t>> pairs;
  for (std::size_t i = 0; i < paired.partner.size(); ++i) {
    const auto other = paired.partner[i];
    if (other > static_cast<std::int32_t>(i) && static_cast<std::size_t>(other) < paired.partner.size() &&
        paired.partner[static_cast<std::size_t>(other)] == static_cast<std::int32_t>(i)) {
      pairs.emplace_back(i, static_cast<std::size_t>(other));
    }
  }
  out.pairs = pairs.size();
  if (pairs.empty()) return out;

  const auto frames = world_reference_frames(skeleton);
  float best = 0.0f;
  for (std::size_t lane = 0; lane < 3; ++lane) {
    const auto apart = reflected_gaps(pairs, frames, lane);
    if (apart.empty()) continue;
    const float typical = apart[apart.size() / 2];
    if (out.lane < 0 || typical < best) {
      out.lane = static_cast<std::int32_t>(lane);
      out.typical = typical;
      out.worst = apart.back();
      best = typical;
    }
  }
  return out;
}

namespace {

void check_chain(const CharacterAssets& assets, CharacterReport& report) {
  if (assets.assets == nullptr || assets.project_hkx.empty()) return;
  const auto chain = resolve_project_chain(*assets.assets, assets.project_hkx);
  auto& out = report.chain;
  out.checked = true;
  out.root = chain.root;
  out.links = chain.links.size();
  out.animations = chain.animations.size();
  out.missing_animations = static_cast<std::size_t>(std::count_if(
      chain.animations.begin(), chain.animations.end(), [](const HkxChainLink& link) { return !link.exists; }));
  out.problems = chain.problems;
  out.warnings = chain.warnings;
  for (const auto& text : chain.problems) report.findings.push_back(finding(CharacterLevel::error, "animation", "project chain", text));
  for (const auto& text : chain.warnings) report.findings.push_back(finding(CharacterLevel::warning, "animation", "project chain", text));
}

}  // namespace

CharacterReport check_character(const CharacterAssets& assets) {
  CharacterReport report;
  HkxPackfile packfile;
  if (const auto status = read_packfile(assets.skeleton, packfile); status != HkxStatus::Ok) {
    report.refusal = status_text(status, assets.skeleton, "skeleton packfile read");
    return report;
  }
  HkxSkeleton skeleton;
  if (const auto status = read_skeleton(packfile, skeleton); status != HkxStatus::Ok) {
    report.refusal = status_text(status, assets.skeleton, "skeleton read");
    return report;
  }
  report.skeleton_name = skeleton.name;
  report.bones = skeleton.bone_names.size();

  for (auto& item : validate_skeleton(skeleton)) report.findings.push_back(std::move(item));
  report.mirror = inspect_mirror_pairs(skeleton);
  if (report.mirror.pairs == 0) {
    report.findings.push_back(finding(CharacterLevel::info, "skeleton", "mirror", "no bone in this rig pairs with an opposite one"));
  } else {
    report.findings.push_back(finding(
        CharacterLevel::info, "skeleton", "mirror",
        plural(report.mirror.pairs, "left/right pair") +
            (report.mirror.ambiguous.empty() ? "" : ", " + number(report.mirror.ambiguous.size()) + " name a side in more than one place") +
            (report.mirror.unconfirmed.empty() ? "" : ", " + number(report.mirror.unconfirmed.size()) + " the rig did not bear out")));
  }

  check_ragdoll(assets, skeleton, report);
  check_mappers(assets, skeleton, report);
  check_chain(assets, report);
  check_skin(assets, skeleton, report);
  return report;
}

namespace {

struct Dependent {
  std::string what;
  std::vector<std::int32_t> bones;
};

std::vector<std::int32_t> new_index_of(std::size_t old_count, const std::vector<std::size_t>& origin) {
  std::vector<std::int32_t> moved(old_count, -1);
  for (std::size_t now = 0; now < origin.size(); ++now) {
    if (origin[now] < old_count) moved[origin[now]] = static_cast<std::int32_t>(now);
  }
  return moved;
}

void count_shifted(const Dependent& dependent, const std::vector<std::int32_t>& moved, const HkxSkeleton& before,
                   CharacterSavePlan& plan) {
  std::size_t shifted = 0;
  std::size_t removed = 0;
  std::vector<std::string> named;
  for (const auto bone : dependent.bones) {
    if (bone < 0 || static_cast<std::size_t>(bone) >= moved.size()) continue;
    const auto now = moved[static_cast<std::size_t>(bone)];
    if (now == bone) continue;
    ++shifted;
    if (now < 0) {
      ++removed;
      if (named.size() < k_names_listed) named.push_back(before.bone_names[static_cast<std::size_t>(bone)]);
    }
  }
  if (shifted == 0) return;
  plan.shifted_references += shifted;
  std::string message = plural(shifted, "reference") + " would point at another bone after the edit";
  if (removed > 0) message += ", " + number(removed) + " at a removed bone (" + name_list(named) + ")";
  message += "; the dependent file is not rewritten, so remap it before saving the skeleton";
  plan.findings.push_back(finding(CharacterLevel::error, "save", dependent.what, message));
}

void check_impact(const CharacterSaveInput& input, CharacterSavePlan& plan) {
  if (input.skeleton.empty() || input.skeleton_edits.empty()) return;
  HkxPackfile packfile;
  HkxSkeleton before;
  if (const auto status = read_packfile(input.skeleton, packfile); status != HkxStatus::Ok) {
    plan.findings.push_back(finding(CharacterLevel::error, "save", "skeleton", status_text(status, input.skeleton, "skeleton packfile read")));
    return;
  }
  if (const auto status = read_skeleton(packfile, before); status != HkxStatus::Ok) {
    plan.findings.push_back(finding(CharacterLevel::error, "save", "skeleton", status_text(status, input.skeleton, "skeleton read")));
    return;
  }
  HkxSkeleton after;
  std::vector<std::size_t> origin;
  const auto applied = apply_skeleton_edits(before, input.skeleton_edits, after, &origin);
  if (!applied.refusal.empty()) {
    plan.findings.push_back(finding(CharacterLevel::error, "save", "skeleton edits", applied.refusal));
    return;
  }
  std::vector<std::size_t> identity(before.bone_names.size());
  std::iota(identity.begin(), identity.end(), std::size_t{0});
  if (origin == identity) return;

  plan.findings.push_back(finding(CharacterLevel::info, "save", "animations",
                                  "animation clips bind tracks to bones by position and are not rewritten by a skeleton save"));
  const auto moved = new_index_of(before.bone_names.size(), origin);

  const auto ragdoll_bytes = first_given(input.ragdoll, input.skeleton);
  const auto ragdoll_given = !input.ragdoll.empty();
  const auto skipped = [&](const std::string& why) {
    if (ragdoll_given) {
      plan.findings.push_back(finding(CharacterLevel::warning, "save", "ragdoll", why + "; its bone references were not checked"));
    }
  };
  HkxPackfile ragdoll_pack;
  if (const auto status = read_packfile(ragdoll_bytes, ragdoll_pack); status != HkxStatus::Ok) {
    skipped(status_text(status, input.ragdoll, "ragdoll packfile read"));
  } else {
    HkxRagdollModel model;
    HkxPhysicsLoadReport load;
    if (const auto read_status = read_ragdoll(ragdoll_pack, model, load); read_status != HkxStatus::Ok) {
      skipped(status_text(read_status, input.ragdoll, "ragdoll read"));
    } else if (!model.skeleton || !same_names(model.skeleton->bone_names, before.bone_names)) {
      skipped("its skeleton does not match the one being edited");
    } else {
      Dependent bindings{"ragdoll bone bindings", {}};
      for (const auto& binding : model.bone_bindings) bindings.bones.push_back(binding.bone_index);
      count_shifted(bindings, moved, before, plan);
    }
  }
  const auto mapper_bytes = first_given(input.mapper, input.ragdoll, input.skeleton);
  const auto mapper_given = !input.mapper.empty();
  HkxPackfile mapper_pack;
  std::vector<HkxSkeletonMapper> mappers;
  if (const auto status = read_packfile(mapper_bytes, mapper_pack); status != HkxStatus::Ok) {
    if (mapper_given) {
      plan.findings.push_back(finding(CharacterLevel::warning, "save", "mappers",
                                      status_text(status, input.mapper, "mapper packfile read") + "; its bone references were not checked"));
    }
    return;
  }
  if (const auto status = read_skeleton_mappers(mapper_pack, mappers); status != HkxStatus::Ok) {
    if (mapper_given) {
      plan.findings.push_back(finding(CharacterLevel::warning, "save", "mappers",
                                      status_text(status, input.mapper, "mapper read") + "; its bone references were not checked"));
    }
    return;
  }
  bool matched = false;
  for (std::size_t index = 0; index < mappers.size(); ++index) {
    const auto& mapper = mappers[index];
    for (int side = 0; side < 2; ++side) {
      const auto& rig = side == 0 ? mapper.skeleton_a : mapper.skeleton_b;
      if (!same_names(rig.bone_names, before.bone_names)) continue;
      matched = true;
      Dependent rows{"mapper " + number(index) + " skeleton " + (side == 0 ? "A" : "B"), {}};
      for (const auto& row : mapper.rows) rows.bones.push_back(side == 0 ? row.bone_a : row.bone_b);
      for (const auto& chain : mapper.chains) {
        rows.bones.push_back(side == 0 ? chain.start_bone_a : chain.start_bone_b);
        rows.bones.push_back(side == 0 ? chain.end_bone_a : chain.end_bone_b);
      }
      if (side == 0) {
        for (const auto bone : mapper.unmapped_bones) rows.bones.push_back(bone);
      }
      count_shifted(rows, moved, before, plan);
    }
  }
  if (mapper_given && !matched) {
    plan.findings.push_back(finding(CharacterLevel::warning, "save", "mappers",
                                    "no mapper in the given file uses the skeleton being edited; its bone references were not checked"));
  }
}

int rank_of(CharacterDocumentKind kind) { return static_cast<int>(kind); }

}  // namespace

std::string character_path_key(std::string_view path) {
  std::error_code ec;
  const auto resolved = std::filesystem::weakly_canonical(std::filesystem::path(std::string(path)), ec);
  std::string out = lower(ec ? std::string(path) : resolved.generic_string());
  std::replace(out.begin(), out.end(), '\\', '/');
  return out;
}

bool character_same_file(std::string_view a, std::string_view b) {
  if (a.empty() || b.empty()) return false;
  if (character_path_key(a) == character_path_key(b)) return true;
  std::error_code ec;
  return std::filesystem::equivalent(std::filesystem::path(std::string(a)), std::filesystem::path(std::string(b)), ec) && !ec;
}

CharacterSavePlan plan_character_save(const CharacterSaveInput& input) {
  CharacterSavePlan plan;
  const auto& docs = input.documents;
  const auto bad = [&](std::size_t index, std::string message) {
    plan.findings.push_back(finding(CharacterLevel::error, "save", character_document_kind_name(docs[index].kind), std::move(message)));
  };

  std::vector<std::string> writes(docs.size());
  for (std::size_t i = 0; i < docs.size(); ++i) {
    if (docs[i].source.empty()) bad(i, "it has no source file");
    if (docs[i].target.empty() && docs[i].kind != CharacterDocumentKind::skeleton) {
      bad(i, "it needs a target file: only a skeleton save may replace its source in place");
    }
    writes[i] = docs[i].target.empty() ? docs[i].source : docs[i].target;
  }

  std::vector<std::size_t> order(docs.size());
  std::iota(order.begin(), order.end(), std::size_t{0});
  std::stable_sort(order.begin(), order.end(),
                   [&](std::size_t a, std::size_t b) { return rank_of(docs[a].kind) < rank_of(docs[b].kind); });

  std::vector<std::string> chain_end;
  std::vector<std::string> chain_source;
  for (const std::size_t i : order) {
    CharacterSaveStep step;
    step.document = i;
    step.kind = docs[i].kind;
    step.reads = docs[i].source;
    step.writes = writes[i];
    if (docs[i].kind != CharacterDocumentKind::skin) {
      const std::string key = character_path_key(docs[i].source);
      for (std::size_t c = 0; c < chain_source.size(); ++c) {
        if (chain_source[c] != key) continue;
        step.reads = chain_end[c];
        step.reads_previous_output = true;
        chain_end[c] = step.writes;
        break;
      }
      if (!step.reads_previous_output) {
        chain_source.push_back(key);
        chain_end.push_back(step.writes);
      }
    }
    if (docs[i].kind == CharacterDocumentKind::ragdoll && !step.writes.empty() && character_same_file(step.writes, step.reads)) {
      bad(i, "the ragdoll writer refuses a target equal to the file it reads; choose another target");
    }
    const bool own_source = !docs[i].source.empty() && character_same_file(step.writes, docs[i].source);
    const bool refused_already = docs[i].kind == CharacterDocumentKind::ragdoll && !step.reads_previous_output;
    const bool no_target = docs[i].target.empty() && docs[i].kind != CharacterDocumentKind::skeleton;
    if (own_source && !input.overwrite && !refused_already && !no_target) {
      bad(i, docs[i].target.empty()
                 ? "it would replace its source file in place; set overwrite (a .bak copy of the original is kept) or choose a target"
                 : "its target is its own source file; choose another target or set overwrite (a .bak copy of the original is kept)");
    }
    if (step.reads_previous_output && own_source) {
      plan.findings.push_back(finding(CharacterLevel::warning, "save", character_document_kind_name(docs[i].kind),
                                      "it replaces the original source file, which an earlier step already wrote a copy of"));
    }
    plan.steps.push_back(std::move(step));
  }

  for (std::size_t a = 0; a < docs.size(); ++a) {
    for (std::size_t b = a + 1; b < docs.size(); ++b) {
      if (!writes[a].empty() && !writes[b].empty() && character_same_file(writes[a], writes[b])) {
        bad(b, "it writes the same file as the " + std::string(character_document_kind_name(docs[a].kind)) +
                   " document, so the second save would replace the first");
      }
    }
  }
  for (std::size_t a = 0; a < docs.size(); ++a) {
    for (std::size_t b = 0; b < docs.size(); ++b) {
      if (a == b || docs[a].target.empty()) continue;
      const bool shares_chain = docs[a].kind != CharacterDocumentKind::skin && docs[b].kind != CharacterDocumentKind::skin &&
                                character_same_file(docs[a].source, docs[b].source);
      if (!shares_chain && character_same_file(docs[a].target, docs[b].source)) {
        bad(a, "its target is the source file of the " + std::string(character_document_kind_name(docs[b].kind)) + " document");
      }
    }
  }

  check_impact(input, plan);
  return plan;
}

}  // namespace fo4::hkx
