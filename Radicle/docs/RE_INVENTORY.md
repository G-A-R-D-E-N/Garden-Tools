# Reverse-engineering inventory

Sources were inspected in place. CKRE and ReverseEngineering were not modified.

Confidence: `confirmed` / `strong` / `probable` / `speculative`.

## Source trees

| Tree | What it is | Useful for this library? |
|------|------------|--------------------------|
| `CKRE` | CreationKit.exe 1.11.137.0 | Record enum, masterfile save pipeline, ESL converter switch, TES* class list |
| `ReverseEngineering` | Fallout4.exe OG 1.10.163 unpacked (AE 1.11.221 also present) | TESFile API, FormID compile indexes, CELL/LAND, chunk Load() patterns |
| CommonLibF4 (oracle, not copied) | Engine headers | FORM/FILE_HEADER sizes, TESFile flags, ENUM_FORM_ID |
| PluginEditTool / Mutagen (oracle, not copied) | FO4IDE tests | On-disk light-master FormID `0x02000812` case |

**Caveat:** ReverseEngineering's original Ghidra project was built from a packed exe. Decompiles from that project are unverified. Use `fwgh_proj_1.10.163_unpacked`.

---

## CKRE

### Record enum / factory

**Source:** `/run/media/user/Games-Storage/Projects/Tools & Reverse Engineering/CKRE/12-RECORD-DEFINITION.md`  
**App:** CreationKit 1.11.137.0  
**Address:** `formEnumString[]` `.data` RVA `0x3b9a540`; file-load table `.rdata` RVA `0x300d478`; factory `.data` RVA `0x3d49cd0`; validator `FUN_1408bc610`

**Proves:** 159-entry index→4CC table, 24-byte entries, index 0 `NONE`, packed 4CC little-endian `'TES4' = 0x34534554`. Validator lives in `tesform.cpp`. Factory setter `FUN_1408bc350` bounds-checks `< 0x9f`.

**Confidence:** `confirmed`. `CheckValidIDForSmallFile` is `(id & 0x00FFF000) == 0` and `AdjustFormIDFileIndex` is as described in `ESL_FORMIDS.md`, recorded in `ReverseEng/commonlib_NonVR` @ `dbaeca7` as the predicate bytes and as pseudocode respectively (`RE-Work/docs/26-RE-TARGETS.md` section 4, `RE-Work/Runtime/03-FINDINGS.md` "FormID compile-index remap decoded"). Addresses are build-specific: the RE build table lists two images, Tool-Build (`0x5dac778e`) and 1.10.163 (`0x5ddc40fa`), whose RVAs for these functions differ by `0x10`. Match the symbol table to the image by PE timestamp; there is no symbol-table shift to compensate for.

**Library impact:** The generic reader does not need the factory. The schema system should use this 4CC list as the closed set of known signatures, while still preserving unknown signatures.

**Do not trust:** `record-definition-table.txt` class column. It assigns `GMST → BGSKeyword`. Root cause (confirmed by CK audit): MSVC mangling for template ints 1–10 is `$0` + digit `(N-1)`, so `$03` is **4** (KYWD / BGSKeyword), not 3. Ghidra’s demangler already prints `<BGSComponent,8,...>` correctly. Rebuild any class map from RTTI; do not copy that dump.

**Disputed:** "RACE is not file-loadable." CKRE says the load table skips enum index 17. RACE records exist in shipped plugins. Generic parser still reads them.

### TES* class surface

**Source:** `CKRE/02-RTTI-CLASS-MAP.md`, `CKRE/ghidra-dump/tes-classes.txt`  
**App:** CreationKit 1.11.137.0  

**Proves:** 126 `TES*` RTTI classes including `TESForm`, `TESObjectREFR`, `TESWorldSpace`, `TESCell` (RTTI name; CommonLib uses `TESObjectCELL`), `TESNPC`, `TESQuest`, `TESTopic`/`TESTopicInfo`. Mixins: `TESFullName`, `TESCondition`, `TESModel`, etc.

**Confidence:** `confirmed` that the classes exist. Layouts are not in this dump.

**Library impact:** Later schema/class names. Not needed for the generic container.

### Masterfile load / save

**Source:** `CKRE/15-BATCH-PROCESSES.md`, `CKRE/19-BATCH-EXIT-CODES.md`, `CKRE/05-BATCH-CLI.md`  
**App:** CreationKit 1.11.137.0  
**Address:** FileSaver Execute `FUN_14048eb10`; `DataHandler::SaveDefaultPlugin` `FUN_140862120`; runner `FUN_1404906a0`; global active `TESFile*` `DAT_143d33938`

**Proves:** Save pipeline is "prepare files → temp → backup → fill header → save header → saving data → post processing forms". Failures: `CreateTES failed for file '%s'`, `Pre-processing failed on form '%s'`. MasterfileOptimizer converts ESM "optimized for in-game loading" via `BSMasterFileConverter` (`FUN_1406de7d0`, flags `0xf00000`). Duplicate-reference check walks cells + persistent-reference arrays.

**Confidence:** `strong` for the pipeline steps and strings. Converter flag bits `0xf00000` are `speculative`.

**Library impact:** Writer should eventually match CK save order (header then data, CreateTES, form post-process). Persistent vs temporary references are distinct arrays in CK. ESL files are rejected by Tagifier (`"ESL files are not supported"`). `-ConvertToESL` exists as bit `0x40000000` (`ESLConverter`) but was not decompiled in this pass.

### Cell size in CK map maker

**Source:** `CKRE/15-BATCH-PROCESSES.md` MapMaker  
**Address:** `FUN_1406bc9b0`

**Proves:** `world = (cell << 12) + worldOrigin` (4096-unit cells). Matches the runtime LAND finding.

**Confidence:** `confirmed`.

### Init / remap

**Source:** `CKRE/11-INIT-CHAIN.md`

**Proves:** CK allocates a 32 MB FormID remap table (`0x2000000`) and reads `[ReMapIDs]`. Archive init mentions `Fallout4 - Voices1.ba2`.

**Confidence:** `strong` that CK remaps IDs; table format `speculative`.

**Library impact:** FormID remapping is a first-class CK operation. Our FormKey model is the safe equivalent.

### Gaps in CKRE

TESFile record/chunk parser was named (`tesfilecontainer.cpp`) but the actual `NextForm` / `XXXX` / zlib path was not decompiled in the dated CKRE docs. BA2 GNRL/DX10 layout is not documented there. Archive2 is a companion tool, not dumped.

---

## ReverseEngineering (Fallout4.exe)

### TESFile API (highest value)

**Source:** `ReverseEngineering/Organized/1.10.163/Forms_TES.txt`  
**App:** Fallout4.exe OG 1.10.163  
**Addresses (image base `0x140000000`, RVAs below):**

| RVA | Name |
|-----|------|
| `0x12e4a0` | `TESFile::OpenTES` |
| `0x12df80` | `TESFile::CreateTES` |
| `0x12ed70` | `TESFile::LoadTESInfo` |
| `0x130f70` | `TESFile::NextForm` |
| `0x131730` | `TESFile::NextChunk` |
| `0x1317f0` | `TESFile::GetChunkData` |
| `0x48e40` | `TESFile::GetChunkSize` |
| `0x1306c0` / `0x130740` | `StartGroup` / `EndGroup` |
| `0x128db0` | `GetMasterList` |
| `0x12fa50` / `0x12fe00` | `AddMaster` / `RemoveMaster` |
| `0x12f7e0` | `GenIndexTable` |
| `0x131fa0` | `AdjustFormIDFileIndex` |
| `0x1304d0` / `0x1304e0` | `GetCompileIndex` / `SetCompileIndex` (`uchar`) |
| `0x12cf30` | `SetSmallCompileIndex` (`ushort`) |
| `0x131f60` / `0x131f80` | `SetSmallFile` / `GetSmallFile` |
| `0x130560` | `CheckValidIDForSmallFile` |
| `0x126ce0` / `0x126d40` | `TESFileCollection::AddNormalFile` / `AddSmallFile` |

**Proves:** The engine distinguishes normal vs small files, has a master list, builds an index table, and adjusts FormIDs. Chunk IO is the subrecord loop. `GetFormVersion` exists (`0x52970`).

**Confidence:** `confirmed` that these symbols/RVAs exist in the unpacked OG index and that the section-aware raw mapping reaches their bodies. `CheckValidIDForSmallFile` is confirmed as `(formID & 0x00FFF000) == 0`; the 10-byte body is `f7 c1 00 f0 ff 00 0f 94 c0 c3`. The reusable map/sweep is `Resources/RE-Work/tools/rebase_runtime_symbols.py`.

**Library impact:** This is the runtime specification for FormKey encode/decode and for the generic chunk reader.

### TESFile in-memory (CommonLib oracle)

**Source:** `CommonLibF4/include/RE/T/TESFile.h`  
**App:** Fallout4 (header reconstructed; `sizeof(TESFile)==0x3C0`)

**Proves:** `kSmallFile = 1 << 9`, `compileIndex` at `0x370`, `smallFileCompileIndex` at `0x372`, `decompressedFormBuffer` at `0x398`, masters as `BSSimpleList<char*>`, `IsFormInMod` encoding for full vs `0xFE` light.

**Confidence:** `strong` (layout used by working F4SE plugins; cross-check against OG not fully repeated here).

### Chunk Load() pattern

**Source:** `ReverseEngineering/07-DECOMPILE-CANDIDATES.md`  
**App:** Fallout4 OG 1.10.163

**Proves:** Typed `Load(TESFile*)` implementations loop TESChunks:

- `TESObjectMISC`: FULL / EDID / DEST / MODL / OBND / KSIZ / DODT
- `TESRegion`: RPLD / BODT / VMAD
- `TESPackage`: PTDA / PLD2 / ...
- `NavMesh`: NAVM / MNAM / VMAD / PSTC
- `TESLeveledList::InitComponent` resolves FormIDs via `GetFormByNumericID` / `AddCompileIndex`

**Confidence:** `strong`.

**Library impact:** Typed schema later. Generic reader already stores every chunk. `AddCompileIndex` is another name for runtime FormID remap.

### CELL / LAND

**Source:** `ReverseEngineering/Organized/1.10.163/README-Landscape.md`, `03-FINDINGS.md` 2026-07-23  
**App:** Fallout4 OG 1.10.163  
**Address:** `TESObjectLAND::GetWorldCellX` RVA `0x3a0730`; `LoadVerticesIntoArrays` RVA `0x3a09e0`

**Proves:** 4096-unit cells; VHGT `0x448` bytes; ×8 height scale; 33x33 grid; REFR positions are absolute.

**Confidence:** `confirmed`.

**Library impact:** Typed LAND/CELL later. Group nesting for WRLD/CELL is required now.

### TESQuest / TESNPC

**Source:** `03-FINDINGS.md` quest rounds 2026-07-11  
**App:** Fallout4 OG 1.10.163

**Proves:** Runtime quest stage bits, story manager, TESNPC package overrides. These are runtime semantics, not on-disk layouts.

**Confidence:** `confirmed` for runtime; on-disk QUST/NPC_ remain `speculative` here.

**Library impact:** Defer typed QUST/NPC_.

### BA2

No dedicated BA2 v7/v8 layout write-up was found in ReverseEngineering findings. CK init opens `Fallout4 - Voices1.ba2`. PluginEditTool already has `Ba2NextGenDecompressionTests` (including its version 7 cases) as a later oracle. `speculative` for this repo until that work is inventoried in a BA2 pass. Radicle now implements v7/v8 GNRL and DX10 and has measured their on-disk field widths on installed archives (`docs/BA2.md`); that is a file-format measurement, not RE evidence, so the RE status above is unchanged.

### Packed-exe caveat

**Source:** `ReverseEngineering/03-FINDINGS.md` 2026-07-07  

Anything derived from the packed `fwgh_proj` `.text` is unverified. Named TESFile RVAs above come from the unpacked index / CommonLib cross-ref, not from packed decompiles.

---

## FO4IDE regression cases to keep visible

From PluginEditTool tests (oracle, not copied):

- custom ActorValue CTDA FormLink uses destination MAST ordinal (`0x02000812`)
- master order must be the written master list, not a guessed load order
- BA2 v7 GNRL / DX10 and v8 GNRL / DX10 already have FO4IDE tests; Radicle now implements them and has its own synthetic and installed-data tests (`docs/BA2.md`)
- PERK Add Activate Choice, nullable enum, short/sbyte enum: FO4IDE history, not yet reproduced here

---

## Live binary samples (this pass)

**Source:** `/run/media/user/Games-Storage/SteamLibrary/steamapps/common/Fallout 4/Data/ccBGSFO4096-AS_Enclave.esl`  
**App:** Fallout 4 shipped plugin  
**Proves:** TES4 24-byte header, form version 131, flags `0x281`, HEDR version 1.0 / count 22 / next `0xFF0090C5`, MAST+DATA, INTV=13. Local ESL FormIDs use high byte `0x01` (self), overrides use high byte `0x00`. Parsed by `fo4inspect` in this repo.  
**Confidence:** `confirmed` for this file.  
**Library impact:** On-disk light records are plugin-relative, not FE-encoded.

**Source:** `testdata/FW_HeatherSurvivalPatch.esp`  
**Proves:** three-master ESP, form version 131, HEDR count = groups + major records (7 = 3 + 4).  
**Confidence:** `confirmed`.

## Deep audit follow-up (2026-08-19)

These came from the full CKRE / ReverseEngineering / testdata sweeps after the first inventory pass.

### TESFile internals (named, still not decompiled)

**Source:** `ReverseEngineering/Organized/1.10.163/Forms_TES.txt`  
**App:** Fallout4 OG 1.10.163 unpacked

| RVA | Function | Size |
|-----|----------|------|
| `0x12ed70` | `LoadTESInfo` | 1775 |
| `0x132740` | `ReadFormHeader` | 234 |
| `0x132830` | `ReadChunkHeader` | 242 |
| `0x130f70` | `NextForm` | 400 |
| `0x132500` | `DecompressCurrentForm` | 566 |
| `0x135cc0` | `IsCurrentFormCompressed` | 32 |
| `0x135990` | `GetFileVersion` | returns **float** (HEDR version) |
| `0x12f470` / `0x12f530` | `Get/SetTempOffsetForCell` | cell temp GRUP seek |
| `0x12f4d0` / `0x12f600` | `Get/SetInteriorOffsetForCell` | interior offset table |

**Confidence:** `confirmed` they exist. Bodies `probable` until decompiled.

**Library impact:** Compressed records are per-FORM, after the header, before the chunk walk. Persistent vs temporary CELL children are **seek tables on TESFile**, not a second coordinate space.

### CK zlib is confirmed

**Source:** CKRE `ghidra-dump/strings-asc.txt` (`tesfile.cpp` / `tesform.cpp`)  
**App:** CreationKit 1.11.137.0

Strings: `Error initializing ZLib inflate stream`, `Error inflating ZLib stream`, `ZLib stream did not terminate`, `Error deflating ZLib stream`, `Failed to read in buffer data for compressed form`.

**Confidence:** `confirmed` that CK load/save uses zlib inflate/deflate for compressed forms. Flag bit and uncompressed-size field still not dumped from CK.

`GetChunkData` also logs `Chunk size %u too big in chunk %s_ID in form %s_ID (%08X). Max size is %u, data truncated.` Engine truncates oversize chunks rather than aborting. Our reader currently errors; that policy difference is documented, not copied yet.

### Localization

**Source:** CKRE `bgslocalizedstring.cpp` strings  
**Proves:** `.STRINGS` / `.DLSTRINGS` / `.ILSTRINGS`. Delocalized files must store 4-byte string-table indices (`LOCALIZATION: Delocalized TESFile, but chunksize is not a BSUInt32.`).

**Confidence:** `confirmed` three-file scheme and 4-byte IDs. On-disk strings-file directory layout is still a gap.

### CELL REFR load gap

**Source:** ReverseEngineering Forms_TES export  
**Proves:** There is **no** named `TESObjectREFR::Load(TESFile*)` in the OG 1.10.163 export. Plugin REFR parse is almost certainly inside `TESObjectCELL::Load` (`0x3ad0e0`) / `LoadTempDataFromFile` (`0x3bd910`) / `ConstructObject`. Savegame REFR buffers are the wrong format.

**Confidence:** `strong`.

### CTDA / VMAD / versioned chunks

- `TESCondition::Load` `0x72aa30` and `LoadCompact` `0x72ab20` both exist.
- `TESForm::LoadVMData` `0x154230` (660B), VMAD seen on MISC/REGN/NAVM/story nodes.
- Obsolete layouts are first-class: `EFFECT_ITEM_PRE_22`, `ParamData_Untilv24`, `QUEST_STAGE_DATA_OLD`, `OLD_QUEST_DATA`.
- `TESNPC::Load` `0x5af310` is 6923 bytes (largest form loader noted).

**Confidence:** `confirmed` existence; chunk maps not extracted.

### Runtime vs plugin pipelines

`TESFile` + `TESForm::Load(TESFile*)` is plugin IO. `BGSLoadFormBuffer` / `LoadGame` is savegame. Many CommonLib vtable names swapped those. Do not merge them.

### Testdata oracles (not vendored)

Mutagen already has FO4 fixtures we should copy later, not invent:

- `PluginEditTool/tools/Mutagen/Mutagen.Bethesda.Testing/Files/Fallout4/LeveledItemsOverflow.esp` (`XXXX`)
- `ObjectModificationNoData.esp`, `SubgraphOutOfOrder.esp`
- xEdit `TES5Edit-dev-4.1.6/Core/Hardcoded/Fallout4.esp`

FO4IDE BA2 tests (`Ba2NextGenDecompressionTests`, including its version 7 cases) are the BA2 oracle. This RE tree has `BSResource::Archive2::BSALoader` (`0x1b740c0`) but no GNRL/DX10 header dump.

### Highest-value next decompiles

Must use `fwgh_proj_1.10.163_unpacked` only:

1. `LoadTESInfo`, `ReadFormHeader`, `ReadChunkHeader`, `NextForm`, `DecompressCurrentForm`
2. `AdjustFormIDFileIndex`, `CheckValidIDForSmallFile`, `GenIndexTable`
3. `TESDataHandler::ConstructObject` `0x118c50`
4. `TESObjectCELL::Load` / `LoadTempDataFromFile` (REFR children)
5. `TESNPC::Load`, `TESQuest::Load`, `TESObjectWEAP::Load`, `BGSMod::Attachment::Mod::Load`
6. `LoadVMData`, `TESCondition::Load` / `LoadCompact`

## What was not found

- A complete TESFile::NextChunk decompile covering `XXXX`
- A named CommonLib `kCompressed` enumerator
- On-disk FormID samples from a shipped ESL record body (header TES4 flags were sampled)
- CK `-ConvertToESL` implementation
- Localization string table (`.STRINGS`) format notes in either RE tree
- VMAD / CTDA binary layout notes in either RE tree (only chunk names)
