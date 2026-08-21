import { mkdtempSync, readFileSync, rmSync } from "node:fs";
import { tmpdir } from "node:os";
import { join } from "node:path";
import { parse } from "csv-parse/sync";
import { afterEach, describe, expect, it } from "vitest";
import { csvField, writeCsv } from "../../src/organized/writeCsv.js";

const cleanups: (() => void)[] = [];
afterEach(() => {
  while (cleanups.length) cleanups.pop()!();
});

function tempPath(name: string): string {
  const dir = mkdtempSync(join(tmpdir(), "re-writecsv-"));
  cleanups.push(() => rmSync(dir, { recursive: true, force: true }));
  return join(dir, name);
}

describe("csvField", () => {
  it("passes a plain value through unquoted", () => {
    expect(csvField("Actor::SetFlag")).toBe("Actor::SetFlag");
  });

  it("quotes and escapes a value containing a comma", () => {
    expect(csvField("a, b")).toBe('"a, b"');
  });

  it("escapes an embedded quote by doubling it", () => {
    expect(csvField('say "hi"')).toBe('"say ""hi"""');
  });

  it("renders null as an empty field", () => {
    expect(csvField(null)).toBe("");
  });

  it("renders a number as its string form", () => {
    expect(csvField(42)).toBe("42");
  });
});

describe("writeCsv", () => {
  it("creates parent directories that do not exist yet", () => {
    const path = tempPath("nested/dir/out.csv");
    writeCsv(path, ["a", "b"], [["1", "2"]]);
    expect(readFileSync(path, "utf8")).toContain("a,b");
  });

  it("round-trips through the same CSV reader the corpus uses", () => {
    const path = tempPath("out.csv");
    writeCsv(path, ["address", "note"], [
      ["0x1", "contains, a comma"],
      ["0x2", 'has "quotes"'],
      ["0x3", null],
    ]);
    const rows = parse(readFileSync(path, "utf8"), { columns: true }) as Record<string, string>[];
    expect(rows).toEqual([
      { address: "0x1", note: "contains, a comma" },
      { address: "0x2", note: 'has "quotes"' },
      { address: "0x3", note: "" },
    ]);
  });
});
