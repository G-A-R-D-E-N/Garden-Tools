#include "c_api_forms_internal.hpp"
#include "fo4/c_api_armor.h"
#include "fo4/forms/armor.hpp"

#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <memory>
#include <optional>
#include <string_view>

namespace {

using namespace radicle_c_detail;
using namespace radicle_c_env_detail;
using namespace radicle_c_forms_detail;

struct ArmorHolder {
  radicle_armor_info view{};
  TextStore text;
};

struct ArmorAddonHolder {
  radicle_armor_addon_info view{};
  TextStore text;
};

fo4::forms::ArmorCreation to_creation(const radicle_armor_creation& source) {
  fo4::forms::ArmorCreation creation;
  creation.editor_id = optional_view(source.editor_id);
  creation.male_world_model = optional_view(source.male_world_model);
  creation.male_icon = optional_view(source.male_icon);
  creation.male_message_icon = optional_view(source.male_message_icon);
  creation.female_world_model = optional_view(source.female_world_model);
  creation.female_icon = optional_view(source.female_icon);
  creation.female_message_icon = optional_view(source.female_message_icon);
  creation.description = optional_view(source.description);
  creation.value = source.value;
  creation.weight = source.weight;
  creation.health = source.health;
  return creation;
}

fo4::forms::ArmorAddonCreation to_creation(const radicle_armor_addon_creation& source) {
  fo4::forms::ArmorAddonCreation creation;
  creation.editor_id = optional_view(source.editor_id);
  creation.male_biped_model = optional_view(source.male_biped_model);
  creation.female_biped_model = optional_view(source.female_biped_model);
  creation.male_first_person_model = optional_view(source.male_first_person_model);
  creation.female_first_person_model = optional_view(source.female_first_person_model);
  creation.male_priority = source.male_priority;
  creation.female_priority = source.female_priority;
  creation.male_weight_slider = source.male_weight_slider;
  creation.female_weight_slider = source.female_weight_slider;
  creation.unknown_before_detection = {std::byte{source.unknown_before_detection[0]},
                                       std::byte{source.unknown_before_detection[1]}};
  creation.detection_sound_value = source.detection_sound_value;
  creation.unknown_before_weapon_adjust = std::byte{source.unknown_before_weapon_adjust};
  creation.weapon_adjust = source.weapon_adjust;
  return creation;
}

}  // namespace

extern "C" {

radicle_status radicle_armor_decode(const radicle_env* env,
                                    const radicle_record* record,
                                    radicle_armor_info** out_info) {
  clear_error();
  if (out_info != nullptr) *out_info = nullptr;
  if (const auto status = require_env_record(env, record, out_info); status != RADICLE_STATUS_OK) return status;
  return guarded("unknown exception while decoding ARMO", [&] {
    try {
      const auto& ref = *to_ref(record);
      const auto data = fo4::forms::decode_armor(env->value.plugin(ref.plugin), ref.record->form_key);
      auto holder = std::make_unique<ArmorHolder>();
      auto& view = holder->view;
      view.record = to_c_key(*env, data.key);
      view.editor_id = holder->text.keep(data.editor_id);
      view.male_world_model = holder->text.keep(data.male_world_model);
      view.male_icon = holder->text.keep(data.male_icon);
      view.male_message_icon = holder->text.keep(data.male_message_icon);
      view.female_world_model = holder->text.keep(data.female_world_model);
      view.female_icon = holder->text.keep(data.female_icon);
      view.female_message_icon = holder->text.keep(data.female_message_icon);
      holder->text.keep(data.full_name, view.full_name_kind, view.full_name_text, view.full_name_string_id);
      holder->text.keep(data.description, view.description_kind, view.description_text,
                        view.description_string_id);
      view.value = data.value;
      view.weight = data.weight;
      view.health = data.health;
      view.internal = holder.get();
      *out_info = &holder.release()->view;
      return RADICLE_STATUS_OK;
    } catch (const fo4::forms::ArmorError& error) {
      return fail_exception(RADICLE_STATUS_PARSE_ERROR, error);
    }
  });
}

void radicle_armor_free(radicle_armor_info* info) {
  clear_error();
  if (info == nullptr) return;
  delete static_cast<ArmorHolder*>(info->internal);
}

radicle_status radicle_armor_addon_decode(const radicle_env* env,
                                          const radicle_record* record,
                                          radicle_armor_addon_info** out_info) {
  clear_error();
  if (out_info != nullptr) *out_info = nullptr;
  if (const auto status = require_env_record(env, record, out_info); status != RADICLE_STATUS_OK) return status;
  return guarded("unknown exception while decoding ARMA", [&] {
    try {
      const auto& ref = *to_ref(record);
      const auto data = fo4::forms::decode_armor_addon(env->value.plugin(ref.plugin), ref.record->form_key);
      auto holder = std::make_unique<ArmorAddonHolder>();
      auto& view = holder->view;
      view.record = to_c_key(*env, data.key);
      view.editor_id = holder->text.keep(data.editor_id);
      view.male_biped_model = holder->text.keep(data.male_biped_model);
      view.female_biped_model = holder->text.keep(data.female_biped_model);
      view.male_first_person_model = holder->text.keep(data.male_first_person_model);
      view.female_first_person_model = holder->text.keep(data.female_first_person_model);
      view.male_priority = data.male_priority;
      view.female_priority = data.female_priority;
      view.male_weight_slider = data.male_weight_slider;
      view.female_weight_slider = data.female_weight_slider;
      view.unknown_before_detection[0] = std::to_integer<uint8_t>(data.unknown_before_detection[0]);
      view.unknown_before_detection[1] = std::to_integer<uint8_t>(data.unknown_before_detection[1]);
      view.detection_sound_value = data.detection_sound_value;
      view.unknown_before_weapon_adjust = std::to_integer<uint8_t>(data.unknown_before_weapon_adjust);
      view.weapon_adjust = data.weapon_adjust;
      view.internal = holder.get();
      *out_info = &holder.release()->view;
      return RADICLE_STATUS_OK;
    } catch (const fo4::forms::ArmorError& error) {
      return fail_exception(RADICLE_STATUS_PARSE_ERROR, error);
    }
  });
}

void radicle_armor_addon_free(radicle_armor_addon_info* info) {
  clear_error();
  if (info == nullptr) return;
  delete static_cast<ArmorAddonHolder*>(info->internal);
}

radicle_status radicle_plugin_create_armor(radicle_plugin* plugin,
                                           const radicle_armor_creation* creation,
                                           radicle_form_key* out_key) {
  clear_error();
  if (out_key != nullptr) *out_key = {nullptr, 0};
  if (plugin == nullptr || creation == nullptr || out_key == nullptr) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "plugin, creation and out_key are required");
  }
  if (!texts_representable({creation->editor_id, creation->male_world_model, creation->male_icon,
                            creation->male_message_icon, creation->female_world_model,
                            creation->female_icon, creation->female_message_icon,
                            creation->description})) {
    return bad_text();
  }
  return guarded("unknown exception while creating ARMO", [&] {
    try {
      const auto key = fo4::forms::create_armor(plugin->value, to_creation(*creation));
      *out_key = {plugin->value.key.filename().data(), key.local().value()};
      return RADICLE_STATUS_OK;
    } catch (const fo4::forms::ArmorError& error) {
      return fail_exception(RADICLE_STATUS_INVALID_ARGUMENT, error);
    } catch (const fo4::FormIdError& error) {
      return fail_exception(RADICLE_STATUS_INVALID_ARGUMENT, error);
    }
  });
}

radicle_status radicle_plugin_create_armor_addon(radicle_plugin* plugin,
                                                 const radicle_armor_addon_creation* creation,
                                                 radicle_form_key* out_key) {
  clear_error();
  if (out_key != nullptr) *out_key = {nullptr, 0};
  if (plugin == nullptr || creation == nullptr || out_key == nullptr) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "plugin, creation and out_key are required");
  }
  if (!texts_representable({creation->editor_id, creation->male_biped_model,
                            creation->female_biped_model, creation->male_first_person_model,
                            creation->female_first_person_model})) {
    return bad_text();
  }
  return guarded("unknown exception while creating ARMA", [&] {
    try {
      const auto key = fo4::forms::create_armor_addon(plugin->value, to_creation(*creation));
      *out_key = {plugin->value.key.filename().data(), key.local().value()};
      return RADICLE_STATUS_OK;
    } catch (const fo4::forms::ArmorError& error) {
      return fail_exception(RADICLE_STATUS_INVALID_ARGUMENT, error);
    } catch (const fo4::FormIdError& error) {
      return fail_exception(RADICLE_STATUS_INVALID_ARGUMENT, error);
    }
  });
}

radicle_status radicle_plugin_set_armor_models(radicle_plugin* plugin,
                                               radicle_form_key key,
                                               const char* male_world_model,
                                               const char* male_icon,
                                               const char* male_message_icon,
                                               const char* female_world_model,
                                               const char* female_icon,
                                               const char* female_message_icon) {
  clear_error();
  if (plugin == nullptr) return fail(RADICLE_STATUS_INVALID_ARGUMENT, "plugin is required");
  const auto form_key = to_form_key(key);
  if (!form_key) return fail(RADICLE_STATUS_INVALID_ARGUMENT, "a valid record key is required");
  if (!texts_representable({male_world_model, male_icon, male_message_icon, female_world_model,
                            female_icon, female_message_icon})) {
    return bad_text();
  }
  return guarded("unknown exception while setting ARMO model fields", [&] {
    try {
      const bool found = fo4::forms::set_armor_model_fields(
          plugin->value, *form_key, optional_view(male_world_model), optional_view(male_icon),
          optional_view(male_message_icon), optional_view(female_world_model),
          optional_view(female_icon), optional_view(female_message_icon));
      if (!found) return fail(RADICLE_STATUS_NOT_FOUND, "plugin has no such record");
      return RADICLE_STATUS_OK;
    } catch (const fo4::forms::ArmorError& error) {
      return fail_exception(RADICLE_STATUS_INVALID_ARGUMENT, error);
    } catch (const fo4::FormIdError& error) {
      return fail_exception(RADICLE_STATUS_INVALID_ARGUMENT, error);
    }
  });
}

radicle_status radicle_plugin_set_armor_addon_models(radicle_plugin* plugin,
                                                     radicle_form_key key,
                                                     const char* male_biped_model,
                                                     const char* female_biped_model,
                                                     const char* male_first_person_model,
                                                     const char* female_first_person_model) {
  clear_error();
  if (plugin == nullptr) return fail(RADICLE_STATUS_INVALID_ARGUMENT, "plugin is required");
  const auto form_key = to_form_key(key);
  if (!form_key) return fail(RADICLE_STATUS_INVALID_ARGUMENT, "a valid record key is required");
  if (!texts_representable({male_biped_model, female_biped_model, male_first_person_model,
                            female_first_person_model})) {
    return bad_text();
  }
  return guarded("unknown exception while setting ARMA model fields", [&] {
    try {
      const bool found = fo4::forms::set_armor_addon_model_fields(
          plugin->value, *form_key, optional_view(male_biped_model),
          optional_view(female_biped_model), optional_view(male_first_person_model),
          optional_view(female_first_person_model));
      if (!found) return fail(RADICLE_STATUS_NOT_FOUND, "plugin has no such record");
      return RADICLE_STATUS_OK;
    } catch (const fo4::forms::ArmorError& error) {
      return fail_exception(RADICLE_STATUS_INVALID_ARGUMENT, error);
    } catch (const fo4::FormIdError& error) {
      return fail_exception(RADICLE_STATUS_INVALID_ARGUMENT, error);
    }
  });
}

}  // extern "C"
