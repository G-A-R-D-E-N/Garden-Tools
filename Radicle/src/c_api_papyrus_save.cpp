#include "c_api_env_internal.hpp"
#include "c_api_internal.hpp"
#include "c_api_papyrus_internal.hpp"
#include "c_api_papyrus_runtime_internal.hpp"
#include "c_api_pex_internal.hpp"
#include "fo4/c_api_papyrus_save.h"
#include "fo4/papyrus/runtime.hpp"
#include "fo4/papyrus/save_state.hpp"

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <map>
#include <string>
#include <vector>

namespace {

char* copyText(const std::string& text) {
  char* out = static_cast<char*>(std::malloc(text.size() + 1));
  if (out == nullptr) return nullptr;
  std::memcpy(out, text.c_str(), text.size() + 1);
  return out;
}

}  // namespace

extern "C" {

void radicle_papyrus_string_free(char* text) {
  radicle_c_detail::clear_error();
  std::free(text);
}

radicle_status radicle_papyrus_script_fingerprint(const radicle_pex* pex, const char* script,
                                                  char** out_fingerprint) {
  radicle_c_detail::clear_error();
  if (out_fingerprint != nullptr) *out_fingerprint = nullptr;
  if (pex == nullptr || script == nullptr || out_fingerprint == nullptr) {
    return radicle_c_detail::fail(RADICLE_STATUS_INVALID_ARGUMENT,
                                  "pex, script and out_fingerprint are required");
  }
  return radicle_c_env_detail::guarded("unknown exception while fingerprinting a script", [&] {
    for (const fo4::pex::PexObject& object : pex->value.objects) {
      if (fo4::papyrus::sameName(object.name, script)) {
        *out_fingerprint = copyText(fo4::papyrus::scriptFingerprint(object));
        if (*out_fingerprint == nullptr) {
          return radicle_c_detail::fail(RADICLE_STATUS_OUT_OF_MEMORY, "failed to allocate the fingerprint");
        }
        return RADICLE_STATUS_OK;
      }
    }
    return radicle_c_detail::fail(RADICLE_STATUS_NOT_FOUND, "no object of that name is in the pex");
  });
}

radicle_status radicle_papyrus_save_capture(const radicle_papyrus_runtime* runtime, char** out_json,
                                            char** out_digest) {
  radicle_c_detail::clear_error();
  if (out_json != nullptr) *out_json = nullptr;
  if (out_digest != nullptr) *out_digest = nullptr;
  if (runtime == nullptr || out_json == nullptr || out_digest == nullptr) {
    return radicle_c_detail::fail(RADICLE_STATUS_INVALID_ARGUMENT,
                                  "runtime, out_json and out_digest are required");
  }
  return radicle_c_env_detail::guarded("unknown exception while capturing the save state", [&] {
    std::vector<fo4::papyrus::SavedRegistration> registrations;
    std::vector<fo4::papyrus::SavedTimer> timers;
    for (const fo4::papyrus::RegistrationEntry& entry : runtime->runtime.registrations().entries()) {
      if (entry.registration.kind != fo4::papyrus::RegistrationKind::Timer) {
        const bool remote = entry.registration.kind == fo4::papyrus::RegistrationKind::RemoteEvent;
        fo4::papyrus::SavedRegistration registration;
        registration.ownerKey = entry.owner.key();
        registration.kind = remote ? "RemoteEvent" : "CustomEvent";
        if (remote && entry.registration.source.has_value()) registration.sourceKey = entry.registration.source->key();
        registration.eventName = entry.registration.eventName;
        registration.interval = entry.registration.intervalSeconds;
        registration.repeats = entry.registration.repeats;
        registration.clock = entry.registration.clock == fo4::papyrus::ClockSource::GameTime ? "GameTime" : "RealTime";
        registrations.push_back(std::move(registration));
      } else {
        fo4::papyrus::SavedTimer timer;
        timer.ownerKey = entry.owner.key();
        timer.eventName = entry.registration.eventName;
        timer.elapsed = entry.elapsedSeconds;
        timer.fires = entry.fires;
        timers.push_back(std::move(timer));
      }
    }
    const fo4::papyrus::SaveState state = fo4::papyrus::captureSaveState(
        runtime->runtime.captureThreads(), std::move(registrations), std::move(timers), {});
    *out_json = copyText(state.serialize());
    *out_digest = copyText(state.digest());
    if (*out_json == nullptr || *out_digest == nullptr) {
      std::free(*out_json);
      std::free(*out_digest);
      *out_json = nullptr;
      *out_digest = nullptr;
      return radicle_c_detail::fail(RADICLE_STATUS_OUT_OF_MEMORY, "failed to allocate the save state");
    }
    return RADICLE_STATUS_OK;
  });
}

radicle_status radicle_papyrus_save_restore(const char* json, const char* const* scripts,
                                            const char* const* fingerprints, size_t script_count,
                                            char** out_report, int32_t* out_resumed,
                                            int32_t* out_rejected) {
  radicle_c_detail::clear_error();
  if (out_report != nullptr) *out_report = nullptr;
  if (out_resumed != nullptr) *out_resumed = 0;
  if (out_rejected != nullptr) *out_rejected = 0;
  if (json == nullptr || out_report == nullptr || out_resumed == nullptr || out_rejected == nullptr ||
      (script_count != 0 && (scripts == nullptr || fingerprints == nullptr))) {
    return radicle_c_detail::fail(RADICLE_STATUS_INVALID_ARGUMENT,
                                  "json, out_report, out_resumed, out_rejected and the fingerprint pairs are required");
  }
  return radicle_c_env_detail::guarded("unknown exception while restoring the save state", [&] {
    std::map<std::string, std::string> current;
    for (size_t i = 0; i < script_count; ++i) {
      if (scripts[i] == nullptr || fingerprints[i] == nullptr) {
        return radicle_c_detail::fail(RADICLE_STATUS_INVALID_ARGUMENT, "fingerprint pairs must not be null");
      }
      current[scripts[i]] = fingerprints[i];
    }
    std::string error;
    const std::optional<fo4::papyrus::SaveState> state =
        fo4::papyrus::SaveState::deserialize(json, error);
    if (!state.has_value()) return radicle_c_detail::fail(RADICLE_STATUS_PARSE_ERROR, error.c_str());
    const fo4::papyrus::RestoreReport report = fo4::papyrus::restoreSaveState(*state, current);
    *out_report = copyText(report.toReport());
    if (*out_report == nullptr) {
      return radicle_c_detail::fail(RADICLE_STATUS_OUT_OF_MEMORY, "failed to allocate the restore report");
    }
    *out_resumed = report.resumed;
    *out_rejected = report.rejected;
    return RADICLE_STATUS_OK;
  });
}

}  // extern "C"
