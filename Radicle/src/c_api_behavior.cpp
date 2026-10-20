#include "c_api_env_internal.hpp"
#include "fo4/c_api_behavior.h"
#include "fo4/environment/behavior.hpp"
#include "fo4/environment/package_procedure.hpp"
#include "fo4/text/encoding.hpp"

#include <cstring>
#include <deque>
#include <memory>
#include <optional>
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
  *out_tree = nullptr;
  if (!belongs_to_env(env, to_ref(record))) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "record does not belong to this environment");
  }
  if (to_ref(record)->record->header.signature != fo4::Signature::from_string("PACK")) {
    return fail(RADICLE_STATUS_PARSE_ERROR, "expected a PACK record");
  }
  return guarded("unknown exception while decoding PACK procedure tree", [&] {
    const auto t = fo4::env::decode_package_procedure(*to_ref(record)->record);
    auto h = std::make_unique<ProcedureTreeHolder>(); auto& v = h->view;
    v.decoded = t.decoded; v.region_begin = t.region_begin; v.region_end = t.region_end;
    if (t.next_input_id) { v.has_next_input_id = 1; v.next_input_id = *t.next_input_id; }
    for (const auto& input : t.inputs) {
      h->names.push_back(input.type_name);
      const char* type_name = h->names.back().c_str();
      h->names.push_back(input.value_tag.to_string());
      h->inputs.push_back({type_name, input.subrecord_begin, input.subrecord_end, input.index,
          h->names.back().c_str(), input.value_index,
          static_cast<radicle_package_input_value_kind>(input.kind), input.boolean ? 1 : 0, input.integer,
          input.integer_raw, input.real});
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

extern "C++" {
namespace {

std::optional<fo4::env::PackageInputValue> to_input_value(const radicle_package_input_value& value) {
  using Kind = fo4::env::PackageInputValueKind;
  const auto kind = static_cast<int>(value.kind);
  if (kind < RADICLE_PACKAGE_INPUT_NONE || kind > RADICLE_PACKAGE_INPUT_TOPIC) return std::nullopt;
  fo4::env::PackageInputValue out;
  out.kind = static_cast<Kind>(value.kind);
  out.boolean = value.boolean != 0;
  out.integer = value.integer;
  out.real = value.real;
  if (value.payload != nullptr && value.payload_size > 0) {
    out.payload.resize(value.payload_size);
    std::memcpy(out.payload.data(), value.payload, value.payload_size);
  }
  return out;
}

radicle_status package_input_failure(fo4::env::PackageInputMutationStatus status) {
  using Status = fo4::env::PackageInputMutationStatus;
  switch (status) {
    case Status::updated:
    case Status::unchanged:
      return RADICLE_STATUS_OK;
    case Status::record_missing:
      return fail(RADICLE_STATUS_NOT_FOUND, "plugin has no such record");
    case Status::input_id_missing:
      return fail(RADICLE_STATUS_NOT_FOUND, "package has no data input with that id");
    case Status::not_a_package:
      return fail(RADICLE_STATUS_PARSE_ERROR, "expected a PACK record");
    case Status::undecodable:
      return fail(RADICLE_STATUS_PARSE_ERROR, "package data inputs do not reconcile");
    case Status::unknown_input_type:
      return fail(RADICLE_STATUS_INVALID_ARGUMENT, "type name is not a registered data input type");
    case Status::duplicate_input_id:
      return fail(RADICLE_STATUS_INVALID_ARGUMENT, "package stores duplicate data input ids");
    case Status::kind_mismatch:
      return fail(RADICLE_STATUS_INVALID_ARGUMENT, "value kind does not match the data input type");
    case Status::payload_size_invalid:
      return fail(RADICLE_STATUS_INVALID_ARGUMENT, "payload size is not a stored layout size for this type");
    case Status::payload_layout_undetermined:
      return fail(RADICLE_STATUS_INVALID_ARGUMENT, "payload layout is not writable for this type");
    case Status::payload_tag_mismatch:
      return fail(RADICLE_STATUS_INVALID_ARGUMENT, "payload subrecord does not match the stored value subrecord");
    case Status::procedure_tree_present:
      return fail(RADICLE_STATUS_INVALID_ARGUMENT, "package has an unlinked procedure tree, so removal is refused");
    case Status::no_free_input_id:
      return fail(RADICLE_STATUS_INVALID_ARGUMENT, "no free data input id remains");
  }
  return fail(RADICLE_STATUS_INTERNAL_ERROR, "unknown package input mutation status");
}

}
}

radicle_status radicle_package_input_add(radicle_plugin* plugin, radicle_form_key key, const char* type_name,
                                         const radicle_package_input_value* value) {
  clear_error();
  const auto form_key = to_form_key(key);
  if (plugin == nullptr || !form_key || type_name == nullptr || value == nullptr) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "plugin, a valid record key, a type name and a value are required");
  }
  const auto converted = to_input_value(*value);
  if (!converted) return fail(RADICLE_STATUS_INVALID_ARGUMENT, "unknown data input value kind");
  return guarded("unknown exception while adding a PACK data input", [&] {
    return package_input_failure(fo4::env::add_package_data_input(plugin->value, *form_key, type_name, *converted));
  });
}

radicle_status radicle_package_input_set(radicle_plugin* plugin, radicle_form_key key, uint8_t id,
                                         const radicle_package_input_value* value) {
  clear_error();
  const auto form_key = to_form_key(key);
  if (plugin == nullptr || !form_key || value == nullptr) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "plugin, a valid record key and a value are required");
  }
  const auto converted = to_input_value(*value);
  if (!converted) return fail(RADICLE_STATUS_INVALID_ARGUMENT, "unknown data input value kind");
  return guarded("unknown exception while setting a PACK data input", [&] {
    return package_input_failure(fo4::env::set_package_data_input(plugin->value, *form_key, id, *converted));
  });
}

radicle_status radicle_package_input_remove(radicle_plugin* plugin, radicle_form_key key, uint8_t id) {
  clear_error();
  const auto form_key = to_form_key(key);
  if (plugin == nullptr || !form_key) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "plugin and a valid record key are required");
  }
  return guarded("unknown exception while removing a PACK data input", [&] {
    return package_input_failure(fo4::env::remove_package_data_input(plugin->value, *form_key, id));
  });
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
