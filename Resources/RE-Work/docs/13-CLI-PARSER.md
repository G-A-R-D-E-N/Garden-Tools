# 13 — CLI Token→Bitmask Parser (`FUN_140487010`) and the Dispatcher (`FUN_1404870f0`)

**Binary:** CreationKit.exe 1.11.137.0 · **All RVAs** relative to image base `0x140000000`.

This doc closes the loop on the batch-mode command line: *where the switch strings
become bits*. Doc 09 documented the PreCombined/PreVis handler that consumes bits
`0x40000`/`0x80000`; this doc covers the parser that produces them, plus the full
31-switch surface it exposes. The quick-reference surface (full switch table,
arg grammars, one-switch-per-invocation rule) is reconciled in
[05-BATCH-CLI.md](05-BATCH-CLI.md).

## The missing first 10 switches

Doc 09 listed 21 switches recovered from the pointer table at `.data 0x3b8c7d0`.
That was only the **tail** of the real table. The parser walks a **31-entry** table
starting at `.data 0x3b8c740`, so ten more switches exist ahead of the previously
known ones:

| idx | bit      | token |
|----:|---------:|-------|
|  0  | `0x1`    | `-OptimizeMasterFile` |
|  1  | `0x2`    | `-ExportDialogue` |
|  2  | `0x4`    | `-GenerateLips` |
|  3  | `0x8`    | `-ExportFaceGenData` |
|  4  | `0x10`   | `-TagifyMasterfile` |
|  5  | `0x20`   | `-ExportText` |
|  6  | `0x40`   | `-CompileTextExport` |
|  7  | `0x80`   | `-DelocalizeMasterfile` |
|  8  | `0x100`  | `-SaveDefaultPlugin` |
|  9  | `0x200`  | `-CheckInPlugin` |
| 10  | `0x400`  | `-Install` |
| 11  | `0x800`  | `-DumpNeededFiles` |
| 12  | `0x1000` | `-OptimizeDLC` |
| 13  | `0x2000` | `-GenerateSEQ` |
| 14  | `0x4000` | `-GenerateWarnings` |
| 15  | `0x8000` | `-GenerateAnimInfo` |
| 16  | `0x10000`| `-GenerateSingleLip` |
| 17  | `0x20000`| `-GenerateStaticCollections` |
| 18  | `0x40000`| `-GeneratePreCombined` |
| 19  | `0x80000`| `-GeneratePreVisData` |
| 20  | `0x100000`| `-DepersistRefs` |
| 21  | `0x200000`| `-MapMaker` |
| 22  | `0x400000`| `-MapInfo` |
| 23  | `0x800000`| `-OutputAreaArt` |
| 24  | `0x1000000`| `-UpdateModelData` |
| 25  | `0x2000000`| `-ExportDismemberData` |
| 26  | `0x4000000`| `-ConvertMaterials` |
| 27  | `0x8000000`| `-ImportScalingData` |
| 28  | `0x10000000`| `-CompressPSG` |
| 29  | `0x20000000`| `-BuildCDX` |
| 30  | `0x40000000`| `-ConvertToESL` |

Table layout: `.data RVA 0x3b8c740`, **31 × 8-byte** pointers into `.rdata`
(each points at a NUL-terminated token string). The parser's loop bound is
`if (0x1e < iVar4) break`, i.e. indices 0..30 — exactly 31 entries. The qwords
immediately after `0x3b8c838` are not tokens (they're unrelated data), so the
table ends cleanly at entry 30.

Bit numbering: bit = `1 << idx` (`uVar5` starts at 1 and rotates left with carry
per iteration: `uVar5 = uVar5 << 1 | (uint)((int)uVar5 < 0)`). This matches doc 09:
`-GeneratePreCombined` = `0x40000`, `-GeneratePreVisData` = `0x80000`.

## The parser (`FUN_140487010` @ RVA `0x487010`)

```c
bool FUN_140487010(char *param_1)          // param_1 = full command line (lpCmdLine)
{
  bool   matched = false;
  char **ppuVar6 = &PTR_s__OptimizeMasterFile_143b8c740;   // token table base
  int    idx = 0;
  uint   bit = 1;
  do {
    if (0x1e < idx) break;                 // 31 entries max
    size_t len = strlen(*ppuVar6);         // token length
    int cmp = _strnicmp(param_1, *ppuVar6, len);   // case-INSENSITIVE prefix compare
    if (cmp == 0) {                        // command line starts with this token
      DAT_143d2f34c |= bit;                // set the bit (OR, never cleared here)
      matched = true;
      if (param_1[len] == ':') {           // -Switch:<args>
        strcpy_s(&DAT_143d2d3d0 + (longlong)idx * 0x104, 0x104,
                 param_1 + len + 1);       // copy everything after ':' into arg slot
      }
    }
    idx++;
    bit = bit << 1 | (uint)((int)bit < 0); // rotate left (RCL-style)
    ppuVar6++;
  } while (!matched);                      // STOP AT FIRST MATCH — at most one switch
  if (DAT_143d2f34c != 0) {
    DAT_143d3410b = 1;                     // batch-mode engaged
    DAT_147fda42c = 0xffffffff;
  }
  return DAT_143d2f34c != 0;
}
```

Semantics, precisely:

- **One switch per invocation.** The loop exits on the first matching token
  (`while (!matched)`). Only the highest-priority (lowest-index) match survives.
- **Prefix match, case-insensitive.** `_strnicmp(cmdline, token, strlen(token))`
  means `-GeneratePreCombined:...` matches token `-GeneratePreCombined`; it also
  means `-GeneratePreCombinedFOO` would match — token matching is a prefix test,
  not a whole-word test.
- **Argument capture.** If the char immediately after the matched token is `:`,
  the remainder of the command line (`cmdline + len + 1`) is copied into the
  per-slot arg buffer `DAT_143d2d3d0 + idx*0x104` (0x104 = 260-byte slots, 31 slots).
- **Batch flag.** Any nonzero bitmask sets `DAT_143d3410b = 1` (batch mode) and
  `DAT_147fda42c = 0xffffffff`.
- Return value = "some switch matched".

### Caller: WinMain (`FUN_1406946a0`, call site RVA `0x69485e`)

The parser is invoked **once**, from WinMain, with WinMain's third parameter
(`lpCmdLine`):

```
GetCommandLineA()                          // full command line
  -> exe-path extraction into DAT_143d34140
  -> SetCurrentDirectoryA(exe dir)         // run from the exe's folder
  -> first-token copy into local_8a8
  -> if (local_8a8[0] && !FUN_140487010(lpCmdLine) && first-token has ext)
       -> if extension == ".NIF": NIF preview mode
          (DAT_143d33b60 = token, DAT_143d3410a |= 1)
```

So: if the parser matches a switch, the editor boots in batch mode. If it does
**not** match and the first token ends in `.NIF`, the editor instead launches in
**NIF-preview mode** (load the model file directly, no window loop). Any other
first token → normal interactive editor.

## The dispatcher (`FUN_1404870f0` @ RVA `0x4870f0`)

Called from the main window's WM_CREATE path in the window proc
(`FUN_140696e00`). It sets a running flag (`DAT_143d2f350 = 1`), tests each bit,
runs the matching process, then clears the flag and returns
`DAT_143d2f34c != 0` — the caller posts `WM_CLOSE` (`SendMessageA(param_1, 0x10, 0, 0)`)
so a successful batch run exits the editor immediately.

Complete bit → process map (process objects are `_anon_F29367BE::*` instances
passed to the shared process runner `FUN_1404906a0`, which calls vftable slot 1 =
`Execute()`):

| bit      | switch                 | process object / action | arg grammar (strtok_s on slot, `" "`) |
|---------:|------------------------|-------------------------|----------------------------------------|
| `0x1`    | `-OptimizeMasterFile`  | `MasterfileOptimizer`   | `<esm>` |
| `0x2`    | `-ExportDialogue`      | `DialogueExporter`      | `<esm>` (raw slot; no tokenization) |
| `0x4`    | `-GenerateLips`        | `LipGenerator`          | `<esm> <lang> <text-export>` |
| `0x8`    | `-ExportFaceGenData`   | `FaceGenDataExporter`   | `<esm> <lang> <path>`; 2nd token validated by `FUN_1404905a0` |
| `0x10`   | `-TagifyMasterfile`    | `MasterfileTagifier`    | `<esm>` |
| `0x20`   | `-ExportText`          | `MasterfileTextExporter`| `<esm>` (guarded by `FUN_1409166b0`) |
| `0x40`   | `-CompileTextExport`   | `FUN_140916980` batch   | `<esm> <lang> <PathToTextExport> [PathToBackupTextExport]` |
| `0x80`   | `-DelocalizeMasterfile`| `MasterfileDelocalizer` | `<esm>` |
| `0x100`  | `-SaveDefaultPlugin`   | `FileSaver`             | `<esm> <dest>` |
| `0x200`  | `-CheckInPlugin`       | `CheckItInChappy`       | `<plugin> <esm>`; does backup to `Backup\` + network copy |
| `0x400`  | `-Install`             | `FUN_14047cc40(1)`      | (AdminTasks elevation runner) |
| `0x800`  | `-DumpNeededFiles`     | `NeededFileDumper`      | `<esm> <output-filepath>` |
| `0x1000` | `-OptimizeDLC`         | `DLCMasterfileOptimizer`| `<esm>` |
| `0x2000` | `-GenerateSEQ`         | `SEQExporter`           | `<esm>` |
| `0x4000` | `-GenerateWarnings`    | `WarningGenerator`      | `<esm>` |
| `0x8000` | `-GenerateAnimInfo`    | `AnimationBuildProcessDataExport` | `<esm> <anim-file>...`; extra tokens parsed as `--speed`, `--stance`, `--forceAll` |
| `0x10000`| `-GenerateSingleLip`   | inline LIP gen (`FUN_1406495e0`) | `"WAVFilePath" "Text For WAV File."` (quoted tokens via `FUN_140490960`); output = wav path with `.lip` extension |
| `0x20000`| `-GenerateStaticCollections` | `StaticCollectionProcess` | `<esm> <XB1|X64|PS4|W32>` (2nd token validated by `FUN_1404905a0`) |
| `0x40000`| `-GeneratePreCombined` | `PreVisPreCombineDataProcess` (shared) | `<esm> [clean|filtered] [all|main|ints|other] [currentset totalsetcount]` — see doc 09 |
| `0x80000`| `-GeneratePreVisData`  | `PreVisPreCombineDataProcess` (shared) | same grammar and parser quirks as above |
| `0x100000`| `-DepersistRefs`      | `DepersistRefsProcess`   | `<esm>` |
| `0x200000`| `-MapMaker`           | `MapMakerProcess`        | `<esm> <Worldspace\|Interior>` |
| `0x400000`| `-MapInfo`            | inline map-info writer   | `<esm> <worldspace> <doors\|markers\|interiors>` (3rd token validated against the 3-entry enum table at `.data 0x2fdb3e0`: doors=0, markers=1, interiors=2) |
| `0x800000`| `-OutputAreaArt`      | `AreaArtOutput`          | `<esm> <worldspace> <outputdir>` |
| `0x1000000`| `-UpdateModelData`   | `ModelDataUpdater`       | `<esm>` |
| `0x2000000`| `-ExportDismemberData`| `DismemberDataExporter`  | `<esm> <lang> <path>` (2nd token validated) |
| `0x4000000`| `-ConvertMaterials`  | `MaterialConverter`      | (no args; raises `0x6b650001` on failure) |
| `0x8000000`| `-ImportScalingData` | `ImportScalingDataProcess` | `<esm>` |
| `0x10000000`| `-CompressPSG`      | `CompressPSGProcess`     | `<file>`; builds `data\<name> - Geometry.psg` / `... - Geometry.raw` paths; raises `0x6b650001` on failure |
| `0x20000000`| `-BuildCDX`         | `BuildCDX`               | `<esm>`; writes `<name>.cdx`, extracts to `data\cdxtmp` |
| `0x40000000`| `-ConvertToESL`     | `ESLConverter`           | `<esm>` |

Notes:

- `-ExportDialogue` is intentionally absent from the arg-helper list: its
  `DialogueExporter::Execute` (`0x14048e3e0`) passes the complete slot string
  to `FUN_14005d7ce`/`0x14085cd50`, which performs an exact ESM-file lookup.
  It therefore accepts only `<ESMFilename>`, not `<Language> <PathToTextExport>`.
  The worker `FUN_140090741` → `0x1404acd00` writes human-readable reports
  below `Logs/DialogueExport` using `dialogueExport%s%s.txt` or
  `dialogueExport%s%s%s.txt`; these substitutions are runtime quest/voice
  context names. See the durable reconstruction in
  `ghidra-dump/CLI-DIALOGUE-EXPORTER-FUN_14048e3e0.txt`.
- `FUN_1404905a0` appears in the `FaceGenDataExporter`, `DismemberDataExporter`,
  and `StaticCollectionProcess` branches as a validator for the second token
  (likely a language/platform allow-list check).
- `FUN_140490960` is the quoted-token splitter used by `-GenerateSingleLip`
  (and `-CompileTextExport`) — it handles `"..."` quoting that `strtok_s` can't.
- The `0x40000`/`0x80000` pair shares one handler; the handler itself
  distinguishes PreCombined vs PreVis internally (doc 09, `Execute` = `0x48f920`).
- The PreCombined/PreVis parser accepts ESM-only, ESM+mode, and
  ESM+mode+scope forms using defaults (filtered, all, current=1, total=1;
  an explicit all/main/ints scope changes the missing-pair fallback to current=0).
  `all`, `main`, and `ints` parse a chunk pair only when both tokens are
  present; `other` skips that parser entirely. The pair uses `atoi`-to-uint
  semantics (`0`/`total < current` fail), and trailing words are ignored.
  These are parser rules; downstream partial/filtered support is documented
  separately in [09-PRECOMBINED-PREVIS.md](09-PRECOMBINED-PREVIS.md).
- `FUN_140916980` constructs the three language-specific localized-string
  outputs `Data\\STRINGS\\<ESM stem>_<Language>.{STRINGS,DLSTRINGS,ILSTRINGS}`
  through `FUN_14091aa30`; see [05-BATCH-CLI.md](05-BATCH-CLI.md) and the
  byte-level reconstruction
  [`ghidra-dump/decompile-CompileTextExport-FUN_140916980.md`](ghidra-dump/decompile-CompileTextExport-FUN_140916980.md)
  for its `bValidateStringsOnCompile` preflight, backup-before-primary root
  order, supplemental filename-ID scan, temporary-file status bytes, and hard
  versus recoverable failure paths.
- `-Install` (`0x400`) does **not** run a process — it calls `FUN_14047cc40(1)`,
  the AdminTasks elevation runner.
- The batch-failure contract (`RaiseException(0x6b650000/1)`) is used by
  `-ConvertMaterials` and `-CompressPSG` directly, and by the shared runner for
  every process when `bThrowProcessCompleteExceptions:General` is set — see
  [19-BATCH-EXIT-CODES.md](19-BATCH-EXIT-CODES.md) for the full gate-setting
  analysis and the complete 31-bit exit-code matrix.

## Arg helpers: the quoted-token splitter and the platform validator

Two helper functions do the finer arg parsing the plain `strtok_s` branches
can't; the export branches use them to consume their per-switch arg slots
(`DAT_143d2d3d0 + idx*0x104`).

### `FUN_140490960` — quoted-token splitter (`byte *FUN_140490960(byte *buf, undefined8 *next)`)

An in-place `strtok`-style splitter that understands double quotes. Returns a
pointer to the next token, and writes the **continuation pointer** into
`*next` (the char right after the token's terminator — so callers loop with
`FUN_140490960(next, &next)`).

- NUL-terminates each token **in place** (writes `0` over the terminator).
- **Quoted tokens** start with `"` (0x22): they may contain spaces and run to
the closing `"`, which is NUL'd. This is how paths with spaces survive.
- **Unquoted tokens** are `isgraph` runs: terminated by the first whitespace
or quote character (also NUL'd). Leading whitespace is skipped.
- Returns `NULL` at end of string (then `*next = 0`).

Used by `-GenerateSingleLip` (tokens 1–2) and `-CompileTextExport` (tokens 3–4,
after two `strtok_s` tokens). For `-CompileTextExport`, the ESM and language
remain ordinary space-delimited tokens; only the required primary root and
optional backup root can preserve spaces with quotes. The worker's exact input
and failure behavior is retained in
[`ghidra-dump/decompile-CompileTextExport-FUN_140916980.md`](ghidra-dump/decompile-CompileTextExport-FUN_140916980.md).

### `FUN_1404905a0` — generation-target validator (`undefined4 FUN_1404905a0(char *tok)`)

Validates the `<XB1|X64|PS4|W32>` selector. Case-insensitive (`_stricmp`); on
failure logs `"Unrecognized generation target '%s'. Should be XB1, X64, PS4 or W32."`
and returns 0 (the branch then logs its own parse error and skips the process).

| token | writes | platform enum |
|---|---|---|
| `XB1` | `thunk_FUN_1420d3870(0)` | 0 |
| `X64` | `thunk_FUN_1420d3870(0)` | 0 |
| `W32` | `thunk_FUN_1420d3870(1)` | 1 |
| `PS4` | `thunk_FUN_1420d3870(2)` | 2 |

`FUN_1420d3870` / `FUN_1420d3880` are the platform **setter/getter pair** for the
global `DAT_147322d04` (setter `MOV [mem],EAX; RET`, getter `MOV EAX,[mem]; RET`).
The FaceGen string encoders `FUN_1420d4410` / `FUN_1420d4840` consume it and pick
a per-platform binary format table: platform 0 (XB1/X64) → `.data 0x328c0c0`,
platform 1 (W32) → `0x328c0b8`, platform 2 (PS4) → `0x328c118`.

### Export branches — slot consumption

| branch | slot (RVA) | tokenizer | tokens → process args |
|---|---|---|---|
| `0x2` `-ExportDialogue` | `0x3d2d4d4` (idx 1) | none in dispatcher/Execute | raw `<esm>` only; exact file lookup → `DialogueExporter` via runner |
| `0x8` `-ExportFaceGenData` | `0x3d2d6dc` (idx 3) | `strtok_s` | `<esm> <platform>`; `FUN_1404905a0` gates → `FaceGenDataExporter` via runner |
| `0x2000000` `-ExportDismemberData` | `0x3d2ed34` (idx 25) | `strtok_s` | `<esm> <platform>`; same gate → `DismemberDataExporter` via runner |
| `0x20000` `-GenerateStaticCollections` | `0x3d2e514` (idx 17) | `strtok_s` | `<esm> <platform>`; same gate → `StaticCollectionProcess` via runner |
| `0x40` `-CompileTextExport` | `0x3d2d9e8` (idx 6) | `strtok_s` ×2 then `FUN_140490960` ×2 | `<esm> <lang> <PathToTextExport> [PathToBackupTextExport]` → `FUN_140916980` batch |
| `0x10000` `-GenerateSingleLip` | `0x3d2e410` (idx 16) | `FUN_140490960` ×2 | `"WAVFilePath" "Text For WAV File."` → `FUN_1406495e0`; output = wav path + `.lip` |

On validator failure the three platform-gated branches fall through to their
usage-string log and **skip** the runner call (no process runs, no raise).

## Global state touched

| global | RVA | meaning |
|--------|-----|---------|
| `DAT_143d2f34c` | `0x3d2f34c` | the switch **bitmask** (OR-accumulated, read by dispatcher) |
| `DAT_143d2f350` | `0x3d2f350` | dispatch "running" flag (1 during dispatcher, 0 after) |
| `DAT_143d2d3d0` | `0x3d2d3d0` | 31 × 260-byte arg slots; slot `idx` holds `-Switch:` remainder |
| `DAT_143d3410b` | `0x3d3410b` | batch-mode flag |
| `DAT_147fda42c` | `0x47fda42c` | batch-mode marker (`0xffffffff`) |
| `DAT_143d33b60` | `0x3d33b60` | NIF-preview file path |
| `DAT_143d3410a` | `0x3d3410a` | NIF-preview mode flag |

## Verified data (raw dump)

```
.data 0x3b8c740 (+idx*8) -> .rdata token:
  0: 0x2fb60b0 "-OptimizeMasterFile"
  1: 0x2fb60c8 "-ExportDialogue"
  2: 0x2fb60e0 "-GenerateLips"
  3: 0x2fb60f0 "-ExportFaceGenData"
  4: 0x2fb6108 "-TagifyMasterfile"
  5: 0x2fb6120 "-ExportText"
  6: 0x2fb6130 "-CompileTextExport"
  7: 0x2fb6148 "-DelocalizeMasterfile"
  8: 0x2fb6168 "-SaveDefaultPlugin"
  9: 0x2fb6180 "-CheckInPlugin"
 10: 0x2fb4c88 "-Install"
 11: 0x2fb6198 "-DumpNeededFiles"
 12: 0x2fb61b0 "-OptimizeDLC"
 13: 0x2fb61c0 "-GenerateSEQ"
 14: 0x2fb61d0 "-GenerateWarnings"
 15: 0x2fb61e8 "-GenerateAnimInfo"
 16: 0x2fb6200 "-GenerateSingleLip"
 17: 0x2fb6218 "-GenerateStaticCollections"
 18: 0x2fb6238 "-GeneratePreCombined"
 19: 0x2fb6258 "-GeneratePreVisData"
 20: 0x2fb6270 "-DepersistRefs"
 21: 0x2fb6288 "-MapMaker"
 22: 0x2fb6298 "-MapInfo"
 23: 0x2fb62a8 "-OutputAreaArt"
 24: 0x2fb62c0 "-UpdateModelData"
 25: 0x2fb62d8 "-ExportDismemberData"
 26: 0x2fb62f8 "-ConvertMaterials"
 27: 0x2fb6310 "-ImportScalingData"
 28: 0x2fb6328 "-CompressPSG"
 29: 0x2fb6338 "-BuildCDX"
 30: 0x2fb6348 "-ConvertToESL"
```

## Decompiles

- `ghidra-dump/CLI-PARSER-FUN_140487010.txt`
- `ghidra-dump/CLI-DISPATCHER-FUN_1404870f0.txt`
- `ghidra-dump/decompile-arghelpers-490960-4905a0-20d3870-20d3880-20d4410-20d4840.log`
  (quoted-token splitter, platform validator, platform setter/getter, FaceGen string encoders)
