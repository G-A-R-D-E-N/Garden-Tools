#include "fo4/plugin/mutation.hpp"

#include "field_writer_internal.hpp"
#include "fo4/plugin/record_edit.hpp"

#include "fo4/plugin/sound_shapes.hpp"
#include "fo4/localization/strings.hpp"
#include "fo4/plugin/reader.hpp"
#include "fo4/plugin/visit.hpp"
#include "fo4/schema/field_address.hpp"
#include "fo4/schema/formlink_lists.hpp"
#include "fo4/schema/localized_fields.hpp"
#include "fo4/schema/record_constraints.hpp"
#include "fo4/schema/scalar.hpp"
#include "fo4/text/encoding.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <iterator>
#include <limits>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace fo4 {
namespace {
using detail::find_mutable_record;
using detail::group_spans;
using detail::mark_record_modified;

void require_scalar_record(const Record& record, const schema::ScalarFieldDescriptor& field) {
  if (record.header.signature != field.record) {
    throw schema::ScalarError(std::string(field.path) + " does not apply to " +
                              record.header.signature.to_string() + " records");
  }
}

const schema::SubrecordGroupMemberDescriptor* group_member(const schema::SubrecordGroupDescriptor& group,
                                                           Signature subrecord) {
  for (const auto& member : group.members) {
    if (member.subrecord == subrecord) return &member;
  }
  return nullptr;
}

bool response_shape_valid(const Record& record) {
  constexpr Signature members[] = {Signature::from_string("NAM1"), Signature::from_string("NAM2"),
                                    Signature::from_string("NAM3"), Signature::from_string("NAM4")};
  std::size_t responses = 0;
  std::size_t counts[4] = {};
  const auto& subs = record.subrecords;
  for (std::size_t i = 0; i < subs.size(); ++i) {
    for (std::size_t m = 0; m < 4; ++m) {
      if (subs[i].signature == members[m]) { ++counts[m]; }
    }
    if (subs[i].signature != sig_trda) { continue; }
    ++responses;
    if (subs[i].data.size() != 20 || subs.size() - i <= 4) { return false; }
    for (std::size_t m = 0; m < 4; ++m) {
      if (subs[i + 1 + m].signature != members[m]) { return false; }
    }
  }
  return std::all_of(counts, counts + 4,
                     [&](std::size_t count) { return count == responses; });
}

schema::FieldSlots scalar_slots(const Record& record, const schema::ScalarFieldDescriptor& field) {
  if (field.record == Signature::from_string("INFO") && field.scalar.address.repeated &&
      !response_shape_valid(record)) {
    throw schema::ScalarError("INFO response fields require complete TRDA,NAM1..NAM4 groups");
  }
  if (const auto group = schema::subrecord_group_for_member(record.header.signature, field.scalar.subrecord)) {
    try {
      static_cast<void>(group_spans(record, *group));
    } catch (const schema::SubrecordGroupError& error) {
      throw schema::ScalarError(error.what());
    }
  }
  try {
    if (record.header.signature == Signature::from_string("SNDR") &&
        (field.scalar.subrecord == Signature::from_string("INTV") ||
         field.scalar.subrecord == Signature::from_string("FNAM"))) {
      static_cast<void>(parse_sound_rate_files(record));
    } else if (record.header.signature == Signature::from_string("SOUN") &&
               field.scalar.subrecord == Signature::from_string("REPT")) {
      static_cast<void>(parse_sound_repeat(record));
    }
  } catch (const RecordShapeError& error) {
    throw schema::ScalarError(error.what());
  }
  auto resolved = schema::resolve_field_slots(record, schema::scalar_field_shape(field.scalar),
                                              field.scalar.address);
  if (resolved.duplicate) {
    throw schema::ScalarError("record contains duplicate scalar subrecords: " +
                              field.scalar.subrecord.to_string());
  }
  if (resolved.malformed) {
    throw schema::ScalarError(std::string(field.scalar.field_name) + " has unexpected binary width");
  }
  return resolved;
}

schema::FieldSlots formlink_slots(const Record& record, const schema::FormLinkFieldDescriptor& field) {
  if (record.header.signature == Signature::from_string("FSTS") &&
      field.subrecord == Signature::from_string("DATA")) {
    try {
      static_cast<void>(parse_footstep_counts(record));
    } catch (const RecordShapeError& error) {
      throw schema::FormLinkFieldError(error.what());
    }
  }
  if (field.record == Signature::from_string("INFO") && field.address.repeated &&
      !response_shape_valid(record)) {
    throw schema::FormLinkFieldError("INFO response fields require complete TRDA,NAM1..NAM4 groups");
  }
  if (const auto group = schema::subrecord_group_for_member(record.header.signature, field.subrecord)) {
    try {
      static_cast<void>(group_spans(record, *group));
    } catch (const schema::SubrecordGroupError& error) {
      throw schema::FormLinkFieldError(error.what());
    }
  }
  schema::FieldShape shape;
  shape.subrecord = field.subrecord;
  shape.offset = field.offset;
  shape.width = sizeof(std::uint32_t);
  shape.min_form_version = field.min_form_version;
  shape.max_form_version = field.max_form_version;
  auto resolved = schema::resolve_field_slots(record, shape, field.address);
  if (resolved.duplicate) {
    throw schema::FormLinkFieldError("record contains duplicate FormLink subrecords: " +
                                     field.subrecord.to_string());
  }
  const bool version_admits =
      record.header.form_version >= field.min_form_version &&
      (field.max_form_version == 0 || record.header.form_version < field.max_form_version);
  if (field.payload_sizes[0] != 0 && version_admits &&
      (!schema::discriminator_guarded(field.address.when) ||
       std::any_of(resolved.slots.begin(), resolved.slots.end(),
                   [](const auto& slot) { return slot.valid || slot.misfit; }))) {
    for (const auto& subrecord : record.subrecords) {
      if (subrecord.signature == field.subrecord &&
          subrecord.data.size() != field.payload_sizes[0] &&
          (field.payload_sizes[1] == 0 || subrecord.data.size() != field.payload_sizes[1])) {
        throw schema::FormLinkFieldError(std::string(field.path) + " has unexpected payload size");
      }
    }
  }
  return resolved;
}

// Constraint: only an absent optional member is created where the authoring order places it; required, forbidden or unlisted members stay refused.
bool insert_authoring_subrecord(Record& record, Signature subrecord, const schema::ScalarDescriptor& scalar) {
  const auto constraint = schema::record_authoring_constraint(record.header.signature);
  if (!constraint) return false;
  const auto rules = schema::authoring_subrecords(*constraint);
  std::size_t wanted = rules.size();
  for (std::size_t rule = 0; rule < rules.size(); ++rule) {
    if (rules[rule].signature == subrecord) {
      wanted = rule;
      break;
    }
  }
  if (wanted == rules.size() || rules[wanted].maximum == 0 || rules[wanted].minimum != 0) return false;
  if (std::any_of(record.subrecords.begin(), record.subrecords.end(),
                  [&](const Subrecord& sub) { return sub.signature == subrecord; })) {
    return false;
  }

  std::size_t payload = scalar.payload_size;
  if (payload == 0) payload = schema::scalar_storage_size(scalar.storage);
  const auto width = schema::scalar_storage_size(scalar.storage);
  if (scalar.embedded && payload < scalar.offset + width) payload = scalar.offset + width;

  const auto rule_of = [&](Signature signature) {
    for (std::size_t rule = 0; rule < rules.size(); ++rule) {
      if (rules[rule].signature == signature) return rule;
    }
    return rules.size();
  };
  std::size_t insert_at = 0;
  bool anchored = false;
  for (std::size_t slot = 0; slot < record.subrecords.size(); ++slot) {
    if (rule_of(record.subrecords[slot].signature) < wanted) {
      insert_at = slot + 1;
      anchored = true;
    }
  }
  if (!anchored) {
    for (std::size_t slot = 0; slot < record.subrecords.size(); ++slot) {
      if (rule_of(record.subrecords[slot].signature) > wanted) {
        insert_at = slot;
        break;
      }
    }
  }
  Subrecord member;
  member.signature = subrecord;
  member.data.assign(payload, std::byte{0});
  record.subrecords.insert(record.subrecords.begin() + static_cast<std::ptrdiff_t>(insert_at), std::move(member));
  return true;
}

// Constraint: the largest shipped WRLD grid is 148996 bytes, so this only stops a bounds edit
// Constraint: from asking for a payload no plugin can carry while keeping the multiplication exact.
constexpr std::uint64_t k_max_world_grid_cells = 1u << 22;

bool is_world_bounds_path(std::string_view path) {
  return path == "WRLD\\BoundsMinX" || path == "WRLD\\BoundsMinY" ||
         path == "WRLD\\BoundsMaxX" || path == "WRLD\\BoundsMaxY";
}

std::optional<std::size_t> single_subrecord(const Record& record, Signature signature) {
  std::optional<std::size_t> found;
  for (std::size_t i = 0; i < record.subrecords.size(); ++i) {
    if (record.subrecords[i].signature != signature) continue;
    if (found) {
      throw schema::ScalarError("record has duplicate " + signature.to_string() + " subrecords");
    }
    found = i;
  }
  return found;
}

float subrecord_float(const Record& record, std::size_t subrecord, std::size_t offset) {
  const auto& data = record.subrecords[subrecord].data;
  if (data.size() < offset + sizeof(float)) {
    throw schema::ScalarError("WRLD bounds subrecord is shorter than eight bytes");
  }
  float value = 0.0f;
  std::memcpy(&value, data.data() + offset, sizeof(value));
  return value;
}

// Invariant: NAM0/NAM9 store world units, 4096 to the cell. xEdit counts the grid from the
// Constraint: element's native value, which TwbFloatDef.ToValue has already scaled by the 1/4096 on the def
// Constraint: (Core/wbDefinitionsCommon.pas wbWorldColumnsCounter, Core/wbInterface.pas TwbFloatDef.ToValue).
constexpr float k_world_units_per_cell = 4096.0f;

std::int64_t world_bound_cells(float value) {
  if (!std::isfinite(value)) {
    throw schema::ScalarError("WRLD bounds must be finite");
  }
  // Invariant: xEdit's wbWorldColumnsCounter treats a Single.MaxValue/MinValue sentinel as 0
// Constraint: and rounds with Delphi's Round, which ties to even like nearbyint's default mode. The
  // Constraint: sentinel is tested on the stored value because xEdit returns it before scaling.
  constexpr float sentinel = std::numeric_limits<float>::max();
  if (value == sentinel || value == -sentinel) return 0;
  return static_cast<std::int64_t>(std::nearbyint(value / k_world_units_per_cell));
}

struct WorldGridSize {
  std::size_t columns;
  std::size_t rows;
};

WorldGridSize world_grid_size(float min_x, float min_y, float max_x, float max_y) {
  const auto columns = world_bound_cells(max_x) - world_bound_cells(min_x) + 1;
  const auto rows = world_bound_cells(max_y) - world_bound_cells(min_y) + 1;
  if (columns <= 0 || rows <= 0) {
    throw schema::ScalarError("WRLD bounds do not describe a grid");
  }
  if (static_cast<std::uint64_t>(columns) * static_cast<std::uint64_t>(rows) >
      k_max_world_grid_cells) {
    throw schema::ScalarError("WRLD bounds describe a grid that is too large");
  }
  return {static_cast<std::size_t>(columns), static_cast<std::size_t>(rows)};
}

struct WorldGridWrite {
  std::size_t subrecord;
  std::vector<std::byte> data;
};

void require_world_grid(const std::vector<std::byte>& payload, WorldGridSize size,
                        Signature signature) {
  constexpr std::size_t k_cell = sizeof(std::uint32_t);
  if (payload.size() % k_cell != 0 || payload.size() != size.columns * size.rows * k_cell) {
    throw schema::ScalarError("WRLD " + signature.to_string() +
                              " does not match its NAM0/NAM9 bounds");
  }
}

std::vector<std::byte> resized_world_grid(const std::vector<std::byte>& payload,
                                          WorldGridSize old_size, WorldGridSize new_size) {
  constexpr std::size_t k_cell = sizeof(std::uint32_t);
  std::vector<std::byte> resized(new_size.columns * new_size.rows * k_cell, std::byte{0});
  const auto rows = std::min(old_size.rows, new_size.rows);
  const auto columns = std::min(old_size.columns, new_size.columns);
  for (std::size_t row = 0; row < rows; ++row) {
    const auto from =
        payload.begin() + static_cast<std::ptrdiff_t>(row * old_size.columns * k_cell);
    const auto to =
        resized.begin() + static_cast<std::ptrdiff_t>(row * new_size.columns * k_cell);
    std::copy_n(from, columns * k_cell, to);
  }
  return resized;
}

// Constraint: Validates the whole resize before write_scalar mutates the record, so a bad bounds value or a malformed existing grid leaves the plugin unchanged. Cells that survive keep their row and column; cells a grow adds are zero and a shrink drops the trailing rows and columns.
std::vector<WorldGridWrite> prepare_world_grid_resize(const Record& record, std::string_view path,
                                                      std::span<const std::byte> encoded) {
  constexpr Signature nam0 = Signature::from_string("NAM0");
  constexpr Signature nam9 = Signature::from_string("NAM9");
  constexpr Signature ofst = Signature::from_string("OFST");
  constexpr Signature clsz = Signature::from_string("CLSZ");
  const auto ofst_index = single_subrecord(record, ofst);
  const auto clsz_index = single_subrecord(record, clsz);
  if (!ofst_index && !clsz_index) return {};
  const auto nam0_index = single_subrecord(record, nam0);
  const auto nam9_index = single_subrecord(record, nam9);
  if (!nam0_index || !nam9_index) {
    throw schema::ScalarError("WRLD cell grids require NAM0 and NAM9 bounds");
  }
  float min_x = subrecord_float(record, *nam0_index, 0);
  float min_y = subrecord_float(record, *nam0_index, 4);
  float max_x = subrecord_float(record, *nam9_index, 0);
  float max_y = subrecord_float(record, *nam9_index, 4);
  const auto old_size = world_grid_size(min_x, min_y, max_x, max_y);
  float written = 0.0f;
  if (encoded.size() != sizeof(written)) {
    throw schema::ScalarError("WRLD bounds must be four bytes");
  }
  std::memcpy(&written, encoded.data(), sizeof(written));
  if (path == "WRLD\\BoundsMinX") min_x = written;
  if (path == "WRLD\\BoundsMinY") min_y = written;
  if (path == "WRLD\\BoundsMaxX") max_x = written;
  if (path == "WRLD\\BoundsMaxY") max_y = written;
  const auto new_size = world_grid_size(min_x, min_y, max_x, max_y);

  std::vector<WorldGridWrite> writes;
  const auto consider = [&](Signature signature, std::optional<std::size_t> index) {
    if (!index) return;
    const auto& payload = record.subrecords[*index].data;
    require_world_grid(payload, old_size, signature);
    if (old_size.columns == new_size.columns && old_size.rows == new_size.rows) return;
    writes.push_back({*index, resized_world_grid(payload, old_size, new_size)});
  };
  consider(ofst, ofst_index);
  consider(clsz, clsz_index);
  return writes;
}

ScalarMutationStatus write_scalar(Plugin& plugin,
                                  const FormKey& record_key,
                                  const schema::ScalarFieldDescriptor& field,
                                  std::size_t index,
                                  std::string_view value,
                                  bool string_setter,
                                  bool create_missing) {
  assign_record_form_keys(plugin);
  auto* target = find_mutable_record(plugin, record_key);
  if (target == nullptr) return ScalarMutationStatus::record_missing;
  require_scalar_record(*target, field);
  if (schema::is_counted_array_count_subrecord(field.record, field.scalar.subrecord)) {
    throw schema::ScalarError(std::string(field.path) + " is maintained by counted array insert and remove");
  }
  if (schema::is_subrecord_group_count_subrecord(field.record, field.scalar.subrecord)) {
    throw schema::ScalarError(std::string(field.path) + " is maintained by subrecord group insert and remove");
  }
  if (schema::is_derived_scalar_row(field.record, field.scalar.subrecord)) {
    if (field.record == Signature::from_string("INNR")) {
      throw schema::ScalarError(std::string(field.path) +
                                " is maintained by the INNR naming-rule and keyword lists");
    }
    throw schema::ScalarError(std::string(field.path) +
                              " is derived from the five footstep category lists in DATA");
  }

  if (schema::is_object_template_count_row(field)) {
    throw schema::ScalarError(std::string(field.path) +
                              " is maintained by the object template include and property entries");
  }

  const bool is_string = field.scalar.storage == schema::ScalarStorage::zstring ||
                         field.scalar.storage == schema::ScalarStorage::fixed_string;
  if (is_string != string_setter) {
    throw schema::ScalarError(std::string(field.path) +
                              (is_string ? " is a string field; use the string setter"
                                         : " is not a string field"));
  }
  const auto member_group = schema::subrecord_group_for_member(field.record, field.scalar.subrecord);
  const bool member_string = is_string && field.scalar.embedded && member_group.has_value();
  if (is_string && field.scalar.embedded && !member_string) {
    throw schema::ScalarError(std::string(field.path) +
                              " sits in a fixed-size slot and cannot be written generically");
  }

  auto resolved = scalar_slots(*target, field);
  if (index >= resolved.slots.size() || !resolved.slots[index].valid) {
    if (!create_missing || !insert_authoring_subrecord(*target, field.scalar.subrecord, field.scalar)) {
      return ScalarMutationStatus::subrecord_missing;
    }
    resolved = scalar_slots(*target, field);
    if (index >= resolved.slots.size() || !resolved.slots[index].valid) {
      return ScalarMutationStatus::subrecord_missing;
    }
  }
  const auto slot = resolved.slots[index];
  auto& data = target->subrecords[slot.subrecord].data;

  if (is_string && plugin.header.tes4_flags.is_localized() &&
      (field.scalar.localized || schema::is_localized_field(field.record, field.scalar.subrecord))) {
    throw schema::ScalarError(std::string(field.path) +
                              " is a localized string in a localized plugin");
  }

  const auto encoded = schema::encode_scalar(value, field.scalar);
  // Invariant: the ALED Alias End marker must stay empty.
  if (field.path == "QUST\\AliasEnd" && !encoded.empty()) {
    throw schema::ScalarError("QUST\\AliasEnd is an empty marker and takes no value");
  }
  if (field.path == "QUST\\NextAliasID") {
    // Invariant: QUST Next Alias ID must stay above every existing alias ID.
    std::uint32_t next_alias_id = 0;
    std::memcpy(&next_alias_id, encoded.data(), sizeof(next_alias_id));
    const auto highest = detail::highest_quest_alias_id(plugin, record_key);
    if (highest && next_alias_id <= *highest) return ScalarMutationStatus::next_alias_id_too_low;
  }
  std::vector<WorldGridWrite> grid_writes;
  if (field.record == Signature::from_string("WRLD") && is_world_bounds_path(field.path)) {
    grid_writes = prepare_world_grid_resize(*target, field.path, encoded);
  }
  std::size_t slot_size = 0;
  {
    const auto existing = schema::scalar_slot_bytes(data, slot, field.scalar);
    static_cast<void>(schema::decode_scalar(existing, field.scalar));
    if (std::equal(existing.begin(), existing.end(), encoded.begin(), encoded.end())) {
      return ScalarMutationStatus::unchanged;
    }
    slot_size = existing.size();
  }

  if (field.scalar.storage == schema::ScalarStorage::bytes) {
    // Constraint: a bytes row owns the whole payload, so a write replaces it at whatever length the value encodes to.
    data = encoded;
  } else if (is_string && member_string) {
    // Constraint: a variable-length member string row that owns the whole payload must resize it to the encoded value; every other member string row requires the encoded value to fit the slot the subrecord already carries.
    const auto* member = group_member(*member_group, field.scalar.subrecord);
    if (member != nullptr && member->payload_size == 0 && slot.offset == 0 &&
        slot_size == data.size()) {
      data = encoded;
    } else {
      if (encoded.size() > slot_size) {
        throw schema::ScalarError(std::string(field.path) + " does not fit in its " +
                                  std::to_string(slot_size) + "-byte slot");
      }
      std::copy(encoded.begin(), encoded.end(),
                data.begin() + static_cast<std::ptrdiff_t>(slot.offset));
      if (encoded.size() < slot_size) {
        std::fill(data.begin() + static_cast<std::ptrdiff_t>(slot.offset + encoded.size()),
                  data.begin() + static_cast<std::ptrdiff_t>(slot.offset + slot_size), std::byte{0});
      }
    }
  } else if (is_string) {
    data = encoded;
  } else {
    std::copy(encoded.begin(), encoded.end(),
              data.begin() + static_cast<std::ptrdiff_t>(slot.offset));
  }
  for (auto& grid : grid_writes) {
    target->subrecords[grid.subrecord].data = std::move(grid.data);
    // Invariant: a grid past 65535 bytes is read with its XXXX size subrecord kept in front of it. The writer
    // Constraint: emits that size from the payload, so the kept copy must follow the resize or the record in
    // Constraint: memory no longer matches what a reread of its own bytes returns.
    if (grid.subrecord == 0) continue;
    auto& prefix = target->subrecords[grid.subrecord - 1];
    if (prefix.signature != Signature::from_string("XXXX") || prefix.data.size() != 4) continue;
    const auto size = static_cast<std::uint32_t>(target->subrecords[grid.subrecord].data.size());
    std::memcpy(prefix.data.data(), &size, sizeof(size));
  }
  mark_record_modified(plugin, *target);
  return ScalarMutationStatus::updated;
}

}

ScalarMutationStatus set_record_scalar(Plugin& plugin,
                                       const FormKey& record_key,
                                       const schema::ScalarFieldDescriptor& field,
                                       std::size_t index,
                                       std::string_view value,
                                       bool create_missing) {
  return write_scalar(plugin, record_key, field, index, value, false, create_missing);
}

ScalarMutationStatus set_record_string(Plugin& plugin,
                                       const FormKey& record_key,
                                       const schema::ScalarFieldDescriptor& field,
                                       std::size_t index,
                                       std::string_view value,
                                       bool create_missing) {
  return write_scalar(plugin, record_key, field, index, value, true, create_missing);
}

ScalarMutationStatus set_record_string(Plugin& plugin,
                                       const FormKey& record_key,
                                       std::string_view path,
                                       std::size_t index,
                                       std::string_view value) {
  const auto field = schema::scalar_field_descriptor(path);
  if (!field) throw schema::ScalarError("unknown string field path: " + std::string(path));
  return write_scalar(plugin, record_key, *field, index, value, true, false);
}

FormLinkFieldMutationStatus set_record_formlink_field(
    Plugin& plugin,
    const FormKey& record_key,
    const schema::FormLinkFieldDescriptor& field,
    std::size_t index,
    const FormKey& target) {
  assign_record_form_keys(plugin);
  auto* record = find_mutable_record(plugin, record_key);
  if (record == nullptr) return FormLinkFieldMutationStatus::record_missing;
  if (record->header.signature != field.record) {
    return FormLinkFieldMutationStatus::record_mismatch;
  }

  if (target.owner().empty() && target.local().value() != 0) {
    throw schema::FormLinkFieldError("FormLink target has no owner");
  }
  if (target.owner().empty() && target.local().value() == 0 && !field.nullable) {
    throw schema::FormLinkFieldError(std::string(field.path) + " does not allow NULL");
  }
  if (!target.owner().empty() && target.owner() == plugin.key) {
    const auto* local_target = find_record(plugin, target);
    if (local_target == nullptr ||
        !schema::formlink_field_target_allowed(field, local_target->header.signature)) {
      throw schema::FormLinkFieldError(std::string(field.path) + " target type is not allowed");
    }
  }

  const auto resolved = formlink_slots(*record, field);
  if (index >= resolved.slots.size()) return FormLinkFieldMutationStatus::subrecord_missing;
  const auto slot = resolved.slots[index];
  if (slot.misfit) {
    throw schema::FormLinkFieldError(std::string(field.path) +
                                     " does not fit in the subrecord payload");
  }
  if (!slot.valid) return FormLinkFieldMutationStatus::subrecord_missing;
  auto& data = record->subrecords[slot.subrecord].data;

  std::uint32_t current_raw = 0;
  std::memcpy(&current_raw, data.data() + slot.offset, sizeof(current_raw));
  const auto encoded_value = encode_plugin_form_id(target, plugin_form_id_context(plugin)).value();
  if (current_raw == encoded_value) return FormLinkFieldMutationStatus::unchanged;

  std::memcpy(data.data() + slot.offset, &encoded_value, sizeof(encoded_value));
  mark_record_modified(plugin, *record);
  return FormLinkFieldMutationStatus::updated;
}

std::optional<std::size_t> record_field_count(const Plugin& plugin,
                                              const FormKey& record_key,
                                              const schema::ScalarFieldDescriptor& field) {
  const auto* record = find_record(plugin, record_key);
  if (record == nullptr) return std::nullopt;
  require_scalar_record(*record, field);
  return scalar_slots(*record, field).slots.size();
}

bool record_scalar_is_localized_string(const Plugin& plugin,
                                       const schema::ScalarFieldDescriptor& field) {
  const bool is_string = field.scalar.storage == schema::ScalarStorage::zstring ||
                         field.scalar.storage == schema::ScalarStorage::fixed_string;
  return is_string && plugin.header.tes4_flags.is_localized() &&
         (field.scalar.localized || schema::is_localized_field(field.record, field.scalar.subrecord));
}

std::optional<std::uint32_t> record_localized_string_id(const Plugin& plugin,
                                                       const Record& record,
                                                       const schema::ScalarFieldDescriptor& field,
                                                       std::size_t index) {
  require_scalar_record(record, field);
  if (!record_scalar_is_localized_string(plugin, field)) {
    throw schema::ScalarError(std::string(field.path) +
                              " is not a localized string in a localized plugin");
  }
  const auto resolved = scalar_slots(record, field);
  if (index >= resolved.slots.size() || !resolved.slots[index].valid) return std::nullopt;
  const auto& slot = resolved.slots[index];
  const auto& data = record.subrecords[slot.subrecord].data;
  const auto offset = field.scalar.embedded ? slot.offset : std::size_t{0};
  if (data.size() < offset + sizeof(std::uint32_t) ||
      (!field.scalar.embedded && data.size() != sizeof(std::uint32_t))) {
    throw schema::ScalarError(std::string(field.path) + " localized string id is not 4 bytes");
  }
  std::uint32_t id = 0;
  std::memcpy(&id, data.data() + offset, sizeof(id));
  return id;
}

static bool localized_id_used_elsewhere(const Plugin& plugin,
                                  const Record& own,
                                  std::size_t own_subrecord,
                                  std::size_t own_offset,
                                  localization::StringTableKind kind,
                                  std::uint32_t id) {
  bool used = false;
  visit_records(plugin, [&](const Record& record) {
    if (used || record.header.signature == sig_tes4) return;
    for (std::size_t i = 0; i < record.subrecords.size(); ++i) {
      const auto& subrecord = record.subrecords[i];
      if (&record == &own && i == own_subrecord) continue;
      if (subrecord.data.size() != sizeof(id) ||
          !schema::is_localized_field(record.header.signature, subrecord.signature) ||
          localization::string_table_kind(record.header.signature, subrecord.signature) != kind) {
        continue;
      }
      std::uint32_t other = 0;
      std::memcpy(&other, subrecord.data.data(), sizeof(other));
      if (other == id) {
        used = true;
        return;
      }
    }
    for (const auto& field : schema::scalar_fields(record.header.signature)) {
      const auto& scalar = field.scalar;
      const bool is_string = scalar.storage == schema::ScalarStorage::zstring ||
                             scalar.storage == schema::ScalarStorage::fixed_string;
      if (!is_string || !(scalar.localized || schema::is_localized_field(field.record, scalar.subrecord)) ||
          localization::string_table_kind(record.header.signature, scalar.subrecord) != kind) {
        continue;
      }
      const auto resolved = schema::resolve_field_slots(record, schema::scalar_field_shape(scalar), scalar.address);
      for (const auto& slot : resolved.slots) {
        const auto offset = scalar.embedded ? slot.offset : std::size_t{0};
        const auto& data = record.subrecords[slot.subrecord].data;
        if (!slot.valid || (&record == &own && slot.subrecord == own_subrecord && offset == own_offset) ||
            data.size() < offset + sizeof(id)) {
          continue;
        }
        std::uint32_t other = 0;
        std::memcpy(&other, data.data() + offset, sizeof(other));
        if (other == id) {
          used = true;
          return;
        }
      }
    }
  });
  return used;
}

ScalarMutationStatus set_record_localized_string(Plugin& plugin,
                                                 const FormKey& record_key,
                                                 const schema::ScalarFieldDescriptor& field,
                                                 std::size_t index,
                                                 std::string_view stored_text,
                                                 localization::LocalizationTables& tables) {
  auto* record = find_mutable_record(plugin, record_key);
  if (record == nullptr) return ScalarMutationStatus::record_missing;
  const auto current = record_localized_string_id(plugin, *record, field, index);
  if (!current) return ScalarMutationStatus::subrecord_missing;

  auto& table = localization::table_for(tables, localization::string_table_kind(record->header.signature,
                                                                                field.scalar.subrecord));
  std::uint32_t id = 0;
  if (!stored_text.empty()) {
    // Constraint: an id is rewritten in place only when it is in this plugin's own table and no other localized subrecord of the plugin uses it; any other id, including a master's, gets a fresh entry.
    const bool own_id = *current != 0 && localization::find_string(table, *current) != nullptr;
    bool shared = false;
    if (own_id) {
      // Constraint: the shared-id scan walks the whole plugin, so it must run only for an id found in this plugin's own table.
      const auto own_slot = scalar_slots(*record, field).slots[index];
      shared = localized_id_used_elsewhere(plugin, *record, own_slot.subrecord,
                                           field.scalar.embedded ? own_slot.offset : std::size_t{0}, table.kind,
                                           *current);
    }
    if (own_id && !shared) {
      static_cast<void>(localization::set_string(table, *current, stored_text));
      id = *current;
    } else {
      id = localization::add_string(table, stored_text);
    }
  }
  if (id == *current) return ScalarMutationStatus::unchanged;

  const auto resolved = scalar_slots(*record, field);
  const auto offset = field.scalar.embedded ? resolved.slots[index].offset : std::size_t{0};
  std::memcpy(record->subrecords[resolved.slots[index].subrecord].data.data() + offset, &id, sizeof(id));
  mark_record_modified(plugin, *record);
  return ScalarMutationStatus::updated;
}

std::optional<std::string> record_scalar_text(const Plugin& plugin,
                                              const Record& record,
                                              const schema::ScalarFieldDescriptor& field,
                                              std::size_t index) {
  require_scalar_record(record, field);
  const bool is_string = field.scalar.storage == schema::ScalarStorage::zstring ||
                         field.scalar.storage == schema::ScalarStorage::fixed_string;
  const auto resolved = scalar_slots(record, field);
  if (index >= resolved.slots.size() || !resolved.slots[index].valid) return std::nullopt;
  if (record_scalar_is_localized_string(plugin, field)) {
    throw schema::ScalarError(std::string(field.path) +
                              " is a localized string in a localized plugin");
  }
  const auto& slot = resolved.slots[index];
  auto decoded = schema::decode_scalar(
      schema::scalar_slot_bytes(record.subrecords[slot.subrecord].data, slot, field.scalar),
      field.scalar);
  return is_string ? text::windows1252_to_utf8(decoded) : decoded;
}

std::size_t record_field_count(const Record& record, const schema::ScalarFieldDescriptor& field) {
  require_scalar_record(record, field);
  return scalar_slots(record, field).slots.size();
}

std::optional<std::vector<std::byte>> record_scalar_bytes(const Record& record,
                                                         const schema::ScalarFieldDescriptor& field,
                                                         std::size_t index) {
  require_scalar_record(record, field);
  const auto resolved = scalar_slots(record, field);
  if (index >= resolved.slots.size() || !resolved.slots[index].valid) return std::nullopt;
  const auto& slot = resolved.slots[index];
  const auto bytes =
      schema::scalar_slot_bytes(record.subrecords[slot.subrecord].data, slot, field.scalar);
  return std::vector<std::byte>(bytes.begin(), bytes.end());
}

std::optional<std::size_t> record_field_count(const Plugin& plugin,
                                              const FormKey& record_key,
                                              const schema::FormLinkFieldDescriptor& field) {
  const auto* record = find_record(plugin, record_key);
  if (record == nullptr) return std::nullopt;
  if (record->header.signature != field.record) {
    throw schema::FormLinkFieldError(std::string(field.path) + " does not apply to " +
                                     record->header.signature.to_string() + " records");
  }
  return formlink_slots(*record, field).slots.size();
}

std::optional<RawFormId> record_formlink_field_raw(const Record& record,
                                                   const schema::FormLinkFieldDescriptor& field,
                                                   std::size_t index) {
  if (record.header.signature != field.record) {
    throw schema::FormLinkFieldError(std::string(field.path) + " does not apply to " +
                                     record.header.signature.to_string() + " records");
  }
  const auto resolved = formlink_slots(record, field);
  if (index >= resolved.slots.size()) return std::nullopt;
  const auto& slot = resolved.slots[index];
  const auto& data = record.subrecords[slot.subrecord].data;
  // Constraint: the admitted short payload omits the optional trailing slot, so a slot past its end must read as a null link at that index.
  if (slot.misfit && field.payload_sizes[1] != 0 && data.size() == field.payload_sizes[1]) {
    return RawFormId{0};
  }
  if (!slot.valid) return std::nullopt;
  if (slot.offset + sizeof(std::uint32_t) > data.size()) return std::nullopt;
  std::uint32_t raw = 0;
  std::memcpy(&raw, data.data() + slot.offset, sizeof(raw));
  return RawFormId{raw};
}

std::optional<std::size_t> record_field_count(const Plugin& plugin,
                                              const FormKey& record_key,
                                              std::string_view path) {
  if (const auto scalar = schema::scalar_field_descriptor(path)) {
    return record_field_count(plugin, record_key, *scalar);
  }
  if (const auto link = schema::formlink_field_descriptor(path)) {
    return record_field_count(plugin, record_key, *link);
  }
  if (const auto list = schema::formlink_list_descriptor(
          Signature::from_string(path.substr(0, 4)), path)) {
    return schema::record_formlink_list_size(plugin, record_key, *list);
  }
  throw schema::ScalarError("unknown field path: " + std::string(path));
}

}
