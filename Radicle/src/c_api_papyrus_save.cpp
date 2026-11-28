#include "c_api_env_internal.hpp"
#include "c_api_internal.hpp"
#include "c_api_papyrus_internal.hpp"
#include "c_api_papyrus_runtime_internal.hpp"
#include "c_api_pex_internal.hpp"
#include "fo4/c_api_papyrus_save.h"
#include "fo4/papyrus/runtime.hpp"
#include "fo4/papyrus/save_state.hpp"
#include "fo4/papyrus/vm_state.hpp"

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace {

char* copyText(const std::string& text) {
  char* out = static_cast<char*>(std::malloc(text.size() + 1));
  if (out == nullptr) return nullptr;
  std::memcpy(out, text.c_str(), text.size() + 1);
  return out;
}

radicle_status restoreInstances(radicle_papyrus_runtime* runtime, const char* blob, size_t blob_size, uint32_t flags,
                                const radicle_papyrus_engine_handle_map* engine_map, size_t engine_map_count, bool restricted,
                                const radicle_papyrus_owner_kind* owner_kinds, const char* const* base_form_keys,
                                const char* const* discriminators, const char* const* scripts, size_t identity_count,
                                radicle_papyrus_state_report* out_report) {
  radicle_c_detail::clear_error();
  if (runtime == nullptr || blob == nullptr || out_report == nullptr ||
      out_report->struct_size < sizeof(radicle_papyrus_state_report) ||
      (engine_map_count != 0 && engine_map == nullptr) ||
      (restricted && identity_count != 0 &&
       (owner_kinds == nullptr || base_form_keys == nullptr || discriminators == nullptr || scripts == nullptr))) {
    return radicle_c_detail::fail(RADICLE_STATUS_INVALID_ARGUMENT,
                                  "runtime, blob, the engine map, the identity arrays and a sized out_report are required");
  }
  out_report->instances = 0;
  out_report->variables = 0;
  out_report->skipped_instances = 0;
  out_report->skipped_variables = 0;
  out_report->unbound_references = 0;
  out_report->report = nullptr;
  if ((flags & ~RADICLE_PAPYRUS_STATE_SKIP_UNKNOWN) != 0) {
    return radicle_c_detail::fail(RADICLE_STATUS_INVALID_ARGUMENT, "unknown restore flags");
  }
  return radicle_c_env_detail::guarded("unknown exception while restoring the instance state", [&] {
    fo4::papyrus::InstanceStateOptions options;
    options.skipUnknown = (flags & RADICLE_PAPYRUS_STATE_SKIP_UNKNOWN) != 0;
    for (size_t i = 0; i < engine_map_count; ++i) {
      if (!options.engineRemap.emplace(engine_map[i].saved, engine_map[i].live).second) {
        return radicle_c_detail::fail(RADICLE_STATUS_INVALID_ARGUMENT, "the engine handle map repeats a saved handle");
      }
    }
    if (restricted) {
      options.only.emplace();
      for (size_t i = 0; i < identity_count; ++i) {
        if (base_form_keys[i] == nullptr || scripts[i] == nullptr || *scripts[i] == '\0') {
          return radicle_c_detail::fail(RADICLE_STATUS_INVALID_ARGUMENT, "an instance identity needs a base key and a script");
        }
        std::optional<fo4::papyrus::OwnerId> owner =
            radicle_c_papyrus_detail::ownerFrom(owner_kinds[i], base_form_keys[i], discriminators[i]);
        if (!owner.has_value()) return radicle_c_detail::fail(RADICLE_STATUS_INVALID_ARGUMENT, "an instance identity has an unusable owner");
        options.only->push_back(fo4::papyrus::InstanceIdentity{std::move(*owner), scripts[i]});
      }
    }
    fo4::papyrus::InstanceStateReport report;
    fo4::papyrus::InstanceStatePlan staged;
    const fo4::papyrus::InstanceStateResult result =
        fo4::papyrus::stageInstanceState(runtime->runtime, std::string_view(blob, blob_size), options, staged, report);
    if (!result.ok()) {
      using fo4::papyrus::InstanceStateRefusal;
      const radicle_status status = result.refusal == InstanceStateRefusal::Malformed ? RADICLE_STATUS_PARSE_ERROR
                                    : result.refusal == InstanceStateRefusal::NotFound ? RADICLE_STATUS_NOT_FOUND
                                                                                       : RADICLE_STATUS_INVALID_ARGUMENT;
      return radicle_c_detail::fail(status, result.reason.c_str());
    }
    char* text = copyText(report.toReport());
    if (text == nullptr) return radicle_c_detail::fail(RADICLE_STATUS_OUT_OF_MEMORY, "failed to allocate the restore report");
    fo4::papyrus::commitInstanceState(runtime->runtime, staged);
    out_report->instances = report.instances;
    out_report->variables = report.variables;
    out_report->skipped_instances = report.skippedInstances;
    out_report->skipped_variables = report.skippedVariables;
    out_report->unbound_references = report.unboundReferences;
    out_report->report = text;
    return RADICLE_STATUS_OK;
  });
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

radicle_status radicle_papyrus_runtime_state_capture(const radicle_papyrus_runtime* runtime,
                                                     radicle_papyrus_state_capture* out_capture) {
  radicle_c_detail::clear_error();
  if (runtime == nullptr || out_capture == nullptr || out_capture->struct_size < sizeof(radicle_papyrus_state_capture)) {
    return radicle_c_detail::fail(RADICLE_STATUS_INVALID_ARGUMENT,
                                  "runtime and a sized out_capture are required");
  }
  out_capture->blob = nullptr;
  out_capture->digest = nullptr;
  out_capture->engine_handles = nullptr;
  out_capture->engine_handle_count = 0;
  return radicle_c_env_detail::guarded("unknown exception while capturing the instance state", [&] {
    fo4::papyrus::InstanceStateCapture capture;
    const fo4::papyrus::InstanceStateResult result = fo4::papyrus::captureInstanceState(runtime->runtime, capture);
    if (!result.ok()) return radicle_c_detail::fail(RADICLE_STATUS_INVALID_ARGUMENT, result.reason.c_str());
    char* blob = copyText(capture.blob);
    char* digest = copyText(capture.digest);
    uint64_t* handles = nullptr;
    if (!capture.engineHandles.empty()) {
      handles = static_cast<uint64_t*>(std::malloc(capture.engineHandles.size() * sizeof(uint64_t)));
      if (handles != nullptr) std::memcpy(handles, capture.engineHandles.data(), capture.engineHandles.size() * sizeof(uint64_t));
    }
    if (blob == nullptr || digest == nullptr || (handles == nullptr && !capture.engineHandles.empty())) {
      std::free(blob);
      std::free(digest);
      std::free(handles);
      return radicle_c_detail::fail(RADICLE_STATUS_OUT_OF_MEMORY, "failed to allocate the instance state");
    }
    out_capture->blob = blob;
    out_capture->digest = digest;
    out_capture->engine_handles = handles;
    out_capture->engine_handle_count = capture.engineHandles.size();
    return RADICLE_STATUS_OK;
  });
}

void radicle_papyrus_state_capture_destroy(radicle_papyrus_state_capture* capture) {
  radicle_c_detail::clear_error();
  if (capture == nullptr) return;
  std::free(capture->blob);
  std::free(capture->digest);
  std::free(capture->engine_handles);
  capture->blob = nullptr;
  capture->digest = nullptr;
  capture->engine_handles = nullptr;
  capture->engine_handle_count = 0;
}

radicle_status radicle_papyrus_runtime_state_restore(radicle_papyrus_runtime* runtime, const char* blob, size_t blob_size,
                                                     uint32_t flags, const radicle_papyrus_engine_handle_map* engine_map,
                                                     size_t engine_map_count, radicle_papyrus_state_report* out_report) {
  return restoreInstances(runtime, blob, blob_size, flags, engine_map, engine_map_count, false, nullptr, nullptr, nullptr, nullptr, 0,
                          out_report);
}

radicle_status radicle_papyrus_runtime_state_restore_instances(
    radicle_papyrus_runtime* runtime, const char* blob, size_t blob_size, uint32_t flags,
    const radicle_papyrus_engine_handle_map* engine_map, size_t engine_map_count, const radicle_papyrus_owner_kind* owner_kinds,
    const char* const* base_form_keys, const char* const* discriminators, const char* const* scripts, size_t identity_count,
    radicle_papyrus_state_report* out_report) {
  return restoreInstances(runtime, blob, blob_size, flags, engine_map, engine_map_count, true, owner_kinds, base_form_keys,
                          discriminators, scripts, identity_count, out_report);
}

void radicle_papyrus_state_report_destroy(radicle_papyrus_state_report* report) {
  radicle_c_detail::clear_error();
  if (report == nullptr) return;
  std::free(report->report);
  report->report = nullptr;
}

}  // extern "C"
