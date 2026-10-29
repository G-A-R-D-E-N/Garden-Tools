#include "fo4/schema/preflight.hpp"
#include "fo4/schema/field_address.hpp"

#include "fo4/plugin/visit.hpp"
#include "fo4/schema/formlink_lists.hpp"
#include "fo4/schema/localized_fields.hpp"
#include "fo4/schema/record_constraints.hpp"
#include "fo4/schema/scalar.hpp"

#include <charconv>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

namespace fo4::schema {
namespace {

PreflightIssue issue_for(const Record& record,
                         const ScalarFieldDescriptor& field,
                         std::string code,
                         std::string message,
                         PreflightSeverity severity) {
  return PreflightIssue{
      .record = record.form_key,
      .path = std::string(field.path),
      .code = std::move(code),
      .message = std::move(message),
      .severity = severity,
  };
}

PreflightIssue field_issue(const Record& record,
                           std::string path,
                           std::string code,
                           std::string message,
                           PreflightSeverity severity) {
  return PreflightIssue{
      .record = record.form_key,
      .path = std::move(path),
      .code = std::move(code),
      .message = std::move(message),
      .severity = severity,
  };
}

PreflightIssue authoring_issue(const FormKey& key,
                               std::string path,
                               std::string code,
                               std::string message) {
  return PreflightIssue{
      .record = key,
      .path = std::move(path),
      .code = std::move(code),
      .message = std::move(message),
      .severity = PreflightSeverity::error,
  };
}

struct RecordLocation {
  const Record* record{nullptr};
  bool direct_top_level{false};
  bool top_label_matches{false};
  std::int32_t parent_group_type{-1};
};

void locate_record_in_group(const Group& group,
                            const FormKey& key,
                            bool top_level_group,
                            Signature top_label,
                            std::size_t depth,
                            RecordLocation& result) {
  if (result.record != nullptr) return;
  for (const auto& child : group.children) {
    if (const auto* record = std::get_if<Record>(&child)) {
      if (record->form_key != key) continue;
      result.record = record;
      result.direct_top_level = top_level_group && depth == 0;
      result.top_label_matches = top_label == record->header.signature;
      result.parent_group_type = group.header.group_type;
      return;
    }
    locate_record_in_group(
        std::get<Group>(child), key, top_level_group, top_label, depth + 1, result);
    if (result.record != nullptr) return;
  }
}

RecordLocation locate_record(const Plugin& plugin, const FormKey& key) {
  RecordLocation result;
  for (const auto& group : plugin.groups) {
    const bool top_level_group =
        group.header.group_type == static_cast<std::int32_t>(GroupType::top);
    locate_record_in_group(
        group, key, top_level_group, group.header.top_label(), 0, result);
    if (result.record != nullptr) break;
  }
  return result;
}

bool first_nested_terminator_descriptor(std::size_t descriptor_index) {
  const auto& descriptor = k_generated_nested_formlink_lists[descriptor_index];
  for (std::size_t index = 0; index < descriptor_index; ++index) {
    const auto& previous = k_generated_nested_formlink_lists[index];
    if (previous.record == descriptor.record &&
        previous.container_path == descriptor.container_path &&
        previous.terminator == descriptor.terminator &&
        previous.terminator_size == descriptor.terminator_size) {
      return false;
    }
  }
  return true;
}

std::optional<std::int64_t> stored_integer(std::string_view decoded) noexcept {
  std::int64_t value = 0;
  const auto* begin = decoded.data();
  const auto* end = begin + decoded.size();
  const auto parsed = std::from_chars(begin, end, value);
  if (parsed.ec != std::errc{} || parsed.ptr != end) return std::nullopt;
  return value;
}

std::string unnamed_bit_list(std::uint64_t unknown) {
  std::string out;
  for (int bit = 0; bit < 64; ++bit) {
    if ((unknown & (std::uint64_t{1} << bit)) == 0) continue;
    if (!out.empty()) out += ", ";
    out += "<Unknown: " + std::to_string(bit) + ">";
  }
  return out;
}

void value_membership_issues(const Record& record,
                             const ScalarFieldDescriptor& field,
                             std::string_view decoded,
                             std::vector<PreflightIssue>& issues) {
  if (field.scalar.names.empty()) return;
  const auto stored = stored_integer(decoded);
  if (!stored.has_value()) return;

  if (field.scalar.presentation == ScalarPresentation::enumeration) {
    if (scalar_value_name(field.scalar, *stored).has_value()) return;
    issues.push_back(issue_for(
        record,
        field,
        "scalar_enum_value_unnamed",
        std::string(field.scalar.field_name) + " value " + std::to_string(*stored) +
            " has no name in the generated enumeration (<Unknown: " +
            std::to_string(*stored) + ">)",
        PreflightSeverity::warning));
    return;
  }
  if (field.scalar.presentation != ScalarPresentation::flags) return;

  std::uint64_t declared = 0;
  for (const auto& name : field.scalar.names) declared |= static_cast<std::uint64_t>(name.value);
  const auto unknown = static_cast<std::uint64_t>(*stored) & ~declared;
  if (unknown == 0) return;
  issues.push_back(issue_for(
      record,
      field,
      "scalar_flag_bits_unnamed",
      std::string(field.scalar.field_name) + " sets bits the generated flag row does not name: " +
          unnamed_bit_list(unknown),
      PreflightSeverity::warning));
}

constexpr int k_climate_full_day_units = 144;

void climate_sun_window_issues(const Record& record, std::vector<PreflightIssue>& issues) {
  if (record.header.signature != Signature::from_string("CLMT")) return;
  for (const auto& subrecord : record.subrecords) {
    if (subrecord.signature != Signature::from_string("TNAM") || subrecord.data.size() != 6) continue;
    const auto byte = [&](std::size_t offset) { return std::to_integer<int>(subrecord.data[offset]); };
    for (const std::size_t begin : {std::size_t{0}, std::size_t{2}}) {
      const int span = byte(begin + 1) - byte(begin);
      const std::string window = begin == 0 ? "sunrise" : "sunset";
      if (span == k_climate_full_day_units) {
        issues.push_back(field_issue(
            record,
            "CLMT\\TNAM",
            "climate_sun_window_full_day",
            window + " window ends 24 hours after it begins, which the engine divides by zero",
            PreflightSeverity::warning));
      } else if (span <= 0) {
        issues.push_back(field_issue(
            record,
            "CLMT\\TNAM",
            "climate_sun_window_never_opens",
            window + " window ends at or before it begins, so the engine never opens it",
            PreflightSeverity::warning));
      }
    }
  }
}

std::uint32_t little_endian_u32(const std::vector<std::byte>& data, std::size_t offset) {
  std::uint32_t value = 0;
  for (std::size_t i = 0; i < 4; ++i) {
    value |= static_cast<std::uint32_t>(std::to_integer<std::uint8_t>(data[offset + i]))
             << (8 * i);
  }
  return value;
}

std::string hex_byte_string(std::uint32_t value) {
  std::string out("00000000");
  constexpr char digits[] = "0123456789ABCDEF";
  for (int index = 7; index >= 0; --index) {
    out[static_cast<std::size_t>(index)] = digits[value & 0xFu];
    value >>= 4;
  }
  return out;
}

// Invariant: LLCT must equal the LVLO entry count (decode_leveled_list requires the coupling).
void leveled_list_entry_issues(const Record& record, std::vector<PreflightIssue>& issues) {
  const auto signature = record.header.signature;
  if (signature != Signature::from_string("LVLI") &&
      signature != Signature::from_string("LVLN") && signature != Signature::from_string("LVSP")) {
    return;
  }
  const std::string prefix = signature.to_string();
  const auto lvlo = Signature::from_string("LVLO");
  std::size_t entries = 0;
  for (const auto& subrecord : record.subrecords) {
    if (subrecord.signature != lvlo) continue;
    // Invariant: an LVLO entry must name an item (offset 4 holds the Form-ID, which must be non-null).
    if (subrecord.data.size() >= 8 && little_endian_u32(subrecord.data, 4) == 0) {
      issues.push_back(field_issue(record, prefix + "\\LVLO[" + std::to_string(entries) + "]",
                                   "leveled_list_null_entry",
                                   prefix + " LVLO entry " + std::to_string(entries) + " names no item",
                                   PreflightSeverity::error));
    }
    ++entries;
  }
  bool saw_count = false;
  std::uint8_t declared = 0;
  for (const auto& subrecord : record.subrecords) {
    if (subrecord.signature != Signature::from_string("LLCT")) continue;
    saw_count = true;
    if (!subrecord.data.empty()) declared = std::to_integer<std::uint8_t>(subrecord.data[0]);
  }
  if (!saw_count && entries > 0) {
    issues.push_back(field_issue(record, prefix + "\\LLCT", "leveled_list_count_mismatch",
                                 prefix + " holds " + std::to_string(entries) +
                                     " LVLO entries but has no LLCT subrecord",
                                 PreflightSeverity::error));
  } else if (saw_count && declared != entries) {
    issues.push_back(field_issue(
        record, prefix + "\\LLCT", "leveled_list_count_mismatch",
        prefix + " LLCT declares " + std::to_string(declared) + " but holds " +
            std::to_string(entries) + " LVLO entries",
        PreflightSeverity::error));
  }
}

// Invariant: one FLST must not repeat the same LNAM key (a repeated key double-counts the entry).
void form_list_duplicate_issues(const Record& record, std::vector<PreflightIssue>& issues) {
  if (record.header.signature != Signature::from_string("FLST")) return;
  std::set<std::uint32_t> seen;
  std::size_t ordinal = 0;
  for (const auto& subrecord : record.subrecords) {
    if (subrecord.signature != Signature::from_string("LNAM")) continue;
    const std::size_t index = ordinal++;
    if (subrecord.data.size() != 4) continue;
    const auto key = little_endian_u32(subrecord.data, 0);
    if (key == 0 || seen.insert(key).second) continue;
    issues.push_back(field_issue(record, "FLST\\LNAM[" + std::to_string(index) + "]",
                                 "duplicate_formlink_list_entry",
                                 "FLST repeats LNAM entry " + hex_byte_string(key) +
                                     ", which the game counts twice",
                                 PreflightSeverity::warning));
  }
}

// Invariant: a KSIZ counter must equal its packed KWDA entry count (quest aliases pair their own counts).
void keyword_count_issues(const Record& record, std::vector<PreflightIssue>& issues) {
  for (const auto& list : schema::k_generated_formlink_lists) {
    if (list.record != record.header.signature) continue;
    if (list.storage != schema::FormLinkListStorage::packed_array) continue;
    if (list.counter == Signature{}) continue;
    if (list.segment_count != 0 || list.counter_payload_size != 0) continue;
    bool saw_count = false;
    bool multi_count = false;
    std::uint32_t declared = 0;
    std::size_t bytes = 0;
    for (const auto& subrecord : record.subrecords) {
      if (subrecord.signature == list.counter) {
        if (saw_count) multi_count = true;
        saw_count = true;
        if (subrecord.data.size() >= 4) declared = little_endian_u32(subrecord.data, 0);
      } else if (subrecord.signature == list.subrecord) {
        bytes += subrecord.data.size();
      }
    }
    if (multi_count || bytes % 4 != 0) continue;
    const std::size_t entries = bytes / 4;
    const std::string prefix = record.header.signature.to_string();
    if (!saw_count && entries > 0) {
      issues.push_back(field_issue(
          record, prefix + "\\" + list.counter.to_string(), "keyword_count_mismatch",
          prefix + " holds " + std::to_string(entries) + " " + list.subrecord.to_string() +
              " entries but has no " + list.counter.to_string() + " subrecord",
          PreflightSeverity::error));
    } else if (saw_count && declared != entries) {
      issues.push_back(field_issue(
          record, prefix + "\\" + list.counter.to_string(), "keyword_count_mismatch",
          prefix + " " + list.counter.to_string() + " declares " + std::to_string(declared) +
              " but holds " + std::to_string(entries) + " " + list.subrecord.to_string() +
              " entries",
          PreflightSeverity::error));
    }
  }
}

}  // namespace

std::vector<PreflightIssue> preflight_record(const Record& record, bool localized) {
  std::vector<PreflightIssue> issues;

  for (const auto& field : scalar_fields(record.header.signature)) {
    const auto resolved = resolve_field_slots(record, scalar_field_shape(field.scalar),
                                              field.scalar.address);

    if (resolved.duplicate) {
      issues.push_back(issue_for(
          record,
          field,
          "duplicate_scalar_subrecord",
          record.header.signature.to_string() + " contains duplicate " +
              field.scalar.subrecord.to_string() + " subrecords for " +
              std::string(field.path),
          PreflightSeverity::error));
      continue;
    }

    if (resolved.malformed) {
      issues.push_back(issue_for(
          record,
          field,
          "invalid_scalar_storage",
          std::string(field.scalar.field_name) + " has unexpected binary width",
          PreflightSeverity::error));
      continue;
    }

    if (resolved.misaligned) {
      issues.push_back(issue_for(
          record,
          field,
          "invalid_scalar_storage",
          std::string(field.scalar.field_name) + " packed array is not a whole number of elements",
          PreflightSeverity::error));
    }

    for (const auto& slot : resolved.slots) {
      if (!slot.valid) continue;
      if (localized && field.scalar.storage == ScalarStorage::zstring && record.subrecords[slot.subrecord].data.size() == 4 &&
          (field.scalar.localized || is_localized_field(field.record, field.scalar.subrecord))) {
        continue;
      }
      std::string decoded;
      try {
        const auto bytes = scalar_slot_bytes(record.subrecords[slot.subrecord].data, slot, field.scalar);
        decoded = decode_scalar(bytes, field.scalar);
      } catch (const ScalarError& error) {
        issues.push_back(issue_for(
            record,
            field,
            "invalid_scalar_storage",
            error.what(),
            PreflightSeverity::error));
        continue;
      }

      try {
        static_cast<void>(encode_scalar(decoded, field.scalar));
      } catch (const ScalarError& error) {
        issues.push_back(issue_for(
            record,
            field,
            "scalar_value_out_of_range",
            error.what(),
            PreflightSeverity::warning));
      }

      value_membership_issues(record, field, decoded, issues);
    }
  }

  for (const auto& descriptor : k_generated_formlink_lists) {
    if (descriptor.record != record.header.signature) continue;
    std::size_t ordinal = 0;
    for (const auto& subrecord : record.subrecords) {
      if (subrecord.signature != descriptor.subrecord) continue;
      const bool packed = descriptor.storage == FormLinkListStorage::packed_array;
      if (packed ? subrecord.data.size() % 4 != 0 : subrecord.data.size() != 4) {
        issues.push_back(field_issue(
            record,
            std::string(descriptor.path) + "[" + std::to_string(ordinal) + "]",
            "invalid_formlink_list_storage",
            descriptor.record.to_string() + " " +
                descriptor.subrecord.to_string() +
                (packed ? " packed FormLink list must be a multiple of four bytes"
                        : " FormLink list element must be exactly four bytes"),
            PreflightSeverity::error));
      }
      ++ordinal;
    }
  }

  for (std::size_t descriptor_index = 0;
       descriptor_index < std::size(k_generated_nested_formlink_lists);
       ++descriptor_index) {
    const auto& descriptor = k_generated_nested_formlink_lists[descriptor_index];
    if (descriptor.record != record.header.signature) continue;

    const bool validate_terminator =
        first_nested_terminator_descriptor(descriptor_index);
    std::size_t container_index = 0;
    std::size_t element_index = 0;
    bool member_since_terminator = false;
    for (const auto& subrecord : record.subrecords) {
      if (subrecord.signature == descriptor.subrecord) {
        member_since_terminator = true;
        if (subrecord.data.size() != 4) {
          issues.push_back(field_issue(
              record,
              std::string(descriptor.container_path) + "[" +
                  std::to_string(container_index) + "]\\" +
                  std::string(descriptor.member) + "[" +
                  std::to_string(element_index) + "]",
              "invalid_nested_formlink_list_storage",
              descriptor.record.to_string() + " " +
                  descriptor.subrecord.to_string() +
                  " nested FormLink list element must be exactly four bytes",
              PreflightSeverity::error));
        }
        ++element_index;
      }

      if (subrecord.signature != descriptor.terminator) continue;
      if (validate_terminator &&
          subrecord.data.size() != descriptor.terminator_size) {
        issues.push_back(field_issue(
            record,
            std::string(descriptor.container_path) + "[" +
                std::to_string(container_index) + "]",
            "invalid_nested_formlink_terminator_storage",
            descriptor.record.to_string() + " " +
                descriptor.terminator.to_string() +
                " nested-list terminator has unexpected storage size",
            PreflightSeverity::error));
      }
      ++container_index;
      element_index = 0;
      member_since_terminator = false;
    }

    if (member_since_terminator) {
      issues.push_back(field_issue(
          record,
          std::string(descriptor.container_path) + "[" +
              std::to_string(container_index) + "]\\" +
              std::string(descriptor.member),
          "unterminated_nested_formlink_list",
          descriptor.record.to_string() + " nested FormLink list reaches the end of the record without " +
              descriptor.terminator.to_string() + " terminator",
          PreflightSeverity::error));
    }
  }

  leveled_list_entry_issues(record, issues);
  form_list_duplicate_issues(record, issues);
  keyword_count_issues(record, issues);
  climate_sun_window_issues(record, issues);
  return issues;
}

std::vector<PreflightIssue> preflight_plugin(const Plugin& plugin) {
  std::vector<PreflightIssue> issues;
  visit_records(plugin, [&](const Record& record) {
    auto record_issues = preflight_record(record, plugin.header.tes4_flags.is_localized());
    issues.insert(issues.end(),
                  std::make_move_iterator(record_issues.begin()),
                  std::make_move_iterator(record_issues.end()));
  });
  return issues;
}

std::vector<PreflightIssue> preflight_authoring_record(
    const Plugin& plugin,
    const FormKey& record_key) {
  std::vector<PreflightIssue> issues;
  const auto location = locate_record(plugin, record_key);
  if (location.record == nullptr) {
    issues.push_back(authoring_issue(
        record_key,
        {},
        "record_missing",
        "authored record is not present in the plugin"));
    return issues;
  }

  const auto constraint =
      record_authoring_constraint(location.record->header.signature);
  if (!constraint.has_value()) {
    issues.push_back(authoring_issue(
        record_key,
        location.record->header.signature.to_string(),
        "authoring_schema_missing",
        "record type has no generated authoring schema"));
    return issues;
  }

  if (constraint->placement == RecordPlacement::top_level &&
      (!location.direct_top_level || !location.top_label_matches)) {
    issues.push_back(authoring_issue(
        record_key,
        location.record->header.signature.to_string(),
        "record_placement_invalid",
        "record must be a direct child of its matching top-level GRUP"));
  }
  const std::int32_t required_parent =
      constraint->placement == RecordPlacement::cell_children ? static_cast<std::int32_t>(GroupType::cell_temporary_children)
      : constraint->placement == RecordPlacement::quest_children   ? static_cast<std::int32_t>(GroupType::quest_children)
      : constraint->placement == RecordPlacement::topic_children ? static_cast<std::int32_t>(GroupType::topic_children)
                                                                 : -1;
  const bool cell_reference = constraint->placement == RecordPlacement::cell_reference_children;
  // Invariant: only REFR may sit in a visible-distant children group.
  const bool cell_reference_parent =
      location.parent_group_type == static_cast<std::int32_t>(GroupType::cell_persistent_children) ||
      location.parent_group_type == static_cast<std::int32_t>(GroupType::cell_temporary_children) ||
      (location.parent_group_type == static_cast<std::int32_t>(GroupType::cell_visible_distant_children) &&
       location.record->header.signature == Signature::from_string("REFR"));
  if ((required_parent >= 0 && location.parent_group_type != required_parent) ||
      (cell_reference && !cell_reference_parent)) {
    issues.push_back(authoring_issue(
        record_key,
        location.record->header.signature.to_string(),
        "record_placement_invalid",
        constraint->placement == RecordPlacement::cell_children
            ? "record must be a direct child of a cell temporary children GRUP"
            : constraint->placement == RecordPlacement::quest_children
            ? "record must be a direct child of a quest children GRUP"
            : cell_reference
            ? "record must be a direct child of a cell persistent, temporary or visible-distant children GRUP"
            : "record must be a direct child of a topic children GRUP"));
  }

  // Constraint: a CELL is a direct child of its matching top-level GRUP, a world children GRUP, or an interior/exterior cell block GRUP.
  const bool cell_parent_ok =
      location.parent_group_type == static_cast<std::int32_t>(GroupType::world_children) ||
      location.parent_group_type == static_cast<std::int32_t>(GroupType::interior_cell_block) ||
      location.parent_group_type == static_cast<std::int32_t>(GroupType::interior_cell_subblock) ||
      location.parent_group_type == static_cast<std::int32_t>(GroupType::exterior_cell_block) ||
      location.parent_group_type == static_cast<std::int32_t>(GroupType::exterior_cell_subblock);
  if (constraint->placement == RecordPlacement::cell_block &&
      !((location.direct_top_level && location.top_label_matches) || cell_parent_ok)) {
    issues.push_back(authoring_issue(
        record_key,
        location.record->header.signature.to_string(),
        "record_placement_invalid",
        "record must be a direct child of a top-level CELL GRUP, a world children GRUP, or an interior/exterior cell block GRUP"));
  }

  const auto rules = authoring_subrecords(*constraint);
  std::vector<std::size_t> counts(rules.size(), 0);
  std::optional<std::size_t> last_rule;
  bool order_reported = false;

  for (const auto& subrecord : location.record->subrecords) {
    std::optional<std::size_t> rule_index;
    for (std::size_t index = 0; index < rules.size(); ++index) {
      if (rules[index].signature == subrecord.signature) {
        rule_index = index;
        break;
      }
    }
    if (!rule_index.has_value()) continue;

    ++counts[*rule_index];
    if (!order_reported && last_rule.has_value() && *rule_index < *last_rule) {
      issues.push_back(authoring_issue(
          record_key,
          location.record->header.signature.to_string() + "\\" +
              subrecord.signature.to_string(),
          "subrecord_order_invalid",
          "schema-known subrecords are not in the generated authoring order"));
      order_reported = true;
    }
    last_rule = rule_index;
  }

  for (std::size_t index = 0; index < rules.size(); ++index) {
    const auto count = counts[index];
    const auto& rule = rules[index];
    const auto path = location.record->header.signature.to_string() + "\\" +
                      rule.signature.to_string();
    if (count < rule.minimum) {
      issues.push_back(authoring_issue(
          record_key,
          path,
          "required_subrecord_missing",
          "authored record is missing required " + rule.signature.to_string() +
              " subrecord"));
    } else if (count > rule.maximum) {
      issues.push_back(authoring_issue(
          record_key,
          path,
          "subrecord_cardinality_invalid",
          "authored record contains too many " + rule.signature.to_string() +
              " subrecords"));
    }
  }

  const auto unpaired = mismatched_subrecord_pair(location.record->header.signature, rules, counts);
  if (unpaired.has_value()) {
    issues.push_back(authoring_issue(
        record_key,
        location.record->header.signature.to_string() + "\\" + unpaired->subrecord.to_string(),
        "subrecord_count_mismatch",
        "authored record has a different number of " + unpaired->subrecord.to_string() +
            " and " + unpaired->partner.to_string() + " subrecords"));
  }

  auto scalar_issues = preflight_record(*location.record, plugin.header.tes4_flags.is_localized());
  issues.insert(issues.end(),
                std::make_move_iterator(scalar_issues.begin()),
                std::make_move_iterator(scalar_issues.end()));
  return issues;
}

}  // namespace fo4::schema
