# 22 — Batch Pipeline Setting Dependencies

This document cross-references the batch switches with the Creation Kit's embedded
`bKey:Section` descriptor inventory (`ghidra-dump/ck-settings-keys.txt`). The inventory
contains 1,629 unique setting literals; the byte-level descriptor census in doc 18
finds 1,641 descriptor instances (some keys have more than one registered record).

## 1. Scope and method

The CK does **not** load settings lazily per batch switch. WinMain opens the
`INISettingCollection` and `ReadAll` walks the registered descriptors, so every
registered setting is read from `CreationKit.ini`/`CreationKitCustom.ini` during
startup. Here, “batch dependency” means a setting whose **value storage is directly
consumed or changed by code on a batch path**, rather than every setting that happens
to be loaded by the global reader.

The dependency census was made against the FO4 CK 1.11.137.0 image:

```text
SettingT record:  .data record +0x00 vftable, +0x08 value, +0x10 key:section
ReadAll:         FUN_1402cb660 -> INISettingCollection::ReadSetting (FUN_1425c7f90)
notify/setter:   FUN_1425c6b90(record)        // callback dispatch, not an ini write
value reference: RIP-relative reference to record+0x08
```

Record VAs and value offsets below are image VAs. A source address is a direct
value read/write only when it targets `record+0x08`; a reference to `record` itself
usually belongs to registration or the notify callback setup. The raw descriptor
list and decompiles are retained under `ghidra-dump/`; the most useful anchors are
`CLI-DISPATCHER-FUN_1404870f0.txt`, `decompile-ProcessRunner-0x4906a0.log`,
`decompile-PreVisPreCombine-Execute-0x48f920.log`,
`decompile-PreCombinedWorld-0xd20d90.log`, and
`decompile-PreVisCell-0x1181210.log`.

## 2. The two batch-wide controls

| setting | descriptor / value | default | direct batch use | confidence |
|---|---:|---:|---|---|
| `bThrowProcessCompleteExceptions:General` | record `0x143b8c880`, value `0x143b8c888` | 0 | `FUN_1404870f0` at `0x1404874d7`, `0x1404875d9`, `0x14048782a`, `0x140488a44`; shared runner `FUN_1404906a0` at `0x1404907f9`, `0x140490880`, `0x140490899` | **confirmed** |
| `bGeneratePreCombinedGeometryPreVis:General` | record `0x143b8c868`, value `0x143b8c870` | 1 | batch/precombined setup reads the value at `0x14048b50c`; setup/notify helpers also touch it at `0x140594e6b`, `0x140594e8b`, `0x14059512b`, and `0x14082d496` | **confirmed** |

The first setting gates the `0x6b650000`/`0x6b650001` exception protocol; it does
not decide whether an individual `Execute()` succeeds. The second is the editor's
pipeline-enable setting. It is not the CLI mode bit: `-GeneratePreCombined` and
`-GeneratePreVisData` still select their process through `DAT_143d2f34c`; this
setting controls the surrounding precombined/PreVis subsystem.

## 3. PreCombined and PreVis dependencies

These are the settings with value references in the CLI/precompute, combined-world,
and PreVis generator paths. They are the settings a hand-written ini can change
before a precombined/PreVis batch run.

### 3.1 Object selection, combined output, and chunk setup

| setting | record / value | direct consumers | observed references |
|---|---|---|---|
| `bUseCombinedObjects:General` | `0x143bd59b0` / `0x143bd59b8` | combined-object selection and cleanup | `0x140d1c356`, `0x140d1c4c8` |
| `bForcePreCombinedObject32bitExport:General` | `0x143bd5b18` / `0x143bd5b20` | combined mesh writer's index/format choice | `0x140d20e9c` |
| `uDefaultCombinedObjectLevelsXY:General` | `0x143bd5b60` / `0x143bd5b68` | default combined-object level selection | `0x140d223cc`, `0x140d22aa0`, `0x140d22b16`, `0x140d46d32` |
| `uDefaultCombinedObjectLevelsZ:General` | `0x143bd5b78` / `0x143bd5b80` | default combined-object Z-level selection | `0x140d2240f`, `0x140d22ae0`, `0x140d22b2d`, `0x140d46d58` |
| `bOnlyLoadCombinable3D:General` | `0x143b98f30` / `0x143b98f38` | temporary selection state set by the PreVis/PreCombined dispatcher | `0x14048f97e` |
| `bCombineStaticCollectionArtOnClone:General` | `0x143bd04b8` / `0x143bd04c0` | temporary save/restore state around the combined-object branch | `0x14048f94d`, `0x14048f96e`, `0x14048f9ac`, `0x1404900a1`; writer read at `0x140d3d4e7` |
| `fSmallObjectSize:General` | record value `0x143bd5a90` / `0x143bd5a98` | small-object classification and PreVis culling | `0x140d2267c`, `0x140d2b5b3`, `0x141181844` |
| `uDistantObjectExtraHideDelayMS:LOD` | record value `0x143bd5a58` / `0x143bd5a60` | combined-world object visibility timing | `0x140d1a2e5` |
| `bOutputCollectObjectFailure:General` | record value `0x143bd64c8` / `0x143bd64d0` | failure reporting while collecting combined objects | `0x140d7453b`, `0x140d74a21` |

`bOnlyLoadCombinable3D` and `bCombineStaticCollectionArtOnClone` are especially
important compatibility details: the dispatcher temporarily changes their in-memory
values through `FUN_1425c6b90` and restores them. They are not extra command-line
arguments and are not persisted by that callback.

### 3.2 PreVis scene generation and visibility inputs

| setting | record / value | direct consumers | observed references |
|---|---|---|---|
| `bOnlyLoadPreVis3D:General` | `0x143b98f48` / `0x143b98f50` | PreVis/PreCombined process setup; sets the process's filtered load state | `0x14048f95b`, `0x14048f962`, `0x140594e57`, `0x140594e78`, `0x140595112`, `0x14082d477` |
| `bUsePreCulledObjects:Display` | `0x143c60da8` / `0x143c60db0` | renderer/visibility culling support used by scene construction | `0x142cc1d47`, `0x142cc227c`, `0x142cc2348`, `0x142cc258d`, `0x142cc2b55`, `0x142cc3029`, `0x142cc306a`, `0x142cc30af` |
| `fPreVisCameraBufferNear:General` | `0x143c5f828` / `0x143c5f830` | near-plane/camera buffer in PreVis scene construction | `0x142c4f402` |
| `fPreVisCameraZShift:General` | `0x143c5f840` / `0x143c5f848` | camera Z shift in visibility projection | `0x142c4cd90`, `0x142c4f507` |
| `bExportSceneOnUmbraGenerate:General` | record value `0x143bfbd00` | scene export side of `FUN_141181210` | `0x1411819a9` |
| `bExportTomeOnUmbraGenerate:General` | record value `0x143bfbd18` | tome/export side of `FUN_141181210` | `0x141181da1` |
| `iMaxUmbraBakeThreads:General` | record value `0x143bfbd30` | Umbra/PreVis bake worker count | `0x141181ac4` |
| `bShowMarkers:Visibility` | record value `0x143b91368` / `0x143b91370` | marker inclusion while constructing the visibility scene | `0x14118131f` (also combined-world refs) |
| `bShowRarityMarkers:Visibility` | record value `0x143b91518` / `0x143b91520` | marker filtering in the combined-world visibility pass | `0x140d79abc` |

The process object’s `clean/filtered`, `scope`, and `currentset/totalsetcount`
fields are CLI arguments, not settings. The settings above affect what the selected
cell/worldspace slice does after the dispatcher has created it. See doc 09 for the
slice arithmetic and `all/main/ints/other` semantics.

### 3.3 Havok and precomputed-data behavior

| setting | record / value | direct consumer | observed reference |
|---|---|---|---|
| `bPreventHavokAddAll:HAVOK` | `0x143bd6410` / `0x143bd6418` | combined physics construction | `0x140d753a8` |
| `bPreventHavokAddClutter:HAVOK` | `0x143bd63f8` / `0x143bd6400` | clutter physics filtering | `0x140d753bb` |
| `bAllowPreCalcCheckIn:General` | `0x143bd5c90` / `0x143bd5c98` | precomputed-data check-in path | `0x140d44d07` |
| `bPreCalcSaveOnly:General` | `0x143bd5ca8` / `0x143bd5cb0` | precomputed-data save-only path; dispatcher toggles it around a process | `0x14048fefd`, `0x14049008e`, `0x140d44c0b` |
| `bAllowPluginPreCalcDataWipe:General` | `0x143bd59c8` / `0x143bd59d0` | registered precomputed-data cleanup/check-in setting; no direct value read was found in the scoped worker scan | descriptor registration at `0x1401b437a`; keep as an indirect candidate, not a confirmed worker read |
| `bUseNewTerrainSystem:TerrainManager` | `0x143bf98b0` / `0x143bf98b8` | terrain-backed world data used by combined/PreVis work | `0x140fc1637` |

The two `bPreventHavok*` keys are not needed to parse a command line; they change
whether the combined mesh's physics collection accepts all objects or clutter. The
`bPreCalcSaveOnly` value is explicitly mutated by the dispatcher and restored, so a
validator should report it as a runtime mode switch rather than as a static default
only.

## 4. Dialogue, text, lip, and animation exporters

### 4.1 `-ExportDialogue`

The branch itself takes only `<ESMFilename>` (doc 05). The worker does not consume
`Language` or `TextExport.txt`, but its dialogue/voice file path layer directly reads:

| setting | record / value | use | observed references |
|---|---|---|---|
| `bEnableDialogueExportLogging:General` | `0x143b8cae8` / `0x143b8caf0` | verbose dialogue-export diagnostics | `0x1404acd53`, `0x1404adf41`, `0x1404b01f9`, `0x1404b1023` |
| `bDialogueExportCheckFileExists:General` | `0x143b8cb00` / `0x143b8cb08` | file-existence check for voice/export assets | `0x1404b86d4` |
| `bMustConfirmDialogueExport:General` | `0x143b8cc38` / `0x143b8cc40` | confirmation gate in dialogue export | `0x1404ad717`, `0x1404ad74d` |
| `sExternalVoiceFilePath1:General` … `sExternalVoiceFilePath5:General` | records `0x143b8cb18`…`0x143b8cb78`; values `+0x08` | external voice search roots | `0x1404aef5c`, `0x1404aef6b`, `0x1404aef7e`, `0x1404aef91`, `0x1404aefa4` |

The actual report output remains `Logs/DialogueExport/dialogueExport*.txt`; these
settings do not alter the CLI grammar. `sExternalVoiceFilePath*` are raw string
settings and may be empty.

### 4.2 `-CompileTextExport`, `-GenerateLips`, and animation build data

| setting | record / value | direct consumer | observed references |
|---|---|---|---|
| `sLanguage:General` | `0x143b992c8` / `0x143b992d0` | localized string/compiler language context | `0x14091a2c9`, `0x14091aa9d`, `0x14091ae68` |
| `bValidateStringsOnCompile:General` | record value `0x143b9c088` | text-export compiler validation | `0x1409169f5` |
| `bKeepDLStringBlocksLoaded:General` | record value `0x143b9c040` | `.DLSTRINGS` block lifetime | `0x1409155b5`, `0x140915b0a`, `0x14091652a` |
| `bKeepILStringBlocksLoaded:General` | record value `0x143b9c058` | `.ILSTRINGS` block lifetime | `0x140915d8b`, `0x1409161c0`, `0x1409165cd` |
| `bForceAllAnimationTextExports:Animation` | `0x143b877b8` / `0x143b877c0` | force animation text export; set by `--forceAll` | runtime reads `0x140303f33`, `0x140303f7f`, `0x140303ff9`, `0x140304759`, `0x14030783f`, `0x140308569`, `0x14030e70e`, `0x14030eb19`; dispatcher sets at `0x140487d9d` |
| `bRunSpeedDataExport:Animation` | `0x143b87788` / `0x143b87790` | `--speed` animation export | runtime read `0x1402cfb9c`; dispatcher sets at `0x140487d47` |
| `bRunStanceDataExport:Animation` | `0x143b877a0` / `0x143b877a8` | `--stance` animation export | runtime reads `0x1402d173e`, `0x1402d1d83`; dispatcher sets at `0x140487d72` |
| `bOutputAnimationFilenames:Animation` | `0x143b877d0` / `0x143b877d8` | animation output filename listing | `0x1402cfd9e`, `0x1402cff38` |
| `bSkipAnimationTextExport:Animation` | `0x143b99bb8` / `0x143b99bc0` | shared “skip animation text export” gate | dispatcher/process setup at `0x1404907a1`; consumer at `0x140869590` |
| `sFileTypeLip:Voice` | `0x143ba4fa0` / `0x143ba4fa8` | LIP/source file extension paths | `0x140b31915`, `0x140b37604`, `0x140b72d2b` |
| `sFaceFXLanguage:Sound` | `0x143b91988` / `0x143b91990` | FaceFX language used by lip/face export | `0x1406495f9`, `0x14064ae80` |

`-CompileTextExport` still receives the explicit command-line language and input
roots documented in doc 05; the `sLanguage` setting is the engine's general
localization context and is not a replacement for that argument.

### 4.3 FaceGen and platform exporters

The platform token (`XB1`, `X64`, `PS4`, `W32`) is CLI state in `DAT_147322d04`,
not an INI setting. The FaceGen worker nevertheless directly consumes these
settings in its export/init path:- `bUseFaceGenPreprocessedHeads:General`, value `0x143bf8438`, read at
  `0x140a9595d`.
- `bCheckEyesOnInit:General`, value `0x143ba47e0`, read at
  `0x140a90f61`.
- `bCheckHairOnInit:General`, value `0x143ba47c8`, read at
  `0x140a90e39`.
- `bFixInvalidExtraHeadParts:General`, value `0x143ba40d8`, read at
  `0x140a91d26`.
- `bWarnWhenReplacingInvalidHeadParts:General`, value `0x143ba4090`, read at
  `0x140a918f8`, `0x140a9190d`, `0x140a9191c`.

These are shared FaceGen/editor validation settings, so they are marked **direct
worker dependencies**, not claims that the CLI parser reads them.

## 5. Other batch branches and shared services

The following direct value references are relevant to branches outside the
PreCombined/PreVis and dialogue/text groups:

| setting | observed use |
|---|---|
| `bUseVersionControl:General` | Tagify/check-in control path; reads and temporary writes around `0x140917a54`–`0x140917a98`; shared converter path also references it at `0x140d44bb5` |
| `sNetworkMasterPath:General` | VCS/network master path used at `0x14048cacf` |
| `sLocalMasterPath:General` | local master path used by batch startup/save helpers at `0x14048240f`, `0x140482493`, `0x1404841d1`, `0x140484232`, `0x1404842c6`, `0x1404851ea` |
| `bSkipValidateForms:General` / `bSkipValidateInfos:General` | set by dispatcher at `0x140488e48`/`0x140488e5b` for validation-oriented batch branches |
| `bFixUnneededPersistentRefs:General` | set by the depersist/cleanup branch at `0x14048863a` |
| `bLeveledCreaturesAreMarkers:General` | shared form classification used by combined/check-in paths; refs include `0x14048e985`–`0x14048ea37` |
| `sFileTypeGame:Voice` / `sFileTypeSource:Voice` | voice asset extension/path helpers at `0x1404bc4a0` and `0x1404aeffd` |
| `sArchive2Folder:Archive2`, `sAudioConversionFolder:AudioConversion`, `sElricFolder:Elric` | startup-side batch tool path setup at `0x140485385`, `0x140482ecb`, `0x140482b3e`; not process-specific output settings |

`-Install`, `-ConvertMaterials`, and `-CompressPSG` have no additional setting
contract established by the dispatcher decompile; their arguments and direct exit
behavior are documented in docs 05, 13, and 19.

## 6. Negative result: `iPlacedObjectRender`

`iPlacedObjectRender` is **not** one of the CK's embedded settings:

- no `iPlacedObjectRender:*` literal occurs in `ghidra-dump/ck-settings-keys.txt`;
- no exact `iPlacedObjectRender` string occurs in the CreationKit binary/string dump;
- no descriptor record or `record+0x08` value reference can therefore be mapped.

It must not be added to `CreationKit.ini` as a guessed key. The similarly named
placed-object/render concepts are runtime/editor state, not a `SettingT` descriptor
in this build. This negative result is part of the oracle contract: an unknown key
is dead configuration and is reported by `tools/validate_ckini.py` as `UNKNOWN`.

## 7. Dependency tiers for tooling

A compatibility tool should expose the following tiers rather than pretending that
all 1,629 keys are batch-specific:

1. **Batch control:** `bThrowProcessCompleteExceptions`,
   `bGeneratePreCombinedGeometryPreVis`.
2. **Process-mode state:** `bOnlyLoadCombinable3D`, `bOnlyLoadPreVis3D`,
   `bCombineStaticCollectionArtOnClone`, `bPreCalcSaveOnly`,
   `bSkipAnimationTextExport`, and the three `GenerateAnimInfo` flags. These can
   be changed by the dispatcher and restored without an ini write.
3. **Exporter inputs:** combined-object, PreVis camera/scene, Havok, dialogue/voice,
   localized-string, animation, and FaceGen settings listed above.
4. **Shared editor services:** VCS paths, validation flags, archive/tool paths, and
   form-classification settings. They are read on a batch path but also affect GUI
   operation.
5. **Global-only:** the remaining descriptor records. They are read at startup but
   have no direct value reference in the scoped batch/export code.

The existing oracle already validates the descriptor/type-letter contract and the
CLI grammar. This document supplies the dependency metadata that a future
`--explain-batch-deps <Switch>` mode can consume; it does not change the parser's
acceptance rules or doc-19's runtime-dependent exit prediction.

## 8. Evidence and limitations

The positive entries above are byte-level RIP-relative references to descriptor
value storage, cross-checked against the decompiled branch addresses. A setting
used only through an opaque singleton, a callback, a form object, or a library
function may not have a direct reference in the caller; it is therefore not promoted
to the “confirmed direct” tables without an address. Conversely, broad shared
functions can reference display/decals/UI settings while executing near a batch
worker; those are retained only when the setting's role is clear.

This is a dependency map, not a claim that changing every listed setting is safe.
For example, `bPreCalcSaveOnly` and the temporary selection flags are deliberately
mutated by the CK, and `bThrowProcessCompleteExceptions` changes process termination
rather than export data. Always compare the source RVA and descriptor VA when
porting this map to another Creation Kit build.
