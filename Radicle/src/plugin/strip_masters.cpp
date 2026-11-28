#include "fo4/plugin/strip_masters.hpp"

#include "fo4/binary/raw.hpp"
#include "fo4/io/atomic_write.hpp"
#include "fo4/plugin/master_usage.hpp"
#include "fo4/plugin/reader.hpp"
#include "fo4/plugin/visit.hpp"
#include "fo4/plugin/writer.hpp"
#include "fo4/schema/condition_scopes.hpp"
#include "fo4/signature.hpp"

#include <algorithm>
#include <exception>
#include <utility>

namespace fo4 {
namespace {

std::string usage_refusal(const MasterUsage& usage) {
  std::string reason;
  if (usage.records_with_incomplete_vmad != 0) {
    reason += std::to_string(usage.records_with_incomplete_vmad) + " record(s) carry VMAD data Radicle cannot fully scan";
  }
  if (usage.undecodable_records != 0) {
    if (!reason.empty()) reason += "; ";
    reason += std::to_string(usage.undecodable_records) + " undecodable record(s) cannot be scanned";
  }
  return reason + "; nothing was written";
}

bool names_stripped_master(const CtdaCondition& condition, const PluginFormIdContext& context,
                           const std::set<PluginKey>& strip) {
  const Subrecord sub{sig_ctda, serialize_ctda_condition(condition)};
  for (const auto offset : ctda_formid_offsets(sub)) {
    if (offset + 4 > sub.data.size()) continue;
    const auto raw = binary::raw::read_u32(sub.data, offset);
    if (raw == 0) continue;
    try {
      if (strip.contains(resolve_plugin_form_id(RawFormId{raw}, context).owner())) return true;
    } catch (const FormIdError&) {
    }
  }
  return false;
}

}  // namespace

std::expected<StripMastersReport, StripMastersError> strip_masters_clean(
    const std::filesystem::path& source,
    const std::set<PluginKey>& strip,
    const std::optional<std::filesystem::path>& output) {
  try {
    auto plugin = read_plugin_file(source);
    const auto original = plugin.header.masters;
    StripMastersReport report;
    for (const auto& master : original) {
      if (strip.contains(master.key)) report.targets.emplace_back(master.key.filename());
    }

    std::optional<StripMastersError> failure;
    const auto context = plugin_form_id_context(plugin);
    const auto cobj = Signature::from_string("COBJ");
    visit_records(plugin, [&](Record& record) {
      if (failure || record.header.signature != cobj || record.undecodable) return;
      const auto read = schema::read_root_conditions(record);
      if (!read.ok) {
        failure = StripMastersError{"ConditionReadFailed",
                                    "COBJ conditions could not be read losslessly: " + record.form_key.to_string()};
        return;
      }
      std::vector<CtdaCondition> kept;
      for (const auto& condition : read.conditions) {
        if (!names_stripped_master(condition, context, strip)) kept.push_back(condition);
      }
      if (kept.size() == read.conditions.size()) return;
      const auto window = schema::root_condition_window(record);
      const auto written = write_ctda_conditions_in(record, window.begin, window.end, kept, window.insert_anchor);
      if (written != CtdaScopeWrite::replaced) {
        failure = StripMastersError{"ConditionWriteFailed",
                                    "COBJ conditions could not be rewritten: " + record.form_key.to_string()};
        return;
      }
      report.dropped_conditions += read.conditions.size() - kept.size();
      ++report.touched_records;
      record.original_range.clear();
      record.header.data_size = 0;
      record.uncompressed_size = 0;
    });
    if (failure) return std::unexpected(*failure);
    if (report.dropped_conditions != 0) plugin.original_range.clear();

    const auto usage = scan_master_usage(plugin);
    if (!usage.complete()) return std::unexpected(StripMastersError{"MasterUsageIncomplete", usage_refusal(usage)});
    const auto used = used_masters(plugin, usage);

    std::vector<MasterDescriptor> kept;
    for (const auto& master : original) {
      const bool targeted = strip.contains(master.key);
      const bool in_use = std::any_of(used.begin(), used.end(), [&](const auto& item) { return item.key == master.key; });
      if (targeted && !in_use) {
        report.removed.emplace_back(master.key.filename());
        continue;
      }
      if (targeted) report.survivors.emplace_back(master.key.filename());
      kept.push_back({master.key, master.kind});
    }

    const bool nothing_to_do = report.removed.empty() && report.dropped_conditions == 0;
    if (!output || nothing_to_do) {
      for (const auto& master : plugin.header.masters) report.masters_after.emplace_back(master.key.filename());
      return report;
    }

    if (!report.removed.empty()) plugin = remap_plugin_masters(std::move(plugin), std::move(kept));
    const auto bytes = write_plugin(plugin, *output);
    io::write_file_atomic(*output, bytes);
    for (const auto& master : plugin.header.masters) report.masters_after.emplace_back(master.key.filename());
    report.written = true;
    return report;
  } catch (const std::exception& error) {
    return std::unexpected(StripMastersError{"StripMastersFailed", error.what()});
  }
}

}  // namespace fo4
