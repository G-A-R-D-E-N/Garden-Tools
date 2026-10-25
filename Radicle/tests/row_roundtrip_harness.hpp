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
#include <cstdlib>
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
  std::size_t skipped = 0;
};

// Constraint: every skip is logged with its reason so a family that stops proving a row is visible in the log.
inline void skip_note(const char* family, std::string_view path, std::string_view reason) {
  std::clog << family << ": skipped " << path << " (" << reason << ")\n";
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

  std::optional<fo4::FormKey> chosen;
  fo4::visit_records(plugin, [&](const fo4::Record& record) {
    if (chosen.has_value() || record.header.signature != signature) return;
    for (const auto& field : rows) {
      const auto count = fo4::record_field_count(plugin, record.form_key, field);
      if (count && *count > 0) {
        chosen = record.form_key;
        return;
      }
    }
  });
  ASSERT_TRUE(chosen.has_value()) << family << " has no record carrying one of its rows";
  const auto key = *chosen;
  auto edited = fo4::read_plugin_file(path);
  const auto* live = fo4::find_record(edited, key);
  ASSERT_NE(live, nullptr) << key.to_string();
  const auto before = snapshot(*live);

  Tally tally;
  struct EditedRow {
    ScalarFieldDescriptor field;
    std::string expected;
  };
  std::vector<EditedRow> edited_rows;
  std::vector<EditedSpan> edited_spans;
  for (const auto& field : rows) {
    ++tally.rows;
    if (field.scalar.storage == ScalarStorage::zstring ||
        field.scalar.storage == ScalarStorage::fixed_string ||
        field.scalar.storage == ScalarStorage::bytes) {
      ++tally.skipped;
      skip_note(family, field.path, "whole-payload storage has its own round-trip test");
      continue;
    }
    const auto count = fo4::record_field_count(edited, key, field);
    if (!count || *count == 0) {
      ++tally.skipped;
      skip_note(family, field.path, "row absent from the chosen record");
      continue;
    }
    const auto* current = fo4::find_record(edited, key);
    ASSERT_NE(current, nullptr);
    const auto slot = slot_of(*current, field, 0);
    if (!slot.has_value()) {
      ++tally.skipped;
      skip_note(family, field.path, "row does not resolve a slot here");
      continue;
    }
    const EditedSpan span{slot->subrecord, slot->offset, row_width(*current, field, *slot), field.path};
    const auto shared = std::find_if(edited_spans.begin(), edited_spans.end(),
                                     [&](const EditedSpan& taken) { return overlaps(taken, span); });
    if (shared != edited_spans.end()) {
      ++tally.skipped;
      skip_note(family, field.path, "bytes already written by an earlier row");
      continue;
    }

    const auto original = decode_row(*current, field);
    std::string applied;
    std::string decoded;
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
      ++tally.skipped;
      skip_note(family, field.path, "no candidate decodes to a new value");
      continue;
    }
    edited_spans.push_back(span);
    edited_rows.push_back({field, decoded});
    ++tally.exercised;
  }

  ASSERT_GT(tally.exercised, 0u) << family << " exercised no row";
  const auto* written = fo4::find_record(edited, key);
  ASSERT_NE(written, nullptr);
  const auto after_edit = snapshot(*written);
  ASSERT_EQ(before.size(), after_edit.size())
      << family << " changed the record's subrecord count, so a scalar write is not what ran";
  EXPECT_FALSE(same_record(before, *written))
      << family << " wrote nothing to the record";

  const auto bytes = fo4::write_plugin(edited);
  const auto reread = fo4::read_plugin(bytes, path);
  const auto* restored = fo4::find_record(reread, key);
  ASSERT_NE(restored, nullptr) << key.to_string();
  EXPECT_TRUE(same_record(after_edit, *restored))
      << family << " did not re-read the edited record byte for byte";

  for (const auto& row : edited_rows) {
    EXPECT_EQ(decode_row(*restored, row.field), row.expected)
        << family << " lost " << row.field.path << " across the write";
  }
  EXPECT_EQ(edited_rows.size(), tally.exercised) << family;

  std::size_t changed = 0;
  for (std::size_t index = 0; index < before.size(); ++index) {
    if (before[index].signature != after_edit[index].signature) {
      ++changed;
      continue;
    }
    if (before[index].data == after_edit[index].data) continue;
    ++changed;
    bool named = false;
    for (const auto& row : edited_rows) {
      if (row.field.scalar.subrecord == before[index].signature) named = true;
    }
    EXPECT_TRUE(named) << family << " changed " << before[index].signature.to_string()
                       << " which no edited row names";
  }
  std::vector<Signature> distinct;
  for (const auto& row : edited_rows) {
    if (std::find(distinct.begin(), distinct.end(), row.field.scalar.subrecord) == distinct.end()) {
      distinct.push_back(row.field.scalar.subrecord);
    }
  }
  EXPECT_GE(changed, 1u) << family;
  EXPECT_LE(changed, distinct.size())
      << family << " changed more subrecords than the distinct subrecords its rows name";
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
  fo4::visit_records(plugin, [&](const fo4::Record& record) {
    if (chosen.has_value() || record.header.signature != signature) return;
    for (const auto& field : rows) {
      const auto count = fo4::record_field_count(plugin, record.form_key, field);
      if (count && *count > 0) {
        chosen = record.form_key;
        return;
      }
    }
  });
  ASSERT_TRUE(chosen.has_value()) << family << " has no record carrying one of its links";
  const auto key = *chosen;

  std::optional<fo4::FormKey> target;
  fo4::visit_records(plugin, [&](const fo4::Record& record) {
    if (target.has_value() || record.header.signature != signature) return;
    for (const auto& field : rows) {
      const auto allowed = fo4::schema::formlink_field_targets(field);
      if (allowed.empty() || allowed.front() != record.header.signature) continue;
      if (record.form_key == key) continue;
      target = record.form_key;
      return;
    }
  });
  ASSERT_TRUE(target.has_value()) << family << " has no second record to link to";
  const auto* target_record = fo4::find_record(plugin, *target);
  ASSERT_NE(target_record, nullptr) << target->to_string();
  const auto target_signature = target_record->header.signature;

  auto edited = fo4::read_plugin_file(path);
  const auto* live = fo4::find_record(edited, key);
  ASSERT_NE(live, nullptr) << key.to_string();
  const auto before = snapshot(*live);

  std::size_t exercised = 0;
  std::vector<Signature> distinct;
  for (const auto& field : rows) {
    if (!fo4::schema::formlink_field_target_allowed(field, target_signature)) continue;
    const auto count = fo4::record_field_count(edited, key, field);
    if (!count || *count == 0) continue;
    const auto* current = fo4::find_record(edited, key);
    ASSERT_NE(current, nullptr);
    const auto prior = snapshot(*current);
    if (fo4::set_record_formlink_field(edited, key, field, 0, *target) !=
        fo4::FormLinkFieldMutationStatus::updated) {
      continue;
    }
    const auto* probe = fo4::find_record(edited, key);
    ASSERT_NE(probe, nullptr);
    if (same_record(prior, *probe)) continue;
    if (std::find(distinct.begin(), distinct.end(), field.subrecord) == distinct.end()) {
      distinct.push_back(field.subrecord);
    }
    ++exercised;
  }
  ASSERT_GT(exercised, 0u) << family << " wrote no link";
  const auto* written = fo4::find_record(edited, key);
  ASSERT_NE(written, nullptr);
  const auto after_edit = snapshot(*written);
  ASSERT_EQ(before.size(), after_edit.size())
      << family << " changed the record's subrecord count, so a link write is not what ran";
  const auto bytes = fo4::write_plugin(edited);
  const auto reread = fo4::read_plugin(bytes, path);
  const auto* restored = fo4::find_record(reread, key);
  ASSERT_NE(restored, nullptr) << key.to_string();
  EXPECT_TRUE(same_record(after_edit, *restored))
      << family << " did not re-read the edited link byte for byte";
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
  if (!chosen.has_value()) {
    GTEST_SKIP() << family << " has no record with two distinct values in one of its lists";
  }
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

inline std::size_t editor_id_payload(const fo4::Plugin& plugin, const fo4::FormKey& key) {
  const auto* record = fo4::find_record(plugin, key);
  if (record == nullptr) return 0;
  for (const auto& subrecord : record->subrecords) {
    if (subrecord.signature == Signature::from_string("EDID")) return subrecord.data.size();
  }
  return 0;
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

  std::optional<ScalarFieldDescriptor> editor;
  for (const auto& field : fo4::schema::scalar_fields(signature)) {
    if (field.scalar.subrecord == Signature::from_string("EDID") &&
        field.scalar.storage == ScalarStorage::zstring) {
      editor = field;
      break;
    }
  }
  ASSERT_TRUE(editor.has_value()) << family << " has no editor ID string row";

  std::optional<fo4::FormKey> chosen;
  fo4::visit_records(plugin, [&](const fo4::Record& record) {
    if (chosen.has_value() || record.header.signature != signature) return;
    if (fo4::record_field_count(plugin, record.form_key, *editor).value_or(0) != std::size_t{1}) {
      return;
    }
    if (decode_row(record, *editor).size() < 3) return;
    chosen = record.form_key;
  });
  if (!chosen.has_value()) {
    GTEST_SKIP() << family << " has no record with an editor ID of at least three characters";
  }
  const auto key = *chosen;

  auto edited = fo4::read_plugin_file(path);
  const auto* live = fo4::find_record(edited, key);
  ASSERT_NE(live, nullptr) << key.to_string();
  const auto before = snapshot(*live);
  const auto original = decode_row(*live, *editor);
  ASSERT_GE(original.size(), std::size_t{3}) << key.to_string();
  const auto longer = original + "_Radicle602";
  const auto shorter = original.substr(0, original.size() / 2 + 1);
  ASSERT_LT(shorter.size(), original.size());
  ASSERT_GT(longer.size(), original.size());

  ASSERT_EQ(fo4::set_record_string(edited, key, *editor, 0, longer),
            fo4::ScalarMutationStatus::updated);
  const auto* grown = fo4::find_record(edited, key);
  ASSERT_NE(grown, nullptr);
  EXPECT_EQ(decode_row(*grown, *editor), longer);
  EXPECT_EQ(editor_id_payload(edited, key), longer.size() + 1)
      << family << " did not grow the editor ID payload to the new length";
  const auto grown_snapshot = snapshot(*grown);

  auto bytes = fo4::write_plugin(edited);
  auto reread = fo4::read_plugin(bytes, path);
  const auto* restored = fo4::find_record(reread, key);
  ASSERT_NE(restored, nullptr) << key.to_string();
  EXPECT_TRUE(same_record(grown_snapshot, *restored))
      << family << " did not re-read the longer editor ID byte for byte";
  EXPECT_EQ(decode_row(*restored, *editor), longer);

  ASSERT_EQ(fo4::set_record_string(reread, key, *editor, 0, shorter),
            fo4::ScalarMutationStatus::updated);
  const auto* shrunk = fo4::find_record(reread, key);
  ASSERT_NE(shrunk, nullptr);
  EXPECT_EQ(decode_row(*shrunk, *editor), shorter);
  EXPECT_EQ(editor_id_payload(reread, key), shorter.size() + 1)
      << family << " did not shrink the editor ID payload to the new length";
  const auto shrunk_snapshot = snapshot(*shrunk);

  bytes = fo4::write_plugin(reread);
  const auto second = fo4::read_plugin(bytes, path);
  const auto* shortened = fo4::find_record(second, key);
  ASSERT_NE(shortened, nullptr) << key.to_string();
  EXPECT_TRUE(same_record(shrunk_snapshot, *shortened))
      << family << " did not re-read the shorter editor ID byte for byte";
  EXPECT_EQ(decode_row(*shortened, *editor), shorter);

  ASSERT_EQ(fo4::set_record_string(edited, key, *editor, 0, original),
            fo4::ScalarMutationStatus::updated);
  const auto* reverted = fo4::find_record(edited, key);
  ASSERT_NE(reverted, nullptr);
  EXPECT_TRUE(same_snapshot(before, snapshot(*reverted)))
      << family << " did not restore the editor ID after a longer and a shorter write";
}

}  // namespace row_roundtrip
