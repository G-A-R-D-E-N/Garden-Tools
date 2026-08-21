# 18 · The Elric SDK PDB — ground truth for the shared engine layer

The Elric package (`/home/ciusr/Downloads/Elric+PDB/Elric`, May 2016 build) is
Bethesda's NIF/texture processing tool for Fallout 4 — the same engine family
as the Creation Kit. Its interop PDB carries **90,090 unique mangled C++
symbols**, which confirm (and correct) the names we assigned to the CK's
decompiled functions. This doc is the cross-reference.

## 1. What the package is

| file | identity |
|---|---|
| `Elrich.exe` | PE32 .NET launcher (GUI + CLI) — loads an `.esf` settings file, `-ElricOptions.*` overrides |
| `ElricInterop.dll` | PE32 .NET C++/CLI interop assembly — **`ElricInterop.pdb`, 95 MB = the symbol motherlode** |
| `ElricUtilities.dll` / `Elrich.pdb` (130 KB) | utility assembly / launcher symbols |
| `BSCore.dll` `BSMain.dll` `BSGraphics.dll` `BSShader.dll` `BSResource.dll` `BSSystem.dll` `ToolLib.dll` | native 32-bit engine DLLs (no PDBs shipped) |
| `NiMain.dll` `NiAnimation.dll` `NiCollision.dll` `NiParticle.dll` `NiStripify.dll` `NiOptimization.dll` `NiSystem.dll` | native NIF pipeline DLLs |
| `texconv.exe` | Microsoft DirectXTex wrapper (per README) |
| `Settings/PCMeshes.esf` + `PCFilter.cs` | the tool's own settings / shader-pick filter |

README's stated job: "process mesh files (NIF) and texture files (TGA,DDS)
into a more optimized form Fallout 4 will load … converts Havok data in an
exported NIF to a format Fallout 4 will be able to load."

**Caveat (important):** this is a **32-bit, May-2016 build**; the CK is
**64-bit, 2021**. The PDB confirms *names and semantics* of the shared engine,
never *addresses* — no RVA in this PDB maps into the CK.

## 2. Symbol inventory

Extracted: `ghidra-dump/elric-pdb-symbols.txt` (90,090 unique mangled names,
7.6 MB). Families relevant to the CK analysis:

- `bstscattertable.h` — `BSTScatterTableBase` / `BSTScatterTableKernel` /
  `BSTScatterTableTraits` / `BSTScatterTableEntry` / `BSTScatterTableDefaultHashPolicy` /
  `BSTScatterTableDefaultKVStorage` / `BSTScatterTableHeapAllocator` /
  `BSTScatterTableScrapAllocator` + `BSTSetBase`/`BSTSetTraits` — with full
  demangled template instantiations (e.g.
  `BSTScatterTableBase<Traits<bhkNPCollisionObject*, DamageImpactData*, ...>>::ClearEachImpl<HashMapDeleter<...>>`)
- `bsmain\setting.h` — `Setting`, `SettingT<INISettingCollection>`,
  `INISettingCollection`, `INISettingCollectionBase`, `SettingCollection<Setting>`,
  `SettingCollectionList<Setting>`
- NIF pipeline — `NiStream` (load side), `NiAVObject`/`NiNode`/`NiTriShape`/
  `NiTriStrips`, `bhkNiTriStripsShape`, `BSSubIndexTriShape` (name-only), Havok
  (`hkp*`, `hk*` migration/util wrappers)
- Build tree: `e:\projects\exporterpublic\bscore\bstscattertable.h` and
  `...\bsmain\setting.h` — the same files the CK embeds as
  `e:\buildagent\work\4a2aa37ca4b2ffa3\scripts\_workspace\code\bscore\bstscattertable.h`
  (doc 07).

**What is NOT here** (editor-only CK code): PreVis/PreCombine, object-window
layout, the batch processes (`MasterfileOptimizer` & co), the record-definition
table, the CLI parser. Those remain CK-exclusive findings (docs 09/12/13/15).

## 3. The settings machinery — decompile ↔ symbol map

| CK decompile (64-bit, docs 10/16/17) | Elric PDB symbol | verdict |
|---|---|---|
| `FUN_1425c7f90` = reader | `INISettingCollection::ReadSetting` | **confirmed** (name, bool-returning, SettingT& arg) |
| `FUN_1425c7da0` = generic writer | `INISettingCollection::WriteSetting` | **confirmed** |
| `FUN_1425c6b90` = notify dispatch | (registry machinery — scatter-table base) | consistent |
| vftable slot 6 = lazy ini-path marker `(coll,1)` | `INISettingCollection::Open(bool)` — `bool (bool)` | **exact** (the `1` arg is the flag) |
| vftable slot 7 = clear marker | `INISettingCollection::Close()` — `bool ()` | **exact** |
| vftable slot 10 = ReadAll `bool()` | (not in this PDB — Elric reads settings directly) | n/a |
| virtual set | `~INISettingCollection`, `Open(bool)`, `Close()`, `WriteSetting(Setting&)`, `ReadSetting(Setting&)`, `GetViewerStrings(BSTArray<BSStringT>&)` | all six present in the CK vftable; exact slot numbers differ between builds (the 2021 CK added virtuals), so match by signature, not slot index |
| type letters b/c/h/i/u/f/s/r/a (doc 10) | `SettingT<INISettingCollection>` ctors: `(char const*, bool)`, `(char const*, unsigned int)`, `(char const*, int)`, `(char const*, float)` | **confirmed** |
| per-record vftable `SettingT<INISettingCollection>::vftable` | `??_7?$SettingT@VINISettingCollection@@@@6B@` + `Setting` RTTI base (`??_R1A...Setting@@8`) | **confirmed** |
| descriptor records = globals | **the records themselves are symbols**: `?bAllowCharacterBumper@?A0x958245b0@@3V?$SettingT@VINISettingCollection@@@@A` | **confirmed — doc 10's record layout is literally the binary's symbol table** |
| `Setting::` value accessors | `Setting::operator bool/float/unsigned int(void) const` | confirmed |
| `SettingCollection` class family | `SettingCollection<Setting>` / `SettingCollectionList<Setting>` RTTI | confirmed |

### 3a. The settings — full symbol ↔ record-address correlation

The PDB names every `SettingT<INISettingCollection>` global the tool declares:
**37 symbols** (34 file-scope `@@3V` globals + 3 class-static `@@1V` members).
Correlating them against the CK's descriptor table by scanning `.data` for
records whose `+0x10` field points at a `key:Section` literal (or a bare key)
resolves **35 of the 37** — the two tables are the same codebase's settings,
five years apart. All 35 records are in `.data`, share the single vftable
`0x142f8dce8`, and decode to known FO4 defaults. Full machine table with
mangled names: `ghidra-dump/elric-setting-records.txt`.

| Elric PDB symbol (demangled) | CK key:section | record VA | `.data+` | default |
|---|---|---|---|---|
| `?A0x958245b0::bAllowCharacterBumper` | `bAllowCharacterBumper:HAVOK` | `0x143c39350` | +0xb2350 | true |
| `?A0x6c6a5b86::bDebugMultithreaded` | `bDebugMultithreaded:HAVOK` | `0x143c38c08` | +0xb1c08 | false |
| `?A0x6c6a5b86::bEnableContactCaching` | `bEnableContactCaching:HAVOK` | `0x143c38c38` | +0xb1c38 | true |
| `bEnableThreadSafetyChecks` | `bEnableThreadSafetyChecks:HAVOK` | `0x143c38bf0` | +0xb1bf0 | false |
| `bshowDisabledAsserts` | `bshowDisabledAsserts:HAVOK` | `0x143c39190` | +0xb2190 | false |
| `fHavokWindMultiplier` | `fHavokWindMultiplier:HAVOK` | `0x143c38bd8` | +0xb1bd8 | 1500.0 |
| `fMaxRandomDelay` | `fMaxRandomDelay:HAVOK` | `0x143c38c98` | +0xb1c98 | -1.0 |
| `fMaxWindActionTimingDifference` | `fMaxWindActionTimingDifference:HAVOK` | `0x143c38c80` | +0xb1c80 | 0.8 |
| `fStepHeightModifier` | `fStepHeightModifier:HAVOK` | `0x143c39398` | +0xb2398 | 0.5 |
| `?A0xe799298d::bUseCastFromOffset` | `bUseCastFromOffset:Havok` | `0x143c56168` | +0xcf168 | true |
| `?A0xb19ccac3::fInAirFallingCharGravityMult` | `fInAirFallingCharGravityMult:Havok` | `0x143c560f0` | +0xcf0f0 | 1.85 |
| `?A0xb8c65992::bUseCharRBExtrapolation` | `bUseCharRBExtrapolation:HAVOK` | `0x143c56268` | +0xcf268 | true |
| `BSClothUtils::bAnimClothLOD` | `bAnimClothLOD:Cloth` | `0x143c39238` | +0xb2238 | true |
| `BSClothUtils::bDrawClothDeformedBones` | `bDrawClothDeformedBones:Cloth` | `0x143c39208` | +0xb2208 | false |
| `BSClothUtils::bDrawParticleStepPositions` | `bDrawParticleStepPositions:Cloth` | `0x143c39220` | +0xb2220 | false |
| `BSClothUtils::bEnablePrediction` | `bEnablePrediction:Cloth` | `0x143c39250` | +0xb2250 | true |
| `BSClothUtils::bEnableScaling` | `bEnableScaling:Cloth` | `0x143c39268` | +0xb2268 | true |
| `BSClothUtils::bLoadLocalSetupData` | `bLoadLocalSetupData:Cloth` | `0x143c391f0` | +0xb21f0 | false |
| `BSClothUtils::fMaxRootAngleBeforeTeleport` | `fMaxRootAngleBeforeTeleport:Cloth` | `0x143c39298` | +0xb2298 | π/2 |
| `BSClothUtils::fMaxRootDistanceBeforeTeleport` | `fMaxRootDistanceBeforeTeleport:Cloth` | `0x143c39280` | +0xb2280 | 100.0 |
| `BSClothUtils::fMaxBoneSpeedBeforeDefaultStiffness` ‡ | `fMaxBoneSpeedBeforeDefaultStiffness` (bare → `[MAIN]`) | `0x143c392b0` | +0xb22b0 | 50.0 |
| `BSClothJobs::uNumSimSettleSteps` | `uNumSimSettleSteps:Cloth` | `0x143c38c68` | +0xb1c68 | 10 (u32) |
| `?A0xed07f71e::bDrawVDBClosestResults` | `bDrawVDBClosestResults:CollisionQuery` | `0x143c39428` | +0xb2428 | false |
| `?A0xed07f71e::bDrawVDBContactResults` | `bDrawVDBContactResults:CollisionQuery` | `0x143c39410` | +0xb2410 | false |
| `?A0xed07f71e::bDrawVDBRaycastResults` | `bDrawVDBRaycastResults:CollisionQuery` | `0x143c393f8` | +0xb23f8 | true |
| `?A0xed07f71e::bForceToPPU` | `bForceToPPU:CollisionQuery` | `0x143c393e0` | +0xb23e0 | true |
| `bhkWorld::bFindContactPointsOnAddS` † | `bFindContactPointsOnAdd:HAVOK` | `0x143c38bc0` | +0xb1bc0 | false |
| `bhkCharacterController::bUseCharacterRBS` † | `bUseCharacterRB:HAVOK` | `0x143c39338` | +0xb2338 | true |
| `bhkCharacterController::fCharControllerWarpDistSqr` † | `fCharControllerWarpDistSqr:HAVOK` | `0x143c39320` | +0xb2320 | 6000000.0 |
| `?A0x958245b0::fGoodPosCastCheckDepthS` † | `fGoodPosCastCheckDepth:HAVOK` | `0x143c39380` | +0xb2380 | 0.1 |
| `?A0x958245b0::fGoodPosCheckDepthS` † | `fGoodPosCheckDepth:HAVOK` | `0x143c39368` | +0xb2368 | 0.1 |
| `?A0x394e5770::bUseUnsupportedCastS` † | `bUseUnsupportedCast:HAVOK` | `0x143c56110` | +0xcf110 | true |
| `?A0x6c6a5b86::bUseWorldLockS` † | `bUseWorldLock:HAVOK` | `0x143c38c20` | +0xb1c20 | true |
| `?A0x394e5770::fUnsupportCastLengthS` † | `fUnsupportCastLength:HAVOK` | `0x143c56140` | +0xcf140 | 1.0 |
| `?A0x394e5770::fUnsupportedGravMultS` † | `fUnsupportedGravMult:HAVOK` | `0x143c56128` | +0xcf128 | 4.0 |

† = the trailing `S` of the 2016 symbol is stripped in the 2021 CK — the key
survives (with the same section/target) as the non-static variant.

‡ = bare key, no `:Section` suffix — at read time `GetSection` defaults it to
`[MAIN]` (doc 10 §1). All 25 no-section records in the two `INISettingCollection`
vft families are engine settings like this one (companion-follow weights,
landscape wetness-control overrides, rain splash tuning, `sControlsRemapFile`,
`fSkyCellRefFadeDistance` in the second-family vft); the unrelated ~5,100-
record game-value table under vft `0x142f97230` (see census below) is a
different `Setting` family entirely.

**Truly dropped by 2021** (no descriptor record, no key literal anywhere in the
binary — verified byte-wise): `bhavokDebugS` and
`?A0x7dd4a675::bregisterAllVDBViewersS`. That's **2 of 37**.

The earlier draft of this section claimed "26 of 34 survive, 9 dropped" — that
undercounted the VDB wildcard (3 keys), missed the eight `S`-suffix renames
(marked †), missed the class-static members (the three `@@1V` `bhkWorld` /
`bhkCharacterController` symbols), and wrongly listed
`fMaxBoneSpeedBeforeDefaultStiffness` (and the six `S`-stripped keys) as
dropped. The real numbers: **35 of 37 symbols resolve; 2 are absent**.

Two bonuses from the same scan:

- The **descriptor census is now byte-verified**: 1,641 `key:Section`
  descriptor records in `.data` (1,632 unique keys — **9 keys appear twice**,
  e.g. `fMouseScale:General`, `sResourcePrefixList:Archive`,
  `strPluginsFileHeader:General`, each with two descriptor instances), spread
  across three vftables: `0x142f8dce8` ×1229, `0x142f959b0` ×405 (the second
  `INISettingCollection` subclass from doc 10 §4), `0x142f97230` ×7 (the 7 =
  `[Interface]`/`[General]` UI keys `fDoorMarkerDisplayHeight`,
  `fQuestMarkerZDiffThreshold`, `fDoorMarkerHeightDiffLimit`,
  `iObjectivesWaitTime`, `iQuestWaitTime`, `uPerkVaultBoyCooldownTimeMS`,
  `fPowerArmorWorkbenchMaxPowerArmorDistance`). This independently confirms doc
  10's "1,641 descriptors" figure. **Plus 25 no-section records** in the
  `0x142f8dce8` (×24) / `0x142f959b0` (×1) families (default `[MAIN]`),
  `fMaxBoneSpeedBeforeDefaultStiffness` among them — and a separate ~5,100-
  record runtime game-value `Setting` table under `0x142f97230` that is *not*
  ini settings (its `sYes`/`fAI*`/`fActor*` bare keys are game values, not
  `bKey:Section` literals). All 35 Elric records use `0x142f8dce8`.
- The `.data+0xb1xxx` / `.data+0xcf1xx` offsets cluster the records into the
  per-TU static arrays exactly as the anonymous-namespace hashes predict:
  `?A0x6c6a5b86` + `bhkWorld` + the `fHavokWindMultiplier`-family in one
  0x18-stride run (`+0xb1bc0..+0xb1c98`, one non-SettingT object interleaved),
  the contiguous 9-record `BSClothUtils` array at `+0xb21f0..+0xb22c8` (its
  bare `fMaxBoneSpeedBeforeDefaultStiffness` closes the array), the
  `?A0x958245b0` character-controller array at `+0xb2320..+0xb2398` and the
  `?A0xed07f71e` collision-query array at `+0xb23e0..+0xb2428` (adjacent,
  small gaps), and the `?A0x394e5770`/`?A0xe799298d`/`?A0xb8c65992` Havok-cast
  TU in the `+0xcf0f0..+0xcf268` run.

Side observation: the anonymous-namespace hashes in the mangled names
(`?A0x958245b0`, `?A0x6c6a5b86`, `?A0xed07f71e`) are per-translation-unit — the
CK's settings land in `[HAVOK]`/`[Havok]`, `[Cloth]`, `[CollisionQuery]`
exactly per file, which is why the CK's `[HAVOK]` and `[Havok]` sections both
exist (two TUs) — and `uNumSimSettleSteps`'s record sits with the `?A0x6c6a5b86`
run because that symbol's TU (`BSClothJobs`) is a different file than
`BSClothUtils`, even though both register `[Cloth]` sections.

## 4. The NIF pipeline (companion to the combined-mesh writer work)

The PDB names both the physics-geometry class (`bhkNiTriStripsShape`, full
inventory below) and the primitive save/load templates NiStream uses; the
CK's 64-bit save chain is then verifiable byte-for-byte against those names.

### 4a. Elric PDB — what it names (and what it doesn't)

- `NiStream` — **load-side only** in the May-2016 PDB: `InsertObject`,
  `GetNumberOfLinkIDs`, `GetObjectFromLinkID`, `LoadCString(AsFixedString)`,
  `Istr()` (binary-stream accessor), plus the per-type primitives
  `NiStreamLoadBinary<T>` / **`NiStreamSaveBinary<T>`** — the generic
  `stream << T`/`stream >> T` byte writers for every scalar and STL-ish array
  type (`ABI C/D/E/G/H/I/K/M/_N`, array + size variants, `hkArray`,
  `NiTFlags`, `hkQuaternionf`, `hkTransformf` …). **`NiStream::SaveStream` and
  `NiStream::AddObject` are absent from the interop** (Elric never saves) — but
  §4e finds the full save/load family (incl. `SaveStream`, `SaveCString`,
  `SaveLinkID`, header/object-size-table writers) as **named exports of
  `NiMain.dll`**, whose x86-2016 bodies are the exact byte-level contract the
  CK's 64-bit chain re-implements (address-accurate via the PDB import; see §4e).
- `bhkNiTriStripsShape` — **48 symbols, the complete class** (see 4d). The
  Havok geometry block base for the CK's `_Physics` NIFs, and the shape whose
  `SaveBinary`/`RegisterStreamables`/`WriteCInfo`/`LoadBinary`/`LinkObject`
  virtual quintet is exactly the per-block contract the CK's writer dispatch
  (vftable slot 0xd8 in the save loop, §4b) calls.
- `AddObjectFunctor` in anonymous namespace `?A0x7927943b` (ctor
  `(bhkNiTriStripsShape&, int, BSFixedString const&)`, `operator()(NiAVObject*)`,
  `IsSubGeometryName`) — an internal fixture used when walking a scene graph
  to add sub-shape objects; the only "AddObject" name in the PDB, and it is
  **not** `NiStream::AddObject`. Keep the two apart when reading the CK.
- `BSSubIndexTriShape` — **name-only** (4 hits, all the
  `bhk*ConstraintMotor::IsSubIndexTriShape()` type-check virtuals). The FO4
  subindex geometry classes post-date this build; the CK's 64-bit BTR writer
  (LODGen work) has no symbol counterpart here.

### 4b. The CK's `NiStream` vftable (byte-confirmed)

**`0x142fa4780` is `NiStream::vftable`** — proven by the FaceGen exporter
(§4c) writing it into a stack-constructed stream object (`lea rax,[0x142fa4780];
mov [rsp+0x270],rax` at `0x140ab905e`). x64 hot-patch stubs: each slot is a
5-byte `jmp` in the `0x1400xxxxx` stub bank; the real bodies:

| slot | stub | → body | role (observed) |
|---|---|---|---|
| 6 | `0x140097e92` | `0x1425fe750` | **file write wrapper** — `SaveStream(this, path)`: opens the file (mode 1, buffer 0x2800, via `0x140015348`→`0x1424207b0`), binds stream→file (`0x140007937`), writes, closes, returns `bool` |
| 12 | `0x14006d2fa` | `0x142498a30` | **version validator** — reads 0x80-byte header, searches `File Format`, reads version u32 into `+0x200`, clamps min `0x04020100` / max `0x14020007` (`0x143c56a7c`/`0x143c56a80`), endian flag = 1, user-version, BS-version ranges; on failure sets error code `+0x4e8` + message `+0x4ec·0x104` via `strcpy_s` |
| 13 | `0x14000da62` | `0x142498e10` | **header writer** — `WriteString("Gamebryo File Format, Version 20.2.0.7\n")` (literal `0x14341a510`), then u32 version `0x14020007`, endian byte 1, user-version u32 `0x143c56a84`=12, `+0x32c` field, then block-type / string-table counts from the `+0xc/+0x4c/+0x8c/+0xcc` runs |

Version globals: `0x143c56a7c` = `0x04020100` (4.2.1.0, min), `0x143c56a80` =
`0x14020007` (20.2.0.7, the version the CK writes), `0x143c56a84` = `0x0c`
(user version 12). Validator error codes: 2 = `Not a NIF file`, 3 =
`NIF version is too old.` / `NIF user defined version is too old.` /
`NIF BSversion is too old`, 4 = `Unknown NIF version.` / `Unknown NIF user
defined version.` / `NIF BSversion is newer than is currently supported!`,
6 = `Endian mismatch.` — i.e. **code 2–4 = header/format errors, 6 = byte
order**, the same family as the load error contract in doc 14's UVD reader.

The object-save driver (`0x142499070`, the fragment after the header writer)
is the actual SaveStream core: three per-object passes over the object array
(`stream+0x320` array, count `+0x328`, counters `+0x3d8..+0x3e0`) calling each
object's **vftable slot 0xd8** then **slot 0xe0** then **slot 0x110** with
`(object, stream)` — reproduce the NiObject virtual quintet
`RegisterStreamables`/`SaveBinary`/`LinkObject` from §4a; the passes match
name-tracking, binary write, and post-link fixup respectively (slot numbers
are CK-specific, 2021 build).

### 4c. FaceGen exporter — the save call chain (`0x140ab8a60`, `tesnpc_editor.cpp`)

Disassembly-verified, top to bottom (RVA trail in parens):

1. Path build: five sequential `CreateDirectoryA` imports (`0x148233b40`) for
   `Data\Meshes\Actors\Character\` (`0x143039498`) → `...\FaceGenData\`
   (`0x1430394c0`) → `...\FaceGeom\` (`0x1430394f8`), then sprintf the pair
   `FaceGeom\%s` (`0x143039538`) + `%s\%08X.NIF` (`0x143039578`) — form ID
   `&0xffffff` of the NPC (`r13+0x14`). Assert strings `0x143039458`
   ("Invalid owner file during export of face geometry.") and `0x143039400`
   ("Invalid bone '%s' on NPC '%s' (%08X).").
2. Stream object: ctor `0x14007854c` (0x1c0 bytes, `0x140ab8d09`); every
   child added via **`NiStream::vftable slot 0x1e0`** (`0x140ab8e71`) = the
   PDB-implied `AddObject` — each `(object, true)` register call.
3. Save-prep `0x140abc3b0` (stream, temp-string).
4. Stack NiStream with vftable `0x142fa4780` (`0x140ab905e`);
   `0x140040403`→`0x142497150` wires the 0x1c0 stream as its output.
5. **`call 0x140097e92`** (`0x140ab909d`) — the vftable slot-6 stub = the
   file-write path (§4b): `SaveStream(file, FaceGenData\FaceGeom\<NPC>\<formid>.NIF)`.
6. Teardown `0x14004d3dd` / `0x140095c19`.

So the CK's combined-mesh / FaceGen NIF writers funnel every file through the
same three points — **vftable `0x142fa4780`, slot-6 stub `0x140097e92`, body
`0x1425fe750`** — and those names are the only pin the 2016 PDB cannot give
(save-side, 64-bit, 2021).

### 4d. `bhkNiTriStripsShape` — the confirmed class layout (PDB, 32-bit 2016)

48 symbols. Virtual quintet driving the §4b save loop, plus the full RTTI /
cloning / streaming inventory:

- **streaming**: `SaveBinary(NiStream&)`, `WriteCInfo(NiStream&)` (returns
  bool), `RegisterStreamables(NiStream&)`, `LoadBinary(NiStream&)`,
  `LinkObject(NiStream&)`, `LoadCInfo(NiStream&)→int`
- **subparts**: `Add(NiAVObject*, int)→bool`, `Add(NiTriShape*, int)`,
  `Add(NiTriStripsData*, int)`, `Add(NiTriStrips*, int)`, `AddSubpart(hkNiTriStripsData const&)`,
  `GetNumSubparts()→int`, `GetSubpart(uint)→hkNiTriStripsData const&`,
  `GetSubpartAt(int)→hkNiTriStripsData&`, plus **static `EmptySubpart`
  member** (`hkNiTriStripsData`) and per-subpart `QSubMaterial(uint)`
- **cloning/UI**: ctor/dtor, `CopyMembers(bhkSerializable&, NiCloningProcess&)`,
  `CanShare(NiCloningProcess&)→bool`, `CreateClone`, `CreateCinfo`/`CopyCinfo`/
  `KillCinfo`/`GetCinfo`/`Init(hkSerializableCinfo const&)`, `IsEqual(NiObject*)`,
  `QCinfoSize()`, `GetScale()→float`, `Gethk()/GetRTTI()/GetViewerStrings`,
  `BuildDisplayGeometry(NiNode*, NiColor const&)`, `CreateObject()` static,
  `CreateNS_bhkNiTriStripsShape::Create`, `?A0x7927943b::AddObjectFunctor`
  (see §4a)
- **metadata**: `iCount` (static instance counter `uint`), `ms_RTTI` (static
  `NiRTTI`), RTTI chain `R0/R1A/R2/R3` + vftable `??_7` + scalar/vector
  deleting dtor `??_E/??_G`, and the `bhkNiTriStripsShape` literal.

Every one of these names is load- or both-side; the *geometry-buffer* write
itself (`WriteGeometry`-equivalent, output-array fill) is the CK-only,
no-symbol part this PDB cannot cover — the exact place the 64-bit
`SaveBinary` body in the save loop implements FO4's `_Physics` NIF layout.

### 4e. `NiStream` serialization — the exact byte-level wire spec (32-bit 2016, address-accurate)

`ElricInterop.pdb` was imported into Ghidra 12.1.3 against `ElricInterop.dll`
(PE32, mixed-mode; PDB Universal applied → real addresses at image base
`0x10000000`). As known, `SaveStream`/`AddObject` do **not** live in the
interop — they are virtuals of `NiStream` in **`NiMain.dll`**, and that DLL
**exports the whole serialization family by name** (3,308 exports). Resolving
the hot-patch `jmp` stubs to their real bodies plus the NiStream vftable gives
the complete byte-level format, product-side engine truth (this is the same
engine generation the CK drives). The disassembly windows are staged verbatim:
`ghidra-dump/nimain-nistream-serial.asm`; full symbol table by address:
`ghidra-dump/nimain-exports.txt`.

**I/O primitives (NiSystem), used everywhere:** `Write(const void*,uint)` /
`Read(void*,uint)` = IAT `0x100ea9a8`/`0x100ea9ac`; `NiMalloc`/`NiFree` =
`0x100ea9b4`/`0x100ea9b0`; `PutS(const char*)` = `0x100ea980` (raw line,
used for the header); `GetLine(char*,uint)` = `0x100ea97c` (loads the 0x80-byte
header). The four scalar/string encodings, disassembly-verified:

| type | save (body @RVA) | load | wire format (all little-endian) |
|---|---|---|---|
| **CString** | `SaveCString` → body `0x7b360` | `LoadCString` → `0x7b2f0` | `u32 len` (strlen, **no NUL**) + `len` bytes. NULL string → `len 0` only. Loader: `len<=0` → pointer **NULL** (not empty); else alloc `len+1`, read `len`, NUL-term in memory only |
| **FixedString** | `SaveFixedString` → `0x7b420` | `LoadFixedString` → `0x7b3c0` | `u32 idx` into the per-file fixed-string pool (`stream+0x344` array). `0xFFFFFFFF` (== `NULL_LINKID`) → empty string. Not-yet-registered strings are added via `RegisterFixedString` (`0x7b5b0`) = urgent table hash-scan + `BSTArrayBase::AddUninitialized` |
| **LinkID** | `SaveLinkID` → `0x17c20` | `ReadLinkID` → `0x7b740`/`ResolveLinkID` `0x7b890` | `u32` = `GetLinkIDFromObject(obj)` (vft `+0x28`, body `0x7b710`). `0xFFFFFFFF` = NULL. Reader pushes live ids onto a pending array (`BSTArray::AddUninitialized` on the per-stream stack at TLS+0x108) for late resolution |
| **header line** | via `PutS` | `GetLine(buf, 0x80)` | `Gamebryo File Format, Version 20.2.0.7\n` (31 bytes, no length prefix) |

**`NiStream` vftable `0x100a8a5c` (`??_7NiStream@@6B@`) — 23 slots, stub → body:**
`+0x00` dtor `0x6a9b`; `+0x04` `Load(NiBinaryStream)` `0x4d6d→0x7b1c0`;
`+0x08` `Load(char*,uint)` `0x150a`; `+0x0c` `Load(const char*)` `0x67bc`;
`+0x10` `Save(NiBinaryStream)` `0x2e23→0x7b210`; `+0x14` `Save(char**&,uint&)` `0x43b3`;
`+0x18` `Save(const char*)` `0x52f9`; `+0x1c` `RegisterFixedString` `0x2bad→0x7b5b0`;
`+0x20` `RegisterSaveObject` `0x6f05→0x7b610`; `+0x24` `ChangeObject` `0x4ce1→0x7b6c0`;
`+0x28` `GetLinkIDFromObject` `0x1c62→0x7b710`; `+0x2c` `SaveLinkID` `0x2bfd`;
`+0x30` `LoadHeader` `0x4e8a→0x7bcc0`; `+0x34` `SaveHeader` `0x4fc5→0x7bf80`;
`+0x38` `LoadStream` `0x12d5→0x7c120`; `+0x3c` `SaveStream` `0x3652→0x7c400`;
`+0x40` `RegisterObjects` `0x6f55→0x7c510`; `+0x44` `LoadTopLevelObjects` `0x740f→0x7c540`;
`+0x48` `SaveTopLevelObjects` `0x2dab→0x7c5e0`; `+0x4c` `LoadObject` `0x4ee4→0x7c630`;
`+0x50` `PreSaveObjectSizeTable` `0x20c2→0x7c770`; `+0x54` `SaveObjectSizeTable`
`0x2f59→0x7c7d0`; `+0x58` `LoadObjectSizeTable` `0x63fc→0x7c830`.

Per-**object** vftable slots the driver dispatches: `+0x74` `RegisterStreamables`,
`+0x78` `SaveBinary`, `+0x6c` `LoadBinary`, `+0x70` `LinkObject`, `+0x88`
`PostLinkObject`, `+0x8c` `IsStreamable`, `+0x90` `GetRTTI`.

**Field layout of a (32-bit) `NiStream`:** `+0x2f8` collection sized from the
file's BS-version u32 (ctor `0x1000716c` / dtor `0x10005dee`);
`+0x2fc`/`+0x304` top-level object array + count; `+0x310`/`+0x314`/`+0x31c`
object-size `BSTArray`; `+0x32c`/`+0x334` registered-object array + count;
`+0x340`/`+0x344`/`+0x34c` fixed-string pool array; `+0x358` read `NiBinaryStream`
(`Istr()`), `+0x35c` write `NiBinaryStream` (`Ostr()`); `+0x1cc`..`+0x1e8`
object-groups array + bs-version; `+0x380/0x384/0x388` load-pass counters;
`+0x490` error code, `+0x494`·0x104 error message.

**On-disk layout — exactly what `SaveStream` (`0x7c400`) writes, in order:**

```
offset  size  field
0       31    "Gamebryo File Format, Version 20.2.0.7\n"   (PutS)
31      4     u32 fileVersion = 0x14020007                 (global 0x100e574c)
35      1     u8  endian = 1
36      4     u32 userVersion = 12                         (global 0x100e5750)
40      4     u32 numRoots  (= stream+0x304)
44      4     u32 BS uiCurrVersion                         [if userVersion != 0] (= 0x82)
48      4×n   4 × (u8 len, len bytes) — BS header strings, len INCLUDES the NUL
48+n   2     u16 numBlockTypes
       4     (u32 len, len bytes) × numBlockTypes          block-type (RTTI) names
       2     u16 typeIndex × numRoots                      hi bit 0x8000 = streamable (IsStreamable)
       4     u32 size × numRoots                           0-placeholder; back-filled by SaveObjectSizeTable
       4     u32 strCount  (fixed-string pool, stream+0x34c)
       4     u32 maxStrLen (max string length seen)
       4     (u32 len, len bytes) × strCount
       4     u32 (groups-1); then (u32) × groups          SaveObjectGroups (0x7d230)
       --    <root payloads>                               per root: obj->SaveBinary(stream), vft +0x78
       4     u32 linkCount (= stream+0x334, registered count)
       4     u32 linkID × linkCount                        per top-level obj via SaveLinkID
```

`SaveStream` call order proves the layout: `RegisterObjects` (vft+0x40) →
prep size array → `SaveHeader` (+0x34) → `SaveRTTI` (direct `0x72fc→0x7cc20`;
block-type names are collected from each object's `GetRTTI` vft +0x90, deduped
through a `NiTStringPointerMap` with a **u16 counter** — that is why the block-type
count is `u16` and why every type index is `u16`) → `PreSaveObjectSizeTable`
(+0x50) → `SaveFixedStringTable` (direct `0x2829→0x7cf30`) →
`UpdateObjectGroups` → `SaveObjectGroups` (`0x1131→0x7d230`) → per-root
`SaveBinary` loop with stream-seek size accounting → `SaveTopLevelObjects`
(+0x48, writes `u32 linkCount` then per-object `SaveLinkID`) →
`SaveObjectSizeTable` (+0x54, **seeks back** to the placeholder and fills the
`numRoots × u32` sizes).

**Load side (`LoadStream` `0x7c120`):** enter the stream critical section
(`0x100e9278`) → `GetLine`-validate → `LoadHeader` (+0x30) version/endian/
user-version bands (`ms_uiNifMinVersion`=`0x04020100`, `MaxVersion`=`0x14020007`,
`MaxUserDefinedVersion`=12; errors: 2 `Not a NIF file`, 3 `NIF version is too
old.`/`NIF user defined version is too old.`/`NIF BSversion is too old`, 4
`Unknown NIF version.`, 6 `Endian mismatch.` — `strcpy_s` into `+0x494`) →
`LoadRTTI` (`0x6a5a→0x7ca00`) → `LoadObjectSizeTable` (+0x58; count comes from
the header's `numRoots` — **no count field on disk for the size table**) →
`LoadFixedStringTable` (`0x469c→0x7d010`) → `LoadObjectGroups` (`0x37d3→0x7d100`)
→ per-object `LoadBinary` (vft +0x6c) with the **size table used to skip**
blocks whose type has no registered loader (`RegisterLoader` factory, global
map `0x100e925c`) → `LoadTopLevelObjects` (+0x44) → object `LinkObject`
pass (+0x70) → `PostLinkObject` pass (+0x88) → global post-process functions
(`0x100e9260`) → leave critical section.

> **Cross-build match:** this 32-bit 2016 contract equals the byte-verified 64-bit
> CK chain in §4b/§4c — same header line + `0x14020007` + endian + user-version
> 12, and the CK's SaveStream-core tri-pass (object vft slots `0xd8/0xe0/0x110`
> = the PDB quintet's `RegisterStreamables`/`SaveBinary`/`LinkObject`) reproduces
> this layout. Slot numbers differ (64-bit 2021 vs 32-bit 2016); the symbol names
> pin the semantics.

## 5. How to use it

```bash
P="/home/ciusr/Downloads/Elric+PDB/Elric/ElricInterop.pdb"
strings -a "$P" | grep -aE '^\?\?|^\?[A-Za-z_]' | sort -u \
    > ghidra-dump/elric-pdb-symbols.txt     # 90,090 symbols (already staged)
grep -a "INISettingCollection" ghidra-dump/elric-pdb-symbols.txt | llvm-undname
```

To probe a family: grep the symbols file for the class name, then pipe through
`llvm-undname` for readable signatures.

## 6. Cross-references

- Reader internals: `10-SETTINGS-READER.md`; write-back: `16-SETTINGS-WRITEBACK.md`
- ini census: `17-INI-PATHS.md`; source-tree recovery: `07-SOURCE-TREE.md`
- Symbol dump: `ghidra-dump/elric-pdb-symbols.txt`
- Symbol↔record-address map: `ghidra-dump/elric-setting-records.txt` (the 37 PDB
  settings → CK descriptor VAs with decoded defaults)
- Key inventory: `06-SETTINGS-KEYS.md`, `ghidra-dump/ck-settings-keys.txt`
- NIF save chain: `ghidra-dump/nifwriter-facegen-0x140ab8a60.asm` (FaceGen
  exporter incl. the `AddObject` slot-0x1e0 registers) and
  `ghidra-dump/nistream-savecore-0x142498a30.asm` (version validator + header
  writer + SaveStream core fragments)
- Byte-level NiStream wire spec (§4e): disassembly windows
  `ghidra-dump/nimain-nistream-serial.asm`; address-sorted export table
  `ghidra-dump/nimain-exports.txt` (NiMain.dll, 3,308 exports — the source of
  the §4e stub→body map)
