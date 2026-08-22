# Bethesda localized-string files: `.STRINGS` / `.DLSTRINGS` / `.ILSTRINGS`

**Format reference.** Byte-level layout of the three localized-string binaries the
Creation Kit's `-CompileTextExport` worker (`FUN_140916980`, doc 05) writes into
`Data\STRINGS\<ESM stem>_<Language>.{STRINGS,DLSTRINGS,ILSTRINGS}`.

- **Samples:** real Fallout 4 1.10.x files, all shipped languages, extracted from
  `Fallout4 - Misc.ba2` (`.../TEMP_BSA_FILES/.../STRINGS/`). `Fallout4_en.*` is the
  reference set used below.
- **Writer:** `FUN_14091d790` (the per-context finalizer of `FUN_140916980`), which
  converts each `_temp` staging stream into the final file.
- **Loader side:** `.DLSTRINGS` block loader `FUN_140915000` and friends; the engine
  source path `shared\misc\bgslocalizedstring.cpp` is embedded in the binary.

All three files share one header and one offset table; only the string-record
encoding in the data block differs.

## 1. File layout

```text
offset  size  field
------  ----  -----------------------------------------------------------
0x00    4     u32  numStrings   (entry count, == number of offset-table rows)
0x04    4     u32  dataSize     (byte size of the data block at file+8+8*numStrings)
0x08    8*N   rows { u32 stringID; u32 offset; }   (N == numStrings)
0x08+8N  D    data block (D == dataSize)
```

Total file size is exactly `8 + 8*numStrings + dataSize` (verified on every sample).

- `numStrings` — row count of the offset table. There is no other count field.
- `dataSize` — the exact byte length of the data block; the file ends right after it.
- The offset table is **sorted ascending by `stringID`** (the writer sorts the
  records by ID before emitting the table). `stringID` values are **not** dense:
  only IDs that actually have text appear.
- `offset` — byte offset into the data block where that ID's record starts
  (offset 0 = first record). The data block is written in **insertion/hash-table
  order, not ID order**, so consecutive table rows do not imply consecutive
  offsets. Multiple rows may share one offset (deduplicated identical text).

### 1a. `.STRINGS` — NUL-terminated text records

```text
record:  [ text bytes ... 0x00 ]
```

The data block is a concatenation of NUL-terminated UTF-8/legacy-ANSI strings.
`offset` points at the first text byte; the string runs to the next NUL.
No length prefix is present.

### 1b. `.DLSTRINGS` / `.ILSTRINGS` — length-prefixed text records

```text
record:  [ u32 len ][ text bytes (len-1) long ][ 0x00 ]
```

The data block is a concatenation of records; `offset` points at each record's
`u32 len`. **The stored `len` includes the trailing NUL** (`len == strlen+1`;
the hex dump below shows `len=24` for a 23-char string). The payload following
the length field is exactly `len` bytes and the record footprint is
`4 + len`. The data block walks cleanly to EOF as `sum(4 + len)`, and every
table offset lands on a record boundary.

The writer selects the format with the output context's `+0x14e` flag byte:
clear → NUL-terminated `.STRINGS` style, set → length-prefixed
`.DLSTRINGS`/`.ILSTRINGS` style (see §3).

## 2. Annotated hex dump (`Fallout4_en.DLSTRINGS`)

```text
file size 542,887 = 8 + 4,759*8 + 504,807        (invariant holds)

00000000  97 12 00 00                            u32 numStrings = 0x1297 = 4,759
00000004  e7 b3 07 00                            u32 dataSize   = 0x7b3e7 = 504,807
00000008  03 00 00 00  00 00 00 00               row 0: stringID=3    offset=0
00000010  07 00 00 00  1c 00 00 00               row 1: stringID=7    offset=0x1c
00000018  08 00 00 00  3a 00 00 00               row 2: stringID=8    offset=0x3a
        ...  (4,756 more 8-byte rows; ids ascending, offsets not) ...
000094C0  (data block starts at file offset 0x94C0 = 8 + 4,759*8)

data+0    18 00 00 00                            u32 len = 24 (text "Your Infection
                                                   spreads." is 23 chars + NUL)
data+4    59 6f 75 72 20 49 6e 66 65 63 74 69    "Your Infecti"
          6f 6e 20 73 70 72 65 61 64 73 2e 00    "on spreads.\0"   (24 bytes)
data+28   1a 00 00 00 ...                        next record: len=26 ...
```

`.STRINGS` equivalent (`Fallout4_en.STRINGS`): `numStrings=43,933`,
`dataSize=1,173,806`, first row `id=1 off=0` → text `Interrupted\0`; rows run
`id=2 off=12` → `Cures Illness\0`, `id=4 off=26` → `Sleep Interrupted\0`, ...
IDs `1..224,469` with gaps; 61 rows are legacy (CP1252/GB2312) encoded rather
than UTF-8 — encoding is content-level, the container is identical.

## 3. Writer (`FUN_14091d790`, RVA 0x91d790)

Confirmed from machine code:

1. Walks the string collection (`+0x30` array, entries 0x18 bytes: `+0` id,
   `+8` text, `+0x10` hash node) and validates consistency (`0x14091d916`,
   `0x14091d94d`, `0x14091d969` bounds/count asserts).
2. Builds `(stringID, offset)` rows (`0x14091d983`–`0x14091d9e7`); the offset is
   the running data-block position captured per record.
3. Sorts the rows by stringID via `FUN_1409186d0` (16-byte element sort —
   `0x14091da46`).
4. Per record: writes `u32 stringID` and the data offset to the temp table
   (`+0x38` stream), then, gated on context flag `+0x14e` (`0x14091dcf0`):
   - flag set  → write `u32 len+1` (text length plus the NUL) then the text
     and its NUL (`0x14091dd02`/`0x14091dd46`),
     i.e. the length-prefixed record — used for `.DLSTRINGS`/`.ILSTRINGS`;
   - flag clear → write the text only, NUL-terminated `.STRINGS` style.
   Accumulates `dataSize` (`+0x1138`) and the record count (`+0x1134`).
5. Seeks the temp stream to 0 (`0x14001fbe5` via `0x14091dd86`), streams it out
   in 0x1000-byte chunks to the final stream, then writes `u32 numStrings`
   and `u32 dataSize` at the front (`0x14091dc1a`/`0x14091dc37`) and closes.
   The finalizer's `+0x14d` status byte marks the outcome (the caller
   `FUN_140916980` finalizes all three contexts at `0x140917705/711/71d`).

The three output contexts are built by `FUN_14091aa30` with the embedded
extensions `.STRINGS` / `.DLSTRINGS` / `.ILSTRINGS`; the flag at `+0x14e`
distinguishes the length-prefixed pair from the NUL-terminated form.

## 4. ID partitioning across the three files

From the reference samples (en):

| file | count | ID range (first..last) | record encoding |
|---|---:|---:|---|
| `.STRINGS`  | 43,933 | 1 .. 224,469 | NUL-terminated |
| `.DLSTRINGS` | 4,759 | 3 .. 224,466 | u32-length-prefixed |
| `.ILSTRINGS` | 74,953 | 75,462 .. 223,315 | u32-length-prefixed |

IDs are globally unique across the game's three tables (a string ID lives in
exactly one file) **with one known shipped exception**: every shipped language
set carries the two Enclave Creation-Club paint descriptions (IDs `0x36CB7`
and `0x36CB9`) in *both* `.STRINGS` (English text) and `.DLSTRINGS` (localized
text). The engine tolerates the collision; treat the uniqueness rule as a
writer invariant with those two exceptions. IDs below `0x10` are reserved and
skipped by the compiler (`Attempting to use reserved ID 0x%08X. Skipping.`) —
this is an input-side rule; compiled files legitimately contain low IDs
(`.STRINGS` starts at 1, `.DLSTRINGS` at 3). The `.DLSTRINGS` set
(dialogue/voice lines) and `.ILSTRINGS` set (misc localizable UI) are disjoint
ranges; `.STRINGS` carries the ordinary text. The compiler routes categories
`Quest Stage Log Entry`, `Book`, `Description`, `Message` (indices 1,6,7,10 of
the 18-category table) to the dialog table; all other categories go to the
ordinary table.

## 5. Loader / consumer notes

- `.DLSTRINGS` block loader `FUN_140915000` (RVA `0x915000`) resolves the
  language-specific file, reads `numStrings` into `+0x790`/`+0x798`, then reads
  `8*numStrings` bytes for the table and per-row `(stringID, offset)` lookup;
  the `bKeepDLStringBlocksLoaded:General`/`bKeepILStringBlocksLoaded:General`
  values are tested at `0x1409155b5`/`0x140915d8b` (+3 sites each).
- Compiler guard: `Text Compile: string file too large skipping: %s`
  bounds oversized tables.
- The engine source of the machinery is `shared\misc\bgslocalizedstring.cpp`.

## 6. Verification

Byte-parsed with an independent Python checker across 33 shipped files (11
languages × 3 kinds):

- size invariant `len(file) == 8 + 8*numStrings + dataSize` — 33/33;
- table rows ascending by ID, offsets in-bounds, every offset valid
  (`.STRINGS`: NUL present; DL/IL: offset on a length-prefix boundary) — 33/33;
- DL/IL data blocks walk to EOF as `sum(4+len)` — 22/22;
- all texts decode (UTF-8 or legacy ANSI; CJK/Russian files verified);
- stringID partition across the three tables — 33/33 modulo the two known
  Enclave-paint exceptions (§4) present in every language set.

The `--strings` mode of `tools/validate_ckini.py` re-checks all of the above
per file plus the per-language cross-file partition, surfacing the Enclave
exceptions as a documented warning rather than a failure.
