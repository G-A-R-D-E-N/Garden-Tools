#include "check_errors_records.hpp"

#include "fo4/plugin/container.hpp"
#include "fo4/plugin/reader.hpp"
#include "fo4/schema/form_link_mixed.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <format>
#include <span>
#include <string>
#include <utility>

namespace fo4 {
namespace {

constexpr std::array<Signature, 1> k_reference_base{sig_name};
constexpr std::array<Signature, 3> k_partial_allowed{
    sig_edid, Signature::from_string("INOM"), Signature::from_string("INOA")};

std::string contents_except(const Record& record, std::span<const Signature> allowed) {
  std::string out;
  for (const auto& sub : record.subrecords) {
    if (std::find(allowed.begin(), allowed.end(), sub.signature) != allowed.end()) continue;
    if (!out.empty()) out += ", ";
    out += sub.signature.to_string();
  }
  return out;
}

void emit(std::vector<CheckFinding>& out, const Plugin& plugin, const FormKey& record,
         CheckCode code, CheckSeverity severity, std::string message) {
  CheckFinding finding;
  finding.code = code;
  finding.severity = severity;
  finding.plugin = plugin.key;
  finding.record = record;
  finding.message = std::move(message);
  out.push_back(std::move(finding));
}

}  // namespace

std::vector<CheckFinding> check_record_levels(const Plugin& plugin) {
  std::vector<CheckFinding> out;
  const auto master_count = plugin.header.masters.size();
  const bool light = plugin.header.tes4_flags.is_small_file();
  const auto context = plugin_form_id_context(plugin);
  for (const auto* record : collect_records(plugin)) {
    const auto raw = record->header.form_id;
    if (raw.is_null()) continue;
    const auto index = static_cast<std::size_t>(raw.index_byte());
    const auto local = raw.local_24();
    if (light && index >= master_count && local > k_light_local_mask) {
      emit(out, plugin, resolve_plugin_link(raw, context), CheckCode::light_object_id_overflow,
          CheckSeverity::error, std::format("ObjectID {:06X} is invalid for a light module", local));
      continue;
    }
    if (index > master_count) {
      emit(out, plugin, resolve_plugin_link(raw, context), CheckCode::internal_form_id_mismatch,
          CheckSeverity::warning,
          std::format("internal file form ID master index {:02X} is past the {} masters", index,
                      master_count));
      continue;
    }
    if (record->header.flags.is_deleted()) {
      const std::span<const Signature> allowed =
          is_placed_reference_signature(record->header.signature)
              ? std::span<const Signature>{k_reference_base}
              : std::span<const Signature>{};
      const auto contents = contents_except(*record, allowed);
      if (!contents.empty()) {
        emit(out, plugin, record->form_key, CheckCode::deleted_record_content, CheckSeverity::error,
            "Record marked as deleted, but contains: " + contents);
      }
      continue;
    }
    if (is_partial_form(*record)) {
      const auto contents = contents_except(*record, k_partial_allowed);
      if (!contents.empty()) {
        emit(out, plugin, record->form_key, CheckCode::partial_form_content, CheckSeverity::error,
            "Record is Partial Form, but contains: " + contents);
      }
    }
  }
  return out;
}

}  // namespace fo4
