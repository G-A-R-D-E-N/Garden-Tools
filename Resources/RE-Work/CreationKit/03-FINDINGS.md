# CKRE — Findings Index

Dated entries, newest first. Facts land here first, then get expanded into dedicated docs.

---

## 2026-08-19 — `-ExportDialogue` decompiled: raw ESM-only input and human-readable Logs/DialogueExport reports

- The `0x2` dispatcher branch installs `DialogueExporter::vftable` at
  `0x142fb6430` and runs `Execute` at `0x14048e3e0` through the shared runner.
- `Execute` passes its `this+0x08` slot unchanged to `FUN_14005d7ce` →
  `0x14085cd50`, whose file-list walk compares the complete string against the
  ESM filename. It does not tokenize language or text-export-root arguments.
  The corrected grammar is therefore `-ExportDialogue:<ESMFilename>`; a
  three-token suffix fails the ESM lookup.
- The worker `FUN_140090741` → `0x1404acd00` emits the all-quests or
  quest-specific progress line, creates/uses `Logs/DialogueExport/DialogueExport`,
  and uses `dialogueExport%s%s.txt` / `dialogueExport%s%s%s.txt` templates.
  The substitutions are runtime quest/voice/context names and an internal `_`,
  not a language token. Output is a human-readable `.txt` dialogue report.
- This branch does not open `TextExport.txt`, consume `PathToTextExport`, or
  produce localized `.STRINGS`/`.DLSTRINGS`/`.ILSTRINGS` files; those belong to
  `-CompileTextExport`/`FUN_140916980`. Durable reconstruction:
  `ghidra-dump/CLI-DIALOGUE-EXPORTER-FUN_14048e3e0.txt`.

## 2026-08-19 — `FUN_140916980` decompiled: CompileTextExport emits three language-specific string files (docs 05/13)

- The `0x40` dispatcher branch calls `FUN_140916980(esm, language,
  textExportPath, backupTextExportPath)` directly. It returns true only when
  its internal failure flag remains clear; the dispatcher maps false/true to
  `0x6b650001`/`0x6b650000` under the normal batch-exception gate.
- Three output contexts are constructed by `FUN_14091aa30` with embedded
  directory/extension literals: `STRINGS`, `.STRINGS`, `.DLSTRINGS`, and
  `.ILSTRINGS`. The exact language-dependent paths are
  `Data\\STRINGS\\<ESM stem>_<Language>.STRINGS`,
  `Data\\STRINGS\\<ESM stem>_<Language>.DLSTRINGS`, and
  `Data\\STRINGS\\<ESM stem>_<Language>.ILSTRINGS`. Each context also stages
  `<final>_temp` and is finalized through `FUN_14091d790`.
- The ESM stem is cut at the first `.` (`Fallout4.esm` → `Fallout4`), while
  the language token is passed through verbatim. The third argument is an
  export root; the observed `%s/%s/%s` construction locates
  `<root>/<ESMFilename>/TextExport.txt`. The fourth argument is an additional
  backup/fallback root, not an output filename.
- The compiler produces Bethesda localized-string binaries and routes records
  into the ordinary, dialog, and miscellaneous string tables. Its diagnostics
  explicitly cover malformed lines, invalid/reserved IDs, duplicates, unreadable
  inputs, oversized files, and output-open failures.

## 2026-08-18 — Wine startup crash decoded: Papyrus source-zip extraction faults on a NULL shell `Folder*` (doc 20)

- Crash at RVA `0x63ad44` (`movq (%rcx),%rax`, RCX=0) inside `FUN_14063ab40` —
  the doc-17 `WritePrivateProfileStructA`/`[ZipInstall]` saver, now identified as
  the **Papyrus script-source zip extractor**.
- Chain: WinMain (`FUN_1406946a0` +0x1ca8) → Papyrus init `FUN_141123e20` →
  extraction loop `FUN_141129aa0` ("Failed to extract script source zip …") →
  `FUN_14063ab40`. Runs at startup before the editor window exists.
- Extraction mechanism: **Shell.Application COM** — `CoCreateInstance(CLSID_Shell
  13709620-c279-11ce-a49e-444553540000, IID_IShellDispatch d8f015c0-c278-11ce-
  a49e-444553540000)` → `NameSpace(zipPath)` → `Folder*` → `Items()` →
  `destFolder->CopyHere()`. Under Wine, `NameSpace` of a `.zip` returns **S_OK
  with a NULL Folder** (no zip-folder/zipfldr handler); the CK checks only the
  HRESULT sign, then derefs the NULL → page fault. A genuine CK latent bug.
- `[ZipInstall]` stamp gotcha: the expected 8-byte tuple is read from
  **uninitialized stack** (only written post-extraction from the COM result), so
  pre-seeding the ini is unreliable; the zip list is a fixed editor-object array
  (`+0x428` ← `+0x410`, copied by `FUN_141126280`), so removing the zips does
  not help.
- Workaround candidates: binary patch at `0x63ad44` (`48 8B 01 FF 50 58` →
  `31 C0 90 90 90 90`) fakes the item-fetch success and lets the stamp write
  run, skipping extraction on later launches (documented, untested); Wine has
  no shell zip-folder support for a cleaner fix.

## 2026-08-18 — Arg helpers decompiled: the quoted-token splitter and the platform validator (doc 13)

- `FUN_140490960` is an in-place `strtok`-style splitter with quote support:
  quoted tokens (start `"`) may contain spaces and end at the closing `"`;
  unquoted tokens are `isgraph` runs; every token is NUL-terminated in place and
  the continuation pointer is returned via the out-param. Used by
  `-GenerateSingleLip` and `-CompileTextExport` (paths with spaces).
- `FUN_1404905a0` is the generation-target validator: `XB1`/`X64` → 0, `W32` →
  1, `PS4` → 2 via the `FUN_1420d3870/80` setter/getter pair into `DAT_147322d04`;
  failure logs `"Unrecognized generation target '%s'. Should be XB1, X64, PS4 or
  W32."` and the branch skips the process. The platform enum feeds the FaceGen
  string encoders `FUN_1420d4410`/`FUN_1420d4840`, which pick per-platform binary
  format tables (`.data 0x328c0c0` XB1/X64, `0x328c0b8` W32, `0x328c118` PS4).
- Export-branch slot consumption fully mapped: FaceGen (slot idx 3 @
  `0x3d2d6dc`), Dismember (idx 25 @ `0x3d2ed34`), StaticCollections (idx 17 @
  `0x3d2e514`) use `strtok_s` + validator; CompileTextExport (idx 6 @
  `0x3d2d9e8`) mixes `strtok_s`×2 + splitter×2; GenerateSingleLip (idx 16 @
  `0x3d2e410`) uses the splitter only. Validator failure = no process, no raise.

## 2026-08-18 — Doc 05 reconciled with the 31-switch parser: full table, corrected grammars, one-switch-per-invocation

- Doc 05 (string-recovered batch surface, 14 switches) rewritten against doc 13's
  parser: all **31 switches** with bit values (`1 << idx`, table at `.data
  0x3b8c740`) and exact arg grammars; the ten previously-unknown head switches
  (idx 0–9: OptimizeMasterFile, ExportDialogue, GenerateLips, ExportFaceGenData,
  TagifyMasterfile, ExportText, CompileTextExport, DelocalizeMasterfile,
  SaveDefaultPlugin, CheckInPlugin) are marked ★.
- **One-switch-per-invocation** rule documented: the parser loop exits on the
  first prefix match (`while (!matched)`), so only the first `-Switch:` on a
  command line is honored.
- Grammar corrections verified from the dispatcher decompile: `-SaveDefaultPlugin`
  is `<PluginFilename> <MasterFilename>` **space-separated** (the usage string
  prints them concatenated, but strtok_s splits on spaces); `-GenerateAnimInfo`
  options are `--speed`/`--stance`/`--forceAll`; `-MapInfo`'s 3rd token is
  validated against the `.data 0x2fdb3e0` enum (`doors`=0, `markers`=1,
  `interiors`=2) — doc 13's earlier `[doors||all]` phrasing corrected; the
  `ICommandLineHandler` hypothesis replaced by the bitmask+dispatcher model.
- Old "next steps" in doc 05 are all resolved → cross-linked to docs 09/13/15/19.

## 2026-08-18 — The batch exit-code contract decoded: the gate is the `bThrowProcessCompleteExceptions:General` setting (doc 19)

- The raise gate `DAT_143b8c888` is the **bool value field of a
  `SettingT<INISettingCollection>` descriptor** at `.data 0x3b8c880` (vftable at
  +0x0, value at +0x8, key:section `"bThrowProcessCompleteExceptions:General"`
  at +0x10) — loaded from CreationKit.ini/Custom via the standard collection
  machinery. That is why no code writes it: zero write refs in the whole image
  (byte-level RIP-relative scan), 7 read sites only (4 dispatcher + 3 runner).
- Full raise census: dispatcher `FUN_1404870f0` raises at 0x4874d7/0x4875d9/
  0x48782a/0x488a44 (TextExporter post-check, CompileTextExport, MaterialConverter,
  CompressPSG), runner `FUN_1404906a0` at 0x4907f9/0x490880/0x490899
  (post-Execute, masterfile-open hard fail, shared raise). All gated on the
  setting, all continuable (`RaiseException(code, 0, 0, NULL)`).
- Exit-code matrix: 26 of 31 switch bits route through the runner (code decided
  by vtable[+8] Execute); 3 bits raise directly in the dispatcher (0x40
  CompileTextExport `(result ^ 1) + 0x6b650000`, 0x4000000 MaterialConverter,
  0x10000000 CompressPSG); 2 bits never raise (0x400 -Install elevation,
  0x10000 -GenerateSingleLip logs only). Success = `0x6b650000`, failure =
  `0x6b650001` everywhere.
- Termination path: no SEH scope covers any raise site (whole-binary census:
  10 SEH functions, filters check 0xC00000FD/0x406D1388/continue-all). The
  installed unhandled-exception filter `FUN_1406a3e60` writes
  `CreationKit_<ts>.dmp` (MiniDumpWriteDump 0x40) and returns
  EXCEPTION_CONTINUE_SEARCH → OS exit code = the sentinel (1800404992/4993;
  -2494562304/-2494562303 signed). Batch runs still create a window: WM_CREATE
  → dispatcher → nonzero → WM_CLOSE.

## 2026-08-18 — Elric SDK PDB acquired: 90,090 engine symbols confirm the CK decompiles (doc 18)

45. **The Elric 2016 package** (`~/Downloads/Elric+PDB/Elric`): the FO4
    NIF/texture optimizer (Elrich.exe + ElricInterop.dll + native BSCore/BSMain/
    BSGraphics/Ni* DLLs). `ElricInterop.pdb` (95 MB) = 90,090 unique mangled
    C++ symbols, extracted to `ghidra-dump/elric-pdb-symbols.txt`.
46. **Settings machinery names confirmed**: `INISettingCollection::ReadSetting` /
    `::WriteSetting` (our FUN_1425c7f90 / FUN_1425c7da0), `::Open`/`::Close`
    (the lazy ini-path marker slots), `Setting` base with `operator bool/float/
    unsigned int`, `SettingCollection<Setting>`/`SettingCollectionList` RTTI,
    and `SettingT<INISettingCollection>` with the type-tagged ctors
    `(char const*, bool/uint/int/float)` = doc 10's type letters. Source paths
    `exporterpublic\bsmain\setting.h` / `bscore\bstscattertable.h` = the CK's
    embedded `buildagent\...\code\bsmain\setting.h` lineage.
47. **The descriptor records ARE symbols**: the PDB names every
    `SettingT<INISettingCollection>` global — 34 in Elric, **26 of which appear
    in the CK's 1,641-key table with matching sections** (HAVOK/Havok/Cloth/
    CollisionQuery), the anonymous-namespace hashes mapping 1:1 to the CK's
    per-file sections (explains why both `[HAVOK]` and `[Havok]` exist).
48. **NIF pipeline**: `NiStream` load-side methods (InsertObject,
    GetNumberOfLinkIDs, GetObjectFromLinkID, LoadCString, Istr),
    `bhkNiTriStripsShape` vtable/RTTI (physics block base),
    `BSSubIndexTriShape` name-only (predates FO4 subindex usage). Editor-only
    CK code (PreVis, object windows, batch processes) is absent — those docs
    stay CK-exclusive. Caveat: 32-bit 2016 build confirms names/semantics, not
    addresses.

## 2026-08-19 — Elric SettingT globals mapped to CK record addresses (doc 18 §3a)

49. **35 of 37 Elric `SettingT<INISettingCollection>` symbols resolve to CK
    descriptor records**, each mapped to its `.data` record VA (doc 18 §3a
    table; machine dump `ghidra-dump/elric-setting-records.txt`). The draft's
    "26 of 34 survive" was an undercount: the VDB wildcard is 3 keys, eight
    `S`-suffixed 2016 statics survive **S-stripped** (`bUseUnsupportedCast`,
    `bUseWorldLock`, `fGoodPosCheckDepth`, `fGoodPosCastCheckDepth`,
    `fUnsupportCastLength`, `fUnsupportedGravMult`, `bFindContactPointsOnAdd`,
    `bUseCharacterRB`), the class-static members were missed, and
    `fMaxBoneSpeedBeforeDefaultStiffness` exists as a **bare key** (`@0x143c392b0`,
    no `:Section` → `[MAIN]`, default 50.0). Only 2 are truly absent (no
    record, no literal): `bhavokDebugS`, `bregisterAllVDBViewersS`.
50. **Descriptor census byte-verified**: scanning `.data` for 0x18-byte records
    whose +0x10 field points at a `key:Section` literal yields exactly **1,641
    records / 1,632 unique keys** across three vftables (0x142f8dce8 ×1229,
    0x142f959b0 ×405 = doc 10 §4's second `INISettingCollection` subclass,
    0x142f97230 ×7 `[Interface]`/`[General]` UI keys) — an independent
    confirmation of doc 10's 1,641 figure — **plus 25 no-section records**
    (`[MAIN]`) in the two collection families, and a separate ~5,100-record
    runtime game-value `Setting` table under 0x142f97230 that is *not* ini
    settings. Nine keys carry two descriptor instances (fMouseScale/fMinMouseScale:
    General, fMaxDistanceMoved:Pathfinding, sResourcePrefixList:Archive,
    strPluginsFileHeader:General, uMaterialCount:BudgetCaps,
    bSkipInitializationFlows/bSkipProgramFlows:MESSAGES,
    fTessFactorMaxDistanceScale:Display). All 35 Elric records share vft
    0x142f8dce8; embedded defaults decode to known FO4 values
    (fHavokWindMultiplier=1500, fMaxRandomDelay=-1, fCharControllerWarpDistSqr=
    6000000, fGoodPos*Depth=0.1, fStepHeightModifier=0.5, uNumSimSettleSteps=10,
    fMaxBoneSpeedBeforeDefaultStiffness=50.0) — and
    **fInAirFallingCharGravityMult = 1.85** (bits 0x3feccccd), not the 0.925
    commonly quoted in tuning guides.

## 2026-08-19 — CK NIF save chain labeled via Elric symbols: NiStream vftable + bhkNiTriStripsShape (doc 18 §4)

51. **`NiStream::vftable` = `0x142fa4780` byte-confirmed** — the FaceGen
    exporter writes it into a stack stream (`lea rax,[0x142fa4780]; mov
    [rsp+0x270],rax` at `0x140ab905e`). Slots are hot-patch `jmp` stubs:
    slot 6 (`0x140097e92`→`0x1425fe750`) = file-write wrapper `SaveStream(this,
    path)` (open mode 1/buffer 0x2800 via `0x140015348`→`0x1424207b0`, bind
    via `0x140007937`, write, close); slot 12 (`0x14006d2fa`→`0x142498a30`) =
    version validator (reads 0x80-byte header, searches `File Format`, clamps
    version 0x04020100..0x14020007 via globals 0x143c56a7c/0x143c56a80, endian
    flag = 1, user-version, BS-version bands; error code at +0x4e8, message
    buffer +0x4ec·0x104 written via `strcpy_s`; codes 2=Not a NIF file,
    3=version too old, 4=unknown version, 6=Endian mismatch); slot 13
    (`0x14000da62`→`0x142498e10`) = header writer — emits `Gamebryo File
    Format, Version 20.2.0.7\n` (0x14341a510), u32 version 0x14020007, endian
    byte 1, user-version 12, then block-type/string-table counts.
52. **SaveStream core = `0x142499070`**: three per-object passes over the
    object array (stream+0x320, count +0x328, iterators +0x3d8..+0x3e0)
    calling each object's vftable **slot 0xd8**, then **0xe0**, then **0x110**
    with `(object, stream)` — matching `bhkNiTriStripsShape`'s PDB-named
    `RegisterStreamables`/`SaveBinary`/`LinkObject` virtual quintet (the only
    names the 2016 PDB provides; `NiStream::SaveStream`/`AddObject` themselves
    are absent from Elric — save-side is CK-only).
53. **FaceGen exporter `0x140ab8a60` (= `tesnpc_editor.cpp`) call chain**: 5×
    `CreateDirectoryA` for `Data\Meshes\Actors\Character\FaceGenData\FaceGeom\`
    → sprintf `%s\%08X.NIF` (formID &0xffffff) → NiStream ctor `0x14007854c`
    (0x1c0 bytes) → each child added via **vftable slot 0x1e0** (= `AddObject`)
    → save-prep `0x140abc3b0` → stack NiStream vft 0x142fa4780 + output wire
    `0x140040403`→`0x142497150` → `call 0x140097e92` (slot-6 stub = the disk
    write). Also decodes the full `bhkNiTriStripsShape` class layout (48
    PDB symbols: streaming quintet + subpart/`Add` family + `EmptySubpart`
    static + `iCount`/`ms_RTTI`/RTTI chain + `AddObjectFunctor` in
    `?A0x7927943b` — distinct from `NiStream::AddObject`).

## 2026-08-19 — NiStream serialization byte spec from the address-accurate ElricInterop.pdb import (doc 18 §4e)

54. **Imported `ElricInterop.pdb` into Ghidra 12.1.3 against `ElricInterop.dll`**
    (PE32 C++/CLI, PDB Universal, symbol addrs at image base 0x10000000) and
    resolved the full `NiStream` save/load family as **named exports of
    `NiMain.dll`** (3,308 exports): every hot-patch `jmp` stub was resolved to
    its real body and IAT slots were mapped (`Write`/`Read` =
    NiSystem!NiBinaryStream 0x100ea9a8/0x100ea9ac, `NiMalloc`/`NiFree`, `PutS`/
    `GetLine`). Extracted the **exact byte-level NIF wire format**: CString =
    `u32 len` (no NUL) + bytes (NULL → 0; load `len<=0` → NULL); FixedString =
    `u32` pool index, `0xFFFFFFFF`=empty; LinkID = `u32`, `0xFFFFFFFF`=NULL;
    header = 31-byte `Gamebryo File Format, Version 20.2.0.7\n` + u32
    0x14020007 + u8 endian + u32 user-version 12 + u32 numRoots + u32 BS
    uiCurrVersion(=0x82) + 4×`(u8 len incl. NUL, bytes)`; then u16
    numBlockTypes + `(u32 len,bytes)` names + u16 typeIdx×numRoots (bit 0x8000
    = `IsStreamable`) + u32 sizes×numRoots (back-filled) + fixed-string pool
    (`u32 count, u32 maxLen, (u32 len,bytes)×`) + object groups + root
    `SaveBinary` payloads + `u32 linkCount` × u32 linkIDs. Why block-type
    counts/indices are u16: `SaveRTTI` collects type names through a
    `NiTStringPointerMap` with a u16 counter. Size-table count comes from
    header numRoots — no separate count field. Load side skips unloadable
    blocks via the size table; post-load runs `LinkObject`/`PostLinkObject`
    passes + global post-process funcs. Artifacts: `ghidra-dump/nimain-nistream-serial.asm`,
    `ghidra-dump/nimain-exports.txt`. Corrective note for #52: the PDB-absent
    `SaveStream`/`AddObject` **do exist** in the product — as NiMain.dll
    exports (32-bit 2016), byte-identical contract to the CK's 64-bit chain.

## 2026-08-18 — CreationKit.ini validator oracle shipped (tools/validate_ckini.py)

43. **`tools/validate_ckini.py`** replays the reader `FUN_1425c7f90` against the
    1,629-key descriptor table and the `FUN_1425c7210` type-letter switch
    (verified decompile): b/c/h/i/u = GetPrivateProfileIntA (garbage → 0),
    f = sscanf %%f (empty/garbage → default retained, no notify), s = raw
    string (never notifies), r/a = sscanf %%u lists with the engine quirk that a
    partial conversion still overwrites the record + fires notify, unknown
    letters fall into the uint default arm, and LANGUAGE-section keys read from
    `[<sLanguage value>]`. Case-insensitive Win32 matching.
44. **Verdicts**: READ / READ-AS-ZERO / STR-READ / READ-FAIL / UNKNOWN (dead
    key, or known key under the wrong section with its registered section
    named) / MISSING / TYPE9-AS-UINT; exit 0 PASS, 1 on any READ-FAIL,
    `--strict` also fails warnings; `--list-keys` reference card and `--stats`
    (letters b=530 f=554 i=252 s=152 u=141 across 89 sections — the shipped
    table never uses c/h/r/a). Samples: valid/invalid/extended fixtures;
    `samples/descriptors-extended.txt` adds c/h/r/a to exercise the full switch.

## 2026-08-18 — Full ini census: 5 files, every path mapped to its reader; generic WriteSetting found (doc 17)

39. **The CK touches exactly 5 ini files**: `CreationKit.ini`, `CreationKitCustom.ini`,
    `CreationKitPrefs.ini`, `ConstructionSetNetwork.ini`, `Loaders.ini`
    (`ConstructionSet.ini`/`SkyrimEditorCustom.ini` appear only in dialog strings).
    The 3 settings filenames are a literal cluster at `.rdata 0x2fd2998`/`0x2fd29b0`/`0x2fd29d0`,
    concatenated to the base-dir buffer `DAT_14742c2a0`.
40. **Path→reader map complete** (uncapped IAT census: WritePrivateProfileStringA
    43 sites/23 fns, GetPrivateProfileIntA 36/22, GetPrivateProfileStringA 22/11,
    StructA 1+1): CreationKit.ini/Custom → the `INISettingCollection` instance
    (`FUN_1402ccbf0`/`DAT_14742c170`, wired in WinMain 0x694a6e/0x694bd6), read via
    ReadSetting `FUN_1425c7f90` driven by ReadAll `FUN_1402cb660` (vftable slot 10);
    CreationKitPrefs.ini → `INIPrefSettingCollection` (`FUN_14033ab80`/`DAT_143f2f9e8`)
    + ~20 window/dialog readers; ConstructionSetNetwork.ini → `FUN_1406da3c0`/
    `FUN_1406a34f0`/`FUN_1406da920`; Loaders.ini → `FUN_1425feb30`.
41. **The collection vftables decoded** (`.rdata 0x2f8e088` + subclass `0x2f959d0`):
    slot 2 AddSetting, **slot 4 WriteSetting**, slot 5 ReadSetting, slot 6/7 ini-path
    markers (`FUN_1425c6660` sets `coll[0x110]=coll`; `FUN_1425c6670` clears), slot 10 ReadAll.
    The ini path is the inline buffer at `coll+8`; the `DAT_143c575f8` registry singleton
    is a path-less scatter table used only by register/notify/unregister.
42. **Generic writer found — doc 16's "no generic writer" claim corrected**:
    `FUN_1425c7da0` = `INISettingCollection::WriteSetting` (vftable slot 4), the exact
    mirror of the reader — type-letter switch formats the value (`%d`/`%f`/raw/`%u,%u,%u`/`%u,%u,%u,%u`),
    then `WritePrivateProfileStringA(section, key, value, coll+8)`. Dispatched only via
    the collection vftables; the earlier census missed it because the CheckRefs probe
    capped at 6 refs per IAT slot.

## 2026-08-18 — Settings write-back decoded: notify setter, scatter-table registry, ini-write census (doc 16)

34. **The write-back setter is `FUN_1425c6b90`** — a pure keyed dispatch: hash the
    SettingT record address (`FUN_141ffe210`, 256-entry table hash), walk the
    `bstscattertable.h` chain (`{key, callback, next}` nodes, 3 qwords), then fire
    `callback(record)`. It never touches the ini.
35. **Registry API family resolved** in the `FUN_1425c6xxx` block: register
    `0x25c6800` (insert + immediate notify-on-register; cb==0 → remove), unregister
    `0x25c69c0` (key+callback both must match), insert `0x25c75a0`, notify `0x25c6b90`,
    singleton `0x25c7920` over `DAT_143c575f8`. The SettingT per-type vtables at
    `.rdata 0x3a2a440–0x3a2a660` carry register/notify slots; two notify slots alias
    into the setter body at +0x3d/+0x62 (per-type variants).
36. **The reader ends with the notify**: every successful `FUN_1425c7f90` read
    calls `thunk_FUN_1425c6b90(rec)` — read → store → re-broadcast. Late registrants
    get the current value for free (notify-on-register + notify-after-read).
37. **Ini write census — no generic writer exists.** All `WritePrivateProfile*`
    call sites are exactly four savers: `FUN_140522260` (ObjectWindow layout →
    CreationKitPrefs.ini `[General]`), `FUN_14069f330` (window X/Y/Width/Height),
    `FUN_1406da3c0` (ConstructionSetNetwork.ini), `FUN_14063ab40`
    (WritePrivateProfileStructA). The `bKey:Section` settings are read-only
    defaults in this build; mutations propagate only through the in-memory registry.
38. **Notify callers are the whole editor**: WinMain `FUN_1406946a0`, CLI dispatcher
    `FUN_1404870f0` (6 calls), batch runner `FUN_1404906a0`, PreVis family
    (`FUN_14048e930`/`FUN_14048f920`), world gen `FUN_140d20d90`, and the settings
    dialogs (`FUN_1406f3cb0`, `FUN_142825590`, `FUN_1401a9f00` …).

## 2026-08-18 — Batch process internals decompiled; the -SaveDefaultPlugin masterfile pipeline (doc 15)

30. **Shared runner FUN_1404906a0 decoded**: loads the arg-buffer masterfile into
    `DAT_143d33938`, reloads-if-newer, then executes via **vtable[+8]** on the process
    object; batch success/failure = `RaiseException(0x6b650000 / 0x6b650001)`; always
    `DataHandler->SaveAll()` at the end. All five process vtables resolved in `.data`
    (ctor at slot 0, Execute at +8).
31. **MasterfileOptimizer (bit 0x1)** = dup-ref check (`FUN_14085cc40`, "Run duplicate
    reference check for all references in all cells?…") + COCInfo.dat report
    (`FUN_140866780`: "Select Form Component Export File", "Exporting %s '%s' (%08X).",
    cell scatter-table walk, type char '?') + **ESM conversion** (`FUN_140690240`:
    "Confirm ESM Conversion." / "optimized for in-game loading", `BSMasterFileConverter`
    engine `FUN_1406de7d0` flags 0xf00000 → df a70/e71a0/e01d0 → save).
32. **MasterfileTagifier (bit 0x10)** = `FUN_140923d00` via global config
    (`&PTR_vftable_143b9c0e0`, filename +0x10); tags forms with their string IDs
    (`FUN_1409242d0` main loop; "TAGIFICATION: Results: %u strings tagged…").
    **ESL files rejected**: "TAGIFICATION: FAILED - ESL files are not supported".
33. **MapMakerProcess (bit 0x200000)** = `-MapMaker:<ESM> <Worldspace|Interior>`;
    core `FUN_1406bc9b0` validates the cell rect then renders each cell's forms,
    cell→world = `(cell << 12) + origin` (4096-unit cells).
34. **CheckItInChappy (bit 0x200)** = VCS auto check-in (`FUN_14048d4e0`): VCS check-out
    per target file, NavMeshInfoMap check-out, `TESForm*→TESFile*` + form→uint NiTMaps,
    `"AUTO PLUGIN CHECKIN"` commit (`FUN_1406ead80`), per-form check-in
    (`FUN_1408c23e0`, type names from the doc-12 record table), worldspace offset-array
    + owned-ID-array updates.
35. **FileSaver (bit 0x100) = -SaveDefaultPlugin:<PluginFilename> <MasterFilename>**;
    Execute `FUN_14048eb10` = GetFile → unlink from handler list (`FUN_14085ce00`) →
    `FUN_140862120` SaveDefaultPlugin pipeline ("Begin Saving…", temp/backup file,
    header, data, post-processing, "SAVE COMPLETE.", optional PS4/XB1 push). All batch
    code lives in `construction set\misc\bgsautomatedprocesses.cpp`.

## 2026-08-18 — PreVis `.scene` writer decoded; real `.uvd` samples byte-parsed (doc 14)

27. **`.scene` writer fully decoded** (`scenewriter-*.log`): the document serializer
    `FUN_141815c90` (called via wrapper `FUN_14181ae70`) writes `u32 0xBECAFABE, u32 0xF,
    u32 6, count0, count0×int32`, then tag-terminated sections `u32 0; elementA×countA`
    (writer FUN_141813f80, dedup-indexed), `u32 1; elementB×countB` (writer FUN_141819d10,
    u64-key dedup), then `u32 4, u32 countC, elementC×countC` (writer FUN_141814120, flat
    ~11-u32 struct with two u64s written low-then-high). Doc-field map: A=+0x14/+0x08,
    B=+0x2c/+0x20, C=+0x44/+0x38, ints=+0x64/+0x58.
28. **`.scene` never ships** — scanned every archive on disk (base game Data, 384 modlist
    ba2s, PRP 2.3 GB, Rebuild Collection): zero `.scene`; the shipped runtime form is
    `.uvd` (`Vis\<plugin>\<formid8>.UVD`). Three real samples extracted from
    `Fallout4 - MeshesExtra.ba2` (966 total) — header `u32 0xD6000012` magic, per-file
    hash u32, then world-coordinate floats (const −7168.0; ±7168…−207872 extents); no
    0xBECAFABE anywhere inside. `.uvd` is a distinct format, not compiled `.scene`.
29. **`-GeneratePreVisData` wine findings**: batch grammar is space-separated
    (`Fallout4.esm clean all 1 2`); the comma form makes the CK abort with
    `Could not get file pointer for Fallout4.esm,clean,all,1,1, aborting automated
    process.` — real `.scene` generation still pending (CK batch under wine, on hold).

## 2026-08-18 — `.uvd` PreVis scene format fully decoded (21-UVD-FORMAT.md)

50. **`.uvd` compiler chain + byte layout (21-UVD-FORMAT.md)**: the writer is
    `FUN_14187b340` (0x150 header, magic `0xD6000012` at +0), reached via the
    tome builder `FUN_1418802d0` (version-string suffix ` - 3.3.17 F 1 0 OG 0`,
    engine build 3.3.17) from the export driver `FUN_14188e570`; the disk/cache
    writer is `FUN_141876f40` (`_depthmaps.umbracache`, shares framing). The `+4`
    field is **CRC-32C (Castagnoli `0x82F63B78`)** over `[8, 8+size)` — reproduced
    exactly on all three shipped samples. `+8` = total file size (byte-exact).
    Header: f32 tile size T=512 at +0x0c; flags at +0x10; world AABB six f32s at
    +0x14..+0x28; `nodeCount<<5|flags` at +0x2c; header size 0x150 at +0x30;
    nodeCount at +0x38/+0x90; tile stride 0x1c0 at +0x3c; target count at +0x40;
    payload offsets at +0x44/0x48/0x50; version string at +0xbc
    (`T 512.0 SO 128.0 SH 16.000 BF 100 F 0 CS 0.0 - 3.3.17 F 1 0 OG 0`); payload
    from +0x150, each tile 16-byte aligned. File path `Data\Vis\%s\%08X.uvd`
    (`.rdata 0x14309da60`) formatted by `FUN_140d276b0/140d27750/140d27800`.
    Doc 14 §3.2's older parse is corrected/superseded (the "+0x14 count 51208"
    was a misaligned AABB float; the `0x44000000` u32 is f32 512.0).

## 2026-08-18 — CLI token→bitmask parser located (FUN_140487010) + full 31-switch dispatcher map

25. **Parser found at RVA 0x487010** (`CLI-PARSER-FUN_140487010.txt`): WinMain calls it once
    with `lpCmdLine`; it walks the **31-entry** token table at `.data 0x3b8c740` (NOT the
    21-entry tail at 0x3b8c7d0 from entry 21 — that was only the last 21 of 31; the ten
    `-OptimizeMasterFile -ExportDialogue -GenerateLips -ExportFaceGenData -TagifyMasterfile
    -ExportText -CompileTextExport -DelocalizeMasterfile -SaveDefaultPlugin -CheckInPlugin`
    switches precede it). Case-insensitive **prefix** match via `_strnicmp`; first match wins
    (one switch per invocation); `1<<idx` OR-ed into `DAT_143d2f34c`; `-Switch:` remainder
    copied into 260-byte arg slot `DAT_143d2d3d0 + idx*0x104`. Any match sets batch mode
    (`DAT_143d3410b=1`, `DAT_147fda42c=0xffffffff`). No match + first token ends `.NIF` →
    NIF-preview mode instead of batch.
26. **Dispatcher full bit→process map extracted** (`CLI-DISPATCHER-FUN_1404870f0.txt`): all
    31 bits dispatch through the shared process runner 0x4906a0 to `_anon_F29367BE::*`
    process objects — `MasterfileOptimizer`(0x1) `DialogueExporter`(0x2) `LipGenerator`(0x4)
    `FaceGenDataExporter`(0x8) `MasterfileTagifier`(0x10) `MasterfileTextExporter`(0x20)
    text-export compile(0x40) `MasterfileDelocalizer`(0x80) `FileSaver`(0x100)
    `CheckItInChappy`(0x200) Install-elevation(0x400) `NeededFileDumper`(0x800)
    `DLCMasterfileOptimizer`(0x1000) `SEQExporter`(0x2000) `WarningGenerator`(0x4000)
    `AnimationBuildProcessDataExport`(0x8000, `--speed/--stance/--forceAll`) inline LIP(0x10000)
    `StaticCollectionProcess`(0x20000) `PreVisPreCombineDataProcess`(0x40000+0x80000)
    `DepersistRefsProcess`(0x100000) `MapMakerProcess`(0x200000) inline MapInfo(0x400000)
    `AreaArtOutput`(0x800000) `ModelDataUpdater`(0x1000000) `DismemberDataExporter`(0x2000000)
    `MaterialConverter`(0x4000000) `ImportScalingDataProcess`(0x8000000) `CompressPSGProcess`
    (0x10000000) `BuildCDX`(0x20000000) `ESLConverter`(0x40000000). Dispatcher runs from the
    main window's WM_CREATE path (wndproc 0x696e00); nonzero return → WM_CLOSE, so a batch run
    exits the editor. Full table: doc 13-CLI-PARSER.md.

---

## 2026-08-18 — Initial reconnaissance (binary identity + subsystem map)

**Target**: `CreationKit.exe` 1.11.137.0, 69 MB PE32+ x86-64, MSVC 2015+.
**Not packed** — `.text` entropy 5.86 (the game exe from the same era is DRM-encrypted at ~8.0).
Full MSVC RTTI present (4,438 unique type names).

1. **PDB path embedded**: `E:\BuildAgent\work\4a2aa37ca4b2ffa3\scripts\_workspace\Code\Build\PC\CreationKit.pdb`
   (GUID `f276f0de-3312-9745-8404-bd4b2df2a9aa`). TeamCity CI build → no public PDB, but the
   RSDS record is our fingerprint for version matching.
2. **Import surface** (24 DLLs): the editor is a full game-engine build (d3d9/d3d11/dxgi,
   Godrays/SSAO, X3DAudio/XInput) plus the editor extras: `SSCE5564.dll` (script editor),
   WINHTTP/WININET + steam_api64 (online), CRYPT32 `PFXImportCertStore` (cert store — used for
   the old "signed" plugin flow), dbghelp symbol server.
3. **Third-party SDKs embedded**: Autodesk FBX (170 RTTI classes), Alembic (31), Havok
   (animation/cloth/physics), NVIDIA Flex (NVFlex), Umbra, Bink, ANTLR 3 + Wintertree Sentry,
   Json, ATL.
4. **Editor architecture from RTTI**: `BGSRenderWindow` + edit modules
   (Landscape/NavMesh/Primitive/Reference, each with a Factory + Manager) + gizmo family
   (Translation/Rotation/Scale/Primitive). 126 `TES*` record classes, 337 `BGS*` classes.
5. **Masterfile pipeline** (next-gen update): `MasterfileOptimizer`, `DLCMasterfileOptimizer`,
   `MasterfileDelocalizer`, `MasterfileTagifier`, `MasterfileTextExporter`,
   `MasterFilePathingStream*`, `BSResource::CacheDrive` — plus a CLI
   `-SaveDefaultPlugin:<PluginFilename><MasterFilename>`.
6. **Online**: bethesda.net Creations (`<A HREF="%s/%s/mods/fallout4">`), upload/download,
   `AccountVPCRequiredLogin` (VPC = Virtual Possession Code), XB1/PS4 platform flags.
7. **`CreationKit.ini`**: 70+ keys documented in 01-CREATIONKIT-INI.md. Legacy console-era keys
   (X360/PS3 hot-loading, shader packages, `[Bethesda.net]` platform selection); `bAllowUnsafeSave`
   is the notable safety switch; `sMaterialEditorPage` points at a local HTML app
   (`Tools\MaterialEditor\htdocs\index.html`).
8. **Content pack GUIDs** (2021-02-28): Cloth/Animation/Physics/Behavior —
   `Bethesda_Fallout4_PS4-Windows-XboxOne` — the Creation Club packs are recognized by the CK.

## 2026-08-18 (cont.) — Settings DB, source tree, LOD pipeline, batch CLI (all string-recovered)

9. **Internal settings database**: 1,629 keys as `bKey:Section` literals (`bAllowUnsafeSave:General`,
   `fLodDistance:LOD` …). 424 in `General`, 104 `Display`, 95 `Pathfinding`, 51 `BudgetCaps`,
   48 `NavMeshGeneration`/`Landscape`, 38 `Workshop`/`HAVOK`, 34 `Water`/`Papyrus`, 20 `LOD`,
   17 `TerrainManager`/`Grass` … Full list in `ghidra-dump/ck-settings-keys.txt`, summary in
   06-SETTINGS-KEYS.md. The `Section` after `:` is the ini section — the whole editor ini schema.
10. **Embedded source tree**: 1,116 `__FILE__` paths rooted at
    `E:\BuildAgent\work\4a2aa37ca4b2ffa3\scripts\_workspace\code\` — modules `shared` (359),
    `gamebryogen9` (201), `bsshader` (99), `bshavokmigration` (86), `bspathfinding` (61), `bsmain` (61),
    `bscore` (44) … and **`construction set` (86)** — the CK itself: `tesmain.cpp` (entry),
    `bgsrenderwindow*.cpp`, `bgsobjectwindow.cpp`, `tesmapmaker.cpp`, `tespreferences.cpp`,
    `tesversioncontrol.cpp`, `bgsautomatedprocesses.cpp`, `bgsworldlodsettingsdialog.cpp`,
    `bgsdistantlodmeshdialog.cpp`, `pathfinding\navmeshgeneration*.cpp`, `script\bgscs*`,
    `tesforms\*_editor.cpp` (32 editor-side form forks), `visibility\bsprecomputedvisibility_editor.cpp`.
    Full list in `ghidra-dump/srcpaths.txt` + `ck-src-files.txt`, map in 07-SOURCE-TREE.md.
11. **CK has its own LOD pipeline**: `Building LOD for %s...`, per-world settings files
    `Data\LODSettings\%s.LOD`, water LOD (`Building LOD water for cell %i, %i. Water height is %f. Min land height is %f.`),
    object-LOD statics-only rule, atlas size caps, tree LOD textures `Data\Textures\Terrain\%s\Trees\%sTreeLOD.DDS`,
    `Generating LOD tiles using %d threads`, and a hardcoded `SanctuaryHillsWorld` special case.
    Doc: 08-CK-LOD-SUBSYSTEM.md.
12. **Batch CLI** (headless ops): `-CheckInPlugin`, `-DepersistRefs`, `-ExportDismemberData`,
    `-ExportFaceGenData`, `-GenerateAnimInfo`, `-GeneratePreCombined`, `-GeneratePreVisData`,
    `-GenerateStaticCollections`, `-ImportScalingData`, `-MapInfo`, `-MapMaker`, `-OutputAreaArt`,
    `-SaveDefaultPlugin` (masterfile), `-UpdateModelData` — platform targets `XB1|X64|PS4|W32`,
    chunked `[currentset]/[totalsetcount]` for build farming. Doc: 05-BATCH-CLI.md.
13. **Precombined/previs**: `Data\Meshes\PreCombined\%08X_*.NIF`, `Data\PreVis.esp`,
    `CombinedObjects.esp`; `ExtraPreVisRefs` extra-data; `PreVisPreCombineDataProcess`;
    import/export memory warnings (2 GB RAM / 5 GB disk); the previs occluder console command.
14. **Console commands**: `HotLoadPlugin` (hot-reload plugin while running), masterfile-close command.

15. **Archive/FlowChart**: BSA builder uses `*.achlist` file lists + `ArchiveInvalidation.txt` with version-gated repackaging; the quest flowchart is the commercial FlowChartX control (registered via `flowchartx64.dll`), settings section `[Flowchart]` (41 RTTI classes).
16. **Console commands** (with their embedded help strings): `HotLoadPlugin` (load/reload a plugin without restart; see also `ForceCloseFiles`), `ForceCloseFiles` (close masterfile & plugins so the CK can save a plugin loaded in-game), the `DumpPapyrus*` family (Stacks/Timers/EventRegistrations/LOSEvents/DistanceEvents/PersistenceInfo), a previs occluder command (`Hides all objects previs would not use as an occluder. 1-hide, 0-show, 2-partial`), formlist dump, and asset-map dumps (model map / sound map / texture palette).
## 2026-08-18 (ghidra phase) — analysis complete, entry + CLI dispatcher located

17. **Ghidra 12.1.2 auto-analysis completed** (~15 min for 69 MB / 49 MB `.text`). The RTTI
    analyzer recovered the full class graph natively: per-class namespaces with
    `vftable`, `vftable_meta_ptr`, `RTTI_Complete_Object_Locator`, `RTTI_Class_Hierarchy_Descriptor`,
    `RTTI_Base_Class_Array`, `RTTI_Base_Class_Descriptor_at_(n,v,o,flags)` symbols (e.g.
    `@BGSAIWorldLocation`). **No custom naming pass needed** — the analyzer beat the manual script.
18. **RTTI layout quirk (important)**: the 7,132 `type_info`/RTTI type-descriptor objects in
    `.data` (each `[0x1435595b0 vftable][0 spare][name]`) have **zero static pointers to them**
    in the file — a raw scan for 8-byte pointers to the `.?AVClass@@` name strings returns 0 hits
    (verified for VA and RVA encodings). The vftable→COL→descriptor graph edges are not stored as
    plain file pointers; ghidra recovered them by pattern-matching the RTTI structures. Any
    tooling that tries pointer-following on the raw file (e.g. a C++ RTTI dumper) will come up
    empty — use ghidra's RTTI output or pattern-scan for the `0x1435595b0` marker.
19. **Entry chain**: `WinMainCRTStartup @ 0x2d65aa0` → `__scrt_common_main @ 0x2d65ab8` →
    **WinMain @ 0x6946a0** (decompile saved to `ghidra-dump/decompile-WinMain-0x6946a0.log`).
    WinMain opens the four editor logs (`EditorWarnings.txt`, `EditorProgramFlow.txt`,
    `EditorProgrammerOutput.txt`, `EditorStackTrace.txt`), reads the `ReMapIDs` ini section,
    registers **14 window classes**, creates the main window `"Creation Kit"`, then runs a long
    subsystem-init chain.
20. **Batch CLI dispatcher @ 0x4870f0** (decompile saved to
    `ghidra-dump/decompile-CLI-dispatcher-0x4870f0.log`): the option descriptor table lives
    inline at +0x28b; the parse errors confirm all 14 `-Switch:` forms. The dispatcher bootstraps
    the **MasterfileOptimizer** singleton (`_anon_F29367BE::MasterfileOptimizer::vftable`) —
    matching the RTTI `MasterfileOptimizer@?A0xf29367be` classes — so the masterfile pipeline is
    the first thing the batch path initializes.

21. **PreCombined/PreVis export pipeline fully decompiled** (doc 09-PRECOMBINED-PREVIS.md).
    - The CLI token table has **31 switches** (see doc 13): the 21 listed here were the table's
      tail at .data 0x3b8c7d0; the full table starts at 0x3b8c740 with ten more
      (`-OptimizeMasterFile -ExportDialogue -GenerateLips -ExportFaceGenData -TagifyMasterfile
      -ExportText -CompileTextExport -DelocalizeMasterfile -SaveDefaultPlugin -CheckInPlugin`).
    - Bitmask dispatch: 0x40000 = PreCombined, 0x80000 = PreVis; shared runner 0x4906a0 calls
      `vftable[1]` = Execute.
    - `PreVisPreCombineDataProcess::Execute` = 0x48f920. Arg grammar `clean|filtered`,
      scope `all|main|ints|other` (0=both loops, 1/2=CELL loop only, 3=worldspace loop only),
      and **chunking**: `chunkSize = worldCount/totalsetcount; start=(currentset-1)*chunkSize;
      last chunk takes the remainder`.
    - PreCombined per-world 0xd20d90: `Data\Meshes\PreCombined\<FormID%08X>.NIF` +
      `<FormID%08X>_Physics.NIF` (Havok `hknpPhysicsSystemData`), refs via `ExtraCombinedRefs`,
      output `Data\CombinedObjects.esp`.
    - PreVis per-cell 0x1181210: `Data\Vis\Scenes\<Worldspace>_<Plugin>_%+02i_%+02i.scene` /
      `Data\Vis\Scenes\Interior_<Cell>.scene`, 3x3-cell-group worldspace bounds alignment
      requirement, output `Data\PreVis.esp`.
    - Settings keys adjacent to the token table: `bGeneratePreCombinedGeometryPreVis:General`,
      `bThrowProcessCompleteExceptions:General` (0x6b650000/1 exception protocol).


**Next steps** (project queryable now — `Query.java addr:0xRVA,decompile`):
1. Walk WinMain's init chain (FUN_140e84b60, FUN_140e84cf0, FUN_140943620 …) — label the
   subsystems in order.
2. Settings reader: xref `GetPrivateProfileStringA` from WinMain → the `bKey:Section` reader;
   cross-check the 1,629-key database against the ini doc.
3. `-GeneratePreCombined` handler → precombined pipeline; `-SaveDefaultPlugin` → masterfile flow.
4. The record table: find the record-definition dispatch (the 4-char record codes are binary data,
   not strings — locate the record-handler table via the `TES*_editor` classes' vtables).
5. Plugin save/load: `HEDR` string xrefs (12 occurrences) anchor the plugin writer.
21. **PreCombined/PreVis export pipeline fully decompiled** (doc 09-PRECOMBINED-PREVIS.md).
    - The CLI token table has **31 switches** (see doc 13): the 21 listed here were the table's
      tail at .data 0x3b8c7d0; the full table starts at 0x3b8c740 with ten more
      (`-OptimizeMasterFile -ExportDialogue -GenerateLips -ExportFaceGenData -TagifyMasterfile
      -ExportText -CompileTextExport -DelocalizeMasterfile -SaveDefaultPlugin -CheckInPlugin`).
    - Bitmask dispatch: 0x40000 = PreCombined, 0x80000 = PreVis; shared runner 0x4906a0 calls
      `vftable[1]` = Execute.
    - `PreVisPreCombineDataProcess::Execute` = 0x48f920. Arg grammar `clean|filtered`,
      scope `all|main|ints|other` (0=both loops, 1/2=CELL loop only, 3=worldspace loop only),
      and **chunking**: `chunkSize = worldCount/totalsetcount; start=(currentset-1)*chunkSize;
      last chunk takes the remainder`.
    - PreCombined per-world 0xd20d90: `Data\Meshes\PreCombined\<FormID%08X>.NIF` +
      `<FormID%08X>_Physics.NIF` (Havok `hknpPhysicsSystemData`), refs via `ExtraCombinedRefs`,
      output `Data\CombinedObjects.esp`.
    - PreVis per-cell 0x1181210: `Data\Vis\Scenes\<Worldspace>_<Plugin>_%+02i_%+02i.scene` /
      `Data\Vis\Scenes\Interior_<Cell>.scene`, 3x3-cell-group worldspace bounds alignment
      requirement, output `Data\PreVis.esp`.
    - Settings keys adjacent to the token table: `bGeneratePreCombinedGeometryPreVis:General`,
      `bThrowProcessCompleteExceptions:General` (0x6b650000/1 exception protocol).


**Next steps**: ghidra auto-analysis running (69 MB, 49 MB `.text`); RTTI naming pass first, then
targeted decompile: `tesmain` init → plugin load/save → record table → ini reader
(cross-check 01/06 docs) → render window module dispatch → `-GeneratePreCombined` handler.

## Settings reader (10-SETTINGS-READER.md)

- **Reader `FUN_1425c7f90`** = `INISettingCollection::ReadSetting` (vftable slot 5, thunk `0x2ddb7`): the ONLY code path that reads `bKey:Section` keys. Type letter = first char: b=bool, c=char, h=int16, i=int32, u=uint32, f=float, s/S=string, r=RGB, a=RGBA; section/key split at first `:`, default section `MAIN`; `sLanguage` value re-used as section when the requested section is `LANGUAGE`.
- **Descriptors**: 0x18-byte `{SettingT vftable, default, key_ptr}`; 1,641 real keys (each string has exactly one static pointer), ~360 per-subsystem arrays; every record registered by a per-descriptor static initializer (AddSetting via vftable[2], singleton `FUN_1425c7920` @ `DAT_143c575f8`).
- **Read-all proven** on the BudgetCaps collection (`FUN_14092b990`: pointer-table loop, 34 records); main collection uses the same slot-5 dispatch via the `.data` singleton (call sites not statically linkable — same pointer-scrubbing as the RTTI graph).
- Full GetPrivateProfile census: 21 sites — reader(4), Loaders.ini(2), Papyrus prefs dialog(5), app-config(10). None besides the reader touches `bKey:Section` keys.

## Init chain (11-INIT-CHAIN.md)

- **WinMain `FUN_1406946a0`** fully mapped: Phase 0 preflight (crash handlers, DataFiles.txt, COM, four editor logs, CreationKit.ini/Custom/Prefs settings loads, ReMapIDs, single-instance, Preview/NIF mode) → Phase 1 engine core (32 MB formID remap table, NiStaticDataManager, BSHavok + Havok stream, BSCullingProcess/DrawWorld, TESModelDB, BSResourceEntryCache, TES, BSStream/BSTFactory NIF node registration, shell file associations, 14 window classes) → Phase 2 windows (Creation Kit main HWND, BSGraphicsRenderer/RenderWindow, prefs window rect) → Phase 3 content (archive init + invalidation + .ba2 registrations, BSFaceGenManager, RNG seed, object windows loop, message pump with per-frame tick) → Phase 4 teardown mirror.
- **`FUN_140e84b60`** = Havok stream init (havokstream.cpp:0x205): BSHavok + BSHavokMigration boot, TLS-allocated HavokStreambufFactory replacing DAT_143e4fd10/143e4fd20.
- **`FUN_140e84cf0`** = comma/space-list override hook → BSTArray<{hash,flags}>; inert in the shipped binary (guard pointer's first byte is 0x00).
- **`FUN_140943620`** = Lipsync init (lipsynchanim.cpp:0x77d): singleton + two handler-table registrations (DAT_143d52514/52534).

### 12 — Record-definition dispatch table
- `formEnumString[]` (`.data` 0x3b9a540): 159-entry enum, runtime-validated by TESForm ctor (out-of-order/dup checks)
- file-load table (`.rdata` 0x300d478): 157 codes = enum minus NONE minus RACE (RACE not plugin-loadable)
- runtime factory array (`.data` 0x3d49cd0): 159 slots indexed by enum, setter/getter/visitor thunks at 0x13ef3/0x1694b
- RTTI `ConcreteObjectFormFactory<T,idx,size,flags>` names decode to 83 class bindings, zero mismatches
- full join: `record-definition-table.txt`; doc `12-RECORD-DEFINITION.md`
