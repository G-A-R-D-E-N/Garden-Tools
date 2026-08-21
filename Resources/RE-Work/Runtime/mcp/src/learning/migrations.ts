// Ordered, append-only. Never edit a shipped migration's SQL; add a new one instead.
export const MIGRATIONS: { version: number; sql: string }[] = [
  {
    version: 1,
    sql: `
      CREATE TABLE learned_findings (
        id                    INTEGER PRIMARY KEY AUTOINCREMENT,
        version               TEXT NOT NULL,
        rva                   INTEGER NOT NULL,
        proposed_name         TEXT,
        confidence            TEXT NOT NULL CHECK (confidence IN ('high', 'medium', 'low')),
        note                  TEXT,
        source                TEXT NOT NULL DEFAULT 'headless+ollama',
        origin                TEXT NOT NULL CHECK (origin IN ('interactive', 'daemon')),
        raw_decompile_hash    TEXT NOT NULL,
        model                 TEXT NOT NULL,
        prompt_version        TEXT NOT NULL,
        extractor_version     TEXT NOT NULL,
        ghidra_version        TEXT,
        project_fingerprint   TEXT,
        supersedes_id         INTEGER REFERENCES learned_findings(id),
        is_current            INTEGER NOT NULL DEFAULT 1 CHECK (is_current IN (0, 1)),
        created_at            TEXT NOT NULL
      );

      CREATE UNIQUE INDEX idx_learned_findings_one_current
        ON learned_findings(version, rva)
        WHERE is_current = 1;

      CREATE INDEX idx_learned_findings_recent
        ON learned_findings(created_at DESC);

      CREATE INDEX idx_learned_findings_target_history
        ON learned_findings(version, rva, created_at DESC);

      CREATE TABLE discovered_repos (
        path                TEXT PRIMARY KEY,
        repo_fingerprint    TEXT,
        last_discovered_at  TEXT NOT NULL,
        last_scanned_at     TEXT,
        last_scan_error     TEXT
      );

      CREATE TABLE consumer_references (
        id                INTEGER PRIMARY KEY AUTOINCREMENT,
        repo_path         TEXT NOT NULL,
        file_path         TEXT NOT NULL,
        line_number       INTEGER NOT NULL,
        reference_kind    TEXT NOT NULL,
        raw_value         TEXT NOT NULL,
        resolved_version  TEXT,
        resolved_rva      INTEGER,
        scan_fingerprint  TEXT NOT NULL,
        last_seen_at      TEXT NOT NULL,
        UNIQUE(repo_path, file_path, line_number, reference_kind, raw_value)
      );

      CREATE INDEX idx_consumer_references_rva
        ON consumer_references(resolved_version, resolved_rva);

      CREATE INDEX idx_consumer_references_repo
        ON consumer_references(repo_path);
    `,
  },
  {
    version: 2,
    sql: `
      -- One REL::ID fills every runtime slot, so a reference has N resolutions, not one. The old
      -- resolved_version/resolved_rva pair could only hold the first, which is why an AE-authored
      -- id looked like a clean 1.10.163 hit: the OG library is 100% dense over its id range, so
      -- every lookup "succeeds" and returns an unrelated address. Columns are kept for readers
      -- that still select them; reference_slots is the truth.
      ALTER TABLE consumer_references ADD COLUMN form TEXT NOT NULL DEFAULT 'id_scalar';

      CREATE TABLE reference_slots (
        reference_id      INTEGER NOT NULL REFERENCES consumer_references(id) ON DELETE CASCADE,
        runtime           TEXT    NOT NULL CHECK (runtime IN ('kOG', 'kNG', 'kAE')),
        version           TEXT    NOT NULL,
        method            TEXT    NOT NULL CHECK (method IN ('id', 'offset')),
        raw_value         TEXT    NOT NULL,
        resolved_rva      INTEGER,
        unresolved_reason TEXT,
        PRIMARY KEY (reference_id, runtime)
      );

      CREATE INDEX idx_reference_slots_target
        ON reference_slots(version, resolved_rva);

      -- Which id space a file's literals were authored against. Measured, not assumed: a bare
      -- REL::ID(x) declares all three runtimes but was written against exactly one library, and
      -- nothing in the syntax records which. See learning/provenance.ts for the test.
      CREATE TABLE file_provenance (
        repo_path    TEXT    NOT NULL,
        file_path    TEXT    NOT NULL,
        low_id_count INTEGER NOT NULL,
        ae_hit_rate  REAL,
        baseline     REAL,
        verdict      TEXT    NOT NULL,
        computed_at  TEXT    NOT NULL,
        PRIMARY KEY (repo_path, file_path)
      );
    `,
  },
];
