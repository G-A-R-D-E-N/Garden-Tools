#include "fo4/previs/precombine_plan.hpp"

#include <algorithm>
#include <cctype>
#include <map>

namespace fo4::previs {
namespace {

std::string normalize_path_key(std::string path) {
  std::ranges::transform(path, path.begin(), [](unsigned char ch) {
    return static_cast<char>(std::tolower(ch));
  });
  std::ranges::replace(path, '/', '\\');
  return path;
}

bool is_stat_group(const fo4::env::CellReferenceListing& row) {
  if (row.initially_disabled) return false;
  if (!row.base_info.has_value()) return false;
  if (row.base_info->editor_marker) return false;
  if (row.base_info->model_path.empty()) return false;
  return row.base_info->signature == fo4::Signature::from_string("STAT");
}

bool has_subrecord(const Record& record, Signature signature) {
  return std::ranges::any_of(record.subrecords, [&](const Subrecord& sub) { return sub.signature == signature; });
}

// Invariant: the first matching reason wins, so each refused reference is counted once.
bool refused_reference(const env::Environment& env, const FormKey& reference, PrecombineSkipCounts& skipped) {
  const auto* ref = env.winner(reference);
  if (ref == nullptr || ref->record == nullptr || ref->record->header.flags.is_deleted()) return true;
  if (has_subrecord(*ref->record, Signature::from_string("VMAD"))) {
    ++skipped.scripted;
    return true;
  }
  if (has_subrecord(*ref->record, Signature::from_string("XESP"))) {
    ++skipped.enable_parent;
    return true;
  }
  return false;
}

std::string plugin_name_at(const fo4::env::Environment& env, std::size_t plugin_index) {
  if (plugin_index >= env.plugins().size()) return {};
  return std::string(env.plugins()[plugin_index].key.filename());
}

}

std::optional<PrecombinePlan> build_precombine_plan(const env::Environment& env,
                                                      const FormKey& cell,
                                                      std::string_view plugin_filename,
                                                      std::size_t min_instances,
                                                      const PrecombineLodTargets& lod_targets) {
  if (min_instances < 1) min_instances = 1;
  const auto summary = env::describe_cell(env, cell);
  if (!summary) return std::nullopt;
  // Constraint: exterior cells plan their own temporary children, so only an unknown cell is refused here.

  PrecombinePlan out;
  out.cell = cell;
  out.plugin = std::string(plugin_filename);
  out.interior = summary->interior;

  const auto listing = env::list_cell_references(env, cell);
  const auto own_plugin_key = normalize_path_key(std::string(plugin_filename));
  std::map<std::string, std::pair<std::string, std::vector<PrecombinePlanInstance>>, std::less<>> groups;

  for (const auto& row : listing.references) {
    if (row.persistent) continue;
    ++out.temporary_considered;
    if (!is_stat_group(row)) continue;
    if (!row.base.has_value()) continue;
    const auto owner = normalize_path_key(plugin_name_at(env, row.winning_plugin));
    if (owner.empty() || owner != own_plugin_key) {
      if (owner.empty()) ++out.skipped.unresolved_plugin;
      else ++out.skipped.foreign_plugin;
      continue;
    }
    if (refused_reference(env, row.key, out.skipped)) continue;

    PrecombinePlanInstance inst;
    inst.reference = row.key;
    // Constraint: the base object's own NonOccluder bit decides it, so a NonOccluder object is never an occluder box.
    if (const auto* base = env.winner(*row.base)) {
      inst.occludes = !base->record->header.flags.test(RecordFlags::non_occluder);
    }
    if (!row.editor_id.empty()) inst.editor_id = row.editor_id;
    inst.transform.x = row.transform.x;
    inst.transform.y = row.transform.y;
    inst.transform.z = row.transform.z;
    inst.transform.rx = row.transform.rx;
    inst.transform.ry = row.transform.ry;
    inst.transform.rz = row.transform.rz;
    inst.scale = row.scale;
    const auto key = normalize_path_key(row.base_info->model_path);
    auto& bucket = groups[key];
    if (bucket.first.empty()) bucket.first = row.base_info->model_path;
    bucket.second.push_back(std::move(inst));
  }

  for (auto& [key, bucket] : groups) {
    if (bucket.second.size() < min_instances) continue;
    std::ranges::sort(bucket.second, [](const PrecombinePlanInstance& left, const PrecombinePlanInstance& right) {
      return left.reference.local() < right.reference.local();
    });
    out.groups.push_back(PrecombineModelGroup{
        .model_path = bucket.first, .instances = std::move(bucket.second), .lod_target = std::nullopt});
  }
  std::ranges::sort(out.groups, [](const PrecombineModelGroup& left, const PrecombineModelGroup& right) {
    return left.model_path < right.model_path;
  });
  attach_lod_targets(out, lod_targets);
  return out;
}

void attach_lod_targets(PrecombinePlan& plan, const PrecombineLodTargets& lod_targets) {
  PrecombineLodTargets normalized;
  for (const auto& [path, target] : lod_targets) {
    normalized[normalize_path_key(path)] = target;
  }
  plan.lod_targets = normalized;
  for (auto& group : plan.groups) {
    // Constraint: a stale pin must not survive a re-attach, so every group starts without one.
    group.lod_target.reset();
    if (const auto pinned = normalized.find(normalize_path_key(group.model_path)); pinned != normalized.end()) {
      group.lod_target = pinned->second;
    }
  }
}

PrecombineLodTargets lod_targets_from_json(const nlohmann::json& document) {
  PrecombineLodTargets targets;
  if (!document.is_object()) return targets;
  for (const auto& [path, entry] : document.items()) {
    if (path.empty()) continue;
    float level1 = 0.0f;
    float level2 = 0.0f;
    if (entry.is_array() && entry.size() == 2) {
      // Constraint: a pair is [level1, level2] triangle fractions, so a non-numeric member cannot pin a level.
      if (entry[0].is_number()) level1 = entry[0].get<float>();
      if (entry[1].is_number()) level2 = entry[1].get<float>();
    } else if (entry.is_object()) {
      if (entry.contains("level1") && entry["level1"].is_number()) level1 = entry["level1"].get<float>();
      if (entry.contains("level2") && entry["level2"].is_number()) level2 = entry["level2"].get<float>();
    } else {
      continue;
    }
    targets[path] = PrecombineLodTarget{level1, level2};
  }
  return targets;
}

XcriRaw xcri_from_plan(const PrecombinePlan& plan,
                       std::span<const std::uint32_t> oc_ids,
                       const PluginFormIdContext& dest) {
  XcriRaw out;
  if (oc_ids.size() != plan.groups.size()) return out;
  out.mesh_ids.assign(oc_ids.begin(), oc_ids.end());
  for (std::size_t group = 0; group < plan.groups.size(); ++group) {
    for (const auto& instance : plan.groups[group].instances) {
      out.refs.emplace_back(encode_plugin_form_id(instance.reference, dest).value(), oc_ids[group]);
    }
  }
  std::ranges::sort(out.refs, [](const auto& left, const auto& right) {
    return left.first < right.first;
  });
  return out;
}

XcriRaw xcri_from_group_ids(const PrecombinePlan& plan,
                           std::span<const std::uint32_t> group_ids,
                           const PluginFormIdContext& dest) {
  XcriRaw out;
  if (group_ids.size() != plan.groups.size()) return out;
  for (std::size_t group = 0; group < plan.groups.size(); ++group) {
    for (const auto& instance : plan.groups[group].instances) {
      out.refs.emplace_back(encode_plugin_form_id(instance.reference, dest).value(), group_ids[group]);
    }
  }
  std::ranges::sort(out.refs, [](const auto& left, const auto& right) {
    return left.first < right.first;
  });
  out.mesh_ids.assign(group_ids.begin(), group_ids.end());
  std::ranges::sort(out.mesh_ids);
  out.mesh_ids.erase(std::unique(out.mesh_ids.begin(), out.mesh_ids.end()), out.mesh_ids.end());
  return out;
}

PrecombineMaterialOrder order_plan_by_material(
    const PrecombinePlan& plan,
    const std::map<std::string, std::string, std::less<>>& material_by_model) {
  PrecombineMaterialOrder out;
  out.plan = plan;
  out.plan.groups.clear();
  std::vector<std::size_t> order(plan.groups.size());
  for (std::size_t i = 0; i < order.size(); ++i) order[i] = i;
  const auto material_of = [&](std::size_t index) {
    const auto found = material_by_model.find(plan.groups[index].model_path);
    return found == material_by_model.end() ? std::string{} : found->second;
  };
  std::ranges::stable_sort(order, [&](std::size_t left, std::size_t right) {
    const auto left_material = material_of(left);
    const auto right_material = material_of(right);
    if (left_material != right_material) return left_material < right_material;
    return plan.groups[left].model_path < plan.groups[right].model_path;
  });
  out.plan.groups.reserve(plan.groups.size());
  out.materials.reserve(plan.groups.size());
  for (const auto index : order) {
    out.plan.groups.push_back(plan.groups[index]);
    out.materials.push_back(material_of(index));
  }
  return out;
}

}
