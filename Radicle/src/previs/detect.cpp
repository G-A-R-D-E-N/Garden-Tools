#include "fo4/previs/detect.hpp"

#include "fo4/error.hpp"
#include "fo4/plugin/container.hpp"
#include "fo4/previs/xcri.hpp"

#include <algorithm>
#include <span>
#include <utility>

namespace fo4::previs {

std::string_view to_string(PrevisIssue issue) noexcept {
  switch (issue) {
    case PrevisIssue::rvis_without_visi:
      return "rvis_without_visi";
    case PrevisIssue::tome_missing:
      return "tome_missing";
    case PrevisIssue::tome_invalid:
      return "tome_invalid";
    case PrevisIssue::cdx_row_missing:
      return "cdx_row_missing";
    case PrevisIssue::oc_nif_missing:
      return "oc_nif_missing";
    case PrevisIssue::xcri_malformed:
      return "xcri_malformed";
    case PrevisIssue::pcmb_stale:
      return "pcmb_stale";
    case PrevisIssue::visi_stale:
      return "visi_stale";
    case PrevisIssue::rvis_malformed:
      return "rvis_malformed";
    case PrevisIssue::pcmb_ref_newer:
      return "pcmb_ref_newer";
    case PrevisIssue::pcmb_record_untimed:
      return "pcmb_record_untimed";
    case PrevisIssue::previs_disabled_by_override:
      return "previs_disabled_by_override";
    case PrevisIssue::pcmb_dropped_by_override:
      return "pcmb_dropped_by_override";
    case PrevisIssue::tome_unreadable:
      return "tome_unreadable";
    case PrevisIssue::csg_rows_missing:
      return "csg_rows_missing";
  }
  return "unknown";
}

CellPrevisStatus diagnose_cell(const CellPrevisInput& in) {
  CellPrevisStatus out;

  if (in.has_rvis && !in.has_visi) {
    out.broken = true;
    out.issues.push_back({PrevisIssue::rvis_without_visi, "RVIS is set but VISI is missing"});
  }

  if (in.rvis_malformed) {
    out.broken = true;
    out.issues.push_back({PrevisIssue::rvis_malformed, "RVIS is duplicated or not 4 bytes"});
  }

  // A VISI cell requires a valid tome; a missing or invalid one is broken.
  // Without RVIS the cell-named tome convention is unverified, so absence must skip.
  if (in.has_visi) {
    if (!in.uvd_checked) {
      out.skip_unsupported = true;
      out.skips.push_back("no .uvd lookup supplied; tome presence unverified");
    } else if (in.has_uvd) {
      if (in.uvd_unreadable) {
        out.broken = true;
        out.issues.push_back({PrevisIssue::tome_unreadable, "cluster .uvd exists but could not be read"});
      } else if (!in.uvd_valid) {
        out.broken = true;
        out.issues.push_back(
            {PrevisIssue::tome_invalid, "cluster .uvd failed its CRC or structure check"});
      }
    } else if (in.has_rvis) {
      out.broken = true;
      out.issues.push_back(
          {PrevisIssue::tome_missing, "VISI is set but the cluster .uvd is missing"});
    } else {
      out.skip_unsupported = true;
      out.skips.push_back("VISI without RVIS: no cell-named tome found; convention unverified");
    }
  }

  if (in.has_xcri && !in.xcri_unmeasured_version && !in.xcri_index_checks_suppressed) {
    if (!in.cdx_checked) {
      out.skip_unsupported = true;
      out.skips.push_back("no CDX supplied; CDX coverage unverified");
    } else if (!in.has_cdx_row) {
      out.broken = true;
      out.issues.push_back(
          {PrevisIssue::cdx_row_missing, "XCRI is present but the CDX has no row for this cell"});
    }
    if (!in.oc_nif_checked) {
      out.skip_unsupported = true;
      out.skips.push_back("no OC NIF index supplied; combined meshes unverified");
    } else if (in.missing_oc_nifs != 0) {
      out.broken = true;
      out.issues.push_back({PrevisIssue::oc_nif_missing,
                            std::to_string(in.missing_oc_nifs) +
                                " XCRI mesh id(s) have no matching _OC.NIF"});
    }
    // Constraint: a check that could not run must be a skip, never silence.
    if (in.csg_checked && in.csg_broken) {
      out.broken = true;
      out.issues.push_back(
          {PrevisIssue::csg_rows_missing,
           in.csg_detail.empty()
               ? std::string("the cell's OC meshes do not match the plugin's CSG rows")
               : in.csg_detail});
    } else if (!in.csg_checked && in.csg_not_run) {
      out.skip_unsupported = true;
      out.skips.push_back("geometry check did not run: " +
                          (in.csg_detail.empty() ? std::string("no reason recorded") : in.csg_detail));
    }
  }

  if (in.xcri_unmeasured_version) {
    out.skip_unsupported = true;
    out.skips.push_back("XCRI form version not measured");
  }

  if (in.pcmb_stale) {
    out.outdated = true;
    out.outdated_issues.push_back(
        {PrevisIssue::pcmb_stale, "PCMB stamp is older than a child record version date"});
  }
  if (in.visi_stale) {
    out.outdated = true;
    out.outdated_issues.push_back(
        {PrevisIssue::visi_stale, "VISI stamp is older than a child record version date"});
  }
  const auto outdate = [&out](PrevisIssue code, std::string detail) {
    out.outdated = true;
    out.outdated_issues.push_back({code, std::move(detail)});
  };
  if (in.override_refs_newer != 0) {
    outdate(PrevisIssue::pcmb_ref_newer,
            std::to_string(in.override_refs_newer) +
                " XCRI reference(s) have a winning override dated after the PCMB stamp");
  }
  if (in.override_records_untimed != 0) {
    outdate(PrevisIssue::pcmb_record_untimed,
            std::to_string(in.override_records_untimed) +
                " override(s) of a precombined reference or LAND carry no version date");
  }
  if (in.previs_disabled_by_override) {
    outdate(PrevisIssue::previs_disabled_by_override,
            "NoPreVis is set on a cell whose earlier version had working previs");
  }
  if (in.pcmb_zeroed_by_override) {
    outdate(PrevisIssue::pcmb_dropped_by_override,
            "PCMB is zeroed while an earlier version had working precombines");
  }
  if (in.xcri_removed_by_override) {
    outdate(PrevisIssue::pcmb_dropped_by_override,
            "XCRI is removed while an earlier version had working precombines");
  }
  return out;
}

namespace {

bool has_xcri_bytes(const Record& record) {
  for (const auto& sub : record.subrecords) {
    if (sub.signature == Signature::from_string("XCRI")) return true;
  }
  return false;
}

// Staleness must use the packed date in the record header timestamp, not version_control.
// Staleness must use the max over the winning CELL and its same-plugin non-partial children.
std::uint16_t max_same_plugin_version(const env::Environment& env, const env::RecordRef& cell_ref,
                                      const FormKey& cell_key) {
  std::uint16_t max_version = cell_ref.record->header.timestamp;
  for (const auto& child : env.cell_children(cell_key)) {
    if (child.ref == nullptr || child.ref->record == nullptr) continue;
    if (child.ref->plugin != cell_ref.plugin) continue;
    if (is_partial_form(*child.ref->record)) continue;
    max_version = std::max(max_version, child.ref->record->header.timestamp);
  }
  return max_version;
}

// Override invariant: a version owned by another plugin is what the winner replaces.
bool overrides_another_plugin(const env::Environment& env, const env::RecordRef& ref) {
  for (const auto& version : env.versions(ref.record->form_key)) {
    if (version.plugin != ref.plugin) return true;
  }
  return false;
}

constexpr std::uint32_t k_cell_no_previs_flag = 0x80;

std::size_t xcri_ref_count(const Record& record) {
  for (const auto& sub : record.subrecords) {
    if (sub.signature != Signature::from_string("XCRI")) continue;
    try {
      return parse_xcri_bytes(sub.data).refs.size();
    } catch (const parse_error&) {
      return 0;
    }
  }
  return 0;
}

// Byte equality is a lower bound on a no-op override: raw FormIDs differ across master lists.
bool same_payload(const Record& a, const Record& b) {
  return a.header.flags == b.header.flags &&
         std::equal(a.subrecords.begin(), a.subrecords.end(), b.subrecords.begin(),
                    b.subrecords.end(), [](const Subrecord& x, const Subrecord& y) {
                      return x.signature == y.signature && x.data == y.data;
                    });
}

// Constraint: the REFR rules mirror PJM FO4Check_PreVisbines.pas S:1088-1236; a REFR outside this CELL is not a timestamp case.
void scan_override_refs(const env::Environment& env, const env::RecordRef& cell_ref,
                        const env::CellData& data, std::span<const CombinedReference> refs,
                        CellPrevisInput& in) {
  const auto& cell_key = cell_ref.record->form_key;
  const auto cell_versions = env.versions(cell_key);
  if (cell_versions.empty()) return;
  const auto cell_master = cell_versions.front().plugin;
  const auto pcmb = *data.precombined_timestamp;
  for (const auto& entry : refs) {
    const auto* win = env.winner(entry.ref);
    if (win == nullptr || win->record->header.signature != Signature::from_string("REFR")) continue;
    const auto home = env.cell_of(*win);
    if (!home || *home != cell_key) continue;
    const auto versions = env.versions(entry.ref);
    const bool is_override = win->plugin != versions.front().plugin;
    const auto stamp = win->record->header.timestamp;
    if (stamp == 0) {
      if (!is_override) continue;
      const bool in_winning_cell =
          win->plugin == cell_ref.plugin ||
          (!data.grid && env.plugins().at(win->plugin).master_flag);
      const bool disables = versions.front().plugin == cell_master && in_winning_cell;
      if (disables || win->record->header.flags.is_deleted()) ++in.override_records_untimed;
      continue;
    }
    if (stamp <= pcmb || win->plugin == cell_ref.plugin) continue;
    const auto& master = *versions.front().record;
    if (is_override && master.header.timestamp <= pcmb && same_payload(*win->record, master)) {
      continue;
    }
    ++in.override_refs_newer;
  }
}

// Constraint: only a LAND override inside the winning CELL's plugin counts (PJM S:1243-1253).
void scan_winning_land(const env::Environment& env, const env::RecordRef& cell_ref,
                       const env::CellData& data, CellPrevisInput& in) {
  if (!data.grid) return;
  const auto& cell_key = cell_ref.record->form_key;
  const auto* land = env.cell_landscape(cell_key);
  if (land == nullptr) return;
  const auto versions = env.versions(land->record->form_key);
  for (const auto& version : versions) {
    if (version.plugin != cell_ref.plugin || version.plugin == versions.front().plugin) continue;
    const auto home = env.cell_of(version);
    if (!home || *home != cell_key) continue;
    if (version.record->header.timestamp == 0) ++in.override_records_untimed;
  }
}

// Constraint: walks lower non-partial versions only, as PJM S:989-1067 does; NoPreVis is CELL flag 0x80.
void scan_older_versions(const env::Environment& env, const env::RecordRef& cell_ref,
                         CellPrevisInput& in) {
  const auto versions = env.versions(cell_ref.record->form_key);
  if (versions.size() < 2 || env.plugins().at(versions.front().plugin).kind == PluginKind::light) {
    return;
  }
  std::size_t winner_at = versions.size();
  for (std::size_t i = versions.size(); i-- > 0;) {
    if (versions[i].plugin == cell_ref.plugin) {
      winner_at = i;
      break;
    }
  }
  if (winner_at == versions.size()) return;
  const bool no_previs = cell_ref.record->header.flags.test(k_cell_no_previs_flag);
  const bool zeroed = !in.has_pcmb && xcri_ref_count(*cell_ref.record) > 0;
  const bool xcri_gone = xcri_ref_count(*cell_ref.record) == 0;
  const bool bare_pcmb = in.has_pcmb && !in.has_xcri;
  if (!no_previs && !zeroed && !xcri_gone) return;
  bool older_xcri_seen = false;
  for (std::size_t i = winner_at; i-- > 0;) {
    const auto& older = versions[i];
    if (is_partial_form(*older.record)) continue;
    if (!no_previs && !zeroed && !has_xcri_bytes(*older.record)) continue;
    env::CellData data;
    try {
      data = env::decode_cell(env, older);
    } catch (const std::exception&) {
      continue;
    }
    const bool older_pcmb = data.precombined_timestamp.value_or(0) != 0;
    const auto older_refs = xcri_ref_count(*older.record);
    if (no_previs && data.previs_timestamp.value_or(0) != 0 &&
        !older.record->header.flags.test(k_cell_no_previs_flag)) {
      in.previs_disabled_by_override = true;
    }
    if (zeroed && older_pcmb) in.pcmb_zeroed_by_override = true;
    if (xcri_gone && older_pcmb && older_refs > 0) in.xcri_removed_by_override = true;
    if (xcri_gone && bare_pcmb && !older_xcri_seen && has_xcri_bytes(*older.record)) {
      older_xcri_seen = true;
      if (older_refs > 0) in.xcri_removed_by_override = true;
    }
  }
}

CellPrevisStatus unmeasured_skip(std::string reason) {
  CellPrevisStatus status;
  status.skip_unsupported = true;
  status.skips.push_back(std::move(reason));
  return status;
}

}  // namespace

std::vector<CellDiagnosis> diagnose_load_order(const env::Environment& env,
                                               const LoadOrderIndexes& indexes) {
  std::vector<CellDiagnosis> out;
  const auto cell_signature = Signature::from_string("CELL");
  for (const auto* ref : env.winners_of(cell_signature)) {
    if (ref == nullptr || ref->record == nullptr) continue;
    CellDiagnosis diagnosis;
    diagnosis.cell = ref->record->form_key;
    diagnosis.plugin = ref->plugin;
    if (env.plugins().at(ref->plugin).kind == PluginKind::light) {
      diagnosis.status = unmeasured_skip("light plugin layout not measured");
      out.push_back(std::move(diagnosis));
      continue;
    }
    if (indexes.plugin_ships_previs && !indexes.plugin_ships_previs(ref->plugin) &&
        overrides_another_plugin(env, *ref)) {
      // Override invariant: a plugin without previs hides XCRI index findings, not staleness.
      diagnosis.input.xcri_index_checks_suppressed = true;
    }
    std::optional<CellIssue> xcri_issue;
    try {
      const auto data = env::decode_cell(env, *ref);
      auto& in = diagnosis.input;
      in.has_pcmb = data.precombined_timestamp.has_value() && *data.precombined_timestamp != 0;
      in.has_visi = data.previs_timestamp.has_value() && *data.previs_timestamp != 0;
      in.has_rvis = data.previs_root.has_value() && !data.previs_root->owner().empty();
      in.rvis_malformed = env::decode_previs_root(env, *ref).malformed;
      in.has_xcri = has_xcri_bytes(*ref->record);
      in.has_xpri = !data.previs_references.empty();
      const bool unmeasured =
          in.has_xcri && ref->record->header.form_version < k_xcri_min_form_version;
      in.xcri_unmeasured_version = unmeasured;
      const bool suppress_xcri_index_checks = in.xcri_index_checks_suppressed;

      if (in.has_xcri && !unmeasured) {
        // Content validation requires a successful CELL-layer parse, with or without indexes.
        std::vector<std::uint32_t> mesh_ids;
        try {
          const auto combined = decode_combined_index(env, *ref);
          if (combined) mesh_ids = combined->mesh_ids;
          if (combined && in.has_pcmb) {
            try {
              scan_override_refs(env, *ref, data, combined->refs, in);
              scan_winning_land(env, *ref, data, in);
            } catch (const std::exception&) {
              in.override_refs_newer = 0;
              in.override_records_untimed = 0;
            }
          }
        } catch (const std::exception& e) {
          xcri_issue = CellIssue{PrevisIssue::xcri_malformed,
                                 std::string("CELL XCRI failed to parse: ") + e.what()};
        }
        if (!xcri_issue && indexes.missing_oc_nifs && !suppress_xcri_index_checks) {
          const auto missing = indexes.missing_oc_nifs(diagnosis.cell, mesh_ids);
          in.oc_nif_checked = missing.has_value();
          if (missing) in.missing_oc_nifs = *missing;
        }
        if (!xcri_issue && indexes.csg_rows && !suppress_xcri_index_checks) {
          const auto answer = indexes.csg_rows(diagnosis.cell, mesh_ids);
          if (answer) {
            in.csg_checked = answer->checked;
            in.csg_broken = answer->broken;
            in.csg_not_run = answer->not_run;
            if (!answer->issues.empty()) in.csg_detail = answer->issues.front();
          }
        }
      }

      if (indexes.cdx_row && !suppress_xcri_index_checks) {
        CdxCellTarget target;
        target.cell = diagnosis.cell;
        target.world = data.worldspace;
        if (data.grid) target.grid = *data.grid;
        const auto answer = indexes.cdx_row(ref->plugin, target);
        in.cdx_checked = answer.has_value();
        if (answer) in.has_cdx_row = *answer;
      }

      if (indexes.uvd && in.has_visi) {
        const FormKey tome_cell = in.has_rvis ? *data.previs_root : diagnosis.cell;
        const auto answer = indexes.uvd(tome_cell);
        in.uvd_checked = answer.checked;
        in.has_uvd = answer.found;
        in.uvd_unreadable = answer.found && answer.read_failed;
        in.uvd_valid = answer.found && !answer.read_failed && answer.issues.empty();
      }

      const auto max_version = max_same_plugin_version(env, *ref, diagnosis.cell);
      if (in.has_pcmb && is_precalc_stale(*data.precombined_timestamp, max_version)) {
        in.pcmb_stale = true;
      }
      if (in.has_visi && is_precalc_stale(*data.previs_timestamp, max_version)) {
        in.visi_stale = true;
      }
      scan_older_versions(env, *ref, in);

      diagnosis.status = diagnose_cell(in);
      if (xcri_issue) {
        diagnosis.status.broken = true;
        diagnosis.status.issues.insert(diagnosis.status.issues.begin(), *xcri_issue);
      }
      if (suppress_xcri_index_checks) {
        diagnosis.status.skip_unsupported = true;
        diagnosis.status.skips.push_back(
            "override with no previs of its own: engine behaviour unverified");
      }
    } catch (const std::exception& e) {
      // A failed decode must stay a skip: an unreadable CELL is not measured, not proven broken.
      diagnosis.status = unmeasured_skip(std::string("CELL failed to decode: ") + e.what());
      out.push_back(std::move(diagnosis));
      continue;
    }
    out.push_back(std::move(diagnosis));
  }
  return out;
}

}  // namespace fo4::previs
