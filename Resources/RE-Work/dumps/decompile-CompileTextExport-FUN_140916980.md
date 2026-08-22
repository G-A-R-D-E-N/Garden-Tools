# `FUN_140916980` — CompileTextExport decompile

**Binary:** Fallout 4 CreationKit.exe 1.11.137.0, image base `0x140000000`.

This is the direct worker for dispatcher bit `0x40` (`-CompileTextExport`). It
is called by `FUN_1404870f0`, not through `FUN_1404906a0`:

```c
bool FUN_140916980(
    char const *esmName,             // RCX, dispatcher token 1
    char const *language,            // RDX, dispatcher token 2
    char const *textExportRoot,      // R8, quoted helper token 3
    char const *backupExportRoot);   // R9, optional quoted helper token 4
```

The dispatcher calls it as:

```c
pcVar10 = strtok_s(slot, " ", &save);       // ESM
pcVar11 = strtok_s(NULL, " ", &save);       // language
pcVar9  = FUN_140490960(save, &save);       // required root
pcVar8  = FUN_140490960(save, &save);       // optional backup root
ok = pcVar10 && pcVar11 && pcVar9 && *pcVar9
     ? FUN_140916980(pcVar10, pcVar11, pcVar9, pcVar8) : false;
```

The first two tokens are ordinary `strtok_s` tokens; only the root tokens are
quote-aware. Therefore an ESM or language token cannot contain spaces, while a
text-export root may be written as `"root with spaces"`.

## 1. Output-context construction (`0x1409169c2`–`0x140916c5b`)

The function constructs three independent localized-string output contexts by
calling `FUN_14091aa30` three times:

| call site | extension literal | context flag |
|---|---|---:|
| `0x140916c21` | `.STRINGS` (`[0x143b9be90]`) | 0 |
| `0x140916c3e` | `.DLSTRINGS` (`[0x143b9be98]`) | 1 |
| `0x140916c5b` | `.ILSTRINGS` (`[0x143b9bea0]`) | 1 |

`FUN_14091aa30` receives the ESM name as its `r8` argument and the language as
the fifth stack argument. It finds the **last** `.` in the ESM name (`strrchr`, import thunk
`0x142d68e96`), keeps the prefix as the stem, and formats the final path with the literal
`Data/%s/%s_%s%s` (`0x143015ec0`):

```text
Data/STRINGS/<ESM stem>_<language>.STRINGS
Data/STRINGS/<ESM stem>_<language>.DLSTRINGS
Data/STRINGS/<ESM stem>_<language>.ILSTRINGS
```

The directory component is the literal `STRINGS` (`0x143b9bea8`). The language
is not normalized or validated by this function.

A name without a dot uses the whole input as the stem; a dot at the beginning
gives an empty stem according to the helper's `strrchr`/length arithmetic.

Each context also formats `<final>_temp` (`0x1430164f8`) and opens a temporary
stream. The context object has two relevant status bytes at `+0x14c` and
`+0x14d`; the constructor clears them. An output open/resource error marks
`+0x14c`, and `FUN_140916980` checks the three corresponding bytes at:

```text
first  context + 0x14c = function frame + 0x73c
second context + 0x14c = function frame + 0x38c
third  context + 0x14c = function frame + 0x4dc
```

If any is set, control reaches `0x1409177a3`, logs
`Text Compile: Could not open all output files, aborting process.`
(`0x1430166b0`), sets the function failure flag, and skips input compilation.

## 2. Optional character-set preflight (`0x1409169f5`–`0x140916c02`)

The byte at `0x143b9c088` is the registered
`bValidateStringsOnCompile:General` value. When it is clear, the function runs
the existing character-set/resource preflight before constructing the outputs;
when it is set, it jumps directly to output construction at `0x140916c09`.
The preflight reads the configured resource (the default resource literal in
this build is `Interface/FontConfig.txt`) into a bounded `0x400`-byte buffer,
normalizes/compares its characters, and can set the shared failure path before
compilation. The exact resource reader is `FUN_14007d8e9` plus its stream
vtable call at `0x140916a66`; the bounded read is `FUN_14001f168`.

This setting is not a CLI argument. It changes the preflight path and is loaded
by the normal CreationKit.ini settings reader.

## 3. Input-root list and path construction (`0x140916c87`–`0x140916e4c`)

The function builds a small list of roots. If `backupExportRoot` is non-null
and non-empty, it appends that root first (`0x140916c9d`); it then appends the
primary `textExportRoot` unconditionally (`0x140916cb5`). The observable order
is therefore:

```text
[backupExportRoot,] textExportRoot
```

For each root, the main input path is formatted by `FUN_14000415b` at
`0x140916d25` with `%s/%s/%s` (`0x143015708`):

```text
<root>/<ESMFilename>/TextExport.txt
```

`TextExport.txt` is the literal pointed to by `0x143b9bf88`. The ESM filename
is retained as passed; this input path does not use the output stem or language.
The root entries are engine string/resource objects, so an empty or unavailable
root can fail during resource resolution before the file parser runs.

The function then walks the root's input collection. The mandatory
`TextExport.txt` is opened/read first. When the input collection has additional
ID-named text files, the worker derives an ID from the filename stem: it removes
the final extension, requires an 8-character hexadecimal suffix, and uses the
hex value as the file ID. A malformed filename is non-fatal and is skipped with
`Text Compile: Could not parse ID from filename: %s. Skipping.`

## 4. Exact `TextExport.txt` line grammar and routing

The per-line parser is `FUN_14091f3d0`, called at `0x140916e94`. The CK's
`-ExportText` producer helper `FUN_14091bd80` writes the exact four-field line
at `0x1409168fa`; the durable annotated body and producer proof are in
[`decompile-CompileTextExport-line-parser-FUN_14091f3d0.md`](decompile-CompileTextExport-line-parser-FUN_14091f3d0.md).

```text
0x<8-hex-StringID>\\t0x<8-hex-FormID>\\t<Category>\\t<Text>\\n
```

The fields are:

| field | producer | parser behavior |
|---:|---|---|
| 1 | `0x%08X` StringID | parsed with `_strtoui64(..., 16)`; zero, no digits, and `-1` are rejected; the low 32 bits are retained |
| 2 | `0x%08X` owning FormID | structural metadata only; the compiler does not parse or validate it |
| 3 | one of the 18 category names below | compared case-insensitively with `_stricmp`; unknown/empty names keep the default `Misc` category |
| 4 | localized text | copied as the string payload; tabs after the third delimiter are preserved |

`FUN_14091f3d0` reads one byte at a time, replaces the first three tabs with
NUL delimiters, and saves the pointer after tab 3 as `+0x28` in its parser
state. It therefore requires three tabs but does not require a non-empty FormID
or text field. There is no quote or backslash escape grammar. CR or LF ends a
record; the append buffer has a `0x100000`-byte reasonable-size guard.

The accepted category table is the 18-qword table at `.data`/`.rdata`
`0x143b9bef0`, in exact numeric order:

| index | category token | localized-string route |
|---:|---|---|
| 0 | `Response Text` | ordinary `.STRINGS` |
| 1 | `Quest Stage Log Entry` | dialog `.DLSTRINGS` |
| 2 | `Quest Objective Display Text` | ordinary `.STRINGS` |
| 3 | `Name` | ordinary `.STRINGS` |
| 4 | `Topic Text` | ordinary `.STRINGS` |
| 5 | `Game Setting` | ordinary `.STRINGS` |
| 6 | `Book` | dialog `.DLSTRINGS` |
| 7 | `Description` | dialog `.DLSTRINGS` |
| 8 | `Faction Rank` | ordinary `.STRINGS` |
| 9 | `Note` | ordinary `.STRINGS` |
| 10 | `Message` | dialog `.DLSTRINGS` |
| 11 | `Message Box Button` | ordinary `.STRINGS` |
| 12 | `Ammo Short Name` | ordinary `.STRINGS` |
| 13 | `Body Part Name` | ordinary `.STRINGS` |
| 14 | `Alternate Activation Text` | ordinary `.STRINGS` |
| 15 | `Activate Choice Label` | ordinary `.STRINGS` |
| 16 | `Loading Screen Text` | ordinary `.STRINGS` |
| 17 | `Misc` | ordinary `.STRINGS` |

The caller's `0x4c2` bit test sends indices `1, 6, 7, 10` to the dialog
context; the third `.ILSTRINGS` context is used by the supplemental/localized
input paths rather than by another category name. The parser's unknown-category
warning is `Text Compile: Using MISC category, could not find category match
for: %s`; fallback is not a hard parse failure.

The output insertion/character-normalization helper is `FUN_14091d240`, called
for recognized records at `0x140916f7d` and `0x140917069`. The compiler's
embedded diagnostics establish these additional cases:

- `Error parsing line %u` — malformed line;
- `Could not parse ID from string: %s` — no usable ID in a text record;
- `Attempting to use reserved ID 0x%08X. Skipping.` — reserved ID is skipped;
- `Duplicate ID found: 0x%08X` — duplicate in the aggregate string table;
- `Duplicate ID found (0x%08X) in input file: %s` — duplicate within an input;
- `Duplicate ID found (0x%08X) on input file: %s` — duplicate against an input
  file's ID;
- invalid character diagnostics from `FUN_14091d240` — auto-fix or replacement
  with a space/allowed punctuation;
- `Buffer exceeding reasonable size.` — bounded input/string protection.

The reserved-ID, duplicate, category, and invalid-character paths are recovery
or skip diagnostics in the parser; they are not equivalent to the hard output or
mandatory-input failures. A caller should treat the final boolean as authoritative
rather than assuming every warning string means the whole compile failed.

## 5. Hard failure and cleanup paths

The function keeps a failure byte in `r13b`, initialized to zero at
`0x140916c87`. Confirmed hard-failure transitions are:

1. Any of the three output contexts reports an open/resource failure
   (`0x140916c60`–`0x140916c81`) → `0x1409177a3` sets `r13b`.
2. The mandatory input cannot be opened/read. The input stream is checked at
   `0x140916d90`/`0x140916dc3`; the failure path sets the per-file error state,
   logs `Text Compile: Could not open input file, aborting process. (Path: %s)`
   (`0x143016700`), and propagates failure to the final return.
3. An output write/finalization status is carried by each context. The finalizer
   `FUN_14091d790` is called for all three contexts at `0x140917705`,
   `0x140917711`, and `0x14091771d`; it uses the context status bytes and stages
   the final stream from the `_temp` file. Context destructors run on both the
   normal and error exits. The caller does **not** branch on a return value after
   these three finalizer calls; `+0x14d` is therefore an internal finalizer status,
   not an independently proven fourth `r13b` failure transition.

The input-file read warning
`Text Compile: Could not open file for read:\t%s` (`0x143016508`) and the
supplemental-file warning
`Text Compile: Could not open file for read, skipping:\t%s`
(`0x1430163a0`) occur in per-file branches. The latter is explicitly a skip;
the mandatory `TextExport.txt` branch is the aborting case.

On cleanup, `FUN_14091d790` finalizes each output context, the input list and
stream objects are released, and the function returns `!r13b` (`0x140917871`–
`0x140917883`). Thus the dispatcher maps the return to:

```text
true  -> RaiseException(0x6b650000, 0, 0, NULL)
false -> RaiseException(0x6b650001, 0, 0, NULL)
```

when `bThrowProcessCompleteExceptions:General` is enabled. With the gate off,
that direct raise is skipped and the normal batch window close path is used.

## 6. Evidence / limits

Confirmed directly from machine code: argument order, last-dot (`strrchr`) stem handling,
three extensions, `_temp` staging, output status offsets, backup-before-primary
root order, `%s/%s/%s` input path, the exact four-field TextExport line framing,
filename-ID parsing branch, category table and dialog-routing bitmask, hard
input/output failure diagnostics, finalizer calls, and boolean return. The
caller’s lack of a post-finalizer status test is also confirmed; whether a late
`+0x14d` finalizer error is surfaced through another shared status path is not
established by this function alone. The parser intentionally ignores the second
(FormID) field after using its tab as a delimiter; form-to-category producer
selection is data-dependent on the owning form/chunk and is separate from the
18-token parser table.
