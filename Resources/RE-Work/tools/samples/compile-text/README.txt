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

Output-table routing (FUN_140916980 ``0x4c2`` dialog bitmask): every record
with a usable ID also gets a ROUTE line predicting the Data\STRINGS output
file it would land in.

  * TextExport.txt records route by category -- indices {1,6,7,10} (Quest
    Stage Log Entry, Book, Description, Message) go to .DLSTRINGS; every
    other category (and the unknown-category Misc fallback) goes to .STRINGS
  * supplemental ID-named files feed .ILSTRINGS regardless of category
    (that is why primary/00010004.txt, a Book-category record, routes to
    .ILSTRINGS, not .DLSTRINGS)

Expected routing on this fixture set:

    .DLSTRINGS=1 (0x00010002 Quest Stage Log Entry)
    .ILSTRINGS=2 (backup+primary supplemental files)
    .STRINGS=5   (Misc, Topic Text, duplicate Misc, Loading Screen Text,
                  unknown-category fallback)

The output filename uses the <Language> argument when --command is given
(ENGLISH here) and DEFAULT_SLANGUAGE otherwise.

Cross-check against real compiled output
----------------------------------------

Combine --compile-text with --strings to verify the predictions against
real compiled .STRINGS/.DLSTRINGS/.ILSTRINGS files (e.g. the shipped
Fallout4 STRINGS extracted from Fallout4 - Misc.ba2):

    python3 validate_ckini.py --compile-text \
        samples/compile-text/backup samples/compile-text/primary \
        --strings '.../TEMP_BSA_FILES/*/STRINGS'

For every routed record the ID must be present in its predicted table
(MATCH); an ID found in a *different* table is a MISMATCH (hard failure),
and an ID absent from all matching compiled files is a WARNING (the
compiled set may predate the export). Files are matched to the compile by
<ESM stem>_ prefix, so shipped language-code names (Fallout4_en.*) match a
Fallout4.esm compile regardless of the CLI language spelling. The shipped
fixtures' synthetic IDs (0x1000A etc.) are not in the game files, so a
cross-check run reports them as not-found warnings rather than mismatches.
