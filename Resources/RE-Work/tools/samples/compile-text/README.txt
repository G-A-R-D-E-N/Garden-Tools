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

Committed cross-check fixture (no game files needed)
----------------------------------------------------

cross-check/ contains a self-contained set that reproduces the MATCH,
MISMATCH, and WARNING paths without the shipped Fallout4 STRINGS:

    python3 validate_ckini.py --compile-text \
        samples/compile-text/cross-check/input \
        --strings samples/compile-text/cross-check/STRINGS

  * input/Fallout4.esm/TextExport.txt      six records whose IDs were
                                           chosen to hit each path:
                                           0x12/0x13 -> .STRINGS (MATCH),
                                           0x10/0x36 -> .DLSTRINGS (MATCH),
                                           0x126C6 (Book) predicted
                                           .DLSTRINGS but present only in
                                           .ILSTRINGS (MISMATCH -- mirrors
                                           the shipped Enclave paint), and
                                           0x345678 absent from every
                                           compiled table (WARNING)
  * input/Fallout4.esm/000126c5.txt        supplemental file -> .ILSTRINGS
                                           (MATCH) regardless of its Book
                                           category
  * STRINGS/Fallout4_en.{STRINGS,
    DLSTRINGS,ILSTRINGS}                   tiny valid compiled files built
                                           by make_compiled.py from the
                                           localized-string-file-format.md
                                           byte spec (header, ascending ID
                                           table, NUL-terminated vs
                                           length-prefixed records)

The combined run also prints a per-table dialog-category composition per
language stem, attributing every compiled ID back to its routed record's
category (indices {1,6,7,10}):

    COMPOSITION fallout4_en: 7 routed IDs, per compiled table by
    routed-record category (dialog set {1,6,7,10} -> .DLSTRINGS)
      .STRINGS: 2 IDs, 2 attributed (ordinary-routed=2), 0 unattributed
      .DLSTRINGS: 2 IDs, 2 attributed (dialog-routed=2 (Quest Stage Log
      Entry=1, Description=1)), 0 unattributed
      .ILSTRINGS: 2 IDs, 2 attributed (dialog-routed=2 (Book=2)),
      0 unattributed

Expected: 5 MATCH, 1 MISMATCH, 1 WARNING -- the run exits 1 because the
MISMATCH is a hard failure. Deleting the 0x126C6 line from the input
TextExport.txt makes the run exit 0 with 4 MATCH + 1 WARNING. The compiled
binaries are reproducible: re-run cross-check/make_compiled.py after any
edit (the input .txt files remain the hand-edited source of truth).

Add --verbose to the same command to list all 18 category names with counts
per table (canonical order, zeros included) under each composition line;
without it only the dialog set {1,6,7,10} is broken out. The --json variant
always carries the full per-category counts in cross_check.composition.
