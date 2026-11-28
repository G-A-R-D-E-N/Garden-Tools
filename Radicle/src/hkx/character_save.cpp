#include "fo4/hkx/character_save.hpp"

#include "fo4/error.hpp"
#include "fo4/hkx/skeleton_write.hpp"
#include "fo4/io/atomic_write.hpp"
#include "fo4/io/path_text.hpp"
#include "fo4/io/sha256.hpp"
#include "fo4/nif/nif.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <limits>
#include <map>
#include <optional>
#include <stdexcept>
#include <string_view>
#include <system_error>
#include <utility>

namespace fo4::hkx {
namespace {

constexpr std::array<std::string_view, 10> k_status_names{
    "Ok", "Truncated", "BadMagic", "UnsupportedLayout", "MissingSection",
    "BadSection", "FixupOutOfRange", "DuplicateFixup", "BadClassName", "MissingObject",
};

std::string status_name(HkxStatus status) {
  const auto index = static_cast<std::size_t>(status);
  return index < k_status_names.size() ? std::string(k_status_names[index]) : "unknown";
}

const char* doc_name(CharacterDocumentKind kind) { return character_document_kind_name(kind); }

std::string step_label(std::size_t index, CharacterDocumentKind kind) {
  return "step " + std::to_string(index + 1) + " (" + doc_name(kind) + ")";
}

std::optional<std::vector<std::byte>> read_file(const std::string& path) {
  std::ifstream stream(std::filesystem::path(path), std::ios::binary);
  if (!stream) return std::nullopt;
  const std::vector<char> raw((std::istreambuf_iterator<char>(stream)), std::istreambuf_iterator<char>());
  if (stream.bad()) return std::nullopt;
  const auto view = std::as_bytes(std::span(raw));
  return std::vector<std::byte>(view.begin(), view.end());
}

float rotation_gap(const std::array<float, 4>& left, const std::array<float, 4>& right) {
  double dot = 0.0;
  for (std::size_t i = 0; i < 4; ++i) dot += static_cast<double>(left[i]) * right[i];
  if (!std::isfinite(dot)) return std::numeric_limits<float>::quiet_NaN();
  return static_cast<float>(2.0 * std::acos(std::min(1.0, std::fabs(dot))));
}

std::optional<std::int64_t> integer_member(const nlohmann::json& item, const char* key) {
  const auto found = item.find(key);
  if (found == item.end() || found->is_null()) return std::nullopt;
  if (!found->is_number_integer()) return std::numeric_limits<std::int64_t>::min();
  return found->get<std::int64_t>();
}

constexpr std::int64_t k_uint32_max = std::numeric_limits<std::uint32_t>::max();

bool fits_int32(std::int64_t value) {
  return value >= std::numeric_limits<std::int32_t>::min() && value <= std::numeric_limits<std::int32_t>::max();
}

std::string verify_output(CharacterDocumentKind kind, std::span<const std::byte> bytes) {
  if (kind == CharacterDocumentKind::skin) {
    try {
      (void)nif::read_nif(bytes);
    } catch (const parse_error& error) {
      return error.parse_message();
    } catch (const std::exception& error) {
      return error.what();
    }
    return {};
  }
  HkxPackfile packfile;
  if (const auto status = read_packfile(bytes, packfile); status != HkxStatus::Ok) return "packfile read: " + status_name(status);
  if (kind == CharacterDocumentKind::skeleton) {
    HkxSkeleton skeleton;
    if (const auto status = read_skeleton(packfile, skeleton); status != HkxStatus::Ok) return "skeleton read: " + status_name(status);
  } else if (kind == CharacterDocumentKind::mapper) {
    std::vector<HkxSkeletonMapper> mappers;
    if (const auto status = read_skeleton_mappers(packfile, mappers); status != HkxStatus::Ok) return "mapper read: " + status_name(status);
  } else {
    HkxRagdollModel model;
    HkxPhysicsLoadReport load;
    if (const auto status = read_ragdoll(packfile, model, load); status != HkxStatus::Ok) return "ragdoll read: " + load.headline();
  }
  return {};
}

}  // namespace

std::string parse_skeleton_edits(const nlohmann::json& edits, std::vector<HkxSkeletonEdit>& out) {
  out.clear();
  if (!edits.is_array()) return "edits must be a JSON array";
  for (std::size_t i = 0; i < edits.size(); ++i) {
    const auto& item = edits[i];
    const std::string where = "edit " + std::to_string(i + 1) + ": ";
    if (!item.is_object()) return where + "must be an object";
    HkxSkeletonEdit edit;
    const auto kind = item.find("kind");
    if (kind == item.end() || !kind->is_string()) return where + "kind is missing";
    const auto text = kind->get<std::string>();
    if (text == "rename") {
      edit.kind = HkxSkeletonEditKind::Rename;
    } else if (text == "reparent") {
      edit.kind = HkxSkeletonEditKind::Reparent;
    } else if (text == "remove") {
      edit.kind = HkxSkeletonEditKind::Remove;
    } else if (text == "set_pose") {
      edit.kind = HkxSkeletonEditKind::SetPose;
    } else if (text == "set_lock") {
      edit.kind = HkxSkeletonEditKind::SetLock;
    } else {
      return where + "kind must be rename, reparent, remove, set_pose or set_lock";
    }
    const auto bone = item.find("bone");
    if (bone == item.end() || !bone->is_number_integer() || bone->get<std::int64_t>() < 0) {
      return where + "bone must be zero or more";
    }
    edit.bone = static_cast<std::size_t>(bone->get<std::int64_t>());
    if (const auto name = item.find("name"); name != item.end()) {
      if (!name->is_string()) return where + "name must be text";
      edit.name = name->get<std::string>();
    }
    if (const auto parent = item.find("parent"); parent != item.end()) {
      if (!parent->is_number_integer() || parent->get<std::int64_t>() < -1 ||
          parent->get<std::int64_t>() > std::numeric_limits<std::int16_t>::max()) {
        return where + "parent must be -1 or a bone index";
      }
      edit.parent = static_cast<std::int16_t>(parent->get<std::int64_t>());
    } else if (edit.kind == HkxSkeletonEditKind::Reparent) {
      return where + "reparent needs a parent";
    }
    if (edit.kind == HkxSkeletonEditKind::SetPose) {
      const auto fill = [&](const char* key, float* lanes, std::size_t size) -> std::string {
        const auto found = item.find(key);
        const std::string shape = std::string(key) + " must be a list of " + std::to_string(size) + " numbers";
        if (found == item.end() || !found->is_array() || found->size() != size) return shape;
        for (std::size_t k = 0; k < size; ++k) {
          if (!(*found)[k].is_number()) return shape;
          const double wide = (*found)[k].get<double>();
          if (!std::isfinite(wide) || std::abs(wide) > static_cast<double>(std::numeric_limits<float>::max())) {
            return std::string(key) + " must hold finite numbers";
          }
          lanes[k] = static_cast<float>(wide);
        }
        return {};
      };
      for (const auto& why : {fill("translation", edit.pose.translation.data(), 3),
                              fill("rotation", edit.pose.rotation.data(), 4), fill("scale", edit.pose.scale.data(), 3)}) {
        if (!why.empty()) return where + "set_pose: " + why;
      }
    } else if (edit.kind == HkxSkeletonEditKind::SetLock) {
      const auto locked = item.find("locked");
      if (locked == item.end() || !locked->is_boolean()) return where + "set_lock needs locked true or false";
      edit.locked = locked->get<bool>();
    }
    out.push_back(std::move(edit));
  }
  return {};
}

std::string parse_skin_edit(const nlohmann::json& item, nif::SkinEditRequest& out) {
  if (!item.is_object()) return "a skin edit must be an object";
  const auto kind_it = item.find("kind");
  if (kind_it == item.end() || !kind_it->is_string()) return "kind must be set_influences, copy_bone or mirror_weights";
  const auto kind = nif::parse_skin_edit_kind(kind_it->get<std::string>());
  if (!kind) return "kind must be set_influences, copy_bone or mirror_weights";
  const auto min = std::numeric_limits<std::int64_t>::min();
  nif::SkinEditRequest request;
  request.kind = *kind;
  const auto block = integer_member(item, "block");
  if (!block || *block == min || *block < 0 || *block > k_uint32_max) return "block must be a block id from 0 to 4294967295";
  request.block = static_cast<std::uint32_t>(*block);
  if (const auto vertex = integer_member(item, "vertex")) {
    if (*vertex == min || *vertex < 0 || *vertex > k_uint32_max) return "vertex must be a vertex index from 0 to 4294967295";
    request.vertex = static_cast<std::uint32_t>(*vertex);
  }
  if (const auto found = item.find("influences"); found != item.end() && !found->is_null()) {
    if (!found->is_array()) return "influences must be a list of {bone, weight} objects";
    for (const auto& entry : *found) {
      if (!entry.is_object() || !entry.contains("bone") || !entry.contains("weight") || !entry.at("bone").is_number_integer() ||
          !entry.at("weight").is_number()) {
        return "influences must be a list of {bone, weight} objects";
      }
      const auto bone = entry.at("bone").get<std::int64_t>();
      if (!fits_int32(bone)) return "influences bone must be a bone index that fits 32 bits";
      request.influences.push_back(nif::Influence{static_cast<std::int32_t>(bone), entry.at("weight").get<float>()});
    }
  }
  if (const auto from = integer_member(item, "from_bone")) {
    if (*from == min || !fits_int32(*from)) return "from_bone must be a bone index that fits 32 bits";
    request.from_bone = static_cast<std::int32_t>(*from);
  }
  if (const auto to = integer_member(item, "to_bone")) {
    if (*to == min || !fits_int32(*to)) return "to_bone must be a bone index that fits 32 bits";
    request.to_bone = static_cast<std::int32_t>(*to);
  }
  if (const auto found = item.find("keep_source"); found != item.end() && !found->is_null()) {
    if (!found->is_boolean()) return "keep_source must be true or false";
    request.keep_source = found->get<bool>();
  }
  if (const auto lane = integer_member(item, "lane")) {
    if (*lane < 0 || *lane > 2) return "lane must be 0, 1 or 2";
    request.lane = static_cast<std::uint8_t>(*lane);
  }
  if (const auto found = item.find("from_positive_side"); found != item.end() && !found->is_null()) {
    if (!found->is_boolean()) return "from_positive_side must be true or false";
    request.from_positive_side = found->get<bool>();
  }
  out = std::move(request);
  return {};
}

namespace {

const nlohmann::json* ragdoll_member(const nlohmann::json& row, const char* key) {
  const auto found = row.find(key);
  if (found == row.end() || found->is_null()) return nullptr;
  return &*found;
}

std::optional<float> ragdoll_number(const nlohmann::json& row, const char* key) {
  const auto* value = ragdoll_member(row, key);
  if (value == nullptr) return std::nullopt;
  if (!value->is_number()) throw std::invalid_argument(std::string(key) + " must be a number");
  return value->get<float>();
}

// Invariant: an integer is range-checked in 64 bits before the 32-bit cast, so a huge value is refused instead of wrapping to a valid id.
std::int32_t ragdoll_id(const nlohmann::json& row, const char* key) {
  const auto* value = ragdoll_member(row, key);
  const auto hi = std::numeric_limits<std::int32_t>::max();
  if (value == nullptr || !value->is_number_integer()) throw std::invalid_argument(std::string(key) + " must be an integer");
  const bool too_big = value->is_number_unsigned() && value->get<std::uint64_t>() > static_cast<std::uint64_t>(hi);
  const std::int64_t number = too_big ? static_cast<std::int64_t>(hi) + 1 : value->get<std::int64_t>();
  if (number < 0 || number > hi) throw std::invalid_argument(std::string(key) + " must be between 0 and " + std::to_string(hi));
  return static_cast<std::int32_t>(number);
}

std::optional<std::int32_t> ragdoll_optional_id(const nlohmann::json& row, const char* key) {
  if (ragdoll_member(row, key) == nullptr) return std::nullopt;
  return ragdoll_id(row, key);
}

std::vector<float> ragdoll_list(const nlohmann::json& value, const char* key) {
  if (!value.is_array()) throw std::invalid_argument(std::string(key) + " must be an array of numbers");
  std::vector<float> out;
  for (const auto& entry : value) {
    if (!entry.is_number()) throw std::invalid_argument(std::string(key) + " must be an array of numbers");
    out.push_back(entry.get<float>());
  }
  return out;
}

template <std::size_t N>
std::optional<std::array<float, N>> ragdoll_array(const nlohmann::json& row, const char* key) {
  const auto* value = ragdoll_member(row, key);
  if (value == nullptr) return std::nullopt;
  const auto list = ragdoll_list(*value, key);
  if (list.size() != N) throw std::invalid_argument(std::string(key) + " must hold " + std::to_string(N) + " numbers");
  std::array<float, N> out{};
  for (std::size_t i = 0; i < N; ++i) out[i] = list[i];
  return out;
}

std::optional<std::vector<float>> ragdoll_optional_list(const nlohmann::json& row, const char* key) {
  const auto* value = ragdoll_member(row, key);
  if (value == nullptr) return std::nullopt;
  return ragdoll_list(*value, key);
}

const nlohmann::json& ragdoll_rows(const nlohmann::json& source, const char* key) {
  static const nlohmann::json none = nlohmann::json::array();
  const auto* value = ragdoll_member(source, key);
  if (value == nullptr) return none;
  if (!value->is_array()) throw std::invalid_argument(std::string(key) + " must be an array");
  return *value;
}

HkxRagdollEdits ragdoll_edits_of(const nlohmann::json& source) {
  if (!source.is_object()) throw std::invalid_argument("edits must be a JSON object");
  HkxRagdollEdits edits;
  for (const auto& row : ragdoll_rows(source, "bodies")) {
    HkxRagdollBodyEdit edit;
    edit.body = ragdoll_id(row, "body");
    edit.position = ragdoll_array<3>(row, "position");
    edit.rotation = ragdoll_array<4>(row, "rotation");
    edit.center_of_mass = ragdoll_array<3>(row, "center_of_mass");
    edit.mass = ragdoll_number(row, "mass");
    edits.bodies.push_back(edit);
  }
  for (const auto& row : ragdoll_rows(source, "constraints")) {
    HkxRagdollConstraintEdit edit;
    edit.constraint = ragdoll_id(row, "constraint");
    edit.body_a = ragdoll_optional_id(row, "body_a");
    edit.body_b = ragdoll_optional_id(row, "body_b");
    edit.twist_min_angle = ragdoll_number(row, "twist_min");
    edit.twist_max_angle = ragdoll_number(row, "twist_max");
    edit.cone_max_angle = ragdoll_number(row, "cone_max");
    edit.min_angle = ragdoll_number(row, "hinge_min");
    edit.max_angle = ragdoll_number(row, "hinge_max");
    edit.plane_min_angle = ragdoll_number(row, "plane_min");
    edit.plane_max_angle = ragdoll_number(row, "plane_max");
    edit.friction = ragdoll_number(row, "friction");
    edit.frame_a = ragdoll_optional_list(row, "frame_a");
    edit.frame_b = ragdoll_optional_list(row, "frame_b");
    edits.constraints.push_back(std::move(edit));
  }
  for (const auto& row : ragdoll_rows(source, "bone_map")) edits.bone_map.push_back({ragdoll_id(row, "bone"), ragdoll_id(row, "body")});
  for (const auto& row : ragdoll_rows(source, "shapes")) {
    HkxRagdollShapeEdit edit;
    edit.shape = ragdoll_id(row, "shape");
    edit.a = ragdoll_array<3>(row, "a");
    edit.b = ragdoll_array<3>(row, "b");
    edit.radius = ragdoll_number(row, "radius");
    edits.shapes.push_back(edit);
  }
  for (const auto& row : ragdoll_rows(source, "materials")) {
    HkxRagdollMaterialEdit edit;
    edit.material = ragdoll_id(row, "material");
    edit.dynamic_friction = ragdoll_number(row, "dynamic_friction");
    edit.static_friction = ragdoll_number(row, "static_friction");
    edit.restitution = ragdoll_number(row, "restitution");
    if (const auto* shared = ragdoll_member(row, "allow_shared")) {
      if (!shared->is_boolean()) throw std::invalid_argument("allow_shared must be true or false");
      edit.allow_shared = shared->get<bool>();
    }
    edits.materials.push_back(edit);
  }
  return edits;
}

}  // namespace

std::string parse_ragdoll_edits(const nlohmann::json& source, HkxRagdollEdits& out) {
  try {
    out = ragdoll_edits_of(source);
  } catch (const std::exception& error) {
    out = {};
    return error.what();
  }
  return {};
}

MapperRowDelta mapper_row_delta(const HkxSkeletonMapper& mapper, const HkxMapperSimpleRow& row) {
  MapperRowDelta out;
  auto derived = row.a_from_b;
  out.error = recompute_mapping_row(mapper.skeleton_a, row.bone_a, mapper.skeleton_b, row.bone_b, derived);
  if (!out.error.empty()) return out;
  const auto stored = row.a_from_b.translation();
  const auto fresh = derived.translation();
  float squared = 0.0f;
  for (std::size_t i = 0; i < 3; ++i) squared += (stored[i] - fresh[i]) * (stored[i] - fresh[i]);
  out.recomputed = derived;
  out.translation_delta = std::sqrt(squared);
  out.rotation_delta = rotation_gap(row.a_from_b.rotation(), derived.rotation());
  return out;
}

CharacterTransform skeleton_transform(std::vector<HkxSkeletonEdit> edits) {
  return [edits = std::move(edits)](std::span<const std::byte> in, std::vector<std::byte>& out) -> std::string {
    out.clear();
    HkxSkeletonWrite written;
    const auto status = write_skeleton(in, edits, written);
    if (status != HkxStatus::Ok) {
      return written.refusal.empty() ? "the skeleton writer failed: " + status_name(status) : written.refusal;
    }
    if (written.changed) out = std::move(written.bytes);
    return {};
  };
}

CharacterTransform ragdoll_transform(HkxRagdollEdits edits) {
  return [edits = std::move(edits)](std::span<const std::byte> in, std::vector<std::byte>& out) -> std::string {
    out.clear();
    auto applied = apply_ragdoll_edits(in, edits);
    if (applied.status != HkxRagdollEditStatus::Ok) {
      return std::string(ragdoll_edit_status_name(applied.status)) + ": " + applied.message;
    }
    if (!std::equal(applied.bytes.begin(), applied.bytes.end(), in.begin(), in.end())) out = std::move(applied.bytes);
    return {};
  };
}

CharacterTransform skin_transform(std::vector<nif::SkinEditRequest> edits) {
  return [edits = std::move(edits)](std::span<const std::byte> in, std::vector<std::byte>& out) -> std::string {
    out.clear();
    if (edits.empty()) return "there are no skin edits";
    try {
      std::vector<std::byte> current(in.begin(), in.end());
      for (const auto& request : edits) current = nif::apply_skin_edit(nif::read_nif(current), request);
      if (!std::equal(current.begin(), current.end(), in.begin(), in.end())) out = std::move(current);
    } catch (const parse_error& error) {
      return error.parse_message();
    } catch (const std::exception& error) {
      return error.what();
    }
    return {};
  };
}

CharacterTransform mapper_transform(std::size_t mapper, std::function<std::string(HkxSkeletonMapper&)> apply) {
  return [mapper, apply = std::move(apply)](std::span<const std::byte> in, std::vector<std::byte>& out) -> std::string {
    out.clear();
    HkxPackfile packfile;
    if (const auto status = read_packfile(in, packfile); status != HkxStatus::Ok) return "packfile read: " + status_name(status);
    std::vector<HkxSkeletonMapper> edited;
    if (const auto status = read_skeleton_mappers(packfile, edited); status != HkxStatus::Ok) {
      return "skeleton mapper read: " + status_name(status);
    }
    if (mapper >= edited.size()) {
      return "file holds " + std::to_string(edited.size()) + " mapper(s), so there is no mapper " + std::to_string(mapper);
    }
    if (auto why = apply(edited[mapper]); !why.empty()) return why;
    std::vector<std::byte> produced;
    std::string refusal;
    if (const auto status = write_skeleton_mappers(in, edited, produced, &refusal); status != HkxStatus::Ok) {
      return "not writable: " + (refusal.empty() ? status_name(status) : refusal);
    }
    if (!std::equal(produced.begin(), produced.end(), in.begin(), in.end())) out = std::move(produced);
    return {};
  };
}

namespace {

struct Prepared {
  std::vector<std::byte> input;
  std::vector<std::byte> output;
  bool changed{false};
  bool write_needed{false};
  bool replace{false};
};

// Constraint: an existing .bak is kept, so it holds the first original; a file that cannot be copied refuses the replace.
std::string keep_backup(const std::string& target, std::vector<std::string>& made) {
  const std::filesystem::path file(target);
  std::error_code ec;
  if (!std::filesystem::is_regular_file(file, ec)) return {};
  const std::string name = target + ".bak";
  const std::filesystem::path backup(name);
  if (std::filesystem::exists(backup, ec)) {
    return std::filesystem::is_regular_file(backup, ec) ? std::string{} : name + " exists and is not a file, so no backup could be kept";
  }
  if (!std::filesystem::copy_file(file, backup, ec) || ec) {
    const std::string why = ec.message();
    std::filesystem::remove(backup, ec);
    return "no backup could be kept at " + name + ": " + why;
  }
  made.push_back(name);
  return {};
}

CharacterSaveResult refuse(CharacterSaveResult result, std::string message) {
  result.saved = false;
  result.refusal = std::move(message);
  return result;
}

}  // namespace

CharacterSaveResult save_character(const CharacterSaveRequest& request) {
  CharacterSaveResult result;
  const auto& stages = request.stages;
  if (stages.empty()) return refuse(std::move(result), "there is nothing to save: no document was given");

  std::vector<std::vector<std::byte>> sources(stages.size());
  CharacterSaveInput input;
  for (std::size_t i = 0; i < stages.size(); ++i) {
    input.documents.push_back({stages[i].kind, stages[i].source, stages[i].target});
    if (!stages[i].transform) return refuse(std::move(result), step_label(i, stages[i].kind) + " has no edits to apply");
    if (stages[i].source.empty()) continue;
    auto bytes = read_file(stages[i].source);
    if (!bytes) return refuse(std::move(result), "cannot read " + stages[i].source);
    sources[i] = std::move(*bytes);
    if (!stages[i].expected_source_sha256.empty() && io::sha256_hex(sources[i]) != stages[i].expected_source_sha256) {
      return refuse(std::move(result), step_label(i, stages[i].kind) + ": " + stages[i].source +
                                           " changed on disk since it was opened; open it again before saving");
    }
    if (stages[i].kind == CharacterDocumentKind::skeleton && input.skeleton.empty()) input.skeleton = sources[i];
    if (stages[i].kind == CharacterDocumentKind::ragdoll && input.ragdoll.empty()) input.ragdoll = sources[i];
    if (stages[i].kind == CharacterDocumentKind::mapper && input.mapper.empty()) input.mapper = sources[i];
  }
  input.skeleton_edits = request.skeleton_edits;
  input.overwrite = request.overwrite;
  result.plan = plan_character_save(input);
  if (!result.plan.ok()) {
    std::string message = "the plan was refused";
    for (const auto& item : result.plan.findings) {
      if (item.level == CharacterLevel::error) {
        message += ": " + item.where + " " + item.message;
        break;
      }
    }
    return refuse(std::move(result), std::move(message));
  }

  std::vector<Prepared> prepared(result.plan.steps.size());
  std::map<std::string, std::vector<std::byte>> latest;
  for (std::size_t n = 0; n < result.plan.steps.size(); ++n) {
    const auto& step = result.plan.steps[n];
    const auto& stage = stages[step.document];
    const auto label = step_label(n, step.kind);
    auto& entry = prepared[n];
    if (step.reads_previous_output) {
      const auto found = latest.find(character_path_key(step.reads));
      if (found == latest.end()) return refuse(std::move(result), label + " reads " + step.reads + ", which no earlier step wrote");
      entry.input = found->second;
    } else {
      auto bytes = read_file(step.reads);
      if (!bytes) return refuse(std::move(result), label + ": cannot read " + step.reads);
      entry.input = std::move(*bytes);
      if (!stage.expected_source_sha256.empty() && io::sha256_hex(entry.input) != stage.expected_source_sha256) {
        return refuse(std::move(result), label + ": " + step.reads + " changed on disk since it was opened; open it again before saving");
      }
    }
    if (auto why = stage.transform(entry.input, entry.output); !why.empty()) {
      return refuse(std::move(result), label + " refused: " + why);
    }
    entry.changed = !entry.output.empty();
    if (!entry.changed) entry.output = entry.input;
    if (entry.changed) {
      if (auto why = verify_output(step.kind, entry.output); !why.empty()) {
        return refuse(std::move(result), label + " would write a file that does not read back: " + why);
      }
    }
    const bool in_place = character_same_file(step.writes, step.reads);
    entry.write_needed = entry.changed || !in_place;
    entry.replace = request.overwrite;
    if (entry.write_needed) {
      if (request.target_policy) {
        if (auto why = request.target_policy(step.writes, step.kind); !why.empty()) return refuse(std::move(result), label + ": " + why);
      }
      std::error_code ec;
      if (!entry.replace && std::filesystem::exists(step.writes, ec)) {
        return refuse(std::move(result), label + ": " + step.writes + " already exists; choose another file or allow overwrite");
      }
    }
    latest[character_path_key(step.writes)] = entry.output;
    CharacterSaveStepResult row;
    row.stage = step.document;
    row.kind = step.kind;
    row.reads = step.reads;
    row.writes = step.writes;
    row.changed = entry.changed;
    row.bytes = entry.output.size();
    row.source_sha256 = io::sha256_hex(entry.input);
    row.output_sha256 = io::sha256_hex(entry.output);
    result.steps.push_back(std::move(row));
  }

  const auto fail = [&](std::size_t n, const std::string& why) {
    result.saved = false;
    result.partial = !result.written.empty();
    result.failed_stage = result.plan.steps[n].document;
    result.refusal = step_label(n, result.plan.steps[n].kind) + " failed: " + why;
    if (result.written.empty()) {
      result.refusal += "; nothing was written";
    } else {
      result.refusal += "; already written:";
      for (const auto& path : result.written) result.refusal += " " + path;
    }
    return result;
  };

  for (std::size_t n = 0; n < result.plan.steps.size(); ++n) {
    if (!prepared[n].write_needed || !prepared[n].replace) continue;
    if (auto why = keep_backup(result.plan.steps[n].writes, result.backups); !why.empty()) {
      return refuse(std::move(result), step_label(n, result.plan.steps[n].kind) + ": " + why + "; nothing was written");
    }
  }

  for (std::size_t n = 0; n < result.plan.steps.size(); ++n) {
    const auto& step = result.plan.steps[n];
    const auto& entry = prepared[n];
    const auto current = read_file(step.reads);
    if (!current || !std::equal(current->begin(), current->end(), entry.input.begin(), entry.input.end())) {
      return fail(n, step.reads + " changed on disk while the save was running");
    }
    if (!entry.write_needed) continue;
    try {
      auto staged = io::StagedFile::create(std::filesystem::path(step.writes));
      staged.write(entry.output);
      staged.finish();
      staged.verify(entry.output.size());
      if (entry.replace) {
        std::move(staged).commit();
      } else if (!std::move(staged).commit_if_absent()) {
        return fail(n, step.writes + " appeared after the check and was not replaced");
      }
    } catch (const std::exception& error) {
      return fail(n, std::string("not saved: ") + error.what());
    }
    result.written.push_back(step.writes);
    result.steps[n].written = true;
    const auto reread = read_file(step.writes);
    if (!reread || !std::equal(reread->begin(), reread->end(), entry.output.begin(), entry.output.end())) {
      return fail(n, step.writes + " does not read back as what was written");
    }
  }
  result.saved = true;
  return result;
}

}  // namespace fo4::hkx
