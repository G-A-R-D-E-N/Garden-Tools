#include "c_api_env_internal.hpp"
#include "fo4/c_api_placed_create.h"
#include "fo4/forms/placed_reference.hpp"
#include "fo4/plugin/reader.hpp"

#include <cmath>
#include <cstddef>

using namespace radicle_c_detail;
using namespace radicle_c_env_detail;

extern "C" {

radicle_status radicle_plugin_create_placed_actor(radicle_plugin* plugin,
                                                  radicle_form_key cell,
                                                  radicle_form_key base,
                                                  const float position[3],
                                                  const float rotation[3],
                                                  int32_t persistent,
                                                  int32_t initially_disabled,
                                                  radicle_form_key* out_key) {
  clear_error();
  if (out_key != nullptr) *out_key = {nullptr, 0};
  if (plugin == nullptr || position == nullptr || rotation == nullptr || out_key == nullptr) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "plugin, position, rotation and out_key are required");
  }
  for (std::size_t axis = 0; axis < 3; ++axis) {
    if (!std::isfinite(position[axis]) || !std::isfinite(rotation[axis])) {
      return fail(RADICLE_STATUS_INVALID_ARGUMENT, "position and rotation must be finite");
    }
  }
  const auto cell_key = to_form_key(cell);
  const auto base_key = to_form_key(base);
  if (!cell_key || !base_key) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "a valid cell and base form ID are required");
  }
  if (fo4::find_record(plugin->value, *cell_key) == nullptr) {
    return fail(RADICLE_STATUS_NOT_FOUND, "plugin has no such CELL record");
  }
  return guarded("unknown exception while creating ACHR", [&] {
    try {
      fo4::forms::PlacedReferenceCreation creation;
      creation.cell = *cell_key;
      creation.base = *base_key;
      creation.transform = fo4::forms::PositionRotation{position[0], position[1], position[2],
                                                        rotation[0], rotation[1], rotation[2]};
      creation.persistent = persistent != 0;
      creation.initially_disabled = initially_disabled != 0;
      creation.kind = fo4::forms::PlacedReferenceKind::npc;
      const auto key = fo4::forms::create_placed_reference(plugin->value, creation);
      *out_key = {plugin->value.key.filename().data(), key.local().value()};
      return RADICLE_STATUS_OK;
    } catch (const fo4::forms::PlacedReferenceError& error) {
      return fail_exception(RADICLE_STATUS_INVALID_ARGUMENT, error);
    } catch (const fo4::FormIdError& error) {
      return fail_exception(RADICLE_STATUS_INVALID_ARGUMENT, error);
    }
  });
}

}