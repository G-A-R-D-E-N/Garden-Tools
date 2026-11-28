#pragma once

#include "fo4/formid.hpp"
#include "fo4/plugin/container.hpp"
#include "fo4/previs/xcri.hpp"

#include <cstdint>
#include <optional>

namespace fo4::previs {

enum class StampStatus {
  ok,
  record_missing,
  subrecord_error,
};

struct StampRequest {
  std::uint16_t pcmb_stamp{0};
  std::uint16_t visi_stamp{0};
  XcriRaw xcri{};
  // RVIS is a 4-byte file Form-ID of the root cell, encoded against the stamped plugin's master list.
  // Empty leaves the record's RVIS as it is.
  std::optional<RawFormId> rvis_root;
};

StampStatus stamp_cell_previs(Plugin& plugin, const FormKey& cell_key, const StampRequest& req);

}  // namespace fo4::previs
