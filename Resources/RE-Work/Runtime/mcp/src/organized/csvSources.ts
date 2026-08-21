import { readFileSync } from "node:fs";
import { parse } from "csv-parse/sync";
import { parseAddressToRva } from "../db.js";

export interface OrganizedFileMapping {
  file: string;
  addressCol: string | null;
  addressPrefix?: string;
  nameACol: string | null;
  nameBCol: string | null;
  kindCol: string | null;
  noteCol: string | null;
  extraCols: string[];
}

// One entry per real file in Organized/. Every declared column name was verified
// against the real files' headers AND data (2026-08-13, see the plan ledger
// "TASK 3 PRE-FLIGHT"): all 24 files match exactly, no missing columns, no ragged
// rows. Known data characteristics: hknpShape_authoritative.csv's existing_name is
// an all-empty ghost column (nameA is always null there — by data, not by mapping),
// and decompile_targets.csv's target column holds either "addr:<bare RVA>" or a bare
// symbol name (the addressPrefix/dual-purpose branch below).
export const CSV_SOURCES: OrganizedFileMapping[] = [
  { file: "all_inconclusive_resolved.csv", addressCol: "address", nameACol: null, nameBCol: "name", kindCol: null, noteCol: null, extraCols: [] },
  { file: "applied_hknpShape.csv", addressCol: "address", nameACol: "old_name", nameBCol: "new_name", kindCol: "kind", noteCol: "note", extraCols: [] },
  { file: "applied_misname_corrections.csv", addressCol: "address", nameACol: "old_name", nameBCol: "new_name", kindCol: "method", noteCol: "note", extraCols: [] },
  { file: "applied_reassignments.csv", addressCol: "address", nameACol: "old_name", nameBCol: "new_name", kindCol: "kind", noteCol: "note", extraCols: [] },
  { file: "applied_safe_gaps.csv", addressCol: "address", nameACol: "old_name", nameBCol: "new_name", kindCol: "kind", noteCol: "note", extraCols: [] },
  { file: "applied_std_func.csv", addressCol: "address", nameACol: "old_name", nameBCol: "new_name", kindCol: "kind", noteCol: "note", extraCols: [] },
  { file: "commonlib_vtable_slots.csv", addressCol: null, nameACol: "class", nameBCol: "expected_method", kindCol: "slot", noteCol: null, extraCols: [] },
  { file: "confirmed_misnames_to_apply.csv", addressCol: "address", nameACol: "old_name", nameBCol: "claimed_real_name", kindCol: null, noteCol: "evidence", extraCols: [] },
  { file: "crossref_163_disagreements_vs_155.csv", addressCol: "address", nameACol: "existing_name", nameBCol: "decoded_name", kindCol: "verdict_vs_1.10.155", noteCol: "note", extraCols: [] },
  { file: "crossref_review_candidates.csv", addressCol: "address", nameACol: "existing_name", nameBCol: "decoded_name", kindCol: null, noteCol: null, extraCols: [] },
  { file: "crossref_safe_naming_gaps.csv", addressCol: "address", nameACol: "existing_name", nameBCol: "decoded_name", kindCol: null, noteCol: null, extraCols: [] },
  { file: "decompile_targets.csv", addressCol: "target", addressPrefix: "addr:", nameACol: "target", nameBCol: null, kindCol: "category", noteCol: "note", extraCols: [] },
  { file: "final_2_held.csv", addressCol: "address", nameACol: null, nameBCol: "name", kindCol: null, noteCol: null, extraCols: [] },
  { file: "hknpShape_authoritative.csv", addressCol: "address", nameACol: "existing_name", nameBCol: "decoded_name", kindCol: null, noteCol: null, extraCols: [] },
  { file: "hknpShape_rename.csv", addressCol: "address", nameACol: null, nameBCol: "revert_to_name", kindCol: null, noteCol: null, extraCols: [] },
  { file: "inconclusive_resolved_rename.csv", addressCol: "address", nameACol: null, nameBCol: "revert_to_name", kindCol: null, noteCol: null, extraCols: [] },
  { file: "misnames_to_revert.csv", addressCol: "address", nameACol: "wrongly_renamed_to", nameBCol: "revert_to_commonlib_confirmed_name", kindCol: null, noteCol: null, extraCols: [] },
  { file: "misnames_to_revert_simple.csv", addressCol: "address", nameACol: null, nameBCol: "revert_to_name", kindCol: null, noteCol: null, extraCols: [] },
  { file: "reassign_apply.csv", addressCol: "address", nameACol: "existing_name", nameBCol: "decoded_name", kindCol: null, noteCol: null, extraCols: [] },
  { file: "safe_gaps_apply.csv", addressCol: "address", nameACol: "existing_name", nameBCol: "decoded_name", kindCol: null, noteCol: null, extraCols: [] },
  { file: "safe_gaps_class_reassigned_deferred.csv", addressCol: "address", nameACol: "existing_name", nameBCol: "decoded_name", kindCol: null, noteCol: null, extraCols: [] },
  { file: "std_func_sweep.csv", addressCol: "address", nameACol: "existing_name", nameBCol: "decoded_name", kindCol: null, noteCol: null, extraCols: [] },
  { file: "vtable_audit_fixes.csv", addressCol: "address", nameACol: null, nameBCol: "expected_qualified_name", kindCol: null, noteCol: null, extraCols: [] },
  { file: "vtable_audit_report.csv", addressCol: "address", nameACol: "expected_method", nameBCol: "actual_name", kindCol: "verdict", noteCol: null, extraCols: ["class", "slot"] },
];

export interface ParsedOrganizedRow {
  rowNo: number;
  address: string | null;
  rva: number | null;
  nameA: string | null;
  nameB: string | null;
  kind: string | null;
  note: string | null;
  extra: Record<string, string>;
}

// Empty cells are as absent as undefined ones — hknpShape_authoritative.csv carries an
// all-empty existing_name column, so "" must not leak out as a name (or an address).
const cell = (v: string | undefined): string | null =>
  v === undefined || v === "" ? null : v;

export function parseOrganizedCsv(
  mapping: OrganizedFileMapping,
  rows: Record<string, string>[]
): ParsedOrganizedRow[] {
  return rows.map((row, i) => {
    let addressRaw = mapping.addressCol ? cell(row[mapping.addressCol]) : null;

    if (mapping.addressPrefix) {
      if (addressRaw && addressRaw.startsWith(mapping.addressPrefix)) {
        addressRaw = addressRaw.slice(mapping.addressPrefix.length);
      } else {
        // Not address-shaped after all — decompile_targets.csv's bare-name rows land here.
        addressRaw = null;
      }
    }

    const rva = addressRaw ? parseAddressToRva(addressRaw) : null;

    let nameA: string | null = null;
    if (mapping.nameACol) {
      if (mapping.nameACol === mapping.addressCol) {
        // Dual-purpose column: only treat it as a name when it did NOT resolve as an address.
        nameA = rva === null ? cell(row[mapping.nameACol]) : null;
      } else {
        nameA = cell(row[mapping.nameACol]);
      }
    }

    const extra: Record<string, string> = {};
    for (const col of mapping.extraCols) {
      if (row[col] !== undefined) extra[col] = row[col];
    }

    return {
      rowNo: i + 1,
      address: addressRaw,
      rva,
      nameA,
      nameB: mapping.nameBCol ? cell(row[mapping.nameBCol]) : null,
      kind: mapping.kindCol ? cell(row[mapping.kindCol]) : null,
      note: mapping.noteCol ? cell(row[mapping.noteCol]) : null,
      extra,
    };
  });
}

export function sourceNameFor(file: string): string {
  return file.endsWith(".csv") ? file.slice(0, -4) : file;
}

export function readOrganizedCsv(path: string): { columns: string[]; rows: Record<string, string>[] } {
  const content = readFileSync(path, "utf8").replace(/^\uFEFF/, "");
  const rows = parse(content, { columns: true, skip_empty_lines: true }) as Record<string, string>[];
  const columns = rows.length > 0 ? Object.keys(rows[0]) : [];
  return { columns, rows };
}
