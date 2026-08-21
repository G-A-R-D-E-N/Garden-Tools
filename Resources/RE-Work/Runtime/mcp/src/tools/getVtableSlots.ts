import { getDb } from "../db.js";

export interface VtableSlotResult {
  slot: number;
  expectedMethod: string | null;
  actualName: string | null;
  verdict: string | null;
  address: string | null;
}

export function getVtableSlots(className: string): VtableSlotResult[] {
  const db = getDb();

  const expected = db
    .prepare("SELECT slot, expected_method FROM vtable_expected WHERE class = ? ORDER BY slot")
    .all(className) as { slot: number; expected_method: string | null }[];

  const audited = db
    .prepare("SELECT slot, actual_name, verdict, address FROM vtable_audit WHERE class = ?")
    .all(className) as { slot: number; actual_name: string | null; verdict: string | null; address: string | null }[];
  const auditBySlot = new Map(audited.map((row) => [row.slot, row]));

  return expected.map((row) => {
    const audit = auditBySlot.get(row.slot);
    return {
      slot: row.slot,
      expectedMethod: row.expected_method,
      actualName: audit?.actual_name ?? null,
      verdict: audit?.verdict ?? "not audited",
      address: audit?.address ?? null,
    };
  });
}
