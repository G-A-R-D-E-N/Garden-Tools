#include "c_api_forms_internal.hpp"
#include "fo4/c_api_eyes.h"
#include "fo4/forms/eyes.hpp"
#include "fo4/plugin/reader.hpp"

#include <memory>
#include <optional>
#include <string_view>

namespace {

using namespace radicle_c_detail;
using namespace radicle_c_env_detail;
using namespace radicle_c_forms_detail;

struct EyesHolder {
  radicle_eyes_info view{};
  TextStore text;
};

}  // namespace

extern "C" {

radicle_status radicle_eyes_decode(const radicle_env* env,
                                   const radicle_record* record,
                                   radicle_eyes_info** out_info) {
  clear_error();
  if (out_info != nullptr) *out_info = nullptr;
  if (const auto status = require_env_record(env, record, out_info); status != RADICLE_STATUS_OK) return status;
  return guarded("unknown exception while decoding EYES", [&] {
    try {
      const auto& ref = *to_ref(record);
      const auto data = fo4::forms::decode_eyes(env->value.plugin(ref.plugin), ref.record->form_key);
      auto holder = std::make_unique<EyesHolder>();
      auto& view = holder->view;
      view.record = to_c_key(*env, data.key);
      view.record_flags = data.record_flags.value();
      view.editor_id = holder->text.keep(data.editor_id);
      view.full_text = holder->text.keep(data.full_text);
      view.full_raw_size = data.full_raw.size();
      view.icon = holder->text.keep(std::string_view(data.icon));
      view.data_flags = data.data_flags;
      view.internal = holder.get();
      *out_info = &holder.release()->view;
      return RADICLE_STATUS_OK;
    } catch (const fo4::forms::EyesError& error) {
      return fail_exception(RADICLE_STATUS_PARSE_ERROR, error);
    }
  });
}

void radicle_eyes_free(radicle_eyes_info* info) {
  clear_error();
  if (info == nullptr) return;
  delete static_cast<EyesHolder*>(info->internal);
}

radicle_status radicle_plugin_create_eyes(radicle_plugin* plugin,
                                          const radicle_eyes_creation* creation,
                                          radicle_form_key* out_key) {
  clear_error();
  if (out_key != nullptr) *out_key = {nullptr, 0};
  if (plugin == nullptr || creation == nullptr || out_key == nullptr) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "plugin, creation and out_key are required");
  }
  if (creation->full_text == nullptr || creation->icon == nullptr) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "EYES full text and icon are required");
  }
  if (!texts_representable({creation->editor_id, creation->full_text, creation->icon})) {
    return bad_text();
  }
  return guarded("unknown exception while creating EYES", [&] {
    try {
      const auto key = fo4::forms::create_eyes(plugin->value, optional_view(creation->editor_id),
                                               creation->full_text, creation->icon,
                                               creation->data_flags,
                                               fo4::RecordFlags{creation->record_flags});
      *out_key = {plugin->value.key.filename().data(), key.local().value()};
      return RADICLE_STATUS_OK;
    } catch (const fo4::forms::EyesError& error) {
      return fail_exception(RADICLE_STATUS_INVALID_ARGUMENT, error);
    } catch (const fo4::FormIdError& error) {
      return fail_exception(RADICLE_STATUS_INVALID_ARGUMENT, error);
    }
  });
}

radicle_status radicle_plugin_set_eyes_text(radicle_plugin* plugin,
                                            radicle_form_key key,
                                            const char* editor_id,
                                            const char* full_text,
                                            const char* icon) {
  clear_error();
  std::optional<fo4::FormKey> form_key;
  if (const auto status = require_plugin_record(plugin, key, form_key); status != RADICLE_STATUS_OK) return status;
  if (!texts_representable({editor_id, full_text, icon})) return bad_text();
  return guarded("unknown exception while setting EYES text fields", [&] {
    try {
      const bool found = fo4::forms::set_eyes_text_fields(plugin->value, *form_key,
                                                          optional_view(editor_id),
                                                          optional_view(full_text),
                                                          optional_view(icon));
      if (!found) return fail(RADICLE_STATUS_NOT_FOUND, "plugin has no such record");
      return RADICLE_STATUS_OK;
    } catch (const fo4::forms::EyesError& error) {
      return fail_exception(RADICLE_STATUS_INVALID_ARGUMENT, error);
    } catch (const fo4::FormIdError& error) {
      return fail_exception(RADICLE_STATUS_INVALID_ARGUMENT, error);
    }
  });
}

radicle_status radicle_plugin_set_eyes_data_flags(radicle_plugin* plugin,
                                                  radicle_form_key key,
                                                  uint8_t flags) {
  clear_error();
  std::optional<fo4::FormKey> form_key;
  if (const auto status = require_plugin_record(plugin, key, form_key); status != RADICLE_STATUS_OK) return status;
  return guarded("unknown exception while setting EYES data flags", [&] {
    try {
      const bool found = fo4::forms::set_eyes_data_flags(plugin->value, *form_key, flags);
      if (!found) return fail(RADICLE_STATUS_NOT_FOUND, "plugin has no such record");
      return RADICLE_STATUS_OK;
    } catch (const fo4::forms::EyesError& error) {
      return fail_exception(RADICLE_STATUS_INVALID_ARGUMENT, error);
    } catch (const fo4::FormIdError& error) {
      return fail_exception(RADICLE_STATUS_INVALID_ARGUMENT, error);
    }
  });
}

}  // extern "C"
