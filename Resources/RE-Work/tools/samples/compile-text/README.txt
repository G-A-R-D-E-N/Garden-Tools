CompileTextExport input fixtures
================================

Mirrors the FUN_140916980 input contract documented in
ghidra-dump/decompile-CompileTextExport-FUN_140916980.md and doc 05:

  * the mandatory input is <root>/<ESMFilename>/TextExport.txt
  * supplemental files must have an 8-character hexadecimal stem before the
    final extension; malformed names are logged and skipped
  * a non-empty backup root is appended FIRST and traversed before the primary
    root, so the backup's records are inserted first
  * identical string IDs across the merged root set are duplicate-ID
    diagnostics on the aggregate string table (skip/recovery, not a hard
    output failure)

Use with:

    python3 validate_ckini.py --compile-text \
        --command '-CompileTextExport:Fallout4.esm ENGLISH backup primary'

Exercise coverage:

  backup/Fallout4.esm/TextExport.txt    mandatory backup file; record
                                        0x0001000A duplicates the primary
  backup/Fallout4.esm/0001000A.txt      supplemental backup file (duplicate)
  primary/Fallout4.esm/TextExport.txt   mandatory primary file; 0x0001000A
                                        collides with the backup, 0x0000000F
                                        is a reserved ID, one record has an
                                        unknown category, one line is
                                        malformed (no tabs / no ID)
  primary/Fallout4.esm/00010004.txt     supplemental primary file
  primary/Fallout4.esm/ZZnothex.txt     malformed filename -> skipped

Expected: VERDICT PASS (all diagnostics are skip/recovery); the duplicate,
reserved-ID, unknown-category, and malformed-line notes are warnings.
