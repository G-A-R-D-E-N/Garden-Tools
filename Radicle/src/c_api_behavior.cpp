#include "c_api_env_internal.hpp"
#include "fo4/c_api_behavior.h"
#include "fo4/environment/behavior.hpp"
#include "fo4/environment/package_procedure.hpp"
#include "fo4/text/encoding.hpp"

#include <deque>
#include <memory>
#include <string>
#include <vector>

namespace {

using namespace radicle_c_detail;
using namespace radicle_c_env_detail;

struct ConditionStorage {
  std::vector<radicle_behavior_condition> rows;
  std::deque<std::string> strings;
  const char* text(const std::optional<std::string>& value) {
    if (!value) return nullptr;
    strings.push_back(fo4::text::windows1252_to_utf8(*value)); return strings.back().c_str();
  }
  void load(const std::vector<fo4::CtdaCondition>& source) {
    for (const auto& c : source) rows.push_back({c.flags_byte, c.comparison_raw, c.function, c.unknown2,
        c.param1, c.param2, c.run_on, c.reference, c.unknown3, text(c.parameter1_string), text(c.parameter2_string)});
  }
};
struct PackageHolder {
  radicle_package_info view{};
  ConditionStorage conditions;
  std::vector<radicle_package_payload> opaque;
  std::vector<radicle_package_location> locations;
  std::vector<radicle_package_target> targets;
  std::vector<radicle_form_key> idle_animations;
};
struct IdleHolder { radicle_idle_info view{}; ConditionStorage conditions; };
struct ProcedureTreeHolder {
  radicle_package_procedure_tree view{};
  std::deque<std::string> names;
  std::vector<radicle_package_procedure_input> inputs;
  std::vector<radicle_package_procedure_node> nodes;
  std::vector<std::vector<std::size_t>> children;
};

radicle_status invalid() { return fail(RADICLE_STATUS_INVALID_ARGUMENT, "env, record and output required"); }

}

extern "C" {

radicle_status radicle_package_decode(const radicle_env* env, const radicle_record* record,
                                       const radicle_package_info** out_package) {
  clear_error();
  if (!env || !record || !out_package) return invalid();
  if (!belongs_to_env(env, to_ref(record))) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "record does not belong to this environment");
  }
  *out_package = nullptr;
  return guarded("unknown exception while decoding PACK", [&] {
    const auto p = fo4::env::decode_package(env->value, *to_ref(record)); auto h = std::make_unique<PackageHolder>(); auto& v = h->view;
    if (p.data) {
      v.has_data = 1; v.flags = p.data->flags; v.type = p.data->type; v.interrupt_override = p.data->interrupt_override;
      v.preferred_speed = p.data->preferred_speed; v.unknown = p.data->unknown; v.interrupt_flags = p.data->interrupt_flags;
      v.trailing_value = p.data->trailing_value;
    }
    if (p.schedule) {
      v.has_schedule = 1; v.month = p.schedule->month; v.weekday = p.schedule->weekday; v.date = p.schedule->date;
      v.hour = p.schedule->hour; v.minute = p.schedule->minute; v.duration_minutes = p.schedule->duration_minutes;
    }
    if (p.counter) {
      v.has_counter = 1; v.input_count = p.counter->input_count; v.version = p.counter->version;
      v.template_package = to_c_key(*env, p.counter->template_package);
    }
    v.combat_style = to_c_key(*env, p.combat_style); v.owner_quest = to_c_key(*env, p.owner_quest);
    v.begin_idle = to_c_key(*env, p.begin_idle); v.end_idle = to_c_key(*env, p.end_idle); v.change_idle = to_c_key(*env, p.change_idle);
    h->conditions.load(p.conditions); v.conditions = h->conditions.rows.data(); v.condition_count = h->conditions.rows.size();
    for (const auto& sub : p.opaque) {
      radicle_package_payload raw{}; const auto name = sub.signature.to_string(); std::copy(name.begin(), name.end(), raw.signature);
      raw.bytes = reinterpret_cast<const std::uint8_t*>(sub.data.data()); raw.size = sub.data.size(); h->opaque.push_back(raw);
    }
    for (const auto& location : p.locations) h->locations.push_back({location.subrecord_index, location.type,
        location.value, location.radius.has_value(), location.radius.value_or(0),
        location.item_index.has_value(), location.item_index.value_or(0), to_c_key(*env, location.form)});
    for (const auto& target : p.targets) h->targets.push_back({target.subrecord_index, target.type,
        target.value, target.count_or_distance, to_c_key(*env, target.form)});
    for (const auto& animation : p.idle_animations) h->idle_animations.push_back(to_c_key(*env, animation));
    v.locations = h->locations.data(); v.location_count = h->locations.size();
    v.targets = h->targets.data(); v.target_count = h->targets.size();
    v.idle_animations = h->idle_animations.data(); v.idle_animation_count = h->idle_animations.size();
    v.opaque = h->opaque.data(); v.opaque_count = h->opaque.size(); v.internal = h.get();
    *out_package = &h.release()->view; return RADICLE_STATUS_OK;
  });
}

void radicle_package_free(const radicle_package_info* package) {
  if (package) delete static_cast<PackageHolder*>(package->internal);
}

radicle_status radicle_combat_style_get_info(const radicle_env* env, const radicle_record* record,
                                             radicle_combat_style_info* out_info) {
  clear_error(); if (!env || !record || !out_info) return invalid();
  if (!belongs_to_env(env, to_ref(record))) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "record does not belong to this environment");
  }
  return guarded("unknown exception while decoding CSTY", [&] {
    const auto c = fo4::env::decode_combat_style(env->value, *to_ref(record)); radicle_combat_style_info v{};
    if (c.general) { v.has_general = 1; std::copy(c.general->begin(), c.general->end(), v.general); }
    v.melee_count = c.melee.size(); std::copy(c.melee.begin(), c.melee.end(), v.melee);
    v.long_range_count = c.long_range.size(); std::copy(c.long_range.begin(), c.long_range.end(), v.long_range);
    if (c.close_range) {
      v.has_close_range = 1; std::copy(c.close_range->values.begin(), c.close_range->values.end(), v.close_range);
      v.throw_max_targets = c.close_range->throw_max_targets; v.flank_variance = c.close_range->flank_variance;
    }
    if (c.flight) { v.has_flight = 1; std::copy(c.flight->begin(), c.flight->end(), v.flight); }
    set_optional(c.ranged_accuracy, v.has_ranged_accuracy, v.ranged_accuracy);
    set_optional(c.cover_search, v.has_cover_search, v.cover_search); set_optional(c.flags, v.has_flags, v.flags);
    *out_info = v; return RADICLE_STATUS_OK;
  });
}

radicle_status radicle_idle_decode(const radicle_env* env, const radicle_record* record, const radicle_idle_info** out_idle) {
  clear_error();
  if (!env || !record || !out_idle) return invalid();
  if (!belongs_to_env(env, to_ref(record))) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "record does not belong to this environment");
  }
  *out_idle = nullptr;
  return guarded("unknown exception while decoding IDLE", [&] {
    const auto idle = fo4::env::decode_idle(env->value, *to_ref(record)); auto h = std::make_unique<IdleHolder>(); auto& v = h->view;
    if (idle.data) {
      v.has_data = 1; v.loop_min = idle.data->loop_min; v.loop_max = idle.data->loop_max; v.flags = idle.data->flags;
      v.group_section = idle.data->group_section; v.replay_delay = idle.data->replay_delay;
    }
    v.parent = to_c_key(*env, idle.parent); v.previous = to_c_key(*env, idle.previous);
    v.behavior_graph = stored_text(*env, idle.behavior_graph); v.animation_event = stored_text(*env, idle.animation_event);
    v.animation_file = stored_text(*env, idle.animation_file); h->conditions.load(idle.conditions);
    v.conditions = h->conditions.rows.data(); v.condition_count = h->conditions.rows.size(); v.internal = h.get();
    *out_idle = &h.release()->view; return RADICLE_STATUS_OK;
  });
}

void radicle_idle_free(const radicle_idle_info* idle) {
  if (idle) delete static_cast<IdleHolder*>(idle->internal);
}

radicle_status radicle_package_procedure_decode(const radicle_env* env, const radicle_record* record,
                                                const radicle_package_procedure_tree** out_tree) {
  clear_error();
  if (!env || !record || !out_tree) return invalid();
  if (!belongs_to_env(env, to_ref(record))) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "record does not belong to this environment");
  }
  *out_tree = nullptr;
  return guarded("unknown exception while decoding PACK procedure tree", [&] {
    const auto t = fo4::env::decode_package_procedure(*to_ref(record)->record);
    auto h = std::make_unique<ProcedureTreeHolder>(); auto& v = h->view;
    v.decoded = t.decoded; v.region_begin = t.region_begin; v.region_end = t.region_end;
    if (t.marker) { v.has_marker = 1; v.marker = *t.marker; }
    for (const auto& input : t.inputs) {
      h->names.push_back(input.type_name);
      h->inputs.push_back({h->names.back().c_str(), input.subrecord_begin, input.subrecord_end, input.index});
    }
    for (const auto& node : t.nodes) {
      h->names.push_back(node.name);
      h->nodes.push_back({node.kind == fo4::env::PackageProcedureKind::leaf, h->names.back().c_str(), node.condition_count,
          node.child_count, node.flags.has_value(), node.flags.value_or(0), node.opaque_begin, node.opaque_end, nullptr,
          node.children.size()});
      h->children.push_back(node.children);
    }
    for (std::size_t i = 0; i < h->nodes.size(); ++i) h->nodes[i].children = h->children[i].data();
    v.inputs = h->inputs.data(); v.input_count = h->inputs.size();
    v.nodes = h->nodes.data(); v.node_count = h->nodes.size(); v.internal = h.get();
    *out_tree = &h.release()->view; return RADICLE_STATUS_OK;
  });
}

void radicle_package_procedure_free(const radicle_package_procedure_tree* tree) {
  if (tree) delete static_cast<ProcedureTreeHolder*>(tree->internal);
}

const char* radicle_package_location_type_name(int32_t type) {
  if (type < 0 || type > static_cast<int32_t>(fo4::env::PackageLocationType::alias_collection_item)) return nullptr;
  return fo4::env::package_location_type_name(static_cast<fo4::env::PackageLocationType>(type));
}

const char* radicle_package_target_type_name(int32_t type) {
  if (type < 0 || type > static_cast<int32_t>(fo4::env::PackageTargetType::alias_collection_item)) return nullptr;
  return fo4::env::package_target_type_name(static_cast<fo4::env::PackageTargetType>(type));
}

}
