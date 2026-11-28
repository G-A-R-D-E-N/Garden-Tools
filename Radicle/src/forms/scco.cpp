#include "fo4/forms/scco.hpp"

#include "fo4/plugin/reader.hpp"
#include "fo4/plugin/record_edit.hpp"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace fo4::forms {
namespace {

constexpr auto k_scco = Signature::from_string("SCCO");
constexpr auto k_scen = Signature::from_string("SCEN");
constexpr auto k_edid = Signature::from_string("EDID");
constexpr auto k_qnam = Signature::from_string("QNAM");
constexpr auto k_snam = Signature::from_string("SNAM");
constexpr auto k_xnam = Signature::from_string("XNAM");
constexpr auto k_vnam = Signature::from_string("VNAM");

std::vector<std::byte> write_u32(std::uint32_t value) {
  std::vector<std::byte> data(sizeof(value));
  std::memcpy(data.data(), &value, sizeof(value));
  return data;
}

std::vector<std::byte> write_coordinates(std::int32_t x, std::int32_t y) {
  std::vector<std::byte> data(8);
  std::memcpy(data.data(), &x, sizeof(x));
  std::memcpy(data.data() + sizeof(x), &y, sizeof(y));
  return data;
}

// Invariant: every Scene Layout SNAM is immediately followed by its 8-byte XNAM and the run
// Constraint: ends at the first VNAM. A malformed run is refused rather than guessing an insertion point.
std::size_t scene_run_end(const Record& record) {
  const auto& subrecords = record.subrecords;
  std::size_t pos = 0;
  if (pos < subrecords.size() && subrecords[pos].signature == k_edid) ++pos;
  if (pos < subrecords.size() && subrecords[pos].signature == k_qnam) ++pos;

  while (pos < subrecords.size() && subrecords[pos].signature == k_snam) {
    if (pos + 1 >= subrecords.size() || subrecords[pos + 1].signature != k_xnam ||
        subrecords[pos + 1].data.size() != 8) {
      throw SccoError("SCCO SNAM scene entry is missing its 8-byte XNAM coordinates");
    }
    pos += 2;
  }

  if (pos < subrecords.size() && subrecords[pos].signature != k_vnam) {
    throw SccoError("SCCO scene layout is not followed by a VNAM marker");
  }
  for (std::size_t index = pos; index < subrecords.size(); ++index) {
    if (subrecords[index].signature == k_snam) {
      throw SccoError("SCCO SNAM scene entry appears outside the Scene Layout run");
    }
  }
  return pos;
}

std::optional<std::size_t> scene_pair_position(const Record& record,
                                               std::size_t scene_index) {
  const auto end = scene_run_end(record);
  std::size_t pos = 0;
  if (pos < end && record.subrecords[pos].signature == k_edid) ++pos;
  if (pos < end && record.subrecords[pos].signature == k_qnam) ++pos;
  std::size_t index = 0;
  while (pos < end) {
    if (index == scene_index) return pos;
    ++index;
    pos += 2;
  }
  return std::nullopt;
}

Record* use_scco_record(Plugin& plugin, const FormKey& scco) {
  assign_record_form_keys(plugin);
  auto* record = detail::find_mutable_record(plugin, scco);
  if (record == nullptr) return nullptr;
  if (record->header.signature != k_scco) {
    throw SccoError("record is not SCCO: " + scco.to_string());
  }
  return record;
}

void require_scene_target(const Plugin& plugin, const FormKey& scene) {
  if (scene.owner().empty()) {
    throw SccoError("SCCO scene link has no owning plugin");
  }
  if (scene.local().value() == 0) {
    throw SccoError("SCCO scene link does not allow NULL");
  }
  if (scene.owner() != plugin.key) return;
  const auto* target = find_record(plugin, scene);
  if (target == nullptr) {
    throw SccoError("SCCO scene link target is missing from this plugin: " + scene.to_string());
  }
  if (target->header.signature != k_scen) {
    throw SccoError("SCCO scene link target " + scene.to_string() + " is " +
                    target->header.signature.to_string() + ", not SCEN");
  }
}

}  // namespace

bool add_scco_scene(Plugin& plugin,
                    const FormKey& scco,
                    const FormKey& scene,
                    std::int32_t x,
                    std::int32_t y) {
  auto next = plugin;
  auto* record = use_scco_record(next, scco);
  if (record == nullptr) return false;

  const auto at = scene_run_end(*record);
  require_scene_target(next, scene);

  std::uint32_t scene_id = 0;
  try {
    scene_id = encode_plugin_form_id(scene, plugin_form_id_context(next)).value();
  } catch (const FormIdError& error) {
    throw SccoError(std::string("SCCO scene FormLink cannot be encoded: ") + error.what());
  }

  // Constraint: both subrecords share one insertion so the layout never holds a half pair.
  const std::vector<Subrecord> pair{Subrecord{k_snam, write_u32(scene_id), false},
                                    Subrecord{k_xnam, write_coordinates(x, y), false}};
  record->subrecords.insert(
      record->subrecords.begin() + static_cast<std::ptrdiff_t>(at), pair.begin(), pair.end());
  detail::mark_record_modified(next, *record);
  plugin = std::move(next);
  return true;
}

bool remove_scco_scene(Plugin& plugin, const FormKey& scco, std::size_t scene_index) {
  auto next = plugin;
  auto* record = use_scco_record(next, scco);
  if (record == nullptr) return false;

  const auto at = scene_pair_position(*record, scene_index);
  if (!at.has_value()) return false;
  // Constraint: erase only the SNAM and its adjacent XNAM; the VNAM markers and Original Coordinates tail must stay in place.
  record->subrecords.erase(
      record->subrecords.begin() + static_cast<std::ptrdiff_t>(*at),
      record->subrecords.begin() + static_cast<std::ptrdiff_t>(*at + 2));
  detail::mark_record_modified(next, *record);
  plugin = std::move(next);
  return true;
}

}  // namespace fo4::forms
