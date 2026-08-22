# Session handoff: packfile layout converter + Havok oracle audit

## Goal
Build a Fallout 4 Havok packfile **layout converter** so BGS can read and write both the
8-byte-pointer layout FO4 ships and the 4-byte-pointer layout the 32-bit Havok tools use.
The point is not the conversion itself. The point is that a 4-byte file can be handed to
Havok's own `AssetCc2` (which is 32-bit and understands behaviour classes), so Havok grades
BGS's understanding of the format. This is the audit mechanism BGS currently lacks.

Tracks GitHub issue #24 "Honour packfile pointer layout rules on read and write".

## Two pieces, kept strictly apart

1. **Layout converter — ships in BGS.** Pure format code in `src/Hkx`. Reads/writes both
   layouts into the same object model. No mention of any external tool. This is the feature.

2. **Oracle harness — dev-only, never committed.** Drives the Havok tools under Wine to
   produce reference output, compares BGS against it. Lives outside the repo (RE workspace or
   an untracked local dir). Issue #9 sets the rule: shipped code has zero external tool or
   runtime dependency. The harness is a test aid, not product.

## Hard constraints (do not violate)
- Commit as **NomadsReach** only. No AI author, committer, co-author trailer, comment, or
  commit-message wording. AI must never appear in this repo in any form.
- Respect all hooks. If a command is blocked, stop and hand it back. Never route around it.
- Never vendor the Havok SDK or the Content Tools, or any of their binaries or extracted
  files, into any repo. They are read locally for reference only.
- Do not reference the Havok tools, Wine, AssetCc, HCT, or file dumps anywhere in the repo,
  in code, comments, tests, commit messages, or issues.

## Format facts (little-endian throughout)

Header, first 0x40 bytes:
- 0x00 magic `0x57e0e057`, `0x10c0c010`
- 0x08 userTag (i32)
- 0x0C fileVersion (i32), FO4 = 11
- 0x10 **layout rules, 4 bytes: [bytesInPointer, littleEndian, reusePadding, emptyBaseClass]**.
  FO4 = `8,1,0,1`. Target for conversion = `4,1,0,1`.
- 0x14 sectionCount (i32)
- 0x18 contentsSectionIndex, 0x1C contentsSectionOffset
- 0x20 contentsClassNameSectionIndex, 0x24 contentsClassNameSectionOffset
- 0x28..0x38 contentsVersion string ("hk_2014.1.0-r1")
- 0x38 flags (i32)
- 0x3C maxPredicate (i16), 0x3E predicate byte count (i16)
- then the predicate area, then section headers each 0x40 bytes.

Section header, 0x40 bytes each (see `PackfileImage.cs` `PackfileSection.Read`):
- +0x00 tag, 20 bytes null-padded ("__classnames__", "__data__")
- +0x14 absolute data start (i32)
- +0x18..0x2C six marks: ends of local fixups, global fixups, virtual fixups, then exports,
  imports, end. All are offsets into the section's data and all move under conversion.
- +0x30..0x40 header tail.

What changes 8-byte -> 4-byte:
- pointer and string members: 8 -> 4 bytes
- `hkArray`: 16 -> 12 bytes (data ptr shrinks; int count and int capacity+flags stay)
- the object's leading vtable/header pointer shrinks, shifting every later member
- alignment and trailing padding recompute, so object sizes shrink
- local fixups (pointer patch sites) and global/virtual fixups (cross-object/section refs):
  every recorded offset moves, and the tables must be rebuilt against the new offsets

Derive the 4-byte member offsets from `src/Hkx/HavokClassTypes.json` (member type + the same
size/offset data), walking each class with a 4-byte pointer rule. Do **not** hand-write a
second offset table. One source of layout truth. That is the whole safety argument: the same
table has to produce both layouts, so a wrong offset can no longer hide in the single layout
it is currently exercised in.

## Relevant BGS code
- `src/Hkx/PackfileImage.cs` — header/section read + `Rebuild()` write. HeaderSize and
  SectionHeaderSize are both 0x40. This is where layout awareness has to enter.
- `src/Hkx/PackfileObjects.cs` — object model over a section.
- `src/Hkx/HavokClassTypes.json` — class table: members with offset/vtype/size, plus signatures.
- `src/Hkx/FixupOrder.cs` — fixup ordering on rebuild.
- `src/Hkx/SaveVerifier.cs` — existing byte-level verification; extend, do not duplicate.

## Oracle harness (local only, out of tree)

Durable installer (NSIS; `7z x` to extract):
`ReverseEngineering/Havok/Havok Content Tools Compendium/Havok Content Tools/Version 2014/HavokContentTools_2014-1-0_20140830_64Bit_PcXs.exe`
- `hctStandAloneFilterManager.exe` is x64 — the only tool that opens FO4 8-byte files.
- `BatchProcess/AssetCc/AssetCc2.exe` is x86 — understands behaviour classes, reads 4-byte only.

Setup:
- Silent-install into a dedicated Wine prefix: `wine <installer> /S` lands it under
  `drive_c/Program Files/Havok/HavokContentTools`. Set `WINEDLLOVERRIDES="winedbg.exe=d"` so a
  crash does not hang on the debugger. Filter mesa/pci/egl stderr noise.
- Write a one-filter `.hko` (single "Write to Platform", `bytesInPointer` 4, preset MSVC_WIN32,
  `tagfile` false, literal filename). Full template is in the `hct-wine-ground-truth-pipeline`
  memory file.

Reference pipeline (Havok's own 64->32, for byte comparison):
1. `wine hctStandAloneFilterManager.exe -s to32.hko -t <in>.hkx` -> `out32.hkx`  (`-t` is a bare bool)
2. `wine AssetCc2.exe -x out32.hkx out.xml` -> ground-truth XML

Limit: works on animation files and **stock-class** behaviour files. Files containing Bethesda
custom classes (`BGSGamebryoSequenceGenerator`, `BSIsActiveModifier`, `BSAssignVariablesModifier`,
`DynamicAnimationTaggingGenerator`, and the rest of the `BS*`/`BGS*` set) cannot be validated
this way and must be excluded from the audit set.

## The audit loop (gold standard)
Corpus: `ReverseEngineering/.bgs-anim-corpus` (~531 behaviour files; ~521 are stock-only).
Select files whose `__classnames__` contains no class starting with `BS`, `BGS`, or
`DynamicAnimationTagging`.

For each stock file F (8-byte):
- **A. Reference:** F -> filter manager -> `F32_havok` (Havok's own 32-bit output).
- **B. BGS:** read F into the object model, write as 4-byte layout -> `F32_bgs`.
- **C. Byte compare** `F32_bgs` vs `F32_havok`. Identical = BGS converter provably correct.
- **D. Independent load:** `AssetCc2 -x F32_bgs`. If it loads and the XML matches Havok's, the
  layout is structurally valid by Havok's own reader.

Also keep the existing 8-byte round-trip: F -> BGS read -> BGS write 8-byte == F.

## Success criteria
- BGS reads and writes both 4-byte and 8-byte layouts into one object model.
- 4-byte offsets derived from `HavokClassTypes.json`, no second hand-written table.
- Across the stock corpus set, `F32_bgs == F32_havok` byte-for-byte.
- `AssetCc2` loads every `F32_bgs` with no error.
- Regression tests cover both layouts over the same source objects.
- Big-endian stays out of scope: report the endian byte as unsupported rather than half-handle it.

## First steps
1. Read `PackfileImage.cs`, `PackfileObjects.cs`, and the shape of `HavokClassTypes.json`.
2. Write the 4-byte offset walker over the class table and unit-test it against a few known
   classes before touching serialization.
3. Stand up the oracle harness on 3-5 stock files, get `F32_havok` for them.
4. Implement 4-byte write, drive the byte compare, iterate until identical.
5. Widen to the full stock set; record any class that will not match.
