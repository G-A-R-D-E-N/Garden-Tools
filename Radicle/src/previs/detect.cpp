#include "fo4/previs/detect.hpp"

#include "fo4/plugin/container.hpp"
#include "fo4/previs/xcri.hpp"

#include <algorithm>
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
  }
  return "unknown";
}

CellPrevisStatus diagnose_cell(const CellPrevisInput& in) {
  CellPrevisStatus out;

  if (in.has_rvis && !in.has_visi) {
    out.broken = true;
    out.issues.push_back({PrevisIssue::rvis_without_visi, "RVIS is set but VISI is missing"});
  }

  // A VISI cell requires a valid tome; a missing or invalid one is broken.
  // Without RVIS the cell-named tome convention is unverified, so absence must skip.
  if (in.has_visi) {
    if (!in.uvd_checked) {
      out.skip_unsupported = true;
      out.skips.push_back("no .uvd lookup supplied; tome presence unverified");
    } else if (in.has_uvd) {
      if (!in.uvd_valid) {
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

  if (in.has_xcri && !in.xcri_unmeasured_version) {
    if (!in.cdx_checked) {
      out.skip_unsupported = true;
      out.skips.push_back("no CDX supplied; CDX coverage unverified");
    } else if (!in.has_cdx_row) {
      out.broken = true;
      out.issues.push_back(
          {PrevisIssue::cdx_row_missing, "XCRI is present but the CDX has no row for this cell"});
    }
    // Per-cell CSG rows are not measured; the per-plugin container check requires the .csg to validate.
    if (!in.oc_nif_checked) {
      out.skip_unsupported = true;
      out.skips.push_back("no OC NIF index supplied; combined meshes unverified");
    } else if (in.missing_oc_nifs != 0) {
      out.broken = true;
      out.issues.push_back({PrevisIssue::oc_nif_missing,
                            std::to_string(in.missing_oc_nifs) +
                                " XCRI mesh id(s) have no matching _OC.NIF"});
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
      diagnosis.status =
          unmeasured_skip("override with no previs of its own: engine behaviour unverified");
      out.push_back(std::move(diagnosis));
      continue;
    }
    std::optional<CellIssue> xcri_issue;
    try {
      const auto data = env::decode_cell(env, *ref);
      auto& in = diagnosis.input;
      in.has_pcmb = data.precombined_timestamp.has_value() && *data.precombined_timestamp != 0;
      in.has_visi = data.previs_timestamp.has_value() && *data.previs_timestamp != 0;
      in.has_rvis = data.previs_root.has_value() && !data.previs_root->owner().empty();
      in.has_xcri = has_xcri_bytes(*ref->record);
      in.has_xpri = !data.previs_references.empty();
      const bool unmeasured =
          in.has_xcri && ref->record->header.form_version < k_xcri_min_form_version;
      in.xcri_unmeasured_version = unmeasured;

      if (in.has_xcri && !unmeasured) {
        // Content validation requires a successful CELL-layer parse, with or without indexes.
        std::vector<std::uint32_t> mesh_ids;
        try {
          const auto combined = decode_combined_index(env, *ref);
          if (combined) mesh_ids = combined->mesh_ids;
        } catch (const std::exception& e) {
          xcri_issue = CellIssue{PrevisIssue::xcri_malformed,
                                 std::string("CELL XCRI failed to parse: ") + e.what()};
        }
        if (!xcri_issue && indexes.missing_oc_nifs) {
          const auto missing = indexes.missing_oc_nifs(diagnosis.cell, mesh_ids);
          in.oc_nif_checked = missing.has_value();
          if (missing) in.missing_oc_nifs = *missing;
        }
      }

      if (indexes.cdx_row) {
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
        in.uvd_valid = answer.found && answer.issues.empty();
      }

      const auto max_version = max_same_plugin_version(env, *ref, diagnosis.cell);
      if (in.has_pcmb && is_precalc_stale(*data.precombined_timestamp, max_version)) {
        in.pcmb_stale = true;
      }
      if (in.has_visi && is_precalc_stale(*data.previs_timestamp, max_version)) {
        in.visi_stale = true;
      }

      diagnosis.status = diagnose_cell(in);
      if (xcri_issue) {
        diagnosis.status.broken = true;
        diagnosis.status.issues.insert(diagnosis.status.issues.begin(), *xcri_issue);
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
