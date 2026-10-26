#pragma once

#include "fo4/plugin/mutation.hpp"
#include "fo4/plugin/reader.hpp"
#include "fo4/plugin/visit.hpp"
#include "fo4/plugin/writer.hpp"
#include "fo4/schema/field_address.hpp"
#include "fo4/schema/formlink_lists.hpp"
#include "fo4/schema/scalar.hpp"
#include "real_data_build.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <filesystem>
#include <iostream>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace row_roundtrip {

using fo4::Signature;
using fo4::schema::ScalarFieldDescriptor;
using fo4::schema::ScalarStorage;

struct SubrecordBytes {
  Signature signature{};
  std::vector<std::byte> data;
};

inline std::vector<SubrecordBytes> snapshot(const fo4::Record& record) {
  std::vector<SubrecordBytes> out;
  out.reserve(record.subrecords.size());
  for (const auto& subrecord : record.subrecords) {
    out.push_back({subrecord.signature, subrecord.data});
  }
  return out;
}

inline bool same_record(const std::vector<SubrecordBytes>& before, const fo4::Record& after) {
  if (before.size() != after.subrecords.size()) return false;
  for (std::size_t index = 0; index < before.size(); ++index) {
    if (before[index].signature != after.subrecords[index].signature) return false;
    if (before[index].data != after.subrecords[index].data) return false;
  }
  return true;
}

inline bool same_snapshot(const std::vector<SubrecordBytes>& left,
                          const std::vector<SubrecordBytes>& right) {
  if (left.size() != right.size()) return false;
  for (std::size_t index = 0; index < left.size(); ++index) {
    if (left[index].signature != right[index].signature) return false;
    if (left[index].data != right[index].data) return false;
  }
  return true;
}

inline std::optional<fo4::schema::FieldSlot> slot_of(const fo4::Record& record,
                                                     const ScalarFieldDescriptor& field,
                                                     std::size_t index) {
  const auto resolved = fo4::schema::resolve_field_slots(
      record, fo4::schema::scalar_field_shape(field.scalar), field.scalar.address);
  if (resolved.duplicate || resolved.malformed || index >= resolved.slots.size()) {
    return std::nullopt;
  }
  const auto slot = resolved.slots[index];
  if (!slot.valid) return std::nullopt;
  return slot;
}

// Constraint: a row decodes the slot it resolves to, so a repeated or strided row reads the element its index names.
inline std::string decode_row(const fo4::Record& record,
                             const ScalarFieldDescriptor& field,
                             std::size_t index = 0) {
  const auto slot = slot_of(record, field, index);
  if (!slot.has_value() || slot->subrecord >= record.subrecords.size()) return {};
  const auto bytes = fo4::schema::scalar_slot_bytes(record.subrecords[slot->subrecord].data,
                                                    *slot, field.scalar);
  if (bytes.empty()) return {};
  return fo4::schema::decode_scalar(bytes, field.scalar);
}

struct EditedSpan {
  std::size_t subrecord{0};
  std::size_t offset{0};
  std::size_t width{0};
  std::string_view path;
};

inline std::size_t row_width(const fo4::Record& record,
                            const ScalarFieldDescriptor& field,
                            const fo4::schema::FieldSlot& slot) {
  if (slot.subrecord >= record.subrecords.size()) return 0;
  const auto width = fo4::schema::scalar_storage_size(field.scalar.storage);
  return width == 0 ? record.subrecords[slot.subrecord].data.size() : width;
}

inline bool overlaps(const EditedSpan& left, const EditedSpan& right) {
  if (left.subrecord != right.subrecord) return false;
  if (left.width == 0 || right.width == 0) return left.offset == right.offset;
  return left.offset < right.offset + right.width && right.offset < left.offset + left.width;
}

inline std::vector<std::string> candidates(const ScalarFieldDescriptor& field) {
  std::vector<std::string> out;
  if (!field.scalar.names.empty()) {
    for (const auto& name : field.scalar.names) out.push_back(std::to_string(name.value));
    return out;
  }
  if (field.scalar.storage == ScalarStorage::f32) {
    out = {"0.5", "-0.5", "2.5", "1.5"};
    return out;
  }
  out = {"1", "2", "3", "0", "7", "42"};
  return out;
}

inline bool inside_bounds(const ScalarFieldDescriptor& field, const std::string& value) {
  try {
    const auto number = std::stod(value);
    return number >= field.scalar.minimum && number <= field.scalar.maximum;
  } catch (const std::exception&) {
    return false;
  }
}

struct Tally {
  std::size_t rows = 0;
  std::size_t exercised = 0;
  std::size_t whole_payload = 0;
  std::size_t derived = 0;
  std::size_t absent = 0;
  std::size_t unresolved = 0;
  std::size_t shared = 0;
  std::size_t no_candidate = 0;
};

// Constraint: every skip is logged with its reason so a family that stops proving a row is visible in the log.
inline void skip_note(const char* family, std::string_view path, std::string_view reason) {
  std::clog << family << ": skipped " << path << " (" << reason << ")\n";
}

// Constraint: one tally line per family, so a floor has a number in the log to be read from.
inline void tally_note(const char* family, const Tally& tally) {
  std::clog << family << ": rows=" << tally.rows << " exercised=" << tally.exercised
            << " shared=" << tally.shared << " no_candidate=" << tally.no_candidate
            << " unresolved=" << tally.unresolved << " absent=" << tally.absent
            << " derived=" << tally.derived << " whole_payload=" << tally.whole_payload << "\n";
}

inline void roundtrip_family(const char* family, Signature signature) {
  const auto* directory = std::getenv("FO4_DATA_DIR");
  if (directory == nullptr) {
    GTEST_SKIP() << "set FO4_DATA_DIR to run official-master coverage";
  }
  record_game_build(detect_game_build(directory));
  const auto path = std::filesystem::path(directory) / "Fallout4.esm";
  ASSERT_TRUE(std::filesystem::is_regular_file(path)) << path.string();
  const auto plugin = fo4::read_plugin_file(path);
  const auto rows = fo4::schema::scalar_fields(signature);
  ASSERT_FALSE(rows.empty()) << family;
  std::vector<fo4::FormKey> keys;
  fo4::visit_records(plugin, [&](const fo4::Record& record) {
    if (record.header.signature == signature) keys.push_back(record.form_key);
  });
  ASSERT_FALSE(keys.empty()) << family << " has no record of its signature";

  auto edited = fo4::read_plugin_file(path);
  Tally tally;
  struct EditedRow {
    ScalarFieldDescriptor field;
    fo4::FormKey key;
    std::string expected;
  };
  struct EditedRecord {
    fo4::FormKey key;
    std::vector<SubrecordBytes> before;
    std::vector<EditedSpan> spans;
  };
  std::vector<EditedRow> edited_rows;
  std::vector<EditedRecord> edited_records;
  std::size_t carried = 0;
  for (const auto& field : rows) {
    ++tally.rows;
    if (field.record == Signature::from_string("ARMA") && field.scalar.subrecord == Signature::from_string("BSMP")) {
      ++tally.no_candidate;
      skip_note(family, field.path, "structural row: sculpt entries repeat BSMB and BSMS pairs");
      continue;
    }
    if (field.scalar.storage == ScalarStorage::zstring ||
        field.scalar.storage == ScalarStorage::fixed_string ||
        field.scalar.storage == ScalarStorage::bytes) {
      ++tally.whole_payload;
      skip_note(family, field.path,
                "whole-payload row cannot prove a resize here; EDID and INFO NAM9 have their own entries");
      continue;
    }
    // Constraint: a derived count row is owned by the list that writes it, and set_record_scalar refuses it.
    if (fo4::schema::is_derived_scalar_row(field.record, field.scalar.subrecord)) {
      ++tally.derived;
      skip_note(family, field.path, "derived count row, written by its list");
      continue;
    }

    // Constraint: a row runs on any record that carries it, so a record is not fixed before the rows are seen.
    std::optional<fo4::FormKey> chosen;
    std::optional<EditedSpan> chosen_span;
    bool row_carried = false, row_resolved = false, capped = false;
    std::size_t carriers = 0;
    for (const auto& key : keys) {
      if (capped) break;
      const auto count = fo4::record_field_count(edited, key, field);
      if (!count || *count == 0) continue;
      row_carried = true;
      capped = ++carriers > 8;
      if (capped) break;
      const auto* current = fo4::find_record(edited, key);
      ASSERT_NE(current, nullptr);
      const auto candidate_slot = slot_of(*current, field, 0);
      if (!candidate_slot.has_value()) continue;
      row_resolved = true;
      const EditedSpan span{candidate_slot->subrecord, candidate_slot->offset,
                            row_width(*current, field, *candidate_slot), field.path};
      const auto taken = std::find_if(
          edited_records.begin(), edited_records.end(),
          [&](const EditedRecord& record) {
            if (record.key != key) return false;
            return std::any_of(record.spans.begin(), record.spans.end(),
                               [&](const EditedSpan& earlier) { return overlaps(earlier, span); });
          });
      if (taken != edited_records.end()) continue;
      chosen = key;
      chosen_span = span;
      break;
    }
    if (!row_carried) {
      ++tally.absent;
      skip_note(family, field.path, "no record carries the row");
      continue;
    }
    ++carried;
    if (!chosen.has_value()) {
      ++(capped ? tally.no_candidate : tally.unresolved);
      skip_note(family, field.path,
                capped ? "key scan stopped after eight carriers without a free record"
                       : row_resolved ? "bytes already written by an earlier row on every record carrying it"
                                      : "row resolves no slot on the records carrying it");
      continue;
    }
    const auto key = *chosen;
    const auto span = *chosen_span;
    auto record = std::find_if(edited_records.begin(), edited_records.end(),
                               [&](const EditedRecord& candidate) { return candidate.key == key; });
    if (record == edited_records.end()) {
      const auto* live = fo4::find_record(edited, key);
      ASSERT_NE(live, nullptr);
      edited_records.push_back({key, snapshot(*live), {}});
      record = edited_records.end() - 1;
    }
    const auto* current = fo4::find_record(edited, key);
    ASSERT_NE(current, nullptr);
    const auto original = decode_row(*current, field);
    std::string applied, decoded;
    for (const auto& candidate : candidates(field)) {
      if (!inside_bounds(field, candidate) || candidate == original) continue;
      if (fo4::set_record_scalar(edited, key, field, 0, candidate) !=
          fo4::ScalarMutationStatus::updated) {
        skip_note(family, field.path, "setter refused " + candidate);
        continue;
      }
      const auto* probe = fo4::find_record(edited, key);
      ASSERT_NE(probe, nullptr);
      decoded = decode_row(*probe, field);
      if (!decoded.empty() && decoded != original) {
        applied = candidate;
        break;
      }
      ASSERT_EQ(fo4::set_record_scalar(edited, key, field, 0, original),
                fo4::ScalarMutationStatus::updated)
          << family << " could not revert a rejected candidate for " << field.path;
    }
    if (applied.empty()) {
      ++tally.no_candidate;
      skip_note(family, field.path, "no candidate decodes to a new value");
      continue;
    }
    // Constraint: the write must move the bytes at the subrecord and offset the descriptor declares.
    const auto* probe = fo4::find_record(edited, key);
    ASSERT_NE(probe, nullptr);
    ASSERT_LT(span.subrecord, probe->subrecords.size());
    const auto& payload = probe->subrecords[span.subrecord].data;
    EXPECT_EQ(probe->subrecords[span.subrecord].signature, field.scalar.subrecord) << field.path;
    const auto encoded = fo4::schema::encode_scalar(applied, field.scalar);
    ASSERT_LE(span.offset + encoded.size(), payload.size()) << field.path;
    EXPECT_TRUE(std::equal(encoded.begin(), encoded.end(),
                           payload.begin() + static_cast<std::ptrdiff_t>(span.offset)))
        << family << " wrote " << field.path << " off its declared slot";
    if (field.scalar.address.stride == 0 && field.scalar.address.array_offset == 0 &&
        field.scalar.address.segment.count == 0) {
      EXPECT_EQ(span.offset, field.scalar.offset) << family << " " << field.path;
    }
    record->spans.push_back(span);
    edited_rows.push_back({field, key, decoded});
    ++tally.exercised;
  }
  ASSERT_GT(tally.exercised, 0u)
      << family
      << " exercised no row; read exercised/shared/unresolved/no_candidate from the first CI log with "
         "FO4_DATA_DIR set before setting a floor for this family";
  EXPECT_EQ(tally.unresolved, 0u) << family << " has a carried row that resolves no slot";
  // Constraint: a carried row is either edited or skipped for a reason the harness cannot avoid, so none vanishes.
  EXPECT_EQ(tally.exercised + tally.shared + tally.unresolved + tally.no_candidate, carried)
      << family << " dropped a carried row without a reason";
  EXPECT_EQ(tally.rows, carried + tally.absent + tally.derived + tally.whole_payload)
      << family << " counted rows do not add up";
  // Constraint: a carried row is never skipped for bytes an earlier row wrote, so the record choice must free it.
  EXPECT_EQ(tally.shared, 0u) << family << " skipped a carried row whose bytes an earlier row had written";
  tally_note(family, tally);
  const auto bytes = fo4::write_plugin(edited);
  const auto reread = fo4::read_plugin(bytes, path);
  for (const auto& record : edited_records) {
    const auto* written = fo4::find_record(edited, record.key);
    ASSERT_NE(written, nullptr) << record.key.to_string();
    const auto after_edit = snapshot(*written);
    ASSERT_EQ(record.before.size(), after_edit.size())
        << family << " changed the subrecord count of " << record.key.to_string();
    EXPECT_FALSE(same_record(record.before, *written))
        << family << " wrote nothing to " << record.key.to_string();
    const auto* restored = fo4::find_record(reread, record.key);
    ASSERT_NE(restored, nullptr) << record.key.to_string();
    EXPECT_TRUE(same_record(after_edit, *restored))
        << family << " did not re-read " << record.key.to_string() << " byte for byte";
    std::size_t changed = 0;
    for (std::size_t index = 0; index < record.before.size(); ++index) {
      if (record.before[index].signature != after_edit[index].signature ||
          record.before[index].data != after_edit[index].data) {
        ++changed;
      }
      if (record.before[index].data == after_edit[index].data) continue;
      bool named = false;
      for (const auto& row : edited_rows) {
        if (row.key == record.key && row.field.scalar.subrecord == record.before[index].signature) {
          named = true;
        }
      }
      EXPECT_TRUE(named) << family << " changed " << record.before[index].signature.to_string()
                         << " on " << record.key.to_string() << " which no edited row names";
    }
    EXPECT_GE(changed, 1u) << family;
    for (const auto& row : edited_rows) {
      if (row.key != record.key) continue;
      EXPECT_EQ(decode_row(*restored, row.field), row.expected)
          << family << " lost " << row.field.path << " across the write";
    }
  }
}

inline std::string_view link_status_name(fo4::FormLinkFieldMutationStatus status) {
  switch (status) {
    case fo4::FormLinkFieldMutationStatus::updated: return "updated";
    case fo4::FormLinkFieldMutationStatus::unchanged: return "unchanged";
    case fo4::FormLinkFieldMutationStatus::record_missing: return "record_missing";
    case fo4::FormLinkFieldMutationStatus::record_mismatch: return "record_mismatch";
    case fo4::FormLinkFieldMutationStatus::subrecord_missing: return "subrecord_missing";
  }
  return "unknown";
}

// Constraint: a link slot is read back through the shape the setter writes, so a setter that reports updated without moving the bytes is visible.
inline std::optional<std::uint32_t> formlink_slot_raw(
    const fo4::Record& record,
    const fo4::schema::FormLinkFieldDescriptor& field,
    std::size_t index) {
  fo4::schema::FieldShape shape;
  shape.subrecord = field.subrecord;
  shape.offset = field.offset;
  shape.width = sizeof(std::uint32_t);
  shape.min_form_version = field.min_form_version;
  shape.max_form_version = field.max_form_version;
  const auto resolved = fo4::schema::resolve_field_slots(record, shape, field.address);
  if (resolved.duplicate || resolved.malformed || index >= resolved.slots.size()) {
    return std::nullopt;
  }
  const auto slot = resolved.slots[index];
  if (!slot.valid || slot.misfit || slot.subrecord >= record.subrecords.size()) {
    return std::nullopt;
  }
  const auto& data = record.subrecords[slot.subrecord].data;
  if (slot.offset + sizeof(std::uint32_t) > data.size()) return std::nullopt;
  std::uint32_t raw = 0;
  std::memcpy(&raw, data.data() + slot.offset, sizeof(raw));
  return raw;
}

inline void roundtrip_links(const char* family, Signature signature) {
  const auto* directory = std::getenv("FO4_DATA_DIR");
  if (directory == nullptr) {
    GTEST_SKIP() << "set FO4_DATA_DIR to run official-master coverage";
  }
  record_game_build(detect_game_build(directory));
  const auto path = std::filesystem::path(directory) / "Fallout4.esm";
  ASSERT_TRUE(std::filesystem::is_regular_file(path)) << path.string();
  const auto plugin = fo4::read_plugin_file(path);
  const auto rows = fo4::schema::formlink_fields(signature);
  ASSERT_FALSE(rows.empty()) << family << " has no FormLink rows";

  std::optional<fo4::FormKey> chosen;
  std::optional<fo4::FormKey> target;
  for (const auto& field : rows) {
    const auto allowed = fo4::schema::formlink_field_targets(field);
    if (allowed.empty()) continue;
    fo4::visit_records(plugin, [&](const fo4::Record& record) {
      if (chosen.has_value() || record.header.signature != signature) return;
      const auto count = fo4::record_field_count(plugin, record.form_key, field);
      if (count && *count > 0) chosen = record.form_key;
    });
    if (!chosen.has_value()) continue;
    fo4::visit_records(plugin, [&](const fo4::Record& record) {
      if (target.has_value() || record.form_key == *chosen) return;
      if (std::find(allowed.begin(), allowed.end(), record.header.signature) == allowed.end()) return;
      target = record.form_key;
    });
    if (target.has_value()) break;
    chosen.reset();
  }
  ASSERT_TRUE(chosen.has_value()) << family << " has no record carrying one of its links";
  const auto key = *chosen;
  ASSERT_TRUE(target.has_value()) << family << " has no record to link to";
  const auto* target_record = fo4::find_record(plugin, *target);
  ASSERT_NE(target_record, nullptr) << target->to_string();
  const auto target_signature = target_record->header.signature;

  auto edited = fo4::read_plugin_file(path);
  const auto* live = fo4::find_record(edited, key);
  ASSERT_NE(live, nullptr) << key.to_string();
  const auto before = snapshot(*live);

  std::size_t exercised = 0;
  std::size_t skipped = 0;
  std::size_t admitted = 0;
  std::vector<Signature> distinct;
  std::vector<fo4::schema::FormLinkFieldDescriptor> written;
  const auto expected_raw =
      fo4::encode_plugin_form_id(*target, fo4::plugin_form_id_context(edited)).value();
  for (const auto& field : rows) {
    if (!fo4::schema::formlink_field_target_allowed(field, target_signature)) continue;
    const auto count = fo4::record_field_count(edited, key, field);
    if (!count || *count == 0) continue;
    ++admitted;
    const auto* current = fo4::find_record(edited, key);
    ASSERT_NE(current, nullptr);
    const auto prior = snapshot(*current);
    const auto status = fo4::set_record_formlink_field(edited, key, field, 0, *target);
    if (status != fo4::FormLinkFieldMutationStatus::updated) {
      ++skipped;
      skip_note(family, field.path,
                std::string("link setter reported ") + std::string(link_status_name(status)));
      continue;
    }
    const auto* probe = fo4::find_record(edited, key);
    ASSERT_NE(probe, nullptr);
    if (same_record(prior, *probe)) {
      ++skipped;
      skip_note(family, field.path, "link setter reported updated but left the bytes alone");
      continue;
    }
    const auto raw = formlink_slot_raw(*probe, field, 0);
    ASSERT_TRUE(raw.has_value()) << family << " cannot read back " << field.path;
    EXPECT_EQ(*raw, expected_raw)
        << family << " wrote a " << field.path << " that does not decode to its target";
    if (std::find(distinct.begin(), distinct.end(), field.subrecord) == distinct.end()) {
      distinct.push_back(field.subrecord);
    }
    written.push_back(field);
    ++exercised;
  }
  ASSERT_GT(exercised, 0u) << family << " wrote no link";
  // Constraint: an admitted link row is either written or skipped for a named reason, so none can vanish.
  EXPECT_EQ(exercised + skipped, admitted) << family << " dropped an admitted link row without a reason";
  const auto* final_record = fo4::find_record(edited, key);
  ASSERT_NE(final_record, nullptr);
  const auto after_edit = snapshot(*final_record);
  ASSERT_EQ(before.size(), after_edit.size())
      << family << " changed the record's subrecord count, so a link write is not what ran";
  const auto bytes = fo4::write_plugin(edited);
  const auto reread = fo4::read_plugin(bytes, path);
  const auto* restored = fo4::find_record(reread, key);
  ASSERT_NE(restored, nullptr) << key.to_string();
  EXPECT_TRUE(same_record(after_edit, *restored))
      << family << " did not re-read the edited link byte for byte";
  const auto reread_raw =
      fo4::encode_plugin_form_id(*target, fo4::plugin_form_id_context(reread)).value();
  for (const auto& field : written) {
    const auto raw = formlink_slot_raw(*restored, field, 0);
    ASSERT_TRUE(raw.has_value()) << family << " lost " << field.path << " across the write";
    EXPECT_EQ(*raw, reread_raw)
        << family << " re-read " << field.path << " as something other than its target";
  }
  std::size_t changed = 0;
  for (std::size_t index = 0; index < before.size(); ++index) {
    if (before[index].signature != after_edit[index].signature ||
        before[index].data != after_edit[index].data) {
      ++changed;
      EXPECT_TRUE(std::find(distinct.begin(), distinct.end(), before[index].signature) != distinct.end())
          << family << " changed " << before[index].signature.to_string()
          << " which no edited link names";
    }
  }
  EXPECT_GE(changed, 1u) << family;
  EXPECT_LE(changed, distinct.size()) << family;
}

inline bool has_distinct_values(const std::vector<fo4::FormKey>& values) {
  return std::any_of(values.begin(), values.end(),
                     [&](const fo4::FormKey& value) { return value != values.front(); });
}

inline void roundtrip_lists(const char* family,
                            Signature signature,
                            std::initializer_list<std::string_view> paths) {
  const auto* directory = std::getenv("FO4_DATA_DIR");
  if (directory == nullptr) {
    GTEST_SKIP() << "set FO4_DATA_DIR to run official-master coverage";
  }
  record_game_build(detect_game_build(directory));
  const auto path = std::filesystem::path(directory) / "Fallout4.esm";
  ASSERT_TRUE(std::filesystem::is_regular_file(path)) << path.string();
  const auto plugin = fo4::read_plugin_file(path);

  std::vector<fo4::schema::FormLinkListDescriptor> descriptors;
  for (const auto base : paths) {
    const auto descriptor = fo4::schema::formlink_list_descriptor(signature, base);
    ASSERT_TRUE(descriptor.has_value()) << base;
    descriptors.push_back(*descriptor);
  }

  std::optional<fo4::FormKey> chosen;
  fo4::visit_records(plugin, [&](const fo4::Record& record) {
    if (chosen.has_value() || record.header.signature != signature) return;
    for (const auto& descriptor : descriptors) {
      const auto values =
          fo4::schema::record_formlink_list_values(plugin, record.form_key, descriptor);
      if (!values || values->size() < 2 || !has_distinct_values(*values)) continue;
      chosen = record.form_key;
      return;
    }
  });
  // Constraint: an official master must carry a record whose list holds two distinct links, so a missing one fails instead of skipping.
  ASSERT_TRUE(chosen.has_value())
      << family << " has no record with two distinct values in one of its lists";
  const auto key = *chosen;
  auto edited = fo4::read_plugin_file(path);
  const auto* live = fo4::find_record(edited, key);
  ASSERT_NE(live, nullptr) << key.to_string();
  const auto before = snapshot(*live);
  std::vector<std::size_t> initial_counts;
  for (const auto& descriptor : descriptors) {
    initial_counts.push_back(
        fo4::schema::record_formlink_list_size(edited, key, descriptor).value_or(0));
  }

  std::size_t exercised = 0;
  std::size_t writes = 0;
  for (std::size_t which = 0; which < descriptors.size(); ++which) {
    const auto& descriptor = descriptors[which];
    auto values = fo4::schema::record_formlink_list_values(edited, key, descriptor);
    ASSERT_TRUE(values.has_value()) << descriptor.path;
    const auto count = values->size();
    if (count == 0) {
      skip_note(family, descriptor.path, "list empty on the chosen record");
      continue;
    }
    std::optional<fo4::FormKey> different;
    for (const auto& value : *values) {
      if (value != values->front()) {
        different = value;
        break;
      }
    }
    if (different.has_value()) {
      const auto original = values->front();
      ASSERT_EQ(fo4::schema::set_record_formlink_list_element(edited, key, descriptor, 0, *different),
                fo4::schema::FormLinkListMutationStatus::updated)
          << descriptor.path;
      const auto swapped = fo4::schema::record_formlink_list_values(edited, key, descriptor);
      ASSERT_TRUE(swapped.has_value());
      ASSERT_EQ(swapped->size(), count);
      ASSERT_EQ(swapped->at(0), *different);
      ASSERT_EQ(fo4::schema::set_record_formlink_list_element(edited, key, descriptor, 0, original),
                fo4::schema::FormLinkListMutationStatus::updated)
          << descriptor.path;
      ++writes;
    } else {
      skip_note(family, descriptor.path, "list holds one repeated value, so no set proves a change");
    }

    const auto appended = values->front();
    ASSERT_EQ(fo4::schema::insert_record_formlink_list_element(edited, key, descriptor, count,
                                                               appended),
              fo4::schema::FormLinkListMutationStatus::updated)
        << descriptor.path;
    const auto grown_record = fo4::find_record(edited, key);
    ASSERT_NE(grown_record, nullptr);
    const auto grown = snapshot(*grown_record);
    const auto grown_values = fo4::schema::record_formlink_list_values(edited, key, descriptor);
    ASSERT_TRUE(grown_values.has_value());
    ASSERT_EQ(grown_values->size(), count + 1) << descriptor.path;
    EXPECT_EQ(grown_values->back(), appended) << descriptor.path;
    for (std::size_t other = 0; other < descriptors.size(); ++other) {
      const auto other_count =
          fo4::schema::record_formlink_list_size(edited, key, descriptors[other]).value_or(0);
      const auto expected = other == which ? count + 1 : initial_counts[other];
      EXPECT_EQ(other_count, expected) << descriptors[other].path << " after appending "
                                       << descriptor.path;
    }

    for (const auto& subrecord : grown) {
      const bool kept = std::any_of(before.begin(), before.end(), [&](const SubrecordBytes& original) {
        return original.signature == subrecord.signature && original.data == subrecord.data;
      });
      if (kept) continue;
      EXPECT_TRUE(subrecord.signature == descriptor.subrecord ||
                  subrecord.signature == descriptor.counter)
          << family << " changed " << subrecord.signature.to_string() << " for "
          << descriptor.path;
    }

    const auto bytes = fo4::write_plugin(edited);
    const auto reread = fo4::read_plugin(bytes, path);
    const auto* restored = fo4::find_record(reread, key);
    ASSERT_NE(restored, nullptr) << key.to_string();
    EXPECT_TRUE(same_record(grown, *restored))
        << family << " did not re-read the appended " << descriptor.path << " link byte for byte";

    ASSERT_EQ(fo4::schema::remove_record_formlink_list_element(edited, key, descriptor, count),
              fo4::schema::FormLinkListMutationStatus::updated)
        << descriptor.path;
    const auto* reverted = fo4::find_record(edited, key);
    ASSERT_NE(reverted, nullptr);
    EXPECT_TRUE(same_snapshot(before, snapshot(*reverted)))
        << family << " did not restore " << descriptor.path << " after an append and a remove";
    ++exercised;
  }

  EXPECT_GT(writes, 0u) << family << " set no list element";
  EXPECT_GT(exercised, 0u) << family << " exercised no list";
}

inline std::vector<std::byte> editor_id_payload(const fo4::Record& record) {
  for (const auto& subrecord : record.subrecords) {
    if (subrecord.signature == Signature::from_string("EDID")) return subrecord.data;
  }
  return {};
}

inline std::vector<std::byte> editor_id_payload(const fo4::Plugin& plugin,
                                                const fo4::FormKey& key) {
  const auto* record = fo4::find_record(plugin, key);
  return record == nullptr ? std::vector<std::byte>{} : editor_id_payload(*record);
}

inline std::vector<std::byte> encoded_editor_id(std::string_view text) {
  std::vector<std::byte> out;
  out.reserve(text.size() + 1);
  for (const char character : text) out.push_back(static_cast<std::byte>(character));
  out.push_back(std::byte{0});
  return out;
}

inline void roundtrip_strings(const char* family, Signature signature) {
  const auto* directory = std::getenv("FO4_DATA_DIR");
  if (directory == nullptr) {
    GTEST_SKIP() << "set FO4_DATA_DIR to run official-master coverage";
  }
  record_game_build(detect_game_build(directory));
  const auto path = std::filesystem::path(directory) / "Fallout4.esm";
  ASSERT_TRUE(std::filesystem::is_regular_file(path)) << path.string();
  const auto plugin = fo4::read_plugin_file(path);

  std::optional<fo4::FormKey> chosen;
  fo4::visit_records(plugin, [&](const fo4::Record& record) {
    if (chosen.has_value() || record.header.signature != signature) return;
    if (editor_id_payload(record).size() < 4) return;
    chosen = record.form_key;
  });
  ASSERT_TRUE(chosen.has_value())
      << family << " has no record with an editor ID of at least three characters";
  const auto key = *chosen;

  auto edited = fo4::read_plugin_file(path);
  const auto* live = fo4::find_record(edited, key);
  ASSERT_NE(live, nullptr) << key.to_string();
  const auto before = snapshot(*live);
  const auto original = editor_id_payload(edited, key);
  ASSERT_GE(original.size(), std::size_t{4}) << key.to_string();
  const auto text = std::string(reinterpret_cast<const char*>(original.data()), original.size() - 1);
  const auto longer = text + "_Radicle602";
  const auto shorter = text.substr(0, text.size() / 2 + 1);
  ASSERT_LT(shorter.size(), original.size());
  ASSERT_GT(encoded_editor_id(longer).size(), original.size());

  ASSERT_TRUE(fo4::set_record_editor_id(edited, key, longer));
  EXPECT_EQ(editor_id_payload(edited, key), encoded_editor_id(longer))
      << family << " did not grow the editor ID payload to the new length";
  const auto* grown = fo4::find_record(edited, key);
  ASSERT_NE(grown, nullptr) << key.to_string();
  const auto grown_snapshot = snapshot(*grown);

  auto bytes = fo4::write_plugin(edited);
  auto reread = fo4::read_plugin(bytes, path);
  const auto* restored = fo4::find_record(reread, key);
  ASSERT_NE(restored, nullptr) << key.to_string();
  EXPECT_TRUE(same_record(grown_snapshot, *restored))
      << family << " did not re-read the longer editor ID byte for byte";
  EXPECT_EQ(editor_id_payload(reread, key), encoded_editor_id(longer));

  ASSERT_TRUE(fo4::set_record_editor_id(reread, key, shorter));
  EXPECT_EQ(editor_id_payload(reread, key), encoded_editor_id(shorter))
      << family << " did not shrink the editor ID payload to the new length";
  const auto* shrunk = fo4::find_record(reread, key);
  ASSERT_NE(shrunk, nullptr) << key.to_string();
  const auto shrunk_snapshot = snapshot(*shrunk);

  bytes = fo4::write_plugin(reread);
  const auto second = fo4::read_plugin(bytes, path);
  const auto* shortened = fo4::find_record(second, key);
  ASSERT_NE(shortened, nullptr) << key.to_string();
  EXPECT_TRUE(same_record(shrunk_snapshot, *shortened))
      << family << " did not re-read the shorter editor ID byte for byte";
  EXPECT_EQ(editor_id_payload(second, key), encoded_editor_id(shorter));

  ASSERT_TRUE(fo4::set_record_editor_id(edited, key, text));
  const auto* reverted = fo4::find_record(edited, key);
  ASSERT_NE(reverted, nullptr) << key.to_string();
  EXPECT_TRUE(same_snapshot(before, snapshot(*reverted)))
      << family << " did not restore the editor ID after a longer and a shorter write";
}

}  // namespace row_roundtrip
