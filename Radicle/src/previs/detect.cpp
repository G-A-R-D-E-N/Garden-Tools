#include "fo4/previs/detect.hpp"

#include "fo4/plugin/container.hpp"
#include "fo4/previs/xcri.hpp"

#include <algorithm>

namespace fo4::previs {

CellPrevisStatus diagnose_cell(const CellPrevisInput& in) {
  CellPrevisStatus out;
  const bool wants_precombine = in.has_pcmb || in.has_xcri || in.has_cdx_row || in.has_csg_rows;
  const bool wants_previs = in.has_visi || in.has_rvis || in.has_xpri || in.has_uvd;

  if (in.has_visi && !in.has_rvis) {
    out.broken = true;
    out.issues.push_back("VISI is set but RVIS is missing");
  }
  if (!in.has_visi && in.has_rvis) {
    out.broken = true;
    out.issues.push_back("RVIS is set but VISI is missing");
  }
  if (in.has_visi && in.uvd_checked && !in.has_uvd) {
    out.broken = true;
    out.issues.push_back("VISI is set but the cluster .uvd is missing");
  }
  if (in.has_visi && in.has_uvd && !in.uvd_valid) {
    out.broken = true;
    out.issues.push_back("cluster .uvd failed CRC or structure check");
  }
  if (wants_precombine && !in.has_xcri && !in.xcri_unmeasured_version) {
    out.broken = true;
    out.issues.push_back("precombined cell has no XCRI");
  }
  if (in.has_xcri && in.cdx_checked && !in.has_cdx_row) {
    out.broken = true;
    out.issues.push_back("XCRI is present but the CDX has no row for this cell");
  }
  if (in.has_xcri && in.csg_checked && !in.has_csg_rows) {
    out.broken = true;
    out.issues.push_back("XCRI is present but the CSG has no rows for this cell");
  }
  if (in.pcmb_stale) {
    out.broken = true;
    out.issues.push_back("PCMB stamp is older than a child record version date");
  }
  if (in.visi_stale) {
    out.broken = true;
    out.issues.push_back("VISI stamp is older than a child record version date");
  }
  if (in.xcri_unmeasured_version) {
    out.skip_unsupported = true;
    out.skips.push_back("XCRI form version not measured");
  }
  if (wants_previs && !in.uvd_checked) {
    out.skip_unsupported = true;
    out.skips.push_back("no .uvd lookup supplied; .uvd presence unverified");
  }
  if (in.has_xcri && !in.cdx_checked) {
    out.skip_unsupported = true;
    out.skips.push_back("no CDX supplied; CDX coverage unverified");
  }
  if (in.has_xcri && !in.csg_checked) {
    out.skip_unsupported = true;
    out.skips.push_back("no CSG cell set supplied; CSG coverage unverified");
  }
  return out;
}

namespace {

// A CELL carries XCRI when any XCRI subrecord is present; checks require a measured version.
bool has_xcri_bytes(const Record& record) {
  for (const auto& sub : record.subrecords) {
    if (sub.signature == Signature::from_string("XCRI")) return true;
  }
  return false;
}

// Staleness invariant: max version_control over the winning CELL and its same-plugin
// non-partial children; a zero date must not move the maximum.
std::uint16_t max_same_plugin_version(const env::Environment& env, const env::RecordRef& cell_ref,
                                      const FormKey& cell_key) {
  std::uint16_t max_version = cell_ref.record->header.version_control;
  for (const auto& child : env.cell_children(cell_key)) {
    if (child.ref == nullptr || child.ref->record == nullptr) continue;
    if (child.ref->plugin != cell_ref.plugin) continue;
    if (is_partial_form(*child.ref->record)) continue;
    max_version = std::max(max_version, child.ref->record->header.version_control);
  }
  return max_version;
}

}  // namespace

std::vector<CellDiagnosis> diagnose_load_order(const env::Environment& env,
                                               const LoadOrderIndexes& indexes) {
  std::vector<CellDiagnosis> out;
  for (const auto* ref : env.winners_of(Signature::from_string("CELL"))) {
    if (ref == nullptr || ref->record == nullptr) continue;
    CellDiagnosis diagnosis;
    diagnosis.cell = ref->record->form_key;
    const std::uint32_t local = diagnosis.cell.local().value() & 0xFFFFFFu;
    try {
      const auto data = env::decode_cell(env, *ref);
      auto& in = diagnosis.input;
      in.has_pcmb = data.precombined_timestamp.has_value() && *data.precombined_timestamp != 0;
      in.has_visi = data.previs_timestamp.has_value() && *data.previs_timestamp != 0;
      in.has_rvis = data.previs_root.has_value();
      in.has_xcri = has_xcri_bytes(*ref->record);
      in.has_xpri = !data.previs_references.empty();
      const bool unmeasured =
          in.has_xcri && ref->record->header.form_version < k_xcri_min_form_version;
      in.xcri_unmeasured_version = unmeasured;
      std::string xcri_error;
      if (in.has_xcri && !unmeasured) {
        try {
          (void)decode_combined_index(env, *ref);
        } catch (const std::exception& e) {
          xcri_error = e.what();
        }
      }
      const CdxFile* cdx = indexes.cdx_for_plugin != nullptr ? indexes.cdx_for_plugin(ref->plugin)
                                                             : indexes.cdx;
      in.cdx_checked = cdx != nullptr;
      in.has_cdx_row = cdx != nullptr && cdx_has_data_for_cell(*cdx, local);
      in.csg_checked = indexes.csg_cells != nullptr;
      in.has_csg_rows = indexes.csg_cells != nullptr && indexes.csg_cells->count(local) != 0;
      in.uvd_checked = static_cast<bool>(indexes.uvd_issues);
      if (indexes.uvd_issues) {
        if (const auto issues = indexes.uvd_issues(local)) {
          in.has_uvd = true;
          in.uvd_valid = issues->empty();
        }
      }
      const auto max_version = max_same_plugin_version(env, *ref, diagnosis.cell);
      if (in.has_pcmb) in.pcmb_stale = is_precalc_stale(*data.precombined_timestamp, max_version);
      if (in.has_visi) in.visi_stale = is_precalc_stale(*data.previs_timestamp, max_version);
      diagnosis.status = diagnose_cell(in);
      if (!xcri_error.empty()) {
        diagnosis.status.broken = true;
        diagnosis.status.issues.insert(diagnosis.status.issues.begin(),
                                       "CELL XCRI failed to parse: " + xcri_error);
      }
    } catch (const std::exception& e) {
      diagnosis.status.broken = true;
      diagnosis.status.issues.push_back(std::string("CELL failed to decode: ") + e.what());
      out.push_back(std::move(diagnosis));
      continue;
    }
    out.push_back(std::move(diagnosis));
  }
  return out;
}

}  // namespace fo4::previs
