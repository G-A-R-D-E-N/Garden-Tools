# CreationKit.exe — Batch / Command-Line Surface

The CK runs **headless batch operations** against a plugin (`<ESMFilename>`),
which is how Bethesda's build pipeline (and the precombined/previs export)
drives it. This doc is the quick reference for the full switch surface and its
grammar. It is reconciled against the corrected parser analysis in
[13-CLI-PARSER.md](13-CLI-PARSER.md) — the older string-recovered list (below)
covered only part of the real table.

> **Correction history.** The original list was recovered from the
> `Could not parse command line properly.  Should be -<Switch>:…` strings plus
> the `ICommandLineHandler` RTTI class. Doc 13 then recovered the **actual
> 31-entry pointer table** the parser walks (`.data 0x3b8c740`) and the
> dispatcher's bit→process map, so this table is authoritative: 31 switches
> with bit values, exact grammar, and process objects.

## Parser model (summary — full analysis in doc 13)

- **One switch per invocation.** `FUN_140487010` walks the token table and stops
  at the **first** prefix match (`while (!matched)`). Only one `-Switch:` is ever
  honored per command line, even if more are present.
- **Bit = `1 << idx`**, accumulated into the `DAT_143d2f34c` bitmask; the
  dispatcher `FUN_1404870f0` tests each bit and runs the mapped process.
- **Prefix match, case-insensitive** (`_strnicmp` against the full token, e.g.
  `-GeneratePreCombined`); the char right after the token must be `:` for the
  remainder to be captured.
- **Arg capture:** everything after `:` is copied into the switch's 260-byte
  slot (`DAT_143d2d3d0 + idx*0x104`), then tokenized on spaces (`strtok_s`).
- **Batch mode:** any match sets `DAT_143d3410b = 1` and
  `DAT_147fda42c = 0xffffffff`; the dispatcher runs during the main window's
  WM_CREATE and posts `WM_CLOSE` on completion.
- **Exit codes:** with `bThrowProcessCompleteExceptions:General=1`, every branch
  terminates with `0x6b650000` (success) / `0x6b650001` (failure) — full matrix
  in [19-BATCH-EXIT-CODES.md](19-BATCH-EXIT-CODES.md).
- **Linting:** `tools/validate_ckini.py --command '<command line>'` checks the
  31-switch table, one-switch rule, `strtok_s` space splitting, quoted
  `FUN_140490960` tokens, platform/MapInfo enums, and PreVis chunk grammar.

## Full switch list (31 switches, table order = bit priority)

Ten switches (indices 0–9, marked **★**) sit **ahead** of the previously known
21 — they were the "missing" first half of the table.

| idx | bit | Switch | Syntax (args after `-Switch:`) | Process object / action |
|---:|---:|---|---|---|
| 0 | `0x1` | ★ `-OptimizeMasterFile` | `<ESMFilename>` | `MasterfileOptimizer` |
| 1 | `0x2` | ★ `-ExportDialogue` | `<ESMFilename>` | `DialogueExporter` |
| 2 | `0x4` | ★ `-GenerateLips` | `<ESMFilename> <Language> <PathToTextExport>` | `LipGenerator` |
| 3 | `0x8` | ★ `-ExportFaceGenData` | `<ESMFilename> <XB1\|X64\|PS4\|W32>` (2nd token validated) | `FaceGenDataExporter` |
| 4 | `0x10` | ★ `-TagifyMasterfile` | `<ESMFilename>` | `MasterfileTagifier` |
| 5 | `0x20` | ★ `-ExportText` | `<ESMFilename>` (guarded by a file-open check) | `MasterfileTextExporter` |
| 6 | `0x40` | ★ `-CompileTextExport` | `<ESMFilename> <Language> <PathToTextExport> [PathToBackupTextExport]` | inline `FUN_140916980` batch; **raises exit code directly** |
| 7 | `0x80` | ★ `-DelocalizeMasterfile` | `<ESMFilename>` | `MasterfileDelocalizer` |
| 8 | `0x100` | ★ `-SaveDefaultPlugin` | `<PluginFilename> <MasterFilename>` (space-separated — see note) | `FileSaver` |
| 9 | `0x200` | ★ `-CheckInPlugin` | `<PluginFilename> <ESMFilename>` | `CheckItInChappy` (VCS check-in) |
| 10 | `0x400` | `-Install` | (no args) | AdminTasks elevation runner `FUN_14047cc40(1)` — **no process, no raise** |
| 11 | `0x800` | `-DumpNeededFiles` | `<ESMFilename> <OutputFilepath>` | `NeededFileDumper` |
| 12 | `0x1000` | `-OptimizeDLC` | `<ESMFilename>` | `DLCMasterfileOptimizer` |
| 13 | `0x2000` | `-GenerateSEQ` | `<ESMFilename>` | `SEQExporter` |
| 14 | `0x4000` | `-GenerateWarnings` | `<ESMFilename>` | `WarningGenerator` |
| 15 | `0x8000` | `-GenerateAnimInfo` | `<ESMFilename> <DataFilepath> <OutputFilepath> [--speed] [--stance] [--forceAll]` | `AnimationBuildProcessDataExport` |
| 16 | `0x10000` | `-GenerateSingleLip` | `"WAVFilePath" "Text For WAV File."` (quoted tokens) | inline LIP gen `FUN_1406495e0`; output = wav path + `.lip`; **no raise** |
| 17 | `0x20000` | `-GenerateStaticCollections` | `<ESMFilename> <XB1\|X64\|PS4\|W32>` (2nd token validated) | `StaticCollectionProcess` |
| 18 | `0x40000` | `-GeneratePreCombined` | `<ESMFilename> [clean,filtered] [all,main,ints,other] [currentset(1-totalsetcount)] [totalsetcount]` | `PreVisPreCombineDataProcess` (doc 09) |
| 19 | `0x80000` | `-GeneratePreVisData` | same grammar as above | `PreVisPreCombineDataProcess` (doc 09) |
| 20 | `0x100000` | `-DepersistRefs` | `<ESMFilename>` | `DepersistRefsProcess` |
| 21 | `0x200000` | `-MapMaker` | `<ESMFilename> <Worldspace\|Interior>` | `MapMakerProcess` |
| 22 | `0x400000` | `-MapInfo` | `<ESMFilename> <Worldspace\|Interior> <doors\|markers\|interiors>` (3rd token validated) | inline map-info writer |
| 23 | `0x800000` | `-OutputAreaArt` | `<ESMFilename> <AreasFilename> <OutputFilename>` | `AreaArtOutput` |
| 24 | `0x1000000` | `-UpdateModelData` | `<ESMFilename>` | `ModelDataUpdater` |
| 25 | `0x2000000` | `-ExportDismemberData` | `<ESMFilename> <XB1\|X64\|PS4\|W32>` (2nd token validated) | `DismemberDataExporter` |
| 26 | `0x4000000` | `-ConvertMaterials` | (no args) | `MaterialConverter`; **raises exit code directly** |
| 27 | `0x8000000` | `-ImportScalingData` | `<ESMFilename>` | `ImportScalingDataProcess` |
| 28 | `0x10000000` | `-CompressPSG` | `<Filename>`; builds `data\<name> - Geometry.psg` / `... .raw` | `CompressPSGProcess`; **raises exit code directly** |
| 29 | `0x20000000` | `-BuildCDX` | `<ESMFilename>`; writes `<name>.cdx`, extracts to `data\cdxtmp` | `BuildCDX` |
| 30 | `0x40000000` | `-ConvertToESL` | `<ESMFilename>` | `ESLConverter` |

## Parameter semantics (corrected)

- **Platform selector** `<XB1|X64|PS4|W32>` appears on the export/generation
  switches — console-era export targets (W32 = Windows 32-bit; X64 = current gen).
  The token is validated by `FUN_1404905a0` on `-ExportFaceGenData`,
  `-ExportDismemberData`, and `-GenerateStaticCollections`: `XB1`/`X64` → 0,
  `W32` → 1, `PS4` → 2 (written to `DAT_147322d04`), which the FaceGen string
  encoders turn into per-platform binary format tables. Quoted arg tokens (paths
  with spaces) use the `FUN_140490960` splitter — full helper analysis in
  [13-CLI-PARSER.md](13-CLI-PARSER.md) §Arg helpers.
- **PreCombined/PreVis args:** `[clean|filtered]` selects the mode flag and
  `[all|main|ints|other]` selects scope. Both are optional; omitted values
  default to filtered, all, current=1, total=1. When `all`, `main`, or `ints`
  is explicitly supplied, the common parser's missing-pair fallback is
  current=0, total=1. `all`, `main`, and `ints`
  enter the two-token chunk parser only when both `currentset` and
  `totalsetcount` are present; `other` skips that parser. The dispatcher uses
  `_stricmp`, `atoi`-to-uint checks (`0` and `total < current` fail), and
  ignores trailing words. These parser quirks are distinct from downstream
  partial/filtered support; full evidence is in doc 09.
- **`-SaveDefaultPlugin`** — the usage string prints
  `<PluginFilename><MasterFilename>` with **no space**, but the arg slot is
  tokenized on spaces, so the real grammar is
  `-SaveDefaultPlugin:<PluginFilename> <MasterFilename>`. The first token goes
  into the `FileSaver` object; the second is loaded as the masterfile by the
  shared runner (doc 15).
- **`-GenerateAnimInfo`** — the `[%s] [%s] [%s]` placeholders in the usage
  string are the option tokens **`--speed`**, **`--stance`**, **`--forceAll`**,
  collected as named string options (validated by `FUN_140071152`).
- **`-MapInfo`** — the third token is validated against the 3-entry enum table
  at `.data 0x2fdb3e0`: `0="doors"`, `1="markers"`, `2="interiors"` (case-
  insensitive `_stricmp`).
- **`-GenerateSingleLip`** — the only switch using **quoted** tokens (via the
  `FUN_140490960` quoted-splitter, not `strtok_s`): `"WAVFilePath"` plus the
  dialog text. The output `.lip` file is derived by replacing the wav's
  extension.
- **`-ExportDialogue`** — the only argument is the ESM filename. Its
  `DialogueExporter::Execute` passes the raw slot directly to the file lookup;
  language and text-export-root tokens are not part of this branch. It writes
  human-readable reports below `Logs/DialogueExport`, using the
  `dialogueExport%s%s.txt` / `dialogueExport%s%s%s.txt` templates.
- **`-CompileTextExport`** — the only switch whose dispatcher branch runs a
  standalone batch function (`FUN_140916980`) instead of a process object; like
  `-ConvertMaterials` and `-CompressPSG`, it raises the exit code **directly**
  in the dispatcher (doc 19).

## `-ExportDialogue` — `DialogueExporter::Execute` contract

The `0x2` branch installs `_anon_F29367BE::DialogueExporter::vftable`
(`0x142fb6430`) and runs `Execute` at `0x14048e3e0` through the shared
runner. Unlike `-CompileTextExport`, it does **not** tokenize the argument
slot in the dispatcher or pass language/text-export arguments to a compiler.
`Execute` passes `this+0x08` unchanged to the file-registry lookup
(`0x14005d7ce` → `0x14085cd50`), which compares the complete string with the
ESM filename. The actual batch grammar is therefore:

```text
-ExportDialogue:<ESMFilename>
```

A command such as `-ExportDialogue:Fallout4.esm en ExportRoot` leaves the
three words in one raw slot and fails the ESM lookup. The earlier three-token
entry was a mistaken carry-over from `-CompileTextExport`/`-GenerateLips`.

### What it writes

The worker thunk `FUN_140090741` targets `0x1404acd00`. It logs either
`Exporting dialogue for all quests` (the batch call supplies a null optional
quest/context) or a quest-specific progress line, then creates/uses the
relative output location:

```text
Logs/DialogueExport/DialogueExport

Logs/DialogueExport/dialogueExport%s%s.txt
Logs/DialogueExport/dialogueExport%s%s%s.txt
```

Its filename templates are the embedded literals:

```text
dialogueExport%s%s.txt
dialogueExport%s%s%s.txt
```

The substitutions are runtime quest/voice/context names returned through
virtual slot `+0x238`, with an internal `_` separator; they are not a
language token. The worker walks quests/topics/info records and writes a
human-readable `.txt` dialogue report, with diagnostics for missing/shared
infos, invalid `GetIsID` conditions, voice compatibility, and file I/O.
The binary also contains `Files included in export:`, `Exporting topics...Done!`,
`Extracting dialogue for topic '%s'`, and `Error writing to file` diagnostics.

This branch does **not** open `TextExport.txt`, does not consume a
`PathToTextExport`, and does not produce `.STRINGS`, `.DLSTRINGS`, or
`.ILSTRINGS`. Those are the `FUN_140916980` outputs documented below.
The exact per-export `%s` values are data-dependent; the binary proves the
directory and templates, not one fixed filename for every quest/voice set.
See [ghidra-dump/CLI-DIALOGUE-EXPORTER-FUN_14048e3e0.txt](ghidra-dump/CLI-DIALOGUE-EXPORTER-FUN_14048e3e0.txt).

## `-CompileTextExport` — `FUN_140916980` writer contract

The dispatcher calls `FUN_140916980(esm, language, textExportPath,
backupTextExportPath)` directly for bit `0x40`; this is not a process-object
`Execute()` call. The function returns a boolean-like result (`AL = 1` only
when its internal failure flag is clear), and the dispatcher converts it to
`0x6b650000`/`0x6b650001` when `bThrowProcessCompleteExceptions:General` is
enabled (doc 19).

### Language-specific output files

The language argument is passed through as a path component; this branch does
not call the platform-token validator and does not normalize the spelling. The
three output contexts are constructed at `FUN_14091aa30` with these literal
extensions:

| context | final path written by the context | temporary path staged by the context |
|---|---|---|
| ordinary strings | `Data\\STRINGS\\<ESM stem>_<Language>.STRINGS` | final path + `_temp` |
| dialog strings | `Data\\STRINGS\\<ESM stem>_<Language>.DLSTRINGS` | final path + `_temp` |
| miscellaneous strings | `Data\\STRINGS\\<ESM stem>_<Language>.ILSTRINGS` | final path + `_temp` |

`<ESM stem>` is the input ESM name with the suffix beginning at the last
`.` removed (`Fallout4.esm` → `Fallout4`); the helper uses `strrchr`, so a
multi-dot name is cut at its final dot. The directory and extensions are not
synthesized: the constructor receives the embedded literals `STRINGS`,
`.STRINGS`, `.DLSTRINGS`, and `.ILSTRINGS`; the path format is
`Data/%s/%s_%s%s`. Each context also creates a `<path>_temp` stream and is
finalized by `FUN_14091d790`. The caller checks the context open/write status
before input processing; it does not branch on a return value after the three
finalizer calls, so a late `+0x14d` finalizer status is not an independently
proven additional return-failure condition. See the durable decompile for the
exact status distinction.

The resulting files are Bethesda localized-string binary files, not copies of
the text export. The compiler fills the three tables while processing the
text records: ordinary string IDs go to `.STRINGS`, dialog/voice-localized
records to `.DLSTRINGS`, and the remaining localized records to `.ILSTRINGS`.
The exact low-level format is documented in
[`ghidra-dump/localized-string-file-format.md`](ghidra-dump/localized-string-file-format.md):
`u32 numStrings`, `u32 dataSize`, `(stringID, offset)` pairs (8 bytes each,
sorted by ID), then a data block of NUL-terminated records (`.STRINGS`) or
`u32 len + text + NUL` records (`.DLSTRINGS`/`.ILSTRINGS`), verified on 33
shipped FO4 files; this function is the producer/orchestrator, not a
plain-text exporter.

### Input roots and text layout

The third argument is an export root. For each input root selected by the
function, the observed path construction is `%s/%s/%s`, with the root,
`<ESMFilename>`, and the embedded `TextExport.txt` literal. In other words,
the primary input is conventionally:

```text
<PathToTextExport>/<ESMFilename>/TextExport.txt
```

The optional fourth argument is a second text-export root (`PathToBackupTextExport`),
not an output filename. The function appends a non-empty backup root **first**
and then appends the primary root; the observed traversal order is therefore
`[backup,] primary`. This is an input precedence/order detail, not proof that
all duplicate records are merged as a semantic fallback. For each root the
main file is `<root>/<ESMFilename>/TextExport.txt`; additional ID-named files
are scanned when present. A supplemental filename must contain an 8-character
hexadecimal ID before its extension; malformed names are logged and skipped.

The parser reports malformed lines, invalid IDs, reserved IDs, duplicate IDs,
unreadable files, and oversized string files through the embedded `Text Compile:`
diagnostics. Reserved IDs, duplicates, invalid characters, and unknown categories
have recovery/skip paths; they should not be treated as identical to the hard
output-open or mandatory-input failures. The exact four-field grammar, first-three-tab
delimiters, hexadecimal StringID rules, and all 18 category tokens are recorded in
[`ghidra-dump/decompile-CompileTextExport-line-parser-FUN_14091f3d0.md`](ghidra-dump/decompile-CompileTextExport-line-parser-FUN_14091f3d0.md).
The complete caller reconstruction and failure map is in
[`ghidra-dump/decompile-CompileTextExport-FUN_140916980.md`](ghidra-dump/decompile-CompileTextExport-FUN_140916980.md).

### Control flow and failure points

1. Optionally run the `bValidateStringsOnCompile:General` character/resource
   preflight (the clear-setting path); the set path jumps directly to output
   construction.
2. Build the three output contexts for the ESM stem and language. Each context
   tracks status bytes at `+0x14c/+0x14d`; an output-open/resource failure sets
   the function's failure path and emits `Could not open all output files`.
3. Build the input-root list in `[backup,] primary` order and open the mandatory
   `TextExport.txt` for each root. A mandatory input open/read failure emits
   `Could not open input file, aborting process` and fails the call; a supplemental
   file can instead emit `Could not open file for read, skipping`.
4. Walk the export records/files, parse IDs and string payloads, and route each
   record to the appropriate output context. Character replacement and duplicate
   or reserved-ID messages are recovery/skip diagnostics.
5. Write the localized-string tables through temporary streams, call
   `FUN_14091d790` for all three contexts, release resources, and return the
   inverse of the function failure byte.

Useful byte-level anchors are `FUN_14091aa30` (one output context),
`FUN_14091d240` (record insertion/character normalization),
`FUN_14091f3d0` (line/buffer parser), `FUN_14091d790` (context finalization),
and the three constructor calls in `FUN_140916980` at `0x140916c09`,
`0x140916c26`, and `0x140916c43`. The dispatcher still maps true/false to
`0x6b650000`/`0x6b650001` only when the exception gate is enabled.

## Related runtime bits

- The switches are dispatched to `_anon_F29367BE::<ProcessName>` objects whose
  **vtable slot 1 = `Execute()`** is called by the shared runner
  `FUN_1404906a0` (the earlier `ICommandLineHandler` hypothesis was replaced by
  the bitmask+dispatcher model in doc 13).
- Admin/batch machinery lives in `code\construction set\misc\bgsautomatedprocesses.cpp`
  and `bgsadmintasks.h`.
- The console command `HotLoadPlugin` (and its description string about closing
  the masterfile — "Close masterfile & plugins. Useful for letting CreationKit
  save to a plugin that is also loaded in-game…") is the editor-side counterpart.
- Batch mode also exposes `Could not parse command line option properly: %s`
  for malformed single options (used by `-GenerateAnimInfo`'s option parsing).

## Resolved next steps

The original open items are all closed:

1. ~~Find the `ICommandLineHandler` vtable array~~ → the real token table is
   `.data 0x3b8c740` (31 × 8-byte pointers), parsed by `FUN_140487010` —
   [13-CLI-PARSER.md](13-CLI-PARSER.md).
2. ~~`-GeneratePreCombined` handler~~ → `PreVisPreCombineDataProcess` `Execute`
   `0x48f920` — [09-PRECOMBINED-PREVIS.md](09-PRECOMBINED-PREVIS.md).
3. ~~`-SaveDefaultPlugin` handler~~ → `FileSaver` in the masterfile pipeline —
   [15-BATCH-PROCESSES.md](15-BATCH-PROCESSES.md).
4. ~~`-MapMaker` handler~~ → `MapMakerProcess` (`cell << 12` = 4096-unit world
   cells) — [15-BATCH-PROCESSES.md](15-BATCH-PROCESSES.md).
