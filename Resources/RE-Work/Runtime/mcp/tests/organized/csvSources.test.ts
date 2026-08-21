import { mkdtempSync, rmSync, writeFileSync } from "node:fs";
import { tmpdir } from "node:os";
import { join } from "node:path";
import { readdirSync } from "node:fs";
import { afterEach, describe, expect, it } from "vitest";
import { RE_ROOT } from "../../src/paths.js";
import {
  CSV_SOURCES,
  parseOrganizedCsv,
  readOrganizedCsv,
  sourceNameFor,
} from "../../src/organized/csvSources.js";

const cleanups: (() => void)[] = [];
afterEach(() => {
  while (cleanups.length) cleanups.pop()!();
});

function writeFixtureCsv(content: string): string {
  const dir = mkdtempSync(join(tmpdir(), "re-csv-"));
  cleanups.push(() => rmSync(dir, { recursive: true, force: true }));
  const path = join(dir, "fixture.csv");
  writeFileSync(path, content);
  return path;
}

describe("CSV_SOURCES completeness", () => {
  it("covers every real file in Organized/, and only real files", () => {
    const real = readdirSync(join(RE_ROOT, "Organized")).filter((f) => f.endsWith(".csv"));
    const declared = CSV_SOURCES.map((s) => s.file);
    expect(declared.sort()).toEqual(real.sort());
  });

  it("has exactly one entry per file, no duplicates", () => {
    const files = CSV_SOURCES.map((s) => s.file);
    expect(new Set(files).size).toBe(files.length);
  });

  it("declares at least a name column or an address column for every file", () => {
    for (const mapping of CSV_SOURCES) {
      expect(mapping.addressCol || mapping.nameACol || mapping.nameBCol, mapping.file).toBeTruthy();
    }
  });
});

describe("sourceNameFor", () => {
  it("strips the .csv extension", () => {
    expect(sourceNameFor("applied_safe_gaps.csv")).toBe("applied_safe_gaps");
  });
});

describe("parseOrganizedCsv — representative shapes", () => {
  it("address,name shape (all_inconclusive_resolved / final_2_held)", () => {
    const mapping = CSV_SOURCES.find((s) => s.file === "all_inconclusive_resolved.csv")!;
    const rows = parseOrganizedCsv(mapping, [{ address: "0x1414d85f0", name: "hknpShape::getType" }]);
    expect(rows).toEqual([
      {
        rowNo: 1,
        address: "0x1414d85f0",
        rva: 0x1414d85f0 - 0x140000000,
        nameA: null,
        nameB: "hknpShape::getType",
        kind: null,
        note: null,
        extra: {},
      },
    ]);
  });

  it("address,old_name,new_name,kind,note shape (applied_*)", () => {
    const mapping = CSV_SOURCES.find((s) => s.file === "applied_safe_gaps.csv")!;
    const rows = parseOrganizedCsv(mapping, [
      { address: "0x140724fa0", old_name: "Old::Name", new_name: "New::Name", kind: "reassign", note: "n" },
    ]);
    expect(rows[0]).toMatchObject({ nameA: "Old::Name", nameB: "New::Name", kind: "reassign", note: "n" });
  });

  it("applied_misname_corrections uses method as kind", () => {
    const mapping = CSV_SOURCES.find((s) => s.file === "applied_misname_corrections.csv")!;
    const rows = parseOrganizedCsv(mapping, [
      { address: "0x140724fa0", old_name: "A", new_name: "B", method: "decompile", note: "n" },
    ]);
    expect(rows[0].kind).toBe("decompile");
  });

  it("class,slot,expected_method shape has no address (commonlib_vtable_slots)", () => {
    const mapping = CSV_SOURCES.find((s) => s.file === "commonlib_vtable_slots.csv")!;
    const rows = parseOrganizedCsv(mapping, [{ class: "Actor", slot: "5", expected_method: "Update" }]);
    expect(rows[0]).toMatchObject({ address: null, rva: null, nameA: "Actor", nameB: "Update", kind: "5" });
  });

  it("crossref_163_disagreements_vs_155 reads the dotted verdict column literally", () => {
    const mapping = CSV_SOURCES.find((s) => s.file === "crossref_163_disagreements_vs_155.csv")!;
    const rows = parseOrganizedCsv(mapping, [
      {
        address: "0x140749ea0",
        existing_name: "A",
        decoded_name: "B",
        note: "n",
        "verdict_vs_1.10.155": "inconclusive",
      },
    ]);
    expect(rows[0].kind).toBe("inconclusive");
  });

  it("vtable_audit_report bundles class and slot into extra", () => {
    const mapping = CSV_SOURCES.find((s) => s.file === "vtable_audit_report.csv")!;
    const rows = parseOrganizedCsv(mapping, [
      {
        class: "ActorPackageData",
        slot: "0",
        expected_method: "~ActorPackageData",
        actual_name: "~ActorPackageData",
        verdict: "match",
        address: "0x140724fa0",
      },
    ]);
    expect(rows[0]).toMatchObject({
      nameA: "~ActorPackageData",
      nameB: "~ActorPackageData",
      kind: "match",
      extra: { class: "ActorPackageData", slot: "0" },
    });
    expect(rows[0].rva).not.toBeNull();
  });

  it("decompile_targets: addr: prefix becomes a bare RVA, not a full VA", () => {
    const mapping = CSV_SOURCES.find((s) => s.file === "decompile_targets.csv")!;
    const rows = parseOrganizedCsv(mapping, [
      { target: "addr:0x5f90b0", category: "Quests", note: "n" },
    ]);
    expect(rows[0].address).toBe("0x5f90b0");
    expect(rows[0].rva).toBe(0x5f90b0);
    expect(rows[0].nameA).toBeNull();
  });

  it("decompile_targets: a bare name with no addr: prefix becomes nameA, not an address", () => {
    const mapping = CSV_SOURCES.find((s) => s.file === "decompile_targets.csv")!;
    const rows = parseOrganizedCsv(mapping, [
      { target: "TESQuestStageItem::Resolve", category: "Quests", note: "n" },
    ]);
    expect(rows[0].address).toBeNull();
    expect(rows[0].rva).toBeNull();
    expect(rows[0].nameA).toBe("TESQuestStageItem::Resolve");
  });

  it("an address that fails to parse yields rva=null without throwing", () => {
    const mapping = CSV_SOURCES.find((s) => s.file === "vtable_audit_fixes.csv")!;
    const rows = parseOrganizedCsv(mapping, [
      { address: "not-hex", expected_qualified_name: "Foo::Bar" },
    ]);
    expect(rows[0].rva).toBeNull();
  });

  it("1-based rowNo across multiple rows", () => {
    const mapping = CSV_SOURCES.find((s) => s.file === "final_2_held.csv")!;
    const rows = parseOrganizedCsv(mapping, [
      { address: "0x140DE5F50", name: "A" },
      { address: "0x140705990", name: "B" },
    ]);
    expect(rows.map((r) => r.rowNo)).toEqual([1, 2]);
  });

  it("normalizes empty cells to null (hknpShape_authoritative ghost column)", () => {
    const mapping = CSV_SOURCES.find((s) => s.file === "hknpShape_authoritative.csv")!;
    // existing_name is an all-empty column in the real file — must come out null, not "".
    const rows = parseOrganizedCsv(mapping, [
      { address: "0x1414d85f0", existing_name: "", decoded_name: "hknpShape::getType" },
    ]);
    expect(rows[0]).toMatchObject({
      address: "0x1414d85f0",
      rva: 0x1414d85f0 - 0x140000000,
      nameA: null,
      nameB: "hknpShape::getType",
    });
    // An empty address cell is also null, and does not turn the row into a name row.
    const emptyAddr = parseOrganizedCsv(mapping, [
      { address: "", existing_name: "x", decoded_name: "y" },
    ]);
    expect(emptyAddr[0]).toMatchObject({ address: null, rva: null, nameA: "x", nameB: "y" });
  });
});

describe("readOrganizedCsv", () => {
  it("reads header and rows, stripping a BOM", () => {
    const path = writeFixtureCsv("\uFEFFaddress,name\n0x140724fa0,Foo::Bar\n");
    const { columns, rows } = readOrganizedCsv(path);
    expect(columns).toEqual(["address", "name"]);
    expect(rows).toEqual([{ address: "0x140724fa0", name: "Foo::Bar" }]);
  });

  it("handles a quoted field containing a comma", () => {
    const path = writeFixtureCsv('address,note\n0x1,"contains, a comma"\n');
    expect(readOrganizedCsv(path).rows[0].note).toBe("contains, a comma");
  });

  it("returns empty columns for a header-only file", () => {
    const path = writeFixtureCsv("address,name\n");
    expect(readOrganizedCsv(path)).toEqual({ columns: [], rows: [] });
  });
});
