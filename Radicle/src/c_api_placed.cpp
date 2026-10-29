#include "c_api_env_internal.hpp"
#include "fo4/environment/placed.hpp"

#include <memory>
#include <optional>
#include <vector>

namespace {

using namespace radicle_c_detail;
using namespace radicle_c_env_detail;

struct PlacedHolder {
  radicle_placed_ref view{};
  fo4::env::PlacedReferenceData data;
  std::vector<radicle_form_key> location_ref_types;
  std::vector<radicle_linked_ref> linked_refs;
  std::vector<radicle_activate_parent> activate_parents;
  std::vector<radicle_portal> portals;
  std::vector<radicle_form_key> linked_rooms;
  std::vector<std::vector<radicle_topic_ref>> topics;
  std::vector<radicle_patrol> patrols;
  std::vector<std::vector<radicle_script_property>> properties;
  std::vector<std::vector<radicle_script_element>> elements;
  std::vector<std::vector<radicle_script_struct>> structs;
  std::vector<std::vector<radicle_script_struct_member>> struct_members;
  std::vector<radicle_script> scripts;
};

void copy3(const fo4::env::Vec3& in, float* out) {
  for (std::size_t i = 0; i < 3; ++i) out[i] = in[i];
}

template <class T>
const T* data_or_null(const std::vector<T>& values) {
  return values.empty() ? nullptr : values.data();
}

void build_view(const radicle_env& env, PlacedHolder& holder) {
  if (holder.data.scripts) {
    for (auto& script : holder.data.scripts->raw.scripts) {
      script.name = fo4::text::windows1252_to_utf8(script.name);
      for (auto& property : script.properties) {
        property.name = fo4::text::windows1252_to_utf8(property.name);
        if (property.struct_elements) {
          for (auto& structure : *property.struct_elements) {
            for (auto& member : structure.members) {
              member.name = fo4::text::windows1252_to_utf8(member.name);
            }
          }
        }
      }
    }
  }
  const auto& d = holder.data;
  auto& v = holder.view;
  const auto key = [&](const std::optional<fo4::FormKey>& value) { return to_c_key(env, value); };

  const auto signature = d.signature.view();
  for (std::size_t i = 0; i < 4; ++i) v.signature[i] = signature[i];
  v.base = key(d.base);
  if (d.position && d.rotation) {
    v.has_placement = 1;
    copy3(*d.position, v.position);
    copy3(*d.rotation, v.rotation);
  }
  if (d.scale) {
    v.has_scale = 1;
    v.scale = *d.scale;
  }
  v.layer = key(d.layer);
  v.encounter_zone = key(d.encounter_zone);
  v.has_emittance = d.emittance ? 1 : 0;
  v.emittance = key(d.emittance);
  v.location_reference = key(d.location_reference);
  v.attach_ref = key(d.attach_ref);

  for (const auto& type : d.location_ref_types) holder.location_ref_types.push_back(key(type));
  v.location_ref_types = data_or_null(holder.location_ref_types);
  v.location_ref_type_count = holder.location_ref_types.size();

  if (d.enable_parent) {
    v.has_enable_parent = 1;
    v.enable_parent = key(d.enable_parent->reference);
    if (d.enable_parent->flags) {
      v.has_enable_parent_flags = 1;
      v.enable_parent_flags = *d.enable_parent->flags;
    }
  }
  if (d.ownership) {
    v.has_ownership = 1;
    v.owner = key(d.ownership->owner);
    if (d.ownership->no_crime) {
      v.has_no_crime = 1;
      v.no_crime = *d.ownership->no_crime ? 1 : 0;
    }
  }
  if (d.faction_rank) {
    v.has_faction_rank = 1;
    v.faction_rank = *d.faction_rank;
  }

  for (const auto& link : d.linked_references) {
    holder.linked_refs.push_back({key(link.keyword), key(link.reference)});
  }
  v.linked_refs = data_or_null(holder.linked_refs);
  v.linked_ref_count = holder.linked_refs.size();

  if (d.activate_parent_flags) {
    v.has_activate_parent_flags = 1;
    v.activate_parent_flags = *d.activate_parent_flags;
  }
  for (const auto& parent : d.activate_parents) {
    holder.activate_parents.push_back({key(parent.reference), parent.delay});
  }
  v.activate_parents = data_or_null(holder.activate_parents);
  v.activate_parent_count = holder.activate_parents.size();

  v.comments = stored_text(env, d.comments);
  v.ignored_by_sandbox = d.ignored_by_sandbox ? 1 : 0;
  v.open_by_default = d.open_by_default ? 1 : 0;

  if (d.map_marker) {
    const auto& marker = *d.map_marker;
    v.has_map_marker = 1;
    if (marker.flags) {
      v.has_map_marker_flags = 1;
      v.map_marker_flags = *marker.flags;
    }
    switch (marker.name.kind) {
      case fo4::env::NameKind::absent:
        v.map_marker_name_kind = RADICLE_NAME_ABSENT;
        break;
      case fo4::env::NameKind::inline_text:
        v.map_marker_name_kind = RADICLE_NAME_INLINE;
        v.map_marker_name_text = stored_text(env, marker.name.text);
        break;
      case fo4::env::NameKind::string_id:
        v.map_marker_name_kind = RADICLE_NAME_STRING_ID;
        v.map_marker_name_string_id = marker.name.string_id;
        break;
      case fo4::env::NameKind::malformed:
        v.map_marker_name_kind = RADICLE_NAME_MALFORMED;
        break;
    }
    if (marker.type) {
      v.has_map_marker_type = 1;
      v.map_marker_type = *marker.type;
    }
  }

  if (d.teleport) {
    v.has_teleport = 1;
    v.teleport_door = key(d.teleport->door);
    copy3(d.teleport->position, v.teleport_position);
    copy3(d.teleport->rotation, v.teleport_rotation);
    v.teleport_flags = d.teleport->flags;
    v.teleport_transition_interior = key(d.teleport->transition_interior);
  }
  if (d.primitive) {
    v.has_primitive = 1;
    copy3(d.primitive->bounds, v.primitive_bounds);
    for (std::size_t i = 0; i < 4; ++i) v.primitive_color[i] = d.primitive->color[i];
    v.primitive_type = d.primitive->type;
  }
  if (d.spline) {
    v.has_spline = 1;
    v.spline_slack = d.spline->slack;
    v.spline_thickness = d.spline->thickness;
    copy3(d.spline->half_extents, v.spline_half_extents);
    if (d.spline->wind_detached_end) {
      v.has_spline_wind_detached_end = 1;
      v.spline_wind_detached_end = *d.spline->wind_detached_end ? 1 : 0;
    }
  }

  for (const auto& portal : d.portals) {
    holder.portals.push_back({key(portal.origin), key(portal.destination)});
  }
  v.portals = data_or_null(holder.portals);
  v.portal_count = holder.portals.size();

  if (d.room_portal) {
    v.has_room_portal = 1;
    v.room_portal_size[0] = d.room_portal->width;
    v.room_portal_size[1] = d.room_portal->height;
    copy3(d.room_portal->position, v.room_portal_position);
    for (std::size_t i = 0; i < 4; ++i) v.room_portal_rotation[i] = d.room_portal->rotation[i];
  }
  if (d.room) {
    v.has_room = 1;
    v.room_linked_room_count = d.room->linked_room_count;
    v.room_flags = d.room->flags;
    v.room_lighting_template = key(d.room->lighting_template);
    v.room_image_space = key(d.room->image_space);
    for (const auto& room : d.room->linked_rooms) holder.linked_rooms.push_back(key(room));
  }
  v.linked_rooms = data_or_null(holder.linked_rooms);
  v.linked_room_count = holder.linked_rooms.size();

  holder.topics.reserve(d.patrols.size());
  for (const auto& patrol : d.patrols) {
    auto& topics = holder.topics.emplace_back();
    for (const auto& topic : patrol.topics) {
      topics.push_back({topic.type, key(topic.topic), topic.subtype});
    }
    holder.patrols.push_back({patrol.idle_time, key(patrol.idle), key(patrol.topic),
                              data_or_null(topics), topics.size()});
  }
  v.patrols = data_or_null(holder.patrols);
  v.patrol_count = holder.patrols.size();

  if (d.count) {
    v.has_count = 1;
    v.count = *d.count;
  }
  if (d.level_modifier) {
    v.has_level_modifier = 1;
    v.level_modifier = *d.level_modifier;
  }

  if (d.scripts) {
    const auto& scripts = *d.scripts;
    v.has_scripts = 1;
    v.script_version = scripts.raw.version;
    v.script_object_format = scripts.raw.object_format;
    v.scripts_unsupported = scripts.raw.unsupported ? 1 : 0;
    holder.properties.reserve(scripts.raw.scripts.size());
    std::size_t property_total = 0;
    for (const auto& raw : scripts.raw.scripts) property_total += raw.properties.size();
    holder.elements.reserve(property_total);
    for (std::size_t i = 0; i < scripts.raw.scripts.size(); ++i) {
      const auto& raw = scripts.raw.scripts[i];
      const auto& resolved = scripts.scripts[i];
      auto& properties = holder.properties.emplace_back();
      for (std::size_t p = 0; p < raw.properties.size(); ++p) {
        const auto& in = raw.properties[p];
        radicle_script_property out{};
        auto& elements = holder.elements.emplace_back();
        bool has_elements = false;
        const auto add = [&](const auto& values, const auto& set) {
          if (!values) return;
          has_elements = true;
          for (std::size_t e = 0; e < values->size(); ++e) {
            radicle_script_element element{};
            set(element, e);
            elements.push_back(element);
          }
        };
        add(in.object_elements, [&](auto& element, std::size_t e) {
          element.object = key(resolved.properties[p].object_elements->at(e));
          element.alias = (*in.object_elements)[e].alias;
        });
        add(in.text_elements, [&](auto& element, std::size_t e) { element.text = (*in.text_elements)[e].c_str(); });
        add(in.integer_elements, [&](auto& element, std::size_t e) { element.integer = (*in.integer_elements)[e]; });
        add(in.real_elements, [&](auto& element, std::size_t e) { element.real = (*in.real_elements)[e]; });
        add(in.boolean_elements, [&](auto& element, std::size_t e) { element.boolean = (*in.boolean_elements)[e] ? 1 : 0; });
        if (has_elements) {
          out.has_elements = 1;
          out.elements = data_or_null(elements);
          out.element_value_count = elements.size();
        }
        if (in.struct_elements) {
          auto& structs = holder.structs.emplace_back();
          for (std::size_t s = 0; s < in.struct_elements->size(); ++s) {
            const auto& structure = (*in.struct_elements)[s];
            auto& members = holder.struct_members.emplace_back();
            for (std::size_t m = 0; m < structure.members.size(); ++m) {
              const auto& member = structure.members[m];
              radicle_script_struct_member out_member{};
              out_member.name = member.name.c_str();
              out_member.type = member.type;
              out_member.flags = member.flags;
              if (member.object) {
                out_member.value.object = key(resolved.properties[p].struct_member_objects->at(s).at(m));
                out_member.value.alias = member.object->alias;
              }
              if (member.text) out_member.value.text = member.text->c_str();
              if (member.integer) out_member.value.integer = *member.integer;
              if (member.real) out_member.value.real = *member.real;
              if (member.boolean) out_member.value.boolean = *member.boolean ? 1 : 0;
              members.push_back(out_member);
            }
            structs.push_back({data_or_null(members), members.size()});
          }
          out.has_structs = 1;
          out.structs = data_or_null(structs);
          out.struct_count = structs.size();
        }
        out.name = in.name.c_str();
        out.type = in.type;
        out.flags = in.flags;
        out.object = key(resolved.properties[p].object);
        if (in.alias) {
          out.has_alias = 1;
          out.alias = *in.alias;
        }
        if (in.text) {
          out.text = in.text->c_str();
          out.text_size = in.text->size();
        }
        if (in.integer) {
          out.has_integer = 1;
          out.integer = *in.integer;
        }
        if (in.real) {
          out.has_real = 1;
          out.real = *in.real;
        }
        if (in.boolean) {
          out.has_boolean = 1;
          out.boolean = *in.boolean ? 1 : 0;
        }
        if (in.element_count) {
          out.has_element_count = 1;
          out.element_count = *in.element_count;
        }
        properties.push_back(out);
      }
      holder.scripts.push_back({raw.name.c_str(), raw.flags, data_or_null(properties),
                                properties.size()});
    }
  }
  v.scripts = data_or_null(holder.scripts);
  v.script_count = holder.scripts.size();
  v.internal = &holder;
}

}  // namespace

extern "C" {

radicle_status radicle_placed_ref_decode(const radicle_env* env,
                                         const radicle_record* record,
                                         radicle_placed_ref** out_ref) {
  clear_error();
  if (env == nullptr || record == nullptr || out_ref == nullptr) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "env, record and out_ref are required");
  }
  if (!belongs_to_env(env, to_ref(record))) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "record does not belong to this environment");
  }
  *out_ref = nullptr;
  return guarded("unknown exception while decoding placed reference", [&] {
    auto holder = std::make_unique<PlacedHolder>();
    holder->data = fo4::env::decode_placed_reference(env->value, *to_ref(record));
    build_view(*env, *holder);
    *out_ref = &holder.release()->view;
    return RADICLE_STATUS_OK;
  });
}

void radicle_placed_ref_free(radicle_placed_ref* ref) {
  clear_error();
  if (ref == nullptr) return;
  delete static_cast<PlacedHolder*>(ref->internal);
}

radicle_status radicle_record_primary_model_path(const radicle_env* env,
                                                 const radicle_record* record,
                                                 const char** out_path) {
  clear_error();
  if (env == nullptr || record == nullptr || out_path == nullptr) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "env, record and out_path are required");
  }
  if (!belongs_to_env(env, to_ref(record))) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "record does not belong to this environment");
  }
  *out_path = nullptr;
  return guarded("unknown exception while reading the primary model path", [&] {
    const auto path = fo4::env::primary_model_path(*to_ref(record));
    if (!path) return fail(RADICLE_STATUS_NOT_FOUND, "record has no model path");
    *out_path = stored_text(*env, *path);
    return RADICLE_STATUS_OK;
  });
}

}  // extern "C"
