import { mkdirSync, writeFileSync } from "node:fs";
import { dirname } from "node:path";

export function csvField(value: string | number | null): string {
  if (value === null || value === undefined) return "";
  const text = String(value);
  if (/[",\n\r]/.test(text)) {
    return '"' + text.replace(/"/g, '""') + '"';
  }
  return text;
}

export function writeCsv(
  path: string,
  header: string[],
  rows: (string | number | null)[][]
): void {
  const lines = [header.map(csvField).join(",")];
  for (const row of rows) lines.push(row.map(csvField).join(","));
  mkdirSync(dirname(path), { recursive: true });
  writeFileSync(path, lines.join("\n") + "\n", "utf8");
}
