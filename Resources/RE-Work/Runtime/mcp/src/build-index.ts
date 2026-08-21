import { createReadStream, existsSync, readFileSync, readdirSync, unlinkSync } from "node:fs";
import { join } from "node:path";
import { createInterface } from "node:readline";
import { pathToFileURL } from "node:url";
import Database from "better-sqlite3";
import { parse } from "csv-parse/sync";
import {
  BINS_DIR,
  DB_PATH,
  IDA_FUNCTIONS_OG_AE_CSV,
  ORGANIZED_DIR,
  PDB_DUMP_PATH,
  SYMBOL_DUMPS,
} from "./paths.js";
import {
  extractCallingConvention,
  extractSignatureName,
  parsePdbLine,
} from "./pdb/parsePdbDump.js";
import {
  CSV_SOURCES,
  parseOrganizedCsv,
  readOrganizedCsv,
  sourceNameFor,
} from "./organized/csvSources.js";

// Confirmed from the OG symbol dump's own header ("# image base 0x140000000"); the audit-trail
// CSVs store full VAs at this base, normalized to RVA here so lookups aren't case/padding-sensitive.
const IMAGE_BASE = 0x140000000;

function vaHexToRva(hex: string | undefined | null): number | null {
  if (!hex) return null;
  const va = parseInt(hex.replace(/^0x/i, ""), 16);
  if (Number.isNaN(va)) return null;
  return va - IMAGE_BASE;
}

function rvaHexToRva(hex: string | undefined | null): number | null {
  if (!hex) return null;
  const rva = parseInt(hex.replace(/^0x/i, ""), 16);
  return Number.isNaN(rva) ? null : rva;
}

// Organized/<version>/<Category>.txt shares the symbol dump's line grammar, minus the signature
// half, which subsystem_index does not need — symbols already carries it.
const CATEGORY_LINE_RE = /^\s*func \[0x([0-9A-Fa-f]+)(?: \| sizeof=\d+)?\] (\S+) :: /;

export function parseCategoryFile(text: string): { rva: number; name: string }[] {
  const rows: { rva: number; name: string }[] = [];
  for (const line of text.split("\n")) {
    const match = line.match(CATEGORY_LINE_RE);
    if (!match) continue;
    rows.push({ rva: parseInt(match[1], 16), name: match[2] });
  }
  return rows;
}

const BIN_FILES = [
  "version-1-10-130-0.bin",
  "version-1-10-138-0.bin",
  "version-1-10-162-0.bin",
  "version-1-10-163-0.bin",
  "version-1-10-980-0.bin",
  "version-1-10-984-0.bin",
  "version-1-11-137-0.bin",
  "version-1-11-159-0.bin",
  "version-1-11-169-0.bin",
  "version-1-11-191-0.bin",
  "version-1-11-221-0.bin",
];

function versionFromBinFilename(filename: string): string {
  const match = filename.match(/^version-(\d+)-(\d+)-(\d+)-\d+\.bin$/);
  if (!match) throw new Error(`Unrecognized bin filename: ${filename}`);
  return `${match[1]}.${match[2]}.${match[3]}`;
}

export function createSchema(db: Database.Database): void {
  db.exec(`
    CREATE TABLE address_library (
      version TEXT NOT NULL,
      id INTEGER NOT NULL,
      rva INTEGER NOT NULL
    );
    CREATE INDEX idx_address_library_id ON address_library(version, id);
    CREATE INDEX idx_address_library_rva ON address_library(version, rva);

    CREATE TABLE symbols (
      version TEXT NOT NULL,
      rva INTEGER NOT NULL,
      name TEXT NOT NULL,
      signature TEXT NOT NULL,
      sizeof INTEGER
    );
    CREATE INDEX idx_symbols_rva ON symbols(version, rva);
    CREATE INDEX idx_symbols_name ON symbols(name);

    CREATE TABLE og_ae_map (
      name TEXT NOT NULL,
      og_rva INTEGER,
      og_rel_id INTEGER,
      ae_rva INTEGER,
      ae_rel_id INTEGER
    );
    CREATE INDEX idx_og_ae_map_name ON og_ae_map(name);
    CREATE INDEX idx_og_ae_map_og_rva ON og_ae_map(og_rva);
    -- No index on ae_rva. That column holds 1.11.191 addresses, not AE ones (see the note in
    -- tools/mapOgAe.ts), so nothing queries it and an index on it is pure write cost. The id
    -- columns are how the bridge is actually traversed, and must not be a table scan.

    -- idx_og_ae_map_name serves exact and prefix matches; the tool searches by infix substring,
    -- which no b-tree can serve. This trigram mirror is what makes that an index lookup instead
    -- of a 267k-row scan. External-content, so it stores the trigrams and not a second copy of
    -- the names; it is populated once, after og_ae_map is filled.
    CREATE VIRTUAL TABLE og_ae_map_fts USING fts5(
      name, content='og_ae_map', content_rowid='rowid', tokenize='trigram');
    CREATE INDEX idx_og_ae_map_og_rel_id ON og_ae_map(og_rel_id);
    CREATE INDEX idx_og_ae_map_ae_rel_id ON og_ae_map(ae_rel_id);

    CREATE TABLE name_disagreements (
      address TEXT NOT NULL,
      rva INTEGER,
      existing_name TEXT,
      decoded_name TEXT,
      note TEXT,
      verdict_155 TEXT
    );
    CREATE INDEX idx_name_disagreements_rva ON name_disagreements(rva);

    CREATE TABLE misname_corrections (
      address TEXT NOT NULL,
      rva INTEGER,
      old_name TEXT,
      new_name TEXT,
      method TEXT,
      note TEXT
    );
    CREATE INDEX idx_misname_corrections_rva ON misname_corrections(rva);
    CREATE INDEX idx_misname_corrections_names ON misname_corrections(old_name, new_name);

    CREATE TABLE misname_reverts (
      address TEXT NOT NULL,
      rva INTEGER,
      wrongly_renamed_to TEXT,
      revert_to TEXT
    );
    CREATE INDEX idx_misname_reverts_rva ON misname_reverts(rva);
    CREATE INDEX idx_misname_reverts_names ON misname_reverts(wrongly_renamed_to, revert_to);

    CREATE TABLE vtable_expected (
      class TEXT NOT NULL,
      slot INTEGER NOT NULL,
      expected_method TEXT
    );
    CREATE INDEX idx_vtable_expected_class ON vtable_expected(class);

    CREATE TABLE vtable_audit (
      class TEXT NOT NULL,
      slot INTEGER NOT NULL,
      expected_method TEXT,
      actual_name TEXT,
      verdict TEXT,
      address TEXT,
      rva INTEGER
    );
    CREATE INDEX idx_vtable_audit_class ON vtable_audit(class);

    CREATE TABLE subsystem_index (
      version  TEXT NOT NULL,
      category TEXT NOT NULL,
      rva      INTEGER NOT NULL,
      name     TEXT NOT NULL,
      PRIMARY KEY (version, category, rva)
    );
    CREATE INDEX idx_subsystem_index_category
      ON subsystem_index(version, category);
    CREATE INDEX idx_subsystem_index_rva
      ON subsystem_index(version, rva);

    -- 1.10.155 Microsoft PDB dump: NAME-KEYED EVIDENCE ONLY. There is deliberately NO index on
    -- pdb_rva — those addresses belong to a different game version and must never be used as
    -- 1.10.163 hook offsets. Isolation rule: nothing joins or looks up this table by address.
    CREATE TABLE pdb_symbols (
      name                TEXT NOT NULL,
      signature           TEXT NOT NULL,
      calling_convention  TEXT,
      sizeof              INTEGER,
      is_fpo              INTEGER NOT NULL DEFAULT 0,
      pdb_rva             INTEGER,
      version             TEXT NOT NULL DEFAULT '1.10.155'
    );
    CREATE INDEX idx_pdb_symbols_name ON pdb_symbols(name);

    -- All 24 Organized/*.csv, corpus-wide, one row per CSV row. The five files that also
    -- feed dedicated tables are intentionally duplicated here so search_organized (Task 5)
    -- sees the whole corpus in one place.
    CREATE TABLE organized_rows (
      source   TEXT NOT NULL,
      row_no   INTEGER NOT NULL,
      address  TEXT,
      rva      INTEGER,
      name_a   TEXT,
      name_b   TEXT,
      kind     TEXT,
      note     TEXT,
      extra    TEXT,
      PRIMARY KEY (source, row_no)
    );
    CREATE INDEX idx_organized_rows_rva    ON organized_rows(rva);
    CREATE INDEX idx_organized_rows_source ON organized_rows(source);
    CREATE INDEX idx_organized_rows_name_a ON organized_rows(name_a);
    CREATE INDEX idx_organized_rows_name_b ON organized_rows(name_b);

    CREATE TABLE organized_sources (
      source     TEXT PRIMARY KEY,
      columns    TEXT NOT NULL,
      row_count  INTEGER NOT NULL,
      shape      TEXT NOT NULL
    );
  `);
}

function indexSubsystems(db: Database.Database): number {
  const insert = db.prepare(
    "INSERT OR REPLACE INTO subsystem_index (version, category, rva, name) VALUES (?, ?, ?, ?)"
  );
  let total = 0;

  for (const { version } of SYMBOL_DUMPS) {
    const dir = join(ORGANIZED_DIR, version);
    if (!existsSync(dir)) {
      console.log(`  SKIP ${dir} (not found)`);
      continue;
    }
    for (const entry of readdirSync(dir)) {
      if (!entry.endsWith(".txt")) continue;
      const category = entry.slice(0, -".txt".length);
      const rows = parseCategoryFile(readFileSync(join(dir, entry), "utf8"));
      db.transaction(() => {
        for (const row of rows) insert.run(version, category, row.rva, row.name);
      })();
      total += rows.length;
      console.log(`  ${version}/${category}: ${rows.length.toLocaleString()} functions`);
    }
  }
  return total;
}

function indexBins(db: Database.Database): number {
  const insert = db.prepare("INSERT INTO address_library (version, id, rva) VALUES (?, ?, ?)");
  const insertMany = db.transaction((rows: [string, number, number][]) => {
    for (const row of rows) insert.run(...row);
  });

  let total = 0;
  for (const filename of BIN_FILES) {
    const version = versionFromBinFilename(filename);
    const buf = readFileSync(join(BINS_DIR, filename));
    const count = Number(buf.readBigUInt64LE(0));
    const rows: [string, number, number][] = [];
    for (let i = 0; i < count; i++) {
      const offset = 8 + i * 16;
      const id = Number(buf.readBigUInt64LE(offset));
      const rva = Number(buf.readBigUInt64LE(offset + 8));
      rows.push([version, id, rva]);
    }
    insertMany(rows);
    total += count;
    console.log(`  ${filename}: ${count.toLocaleString()} records`);
  }
  return total;
}

async function indexSymbolDumps(db: Database.Database): Promise<number> {
  const insert = db.prepare(
    "INSERT INTO symbols (version, rva, name, signature, sizeof) VALUES (?, ?, ?, ?, ?)"
  );
  const lineRe = /^\s*func \[0x([0-9A-Fa-f]+)(?: \| sizeof=(\d+))?\] (\S+) :: (.+)$/;

  let total = 0;
  for (const { version, path } of SYMBOL_DUMPS) {
    if (!existsSync(path)) {
      console.log(`  SKIP ${path} (not found)`);
      continue;
    }
    const insertBatch = db.transaction((rows: [string, number, string, string, number | null][]) => {
      for (const row of rows) insert.run(...row);
    });

    const rl = createInterface({ input: createReadStream(path, { encoding: "utf8" }) });
    let batch: [string, number, string, string, number | null][] = [];
    let count = 0;
    for await (const line of rl) {
      const match = line.match(lineRe);
      if (!match) continue;
      const rva = parseInt(match[1], 16);
      const sizeof = match[2] ? parseInt(match[2], 10) : null;
      batch.push([version, rva, match[3], match[4], sizeof]);
      count++;
      if (batch.length >= 5000) {
        insertBatch(batch);
        batch = [];
      }
    }
    if (batch.length > 0) insertBatch(batch);
    total += count;
    console.log(`  ${version}: ${count.toLocaleString()} symbols`);
  }
  return total;
}

export async function indexPdbSymbols(
  db: Database.Database,
  dumpPath: string = PDB_DUMP_PATH
): Promise<number> {
  if (!existsSync(dumpPath)) {
    console.log(`  SKIP ${dumpPath} (not found)`);
    return 0;
  }

  const insert = db.prepare(
    `INSERT INTO pdb_symbols (name, signature, calling_convention, sizeof, is_fpo, pdb_rva, version)
     VALUES (?, ?, ?, ?, ?, ?, '1.10.155')`
  );
  const insertBatch = db.transaction(
    (rows: [string, string, string | null, number, number, number][]) => {
      for (const row of rows) insert.run(...row);
    }
  );

  const rl = createInterface({ input: createReadStream(dumpPath, { encoding: "utf8" }) });
  let batch: [string, string, string | null, number, number, number][] = [];
  let count = 0;
  for await (const line of rl) {
    const parsed = parsePdbLine(line);
    if (!parsed) continue;
    const name = extractSignatureName(parsed.signature);
    if (!name) continue;
    batch.push([
      name,
      parsed.signature,
      extractCallingConvention(parsed.signature),
      parsed.sizeof,
      parsed.isFpo ? 1 : 0,
      parsed.pdbRva,
    ]);
    count++;
    if (batch.length >= 5000) {
      insertBatch(batch);
      batch = [];
    }
  }
  if (batch.length > 0) insertBatch(batch);
  return count;
}

export function indexOrganizedRowsGeneric(
  db: Database.Database,
  organizedDir: string = ORGANIZED_DIR
): number {
  const insertRow = db.prepare(
    `INSERT INTO organized_rows (source, row_no, address, rva, name_a, name_b, kind, note, extra)
     VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?)`
  );
  const insertSource = db.prepare(
    `INSERT OR REPLACE INTO organized_sources (source, columns, row_count, shape) VALUES (?, ?, ?, ?)`
  );

  let total = 0;
  for (const mapping of CSV_SOURCES) {
    const path = join(organizedDir, mapping.file);
    if (!existsSync(path)) {
      console.log(`  SKIP ${mapping.file} (not found)`);
      continue;
    }
    const source = sourceNameFor(mapping.file);
    const { columns, rows } = readOrganizedCsv(path);
    const parsed = parseOrganizedCsv(mapping, rows);

    db.transaction(() => {
      for (const row of parsed) {
        insertRow.run(
          source,
          row.rowNo,
          row.address,
          row.rva,
          row.nameA,
          row.nameB,
          row.kind,
          row.note,
          JSON.stringify(row.extra)
        );
      }
      insertSource.run(source, JSON.stringify(columns), parsed.length, columns.join(","));
    })();

    total += parsed.length;
    console.log(`  ${source}: ${parsed.length.toLocaleString()} rows`);
  }
  return total;
}

async function indexOgAeMap(db: Database.Database): Promise<number> {
  const insert = db.prepare(
    "INSERT INTO og_ae_map (name, og_rva, og_rel_id, ae_rva, ae_rel_id) VALUES (?, ?, ?, ?, ?)"
  );
  const insertBatch = db.transaction((rows: [string, number | null, number | null, number | null, number | null][]) => {
    for (const row of rows) insert.run(...row);
  });

  const rl = createInterface({ input: createReadStream(IDA_FUNCTIONS_OG_AE_CSV, { encoding: "utf8" }) });
  let batch: [string, number | null, number | null, number | null, number | null][] = [];
  let count = 0;
  let isHeader = true;
  for await (const rawLine of rl) {
    if (isHeader) {
      isHeader = false;
      continue;
    }
    if (!rawLine.trim()) continue;
    const parts = rawLine.split(";");
    if (parts.length < 5) continue;
    const [name, ogAddr, ogRelId, aeAddr, aeRelId] = parts;
    batch.push([
      name,
      rvaHexToRva(ogAddr),
      ogRelId ? parseInt(ogRelId, 10) : null,
      rvaHexToRva(aeAddr),
      aeRelId ? parseInt(aeRelId, 10) : null,
    ]);
    count++;
    if (batch.length >= 5000) {
      insertBatch(batch);
      batch = [];
    }
  }
  if (batch.length > 0) insertBatch(batch);
  // Built in one pass at the end rather than per-insert: an external-content FTS table carries no
  // triggers, and one rebuild is far cheaper than 267k incremental updates.
  db.exec("INSERT INTO og_ae_map_fts(rowid, name) SELECT rowid, name FROM og_ae_map");
  return count;
}

function readCsv(path: string): Record<string, string>[] {
  const content = readFileSync(path, "utf8").replace(/^﻿/, "");
  return parse(content, { columns: true, skip_empty_lines: true });
}

function indexOrganizedCsvs(db: Database.Database): Record<string, number> {
  const counts: Record<string, number> = {};

  const disagreements = readCsv(join(ORGANIZED_DIR, "crossref_163_disagreements_vs_155.csv"));
  const insertDisagreement = db.prepare(
    "INSERT INTO name_disagreements (address, rva, existing_name, decoded_name, note, verdict_155) VALUES (?, ?, ?, ?, ?, ?)"
  );
  db.transaction(() => {
    for (const row of disagreements) {
      insertDisagreement.run(
        row.address,
        vaHexToRva(row.address),
        row.existing_name ?? null,
        row.decoded_name ?? null,
        row.note ?? null,
        row["verdict_vs_1.10.155"] ?? null
      );
    }
  })();
  counts.name_disagreements = disagreements.length;

  const corrections = readCsv(join(ORGANIZED_DIR, "applied_misname_corrections.csv"));
  const insertCorrection = db.prepare(
    "INSERT INTO misname_corrections (address, rva, old_name, new_name, method, note) VALUES (?, ?, ?, ?, ?, ?)"
  );
  db.transaction(() => {
    for (const row of corrections) {
      insertCorrection.run(
        row.address,
        vaHexToRva(row.address),
        row.old_name ?? null,
        row.new_name ?? null,
        row.method ?? null,
        row.note ?? null
      );
    }
  })();
  counts.misname_corrections = corrections.length;

  const reverts = readCsv(join(ORGANIZED_DIR, "misnames_to_revert.csv"));
  const insertRevert = db.prepare(
    "INSERT INTO misname_reverts (address, rva, wrongly_renamed_to, revert_to) VALUES (?, ?, ?, ?)"
  );
  db.transaction(() => {
    for (const row of reverts) {
      insertRevert.run(
        row.address,
        vaHexToRva(row.address),
        row.wrongly_renamed_to ?? null,
        row.revert_to_commonlib_confirmed_name ?? null
      );
    }
  })();
  counts.misname_reverts = reverts.length;

  const vtableExpected = readCsv(join(ORGANIZED_DIR, "commonlib_vtable_slots.csv"));
  const insertVtableExpected = db.prepare(
    "INSERT INTO vtable_expected (class, slot, expected_method) VALUES (?, ?, ?)"
  );
  db.transaction(() => {
    for (const row of vtableExpected) {
      insertVtableExpected.run(row.class, parseInt(row.slot, 10), row.expected_method ?? null);
    }
  })();
  counts.vtable_expected = vtableExpected.length;

  const vtableAudit = readCsv(join(ORGANIZED_DIR, "vtable_audit_report.csv"));
  const insertVtableAudit = db.prepare(
    "INSERT INTO vtable_audit (class, slot, expected_method, actual_name, verdict, address, rva) VALUES (?, ?, ?, ?, ?, ?, ?)"
  );
  db.transaction(() => {
    for (const row of vtableAudit) {
      insertVtableAudit.run(
        row.class,
        parseInt(row.slot, 10),
        row.expected_method ?? null,
        row.actual_name ?? null,
        row.verdict ?? null,
        row.address ?? null,
        vaHexToRva(row.address)
      );
    }
  })();
  counts.vtable_audit = vtableAudit.length;

  return counts;
}

async function main() {
  if (existsSync(DB_PATH)) {
    unlinkSync(DB_PATH);
  }
  const db = new Database(DB_PATH);
  db.pragma("journal_mode = WAL");

  console.log("Creating schema...");
  createSchema(db);

  console.log("Indexing address_library (Bins/*.bin)...");
  const binCount = indexBins(db);
  console.log(`  total: ${binCount.toLocaleString()} records`);

  console.log("Indexing symbols (Symbols/*.txt)...");
  const symbolCount = await indexSymbolDumps(db);
  console.log(`  total: ${symbolCount.toLocaleString()} symbols`);

  console.log("Indexing og_ae_map (IDA_Functions_OG_AE.csv)...");
  const ogAeCount = await indexOgAeMap(db);
  console.log(`  total: ${ogAeCount.toLocaleString()} rows`);

  console.log("Indexing Organized/*.csv...");
  const organizedCounts = indexOrganizedCsvs(db);
  for (const [table, count] of Object.entries(organizedCounts)) {
    console.log(`  ${table}: ${count.toLocaleString()} rows`);
  }

  console.log("Indexing subsystem_index (Organized/<version>/*.txt)...");
  const subsystemCount = indexSubsystems(db);
  console.log(`  total: ${subsystemCount.toLocaleString()} rows`);

  console.log("Indexing pdb_symbols (Symbols/Fallout4_functions.txt)...");
  const pdbCount = await indexPdbSymbols(db);
  console.log(`  total: ${pdbCount.toLocaleString()} symbols`);

  console.log("Indexing organized_rows (Organized/*.csv, all 24 files)...");
  const organizedRowCount = indexOrganizedRowsGeneric(db);
  console.log(`  total: ${organizedRowCount.toLocaleString()} rows`);

  db.close();
  console.log(`\nDone. Index written to ${DB_PATH}`);
}

// Only run the build when executed directly, so tests can import the parsers.
const isDirectRun = process.argv[1] && import.meta.url === pathToFileURL(process.argv[1]).href;
if (isDirectRun) {
  main().catch((err) => {
    console.error("build-index failed:", err);
    process.exit(1);
  });
}
