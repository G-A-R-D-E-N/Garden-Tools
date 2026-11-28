#pragma once

#include "fo4/localization/strings.hpp"
#include "fo4/plugin/container.hpp"
#include "fo4/schema/record_constraints.hpp"

#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

namespace fo4 {

enum class SubrecordAddStatus {
  added,
  already_present,
  record_missing,
  not_authorable,
  unknown_order,
  no_default,
};

enum class SubrecordRemoveStatus {
  removed,
  record_missing,
  absent,
  below_minimum,
  not_authorable,
  unknown_order,
};

enum class RecordCreateStatus {
  created,
  not_authorable,
  no_default,
  invalid_editor_id,
  preflight_failed,
};

struct RecordCreateResult {
  RecordCreateStatus status{RecordCreateStatus::not_authorable};
  FormKey key{};
};

enum class SubrecordBytesStatus {
  replaced,
  appended,
  record_missing,
  occurrence_out_of_range,
};

enum class RawRecordStatus {
  created,
  not_creatable,
  nested_placement,
};

struct RawRecordResult {
  RawRecordStatus status{RawRecordStatus::not_creatable};
  FormKey key{};
};

// Requires: payload is stored unvalidated; occurrence equal to the current count appends at the record end.
SubrecordBytesStatus set_record_subrecord_bytes(Plugin& plugin,
                                                const FormKey& record_key,
                                                Signature subrecord,
                                                std::size_t occurrence,
                                                std::span<const std::byte> bytes);

// Requires: the stored bytes are written after the declared size without compression, so a broken zlib stream or a wrong declared size survives; the subrecords are ignored on write and a later subrecord edit does not change the payload.
bool set_record_raw_compressed_payload(Plugin& plugin,
                                       const FormKey& record_key,
                                       std::uint32_t declared_size,
                                       std::span<const std::byte> bytes);

// Requires: a top-level record only; form_version 0 keeps the plugin form version.
RawRecordResult create_raw_record(Plugin& plugin, Signature record, std::uint16_t form_version);

SubrecordAddStatus add_record_subrecord(Plugin& plugin, const FormKey& record_key, Signature subrecord);

SubrecordRemoveStatus remove_record_subrecord(Plugin& plugin,
                                              const FormKey& record_key,
                                              Signature subrecord,
                                              std::size_t occurrence);

RecordCreateResult create_record(Plugin& plugin, Signature record, std::string_view editor_id);

SubrecordAddStatus add_record_subrecord(Plugin& plugin,
                                        const FormKey& record_key,
                                        Signature subrecord,
                                        std::span<const schema::SubrecordAuthoringConstraint> rules);

SubrecordRemoveStatus remove_record_subrecord(Plugin& plugin,
                                              const FormKey& record_key,
                                              Signature subrecord,
                                              std::size_t occurrence,
                                              std::span<const schema::SubrecordAuthoringConstraint> rules);

RecordCreateResult create_record(Plugin& plugin,
                                 Signature record,
                                 std::string_view editor_id,
                                 std::span<const schema::SubrecordAuthoringConstraint> rules);

struct LocalizedRowId {
  Signature subrecord{};
  std::uint32_t id{0};
};

RecordCreateResult create_record_with_localized_rows(Plugin& plugin,
                                                     Signature record,
                                                     std::string_view editor_id,
                                                     std::span<const LocalizedRowId> localized_ids);

RecordCreateResult create_record_with_localized_rows(Plugin& plugin,
                                                     Signature record,
                                                     std::string_view editor_id,
                                                     std::span<const LocalizedRowId> localized_ids,
                                                     std::span<const schema::SubrecordAuthoringConstraint> rules);

// Constraint: every required localized row is reserved in the supplied tables before the record is built, and a
// refusal must leave neither a record nor a dangling row behind.
RecordCreateResult create_record_with_string_rows(Plugin& plugin,
                                                 Signature record,
                                                 std::string_view editor_id,
                                                 localization::LocalizationTables& tables);

}  // namespace fo4
