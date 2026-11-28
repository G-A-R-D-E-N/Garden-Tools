#include "fo4/plugin/scene_writer.hpp"

#include "fo4/plugin/record_edit.hpp"
#include "scene_writer_internal.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <optional>

namespace fo4 {
namespace {

Signature tag(const char* name) { return Signature::from_string(name); }

struct CameraScope {
  std::size_t begin;
  std::size_t end;
  std::optional<std::size_t> camera;
};

std::optional<CameraScope> camera_scope(const Record& record, std::size_t action) {
  const auto layout = detail::checked_scene_layout(record);
  if (action >= layout.actions.size()) return std::nullopt;
  const auto& group = layout.actions[action];
  const auto& type = record.subrecords[group.open].data;
  if (type.size() != 2 || type[0] != std::byte{0} || type[1] != std::byte{0}) return std::nullopt;
  CameraScope result{group.header_end, group.close, std::nullopt};
  for (auto i = result.begin; i < result.end; ++i) {
    const auto& sub = record.subrecords[i];
    if (sub.signature != tag("CRIS")) continue;
    if (result.camera || sub.data.size() != 8) throw schema::FormLinkFieldError("dialogue camera is malformed");
    float fov, rate;
    std::memcpy(&fov, sub.data.data(), 4); std::memcpy(&rate, sub.data.data() + 4, 4);
    if (!std::isfinite(fov) || !std::isfinite(rate)) throw schema::FormLinkFieldError("dialogue camera is not finite");
    result.camera = i;
  }
  return result;
}

std::optional<std::size_t> camera_insert_anchor(const Record& record, const CameraScope& scope) {
  const std::array known{tag("DATA"), tag("DMAX"), tag("DMIN"), tag("DEMO"), tag("DEVA"),
                         tag("HTID"), tag("VENC"), tag("ONAM"), tag("PNAM")};
  std::array<bool, 9> seen{};
  auto anchor = scope.end;
  for (auto i = scope.begin; i < scope.end; ++i) {
    const auto& sub = record.subrecords[i];
    const auto found = std::find(known.begin(), known.end(), sub.signature);
    if (found == known.end()) return std::nullopt;
    const auto index = static_cast<std::size_t>(found - known.begin());
    if (seen[index] || (sub.signature == tag("HTID") ? sub.data.size() % 4 != 0 : sub.data.size() != 4)) return std::nullopt;
    if (index >= 3 && anchor == scope.end) anchor = i;
    if (index < 3 && anchor != scope.end) return std::nullopt;
    seen[index] = true;
  }
  return anchor;
}

}

std::optional<SceneActionCameraState> read_scene_action_camera(const Plugin& plugin, const FormKey& scene,
                                                               std::size_t action) {
  const auto* record = find_record(plugin, scene);
  if (!record || record->header.signature != tag("SCEN")) return std::nullopt;
  require_decodable(*record);
  const auto scope = camera_scope(*record, action);
  if (!scope) return std::nullopt;
  SceneActionCameraState result;
  if (scope->camera) {
    SceneActionCamera value;
    const auto& data = record->subrecords[*scope->camera].data;
    static_assert(sizeof(float) == 4);
    std::memcpy(&value.fov, data.data(), 4); std::memcpy(&value.rate, data.data() + 4, 4);
    result.value = value;
  }
  return result;
}

FormLinkFieldMutationStatus set_scene_action_camera(Plugin& plugin, const FormKey& scene, std::size_t action,
                                                    std::optional<SceneActionCamera> value) {
  auto* record = detail::locate_mutable_record(plugin, scene);
  if (!record) return FormLinkFieldMutationStatus::record_missing;
  if (record->header.signature != tag("SCEN")) return FormLinkFieldMutationStatus::record_mismatch;
  const auto scope = camera_scope(*record, action);
  if (!scope) return FormLinkFieldMutationStatus::subrecord_missing;
  std::vector<std::byte> bytes;
  if (value) {
    if (!std::isfinite(value->fov) || !std::isfinite(value->rate) ||
        std::abs(value->fov) > 1e9f || std::abs(value->rate) > 1e9f)
      throw schema::FormLinkFieldError("dialogue camera values are outside the supported float range");
    bytes.resize(8);
    std::memcpy(bytes.data(), &value->fov, 4); std::memcpy(bytes.data() + 4, &value->rate, 4);
  }
  if ((!value && !scope->camera) || (value && scope->camera && record->subrecords[*scope->camera].data == bytes))
    return FormLinkFieldMutationStatus::unchanged;
  std::optional<std::size_t> anchor;
  if (value && !scope->camera) {
    anchor = camera_insert_anchor(*record, *scope);
    if (!anchor) return FormLinkFieldMutationStatus::subrecord_missing;
  }
  auto updated = record->subrecords;
  if (!value) updated.erase(updated.begin() + static_cast<std::ptrdiff_t>(*scope->camera));
  else if (scope->camera) updated[*scope->camera].data = std::move(bytes);
  else updated.insert(updated.begin() + static_cast<std::ptrdiff_t>(*anchor), Subrecord{tag("CRIS"), std::move(bytes), false});
  record->subrecords = std::move(updated);
  detail::mark_record_modified(plugin, *record);
  return FormLinkFieldMutationStatus::updated;
}

}
