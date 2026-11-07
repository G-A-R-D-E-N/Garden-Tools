#pragma once

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

}  // namespace fo4
