#include "fo4/forms/cell_relocate_doors.hpp"
#include "fo4/binary/raw.hpp"

#include "fo4/constants.hpp"
#include "fo4/plugin/record_edit.hpp"
#include "fo4/forms/placed_reference.hpp"
#include "fo4/plugin/reader.hpp"
#include "fo4/plugin/visit.hpp"
#include "fo4/signature.hpp"

#include <cmath>
#include <cstddef>
#include <cstring>
#include <exception>
#include <set>
#include <utility>
#include <vector>

namespace fo4::forms {
namespace {

using fo4::binary::raw::read_u32;

constexpr std::size_t k_xtel_short_size = 32;
constexpr std::size_t k_xtel_size = 36;
constexpr std::size_t k_xtel_x_offset = 4;

struct Refusal {
  TeleportTranslateStatus status;
};

void collect_references(const Group& group, std::set<FormKey>& doors) {
  for (const auto& child : group.children) {
    if (const auto* nested = std::get_if<Group>(&child)) {
      collect_references(*nested, doors);
      continue;
    }
    const auto& record = std::get<Record>(child);
    if (record.header.signature == sig_refr && !record.header.flags.is_deleted()) {
      doors.insert(record.form_key);
    }
  }
}

void translate_xy(std::vector<std::byte>& data, float delta_x, float delta_y) {
  const float deltas[2] = {delta_x, delta_y};
  for (std::size_t axis = 0; axis < 2; ++axis) {
    if (deltas[axis] == 0.0f) continue;
    const auto offset = k_xtel_x_offset + axis * sizeof(float);
    float value = 0;
    std::memcpy(&value, data.data() + offset, sizeof(value));
    value += deltas[axis];
    if (!std::isfinite(value)) throw Refusal{TeleportTranslateStatus::malformed};
    std::memcpy(data.data() + offset, &value, sizeof(value));
  }
}

}  // namespace

TeleportTranslateResult translate_incoming_teleports(Plugin& patch,
                                                     const env::Environment& environment,
                                                     const FormKey& cell,
                                                     std::int32_t dx_cells,
                                                     std::int32_t dy_cells) {
  try {
    auto next = patch;
    assign_record_form_keys(next);

    std::set<FormKey> doors;
    {
      auto located = fo4::detail::find_mutable_location(next, cell);
      if (!located.has_value() || located->record().header.signature != sig_cell) {
        return {TeleportTranslateStatus::cell_missing, 0};
      }
      const auto& siblings = located->path.back()->children;
      if (located->index + 1 < siblings.size()) {
        const auto* children = std::get_if<Group>(&siblings[located->index + 1]);
        if (children != nullptr &&
            children->header.group_type == static_cast<std::int32_t>(GroupType::cell_children)) {
          collect_references(*children, doors);
        }
      }
    }
    if (doors.empty() || (dx_cells == 0 && dy_cells == 0)) {
      return {TeleportTranslateStatus::translated, 0};
    }

    struct Source {
      std::size_t plugin;
      FormKey reference;
    };
    std::vector<Source> to_copy;
    for (const auto* ref : environment.winners_of(sig_refr)) {
      if (ref->record->undecodable || ref->record->header.flags.is_deleted()) continue;
      const auto xtel = ref->record->subrecords.first_nonempty_data(sig_xtel);
      if (!xtel.has_value() || xtel->size() < sizeof(std::uint32_t)) continue;
      const auto destination =
          environment.resolve_link(*ref, RawFormId{read_u32(*xtel, 0)});
      if (!doors.contains(destination)) continue;
      if (fo4::find_record(next, ref->record->form_key) != nullptr) continue;
      to_copy.push_back({ref->plugin, ref->record->form_key});
    }
    for (const auto& source : to_copy) {
      try {
        const auto status = copy_placed_reference_as_override(
            next, environment.plugin(source.plugin), source.reference, false);
        if (status != PlacedReferenceCopyStatus::inserted) {
          throw Refusal{TeleportTranslateStatus::copy_failed};
        }
      } catch (const std::exception&) {
        throw Refusal{TeleportTranslateStatus::copy_failed};
      }
    }
    if (!to_copy.empty()) assign_record_form_keys(next);

    const auto context = plugin_form_id_context(next);
    const float delta_x = static_cast<float>(dx_cells) * k_cell_world_units_f32;
    const float delta_y = static_cast<float>(dy_cells) * k_cell_world_units_f32;
    std::size_t updated = 0;
    visit_records(next, [&](Record& record) {
      if (record.header.signature != sig_refr || record.header.flags.is_deleted() ||
          record.undecodable) {
        return;
      }
      std::vector<std::size_t> hits;
      const auto& view = record;
      for (std::size_t index = 0; index < view.subrecords.size(); ++index) {
        const auto& subrecord = view.subrecords[index];
        if (subrecord.signature != sig_xtel || subrecord.data.size() < sizeof(std::uint32_t)) {
          continue;
        }
        const auto destination =
            resolve_plugin_link(RawFormId{read_u32(subrecord.data, 0)}, context);
        if (!doors.contains(destination)) continue;
        if (subrecord.data.size() != k_xtel_short_size && subrecord.data.size() != k_xtel_size) {
          throw Refusal{TeleportTranslateStatus::malformed};
        }
        hits.push_back(index);
      }
      if (hits.empty()) return;
      for (const auto index : hits) translate_xy(record.subrecords[index].data, delta_x, delta_y);
      fo4::detail::mark_record_modified(next, record);
      ++updated;
    });

    if (updated == 0 && to_copy.empty()) return {TeleportTranslateStatus::translated, 0};
    next.original_range.clear();
    if (!to_copy.empty()) next.original_structure_fingerprint = 0;
    patch = std::move(next);
    return {TeleportTranslateStatus::translated, updated};
  } catch (const Refusal& refusal) {
    return {refusal.status, 0};
  } catch (const std::exception&) {
    return {TeleportTranslateStatus::malformed, 0};
  }
}

}  // namespace fo4::forms
