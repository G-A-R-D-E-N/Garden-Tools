#include "fo4/c_api_f4se.h"

#include "c_api_internal.hpp"
#include "fo4/f4se/binding_json.hpp"
#include "fo4/f4se/emitter.hpp"

#include <cstdlib>
#include <cstring>
#include <string>

namespace {

using namespace radicle_c_detail;

constexpr CopyMessages k_copy_messages{"out_bytes and out_size are required", "failed to allocate F4SE emit text"};

radicle_status run(const char* plugin_json,
                   const char* existing_json,
                   int emit_despite_errors,
                   bool round_trip,
                   uint8_t** out_text,
                   size_t* out_size) {
  clear_error();
  if (!out_text || !out_size) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "out_text and out_size are required");
  }
  *out_text = nullptr;
  *out_size = 0;
  if (!plugin_json) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "plugin_json is required");
  }

  return guard_exceptions("unknown exception while emitting the F4SE plugin", [&] {
    auto parsed = fo4::f4se::bindingJson::parsePlugin(plugin_json);
    if (!parsed.plugin) {
      return fail(RADICLE_STATUS_PARSE_ERROR, parsed.error.c_str());
    }

    fo4::f4se::F4SEEmitOptions options;
    options.emitDespiteErrors = emit_despite_errors != 0;
    if (existing_json != nullptr && existing_json[0] != '\0') {
      auto existing = fo4::f4se::bindingJson::parseExisting(existing_json);
      if (!existing.ok) {
        return fail(RADICLE_STATUS_PARSE_ERROR, existing.error.c_str());
      }
      options.existing = std::move(existing.files);
    }

    const fo4::f4se::F4SEEmitter emitter;
    const auto result = emitter.emit(*parsed.plugin, &options);
    if (!round_trip) {
      return copy_out(fo4::f4se::bindingJson::serializeEmit(result), out_text, out_size, k_copy_messages);
    }
    const auto check = fo4::f4se::F4SEEmitter::roundTrip(*parsed.plugin, result);
    return copy_out(fo4::f4se::bindingJson::serializeEmit(result, &check), out_text, out_size, k_copy_messages);
  });
}

}  // namespace

extern "C" {

radicle_status radicle_f4se_emit(const char* plugin_json,
                                 const char* existing_json,
                                 int emit_despite_errors,
                                 uint8_t** out_text,
                                 size_t* out_size) {
  return run(plugin_json, existing_json, emit_despite_errors, false, out_text, out_size);
}

radicle_status radicle_f4se_round_trip(const char* plugin_json,
                                       const char* existing_json,
                                       int emit_despite_errors,
                                       uint8_t** out_text,
                                       size_t* out_size) {
  return run(plugin_json, existing_json, emit_despite_errors, true, out_text, out_size);
}

}  // extern "C"
