#include "c_api_env_internal.hpp"
#include "c_api_internal.hpp"
#include "fo4/c_api_cell_edit.h"
#include "fo4/plugin/edit_history.hpp"

#include <cstddef>
#include <cstdint>
#include <utility>

namespace {

using namespace radicle_c_detail;
using namespace radicle_c_env_detail;

}  // namespace

struct radicle_edit_history {
  fo4::PluginEditHistory history;
};

extern "C" {

radicle_status radicle_plugin_history_create(radicle_edit_history** out_history) {
  clear_error();
  if (out_history == nullptr) return fail(RADICLE_STATUS_INVALID_ARGUMENT, "out_history is required");
  *out_history = nullptr;
  return guarded("unknown exception while creating the patch edit history", [&] {
    *out_history = new radicle_edit_history();
    return RADICLE_STATUS_OK;
  });
}

void radicle_plugin_history_destroy(radicle_edit_history* history) { delete history; }

radicle_status radicle_plugin_history_record(radicle_edit_history* history, const radicle_plugin* before) {
  clear_error();
  if (history == nullptr || before == nullptr) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "history and before are required");
  }
  return guarded("unknown exception while recording a patch snapshot", [&] {
    history->history.record(before->value);
    return RADICLE_STATUS_OK;
  });
}

radicle_status radicle_plugin_history_undo(radicle_edit_history* history, radicle_plugin* patch) {
  clear_error();
  if (history == nullptr || patch == nullptr) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "history and patch are required");
  }
  return guarded("unknown exception while undoing a patch edit", [&] {
    auto previous = history->history.undo(patch->value);
    if (!previous) return fail(RADICLE_STATUS_INVALID_ARGUMENT, "there is no patch edit to undo");
    patch->value = std::move(*previous);
    return RADICLE_STATUS_OK;
  });
}

radicle_status radicle_plugin_history_redo(radicle_edit_history* history, radicle_plugin* patch) {
  clear_error();
  if (history == nullptr || patch == nullptr) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "history and patch are required");
  }
  return guarded("unknown exception while redoing a patch edit", [&] {
    auto next = history->history.redo(patch->value);
    if (!next) return fail(RADICLE_STATUS_INVALID_ARGUMENT, "there is no patch edit to redo");
    patch->value = std::move(*next);
    return RADICLE_STATUS_OK;
  });
}

radicle_status radicle_plugin_history_undo_count(const radicle_edit_history* history, size_t* out_count) {
  clear_error();
  if (out_count) *out_count = 0;
  if (history == nullptr || out_count == nullptr) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "history and out_count are required");
  }
  *out_count = history->history.undo_count();
  return RADICLE_STATUS_OK;
}

radicle_status radicle_plugin_history_redo_count(const radicle_edit_history* history, size_t* out_count) {
  clear_error();
  if (out_count) *out_count = 0;
  if (history == nullptr || out_count == nullptr) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "history and out_count are required");
  }
  *out_count = history->history.redo_count();
  return RADICLE_STATUS_OK;
}

radicle_status radicle_plugin_history_revision(const radicle_edit_history* history, uint64_t* out_revision) {
  clear_error();
  if (out_revision) *out_revision = 0;
  if (history == nullptr || out_revision == nullptr) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "history and out_revision are required");
  }
  *out_revision = history->history.revision();
  return RADICLE_STATUS_OK;
}

}  // extern "C"
