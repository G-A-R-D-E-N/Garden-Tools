#include "c_api_forms_internal.hpp"
#include "fo4/c_api_ui_records.h"
#include "fo4/environment/placed.hpp"
#include "fo4/environment/world.hpp"
#include "fo4/forms/ui_records.hpp"
#include "fo4/plugin/mutation.hpp"
#include "fo4/schema/form_links.hpp"
#include "fo4/schema/scalar.hpp"
#include "fo4/text/encoding.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <deque>
#include <memory>
#include <optional>
#include <string>
#include <type_traits>
#include <vector>

namespace {

constexpr std::uint8_t k_leveled_lock_bit = 0x04;

using namespace radicle_c_detail;
using namespace radicle_c_env_detail;
using namespace radicle_c_forms_detail;

void fill_text(TextStore& store, const std::optional<fo4::schema::LocalizedString>& value, radicle_ui_text& out) {
  store.keep(value, out.kind, out.text, out.string_id);
}

struct FormLinksHolder {
  radicle_form_links view{};
  std::vector<radicle_form_link> links;
};

struct ConstructibleObjectHolder {
  radicle_constructible_object view{};
  TextStore text;
  std::vector<radicle_form_key> keywords;
  std::vector<radicle_component_entry> components;
  std::deque<std::string> condition_text;
  std::vector<radicle_condition> conditions;
  std::vector<radicle_condition_links> condition_links;
};

struct TerminalConditionRows {
  std::vector<radicle_condition> conditions;
  std::vector<radicle_condition_links> condition_links;
};

struct TerminalHolder {
  radicle_terminal_info view{};
  TextStore text;
  std::deque<std::string> condition_text;
  std::deque<TerminalConditionRows> condition_rows;
  std::vector<radicle_terminal_body_text> bodies;
  std::vector<radicle_terminal_menu_item> items;
  std::vector<radicle_terminal_holotape> holotapes;
};

struct PerkEntryPointsHolder {
  radicle_perk_entry_points_info view{};
  std::deque<std::string> condition_text;
  std::deque<TerminalConditionRows> condition_rows;
  std::deque<std::vector<radicle_perk_entry_point_tab>> tab_rows;
  std::vector<radicle_perk_entry_point_effect> effects;
};

struct OmodPropertiesHolder {
  radicle_omod_properties_info view{};
  std::vector<radicle_omod_property_row> properties;
};

struct BodyPartDataHolder {
  radicle_body_part_data_info view{};
  TextStore text;
  std::vector<radicle_body_part_info> parts;
};

struct ActivatorHolder {
  radicle_activator_info view{};
  TextStore text;
};

struct ContainerHolder {
  radicle_container_info view{};
  TextStore text;
  std::vector<radicle_item_count> items;
};

struct DoorHolder {
  radicle_door_info view{};
  TextStore text;
  std::vector<radicle_form_key> destinations;
};

struct FurnitureHolder {
  radicle_furniture_info view{};
  TextStore text;
  std::vector<radicle_item_count> items;
  std::vector<radicle_furniture_marker> markers;
  std::vector<radicle_furniture_entry_point> entry_points;
  std::vector<radicle_furniture_marker_params> marker_params;
};

std::vector<radicle_item_count> to_c_items(const radicle_env& env, const std::vector<fo4::forms::ItemCount>& items) {
  std::vector<radicle_item_count> out;
  out.reserve(items.size());
  for (const auto& entry : items) out.push_back(radicle_item_count{to_c_key(env, entry.item), entry.count});
  return out;
}

const char* condition_string(std::deque<std::string>& text, const std::optional<std::string>& value) {
  if (!value) return nullptr;
  text.push_back(fo4::text::windows1252_to_utf8(*value));
  return text.back().c_str();
}

void append_condition_rows(const radicle_env& env, std::deque<std::string>& text,
                           const std::vector<fo4::CtdaCondition>& conditions,
                           const std::vector<fo4::forms::ConditionLinks>& links,
                           std::vector<radicle_condition>& rows, std::vector<radicle_condition_links>& link_rows) {
  for (std::size_t i = 0; i < conditions.size(); ++i) {
    const auto& c = conditions[i];
    rows.push_back({c.flags_byte, c.comparison_raw, c.function, c.unknown2, c.param1, c.param2, c.run_on, c.reference,
                    c.unknown3, condition_string(text, c.parameter1_string),
                    condition_string(text, c.parameter2_string)});
    link_rows.push_back({to_c_key(env, links[i].global), to_c_key(env, links[i].param1),
                         to_c_key(env, links[i].param2), to_c_key(env, links[i].reference)});
  }
}

const TerminalConditionRows& terminal_condition_rows(const radicle_env& env, std::deque<std::string>& text,
                                                     std::deque<TerminalConditionRows>& rows,
                                                     const std::vector<fo4::CtdaCondition>& conditions,
                                                     const std::vector<fo4::forms::ConditionLinks>& links) {
  auto& out = rows.emplace_back();
  append_condition_rows(env, text, conditions, links, out.conditions, out.condition_links);
  return out;
}

template <typename Holder, typename View>
void publish(std::unique_ptr<Holder>& holder, View& view, View** out) {
  view.internal = holder.get();
  *out = &holder.release()->view;
}

template <typename Body>
radicle_status decode_ui_record(const radicle_env* env, const radicle_record* record, const void* out,
                                const char* what, Body&& body) {
  clear_error();
  if (const auto status = require_env_record(env, record, out); status != RADICLE_STATUS_OK) return status;
  return guarded(what, [&] {
    try {
      const auto& ref = *to_ref(record);
      return body(env->value.plugin(ref.plugin), *ref.record);
    } catch (const fo4::forms::UiRecordError& error) {
      return fail_exception(RADICLE_STATUS_PARSE_ERROR, error);
    } catch (const fo4::forms::ComponentError& error) {
      return fail_exception(RADICLE_STATUS_PARSE_ERROR, error);
    }
  });
}

// Requires: every property read goes through the OMOD DATA run rows, which refuse a miscounted payload.
radicle_status omod_properties(const radicle_env* env,
                               const fo4::Plugin& plugin,
                               const fo4::Record& source,
                               radicle_omod_properties_info** out_info) {
  const bool has_data = std::any_of(source.subrecords.begin(), source.subrecords.end(),
                                    [](const auto& sub) { return sub.signature == fo4::sig_data; });
  if (!has_data) {
    throw fo4::schema::ScalarError("OMOD record has no data subrecord");
  }
  const auto row = [](const char* leaf) {
    const auto field = fo4::schema::scalar_field_descriptor(std::string("OMOD\\Property") + leaf);
    if (!field) throw fo4::schema::ScalarError(std::string("missing OMOD property row ") + leaf);
    return *field;
  };
  const auto value_type = row("ValueType");
  const auto function_type = row("FunctionType");
  const auto property_id = row("Property");
  const auto value1 = row("Value1");
  const auto value2 = row("Value2");
  const auto step = row("Step");
  const auto bytes = [&]<std::size_t N>(const fo4::schema::ScalarFieldDescriptor& field,
                                       std::size_t index, std::integral_constant<std::size_t, N>) {
    const auto stored = fo4::record_scalar_bytes(source, field, index);
    if (!stored || stored->size() != N) {
      throw fo4::schema::ScalarError(std::string(field.path) + " is missing or the wrong width");
    }
    std::array<std::byte, N> fixed{};
    std::copy_n(stored->begin(), N, fixed.begin());
    return fixed;
  };
  constexpr std::integral_constant<std::size_t, 1> w1;
  constexpr std::integral_constant<std::size_t, 2> w2;
  constexpr std::integral_constant<std::size_t, 4> w4;
  const auto count = fo4::record_field_count(source, value_type);
  auto holder = std::make_unique<OmodPropertiesHolder>();
  auto& view = holder->view;
  view.record = to_c_key(*env, source.form_key);
  holder->properties.reserve(count);
  const auto context = fo4::plugin_form_id_context(plugin);
  for (std::size_t index = 0; index < count; ++index) {
    radicle_omod_property_row out{};
    out.value_type = std::to_integer<std::uint8_t>(bytes(value_type, index, w1)[0]);
    out.function_type = std::to_integer<std::uint8_t>(bytes(function_type, index, w1)[0]);
    const auto id = bytes(property_id, index, w2);
    std::memcpy(&out.property, id.data(), sizeof(out.property));
    std::memcpy(out.value1, bytes(value1, index, w4).data(), 4);
    std::memcpy(out.value2, bytes(value2, index, w4).data(), 4);
    const auto step_bytes = bytes(step, index, w4);
    std::memcpy(&out.step, step_bytes.data(), sizeof(out.step));
    std::optional<fo4::FormKey> linked;
    if (out.value_type == 4 || out.value_type == 6) {
      std::uint32_t raw = 0;
      std::memcpy(&raw, out.value1, sizeof(raw));
      linked = fo4::resolve_plugin_form_id(fo4::RawFormId{raw}, context);
    }
    out.value1_form = to_c_key(*env, linked);
    holder->properties.push_back(out);
  }
  view.properties = holder->properties.empty() ? nullptr : holder->properties.data();
  view.property_count = holder->properties.size();
  publish(holder, view, out_info);
  return RADICLE_STATUS_OK;
}

}  // namespace

extern "C" {

radicle_status radicle_record_form_links(const radicle_env* env,
                                         const radicle_record* record,
                                         radicle_form_links** out_links) {
  if (out_links != nullptr) *out_links = nullptr;
  return decode_ui_record(env, record, out_links, "unknown exception while reading form links",
                          [&](const fo4::Plugin& plugin, const fo4::Record& source) {
    const auto collection = fo4::collect_record_form_links(source, fo4::plugin_form_id_context(plugin),
                                                           fo4::plugin_type_lookup(plugin));
    auto holder = std::make_unique<FormLinksHolder>();
    holder->links.reserve(collection.links.size());
    for (const auto& hit : collection.links) {
      radicle_form_link link{};
      const auto signature = hit.subrecord.to_string();
      std::memcpy(link.subrecord, signature.data(), 4);
      link.subrecord[4] = '\0';
      link.ordinal = hit.subrecord_ordinal;
      link.offset = hit.offset;
      link.raw_form_id = hit.raw.value();
      link.key = to_c_key(*env, hit.key);
      holder->links.push_back(link);
    }
    holder->view.links = holder->links.empty() ? nullptr : holder->links.data();
    holder->view.link_count = holder->links.size();
    publish(holder, holder->view, out_links);
    return RADICLE_STATUS_OK;
  });
}

void radicle_form_links_free(radicle_form_links* links) {
  clear_error();
  if (links == nullptr) return;
  delete static_cast<FormLinksHolder*>(links->internal);
}

radicle_status radicle_constructible_object_decode(const radicle_env* env,
                                                   const radicle_record* record,
                                                   radicle_constructible_object** out_object) {
  if (out_object != nullptr) *out_object = nullptr;
  return decode_ui_record(env, record, out_object, "unknown exception while decoding COBJ",
                          [&](const fo4::Plugin& plugin, const fo4::Record& source) {
    const auto data = fo4::forms::decode_constructible_object(plugin, source);
    auto holder = std::make_unique<ConstructibleObjectHolder>();
    auto& view = holder->view;
    view.record = to_c_key(*env, data.key);
    view.workbench_keyword = to_c_key(*env, data.workbench_keyword);
    view.created_object = to_c_key(*env, data.created_object);
    view.has_created_count = data.created_count ? 1 : 0;
    view.created_count = data.created_count.value_or(0);
    view.has_priority = data.priority ? 1 : 0;
    view.priority = data.priority.value_or(0);
    fill_text(holder->text, data.description, view.description);
    for (const auto& keyword : data.category_keywords) holder->keywords.push_back(to_c_key(*env, keyword));
    for (const auto& entry : data.components) {
      holder->components.push_back(radicle_component_entry{to_c_key(*env, entry.component), entry.count});
    }
    view.category_keywords = holder->keywords.empty() ? nullptr : holder->keywords.data();
    view.category_keyword_count = holder->keywords.size();
    view.components = holder->components.empty() ? nullptr : holder->components.data();
    view.component_count = holder->components.size();
    append_condition_rows(*env, holder->condition_text, data.conditions, data.condition_links, holder->conditions,
                          holder->condition_links);
    view.conditions = holder->conditions.empty() ? nullptr : holder->conditions.data();
    view.condition_links = holder->condition_links.empty() ? nullptr : holder->condition_links.data();
    view.condition_count = holder->conditions.size();
    publish(holder, view, out_object);
    return RADICLE_STATUS_OK;
  });
}

void radicle_constructible_object_free(radicle_constructible_object* object) {
  clear_error();
  if (object == nullptr) return;
  delete static_cast<ConstructibleObjectHolder*>(object->internal);
}

radicle_status radicle_perk_entry_points_decode(const radicle_env* env,
                                                const radicle_record* record,
                                                radicle_perk_entry_points_info** out_info) {
  if (out_info != nullptr) *out_info = nullptr;
  return decode_ui_record(env, record, out_info, "unknown exception while decoding PERK entry points",
                          [&](const fo4::Plugin& plugin, const fo4::Record& source) {
    const auto data = fo4::forms::decode_perk_entry_points(plugin, source);
    auto holder = std::make_unique<PerkEntryPointsHolder>();
    auto& view = holder->view;
    view.record = to_c_key(*env, data.key);
    for (const auto& source_effect : data.effects) {
      radicle_perk_entry_point_effect row{};
      row.rank = source_effect.rank;
      row.priority = source_effect.priority;
      row.entry_point = source_effect.entry_point;
      row.function = source_effect.function;
      row.has_parameter_type = source_effect.parameter_type ? 1 : 0;
      row.parameter_type = source_effect.parameter_type.value_or(0);
      row.has_value = source_effect.value ? 1 : 0;
      row.value = source_effect.value.value_or(0.0f);
      auto& tabs = holder->tab_rows.emplace_back();
      for (const auto& source_tab : source_effect.tabs) {
        const auto& rows = terminal_condition_rows(*env, holder->condition_text, holder->condition_rows,
                                                   source_tab.conditions, source_tab.condition_links);
        tabs.push_back({source_tab.run_on, rows.conditions.empty() ? nullptr : rows.conditions.data(),
                        rows.condition_links.empty() ? nullptr : rows.condition_links.data(),
                        rows.conditions.size()});
      }
      row.tabs = tabs.empty() ? nullptr : tabs.data();
      row.tab_count = tabs.size();
      holder->effects.push_back(row);
    }
    view.effects = holder->effects.empty() ? nullptr : holder->effects.data();
    view.effect_count = holder->effects.size();
    publish(holder, view, out_info);
    return RADICLE_STATUS_OK;
  });
}

void radicle_perk_entry_points_free(radicle_perk_entry_points_info* info) {
  clear_error();
  if (info == nullptr) return;
  delete static_cast<PerkEntryPointsHolder*>(info->internal);
}

radicle_status radicle_omod_properties_decode(const radicle_env* env,
                                              const radicle_record* record,
                                              radicle_omod_properties_info** out_info) {
  if (out_info != nullptr) *out_info = nullptr;
  return decode_ui_record(env, record, out_info, "unknown exception while decoding OMOD properties",
                          [&](const fo4::Plugin& plugin, const fo4::Record& source) {
    try {
      return omod_properties(env, plugin, source, out_info);
    } catch (const fo4::schema::ScalarError& error) {
      return fail_exception(RADICLE_STATUS_PARSE_ERROR, error);
    }
  });
}

void radicle_omod_properties_free(radicle_omod_properties_info* info) {
  clear_error();
  if (info == nullptr) return;
  delete static_cast<OmodPropertiesHolder*>(info->internal);
}

radicle_status radicle_body_part_data_decode(const radicle_env* env,
                                             const radicle_record* record,
                                             radicle_body_part_data_info** out_info) {
  if (out_info != nullptr) *out_info = nullptr;
  return decode_ui_record(env, record, out_info, "unknown exception while decoding BPTD",
                          [&](const fo4::Plugin& plugin, const fo4::Record& source) {
    const auto data = fo4::forms::decode_body_part_data(plugin, source);
    auto holder = std::make_unique<BodyPartDataHolder>();
    auto& view = holder->view;
    view.record = to_c_key(*env, data.key);
    for (const auto& part : data.parts) {
      radicle_body_part_info row{};
      fill_text(holder->text, part.name, row.name);
      row.part_node = holder->text.keep(part.part_node);
      row.vats_target = holder->text.keep(part.vats_target);
      if (part.data) {
        const auto& node = *part.data;
        row.has_data = 1;
        row.damage_mult = node.damage_mult;
        row.flags = node.flags;
        row.part_type = node.part_type;
        row.health_percent = node.health_percent;
        row.actor_value = to_c_key(*env, node.actor_value);
        row.to_hit_chance = node.to_hit_chance;
        row.explodable_explosion_chance = node.explodable_explosion_chance;
        row.non_lethal_dismemberment_chance = node.non_lethal_dismemberment_chance;
        row.geometry_segment_index = node.geometry_segment_index;
      }
      holder->parts.push_back(row);
    }
    view.parts = holder->parts.empty() ? nullptr : holder->parts.data();
    view.part_count = holder->parts.size();
    publish(holder, view, out_info);
    return RADICLE_STATUS_OK;
  });
}

void radicle_body_part_data_free(radicle_body_part_data_info* info) {
  clear_error();
  if (info == nullptr) return;
  delete static_cast<BodyPartDataHolder*>(info->internal);
}

radicle_status radicle_terminal_decode(const radicle_env* env,
                                       const radicle_record* record,
                                       radicle_terminal_info** out_info) {
  if (out_info != nullptr) *out_info = nullptr;
  return decode_ui_record(env, record, out_info, "unknown exception while decoding TERM",
                          [&](const fo4::Plugin& plugin, const fo4::Record& source) {
    const auto data = fo4::forms::decode_terminal_info(plugin, source);
    auto holder = std::make_unique<TerminalHolder>();
    auto& view = holder->view;
    view.record = to_c_key(*env, data.key);
    fill_text(holder->text, data.full_name, view.full_name);
    fill_text(holder->text, data.header_text, view.header_text);
    fill_text(holder->text, data.welcome_text, view.welcome_text);
    for (const auto& body : data.body_texts) {
      radicle_terminal_body_text item{};
      fill_text(holder->text, body.text, item.text);
      item.condition_count = body.condition_count;
      const auto& rows = terminal_condition_rows(*env, holder->condition_text, holder->condition_rows,
                                                 body.conditions, body.condition_links);
      item.conditions = rows.conditions.empty() ? nullptr : rows.conditions.data();
      item.condition_links = rows.condition_links.empty() ? nullptr : rows.condition_links.data();
      holder->bodies.push_back(item);
    }
    for (const auto& source_item : data.menu_items) {
      radicle_terminal_menu_item item{};
      fill_text(holder->text, source_item.item_text, item.item_text);
      fill_text(holder->text, source_item.response_text, item.response_text);
      fill_text(holder->text, source_item.display_text, item.display_text);
      item.has_type = source_item.type ? 1 : 0;
      item.type = source_item.type.value_or(0);
      item.has_item_id = source_item.item_id ? 1 : 0;
      item.item_id = source_item.item_id.value_or(0);
      item.image = holder->text.keep(source_item.image);
      item.submenu = to_c_key(*env, source_item.submenu);
      item.condition_count = source_item.condition_count;
      const auto& rows = terminal_condition_rows(*env, holder->condition_text, holder->condition_rows,
                                                 source_item.conditions, source_item.condition_links);
      item.conditions = rows.conditions.empty() ? nullptr : rows.conditions.data();
      item.condition_links = rows.condition_links.empty() ? nullptr : rows.condition_links.data();
      holder->items.push_back(item);
    }
    for (const auto& tape : data.holotapes) {
      holder->holotapes.push_back(radicle_terminal_holotape{to_c_key(*env, tape.holotape), tape.count});
    }
    view.body_texts = holder->bodies.empty() ? nullptr : holder->bodies.data();
    view.body_text_count = holder->bodies.size();
    view.menu_items = holder->items.empty() ? nullptr : holder->items.data();
    view.menu_item_count = holder->items.size();
    view.holotapes = holder->holotapes.empty() ? nullptr : holder->holotapes.data();
    view.holotape_count = holder->holotapes.size();
    publish(holder, view, out_info);
    return RADICLE_STATUS_OK;
  });
}

void radicle_terminal_free(radicle_terminal_info* info) {
  clear_error();
  if (info == nullptr) return;
  delete static_cast<TerminalHolder*>(info->internal);
}

radicle_status radicle_activator_decode(const radicle_env* env,
                                        const radicle_record* record,
                                        radicle_activator_info** out_info) {
  if (out_info != nullptr) *out_info = nullptr;
  return decode_ui_record(env, record, out_info, "unknown exception while decoding ACTI",
                          [&](const fo4::Plugin& plugin, const fo4::Record& source) {
    const auto data = fo4::forms::decode_activator_info(plugin, source);
    auto holder = std::make_unique<ActivatorHolder>();
    auto& view = holder->view;
    view.record = to_c_key(*env, data.key);
    fill_text(holder->text, data.full_name, view.full_name);
    fill_text(holder->text, data.activate_text, view.activate_text);
    view.has_flags = data.flags ? 1 : 0;
    view.flags = data.flags.value_or(0);
    view.interaction_keyword = to_c_key(*env, data.interaction_keyword);
    view.looping_sound = to_c_key(*env, data.looping_sound);
    view.activation_sound = to_c_key(*env, data.activation_sound);
    view.water_type = to_c_key(*env, data.water_type);
    publish(holder, view, out_info);
    return RADICLE_STATUS_OK;
  });
}

void radicle_activator_free(radicle_activator_info* info) {
  clear_error();
  if (info == nullptr) return;
  delete static_cast<ActivatorHolder*>(info->internal);
}

radicle_status radicle_container_decode(const radicle_env* env,
                                        const radicle_record* record,
                                        radicle_container_info** out_info) {
  if (out_info != nullptr) *out_info = nullptr;
  return decode_ui_record(env, record, out_info, "unknown exception while decoding CONT",
                          [&](const fo4::Plugin& plugin, const fo4::Record& source) {
    const auto data = fo4::forms::decode_container_info(plugin, source);
    auto holder = std::make_unique<ContainerHolder>();
    auto& view = holder->view;
    view.record = to_c_key(*env, data.key);
    fill_text(holder->text, data.full_name, view.full_name);
    view.has_data = data.flags ? 1 : 0;
    view.flags = data.flags.value_or(0);
    view.weight = data.weight.value_or(0.0f);
    holder->items = to_c_items(*env, data.items);
    view.items = holder->items.empty() ? nullptr : holder->items.data();
    view.item_count = holder->items.size();
    view.open_sound = to_c_key(*env, data.open_sound);
    view.close_sound = to_c_key(*env, data.close_sound);
    view.take_all_sound = to_c_key(*env, data.take_all_sound);
    view.filter_list = to_c_key(*env, data.filter_list);
    publish(holder, view, out_info);
    return RADICLE_STATUS_OK;
  });
}

void radicle_container_free(radicle_container_info* info) {
  clear_error();
  if (info == nullptr) return;
  delete static_cast<ContainerHolder*>(info->internal);
}

radicle_status radicle_door_decode(const radicle_env* env,
                                   const radicle_record* record,
                                   radicle_door_info** out_info) {
  if (out_info != nullptr) *out_info = nullptr;
  return decode_ui_record(env, record, out_info, "unknown exception while decoding DOOR",
                          [&](const fo4::Plugin& plugin, const fo4::Record& source) {
    const auto data = fo4::forms::decode_door_info(plugin, source);
    auto holder = std::make_unique<DoorHolder>();
    auto& view = holder->view;
    view.record = to_c_key(*env, data.key);
    fill_text(holder->text, data.full_name, view.full_name);
    view.has_flags = data.flags ? 1 : 0;
    view.flags = data.flags.value_or(0);
    view.open_sound = to_c_key(*env, data.open_sound);
    view.close_sound = to_c_key(*env, data.close_sound);
    view.loop_sound = to_c_key(*env, data.loop_sound);
    fill_text(holder->text, data.open_text, view.open_text);
    fill_text(holder->text, data.close_text, view.close_text);
    for (const auto& destination : data.random_destinations) {
      holder->destinations.push_back(to_c_key(*env, destination));
    }
    view.random_destinations = holder->destinations.empty() ? nullptr : holder->destinations.data();
    view.random_destination_count = holder->destinations.size();
    publish(holder, view, out_info);
    return RADICLE_STATUS_OK;
  });
}

void radicle_door_free(radicle_door_info* info) {
  clear_error();
  if (info == nullptr) return;
  delete static_cast<DoorHolder*>(info->internal);
}

radicle_status radicle_furniture_decode(const radicle_env* env,
                                        const radicle_record* record,
                                        radicle_furniture_info** out_info) {
  if (out_info != nullptr) *out_info = nullptr;
  return decode_ui_record(env, record, out_info, "unknown exception while decoding FURN",
                          [&](const fo4::Plugin& plugin, const fo4::Record& source) {
    const auto data = fo4::forms::decode_furniture_info(plugin, source);
    auto holder = std::make_unique<FurnitureHolder>();
    auto& view = holder->view;
    view.record = to_c_key(*env, data.key);
    fill_text(holder->text, data.full_name, view.full_name);
    fill_text(holder->text, data.activate_text, view.activate_text);
    view.has_flags = data.flags ? 1 : 0;
    view.flags = data.flags.value_or(0);
    view.has_marker_flags = data.marker_flags ? 1 : 0;
    view.marker_flags = data.marker_flags.value_or(0);
    view.has_bench_type = data.bench_type ? 1 : 0;
    view.bench_type = data.bench_type.value_or(0);
    view.drinking_water_type = to_c_key(*env, data.drinking_water_type);
    view.associated_form = to_c_key(*env, data.associated_form);
    holder->items = to_c_items(*env, data.items);
    for (const auto& marker : data.markers) {
      holder->markers.push_back(radicle_furniture_marker{
          marker.index, static_cast<uint8_t>(marker.disabled_entry_points ? 1 : 0),
          marker.disabled_entry_points.value_or(0)});
    }
    for (const auto& entry : data.entry_points) {
      holder->entry_points.push_back(radicle_furniture_entry_point{entry.anim_type, entry.entry_points});
    }
    for (const auto& params : data.marker_params) {
      radicle_furniture_marker_params row{};
      for (size_t axis = 0; axis < 3; ++axis) row.offset[axis] = params.offset[axis];
      row.rotation_z = params.rotation_z;
      row.keyword = to_c_key(*env, params.keyword);
      row.entry_types = params.entry_types;
      holder->marker_params.push_back(row);
    }
    view.items = holder->items.empty() ? nullptr : holder->items.data();
    view.item_count = holder->items.size();
    view.markers = holder->markers.empty() ? nullptr : holder->markers.data();
    view.marker_count = holder->markers.size();
    view.entry_points = holder->entry_points.empty() ? nullptr : holder->entry_points.data();
    view.entry_point_count = holder->entry_points.size();
    view.marker_params = holder->marker_params.empty() ? nullptr : holder->marker_params.data();
    view.marker_param_count = holder->marker_params.size();
    view.marker_model = data.marker_model.empty() ? nullptr : holder->text.keep(std::string_view(data.marker_model));
    publish(holder, view, out_info);
    return RADICLE_STATUS_OK;
  });
}

void radicle_furniture_free(radicle_furniture_info* info) {
  clear_error();
  if (info == nullptr) return;
  delete static_cast<FurnitureHolder*>(info->internal);
}

radicle_status radicle_lock_info_read(const radicle_env* env,
                                      const radicle_record* record,
                                      radicle_lock_info* out_info) {
  clear_error();
  if (out_info != nullptr) *out_info = radicle_lock_info{};
  if (const auto status = require_env_record(env, record, out_info); status != RADICLE_STATUS_OK) return status;
  return guarded("unknown exception while reading lock data", [&] {
    const auto& ref = *to_ref(record);
    if (!fo4::env::is_placed_reference_signature(ref.record->header.signature)) {
      return fail(RADICLE_STATUS_INVALID_ARGUMENT, "record is not a placed reference");
    }
    try {
      const auto placed = fo4::env::decode_placed_reference(env->value, ref);
      radicle_lock_info view{};
      if (placed.lock) {
        view.has_lock_data = 1;
        view.level = placed.lock->level;
        view.kind = static_cast<radicle_lock_kind>(fo4::forms::classify_lock_level(placed.lock->level));
        view.key = to_c_key(*env, placed.lock->key);
        view.flags = placed.lock->flags;
        view.leveled_lock = (placed.lock->flags & k_leveled_lock_bit) != 0 ? 1 : 0;
      }
      *out_info = view;
      return RADICLE_STATUS_OK;
    } catch (const fo4::env::WorldDecodeError& error) {
      return fail_exception(RADICLE_STATUS_PARSE_ERROR, error);
    }
  });
}

}  // extern "C"
