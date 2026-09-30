#pragma once

#include "c_api_internal.hpp"
#include "fo4/c_api_env.h"
#include "fo4/archive/assets.hpp"
#include "fo4/environment/environment.hpp"

#include <cstdlib>
#include <cstring>
#include <exception>
#include <new>
#include <optional>
#include <string>
#include <vector>

struct radicle_env {
  fo4::env::Environment value;
  std::vector<std::string> paths;
};

namespace radicle_c_env_detail {

using namespace radicle_c_detail;

inline const fo4::env::RecordRef* to_ref(const radicle_record* record) {
  return reinterpret_cast<const fo4::env::RecordRef*>(record);
}

inline const radicle_record* to_handle(const fo4::env::RecordRef* ref) {
  return reinterpret_cast<const radicle_record*>(ref);
}

inline std::optional<fo4::FormKey> to_form_key(radicle_form_key key) {
  if (key.plugin == nullptr || *key.plugin == '\0' || key.local_id > 0x00FFFFFFu) {
    return std::nullopt;
  }
  return fo4::FormKey{fo4::PluginKey::from_filename(key.plugin),
                      fo4::LocalFormId{key.local_id}};
}

inline std::optional<fo4::Signature> to_signature(const char* text) {
  if (text == nullptr || std::strlen(text) != 4) return std::nullopt;
  return fo4::Signature::from_string(text);
}

inline radicle_status copy_records(const std::vector<const fo4::env::RecordRef*>& refs,
                            const radicle_record*** out_records,
                            size_t* out_count) {
  *out_records = nullptr;
  *out_count = 0;
  if (refs.empty()) return RADICLE_STATUS_OK;
  auto** array = static_cast<const radicle_record**>(
      std::malloc(refs.size() * sizeof(const radicle_record*)));
  if (array == nullptr) {
    return fail(RADICLE_STATUS_OUT_OF_MEMORY, "failed to allocate record array");
  }
  for (size_t i = 0; i < refs.size(); ++i) array[i] = to_handle(refs[i]);
  *out_records = array;
  *out_count = refs.size();
  return RADICLE_STATUS_OK;
}

template <class Fn>
radicle_status guarded(const char* what, Fn&& fn) {
  try {
    return fn();
  } catch (const std::bad_alloc& error) {
    return fail_exception(RADICLE_STATUS_OUT_OF_MEMORY, error);
  } catch (const fo4::env::EnvironmentError& error) {
    return fail_exception(RADICLE_STATUS_PARSE_ERROR, error);
  } catch (const fo4::FormIdError& error) {
    return fail_exception(RADICLE_STATUS_PARSE_ERROR, error);
  } catch (const fo4::archive::Ba2Error& error) {
    return fail_exception(RADICLE_STATUS_PARSE_ERROR, error);
  } catch (const fo4::archive::AssetPathError& error) {
    return fail_exception(RADICLE_STATUS_INVALID_ARGUMENT, error);
  } catch (const std::exception& error) {
    return fail_exception(RADICLE_STATUS_INTERNAL_ERROR, error);
  } catch (...) {
    return fail(RADICLE_STATUS_INTERNAL_ERROR, what);
  }
}

inline radicle_form_key to_c_key(const radicle_env& env, const std::optional<fo4::FormKey>& key) {
  if (!key || key->owner().empty()) return {nullptr, 0};
  const auto* owner = env.value.stable_owner(*key);
  if (owner == nullptr) throw fo4::env::EnvironmentError("FormKey owner is not in the environment");
  return {owner->filename().data(), key->local().value()};
}

radicle_status load_strings(const radicle_env* env,
                            const char* strings_dir,
                            const char* language,
                            const fo4::archive::AssetResolver* assets,
                            radicle_strings** out_strings);

}  // namespace radicle_c_env_detail
