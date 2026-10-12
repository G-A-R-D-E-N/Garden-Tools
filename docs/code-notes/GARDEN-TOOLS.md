# GARDEN-TOOLS code notes

## Radicle/src/schema/form_link_mixed.cpp:struct_payload_formid_offsets

Concept-level validation note: recognized payloads with unsupported extents must fail closed before FormID discovery or remapping. This note does not assert a new runtime or file-layout fact.

## Real-data HEDR acceptance and Windows test portability

`Radicle/tests/test_hedr_count.cpp:CommittedRealPluginsRebuildStaleCounts` independently counts each committed fixture, checks its stored metadata, and poisons the in-memory HEDR count before rebuilding. This prevents a writer that merely preserves the stored count from passing real-fixture acceptance. `recursive_hedr_count` combines the reader's record/group traversals rather than calling the writer's counter. `official_hedr_samples` includes the seven named official masters and every Data `.esl`.

`PluginEditTool/native/src/assets/TextureResolver.cpp:TextureResolver::addDataRoots` retains candidate roots only when their ancestry reaches a Data folder; otherwise recursive BA2 discovery could climb to the filesystem root. `PluginEditTool/native/tests/unit/TextureResolverTests.cpp:Texture resolver does not scan archives above a NIF that has no Data ancestor` puts an unrelated archive outside the NIF tree, while the companion Data-folder case verifies supported fallback.

`Radicle/tests/test_hedr_count.cpp:rebuilt_hedr_count` mutates the test-owned plugin instead of copying its full record graph, then parses only the serialized TES4 prefix. This preserves the rebuilt-header assertion without retaining a second full master graph. `OfficialMastersAndLightPluginsRebuildTheRecursiveCountWhenPresent` compares against the independently traversed structure because an installed official master can carry a different stored count; see `Radicle/docs/HEDR_RECORD_COUNT.md` for the pinned xEdit oracle and observed sample counts.

`PluginEditTool/native/tests/unit/ServiceTargetsTests.cpp:GraphTarget serializes concurrent RPC access to its compiler cache` explicitly discards RPC results because this concurrency check detects thrown exceptions. `PluginEditTool/native/tests/unit/TextureResolverTests.cpp:Texture resolver resolves loose NIFs and NIFs inside BA2s` checks file identity with `std::filesystem::equivalent`, since a case-insensitive filesystem can resolve the correct file while retaining the requested lowercase spelling.

## Radicle/src/schema/form_links.cpp:visit_link_offsets — DOBJ/DNAM proof (#287)

The layout below is verified from the documentation and open-source schema:
TES5Edit/TES5Edit `dev-4.1.6`, pinned at
`9fb016884bec138ea6c7b872cec831537d464c3e`,
[`Core/wbDefinitionsFO4.pas:8618-8627`](https://github.com/TES5Edit/TES5Edit/blob/9fb016884bec138ea6c7b872cec831537d464c3e/Core/wbDefinitionsFO4.pas#L8618-L8627):

```pascal
wbRecord(DOBJ, 'Default Object Manager', [
  wbEDID,
  wbArrayS(DNAM, 'Objects',
    wbStructSK([0], 'Object', [
      wbInteger('Use', itU32, wbEnum([], c), cpNormalIgnoreEmpty),
      wbFormID('Object ID', cpNormalIgnoreEmpty)
    ]), 0, cpNormalIgnoreEmpty
  ).SetAfterLoad(wbDOBJObjectsAfterLoad)
   .SetRequired
]);
```

`Core/wbInterface.pas:8539-8559` explicitly wraps one `wbSubRecord`
around a `TwbArrayDef`; this is not `wbRArrayS`. The signed DNAM occurs
once and is required by the schema. It contains zero or more packed 8-byte
rows with no count prefix: `Use` is an unsigned 4-byte value at row +0;
`Object ID` is a 4-byte FormID at row +4. `wbStructSK([0], ...)` selects
the first member as a sort key without adding bytes. Integer sizing and
sequential struct sizing are in `wbInterface.pas:12761-12773,14078-14143`;
the Object ID overload is `wbInteger(..., itU32, wbFormID, ...)` at
`9078-9084`. The uncounted array parser consumes the enclosing payload.
`Core/wbImplementation.pas:22515-22559` sets the uncounted reader bound to
`High(Integer)` and stops at the payload end, including the empty case.

Present empty DNAM is a valid zero-row array. The callback
`Core/wbDefinitionsCommon.pas:951-974` iterates existing rows and removes
rows whose Use is zero; an empty array executes no iterations. Radicle's
remapper preserves every row and its Use bytes, including unknown and
zero selectors, rather than adopting xEdit cleanup or sorting.
`wbFormID` is unrestricted at this schema layer (`wbInterface.pas:9010-9025`),
unlike `wbFormIDCk`: no target signature list or Use-dependent type check
is specified. Null Object IDs are supported and preserved.

Corpus cross-check on 2026-09-12, using
`Radicle/tools/corpus/wthr_corpus_inventory.py:collect_records`:
installed Fallout4.esm SHA-256
`a3fa1e66d75f80f33f864bf471d25b27f48c1cdbb97acb55c4df83b64ccee1d9`
contains one uncompressed DOBJ (`00000800`), one DNAM, 3168 bytes = 396
rows. The six official DLC masters contain no DOBJ. Local raw inventory:
the local run log `corpus.json` (not retained); no game payload is committed.

Baseline `8d4c6685aa76a65e2a8e9ed408472769dc6aa3d9` trace:
`formid_subrecords.inc` globally lists DNAM, but `visit_link_offsets` only
uses that list for 4-byte payloads. Valid DOBJ rows are skipped; malformed
4-byte DNAM falls through to offset +0. QUST's `qust_non_formid_signature`
excludes DNAM, INFO has a dedicated single-FormID route, and PROJ/HAZD
use `struct_payload_formid_offsets`. `remap_record_form_links`,
`rekey_form_links`, and `collect_form_links_detailed` all share this dispatch.
`Radicle/src/plugin/writer.cpp:remap_plugin_masters` works on a by-value
plugin, and `rekey_form_links` copies before traversal, protecting callers
from partially applied strict failures.

Active DNAM uses inventoried across the pinned FO4 definition: ALCH, AMMO,
ARMA, BOOK, EFSH, KYWD, AACT, TXST, PROJ, HAZD, IMGS, IMAD, BPTD, ADDN,
VTYP, IPCT, MESG, DOBJ, SMBN, SMQN, SMEN, DLBR, MUST, DLVW, SCEN, ARTO,
MATO, SNDR, IDLE, INFO, MGEF, NPC_, QUST, STAT, AECH, AMDL, BNDS, DMGT,
KSSM, LENS, NOTE, WATR, WEAP. RACE's DNAM definition is commented out.
These include text, flags, floats, mixed structures, unions and FormIDs;
this tranche does not claim to correct their legacy signature-only handling.
Collision controls use QUST General data (`10691`), INFO Shared INFO
(`9727`) and IPCT Texture Set `[TXST]` (`8005`).
The QUST control includes both the schema's 12-byte General payload and
a short 4-byte payload specifically exercising the existing early
exclusion before the global signature table.

Regression evidence for `Radicle/tests/test_dobj_formlinks.cpp:FormLink`:
the initial seven DOBJ tests were built and run against unchanged
`Radicle/src` at the baseline above. Six failed; the empty-array test
passed. Object bytes stayed stale, collection omitted the packed lanes,
direct rekey left its original serialization cache intact, malformed
payloads were accepted, and 4-byte DNAM was collected as offset +0.
Raw local output: the local run log `baseline-failing.txt` (not retained).

Parity boundary for `visit_link_offsets` and issue #43, using the schema
verified from the documentation above:

| Surface | DOBJ/DNAM scope |
| --- | --- |
| READ | Generic lossless payload read; Object lane collection is this tranche |
| WRITE | Generic lossless serialization and persistence of remapped Object lanes |
| ADD / REMOVE / CREATE | No typed DOBJ authoring support claimed or added |
| REMAP | Object at 4 + 8*n only; Use remains opaque and unchanged |
| VALIDATE | Whole rows and single DNAM; strict rejection, non-strict omission |
| Targets | Unrestricted FormID at the xEdit definition layer; null preserved |
| Opaque data | Use bytes, unknown subrecords and ordering preserved |

The traversal only sees present subrecords; this remap tranche does not
enforce required-DNAM presence for an otherwise empty/deleted DOBJ.
It does not validate selector membership or resolve target record types.
CLMT/WLST and DOBJ/DNAM are packed mixed arrays; IPDS/PNAM is repeated
fixed-size subrecords. Together they justify a separate descriptor design
review under #43, with representation/cardinality kept explicit. They do
not justify an abstraction or authoring-parity claim in this patch.

Validation of `Radicle/src/schema/form_links.cpp:visit_link_offsets`
against the schema from the documentation above on 2026-09-12 (Windows
x64, MSVC 19.44, Release): the record-qualified route adds 15 production
lines before the generic empty/signature checks. Strict traversal rejects
lengths not divisible by 8 and duplicate DNAM subrecords before visiting
any Object lane in that payload. Non-strict collection omits those
unsupported layouts. Empty DNAM remains present.

`Radicle/tests/test_dobj_formlinks.cpp` adds seven regressions:

- `DobjDnamPackedRowsRemapOnlyObjectsAndPreserveBytes`: packed rows,
  null Object, zero/unknown/FormID-like Use bytes, opaque subrecords,
  save/reload, exact Object collection lanes and ordinal zero.
- `DobjDnamTruncatedPayloadsFailClosedWithoutMutation`: lengths
  1/4/7/9/12/15/17, strict master/rekey failure after an earlier remappable
  record, caller byte equality, non-strict omission.
- `DobjDnamSeparateSubrecordsFailClosed`: rejects repeated DNAM instead
  of interpreting it as IPDS-style storage.
- `DobjDnamEmptyArrayRemainsEmpty`: accepts and preserves zero rows.
- `DobjDnamCollisionControlsPreserveQuestAndRemapIpctAndInfo`: QUST
  exclusion and both generic/dedicated legitimate DNAM link routes.
- `DobjDnamDirectRekeyPersistsAndPreservesAllOtherBytes`: DOBJ-only
  target mutation, plugin cache invalidation and full serialized-byte
  equality against a fixture differing only in the intended Object lane.
- `DobjDnamCollectionPreservesDocumentOrder`: neighboring IPCT/INFO
  records around the packed DOBJ lanes, with offsets and ordinals.

Focused filter `FormLink.DobjDnam*:FormLink.IpdsPnam*:FormLink.ClmtWlst*`:
18/18 passed. Full `ctest --test-dir Radicle/build -C Release --output-on-failure`:
549 discovered, 528 passed, 21 optional corpus tests skipped, zero failures.
The corpus layout cross-check above ran separately against installed game
data. Local run logs `focused-passing.txt` (not retained)
and `full-radicle.txt` (not retained). `git diff --check` passed. No generic descriptor,
selector cleanup/sorting, typed DOBJ authoring, or other parity family
is included.

## Radicle/src/schema/form_links.cpp:visit_link_offsets

Implementation details for the current INFO copy work are recorded in `docs/code-notes/info-copy.md`.

Explanation and rationale that would otherwise clutter source comments, anchored to `file:symbol`.

## Radicle/src/schema/form_links.cpp:visit_link_offsets — CLMT/WLST

The CLMT/WLST route is record-qualified and rejects unsupported payload lengths during strict remapping. `Radicle/tests/test_clmt_formlinks.cpp:FormLink.ClmtWlstRemapsWeatherAndGlobalLanesPreservingChanceBytes` covers the proven lanes and preserves unrelated bytes; `FormLink.ClmtWlstMalformedPayloadFailsClosedWithoutMutation` covers the failure path; `FormLink.ClmtWlstDirectRekeyPersistsThroughWriter` covers direct rekeying and plugin-level serialization cache invalidation; and `FormLink.ClmtWlstCollectionPreservesTraversalOrder` verifies traversal ordering across records.

## Radicle/src/schema/form_links.cpp:visit_link_offsets — IPDS/PNAM

The IPDS/PNAM route is record-qualified and evaluated before generic empty-payload checking, rejecting subrecords whose size is not exactly 8 bytes during strict remapping. In Fallout 4 (xEdit `wbRArrayS('Data', wbStructSK(PNAM, ...))`), IPDS structures impact entries as repeated 8-byte PNAM subrecords, each containing `{ Material FormID (+0), Impact FormID (+4) }`. `Radicle/tests/test_ipds_formlinks.cpp:FormLink.IpdsPnamRemapsMaterialAndImpactLanes` covers remapping both lanes across multiple repeated 8-byte PNAM subrecords and checks subrecord collection ordinals; `FormLink.IpdsPnamMalformedPayloadFailsClosedWithoutMutation`, `FormLink.IpdsPnamSixteenBytePayloadFailsClosedInStrictRemap`, and `FormLink.IpdsPnamEmptyPayloadFailsClosedInStrictRemap` cover strict fail-closed validation with transactional rollback for arbitrary malformed lengths, concatenated 16-byte single payloads, and 0-byte empty subrecords; `FormLink.IpdsPnamCollisionControlsPreserveNavmAndRemapNpc` proves NAVM u16 vertex index arrays remain byte-identical while NPC_ single-FormID PNAM subrecords continue to remap; `FormLink.IpdsPnamDirectRekeyPersistsThroughWriter` verifies direct rekeying and plugin-level serialization cache invalidation; and `FormLink.IpdsPnamCollectionPreservesTraversalOrder` verifies traversal ordering across repeated subrecords.

## Radicle/src/schema/form_links.cpp:visit_link_offsets — WTHR record-qualified FormLinks

WTHR reuses subrecord signatures that are FormID-typed elsewhere in the schema. `LNAM`, `VNAM`, `WNAM`, `NAM1`, `PNAM`, `JNAM`, `NAM0`, `NAM4`, `FNAM`, `RNAM`, `QNAM`, `DALC`, `MNAM`, `NNAM`, `GNAM`, `TNAM`, `SNAM`, `IMSP`, `WGDR`, `DATA`, and `EDID` are all in the generated `wbFormIDCk` signature list, so a signature-only lookup would rewrite WTHR's own fields. The WTHR branch therefore runs before any generic path and decides from WTHR's layout: `MNAM`, `NNAM`, `GNAM`, and `TNAM` are one FormID only at exactly 4 bytes; `IMSP` visits every 4-byte slot at 16 or 32 bytes; `WGDR` does the same at exactly 32; `SNAM` is `{ FormID, SoundType }` so only `+0` moves at exactly 8; `UNAM` visits `+0` and `+8` at 12 or 24. The quarantined signatures never contribute offsets because they are floats, indices, or colour and alpha pairs in WTHR. A proven link given any other size fails closed: strict remap throws, and non-strict collection returns no link for it rather than guessing. An unknown FormID-listed 4-byte signature inside WTHR also fails closed instead of being remapped.

`Radicle/tests/test_weather_formlinks.cpp` pins both halves of that contract. Its fixture has three masters (`Fallout4.esm`, `DropMe.esm`, `KeepMe.esm`) and remaps to `{Fallout4.esm, KeepMe.esm}`, so `KeepMe.esm` moves from index 2 to index 1 and every proven slot must change its index byte while the quarantined payloads stay byte-identical. `PNAM` is deliberately FormID-shaped (`u16{0x0200, 0x0812}`, which reads as `0x02000812` when the two vertex indexes are mistaken for one FormID) so the preservation assertions cannot pass by accident. `ZNAM` covers the unknown-but-listed case, and the well-formed control repeats the same signatures at their proven sizes so a failure can only come from payload size rather than the signature.

## PluginEditTool/native/src/plugin/RadiclePluginBackend.cpp:scanBrokenRefs — VMAD coverage caveat

Collecting links stays best-effort, so a record whose VMAD carries trailing data keeps the links the scan did reach, and a record whose VMAD failed mid-parse contributes none. The report is built from `collection.links`, so those partial hits appear in the dangling list. The caveat therefore says the links from incomplete records may be incomplete and the results are not exhaustive, instead of claiming they are excluded — the previous wording described the opposite of what the loop does. `PluginEditTool/native/tests/unit/RadiclePluginBackendTests.cpp:Radicle plugin backend caveats partial VMAD coverage as non-exhaustive` pins both the wording and the fact that the partial link is still listed.

## Radicle/src/schema/form_links.cpp:navm_vertex_index_signature — record-qualified PNAM/NNAM

Signatures are not globally meaningful, so routing has to be qualified by record. `PNAM` is a `wbFormIDCk` FormLink somewhere in the generated table (Mutagen's Npc.xml defines NPC_ `PNAM` as the HeadPart link), while NAVM `PNAM` and `NNAM` are arrays of `u16` vertex indexes. A 4-byte NAVM `PNAM` or `NNAM` therefore used to match the signature-only scalar path and have its two vertex indexes rewritten as a single FormID; 8-byte payloads escaped only because that path requires exactly four bytes. The NAVM branch now returns before any generic path at every payload size, and NAVM `ONAM` remains the base-object array. This is preservation, not refusal: a valid vertex-index array is legitimate data and must survive a master remap byte-for-byte, so the remap does not throw for it. `Radicle/tests/test_navmesh_formlinks.cpp:NavmFourByteVertexIndexArraysSurviveMasterRemapByteForByte` pins the NAVM side and `NpcFourBytePnamStillRemapsAsAHeadPartFormLink` pins the same signature still remapping on a record where it is a link.

## PluginEditTool/native/src/app/ServiceTargets.cpp:CellTarget::geometryBatch — processOne

Each unique model path is resolved and handed to `niftool geo` on a bounded pool of `std::jthread` workers, writing only its own result slot (so `results` needs no lock; only the work index and progress counters are atomic).

The worker pool uses `std::jthread`, not `std::thread`, and the spawn loop is wrapped in `try/catch (std::system_error)`. Constructing a `std::thread` can throw (resource exhaustion); if a later worker's construction threw while earlier workers were already joinable, stack unwinding would destroy those joinable `std::thread` objects and call `std::terminate`. `std::jthread` joins on destruction, so a mid-spawn throw unwinds cleanly; catching the `system_error` lets the workers that did start drain the queue. Because a partial spawn can leave some slots unprocessed (and a total spawn failure all of them), a final single-threaded sweep runs `processOne` on any result slot still null after the pool has joined — guaranteeing every path is resolved exactly once. `processOne` always writes a non-null object, so `is_null()` reliably marks an untouched slot.

`resolveNif()` and `nif_.geometry()` both touch the filesystem and can throw. A worker thread that lets an exception escape its function calls `std::terminate` and takes the whole native host down. So every per-item failure is caught inside `processOne` and recorded as that slot's `{ "error": ... }` entry — the batch always completes and returns partial results. This mirrors the C# path, where a propagated `Parallel.ForEach` exception is caught by `CellInterop.GetGeometryBatch` and returned as error JSON rather than crashing the process.

The niftool `geo` output is embedded as-is when it is a JSON object (mirrors the C# `JRaw` path); otherwise it is surfaced as an error entry. Unlike the C#, the parse is validated (`parse(..., nullptr, false)` + `is_discarded()`) so a malformed but `{`-leading output cannot corrupt the whole batch payload.

## PluginEditTool/web/src/util/cellGrid.ts:parseCellGridCoordinate — Cell Viewer grid integers (#283)

### Trace

`PluginEditTool/web/src/CellPanel.tsx` used `Number.parseInt(gridX, 10)` and `Number.parseInt(gridY, 10)` in both exterior-grid loading and exterior CELL creation. JavaScript accepts a numeric prefix, so `12abc` became `12` and `1.5` became `1` before the native backend saw the request.

The same permissive check also controlled both grid action buttons.

### Fix

`PluginEditTool/web/src/util/cellGrid.ts:parseCellGridCoordinate` accepts only a complete optional-sign plus decimal-digit token within the signed 32-bit CELL grid range. Both load/create paths and both button guards in `PluginEditTool/web/src/CellPanel.tsx` use the same parser.

The int32 bound matches the native grid arguments. `PluginEditTool/native/src/plugin/RadiclePluginCell.cpp:intArgument` previously converted `GetPlacedReferencesAtGrid` coordinates with an unchecked `get<int>()`, so an out-of-range safe integer such as `4294967297` wrapped to a different cell; it now refuses them. Create range-checks through `RadiclePluginLifecycle.cpp:lifecycleSignedField`.

The native backend remains authoritative for Fallout 4 coordinate range and CELL semantics. This change only prevents the frontend from silently changing the user's token before dispatch.

### Proof

The current branch carries the regression in `f8dd7b3d` before the implementation in `f29d22d6`. Before the implementation, the requested helper did not exist and the `Number.parseInt` behavior reproduced the defect:

- `12abc` -> `12`
- `1.5` -> `1`

Post-fix parser checks in `PluginEditTool/web/src/CellPanel.test.ts` cover signed integer acceptance at both int32 bounds plus suffix, decimal, exponent, whitespace, empty, and out-of-range refusal.

`PluginEditTool/web/src/CellPanelGrid.test.tsx:Cell Viewer exterior grid dispatch` renders the real panel with a simulated native bridge and plugin picker. Invalid persisted X or Y disables both actions and Enter sends no load request. Correcting an invalid value restores loading. Signed coordinates reach CreateRecord, SavePlugin and the subsequent grid reload unchanged. This verifies React dispatch; it does not verify live WebView2 or in-game behavior.

`PluginEditTool/native/src/plugin/RadiclePluginCell.cpp:intArgument` also refuses integers outside `int`, so `GetPlacedReferencesAtGrid` reports `CellGridArgumentsMissing` instead of wrapping; `PluginEditTool/native/tests/unit/RadiclePluginExteriorCellTests.cpp:PluginEditTool creates a new exterior CELL in the WRLD hierarchy` checks that `4294967263` no longer resolves the cell at X=-33.

`PluginEditTool/native/include/fo4ide/core/JsonInt.hpp:jsonInt` is the shared strict range check: it refuses non-integers, unsigned values above `INT_MAX` and signed values outside `int`. `jsonIntSaturated` clamps into `int` instead. Grid lookup (`src/plugin/RadiclePluginCell.cpp:intArgument`), audio bitrate (`src/app/AudioTarget.cpp:intArgument`, fallback 0 = encoder default) and graph schema (`src/graph/GraphDocument.cpp:tryDeserialize`) use the strict form. Paging limits use the saturating form (`src/app/ArchiveTarget.cpp:intArgument`, `src/app/PluginBackendTarget.cpp:intArgument`, `src/plugin/RadiclePluginOverridePatch.cpp:intArgument`, `src/archive/RadicleArchiveBackend.cpp:intField`), so an oversized limit becomes the backend maximum rather than one row or a wrapped value. `tests/unit/PluginBackendTargetTests.cpp:JSON int helpers refuse or saturate values outside int` checks both. A present graph schema that is not an `int` (float, string, out of range) is refused as a malformed document rather than silently loading as the current schema (`tests/unit/GraphDocumentTests.cpp:A document whose schema is not an integer is refused`). A missing archive `List`/`ExtractAll` limit falls back to the backend defaults (2000 and 5000) instead of 0, which the backend would clamp to one row. `src/plugin/ConditionCodec.cpp` `applyParam` types each condition parameter by its declared function kind; the typed parameter rules are in `docs/code-notes/pet-condition-codec.md`. Form-ID parameters are stored as the unsigned bit pattern in a signed field. `src/app/ServiceTargetSupport.hpp:intNumberArgument` covers the JSON numbers that `src/app/PapyrusTarget.cpp` (`SymbolAt` position) and `src/app/GraphTarget.cpp` (`SearchPalette` limit) accept: integers go through the strict `jsonInt` check and in-range floats are truncated as before, while non-finite values take the fallback instead of the undefined double-to-int conversion. Out-of-range values take the fallback for the `SymbolAt` position and saturate for the `SearchPalette` limit (its `saturate` flag), matching the other paging limits. `src/plugin/RadiclePluginLifecycleFields.hpp:lifecycleSignedField` refuses unsigned JSON values above `INT64_MAX` before its signed range check, so 18446744073709551583 is no longer accepted as x = -33.

## PluginEditTool/native/src/app/ServiceTargets.cpp split — PapyrusTarget, GraphTarget, ServiceTargetSupport

`src/app/ServiceTargets.cpp` was split along its target classes to stay under the 750-line source limit: `SettingsTarget`, `NifTarget` and `CellTarget` stay there, `PapyrusTarget` moved to `src/app/PapyrusTarget.cpp`, and `GraphTarget` to `src/app/GraphTarget.cpp`. Helpers used by more than one target (`trim`, argument readers, `pathResult`, base64 decoding, staging directories, file text I/O, `toLower`, `isWhitespace`) live as inline functions in `src/app/ServiceTargetSupport.hpp` (`fo4ide::app::support`), which includes only what those helpers need; each target file includes its own project headers, and helpers used by one target stay in that file's anonymous namespace. Behaviour is unchanged apart from `intNumberArgument` above.

Explanations moved out of the source during the split:
- `src/app/PapyrusTarget.cpp:extractScriptName` mirrors PapyrusService.ExtractScriptName: the first non-comment line, if it starts with ScriptName, contributes its second token.
- `src/app/PapyrusTarget.cpp:namespacedOutPath` mirrors PapyrusService.NamespacedOutPath: namespaced scripts (Foo:Bar) are written into namespace subfolders so they recompile as-is.
- `src/app/PapyrusTarget.cpp:diagnoseDependencies` mirrors PapyrusService.DiagnoseDependencies: it mines unresolved symbols out of PapyrusCompiler.exe output and maps known script extenders to Nexus pages.
- `src/app/PapyrusTarget.cpp:PapyrusTarget::compile` ports PapyrusService.Compile: validation, engine selection, then either the built-in engine (PapyrusAnalysisService.Compile) or the Creation Kit process path when a PapyrusCompiler.exe is configured. Its `workDir` / `target` pair mirrors the C# layout sniffing.
- `src/app/PapyrusTarget.cpp:PapyrusTarget::decompile` ports PapyrusService.Decompile: a single file (written, or a NOT SAVED preview) or a whole folder of .pex, with namespaced output paths so scripts recompile as-is.
- `src/app/GraphTarget.cpp:graphScriptRoots` ports GraphInterop.Roots(): the Papyrus base-import directories that exist. `describeNodeDefinition`, `describeGraphDiagnostic` and `graphDiagnosticsJson` port GraphInterop.Describe(NodeDefinition), Describe(GraphDiagnostic) and Diagnostics() (the `{ diagnostics, ok }` payload).
- `src/app/GraphTarget.cpp:GraphTarget::invoke` locks the whole target because the RPC dispatcher runs `invoke()` concurrently and `compiler()`/`Refresh` mutate `index_`, `compiler_` and `rootsKey_`.

## PluginEditTool/native/src/plugin/RadiclePluginLifecycle.cpp split — RadiclePluginLifecycleFields, RadiclePluginProjExplHazd

`src/plugin/RadiclePluginLifecycle.cpp` was split to stay under the 750-line source limit. Its payload field readers and validators (`lifecycleSignedField`, `lifecycleUnsignedField`, `optionalStringField`, `checkLoadedSlot`, `validateProvidedScalars`, …) moved into `src/plugin/RadiclePluginLifecycleFields.hpp` (`fo4ide::plugin::lifecycle`, inline); apart from the `lifecycleSignedField` unsigned guard described above, their bodies are unchanged. The PROJ, EXPL and HAZD branches of `RadiclePluginBackend::createRecord` moved into private members `RadiclePluginBackend::createProjectileLocked`, `createExplosionLocked` and `createHazardLocked` in `src/plugin/RadiclePluginProjExplHazd.cpp`; they return the created key or the same validation error, and `ProjExplHazdError` still propagates to `createRecord`'s catch. They run under `createRecord`'s lock and mutate only the working plugin copy it passes in.

## Native write model — deferred persistence (#174)

Ordinary record-edit RPCs stage in-memory dirty state via `stagePluginMutationLocked` and require `SavePlugin` to persist. Localized QUST CNAM table changes stage DLSTRINGS through `stagedLocalization` and flush on the same explicit save path (`persistPluginLocked` + dependency-first write). `DiscardPlugin` restores the last persisted snapshot without writing disk. See `docs/code-notes/174c-pending-lifecycle-audit.md` and the `#174C pending lifecycle` note below.

## PluginEditTool/native/src/plugin/RadiclePluginOverridePatch.cpp:createMergedPatch — overwrite

`copy_record_as_override` is called with `overwrite=false`. The C# oracle routes `CreateMergedPatch` through `ResolveConflict` with its default `overwrite=false`, which refuses a record the patch already contains (the result is not "Copied", so it is counted as a failure, not a forward). With `overwrite=false` the native op leaves an existing patch record intact and reports it as "already present in the patch" instead of silently replacing it.

## PluginEditTool/native/src/plugin/RadiclePluginMutation.cpp:RadiclePluginBackend::laterOverridesLocked — #40 losing overrides

`laterOverridesLocked` reads the existing graph's definition chain after the target's index in `plugins_`. `RadiclePluginBackend.cpp:rebuildGraphLocked` builds the graph in that same order. Copying into the target does not move it in load order. `RadiclePluginMutation.cpp:copyAsOverride` validates source presence before returning `LOSES:` and leaves pending state unchanged until consent. Its `optionalBoolArgument` and the corresponding helper in `RadiclePluginBatchMutation.cpp` default absent/null consent to false and reject other non-booleans.

`PluginEditTool/native/src/plugin/RadiclePluginBatchMutation.cpp:copyAsOverrideMany` asks about overwrite before losing load order. `PluginEditTool/native/src/app/PluginBackendTarget.cpp:PluginBackendTarget::invoke` forwards ResolveConflict's optional fourth argument as CopyAsOverride's fifth argument. Omitted consent retains the existing call shape.

`PluginEditTool/web/src/RecordView.tsx:copyInto` asks about losing load order before overwrite and carries consent through the retry. `MainShell.tsx:batchCopyAsOverride` retains overwrite consent while asking about losing order. `ConflictResolver.tsx:handleResolve` retries with consent only after confirmation. These fields are declared in `backend.ts:Backend`.

`PluginEditTool/web/src/overrideConfirmation.test.tsx` renders the three real components with the real confirmation dialog and a simulated native bridge. It checks cancellation without retry, explicit consent, and preservation of consent across both prompt sequences. The plugin picker is stubbed to select a fixed target. This is component acceptance, not WebView2 or game-runtime proof.

`PluginEditTool/native/tests/unit/RadiclePluginMutationTests.cpp` and `RadiclePluginBatchMutationTests.cpp` place the empty target before the source definition to exercise refusal without mutation and consent to a losing copy. The graph still reports the later source as winner.

`PluginEditTool/native/src/assets/TextureResolver.cpp:addDataRoots` adds recursive BA2 scan roots only when the climb reaches a Data ancestor. `PluginEditTool/native/tests/unit/TextureResolverTests.cpp` places an archive beside the NIF tree to detect accidental ancestor discovery. The loose-NIF assertion compares filesystem identity because Windows lookup can return different filename casing.

`PluginEditTool/native/src/plugin/RadiclePluginDeepCopy.cpp:RadiclePluginBackend::deepCopyAsOverride` applies the same rule to every record in the deep-copy set. The dry run lists the records that would lose; apply returns `LOSES:` before the `EXISTS:` overwrite check unless the optional seventh argument (`allowLosing`, after `maxRecords`) is true, matching the single-copy prompt order. `PluginEditTool/native/tests/unit/RadiclePluginDeepCopyTests.cpp:Native DeepCopyAsOverride requires consent before writing overrides that will lose` covers preview, refusal without mutation, and opt-in. `deepOptionalBoolArgument` treats an absent or null `allowLosing` as false and rejects any other non-boolean with `InvalidAllowLosingFlag`, like `RadiclePluginMutation.cpp:optionalBoolArgument`, so a malformed consent value fails loudly instead of being read as a refusal.

`PluginEditTool/web/src/recordActions.ts:useRecordActions` holds the RecordView context-menu operations (including the deep-copy consent retry), `PluginEditTool/web/src/RecordContextMenu.tsx` renders that menu, and `PluginEditTool/web/src/recordStatus.ts` holds the pure cell-status and legend tables. They were split out of `PluginEditTool/web/src/RecordView.tsx` so it stays under the 750-line source limit; behavior is unchanged and `PluginEditTool/web/src/overrideConfirmation.test.tsx` exercises both the record copy and deep copy consent flows through the real menu.

## PluginEditTool/native/src/plugin/RadiclePluginOverridePatch.cpp:revertOverrides — overwrite

`copy_record_as_override` is also called with `overwrite=false` here. The C# oracle reverts through `AddOverride`, which is `GetOrAddAsOverride`: if the patch already contains the record it returns the existing one (non-null, so counted as reverted) rather than replacing its contents. Native therefore counts both `inserted` (a fresh revert) and `already_exists` (the patch already had that FormKey) as a successful revert, and never overwrites the patch's own version with the prior plugin's. This only differs from a plain revert when the patch loads before the reverted plugin and already carries an override of the same record.

## PluginEditTool/native/src/plugin/RadiclePluginOverridePatch.cpp:revertOverrides — skipped accounting

The apply loop increments `skipped` for any copy result other than `inserted`/`already_exists` — chiefly `source_containment_unsupported`, which `copy_record_as_override` returns for a nested record (e.g. an INFO under a DIAL) the patch does not already contain. Without this, a record the dry run listed as revertible could fail to copy during apply and disappear from the result entirely, so `Reverted N` under-counted with nothing explaining the difference; it is now reported in the `skipped` total, matching the C# apply loop (which increments `skipped` on an `AddOverride` failure).

In `RadiclePluginBackend::revertOverrides`, the `prior == nullptr` guard is a defensive `continue` that is **not** counted in `skipped`: the C# oracle has no equivalent (it holds the prior record directly), and the "no prior version" case is already excluded earlier during candidate-finding (`versions.size() < 2`), mirroring C#'s separate `noPrior` counter which is likewise absent from the reported `skipped` total. In current native control flow this guard is effectively unreachable — the prior comes from the loaded version stack under the same lock the apply loop holds.

## PluginEditTool/native/src/plugin/RadiclePluginFormLinkList.cpp:addListItem — target validation

The C# oracle (`WriteService.AddListItem`) stores whatever FormKey it is given: it wraps the value in a `FormLink<T>` without checking that the target resolves in the loaded environment or that its record type matches the list's element type. To stay at oracle parity, native `addListItem` does **not** require the target to be a current `graph_.winner`, and does **not** run its own `formlink_list_target_allowed` check.

Target validation is instead left to the list mutation primitive (`insert_record_formlink_list_element` → `validate_record_and_target`), which admits any target of the schema's declared kind — and for a `wildcard_target` list such as `FLST\Items`, any record at all. The one residual, shared with every other native list op, is that the primitive rejects a *dangling local* FormKey (one owned by the editing plugin with no backing record), and for a typed (non-wildcard) list a local target of the wrong type; the oracle would accept those. This is deliberate: it is the library's shared safety behavior, not specific to AddListItem.

The `field` argument is matched to a list via `formlink_list_descriptor_by_field`, which is ASCII case-insensitive to mirror C#'s `BindingFlags.IgnoreCase` reflection lookup (`Items`, `items`, `ITEMS` are equivalent).

## RadiclePluginFormLinkList.cpp:addListItem / removeListItem — external-owner masters

`encode_plugin_form_id` throws when a FormKey's owner is neither the plugin itself nor one of its declared masters. Adding an external FormLink (e.g. `Something:OtherMod.esp` to a patch that does not yet master `OtherMod.esp`) would therefore fail. The C# oracle never hits this because Mutagen implicitly adds the master when it serializes the FormLink at save time.

So `addListItem` calls `ensureOwnerMaster(mutated, targetKey->owner())` before inserting: if the owner is missing from the master table it is appended (kind taken from the loaded owner plugin's TES4 small-file flag, else the `.esl` filename heuristic) via `remap_plugin_masters`, which re-encodes the plugin's existing FormIDs against the new table. Local targets (`owner == plugin.key`) and null owners are left untouched.

`removeListItem` does the inverse: it never adds a master. If the target's owner is not already the plugin or one of its masters, no LNAM in the plugin could possibly encode to it, so the value cannot be present — it short-circuits to `ListItemNotFound` ("No entry matching …") instead of letting `encode_plugin_form_id` throw. This matches C#, whose removal compares FormKeys directly and needs no master.

## Radicle/include/fo4/schema/formlink_lists.hpp:FormLinkListStorage — packed_array (KWDA/KSIZ)

`FormLinkListDescriptor::storage` distinguishes the two on-disk shapes a FormLink list can take. `per_subrecord` (FLST\Items = repeated `LNAM`, the RACE preset lists) gives each element its own fixed 4-byte subrecord; the mutation primitives insert/erase/reorder whole subrecords and `matching_positions` enforces the 4-byte element size. `packed_array` (a record's `Keywords`) instead stores the entire list inside a single `KWDA` subrecord holding N contiguous 4-byte FormIDs, with the element count carried in a companion counter subrecord named by `descriptor.counter` (`KSIZ`). The count is redundant with `KWDA.size()/4` but is what the game reads, so it is always kept in sync.

`formlink_lists.cpp` branches on `storage` inside each primitive (`record_formlink_list_size`, insert/set/remove/remove-matching/move/clear) so both alias-addressed (`formlink_list_descriptor_by_field`) and indexed-path (`formlink_list_descriptor`) callers work for either shape. The packed splice helpers rewrite the one `KWDA` subrecord's byte array and then reconcile `KSIZ`:
- Inserting the first element into a record with no `KWDA` creates `KSIZ` then `KWDA` at the anchor position resolved by `formlink_lists.cpp:empty_list_insertion_point` — insert after the nearest present predecessor, else before the nearest present successor. The keyword anchor sets are emitted by `formlink_schema_codegen.py` from `keyword_records` in `fields.json`. For packed keyword lists the anchors are the **complete** canonical field-order neighbours of the Keywords block for that record (every subrecord written before Keywords, nearest first, then every one written after, nearest first) mined from Mutagen's `WriteRecordTypes` order. `EDID` is included as a furthest-predecessor fallback **when present** (it is conventionally first, but not universally guaranteed — a record built from only a FormKey has none), a signature emitted on **both** sides of Keywords (e.g. `FULL`, which is also written inside a record's post-Keywords object-template block, or SCEN's repeated `CTDA`/`SCQS`/`TNAM`) is dropped from both sets as ambiguous, and `append_when_empty` is **false** for keyword lists so there is no end-of-record fallback — if no unambiguous canonical predecessor or successor is present, insertion fails closed (`insertion_boundary_unavailable`) rather than appending the block out of canonical order. (The flat FLST\Items list keeps `append_when_empty=true`: it legitimately appends after `EDID`.) Only the 25 major records whose Mutagen class is itself `IKeyworded` get a `keyword_records` entry; `INNR` is excluded because its keyworded type is the nested `InstanceNamingRule` inside `RuleSets`, not the `InstanceNamingRules` major record (a flat `INNR\Keywords` descriptor would splice into whichever nested `KWDA` it found first) — that nested case is left for a future scoped-list implementation. `KSSM` is likewise excluded (its keywords are per-element `KNAM`, not a packed `KWDA`).
- Removing the last element drops **both** the `KWDA` and `KSIZ` subrecords, so a keyword-less record carries neither (matching how vanilla records with no keywords look); a non-empty result rewrites `KWDA` and sets `KSIZ` to the new count.

A `KWDA` whose byte length is not a multiple of four is malformed and throws `FormLinkListError`, the packed analogue of the flat list's four-byte-element guard.

## PluginEditTool/native/src/plugin/RadiclePluginFormLinkList.cpp:addListItem — KWDA requires a real keyword

The FLST target-validation note above documents deliberate oracle-parity leniency for the wildcard `FLST\Items` list. The `Keywords` (KWDA) list does **not** follow that: it is a typed, non-wildcard list whose only declared target is `KYWD`, and native intentionally enforces that. For any non-wildcard field-aliased list, `addListItem` resolves the value through `graph_.winner` and rejects it unless the winner's signature is admitted by `formlink_list_target_allowed` — so a keyword add must resolve to a loaded `KYWD` record (local or from a master), and anything else is refused rather than stored. This is a chosen correctness behavior for the native+Radicle engine, not a mirror of the C# app (which wraps any FormKey as a keyword unchecked). The wildcard `FLST\Items` path keeps its lenient behavior because `wildcard_target` short-circuits the check.

## Radicle/src/forms/components.cpp:set_record_components — CVPA/FVPA component arrays

A record's component list is a single packed subrecord of N eight-byte structs `{u32 component FormID, u32 count}` — `CVPA` on `MISC` (scrap components, targeting `CMPO`) and `FVPA` on `COBJ` (recipe components, targeting a broad item set). Each component FormID is encoded through `encode_plugin_form_id` like any FormLink, and `decode_components`/`set_record_components` are the read/replace primitives. `record_supports_components` gates the two record signatures.

`MISC` also carries a companion `CDIX` — a per-component display-index byte array. `set_record_components` regenerates it as an identity run `0..N-1` in CVPA order whenever it rewrites CVPA, and drops it when the list becomes empty, so CVPA and CDIX never disagree (the C# oracle rewrote only CVPA and left a possibly stale CDIX). `COBJ` has no CDIX. When the record already has the array subrecord the codec replaces it in place (preserving field order); when absent it inserts at the canonical slot resolved by `components.cpp:canonical_insert_index` — after the nearest present predecessor, else before the nearest present successor, each nearest-first. MISC uses predecessors only (`DATA`/`FIMD`/`KWDA`/`KSIZ`/`EDID`) and `append_when_empty=true` because `CVPA`/`CDIX` are the final MISC fields, so end placement is canonical. COBJ carries both a predecessor set (`ZNAM`/`YNAM`/`EDID`) and the full post-`FVPA` successor set (`DESC`/`CTDA`/`CNAM`/`BNAM`/`NAM1`/`NAM2`/`NAM3`/`ANAM`/`FNAM`/`INTV`) and `append_when_empty=false`, so a sparse COBJ (no EDID/YNAM/ZNAM but with a later field like `CNAM`) gets `FVPA` inserted **before** that successor rather than appended out of order — and if neither side can anchor it, the codec fails closed (`ComponentError`) rather than mis-placing, the same policy the KWDA anchors use. `CDIX` is placed immediately after `CVPA`. An empty list removes both. The codec is target-type-agnostic: it encodes whatever FormKeys it is given (validation lives at the native layer).

## PluginEditTool/native/src/plugin/RadiclePluginComponents.cpp:setComponents — strict component validation

`SetComponents(plugin, record, componentsJson)` parses `[{"component","count"}]`, resolves each `component` (FormKey text or EditorID) through `resolveListValue`, and — a deliberate divergence from the C# oracle, which stores any resolved FormKey unchecked — validates it against the loaded environment before writing: it must resolve to a `graph_.winner`, and its winning signature must match the record's component schema — a `MISC` scrap component must be a `CMPO`; a `COBJ` recipe component must be one of Mutagen's 13 `IItem` record types (`RadiclePluginComponents.cpp:isItemSignature`: AMMO ARMO BOOK CMPO COBJ NOTE ALCH INGR KEYM LVLI LIGH MISC WEAP — COBJ is itself an IItem, so a recipe may reference another recipe), so a resolving-but-non-item like a QUST/CELL is refused rather than stored. `count` is parsed with an explicit policy (`parseComponentCount`): absent → 1, otherwise an integer in `1..4294967295` (a value that is negative, zero, non-integer, or exceeds `UINT32_MAX` is rejected — no silent narrowing). Unresolved / mistyped / out-of-range entries are collected with their one-based `#i` index, and any failure refuses the whole call with `ComponentEntriesRefused` (`Refused: K of N component entries failed: #i: reason; …`) before anything is staged, so a bad entry never wipes the existing list (GT-083). External component owners are added as masters via `ensureOwnerMaster` before encoding, exactly as the KWDA/FLST add path does. Mutations are staged in memory and persisted by `SavePlugin`, so the success response ends "SavePlugin to persist.".

## RadiclePluginOverridePatch.cpp:revertOverrides — containsComponent filter

`revertOverrides`' optional `containsComponent` argument (issue #173) now filters candidates instead of failing closed. The component is resolved through `resolveListValue` (FormKey text or EditorID); an unresolvable value returns `ComponentNotResolved`. A revert candidate (a record where the bad plugin currently wins) is kept only if its winning record carries a component list containing that FormKey, read via `components.cpp:decode_components` gated by `record_supports_components`. This is a **deliberate broadening** of the C# `MutagenLoader.CobjUsesComponent`, which checked `COBJ`/FVPA only: native checks any component-bearing record, so a `MISC` whose CVPA scraps into the component is matched too — both record kinds genuinely "use" the component, and `decode_components` handles CVPA and FVPA uniformly. A record type with no component surface never matches. The dry-run message gains the C#'s "(filtered to ones using …)" clause.

## PluginEditTool/native/src/plugin/ConditionFunctions.cpp:kRunOnValues — Run On numbering (GT-197)

CTDA Run On (u32 at payload offset 20) uses xEdit's enum in `Core/wbDefinitionsFO4.pas:5197-5209` @93cc0bc5: `Subject` 0 through `My Killer` 10, with no value 11. The vanilla corpus scan (Fallout4.esm and the official DLC masters, 2026-10-11, output not committed) counts Command Target (8) 163 rows, Event Camera Ref (9) 4, My Killer (10) 10, and value 11 never. `kRunOnValues` follows that numbering. The earlier 9/10/11 values matched the old Mutagen `RunOnType` but not disk data. `runOnName` and `runOnValue` are the only name-to-value map on the native path, used by `ConditionCodec.cpp` and `RadiclePluginQuest.cpp`. `ConditionBackendTests.cpp` `runOns[10] == "MyKiller"` indexes the JSON name list, not the value, and is unchanged.

The repo's Mutagen copy carries the same numbering: `PluginEditTool/Mutagen/Mutagen.Bethesda.Fallout4/Records/Common Subrecords/Condition.cs:RunOnType` now has `CommandTarget = 8`, `EventCameraRef = 9`, `MyKiller = 10`, so the C# writers that take Run On from that enum produce the disk values. Test: `PluginEditTool/FO4RecordEditor.Tests/WriteToolsTests.cs:WriteToolsTests.SetConditions_RunOnMyKillerWritesByteTenAtRunOnOffset` (raw byte 10 at payload offset 20). The test was not run in this change (no local build).

## Radicle/src/schema/ctda.cpp:write_ctda_conditions_in — range-scoped CTDA splice

`write_ctda_conditions_in(record, begin, end, conditions, insert_anchor)` generalizes the whole-record `write_ctda_conditions` to a subrecord half-open range `[begin, end)` so a single record can carry several independent CTDA runs (one per effect). It locates the CTDA run confined to that range (first/last CTDA, extended through trailing `CIS1`/`CIS2`, plus a `CITC` count immediately before the run when present) and returns a `CtdaScopeWrite`: `replaced` when an existing run was rewritten, `inserted` when the range had no run and `insert_anchor` held a slot to place the serialized conditions, `no_op` when there was nothing to do (empty replacement of an absent run), and `no_list` when a non-empty replacement had no run and no anchor. `write_ctda_conditions(record, conditions)` is now `write_ctda_conditions_in(record, 0, size, conditions, nullopt) == replaced`, preserving the exact root-record contract (missing run → `false`, never manufacturing a CTDA list). `read_ctda_conditions` is likewise `read_ctda_conditions_in(record, 0, size)`. Only a `CITC` **inside** the range and immediately before the run is refreshed. On insert, a `CITC` is written only for record types in `Radicle/src/schema/ctda.cpp:k_citc_list_owners`, matching xEdit; effects carry no per-effect `CITC`, so they get none (see `radicle-ctda-contiguous-run.md`).

## Radicle/src/schema/condition_scopes.cpp:set_record_scoped_conditions — nested Effects[i].Conditions

The condition analogue of the nested FormLink list descriptor. `ConditionScopeDescriptor` (record signature, container token e.g. `Effects`, member token `Conditions`, `ConditionScopeKind`) is generated from `schema/fallout4/fields.json`'s `condition_scopes` array by `tools/condition_scope_codegen.py` into `generated_condition_scopes.inc`. The set is SPEL/ENCH/ALCH/INGR — the four Mutagen `ExtendedList<Effect>` records; **FO4 has no SCRL** (Scroll is Skyrim-only), so the handoff's SCRL is dropped.

**Effect boundaries are NOT a single EFID marker.** An Effect is `EFID? EFIT? CTDA*`: Mutagen writes `BaseEffect`→EFID with `WriteNullable` (null base effect ⇒ no EFID) and `Data`→EFIT only `if Data is {}`, and its parser triggers on EFID **or** EFIT **or** CTDA. So `condition_scopes.cpp:effect_element_ranges` segments the effect region with a small grammar scanner over stage(`EFID`=0,`EFIT`=1,`CTDA`=2): a subrecord starts a new element when no element is open, or when its stage is `!= CTDA` and `<= the current element's max stage` (EFID always opens a new element; a second EFIT opens one; repeated CTDA continues; CIS1/CIS2 are transparent and belong to the current element). Element *i* is then `[start[i], start[i+1] or region_end)` where `region_end` is one past the last effect-or-CIS subrecord. This is distinct from the trailing-terminator model of `nested_formlink_lists.cpp:terminated_group_range` and correctly addresses effects whose `BaseEffect`/`Data` are null. `set_record_scoped_conditions` splices only that element's CTDA run (insert anchor = element end, so a conditionless effect gets its CTDA appended after `EFIT` and before the next element), leaving siblings byte-untouched, then clears the record/plugin `original_range` caches like `mutation.hpp:set_record_conditions`. `scoped_condition_path(record_signature, path)` resolves `Container[i].Member` ASCII-case-insensitively (C# `BindingFlags.IgnoreCase` parity) and is keyed by record signature because all four descriptors are otherwise identical.

## PluginEditTool/native/src/app/PluginBackendTarget.cpp:invoke — GetConditionsAt/SetConditionsAt routing

`isRootConditionsPath` replaces the old `requireRootConditionsPath` gate. Root paths (empty or `Conditions`, case-insensitive) still route to the two-arg `GetConditions` / three-arg `SetConditions` backend ops, preserving the existing contract and the RoutingBackend tests. A non-root path now routes to the new `GetConditionsAtPath` / `SetConditionsAtPath` ops with the full argument list (including the path) instead of returning `ConditionPathUnsupported` at the target. The `ConditionPathUnsupported` error still exists but is now raised deeper — by `RadiclePluginBackend.cpp:getConditionsAtPath` / `RadiclePluginMutation.cpp:setConditionsAtPath` when `scoped_condition_path` cannot represent the path for that record signature (e.g. `Perks[0].Conditions`, or any record outside the SPEL/ENCH/ALCH/INGR effect set) — so the frontend degrades gracefully on paths this PR does not cover.

## PluginEditTool/native/src/plugin/RadiclePluginMutation.cpp:setConditionsAtPath — eager-persistence response

`setConditionsAtPath` mirrors `setConditions` (same editability/on-disk-freshness guards, same `buildCondition` JSON→CTDA codec and editor-ID resolver, same write-through to disk) but targets one effect scope. It resolves the record's signature from the mutated copy, looks up the `ConditionScopeDescriptor`, and delegates the splice to `set_record_scoped_conditions`. A failed entry refuses the call with `InvalidConditionsJson` (`#i: reason` for the first failing entry) and nothing is staged (GT-083). On success the response is `Set N condition(s) at {path} on {record} in {plugin}. SavePlugin to persist.` QUST `Aliases[i].Conditions` is deliberately out of scope here: its dual leading markers (`ALST`/`ALLS`), `ALED` terminators, and interleaved combined-alias list need a different scoping primitive and are deferred to a follow-up.

## Radicle/src/schema/condition_scopes.cpp:alias_element_ranges — QUST Aliases[i].Conditions

The second `ConditionScopeKind` (`alias`), for `QUST\Aliases[i].Conditions`. Unlike the effect scanner, QUST carries **record-level** CTDA outside any alias — `DialogConditions` and `UnusedConditions` are `ExtendedList<Condition>` on the Quest itself — so the scanner must bracket strictly and never touch those. `alias_element_ranges` segments the alias array so the element index matches **Mutagen's** `Quest.Aliases` list (the source of the `Aliases[i]` path contract): it anchors at `ANAM` (Next Alias ID), then walks the contiguous run while `is_alias_start` (**three** markers: `ALST` reference, `ALLS` location, `ALCS` collection). `ALST`/`ALLS` aliases run `start..ALED` and their CTDA run is the alias's conditions.

**Collection grouping (the subtle part):** Mutagen `Quest.cs:FillBinaryAliasParseCustom`, on the first `ALCS`/`ALMI`, rewinds and parses a greedy `ListBinaryTranslation<CollectionAlias>` into one `QuestCollectionAlias.Collection` and adds **one** `Aliases[]` entry — so a **contiguous run of `ALCS[+ALMI]`** is a single logical alias, not one-per-ALCS. The scanner groups the whole contiguous ALCS run into one non-conditioned element. Counting each ALCS separately (as `forms/quest_aliases_decode.cpp` decode does) would shift every following alias's index — the same failure class as the effect-scanner's old EFID-only bug — so this **intentionally diverges from `quest_aliases_decode.cpp`** and follows Mutagen, which owns the path index. `QuestCollectionAlias` has no `Conditions` property, so a collection element is flagged non-conditioned and **both** read and set return `element_not_conditioned` (native `ConditionScopeNotConditioned`) — a set of an *empty* list is rejected too, and no write occurs, mirroring C#'s reflection resolver which fails on the missing `Conditions` property before it ever looks at the supplied list. So this is format-correct, not a divergence.

**Canonical insert position:** inserting conditions into a *conditionless* alias must place CTDA at Mutagen's Conditions field slot, not blindly before `ALED`. `alias_condition_insert_anchor` returns the first post-Conditions field present in the bracket: for a **location** alias that is `ALCC` (`ClosestToAlias`, written after Conditions); for a **reference** alias it is the first of `k_reference_condition_successors` (`KSIZ KWDA COCT CNTO COED SPOR OCOR GWOR ECOR ALLA ALDN ALFV ALDI ALSP ALFC ALPC VTCK`, all written after Conditions per `QuestReferenceAlias` WriteRecordTypes — note `ALCC` is a *predecessor* for reference aliases). With no such field present it falls back to the `ALED` index. `ScopeRange` carries `insert_anchor` separately from `[begin,end)`; effect ranges set `insert_anchor = end`.

**Fail-closed structure** (mirrors `quest_aliases_decode.cpp` validation): duplicate `ANAM`, non-4-byte `ANAM`, alias markers with no `ANAM`, a non-4-byte `ALST`/`ALLS`/`ALCS` start id, an `ALST`/`ALLS` alias with no `ALED`, a non-1-byte `ALMI` (Mutagen `MaxInitialFillCount` is a `Byte`), or any alias marker (`ALST`/`ALLS`/`ALCS`/`ALMI`/`ALED`) appearing after the single contiguous alias array (a malformed second array) → `alias_element_ranges` returns nullopt → `alias_structure_invalid` (native `ConditionAliasMalformed`) for both read and set. `read_scoped_conditions` returns `ScopedConditionsRead{ok,error,conditions}` so reads distinguish out-of-range / not-conditioned / malformed. The native `getConditionsAtPath`/`setConditionsAtPath` dispatch is otherwise generic over `ConditionScopeKind`; only the statuses map to errors.

## Radicle/src/schema/condition_scopes.cpp:package_branch_ranges — PACK Branches[i].Conditions

The `package_branch` kind. A procedure-tree branch starts at an `ANAM` immediately followed by `CITC` (`source:xEdit Core/wbDefinitionsFO4.pas:10603-10607 @93cc0bc5`: `Procedure Tree` > `Branches` > `Branch` = `ANAM`, `wbCITCReq`, `wbConditions`). The range covers the branch's own members (`CITC`, `CTDA`, `CIS1`, `CIS2`, `PRCB`, `PNAM`, `FNAM`, `PKC2`, `PFO2`, `PFOR`; `source:xEdit Core/wbDefinitionsFO4.pas:10608-10634 @93cc0bc5`) and stops at the next `ANAM`, so the next branch never leaks into this scope. The insert anchor is the slot right after `CITC`. Package top-level conditions (`source:xEdit Core/wbDefinitionsFO4.pas:10571 @93cc0bc5`) sit before the procedure tree and fall outside every branch range.

## Radicle/src/schema/condition_scopes.cpp:quest_target_ranges — QUST Targets[k].Conditions

The `quest_target` kind. Each `QSTA` starts one objective target (`source:xEdit Core/wbDefinitionsFO4.pas:10779-10793 @93cc0bc5`: `Objectives` > `Objective` > `Targets` > `Target` = `QSTA` struct, then `wbConditions`). `QSTA` appears nowhere else in the definition. The index `k` is the flat ordinal of targets across all objectives in record order. This deliberately departs from Mutagen's nested `Objectives[i].Targets[j].Conditions`, because the native path grammar accepts one index. Renumbering objectives shifts target indices, as it already does for `Aliases[i]`. The range covers `CITC`, `CTDA`, `CIS1`, `CIS2`. `Objective` has no `wbConditions` (`source:xEdit Core/wbDefinitionsFO4.pas:10772-10778 @93cc0bc5`), so no objective scope exists. The insert anchor is after `QSTA`, or after `CITC` when one is present.

## Radicle/tests/test_condition_scopes_real.cpp:ConditionScopesReal.BranchAndTargetScopesMatchOfficialLayoutAndWriteBackIsIdentity — real-data PACK branch and QUST target scopes

Checks `condition_scopes.cpp:package_branch_ranges` and `condition_scopes.cpp:quest_target_ranges` against the seven official masters, through `read_scoped_conditions` and `set_record_scoped_conditions`. Gated on `FO4_DATA_DIR`, parameterized like `test_quest_structure_real.cpp`. This stack branches from a main without the real-data CI wiring; on current main `.github/workflows/native-ci.yml` sets `FO4_DATA_DIR` from the runner variables for the Windows Release pass (main `e224573b`), so it runs with data there once merged, if the variable is set on the runner. Registered in the `fo4_tests` source list in `Radicle/CMakeLists.txt` with no per-test timeout or label; the only timeout is `gtest_discover_tests(DISCOVERY_TIMEOUT 120)`.

**Oracle.** The test does not call the finders to build its expectations. It derives its own ranges from the record layout. A PACK branch starts at `ANAM` immediately followed by `CITC`, and extends over `CITC`, `CTDA`, `CIS1`, `CIS2`, `PRCB`, `PNAM`, `FNAM`, `PKC2`, `PFO2`, `PFOR` (`source:xEdit Core/wbDefinitionsFO4.pas:10604-10634 @93cc0bc5`). A QUST target starts at `QSTA` and extends over `CITC`, `CTDA`, `CIS1`, `CIS2` (`source:xEdit Core/wbDefinitionsFO4.pas:10779-10793 @93cc0bc5`). xEdit has no `CITC` in the target, so an optional leading `CITC` is accepted, as the finder does. Top-level PACK conditions sit before the procedure tree (`source:xEdit Core/wbDefinitionsFO4.pas:10571 @93cc0bc5`).

**Checks, per record.**
- Each independently derived scope reads with `ok`, and the read count equals its CTDA count.
- A leading `CITC`, when present, equals that CTDA count.
- The index one past the last scope returns `container_index_out_of_range`, so the finder does not invent extra scopes.
- Every PACK `CTDA` lies in exactly one branch, or before the first branch (top-level).
- Every QUST `CTDA` between the first `QOBJ` (`source:xEdit Core/wbDefinitionsFO4.pas:10773 @93cc0bc5`) and the first `ANAM` ("Next Alias ID", `source:xEdit Core/wbDefinitionsFO4.pas:10805 @93cc0bc5`) lies in exactly one target.
- Writing back each successfully read scope leaves the subrecord list identical (signature, extended size, payload). The header and compressed bytes are not compared.

**Deviation from the brief.** The QUST window ends at the first `ANAM`, not at the end of the record. Alias conditions sit after `QOBJ` but belong to alias scopes (`source:xEdit Core/wbDefinitionsFO4.pas:10868, 10921 @93cc0bc5`). A literal "after the first QOBJ" rule would flag every quest with alias conditions.

**Totals.** The test prints per master and reports via `RecordProperty` (`PackCount`, `BranchCount`, `QuestCount`, `TargetCount`, `ConditionCount`, and the failure counts). No figures are written here. The lane had no game data and nothing was run.

**Cost.** Each write-back calls `set_record_scoped_conditions`, which scans the whole plugin, so runtime grows with branches times records. Not measured.

## Radicle/src/schema/condition_scopes.cpp:perk_effect_tab_ranges — PERK Tabs[k].Conditions

The `perk_effect_tab` kind. Each `PRKC` ("Run On (Tab Index)", one signed byte) starts one run-on tab of a perk effect (`source:xEdit Core/wbDefinitionsFO4.pas:7643-7647 @93cc0bc5`: `Effects` > `Perk Conditions` > `Perk Condition` = `PRKC` then `wbConditions.SetRequired`). The effect itself is `PRKE` header, `DATA` union, the tab list, then `EPFT`/`EPFB`/`EPF2`/`EPF3`/`EPFD` and the `PRKF` end marker (`source:xEdit Core/wbDefinitionsFO4.pas:7606-7642 @93cc0bc5` and `:7648-7689 @93cc0bc5`). `PRKC` is defined only there in the PERK definition (`source:xEdit Core/wbDefinitionsFO4.pas:7645 @93cc0bc5`), so matching the exact four-byte signature inside PERK cannot collide with another member. The index `k` is the flat ordinal of `PRKC` subrecords across all effects in record order, the same convention as `quest_target`; renumbering effects shifts tab indices.

The range is `PRKC` plus the directly following `CTDA`, `CIS1`, `CIS2` run. Any other subrecord ends it, so a scope cannot cross into the next tab's `PRKC`, the next effect's `PRKE`/`DATA`, or the `EPFT`-to-`PRKF` function parameters. `wbConditions` has no `CITC` member; `.SetCountPath(CITC)` (`source:xEdit Core/wbDefinitionsFO4.pas:5183-5254 @93cc0bc5`) only names the count source, and the PERK tab definition has no `wbCITC` before its `wbConditions`. The insert anchor is therefore directly after `PRKC`, with no count update. A `CITC` appearing right after a `PRKC` is outside xEdit's model and ends the scope, so it is never rewritten. The record-level `wbConditions` at `source:xEdit Core/wbDefinitionsFO4.pas:7591 @93cc0bc5` sits before the first `PRKC` and is not part of any tab scope. Those top-level lists remain the root-list follow-up.

## Radicle/src/schema/ctda.cpp:write_ctda_conditions_in — CITC count on insert

An insert into a scope whose slot directly precedes the anchor with a 4-byte `CITC` now writes the new condition count into that `CITC`, the same way the replace path already does. Without this, an empty `PACK` branch that gained conditions kept `CITC = 0`. Effect and alias scopes carry no `CITC`, so their behaviour is unchanged. Whole-record writes pass no anchor and still refuse to insert.

## Radicle/src/schema/condition_scopes.cpp:root_condition_window — bare `Conditions` root list

The bare `Conditions` path (`PluginEditTool/native/src/app/PluginBackendTarget.cpp:isRootConditionsPath`) reads and writes the record's root list through one window. `read_root_conditions` and `Radicle/include/fo4/plugin/mutation.hpp:set_record_conditions` both call `root_condition_window`, so read and write cannot disagree. The nested ranges come from the existing finders through `scope_ranges`, one call per kind the generated table lists for the record signature (effect, alias, `package_branch_ranges`, `quest_target_ranges`, `perk_effect_tab_ranges`), sorted by start. The root candidates are the gaps between those ranges. One gap holding a CTDA is the root list. Two or more such gaps are `multiple_lists`. A malformed alias array is `alias_structure_invalid`. A record type with no table entries is one gap over the whole record, so its reads and writes match the old whole-record behaviour; the only change is the insert rule below.

Insert position (`Radicle/src/schema/condition_scopes.cpp:root_insert_anchor`): the first present member of a successor set, which lists the members that follow the top-level list in xEdit order, so a new list lands before all of them.
- PACK: `IDLF`, `IDLC`, `IDLT`, `IDLA`, `IDLB`, `CNAM`, `QNAM`, `PKCU` (`source:xEdit Core/wbDefinitionsFO4.pas:10571-10575 @93cc0bc5`: `wbConditions`, then `wbIdleAnimation`, `CNAM`, `QNAM`, `PKCU`; `wbIdleAnimation` is `IDLF`, `IDLC`, `IDLT`, `IDLA`, `IDLB` at `source:xEdit Core/wbDefinitionsCommon.pas:8626-8657 @93cc0bc5`). `PKCU` is SetRequired. The anchor sits before the procedure tree, outside every `Branches[i]` range.
- INFO: `RNAM`, `ANAM`, `TSCE`, `INTV`, `ALFA`, `ONAM`, `GREE`, `TIQS`, `NAM0`, `INCC`, `MODQ`, `INAM` (`source:xEdit Core/wbDefinitionsFO4.pas:9754-9779 @93cc0bc5`). The list follows the Responses group.
- PERK: `DATA`, `SNAM`, `NNAM`, `FNAM`, `PRKE` (`source:xEdit Core/wbDefinitionsFO4.pas:7591-7608 @93cc0bc5`). `DATA` is SetRequired. `PRKE` is the first member of `Effects` (7606-7608) and is a fallback only.

None of these three records lists a `wbCITC` member before its top-level `wbConditions`, so an insert writes no CITC. INFO and PERK name every top-level member defined after the list, so when none of those is present the list is appended at the end of the record (`Radicle/tests/test_condition_root_insert.cpp:InfoWithoutSuccessorAppendsListAtEnd` and `PerkWithoutSuccessorAppendsListAtEnd`). This matches xEdit's placement: `source:xEdit Core/wbImplementation.pas:19317-19324 @93cc0bc5` (`TwbElement.Create` appends the new element to its container), and `source:xEdit Core/wbImplementation.pas:9455-9456 @93cc0bc5` gives it the member index as `SortOrder`. With `wbSortSubRecords` set (`source:xEdit xEdit/xeMainForm.pas:5160 @93cc0bc5`) the sort at `source:xEdit Core/wbImplementation.pas:9480-9481 @93cc0bc5` keeps it after every earlier member; the library default (`source:xEdit Core/wbInterface.pas:150 @93cc0bc5`) leaves the appended element last. PACK keeps `no_condition_list` for an absent anchor: its successor set omits the members that follow its list (`Package Data` at `source:xEdit Core/wbDefinitionsFO4.pas:10581 @93cc0bc5`, `XNAM` at `:10601`, `Procedure Tree` at `:10603`), so an end append is not proven. QUST keeps the `NEXT` anchor only: after its Quest Dialogue list come `NEXT`, Story Manager, Stages and later members (`source:xEdit Core/wbDefinitionsFO4.pas:10737-10756 @93cc0bc5`), so a quest without `NEXT` stays refused. Record types outside these four keep `no_condition_list` for an absent root list (`Radicle/tests/test_condition_scopes_records.cpp:EffectOnlyRecordRefusesBareWriteAndKeepsEffectList`). The root `wbConditions` array itself is `source:xEdit Core/wbDefinitionsFO4.pas:5183-5254 @93cc0bc5`.

QUST has two root lists: Quest Dialogue (`source:xEdit Core/wbDefinitionsFO4.pas:10737 @93cc0bc5`) and Story Manager (`source:xEdit Core/wbDefinitionsFO4.pas:10739 @93cc0bc5`), with `NEXT` (`:10738`) between them. No nested range covers either one, so both fall in one window. That window holds two runs, so the bare path refuses with `multiple_lists` for both reads and writes. Neither list is addressable on its own by the root path. No root insert is defined for QUST. A QUST with exactly one of the two lists is addressed by the bare path.

Stage log entries (`Radicle/src/schema/condition_scopes.cpp:root_window_limit`): for QUST the window ends at the first `INDX`. xEdit places both top-level lists before `Stages` (`source:xEdit Core/wbDefinitionsFO4.pas:10737-10740 @93cc0bc5`), and each log entry's list sits inside a stage (`:10756`), so log-entry conditions are never read or written by the bare path. A QUST whose only list is a log-entry list has no root list (read returns empty, write refuses with `no_condition_list`). PET edits log entries through its own stage paths.

Not added: a scoped root path form. The bare `Conditions` path is routed to whole-record handling by `PluginEditTool/native/src/app/PluginBackendTarget.cpp:isRootConditionsPath`, and this window is the only root resolver. PERK effect conditions are one list per run-on tab (`source:xEdit Core/wbDefinitionsFO4.pas:7643-7646 @93cc0bc5`); they are addressed as `PERK Tabs[k].Conditions` (`perk_effect_tab_ranges` above) and are nested ranges here, so a PERK top-level list beside effect tabs is the root list (`Radicle/tests/test_condition_scopes_records.cpp:PerkRootListBesideEffectTabsReadsAndWritesRoot`).

## Radicle/src/plugin/mutation.cpp:reserve_local_form_key — FormID allocation without a record

Extracted the FormID-allocation half of `create_local_record` into `reserve_local_form_key(Plugin&)`: it assigns form keys, probes upward from `max(HEDR nextFormID, local floor)` for the first unused local id, advances HEDR nextFormID, and returns the `FormKey` — creating no record. `create_local_record` now calls it then places its record in the signature's top group. `copy_placed_reference_as_new` uses it to allocate a fresh destination-local key for a record it places inside a nested CELL bucket instead of a top group. It mutates the plugin in place (unlike `create_local_record`'s copy-then-commit); callers already working on a plugin copy commit it themselves. Throws `FormIdError` when no local slot remains.

## Radicle/src/forms/placed_reference_copy.cpp:copy_placed_reference_as_new — containment-aware CopyAsNewRecord (#179)

Duplicates a **REFR/ACHR** placed reference as a NEW destination-local record inside its source CELL containment — the CopyAsNewRecord analogue of `copy_placed_reference_as_override`. The two share `resolve_placed_containment` (extracted from the override's inline derivation): it locates the reference, requires the containment to end in `cell_children` + a persistent/temporary bucket, reads the parent CELL FormKey from the `cell_children` group label, validates the source CELL (and, for exterior cells, the WRLD) share the reference's containment root, and returns all of it. Scope is deliberately **REFR/ACHR only** — Radicle proves no other placed family's on-disk/FormLink contract (`placed_reference.hpp` documents this), so other signatures return `unsupported_signature` (this also fails-closed a contained INFO, whose signature is not REFR/ACHR). Divergences from the override, per issue #179's decisions: (1) the parent CELL FormKey **must already exist in the destination** — it is never synthesized; absent → `parent_cell_missing`. (2) A fresh key is allocated with `reserve_local_form_key` and the copy is inserted into the source's matching bucket (only the missing `cell_children`/bucket GRUPs are created, anchored on the destination CELL's actual path via `ensure_group_path`/`ensure_cell_children_group`/`ensure_reference_bucket`). (3) The new record's `form_key` is stamped to the allocated key. EditorID assignment and the response string stay in native. Master handling mirrors the override (parent CELL/WRLD owners + the reference's own FormLink owners). Malformed containment still throws `PlacedReferenceError` (native catches → `CopyAsNewFailed`).

## PluginEditTool/native/src/plugin/RadiclePluginMaintenance.cpp:RadiclePluginBackend::invokeLegacyBackend — contained-record routing

When the source record is not top-level but is present, CopyAsNewRecord routes supported contained records through their containment-aware primitive on a copy of the target plugin and maps the result: `created` → set EditorID (requested, else source EDID + `DUP`), write through, and return the existing `Copied … as NEW record {newKey} ('{edid}').` string; REFR/ACHR use `copy_placed_reference_as_new`, while INFO uses `copy_info_as_new` and requires the existing QUST/DIAL ancestry, creating only a missing type-7 Topic Children group. Unsupported signatures remain `SourceContainmentUnsupported`; missing target CELL or DIAL parents use their dedicated errors; `source_missing` → `SourceRecordNotFound`. Malformed containment throws and is caught as `CopyAsNewFailed`. The top-level path is unchanged.

## Radicle/src/forms/quest_aliases_decode.cpp:decode_quest_aliases — QUST collection alias ALCS grouping (#203)

The decoder now models a **contiguous `ALCS[+ALMI]` run as one collection alias** holding ordered `QuestCollectionEntry` items (`quest.hpp:QuestCollectionEntry` = `{id, max_initial_fill_count}`), matching Mutagen `Quest.cs:FillBinaryAliasParseCustom` (greedy `ListBinaryTranslation<CollectionAlias>` → one `QuestCollectionAlias`, one `Aliases[]` entry) and the already-correct condition scanner `condition_scopes.cpp:alias_element_ranges`. Previously each `ALCS` produced its own `QuestAlias`, so any multi-`ALCS` collection shifted every following alias's `Aliases[i]` index — the #185/#186 index-shift class. This is a **Radicle-only** correction: neither the decoder nor `quest_alias_edit.cpp` is consumed by the native/RPC surface (C# `set_quest_aliases` goes straight to Mutagen), so no contract-visible behaviour changes.

Model decisions (per issue #203):
- **Entry[0] is not a logical Alias ID.** `decode_quest_aliases` sets `QuestAlias.id` and the `alias_ids` uniqueness set only for reference/location (`ALST`/`ALLS`); a collection's `id` stays 0 and its ALCS ids live in `collection_entries`. `decode_alias_semantics` walks the grouped `ALCS[+ALMI]` subrecords into `collection_entries` (4-byte ALCS via `alias_id`, 1-byte ALMI). The collection-only `QuestAlias.max_initial_fill_count` scalar was removed (redundant with per-entry data); `QuestAliasCreation.max_initial_fill_count` stays for single-entry authoring.
- **Collection entry ids do not participate in uniqueness or ANAM growth.** A shipped `ALCS(X) ALST(X)` (same id) decodes without a duplicate error; regression `test_quest_alias_collection_grouping.cpp:GroupsContiguousAlcsRunAsOneAliasWithOrderedEntries` proves an ALCS id equal to a following ALST id groups cleanly.
- **Edit scanner** `quest_alias_edit.cpp:inspect_alias_section` groups the whole run into one `AliasRange{collection=true}`; `find_range` matches ref/location ranges only (`!range.collection`), so `add/replace/remove/reorder_quest_alias` address logical aliases by ID and never a collection. `add_quest_alias` skips the id-collision check and `grow_next_alias_id` for collection creations (`is_collection`). `reorder_quest_alias`'s positional `to` now counts a collection run as one position; `test_quest_alias_collection_grouping.cpp:ReorderReferenceAcrossCollectionKeepsRunIntact` moves a reference across a two-entry collection and re-reads with the run intact.

`encode_base_alias` builds the raw subrecords only and no longer calls `decode_alias_semantics` on the collection path (the two public `encode_quest_alias` overloads do the single semantic decode); calling it there double-populated `collection_entries` for a single-ALCS creation.

Scope held out of the original grouping PR: **direct collection-ID-based mutation** (`find_range` skips collection ranges). Neighboring reference/location `reorder_quest_alias` still treats a collection span atomically. `test_quest_alias_real.cpp:marker_count` counts a contiguous `ALCS[+ALMI]` run as one logical alias.

## Radicle/src/forms/quest_aliases_encode.cpp:encode_base_alias — collection multi-entry (#73)

Mutagen `Quest.cs:FillBinaryAliasParseCustom` treats a **contiguous `ALCS[+ALMI]` run as one `QuestCollectionAlias`**. `CollectionAlias_Generated.cs:WriteRecordTypes` emits `ALCS` (i32/u32 id) plus optional 1-byte `ALMI`. Adjacent collection objects in `Aliases[]` cannot be distinguished on disk, so `write_quest_aliases` refuses two collection creations in a row. `QuestAliasCreation.collection_entries` is the multi-entry authoring list; empty means the original single-entry sugar (`id` + optional `max_initial_fill_count`). Duplicate ids **inside one collection** fail closed. Collection ids still do not participate in `ALST`/`ALLS` uniqueness or ANAM growth. `write_quest_aliases` sets ANAM to `max(existing ANAM, max(ref/location id)+1)` and never decreases it; collection-only or empty rewrites keep the prior ANAM. PluginEditTool JSON uses `entries: [{id, maxInitialFillCount?}, ...]`; mixing `entries` with top-level `id`/`maxInitialFillCount` fails. Collection entry ids do not advance the reference/location auto-id allocator. Existing multi-entry collections are no longer `QuestAuthoringCoverageIncomplete`. `add/remove/replace/reorder_quest_alias` refuse any result that would place two collection ranges next to each other (a later decode would silently merge them). `ALCL` and alias-item `COED` stay unmodeled on #73.

## PluginEditTool/native/src/plugin/RadiclePluginLifecycle.cpp:RadiclePluginBackend::createRecord — PROJ/EXPL/HAZD (#76)

Native `CreateRecord` now admits `PROJ`, `EXPL` and `HAZD` through Radicle's measured typed creators (`fo4::forms::create_projectile` / `create_explosion` / `create_hazard`). Payload keys are camelCase and optional except HAZD `model`, which has no attested default. Provided numeric fields are run through `fo4::schema::encode_scalar` on the generated path (`PROJ\\SoundLevel`, `EXPL\\ProjectileCount`, `HAZD\\Limit`, and the rest) so CreateRecord cannot accept a value SetField would reject. Provided FormKey slots must resolve to a loaded `graph_.winner` of the slot's type (EXPL `impactPlacedObject` accepts any loaded record); unresolved or mistyped keys fail as `InvalidTypedFormKey` before the plugin copy is written. External owners are appended via `ensureOwnerMaster` so encode does not throw on a missing MAST. Schema scalar `SetField` continues to apply to the created records because creation emits the current 93/84/52-byte payloads. Embedded FormLink editing is not in this path: the generated schema still has no single-FormLink descriptors for the PROJ/EXPL/HAZD payload slots.

## Radicle/tools/formlink_schema_codegen.py:single-FormLink descriptor audit — PROJ/EXPL/HAZD (#76)

The Phase 1 audit is anchored to `Radicle/docs/FORMAT_NOTES.md`'s measured layouts, xEdit's checked-in FO4 definitions, `Radicle/src/forms/proj_expl_hazd.cpp` and its creation/re-encode tests. Each candidate is a four-byte on-disk FormID inside a required, single-cardinality payload subrecord; every accepted slot is nullable because the xEdit definitions include `NULL` in the target union (EXPL `impactPlacedObject` uses `sigBaseObjects`, which also admits `NULL`).

| Record | Subrecord | Offset | Width | Zero legal | Target signature or union | Subrecord contract | Evidence |
|---|---|---:|---:|---|---|---|---|
| PROJ | DNAM | +16 | 4 | yes | LIGH | required, 93 bytes | measured table; xEdit `wbStruct` |
| PROJ | DNAM | +20 | 4 | yes | LIGH | required, 93 bytes | measured table; xEdit `wbStruct` |
| PROJ | DNAM | +32 | 4 | yes | EXPL | required, 93 bytes | measured table; xEdit `wbStruct` |
| PROJ | DNAM | +36 | 4 | yes | SNDR | required, 93 bytes | measured table; xEdit `wbStruct` |
| PROJ | DNAM | +52 | 4 | yes | SNDR | required, 93 bytes | measured table; xEdit `wbStruct` |
| PROJ | DNAM | +56 | 4 | yes | SNDR | required, 93 bytes | measured table; xEdit `wbStruct` |
| PROJ | DNAM | +60 | 4 | yes | WEAP | required, 93 bytes | measured table; xEdit `wbStruct` |
| PROJ | DNAM | +80 | 4 | yes | TXST | required, 93 bytes | measured table; xEdit `wbStruct` |
| PROJ | DNAM | +84 | 4 | yes | COLL | required, 93 bytes | measured table; xEdit `wbStruct` |
| EXPL | DATA | +0 | 4 | yes | LIGH | required, 52/56/60/64/84 bytes | measured table; xEdit `wbStruct` |
| EXPL | DATA | +4 | 4 | yes | SNDR | required, 52/56/60/64/84 bytes | measured table; xEdit `wbStruct` |
| EXPL | DATA | +8 | 4 | yes | SNDR | required, 52/56/60/64/84 bytes | measured table; xEdit `wbStruct` |
| EXPL | DATA | +12 | 4 | yes | IPDS | required, 52/56/60/64/84 bytes | measured table; xEdit `wbStruct` |
| EXPL | DATA | +16 | 4 | yes | sigBaseObjects | required, 52/56/60/64/84 bytes | measured table; xEdit `wbFormID`/`sigBaseObjects` |
| EXPL | DATA | +20 | 4 | yes | PROJ | required, 52/56/60/64/84 bytes | measured table; xEdit `wbStruct` |
| HAZD | DNAM | +24 | 4 | yes | SPEL or ENCH | required, 52 bytes | measured table; xEdit `wbStruct` |
| HAZD | DNAM | +28 | 4 | yes | LIGH | required, 52 bytes | measured table; xEdit `wbStruct` |
| HAZD | DNAM | +32 | 4 | yes | IPDS | required, 52 bytes | measured table; xEdit `wbStruct` |
| HAZD | DNAM | +36 | 4 | yes | SNDR | required, 52 bytes | measured table; xEdit `wbStruct` |

`PROJ DNAM+89` is intentionally not in the descriptor set. The checked-in GARDEN measured table and xEdit definition call it an unaligned PROJ FormID, while the read-only verified CommonLib audit `27-PROJ-EXPL-HAZD-DATA.md` explicitly says the same bytes are tracer-frequency/padding and must not be treated as a FormID. That is a field/document conflict, so this change does not choose a serialization rule for it. The existing typed creator and remap evidence remain unchanged until that conflict is resolved.

## Radicle/src/forms/quest_aliases_decode.cpp:decode_alias_semantics — QUST alias extra collections (#73)

`QuestAlias` now mirrors the extra FormLink collections already authored on `QuestAliasCreation`: `keywords` (`KSIZ`/`KWDA`), `items` (`COCT`/`CNTO` plus optional per-item `COED`), `linked_aliases` (packed `ALLA` rows), `spells`/`factions`/`packages` (repeatable `ALSP`/`ALFC`/`ALPC`), and the unique remaining members `SPOR`/`OCOR`/`GWOR`/`ECOR`/`ALDN`/`ALFV`/`ALDI`/`VTCK`. `decode_alias_semantics` fills those views after Fill Type decode when a plugin FormID context is present; `subrecords` stay the write authority. Dangling FormIDs follow the existing ALFR rule (`unresolved_links`, drop that one semantic entry). `COED` is not an independent list: each 12-byte extra binds to the immediately preceding `CNTO` (xEdit `wbCOED` / Mutagen `ContainerEntry.Data`). The owner/union is owner-aware: `find_record` on the same plugin types `NPC_` vs `FACT` so decode never fills both `global` and `required_rank`. Mixed items are valid. Location and collection aliases do not populate these fields.

## Radicle/src/schema/form_links.cpp:visit_coed_formids — owner-aware COED remap (#73)

`visit_link_offsets` handles `COED` before `structured_formids.inc`. Offset 0 (owner) always remaps. Offset 4 remaps only when the owner FormID resolves through the current plugin to an `NPC_` record. `FACT` leaves offset 4 as an i32 rank. Null owner or a zero union skips offset 4. Strict remap throws when the payload is not 12 bytes, when a nonzero union cannot be typed from a record in the plugin, or when the owner is neither `NPC_` nor `FACT`. Offset 4 is not registered in `structured_formids.inc`.

## Radicle/src/schema/form_links.cpp:visit_coed_formids

Owner and union dwords are snapshotted before remapping offset 0 so typing uses the old FormID encoding.

## Radicle/src/forms/quest_alias_edit.cpp:write_quest_aliases — full alias-array replace (#73)

`write_quest_aliases` replaces the contiguous post-ANAM alias span in one transaction. Collections cannot be addressed by ID through `remove_quest_alias`, so a whole-array writer is the SetQuestAliases primitive. Encodes every `QuestAliasCreation` first, then splices, then sets ANAM to `max(existing ANAM, max(ref/location id)+1)` without decreasing it. NNAM/GNAM/SNAM tail is preserved. `add/remove/replace/reorder_quest_alias` fail closed if the planned range sequence would join two collection runs.

## PluginEditTool/native/src/plugin/RadiclePluginQuest.cpp:RadiclePluginBackend::setQuestAliases / setQuestStages — file layout

The quest backend is split as a pure move (no behavior change). Shared internal declarations live in `PluginEditTool/native/src/plugin/RadiclePluginQuestInternal.hpp` (namespace `fo4ide::plugin::questinternal`; the `FO4IDE_TEST_HOOKS` hook variables are `inline` there). Each definition appears in exactly one file: `RadiclePluginQuestArgs.cpp` (JSON argument helpers), `RadiclePluginQuestFormKeys.cpp` (FormKey fill checks), `RadiclePluginQuestCondition.cpp` (CTDA/CIS parse and unrepresentable checks), `RadiclePluginQuestAliasFill.cpp` (alias Fill Type parsing), `RadiclePluginQuestExisting.cpp` and `RadiclePluginQuestExistingExtras.cpp` (existing-alias fail-closed checks), `RadiclePluginQuestLocalized.cpp` (localized string-table snapshot and CP1252 encode), `RadiclePluginQuestAliases.cpp` (`setQuestAliases`), and `RadiclePluginQuest.cpp` (test hooks, stage helpers, `setQuestStages`). The anchors below still name `RadiclePluginQuest.cpp`, but the symbols they cite now live in the files above.

## PluginEditTool/native/src/plugin/RadiclePluginQuest.cpp:RadiclePluginBackend::setQuestAliases / setQuestStages (#73, #74)

Native `SetQuestAliases` accepts `kind: reference|location|collection` (default reference), collection `entries: [{id, maxInitialFillCount?}, ...]` (single-entry `id`/`maxInitialFillCount` sugar remains; adjacent collections fail closed), numeric flags, one Fill Type among `forcedReference` / `uniqueActor` / `specificLocation` / `createReference` / `locationAliasReference` / `externalAlias` / `findMatchingNearAlias` / `findMatchingFromEvent` / `findMatchingClosest` / `referenceAliasLocation` / `externalAliasLocation` / `findMatchingLocation`, `conditions` on reference and location aliases (the #225 CTDA/CIS codec), and on reference aliases only `keywords`, `items`, `linkedAliases`, `spells`, `factions`, `packages`, nullable `spectatorOverridePackageList`/`observeDeadBodyOverridePackageList`/`guardWarnOverridePackageList`/`combatOverridePackageList` (`FLST`), `displayName` (`MESG`), `forcedVoice` (`VTYP`), `deathItem` (`LVLI`), and `voiceTypes` (`VTYP`/`FLST`/`NPC_`/`FACT`). Unknown keys fail closed, including nested `items` keys (`item`,`count`,`extra`) / `items[].extra` keys (`owner`,`global`,`requiredRank`,`itemCondition`) and `linkedAliases` keys (`keyword`,`alias`). `Radicle/src/forms/quest_aliases_encode.cpp:insert_alias_conditions` writes the CTDA/CIS run after Fill Type members and before extras / `ALCC`. `Radicle/src/forms/quest_aliases_extras.cpp:insert_alias_extras` emits those extras in Mutagen WriteRecordTypes order before `ALED` (`KSIZ`/`KWDA`, `COCT`/`CNTO`/`COED`, `SPOR`/`OCOR`/`GWOR`/`ECOR`, one packed `ALLA`, `ALDN`/`ALFV`/`ALDI`, `ALSP`, `ALFC`, `ALPC`, `VTCK`). `Radicle/src/forms/quest_aliases_decode.cpp:decode_alias_semantics` fills the matching `QuestAlias` collections so extras are no longer raw-subrecord-only on decode. `ALLA` is xEdit `wbArray`: one subrecord of 8-byte `{Keyword form ID, Alias int32}` rows, not one `ALLA` per row. `Radicle/src/schema/form_links.cpp:visit_link_offsets` remaps QUST `ALLA` at offsets `0,8,16,...` and throws on a payload not divisible by 8. CNTO items use `fo4::schema::is_sig_base_object` (`k_generated_sig_base_objects` from xEdit FO4 `sigBaseObjects`, minus `NULL`). `RadiclePluginQuest.cpp:extraAliasMembersUnrepresentable` fail-closes existing aliases whose extra members are incomplete, out of that order, or whose embedded form IDs fail the same type/count rules as authoring (`KYWD` keywords, `sigBaseObjects` items with count `>= 1`, optional 12-byte `COED` on the preceding `CNTO` with `NPC_`/`FACT`/`NULL` owner, owner-aware `GLOB`/rank union, finite `itemCondition`, `FLST` package-list overrides, `KYWD` ALLA keywords, `MESG` display name, `VTYP` forced voice, `LVLI` death item, `SPEL`/`FACT`/`PACK`, `VTCK` as `VTYP`/`FLST`/`NPC_`/`FACT`; NULL form IDs are not modeled except COED owner/global). Typed FormKeys resolve through `graph_.winner` and masters are added before encode. `PluginEditTool/native/src/plugin/RadiclePluginQuest.cpp:parseAliasFill` maps the remaining Mutagen fill structs onto those JSON objects (`CreateReferenceToObject` ALCO+ALCA with Create At=0/In=0x8000, no `ALCL` Level; `LocationAliasReference` ALFA+ALRT `LCRT`; `ExternalAliasReference` optional ALEQ `QUST` + required ALEA; Near Alias ALNA+ALNT types 0/1; From Event ALFE+ALFD; Closest ALCC on reference aliases only; location `ReferenceAliasLocation` ALFA+optional KNAM; `ExternalAliasLocation` ALEQ and/or ALEA; `FindMatchingLocation` ALFE+ALFD before location `ALCC`). Nested fill objects fail closed on unknown keys. `createReference.level` is Mutagen `Level` / xEdit ALCL: 4-byte enum Easy=0, Medium=1, Hard=2, VeryHard=3 after `ALCO`/`ALCA`. Mutagen `Quest.xml` marks ALCL `nullable=False` so encode always writes it; missing ALCL on disk decodes as Easy. Values outside 0–3 fail closed. `RadiclePluginQuest.cpp:existingFillUnrepresentable` also fail-closes existing `ALCA.create_mode` outside `{0,0x8000}`, existing `ALNT.type > 1`, and existing null `ALFR` (`QuestSpecificReferenceFill` with no FormKey), because JSON cannot author those states. `RadiclePluginQuest.cpp:aliasConditionsUnrepresentable` checks members after the CTDA/CIS run: reference aliases may only keep modeled extras (`KSIZ`/`KWDA`/`COCT`/`CNTO`/`COED`/`SPOR`/`OCOR`/`GWOR`/`ECOR`/`ALLA`/`ALDN`/`ALFV`/`ALDI`/`ALSP`/`ALFC`/`ALPC`/`VTCK`) before `ALED`; location aliases may only keep post-condition `ALCC`. `parseQuestCondition` threads `errorType` through helper branches so alias JSON returns `InvalidQuestAliasPayload`. `requireLoadedFill` resolves non-null Fill Type FormKeys through `graph_.winner` to the Mutagen-pinned target set (`IPlaced` for ALFR, `NPC_` for ALUA, `LCTN` for ALFL, `LCRT` for ALRT, `QUST` for ALEQ, `KYWD` for KNAM, any loaded major for ALCO) before `ensureOwnerMaster`. `parseEntireUnsigned32` requires the whole numeric string to be consumed and to fit `uint32`. A non-string `kind` returns `InvalidQuestAliasPayload` instead of throwing. `SetQuestStages` allowlists stage keys (`index`, `flags`, `logEntry`, `logEntries`, `complete`, `fail`) and log-entry keys (`flags`, `note`, `logText`, `conditions`). `NAM0` and wrong `logEntry`/`complete`/`fail` types fail with no write. On the `logEntry` sugar path, `complete` sets QSDT bit 0 and `fail` sets bit 1 independently (`RadiclePluginQuest.cpp:setQuestStages`), so both true yields flags `3` rather than `fail` overwriting `complete`. Localized plugins refuse new CNAM text because string-table IDs are not allocated here. Alias-level CTDA/CIS uses the same `parseQuestCondition` / `adapterConditionUnrepresentable` / `serialize_ctda_run` path as log entries (`RadiclePluginQuest.cpp:aliasConditionsUnrepresentable`). Collections reject conditions. VMAD fail-closed is unchanged. `PluginEditTool/native/tests/unit/RadiclePluginQuestAuthoringTests.cpp` covers zero/multiple alias conditions, FormKey and scalar parameters, `compareGlobal`, RunOn Reference, CIS1/CIS2, external master insertion, malformed existing CTDA, existing unsupported CIS, wrong-type and unresolved FormKeys, neighbor preservation, reload, and no-write failures. `existingStagesUnrepresentable` / `existingAliasesUnrepresentable` decode the current arrays first and return `QuestAuthoringCoverageIncomplete` without writing when the record already has a non-lossless CTDA/CIS run, a non-zero INDX unknown byte, leading members, localized CNAM, multi-entry collections, unresolved Fill Types, or alias members outside the supported ALST/ALLS/ALCS subset.

The opaque `INDX` preservation behavior is documented below; the historical coverage summary above predates that fix.

## PluginEditTool/native/src/plugin/RadiclePluginQuest.cpp:setQuestStages — per-QSDT CTDA/CIS (#74)

`fo4::forms::lossless_quest_log_entry_conditions` (`Radicle/src/forms/quest_stages.cpp:lossless_quest_log_entry_conditions`) accepts only a leading `CTDA` plus attached exact-zstring `CIS1`/`CIS2` run that `serialize_ctda_run` reproduces byte-for-byte, then leaves `NAM2`/`CNAM` to the existing order checks. That Radicle gate is not enough for whole-array replace: `RadiclePluginQuest.cpp:adapterConditionUnrepresentable` also refuses GetEventData (function 576), unmapped functions, `unknown2 != 0` / `unknown3 != -1`, operators outside 0..5, unsupported run-on, and form ID slots that do not resolve to the `ConditionFunctions::editorFor` target type. `parseQuestCondition` rejects the name `GetEventData` and enum 576, requires FormKeys (and types) for record parameters, integers for scalar/`ParametersUseAliases`/`UsePackData` slots, `cis1`/`cis2` only on declared String/Event slots, and a placed `reference` only when `runOn` is `Reference`. Existing CIS on a non-text slot, or a text slot with a nonzero raw CTDA integer, is `QuestAuthoringCoverageIncomplete`. Masters are added via `ensureOwnerMaster` before `encode_plugin_form_id`. Members are written `QSDT [CTDA/CIS...] [NAM2] [CNAM]`. VMAD guards are unchanged.

## PluginEditTool/native/src/plugin/RadiclePluginQuest.cpp:existingStagesUnrepresentable — opaque INDX byte preservation (#74)

Evidence sources: `Radicle/docs/FORMAT_NOTES.md` (the checked-in Fallout 4 corpus counts and QUST byte layout) and `PluginEditTool/Mutagen/Mutagen.Bethesda.Fallout4/Records/Major Records/Quest.xml` (the pinned stage schema). The corpus sweep found 1,336 QUST records, 8,680 stages, 9,058 log entries, no NAM0 members, and 5,533 stages with a nonzero final INDX byte. That byte remains opaque: the adapter preserves it by matching an existing stage index and writes zero only for a new stage; it does not infer semantics or expose a guessed authoring field. `PluginEditTool/native/tests/unit/RadiclePluginQuestAuthoringTests.cpp:Native SetQuestStages preserves opaque INDX bytes` covers edit, reload, and retention.

## PluginEditTool/native/src/plugin/RadiclePluginQuest.cpp:existingQuestVmadUnrepresentable — QUST VMAD fail-closed (#73, #74)

QUST VMAD extras bind Papyrus to the arrays these RPCs replace. Mutagen `QuestScriptFragment` stores `Stage` plus `StageIndex`; `QuestFragmentAlias.Property` is a VMAD object whose format-2 alias int16 names an alias ID (`Radicle/src/schema/vmad.cpp:walk_quest_extra`, `fo4::quest_vmad_bindings`). `SetQuestAliases` refuses when `alias_count > 0` (or the extras cannot be parsed). `SetQuestStages` refuses when `fragment_count > 0` (or unparseable). Empty extras (zero fragments and zero aliases) do not block either call. `PluginEditTool/native/src/plugin/RadiclePluginQuest.cpp:existingQuestVmadUnrepresentable` counts VMAD subrecords (0 safe, 1 inspected, more than one `QuestAuthoringCoverageIncomplete`) so a binding-free first VMAD cannot hide a later one. `quest_vmad_bindings` requires `VmadScan.fully_consumed` with no `trailing_data`; leftover bytes after a valid extras prefix are unknown, not safe. Remap still uses the historical tolerant `vmad_formid_offsets_for_remap` path. The check runs before JSON mutation. Richer VMAD-aware rewrite remains future #73 work.

## PluginEditTool/native/src/plugin/RadiclePluginQuest.cpp:existingQuestVmadUnrepresentable

Duplicate, trailing, unsupported, or unparseable QUST VMAD still fail closed before write. Alias/stage bindings no longer block `SetQuestAliases`/`SetQuestStages`; `write_quest_aliases` / `write_quest_stages` patch those fields in the same plugin copy or throw without committing.

## PluginEditTool/native/src/plugin/RadiclePluginQuest.cpp:RadiclePluginBackend::setQuestStages — localized CNAM encoding

Localized QUST CNAM writes in `PluginEditTool/native/src/plugin/RadiclePluginQuest.cpp:setQuestStages` use the explicit language and loose-table directory supplied by the caller, but the directory must resolve beneath a trusted editable plugin root; English text is encoded as CP1252, supported non-English text remains UTF-8, and all three allocation-participant sidecars are snapshotted and rechecked before dependency-first persistence of the changed DLSTRINGS and plugin.

## PluginEditTool/native/src/plugin/RadiclePluginBackend.cpp:RadiclePluginBackend::allowedLocalizationDirectory — sidecar path trust

Sidecar roots are canonicalized and accepted only beneath a trusted editable plugin root and outside the protected game Data directory.

## PluginEditTool/native/tests/unit/RadiclePluginQuestAuthoringTests.cpp:mutateStaleLocalizationSiblingAfterTableParse

The test-only localization hook in `PluginEditTool/native/tests/unit/RadiclePluginQuestAuthoringTests.cpp:mutateStaleLocalizationSiblingAfterTableParse` mutates ILSTRINGS after its parse, deterministically proving that the parsed table and retained freshness bytes come from the same read.

# Native Windows launch

## PluginEditTool/native/src/plugin/RadiclePluginAnalysis.cpp:RadiclePluginBackend::getPluginWorklist

The worklist walks the selected plugin's records against the existing conflict graph. Depth counts loaded definitions before this plugin; later definitions determine the winner and losing rows. Counts apply the signature filter before the kind filter and pagination. `OriginLoaded` describes whether the owning plugin is loaded, even when it has no definition of this record. `sizeArgument` clamps unsigned JSON pagination values before conversion so they cannot wrap into negative defaults.

`PluginEditTool/native/tests/unit/RadiclePluginWorklistTests.cpp` covers synthetic chains, filters, pages, a missing origin, an origin loaded without a record, unsigned pagination, unsaved copy/discard, and the committed Heather fixture. `PluginEditTool/native/tests/native_mcp_smoke.py:main` exercises the same query through the stdio host after in-memory record creation.

`PluginEditTool/native/src/assets/TextureResolver.cpp:TextureResolver::addDataRoots` retains the inherited Data-ancestor restriction. `PluginEditTool/native/tests/unit/TextureResolverTests.cpp` verifies archive resolution within Data and refusal to scan unrelated ancestors.

`PluginEditTool/native/src/assets/TextureResolver.cpp:TextureResolver::resolveFromArchives` keeps an explicit NIF-panel texture root as its own scan root when it has no Data ancestor, so a user-chosen folder such as an unpacked mod still has its own archives scanned; only the climb above it is refused. That explicit root goes to `TextureResolver::scanAndLookup` as a shallow root and `ensureRootScanned(root, false)` reads only its top level, so no spelling of a drive root, UNC share, `C:\.` or cache folder can start a recursive crawl; `PluginEditTool/native/tests/unit/TextureResolverTests.cpp:Texture resolver scans an explicit texture root without a Data ancestor only at its top level` fails if the scan recurses. `TextureResolver::ensureRootScanned` memoises shallow scans in `shallowScannedRoots_`, separate from `scannedRoots_`, so the shallow root is listed once and a later recursive scan of the same folder still runs (reachable when the texture root carries trailing whitespace, which defeats the Data-ancestor test but not the trimmed scan key); `PluginEditTool/native/tests/unit/TextureResolverTests.cpp:Texture resolver still scans a folder recursively after a shallow scan of it` fails with a shared set. `PluginEditTool/native/tests/unit/TextureResolverTests.cpp:Texture resolver scans an explicit texture root that has no Data ancestor` fails without that fallback.

`PluginEditTool/native/tests/unit/TextureResolverTests.cpp` checks a loose NIF with `std::filesystem::equivalent`, since a Windows lookup can preserve the query's casing rather than the directory entry's casing.

`Radicle/src/graph/conflict_graph.cpp:ConflictGraph::winner` selects the last loaded definition. This worklist exposes that existing graph behavior. Pinned [xEdit](https://github.com/TES5Edit/TES5Edit/blob/9fb016884bec138ea6c7b872cec831537d464c3e/Core/wbImplementation.pas#L11278) excludes partial forms in `TwbMainRecord.GetIsWinningOverride`; partial-form winner parity is not introduced by this query. No engine layout or runtime behavior is inferred from these source rules.

- `PluginEditTool/web/src/hostbridge.ts:install` supplies the existing HTTP transport for the native host's loopback HTTP page, including when WebView2 exposes its default `chrome.webview` object. The legacy C# host serves `https://app.local` and retains its registered host objects. `PluginEditTool/web/src/hostbridge.test.ts` covers both transports.

- `PluginEditTool/native/tests/native_mcp_smoke.py:main` exercises the executable with an isolated MO2 profile and settings directory. It checks plugin discovery, pending edits, failed-edit preservation, explicit save, reload, discard, and invalid startup arguments. Pass `--build-dir` and optionally `--cmake` to include the Release host build.
- `PluginEditTool/native/src/textures/BcnDecoder.cpp:bc7`, `PluginEditTool/native/src/textures/PngWriter.cpp:deflate`, and `PluginEditTool/native/src/papyrus/Pex.cpp:Reader::u16` retain their existing byte values while making integer types explicit for the MSVC warnings-as-errors build.
- `PluginEditTool/native/apps/fo4ide/main.cpp:main` loads an explicitly requested MO2 instance through the existing `appInterop.OpenMo2Profile` route before serving requests. Invalid arguments or load failures stop startup.
- `PluginEditTool/native/apps/fo4ide/McpTools.cpp:registerPluginTools` exposes existing native plugin operations. `save_plugin` uses the existing in-place SavePatch route. `reload_mo2_profile` reloads the active profile and fails closed on pending edits unless `discard_pending` is true. Desktop and stdio sessions have separate pending state; reload after another session saves. Record creation retains the backend's supported-type and payload restrictions. `get_record` returns the existing record matrix; `list_records_summary` supplies decoded grid values and editable column paths.
- `PluginEditTool/native/include/fo4ide/graph/NodeDefinition.hpp:NodeDefinition::pin` and `PluginEditTool/native/src/graph/GraphDocument.cpp:GraphDocument::node` use distinct parameter names to avoid member-shadowing errors in the MSVC build.

`PluginEditTool/native/src/plugin/RadiclePluginAnalysis.cpp:RadiclePluginBackend::getPluginWorklist` upper-cases the optional signature before matching, so an MCP caller passing `misc` gets the MISC rows instead of a silent empty page; `PluginEditTool/native/tests/unit/RadiclePluginWorklistTests.cpp:Plugin worklist filters by kind and signature and pages without changing counts` checks the lowercase form.

## PluginEditTool/native/src/plugin/RadiclePluginPending.cpp — #174C pending lifecycle

`stagePluginMutationLocked` keeps ordinary edits in memory. Localized QUST CNAM table changes stage DLSTRINGS bytes on `LoadedPlugin::stagedLocalization` and mark the plugin dirty; `SavePlugin`/`masters.SavePlugin` flush through `persistPluginLocked`, which rechecks STRINGS/DLSTRINGS/ILSTRINGS freshness then uses `write_file_pair_dependency_first`. `DiscardPlugin` restores `originalPlugin`, clears staged sidecars, and writes nothing. `RadiclePluginBackend.cpp:getPlugins` and `getActivePlugins` expose `Pending`. MCP `list_plugins` keeps the existing `GetPlugins` list and adds that flag. MCP `discard_plugin` maps to `backend.DiscardPlugin`. Subsequent localized edits reuse `stagedLocalization.dlstringsBytes` as the working table while retaining the original on-disk bytes as the freshness baseline (`RadiclePluginQuest.cpp:setQuestStages`). `RadiclePluginBackend.cpp:loadEnvironment` and `AppInteropTarget.cpp:loadProfile` refuse to replace the plugin set while any plugin is dirty unless `discardPending` is true (`PluginHasPendingChanges`). `loadEnvironment` captures `mutationEpoch_` at consent, parses the candidate snapshot off-lock, then re-checks dirty state / epoch inside the final commit lock before `plugins_ = std::move(loaded)`. A mutation staged after the first check therefore fails closed instead of being swapped away. `discardPending` only covers edits that existed at consent; a later epoch bump still fails closed. `stagePluginMutationLocked` / `stageLocalizationMutationLocked` increment that epoch. AppInterop `AppInteropTarget.cpp:loadProfile` runs `environment.LoadMo2` before writing settings, trusted create-plugin root, snapshot, or watch stamps. A backend `LoadMo2` failure leaves the previous profile/config/root in place. If `LoadMo2` succeeds and `setTrustedNewPluginRoot` or `store_.save` then fails, `restoreLoadedProfile` attempts both restorations independently (previous backend environment or an empty plugin list, then previous trusted root) even if one fails, and the caller returns the original error with any rollback failure appended; `settings_` is restored on save failure and snapshot/watch stamps stay uncommitted. A failed pending query still fails closed (`PendingStatusUnavailable` / the GetPlugins error). MCP `reload_mo2_profile` and desktop Load Env / Open MO2 use that same explicit discard path.

## PluginEditTool/native/src/app/AppInteropTarget.cpp:captureWatchFiles — #54 MO2 freshness/reload

`AppInteropTarget.cpp:captureWatchFiles` stamps `ModOrganizer.ini` as `WatchKind::config` and `plugins.txt` / `modlist.txt` / `loadorder.txt` as `WatchKind::loadOrder`. `getMo2Status` reports `staleConfig`, `staleLoadOrder`, `stale` (either), and `pendingPlugins` from the same GetPlugins list. Mutating those files does not replace `snapshot_`; only `reloadMo2Profile` / `loadProfile` re-reads the profile and sends `environment.LoadMo2`. A blocked or failed reload, including trusted-root or settings-store failure after `LoadMo2`, leaves the previous snapshot, watch stamps, persisted settings, backend profile, and trusted create-plugin root in place.

## Radicle/tools/corpus/wthr_corpus_inventory.py

Proof-first WTHR corpus scanner for Issue #284. Walks top-level `WTHR` GRUPs in Fallout4.esm + DLC masters, records every observed subrecord signature/order/cardinality/payload size, and writes markdown+JSON evidence under `Radicle/docs/`. Does not infer field semantics from names; FormID-looking slots are observations only until target type/nullability/remap are proven separately.

## Radicle/tools/corpus/wthr_formid_resolve.py

Companion to the WTHR corpus inventory for Issue #284. Resolves packed `u32` values at fixed offsets in `MNAM`/`NNAM`/`GNAM`/`TNAM`/`IMSP`/`WGDR`/`SNAM`/`UNAM` against the master FormID index. Emits hit counts only; does not assign semantic field names without a runtime citation.

## Radicle/include/fo4/forms/weather_concept.hpp + weather_concept.cpp

Issue #284 WTHR read-only decode and MNAM mutation. Named `weather_concept` because the Cursor RE gate required a concept-level filename until f4-re-mcp verification was recorded under `Radicle/docs/re-private/`. Field names stay subrecord-mechanical (`mnam`/`nnam`/...) while docs cite `TESWeather` members. Runtime proof: f4-re-mcp `TESWeather::Load` RVA `0x48c2f0` id 731579; corpus + FO4-only FormID resolution in `Radicle/docs/WTHR_*.md`.

## Radicle/src/schema/form_link_mixed.cpp:packed_formid_array_offsets — #287 packed FormID arrays

xEdit FO4 (`TES5Edit/TES5Edit` pin `9fb016884bec138ea6c7b872cec831537d464c3e`):

- `Core/wbDefinitionsFO4.pas:12539` `wbArray(MNAM, 'Target OMOD Keywords', wbFormIDCk('Keyword', [KYWD]))`
- `:12540` `wbArray(FNAM, 'Filter Keywords', wbFormIDCk('Keyword', [KYWD]))`
- `:8837` `wbArray(PNAM, 'Slot Parents', wbFormIDCk('Parent', [EQUP]))`
- `:8683` `wbArray(TNAM, 'Music Tracks', wbFormIDCk('Track', [MUST, NULL]))`

These are uncounted `wbArray` members inside one subrecord, not `wbRArray`. Mutagen writes the same packed shape (`AObjectModification_Generated.cs:2767` `TargetOmodKeywords`/`MNAM`, `:2777` `FilterKeywords`/`FNAM`; `EquipType_Generated.cs:1511` `SlotParents`/`PNAM`; `MusicType_Generated.cs:1614` `Tracks`/`TNAM`).

Fallout4.esm from a local Fallout 4 install (not committed) uncompressed top-level groups, 2026-09-13: OMOD 2409 / MNAM present 1615 (1326×4, 144×8, 145×12) / FNAM present 70 (67×4, 2×8, 1×12); EQUP 38 / PNAM present 23 (3×4, 17×8, 3×12); MUSC 111 / TNAM present 111 (4 through 108). Every present payload is one subrecord and `size % 4 == 0`. Multi-entry arrays are skipped by `formid_subrecords.inc` (4-byte only).

`Radicle/src/schema/form_links.cpp:visit_link_offsets` dispatches `packed_formid_array_offsets` after IPDS/DOBJ and before the empty/global signature table. Foreign uses of the same signatures stay on their existing routes (NAVM PNAM vertices, QUST FNAM flags, MUST FNAM floats, NPC_ PNAM 4-byte listed FormID). Mixed NAVM/PROJ/EXPL/HAZD/OBTS/PERK helpers moved with this dispatch into `form_link_mixed.cpp` so `form_links.cpp` stays under the translation-unit size limit.

`Radicle/tests/test_packed_formlink_arrays.cpp` covers remap, fail-closed odd lengths, empty arrays, collision controls, rekey cache invalidation, and document order.

For `Radicle/src/schema/form_link_mixed.cpp:packed_formid_array_offsets`, the local commonlib_NonVR reference confirms the distinct runtime form types for OMOD, EQUP, and MUSC. Runtime member shape does not independently prove plugin subrecord serialization, xEdit definitions, or quoted RVAs; those claims remain concept-level here and are not shipped as runtime offset evidence.

## PluginEditTool/native/src/plugin/RadiclePluginPending.cpp:RadiclePluginBackend::OpLock — #82 per-plugin write locks

The single `mutex_` is gone. Each backend operation now takes one `OpLock` at the old `std::scoped_lock` site, and the locks are always acquired in this order: `lifecycleMutex_` → owner `LoadedPlugin::writeMutex`es → `stateMutex_`. `saveIoMutex_` sits after the owner locks and is taken only inside `StateReleased`, never while holding `stateMutex_`.

- `Mode::Read` (getters, analysis, grids): shared lifecycle, then shared state. Readers never take owner locks.
- `Mode::Write` (every edit, `savePlugin`, `saveMastersPlugin`, `discardPlugin`): shared lifecycle. It then resolves the named plugins under a short shared state lock, releases it, and locks the owners in canonical `plugin.key` order (address breaks ties), with duplicates removed. Finally it takes shared state for the compute phase. Plugins that aren't loaded are skipped, and the operation still reports its usual `PluginNotLoaded`. Every copy also locks the sources it was given by name: `copyAsOverride`, `deepCopyAsOverride`, `RadiclePluginMaintenance.cpp:invokeLegacyBackend` `CopyAsNewRecord`, and `RadiclePluginBatchMutation.cpp:copyAsOverrideMany` (every item's `source`, which is why `OpLock` takes a `std::vector<std::string_view>` rather than an `initializer_list`). Every other write locks only the plugin it stages into.
- `RadiclePluginPending.cpp:RadiclePluginBackend::OpLock::OpLock` holds the owners as `std::unique_lock`s. If a later acquisition throws (another owner, or shared state; MSVC's `std::mutex::lock` throws `resource_deadlock_would_occur` on a recursive lock), the catch clears them before it releases lifecycle, so no owner stays locked forever. `~OpLock` likewise clears them before releasing lifecycle, because a Lifecycle op may destroy the `LoadedPlugin` that owns the mutex. No regression test: the only hooks (`setWriteOpLockedHook`, `setSaveIoLockedHook`) fire after every lock is held, so nothing can throw between owner acquisitions without a new hook.
- `RadiclePluginAnalysis.cpp:RadiclePluginBackend::getPluginWorklist` uses `Mode::Read` like its siblings `getDependencies`/`getProblems`/`getHistory`; it came from main with the old `std::scoped_lock lock(mutex_)` and failed to compile after the merge. Every other function main added after the branch was cut needs the same treatment, because the global `mutex_` they were written against no longer exists: `RadiclePluginCheck.cpp` `checkForErrors`, `lootCleaningReport` and `findUnreachable`, and `RadiclePluginCompare.cpp:comparePluginTo`, take `Mode::Read` (they only read `plugins_` and the bytes of a loaded plugin, and the shared state hold is the consistent snapshot they already assumed); `RadiclePluginDisabledOverride.cpp:copyAsDisabledOverride` and `RadiclePluginMasters.cpp` `cleanUnusedMasters`/`updateOnam` stage a mutation, so each takes `Mode::Write` with the plugin it stages into. `copyAsDisabledOverride` calls `copyAsOverride` first, which takes and releases its own op, so the two ops do not nest. The compiler is the only guard here: the merge is textually clean and the call sites only fail at `-WX`. Verified at the merge head with MSVC Release and `RADICLE_WARNINGS_AS_ERRORS=ON`: 1624/1624 `ctest` cases pass, including `RadiclePluginConcurrencyTests` and `RadiclePluginPendingLifecycleTests`.
- `Mode::Lifecycle` (`loadEnvironment` commit, `openPlugin`, `createPlugin`, `discardCreatedPlugin`, `setTrustedNewPluginRoot`, `setDataDirectory`, `setLight`): exclusive lifecycle. That holder implicitly owns all state, because every state holder also holds shared lifecycle. `setLight` stays here because `apply_master_kinds` rewrites every loaded plugin.
- `CommitScope` upgrades the current thread's op from shared to exclusive state, or from none to exclusive, and restores the previous hold on exit. `stagePluginMutationLocked`, `stageLocalizationMutationLocked`, the `discardPlugin` restore and the field flip in `persistPluginLocked` run inside it. `RadiclePluginQuest.cpp:setQuestStages` wraps the localization stage and the plugin stage in one scope so readers never see one without the other. Nested scopes are no-ops.
- `RadiclePluginPending.cpp:RadiclePluginBackend::persistPluginLocked` holds the backend-wide `saveIoMutex_` from the sidecar check through `last_write_time`, then releases it before its `CommitScope`. Saves of different plugins can resolve to one file, for example a Save As onto another loaded plugin's `.esp` while that plugin saves in place. Without this mutex their IO interleaved: the file could end up holding one plugin next to the other's new DLSTRINGS, and the in-place saver recorded the other writer's mtime, which fooled `fileUnchangedSinceLoad`. With it, the last writer wins and the loser's recorded mtime no longer matches, so its guard fires. Main had this for free from the global `mutex_`. The mutex never cycles with the other locks because nobody holds state while waiting for it. Regression: `PluginEditTool/native/tests/unit/RadiclePluginConcurrencyTests.cpp:"Saves that resolve to one file serialize their file IO"` parks the in-place save inside the IO window via `setSaveIoLockedHook` (`FO4IDE_TEST_HOOKS`). It checks that the Save As cannot enter the window and that the file ends with the Save As bytes. With the mutex replaced by a per-call mutex, the test fails on all four checks.
- `StateReleased` drops shared state for the file IO in `persistPluginLocked`. The owner lock keeps other writers away from that plugin. Readers may still read `loaded.plugin` concurrently, which is safe because both sides only read. Under a Lifecycle op both helpers do nothing.
- `OpLock::current` is thread_local, so the helpers find the enclosing op without changing any `*Locked` signatures. Ops never nest, because the old non-recursive mutex already ruled that out.

Lifetime: `plugins_` is only reshaped (assigned, `push_back`, `erase`) under exclusive lifecycle. A writer keeps shared lifecycle from name resolution until it releases its owners, so the `writeMutex` pointers and `LoadedPlugin&` references it holds cannot dangle. A name can't be removed and re-added between resolve and lock (ABA). `writeMutex` is a `unique_ptr`, so the vector can move `LoadedPlugin` without moving the mutex.

Write skew: a writer computes against a consistent snapshot under shared state. The upgrade in `CommitScope` unlocks and relocks, so another plugin's writer can commit in that gap. A computation that read *other* plugins or `graph_`, such as `resolveListValue`, `createMergedPatch` or `revertOverrides` choosing winners, may therefore commit against a slightly newer neighbour. That is the accepted trade-off. The target plugin itself cannot change in the gap, because its owner lock is held. Copies also lock a source they were given by name. `deepCopyAsOverride` with no source auto-picks one under shared state only.

Starvation: libstdc++ `shared_mutex` sits on reader-preferring `pthread_rwlock`, so a continuous stream of readers could delay a commit. MSVC's SRW lock makes no fairness promise either. UI and MCP traffic is bursty, so this is not expected in practice.

`setWriteOpLockedHook` (`FO4IDE_TEST_HOOKS`) fires after a Write op holds all its locks. `PluginEditTool/native/tests/unit/RadiclePluginConcurrencyTests.cpp` uses it to show that a second same-plugin writer cannot enter its write op while the first is parked there (`Writers to the same plugin enter their write ops one at a time and keep both edits"` checks `entered == 1` after a 200 ms grace and both edits afterwards; a "second has not finished" timing check would pass without owner locks too, since the second writer would then block in `CommitScope`), and that a writer on another plugin enters its op while the first is parked. Unrelated writers overlap in compute, but their commits still wait for exclusive state. A third test runs reversed Left↔Right `CopyAsOverride` loops to check there is no lock-order deadlock; it passes `allowLosing=true`, since after the merge with main's #40 losing-override check the Right→Left copy would otherwise only report `LOSES`.

The test file builds its plugins in process, so its fixtures are the record keys it writes: `makeLocalPlugin` writes a header with no master and one MISC group holding two local records, `000800` and `000801`, and `makeOverridePlugin` writes a header naming `Master.esm` and a single MISC override at the form ID it is handed. Both hooks run on the worker thread inside the op, so a parked `setWriteOpLockedHook` means that op holds its lifecycle, owner and state locks, and a parked `setSaveIoLockedHook` means it holds the save-IO lock. That is what lets a test park one writer and watch what another one does; a writer parked with the hook is never merely waiting for a lock it has not taken yet.

`PluginEditTool/native/tests/unit/RadiclePluginConcurrencyTests.cpp:"A losing-override check sees no later-plugin commit during its compute phase"` pins compute-phase cross-plugin read isolation. Writer A copies `000800` Src→Left and parks in its Write op; writer B copies the same key Donor→Late, a later plugin, with disjoint owners. B must not finish while A is parked, because B's commit needs exclusive state and A holds it shared, so A's `laterOverridesLocked` check sees Late before B's override and reports `Copied`, not `LOSES`. With the hook changed to drop shared state while parked, the test fails on both checks. It does not show that the check and the commit are atomic: `CommitScope` unlocks and relocks, so B can commit between A's `laterOverridesLocked` and A's commit, leaving Left's override losing to Late despite `allowLosing=false`. That outcome is serializable (A then B), so nothing should rely on the check being atomic with the commit.

Pre-existing race, fixed in the follow-up to PR #321 (branch `fix/82-backend-races`): `RadiclePluginBackend.cpp:RadiclePluginBackend::loadEnvironment` used to compute the Save-As roots with `isEditable`, which reads `dataDirectory_`, before it took the Lifecycle lock, while `setDataDirectory` writes that field under Lifecycle. The Lifecycle `OpLock` is now taken above the roots loop, so the read happens under the lock; the order is unchanged (the earlier Read op is released before it). In production this could not fire: the only `setDataDirectory` caller, `apps/fo4ide/main.cpp`, runs before any RPC target is registered. There is no regression test, because no test hook sits between the roots loop and the lock, so the old and new orderings look the same from outside. The fix needed `RadiclePluginBackend.cpp` split first to get under the 750-line limit (see the next section).

`RadiclePluginMasters.cpp:RadiclePluginBackend::setLight` (same follow-up) now bumps `mutationEpoch_` when it marks the plugin dirty, like `stagePluginMutationLocked` and `stageLocalizationMutationLocked`. Before, a `SetLight` landing between `loadEnvironment`'s consent (the Read op that captures `consentEpoch`) and its Lifecycle commit left the epoch unchanged, so a `discardPending=true` reload passed the staleness check and silently dropped the edit. It does not call `stagePluginMutationLocked`, because that rebuilds the graph and `setLight` must rebuild again after `apply_master_kinds`. Regression: `PluginEditTool/native/tests/unit/RadiclePluginPendingLifecycleTests.cpp:"LoadMo2 discardPending does not drop a SetLight made after consent"` parks the reload with `setLoadMo2AfterPendingCheckHook`, runs `masters.SetLight`, and expects `PluginHasPendingChanges`. It fails without the epoch bump.

## PluginEditTool/native/src/plugin/RadiclePluginBackend.cpp — split into Queries / Conditions (#82)

`RadiclePluginBackend.cpp` was 1299 lines, over the 750-line source limit, so no fix could land in it. It was split by responsibility with no behaviour change:

- `RadiclePluginQueries.cpp`: the read-only query and conflict APIs `getEditablePlugins`, `getActivePlugins`, `getRecordTypeIndex`, `getLoadOrderSummary`, `getConflicts`, `getConflictMatrix`, `getReferencedBy`, `searchRecords`, `scanConflicts`, `scanBrokenRefs` and `listMasters`, plus their only-user helpers `pluginKind`, `containsInsensitive`, `subrecordValue`, `nthSubrecord`, `matrixStatuses`, `matrixDiffers` and `buildRawMatrixRows`.
- `RadiclePluginConditions.cpp`: `getConditions`, `getConditionsAtPath`, `getConditionFunctions` and `getConditionRunOnTypes`.
- `RadiclePluginSupport.hpp` (namespace `fo4ide::plugin::support`): the helpers those three files share, `stringArgument`, `editorId` and `asciiLower`, as `inline` functions, following the `RadiclePluginLifecycleFields.hpp` internal-header pattern. Other `RadiclePlugin*.cpp` files keep their own anonymous-namespace copies; folding them in is a separate cleanup.
- `RadiclePluginBackend.cpp` keeps dispatch (`invoke`), the save-path trust checks, `loadEnvironment`, `getPlugins`/`getChildren`/`openRecord`, `openPlugin`/`savePlugin`/`discardPlugin`, `findPlugin` and `rebuildGraphLocked`.

Comments that moved out of the source with the code:

- `RadiclePluginQueries.cpp:RadiclePluginBackend::listMasters` computes master usage conservatively. A master is "used" when a record in the plugin is owned by it (an override), when a FormLink resolves into it, or when the plugin has VMAD that cannot be fully scanned. In that last case the link set may be incomplete, so every master is reported used rather than telling the user a load-order-relevant master is droppable. Each row exposes the resolved `kind` so clients and tests can catch a full/light mis-stamp before a later remap bakes in wrong FormIDs.
- `RadiclePluginConditions.cpp:RadiclePluginBackend::getConditions` resolves the record by FormKey text first, then by editor ID over the loaded set, mirroring `WriteService.GetConditionsJsonCore` / `ResolveFk`. A `FormKey::parse` failure means the argument is not FormKey text, so it falls through to the editor-ID scan.

## Radicle/src/environment/environment.cpp:fo4::env::Environment — GardenEngine #114 load-order environment

First slice of garden-engine/GardenEngine#114 (replace Mutagen with Radicle). It stands in for the Mutagen `ILinkCache`/`GameEnvironment` role that `opencw/Services/GameLoader.cs` and `Mo2ProfileLoader.cs` build. MO2 parsing stays in C#, which passes the ordered plugin paths in.

- **Load.** `Environment::load(paths)` reads every path in order and stamps master light/full kinds from the loaded set, as `RadiclePluginBackend::loadEnvironment` does. It then indexes every record version by `FormKey`. The load fails closed on an unreadable file, a duplicate filename, or the same FormKey twice inside one plugin. The engine's behaviour for an in-plugin duplicate is unproven, so the index refuses it instead of picking one copy.
- **Unresolved masters.** A master that does not load earlier than its dependent plugin is reported in `PluginInfo::unresolved_masters`, not rejected. Mo2ProfileLoader already prepends the implicit masters and throws only when `Fallout4.esm` is missing.
- **Versions.** `versions()` is ordered lowest priority first, with the winner last. This matches `GameLoader.GetRecordContexts`, which reverses `ResolveAllSimpleContexts`.
- **EditorID lookup.** `find_by_editor_id` matches the vendored Mutagen link cache (`Mutagen.Bethesda.Core/Plugins/Cache/Internals/Implementations/Internal/InternalImmutableLoadOrderLinkCache.cs` and `ImmutableLoadOrderLinkCacheCategory.cs`):
  - It walks plugins from the highest priority down.
  - It indexes every record *version* under that version's own EDID; the first match wins.
  - Matching is `StringComparer.OrdinalIgnoreCase`.
  - Blank or whitespace-only EDIDs never match.
  - Consequence: an EDID that a later override renamed still resolves to the older, lower-priority version, exactly as `GameLoader.ResolveCellId` sees it today.
- **`winners_of(signature)`.** Groups winners by the highest-priority plugin that holds them, then by on-disk order. This is the output order of Mutagen's `OverrideMixIns.WinningOverrides` over `PriorityOrder`.
  - One difference: `WinningOverrides` skips deleted winners by default, but `winners_of` keeps them. Callers filter on `RecordMetadata::deleted`.
  - GardenEngine's current callers (`ListWorldspaces`, `CollectUniqueNifPaths`) enumerate each mod's records and resolve the winner themselves; they do not call `WinningOverrides`.
- **`metadata()`.** Decodes only the generic surfaces:
  - EDID and MODL as NUL-terminated strings; a malformed payload is reported as absent.
  - OBND as six `int16`.
  - The deleted flag, bit 5.
  - FULL. In a localized plugin, FULL is always a four-byte string ID: xEdit FO4 defines one `wbFULL` lstring for every record type, and Mutagen reads every FULL through `StringsSource.Normal`. Otherwise FULL is inline text.
  - String-table resolution and record-specific model fields (ARMO world models, SCOL) are later #114 slices. `ModelPathResolver.PrimaryModelPath` special-cases both.

`Radicle/src/c_api_env.cpp` is the C ABI over this (`fo4/c_api_env.h`):

- Records are borrowed `radicle_record*` handles into the immutable index. All strings point into plugin bytes, so there are no per-query allocations except the record arrays, which are freed with `radicle_free_record_array`.
- It shares `radicle_last_error` and the error helpers with `c_api.cpp` through `src/c_api_internal.hpp`.

`Radicle/tests/test_environment.cpp` covers:

- winner and version order;
- EDID rename and case handling;
- enumeration order;
- light, master and unresolved-master reporting;
- fail-closed loads;
- FULL/OBND/deleted decoding;
- the C ABI round trip and its error statuses.

## Radicle/src/environment/world.cpp:fo4::env::decode_worldspace / decode_cell — GardenEngine #114 WRLD/CELL read surface

Second #114 slice, stacked on the environment slice. It covers what `WorldspaceService`, `CellService` and `CellCompiler` read from WRLD/CELL.

**Containment index.** `Environment::load` walks the groups itself, instead of `visit_records`, and stamps every `RecordRef` with three things:
- the type of the group that directly holds it;
- the enclosing WRLD, taken from the world-children group label (type 1);
- the enclosing CELL, taken from the cell-children group label (type 6).

Labels resolve through the plugin's own master table, so master-owned worlds and cells land on the right FormKey. An unresolvable or null label fails the load. After the walk, per-container member lists are built in load order, so the hierarchy queries never scan the whole load order.

**Hierarchy queries:**
- **`worldspace_top_cell`**: the CELL held directly by the world-children group, taken from the highest-priority plugin.
- **`worldspace_cells`**: every CELL in an exterior sub-block (type 5) under the world. Each entry is the version from the highest plugin that lists it, ordered by that plugin and then on-disk order. `WorldspaceService.ListExteriorCells` iterates WRLD overrides from low to high priority and keeps the last cell per grid; iterating this list and keeping the last cell per grid gives the same result.
- **`interior_cells`**: winning CELLs whose winning version sits in an interior sub-block (type 3).
- **`cell_children`**: mirrors `CellService.GetPlacedReferencesJson`. It walks the persistent (8) and temporary (9) children of every version of the cell, from low to high priority:
  - a later copy replaces an earlier one;
  - a copy with the deleted flag removes the child;
  - the list keeps first-insertion order;
  - every signature is returned (LAND and NAVM included); consumers filter by signature;
  - a child whose winning version sits in another cell is left out, so a reference a later plugin moves appears only in its new cell.
- **`cell_landscape`**: mirrors `WorldspaceService.ResolveWinningLandscape`. It returns the LAND held by the highest-priority cell version that has one.

**Layouts**, from xEdit FO4 at pin `9fb016884bec138ea6c7b872cec831537d464c3e`: `Core/wbDefinitionsFO4.pas` `wbRecord(WRLD` and `wbRecord(CELL`, plus `Core/wbDefinitionsCommon.pas` `wbWorldLandData`, `wbWorldObjectBounds`, `wbCellGrid`, `wbAmbientColors` and `wbByteColors`.
- **WRLD:**
  - PNAM is a `u16` of parent-use flags.
  - DNAM is two floats: default land height and default water height.
  - NAM0 and NAM9 are two floats each. They are returned as stored; xEdit only scales them by 1/4096 for display.
  - DATA is a `u8` of flags.
- **CELL:**
  - DATA is a `u16` of flags. A one-byte DATA is accepted and zero-extended.
  - XCLC is `i32 x`, `i32 y`, then an optional `u8` of land flags and 3 unused bytes, so it is 8 or 12 bytes.
- **CELL XCLL:**
  - Ten fixed members come first: three RGBx byte colors (ambient, directional, fog near), fog near, fog far, directional rotation XY and Z (`i32`), directional fade, clip distance and fog power.
  - Then the directional ambient block: six RGBx colors, plus specular and fresnel power from form version 30. That is 24 bytes before form version 30 and 32 bytes from it.
  - Then the optional tail: fog far color, fog max, light fade begin and end, and `u32` inherits.
  - A payload shorter than its required members fails closed.
  - The tail members beyond inherits are not decoded yet.

**Checked against the vendored Mutagen** (`opencw/third_party/Mutagen`, `Mutagen.Bethesda.Fallout4/Records/Major Records/`):
- **NAM0/NAM9.** `Worldspace.cs` `FillBinaryObjectBoundsMinCustom` / `…MaxCustom` divide the stored floats by 4096. Radicle returns them as stored, so the GardenEngine binding must divide by 4096 to reproduce `ObjectBoundsMin` and `ObjectBoundsMax`.
- **XCLL.** `CellLighting_Generated.cs` always reads a 32-byte `AmbientColors`. Every Fallout 4 record has form version 30 or higher, so the xEdit form-version rule used here produces the same offsets. The member order and the Inherits bit values match `CellLighting.cs`.
- **CELL DATA.** Read at its actual length, 1 or 2 bytes, as done here.
- **XCLC.** Mutagen reads 12 bytes and keeps a raw 4-byte value in `CellGrid.Flag`. Only byte 8 carries the land flags (xEdit `wbCellGrid`: `u8` plus 3 unused bytes), and that byte is what is exposed here.

`Radicle/src/c_api_world.cpp` is the C ABI over this: `radicle_worldspace_get_info`, `radicle_cell_get_info`, `radicle_record_get_container`, and the hierarchy queries. Link fields borrow their owner name from `Environment::stable_owner`, a set of every plugin and master name. A link whose plugin is NULL is absent. `Radicle/tests/test_environment_world.cpp` builds a two-plugin WRLD/CELL fixture covering: top cell, exterior block/sub-block, interior block, persistent and temporary children, a deleted child override, and a LAND override. It also tests malformed and wrong-signature decodes and the C ABI.

## Radicle/src/environment/placed.cpp:fo4::env::decode_placed_reference / primary_model_path — GardenEngine #114 placed references and model paths

Third #114 slice, stacked on the WRLD/CELL slice. It covers every placed-reference field `CellService.GetPlacedReferencesJson` and `ReferenceWriteback` touch, plus `ModelPathResolver.PrimaryModelPath`.

**Scope.** `decode_placed_reference` accepts REFR, ACHR and the placed trap family (PARW, PBAR, PBEA, PCON, PFLA, PHZD, PMIS, PGRE). This matches the record types Mutagen's `Cell.ParseTypical` dispatches.

**Sections.** The decode is sequential because several signatures change meaning with the anchor before them (xEdit FO4 `wbRefRecord(REFR` / `wbRefRecord(ACHR` at pin `9fb016884bec138ea6c7b872cec831537d464c3e`):
- `XMRK` opens the map-marker section: `FNAM` flags, `FULL` name, `TNAM` type.
- `XRMR` opens the room section: `LNAM` lighting template, `INAM` image space, `XLRM` linked rooms. Mutagen's `FillBinaryBoundDataCustom` reads the same members after XRMR.
- `XPRD` opens a patrol: `XPPA`, `INAM` idle, `PDTO` topics, and on ACHR a `TNAM` topic.

Any of those signatures outside its section fails closed. Every other listed field is sized exactly: a wrong size or an unexpected repeat throws `WorldDecodeError`. Unknown subrecords are ignored (they stay in the plugin untouched).

**Layouts** agree between xEdit and the vendored Mutagen:
- **Link and flag fields:**
  - `XESP`: 4 or 8 bytes, FormID then `u8` flags.
  - `XOWN`: 4 or 12 bytes, FormID, 4 unused bytes, then a no-crime byte.
  - `XLKR`: 8 bytes as keyword + reference, or 4 bytes as keyword only, leaving `reference` empty (Mutagen `LinkedReferencesBinaryCreateTranslation.FillBinaryStructs` fills `KeywordOrReference` and stops at the `<Break>`).
  - `XAPR`: FormID + `f32` delay.
- **Geometry fields:**
  - `XTEL`: door, position, rotation, `u32` flags, and at 36 bytes the transition-interior CELL. xEdit lists that member; a 32-byte form is also accepted.
  - `XPRM`: bounds `vec3`, colour RGB floats plus one float Mutagen reads as `Unknown` (`PlacedPrimitive_Generated.cs`, `ColorBinaryType.NoAlphaFloat`), `u32` type. `Primitive::color[3]` and `radicle_placed_ref.primitive_color[3]` hold that float, not alpha. Bounds are returned as stored; Mutagen `PlacedPrimitive.FillBinaryBoundsCustom` and xEdit both double them.
  - `XBSD`: slack, thickness, half extents, then an optional wind-detached byte.
  - `XPTL`: width, height, position, quaternion.
- **Comments.** `MNAM` holds comments on REFR and the traps.

**Scripts.** `VMAD` goes through the new `fo4::read_vmad_scripts` (`Radicle/src/schema/vmad.cpp`), which shares the existing VMAD walker:
- It returns script names, flags and properties.
- Values are kept for object (FormID and alias), string, int, float and bool. Structs and arrays report their leading count.
- A Variable property (type 6 or 16) stops decoding with `unsupported` set. Mutagen throws `NotImplemented` there.

**Model path.** `primary_model_path` mirrors `ModelPathResolver.PrimaryModelPath`:
- SCOL has none.
- ARMO uses its world model: `MOD2` (male), then `MOD4` (female). ARMO `MODL` is an armature FormID, not a path.
- Only the model-bearing signatures in `primary_model_path`'s `modeled` list use their first `MODL`: ACTI, ADDN, ALCH, AMMO, ANIO, ARTO, BOOK, BPTD, CAMS, CLMT, CONT, DOOR, EFSH, EXPL, FLOR, FURN, GRAS, HAZD, HDPT, IDLM, INGR, IPCT, KEYM, LIGH, LVLN, MATO, MISC, MSTT, NOTE, OMOD, PROJ, STAT, TACT, TERM, TREE, WEAP. These are the FO4 major records whose Mutagen getter implements `IModeledGetter` (checked in the vendored `Mutagen.Bethesda.Fallout4/Records/Major Records/*_Generated.cs`), which is the test GardenEngine's Mutagen `ModelPathResolver.PrimaryModelPath` used. RACE, ARMA, WTHR and WRLD have `Model` fields but do not implement it, so they report no model.
- The text is cut at the first NUL, like Mutagen's zero-terminated string read. Empty or unterminated text counts as no model.

`Environment::metadata().model` now uses the same rule.

**C ABI.** `Radicle/src/c_api_placed.cpp` provides `radicle_placed_ref_decode` / `radicle_placed_ref_free`:
- It returns one heap view that owns every array.
- The opaque `internal` pointer lets the free call find that owner.

`radicle_record_primary_model_path` exposes the model rule.

**Tests.** `Radicle/tests/test_environment_placed.cpp` covers:
- a REFR carrying every field;
- ACHR patrol and level modifier;
- a PHZD;
- section and size failures;
- a Variable VMAD;
- model-path parity;
- the C ABI.

## Radicle/src/environment/environment.cpp / placed.cpp — review repairs on main after #322-#324 (GardenEngine #114)

#322-#324 were merged to `main` without the fixes that later stack PRs carry. This branch ports four of #336's fixes and fixes the review findings on the merged code. Each change has a regression test.

Source: the vendored Mutagen in GardenEngine `opencw/third_party/Mutagen` (files cited per item) and the regression tests named per item, each of which fails against `main` at `a041442e`.

- `Radicle/src/environment/environment.cpp:decode_record_name` ignores a `FULL` between `OBTE` and `STOP`. Mutagen reads that span as the `ObjectTemplates` list (`Armor.xml`, `counterRecType="OBTE"`, `endMarker="STOP"`), whose entries carry their own `FULL` name (`ObjectTemplate.xml`). Ported from #336. Test: `test_environment.cpp:NameIgnoresObjectTemplateCombinationFull`.
- `Radicle/src/environment/environment.cpp:Environment::resolve_link` resolves a link whose master index is past the plugin's master table (including `0xFF`) to the plugin itself, as Mutagen `Plugins/Masters/SeparatedMasterPackage.cs:GetFormKey` does for any index at or past the master count. Because both decoders only resolve links through it, they no longer let `FormIdError` escape, so `decode_placed_reference` and the WRLD/CELL decoders throw only `WorldDecodeError` as documented. Ported from #336. Test: `test_environment_placed.cpp:LinkPastTheMasterTableResolvesToTheOwningPlugin`.
- `Radicle/src/environment/environment.cpp:Environment::index_group` used to skip every record whose own header form ID failed to resolve, so such a record vanished from `winner`, `versions`, `find_by_editor_id` and `winners_of` with no error. A null form ID is still skipped; any other unresolvable one now fails the load with an `EnvironmentError` naming the form ID, the plugin, the index and the master count. Ported from #336. Test: `test_environment.cpp:RejectsRecordWhoseMasterIndexIsPastTheMasterTable`.
- `Radicle/src/environment/placed.cpp:primary_model_path` reads `MODL` only for the model-bearing signatures listed above. Ported from #336. Test: `test_environment_placed.cpp:PrimaryModelPathIgnoresModlOnRecordsWithoutAModel`.
- `Radicle/src/environment/environment.cpp:Environment::load` rethrows `std::bad_alloc` instead of wrapping it, so `radicle_env_load` reports `RADICLE_STATUS_OUT_OF_MEMORY` rather than `PARSE_ERROR`. Every `EnvironmentError` message now writes paths through the local `utf8` helper (`path.u8string()`). `path.string()` throws `std::system_error` on MSVC for characters outside the ANSI code page, which replaced the real error with `INTERNAL_ERROR` and dropped the plugin path that GardenEngine's `GameLoader.LoadRadicle` searches for to skip a broken plugin. Test: `test_environment.cpp:LoadErrorsNameNonAnsiPathsInUtf8`.
  - That test builds the folder name from UTF-16 code units. MSVC reads the Radicle test sources as the ANSI code page (no `/utf-8`), so the original raw `u8"…"` literal became a folder name made of ANSI characters: `path.string()` never threw and the test only checked the message encoding. With the real name it fails on MSVC with `std::system_error` against the old `Environment::load` and passes with the helper.
- `Radicle/src/environment/environment.cpp:Environment::cell_children` drops a child whose winning version is in another cell (see the `cell_children` notes above). The GardenEngine Mutagen reference (`MutagenCellService.cs`) keeps such a reference in both cells, so `CellServiceParityTests` will report moved references as a difference once GardenEngine pins a Radicle with this change. Test: `test_environment_world.cpp:AReferenceMovedToAnotherCellLeavesItsOldCell`.
- `Radicle/src/environment/placed.cpp:decode_placed_reference` stores a 4-byte `XLKR` as the keyword, matching Mutagen and GardenEngine's previous `CellService` output, which the XLKR note above already claimed. Test: `test_environment_placed.cpp:DecodesObjectReferenceFields` and `CApiDecodesPlacedReferences`.
- `Radicle/include/fo4/environment/placed.hpp:Script::name` and `ScriptProperty::name` are owned strings. They were views into `Scripts::raw`, so a copy of `PlacedReferenceData` kept pointers into the original, which dangled once it was destroyed. Test: the copy check at the end of `test_environment_placed.cpp:DecodesObjectReferenceFields`.
- `Radicle/include/fo4/c_api_env.h:radicle_placed_ref` lost its lifetime comment, which claimed every pointer lives until `radicle_placed_ref_free`. The arrays do. `comments`, `map_marker_name_text`, script and property names from `raw`, and every `radicle_form_key.plugin` point into the environment and live until `radicle_env_destroy`.
- Not changed: `Radicle/src/environment/environment.cpp:zstring_view` still requires exactly one trailing NUL for EDID and FULL, where Mutagen `BinaryStringUtility.ProcessNullTermination` cuts at the first NUL. Matching it would turn the 4-byte binary FULL that `MetadataDecodesNamesBoundsAndFlags` treats as malformed into one-character text. No real plugin in the 644-plugin Fallen World parity run was affected.

## Radicle/src/environment/terrain.cpp:fo4::env::decode_landscape / landscape_texture_diffuse — GardenEngine #114 terrain surface

Fourth #114 slice, stacked on the placed-reference slice. It covers the LAND, LTEX and TXST data that `TerrainService`, `TerrainTextureBaker` and `CellService` read. Godot mesh and splatmap building stays in GardenEngine.

**LAND layouts.** These follow xEdit FO4 `wbRecord(LAND` plus `wbLandHeights`, `wbLandNormals`, `wbLandColors` and `wbLandLayers` in `Core/wbDefinitionsCommon.pas` (pin `9fb016884bec138ea6c7b872cec831537d464c3e`). The vendored Mutagen (`Landscape_Generated.cs`, `AlphaLayer_Generated.cs`) agrees on every size:
- `DATA` is a `u32` of flags.
- `VHGT` is 1096 bytes: an `f32` offset, 33×33 height bytes, then 3 unused bytes. The two readers disagree on sign: xEdit reads the height bytes as `s8`, Mutagen as `u8`. The bytes are returned untouched, and GardenEngine keeps its own signed-delta ×8 accumulation.
- `VNML` and `VCLR` are 33×33×3 bytes each.
- All grids are row-major, y outer and x inner, which matches Mutagen's `Array2dBinaryTranslation`.

**Layers:**
- `BTXT` (base) and `ATXT` (alpha) share an 8-byte header: LTEX form ID, quadrant `u8` (0–3), an unused byte, and a 16-bit layer number.
  - Source: the vendored Mutagen `Mutagen.Bethesda.Fallout4/Records/Major Records/LayerHeader_Generated.cs` reads the texture link at 0x0 (4 bytes), `Quadrant` at 0x4 (1 byte), `Unused` at 0x5 and `LayerNumber` as `u16` at 0x6. `Radicle/src/environment/terrain.cpp:decode_landscape` reads the same offsets.
  - The layer number is returned as raw bits; xEdit reads it as `s16`, Mutagen as `u16`.
  - A quadrant above 3 fails.
- `VTXT` must directly follow its `ATXT` and be a multiple of 8 bytes: `u16` position, 2 unused bytes, `f32` opacity.
  - It is returned raw, like Mutagen's `AlphaLayerData`.
  - A `VTXT` anywhere else fails.
- An `ATXT` without `VTXT` is valid; its alpha is empty.

**Texture chain:**
- LTEX `TNAM` links the texture set.
- TXST has eight string slots, `TX00`–`TX07` in slot order, plus `MNAM` material, `DNAM` `u16` flags, and the width/height/depth/shininess prefix of `DODT` (xEdit `wbDODT`, Mutagen `Decal.xml`).
  - `Radicle/include/fo4/environment/terrain.hpp:TextureSetData::textures` slots are diffuse, normal/gloss, wrinkles, glow, height, environment, multilayer and smooth spec.
- `landscape_texture_diffuse` walks winning LTEX → `TNAM` → winning TXST → `TX00`, which is `TerrainTextureBaker.Resolve`'s chain. It gives nothing when any link or the path is missing.
- `decode_landscape`, `decode_landscape_texture_set` and `decode_texture_set` throw `WorldDecodeError` on a wrong signature or a malformed subrecord that is present.

**C ABI.** `Radicle/src/c_api_terrain.cpp` provides:
- `radicle_landscape_decode` / `radicle_landscape_free`: one owned view whose grids and alpha payloads point into plugin bytes;
  - `radicle_landscape` `heights`, `normals` and `colors` are the row-major 33×33 grids, NULL when the subrecord is absent;
  - `radicle_land_layer::alpha` is the raw VTXT payload, NULL for base layers.
- `radicle_landscape_texture_get_set`: the LTEX `TNAM`; the out key's `plugin` is NULL when it is absent.
- `radicle_texture_set_get_info`:
  - `textures` is `TX00`–`TX07`, NULL for absent slots. The strings are views into plugin bytes that end at a NUL the decoder located, so they are valid C strings.
  - `decal` holds min width, max width, min height, max height, depth and shininess; only the first `decal_count` are present.
- `radicle_env_landscape_texture_diffuse`: `RADICLE_STATUS_NOT_FOUND` when any link or the path is missing.

**Tests.** `Radicle/tests/test_environment_terrain.cpp` covers:
- a full LAND;
- orphan `VTXT`, `VHGT` size and quadrant failures;
- the LTEX/TXST chain, including missing links;
- the C ABI.

## Radicle/src/environment/strings.cpp:fo4::env::StringTables / string_source — GardenEngine #114 localization

Fifth #114 slice, stacked on the terrain slice. It replaces Mutagen's `StringsFolderLookupOverlay` for loose string files. Reading strings out of BA2 archives waits for the BA2 slice.

**Which table a field uses.** `string_source` sends these fields to `.DLSTRINGS`:
- `DESC`, except in `LSCR`;
- `QUST CNAM` and `BOOK CNAM`.

Every `INFO` field except `RNAM` goes to `.ILSTRINGS`, and everything else to `.STRINGS`. Sources:
- This is xEdit's `TwbLocalizationHandler.LocalizedValueDecider` in `Core/wbLocalization.pas` (pin `9fb016884bec138ea6c7b872cec831537d464c3e`).
- Mutagen upstream (`Mutagen.Bethesda.Fallout4/Records/**/*.xml`, commit `5f8f1adeac2c4aaba59d06779340c7aec3da24c5`) declares the same thing per field:
  - `translated="DL"` on every Fallout 4 `DESC` except `LoadScreen.xml` (`Normal`), and on `Quest.xml`/`Book.xml` `CNAM`;
  - `translated="IL"` only on `DialogResponses.xml` `NAM1`.
- The vendored copy in GardenEngine (`opencw/third_party/Mutagen/Mutagen.Bethesda.Fallout4/Records`) agrees: `translated="DL"` on every `DESC` except `LoadScreen.xml` (`Normal`) and on `Quest.xml`/`Book.xml` `CNAM`; `translated="IL"` only on `DialogResponses.xml` `NAM1`; `DialogResponses.xml` `RNAM` is `Normal`.
- `Radicle/src/environment/strings.cpp:string_source` is the xEdit rule, which sends every `INFO` field except `RNAM` to IL. Mutagen only marks the fields it localizes, and the other `INFO` strings (`NAM2` script notes, `NAM3` edits, `NAM4` alternate lip text) carry no `translated` attribute, so the two agree on every localized field and differ only for fields that are never looked up in a string table.
- `Radicle/src/environment/strings.cpp:string_source_extension` gives the upper-case extension without the dot: `STRINGS`, `DLSTRINGS` or `ILSTRINGS`.

**File discovery:**
- For each plugin with the TES4 localized flag, the loader looks for `<strings_dir>/<plugin stem>_<language>.<STRINGS|DLSTRINGS|ILSTRINGS>`.
- The whole name is matched case-insensitively. This matches Mutagen's `StringsFolderLookupFactory`, which compares mod names with `OrdinalIgnoreCase` and lower-cases the language, and Windows file semantics.
- The language is the ISO token Mutagen's `StringsUtility.GetIsoLanguageString` uses for Fallout 4 (`en`, `de`, `fr`, `it`, `es`, `esmx`, `pl`, `ru`, `ja`, `cn`, `zhhans`, `ptbr`). Any alphanumeric token is accepted, so callers pick the language, with English as the Mutagen default.
- A missing directory or file means that table is absent. Mutagen likewise skips it, and a lookup then fails.
- Two files whose names differ only by case, where that name is actually needed, fail. This can only happen on case-sensitive filesystems.
- `Radicle/src/environment/strings.cpp:list_directory` keys the regular files by their lower-cased UTF-8 name. Lower-casing is ASCII-only, where Mutagen's `OrdinalIgnoreCase` also folds non-ASCII letters.
- Every path in `strings.cpp` is turned into text through the local `utf8` helper (`path.u8string()`), as in `environment.cpp`. With `path.string()`, MSVC threw `std::system_error` for any file in the strings directory whose name has characters outside the ANSI code page, so one unrelated file broke the whole load (`radicle_strings_load` returned `INTERNAL_ERROR`). Test: `test_environment_strings.cpp:HandlesNonAnsiFileNames`, which builds its names from UTF-16 code units because MSVC reads the test sources as the ANSI code page (no `/utf-8`), so a raw non-ASCII `u8` literal becomes a different, ANSI-representable name.

**Error timing.** A malformed table fails the whole load with `EnvironmentError`. Mutagen parses lazily and throws on the first lookup, so Radicle reports the same bad file earlier.

**Lookups:**
- Text always comes from the table of the plugin that owns that record version. An override in `Patch.esp` resolves in `Patch_<lang>.*`, not in the master's table, like Mutagen's per-mod `StringsFolderLookupOverlay`.
- `resolve_name` returns inline `FULL` text unchanged for plugins that are not localized. For a string ID, it reads the owning plugin's Normal table.

**Encoding.** Text is returned as raw bytes, NUL-terminated for the C ABI, and the caller decodes it. Mutagen's `MutagenEncoding.GetSkyrimSeEncoding` covers Fallout 4:
- English is Windows-1252.
- Other Western languages try UTF-8 and fall back to 1252.
- Czech, Hungarian and Polish try UTF-8, then 1250; Russian tries UTF-8, then 1251; Japanese tries UTF-8, then 932.
- Greek tries UTF-8, then 1253; Turkish tries UTF-8, then 1254; Arabic tries UTF-8, then 1256.
- Chinese, Korean and Thai are UTF-8.

The C# binding should apply the same table, so no language choice is built into the SDK.

**C ABI.** `Radicle/src/c_api_strings.cpp` provides:
- `radicle_string_source_for`: both signatures must be four characters.
- `radicle_strings_load` / `radicle_strings_destroy`. The environment must outlive the handle. Missing files are skipped; a malformed table fails with `RADICLE_STATUS_PARSE_ERROR`. The handle is immutable, and borrowed text stays valid until `radicle_strings_destroy`.
- `radicle_strings_table_path`: the loaded table's UTF-8 path (`radicle_strings::paths`, indexed [plugin][source] like `StringTables::tables_`), or `RADICLE_STATUS_NOT_FOUND` when that plugin and source have none.
- `radicle_strings_lookup`: raw NUL-terminated bytes and their size from the plugin's own table; the encoding follows the language and is left to the caller.
- `radicle_strings_resolve_name`: the record version's `FULL`, inline or looked up in the owning plugin's Normal table; `RADICLE_STATUS_NOT_FOUND` when it is absent, malformed or unresolved.

**Tests.** `Radicle/tests/test_environment_strings.cpp` covers:
- the field rules;
- case-insensitive discovery and language selection;
- owning-plugin resolution for an override;
- inline names, a missing ID and a missing directory;
- a malformed table and bad language tokens;
- non-ANSI file and directory names (`test_environment_strings.cpp:HandlesNonAnsiFileNames`);
- the C ABI.

## Radicle/src/archive/ba2_reader.cpp:fo4::archive::Ba2Reader / assets.cpp:AssetResolver — GardenEngine #114 BA2 surface

Sixth #114 slice, stacked on the localization slice. It covers the BA2 C ABI, asset precedence, and string tables stored inside archives.

**Why a second reader.** `read_ba2_general_file` and `read_ba2_texture_file` load the whole archive into memory, which is fine for authoring small archives. Vanilla `Fallout4 - Meshes.ba2` and the texture archives run to gigabytes.

`Ba2Reader::open` reads only three things from disk: the 24-byte header, the entry and chunk table, and the name table. It checks every chunk's range against the file size up front. `extract` then seeks to and reads only the chunks it needs. It opens its own stream on each call, so a const reader can be used from several threads without locks.

Decompression and DDS rebuilding are the existing `extract_ba2_general_entry` / `extract_ba2_texture_entry`, so both readers share one DX10 → DDS header path and the same 1 GiB extraction cap.

**Lookup.** Paths are matched with `normalize_ba2_path`, so case and separator don't matter. An archive without a name table is searched by `compute_ba2_hashes` instead. When one archive lists the same path twice, the first entry wins.

**Load order.** `archive_load_order` builds the list, lowest priority first:
- INI-listed names, in listed order. Reading `Fallout4.ini` (`sResourceArchiveList`/`sResourceArchiveList2` in Mutagen's `GetArchiveIniListings`) stays with the caller, as does MO2 profile parsing.
- Then, for each plugin in load order, `<stem>.ba2`, followed by `<stem> - *.ba2` in case-insensitive ascending order.

Details:
- Names match case-insensitively. Files that are missing on disk are skipped.
- An archive that is both INI-listed and plugin-named appears once, at its INI position, as in Mutagen's `CachedArchiveListingDetailsProvider` comparer.
- Which plugin archives apply follows Mutagen's `CheckArchiveApplicability`: an exact stem match, or the stem before the last ` - `.
  - `Radicle/src/archive/assets.cpp:archive_load_order` used to accept any `<stem> - *.ba2`, so `Mod - Patch - Main.ba2` was mounted at `Mod.esp`'s position (and even without `Mod - Patch.esp`). It now compares the name before the last ` - `, as Mutagen does. Test: `test_ba2_reader.cpp:SuffixedArchivesBelongToTheNameBeforeTheLastSeparator`.
  - One deliberate difference: an empty suffix (`Mod - .ba2`) is not applied, where Mutagen's rule would accept it.
- Mutagen orders the ` - ` suffixes of one plugin in descending ordinal order. Radicle uses ascending order. Each vanilla/DLC plugin's suffixed archives (`Main`, `Textures`, `Voices_*`) hold different paths, so the order does not change any lookup.

**Precedence.** `AssetResolver::locate` checks loose files under `data_dir` first. It then searches the archives from highest priority down, so the last-mounted archive wins, as the game resolves them.

Mutagen differs:
- `GameAssetProvider` also checks loose files before archives.
- But `ArchiveAssetProvider` returns the *first* archive in its applicable order that has the path. INI archives come first there, then plugin archives in ascending load order, so Mutagen resolves a conflicting path to the lowest-priority archive.

GardenEngine needs what the game shows, so Radicle follows the game. This only differs from Mutagen for paths that more than one archive provides.

**Path rules:**
- Asset paths are normalized to lower case with `/` separators.
- A leading separator and a leading `data/` are dropped, like Mutagen's `DataRelativePath`.
- Empty paths and `.`/`..` components are rejected with `AssetPathError`, so a lookup cannot escape `data_dir`.
- On case-sensitive filesystems, loose lookup matches each path component case-insensitively. An exact match is tried first, and when several case variants exist the lexically smallest wins.

**Strings in archives.** `StringTables::load` takes an optional `AssetResolver`. For each table not found loose, it looks up `Strings/<stem>_<lang>.<EXT>` in the archives, highest priority first.
- Mutagen's `StringsFolderLookupFactory` also checks loose files first. It then walks the INI archives plus that plugin's own archives and keeps the first hit.
- String table names are specific to one plugin, so both approaches find the same file in real setups.
- `table_path` reports an archived table as `<archive path>/<entry path>`.

**C ABI.** `Radicle/src/c_api_archive.cpp` provides:
- `radicle_ba2_open` / `close`, `get_info`, `get_entry`, `find`, `extract` (free with `radicle_free_bytes`);
- `radicle_archive_load_order`;
- `radicle_assets_open` / `destroy`, `archive_count`, `get_archive` (borrowed readers), `locate`, `read`;
- `radicle_strings_load_with_assets`.

`Ba2Error` maps to `PARSE_ERROR` and `AssetPathError` to `INVALID_ARGUMENT`. `copy_strings` moved from `c_api.cpp`'s anonymous namespace into `radicle_c_detail` so this file can share it.

**Error paths.** Every Radicle error message that names a path goes through `Radicle/include/fo4/io/path_text.hpp:fo4::io::utf8` (`path.u8string()`). This includes the BA2 readers (`ba2_reader.cpp`, `assets.cpp:AssetResolver::read`), `environment.cpp`, `strings.cpp`, `plugin/reader.cpp:read_plugin_file` and `io/atomic_write.cpp`. It replaced four identical local copies. With `path.string()`, MSVC threw `std::system_error` for characters outside the ANSI code page, replacing the `Ba2Error` (`PARSE_ERROR`) with `INTERNAL_ERROR`. Test: `test_ba2_reader.cpp:ErrorsNameNonAnsiPathsInUtf8`. The older whole-file readers in `ba2.cpp` and `ba2_texture.cpp` now share `Radicle/src/archive/ba2_file.hpp:read_file_bytes`, which writes the path the same way; it replaced two identical copies that used `path.string()`. `read_plugin_file` (missing or unreadable plugin) and `write_file_atomic` (failed replace) had the same `path.string()` problem, so a plugin load or save under a non-ANSI folder raised `std::system_error` instead of naming the file; the same test covers both. `Radicle/src/io/atomic_write.cpp:temp_sibling` builds the staging name by appending to the destination path instead of narrowing its file name. Before, every atomic write to a file whose own name is non-ANSI raised `std::system_error`: plugins, BA2 archives and strings files written through the C API or PluginEditTool's plugin backend, which pass UTF-8 paths. PluginEditTool's archive extraction builds its paths from ANSI strings, so it never reached this. Test: `test_atomic_write.cpp:WritesNonAnsiFileNames`. One success path still narrows the file name: `reader.cpp` (`PluginKey::from_filename`), so loading a plugin whose own name is non-ANSI (not its folder) still throws; the game cannot load such plugin names under the ANSI code page either.

**Contracts** (moved from source comments when the slice was restacked):
- `Radicle/include/fo4/archive/ba2_reader.hpp:Ba2Reader::open` throws `Ba2Error` on anything malformed. `Ba2EntryInfo::path` is the name-table path as stored and is empty when the archive has no name table; the texture header is meaningful only for DX10 archives. The reader's index maps the normalized path, or the packed hashes for archives without names, to the entry index.
- `Ba2Reader::extract` returns the decompressed payload; DX10 entries come back as a complete DDS file. `Radicle/src/archive/ba2_reader.cpp` keeps the DDS header rebuild on for named entries; unnamed archives only hold DDS payloads in DX10 form.
- `Radicle/include/fo4/archive/assets.hpp:AssetResolver::open` takes the archives lowest priority first and reads each index at open. An empty `data_dir` disables loose lookup. `AssetLocation`'s indexes point into `archives()` and that archive's `entries()`.
- `Radicle/src/archive/assets.cpp` lists the regular files directly in a directory as (lower-cased name, path), sorted by lower-cased name.
- `Radicle/include/fo4/c_api_env.h`:
  - `radicle_ba2_entry_info.path` is borrowed and empty for archives without a name table; the texture fields are zero for GNRL. `radicle_ba2_find` gives `RADICLE_STATUS_NOT_FOUND` when absent. `radicle_ba2_extract` bytes are freed with `radicle_free_bytes`.
  - `radicle_archive_load_order`'s result is freed with `radicle_free_string_array`.
  - `radicle_assets_open` takes `archive_paths` lowest priority first; `data_dir` may be NULL to disable loose lookup. The handle owns its readers.
  - `radicle_assets_get_archive` returns a reader borrowed until `radicle_assets_destroy`; it is never passed to `radicle_ba2_close`.
  - `radicle_assets_locate` gives `RADICLE_STATUS_INVALID_ARGUMENT` for empty or `.`/`..` paths and `NOT_FOUND` when nothing provides the path. `archive_index`/`entry_index` are meaningful only for `RADICLE_ASSET_ARCHIVE`.
  - `radicle_assets_read` returns the winning copy's bytes (freed with `radicle_free_bytes`); `out_location` may be NULL.
  - `radicle_strings_load_with_assets` is `radicle_strings_load` plus the archive fallback; `assets` may be NULL. `radicle_strings_table_path` gives the UTF-8 loose path or `<archive>/<entry>` for an archived table.
- `Radicle/src/c_api_env_internal.hpp` holds the loader shared by `radicle_strings_load` and `radicle_strings_load_with_assets`; it clears the error itself.

**Tests.** `Radicle/tests/test_ba2_reader.cpp` covers:
- GNRL and DX10 archives, stored and zlib chunks, with extraction checked against the in-memory reader;
- hash lookup without a name table;
- truncated, wrong-tag, out-of-range-chunk and oversized-count archives;
- load order;
- loose/archive precedence and path rules;
- string tables from an archive, with a loose override;
- the C ABI.

`Radicle/tests/test_ba2_real.cpp` is the real-file proof, gated on `FO4_DATA_DIR`:
- It opens every vanilla `Fallout4 - *.ba2`, extracts a NIF (checks the `Gamebryo File Format` magic) and a DDS (checks `DDS `).
- It resolves WRLD `0000003C` to "Commonwealth" through `Strings/Fallout4_en.STRINGS` in the archives.
- Setting `FO4_BA2_EXTRACT_ALL=1` also extracts every vanilla entry.

# Asset path validation

`Radicle/src/archive/assets.cpp:normalize_asset_path` rejects colons before joining path components to the Data directory. A drive-qualified component can replace that directory on Windows; colons also select alternate data streams. `Radicle/tests/test_ba2_reader.cpp:Ba2ReaderTest.RejectsDriveAndStreamPaths` covers both loose-file and archive lookup entry points. A Windows C ABI reproduction read a temporary sentinel outside Data before the guard and must return `RADICLE_STATUS_INVALID_ARGUMENT` afterward.

## Radicle/src/forms/quest_vmad.cpp:sync_quest_vmad_stages — QUST Partial Form stage fragments

Shipped DLC overrides `DLCCoast.esm 0AE51C:Fallout4.esm` (BoS301) and `DLCNukaWorld.esm 05DEE4:Fallout4.esm` (Min01) carry record flag `0x4000` and only `EDID` + `VMAD`: no `INDX`, `QSDT` or any other stage member. xEdit dev-4.1.6 (`9fb016884bec138ea6c7b872cec831537d464c3e`, `Core/wbDefinitionsFO4.pas`, `wbRecord(QUST ...)`) names that flag `{0x00004000} 14, 'Partial Form'`. Its `wbVMADFragmentedQUST` fragment struct is `Quest Stage` u32, `Quest Stage Index` u32, unknown u8, `ScriptName`, `FragmentName`, and it does not cross-check fragment stages against `INDX`. Decoded with `Radicle/src/schema/vmad.cpp:walk_quest_extra` rules, every fragment stage in both overrides (38 in BoS301, for example 355; 34 in Min01, for example 1110, 2100, and 100/1500 with stage index 1) is an `INDX` stage of the Fallout4.esm master record. BoS301's override VMAD has the same size as the master's (6509 bytes) with reordered fragments. Min01's is 4697 bytes against the master's 4718. The fragments therefore bind master stages that the partial override does not carry.

`sync_quest_vmad_stages` handles only that case. When the record has flag `0x4000` and decodes no stages, an empty replacement leaves VMAD untouched. Any non-empty replacement throws, because what a partial form's own `INDX` does against the master stage list is not established. Non-partial records, and partial forms that carry their own stages, keep the existing identity and body-match check. `0x4000` is record-type-specific in FO4 (other records use bit 14 differently), so the constant stays local to this file rather than in `fo4::RecordFlags`. Regressions: `Radicle/tests/test_quest_authoring.cpp` `PartialFormVmadKeepsMasterStageFragmentsOnUnchangedRewrite` (red before the fix with the real-data message) and `PartialFormVmadRefusesStagesItCannotBind`; real-data `QuestAuthoringRealData.RewritingUnchangedStagesAndObjectivesIsByteExact` is green.

Further comparison against the same pinned [xEdit source](https://github.com/TES5Edit/TES5Edit/blob/9fb016884bec138ea6c7b872cec831537d464c3e/Core/wbDefinitionsFO4.pas#L936) confirms that `wbConditionQuestStageToStr` uses the winning override only when it is not a partial form; if the selected quest is itself partial, it selects `MasterOrSelf`. This supports retaining fragments whose stages are absent from the partial override. Adding stages remains TODO-verify against plugin-load behavior; runtime stage-execution evidence alone does not establish how partial-form stage arrays are combined.

`Radicle/tests/test_quest_authoring.cpp:PartialFormVmadKeepsMasterStageFragmentsOnUnchangedRewrite` also checks serialization/reload, `PartialFormVmadRefusesStagesItCannotBind` checks rejection leaves serialized bytes unchanged, and `PartialFormWithLocalStagesKeepsVmadValidation` checks the partial-form flag does not bypass binding validation when local stages exist.

`PluginEditTool/native/tests/unit/RadiclePluginQuestPartialFormTests.cpp:Native SetQuestStages preserves partial-form master fragments through save and reload` exercises the JSON backend and explicit save path. An empty replacement preserves the partial flag and the EDID/VMAD members after reload. Adding even the fragment's existing master-stage number is refused; saving after that refusal must leave the file unchanged. `Radicle/tests/test_quest_authoring_real.cpp:PartialFormMasterFragmentsSurviveSerialization` targets the two DLC overrides identified above rather than relying on the general corpus sampler to select them, and checks their full subrecord snapshots after serialization/reload. Both checks validate file preservation, not runtime stage merging.

## Radicle/src/plugin/mutation.cpp:copy_record_as_override — editing stages from a partial quest

For stage authoring, explicitly select a full source record and use native `PluginEditTool/native/src/plugin/RadiclePluginMutation.cpp:copyAsOverride` with `CopyAsOverride([sourcePlugin, formKey, targetPlugin, true])` before `SetQuestStages`. The overwrite replaces the whole partial record, including its VMAD, with the selected source and remaps FormIDs; it does not combine the partial VMAD with the source's stages. The source must be nonpartial and appropriate for the target's master chain. Radicle's explicit operation does not automatically select that source. This matches the full-record restoration approach in pinned [xEdit `TwbMainRecord.SetIsPartialForm`](https://github.com/TES5Edit/TES5Edit/blob/9fb016884bec138ea6c7b872cec831537d464c3e/Core/wbImplementation.pas#L14602), which restores a visible nonpartial master when clearing the partial flag.

`PluginEditTool/native/tests/unit/RadiclePluginQuestPartialFormTests.cpp:Native full master override makes partial quest stages editable` verifies that overwrite=false keeps the partial-stage rejection, explicit overwrite clears the partial flag, a stage renumber updates only its VMAD binding, and a new stage can be added. Save/reload retains the master's other quest members, leaves the source file unchanged, and retains rejection of removing a bound stage without a replacement. This is file-authoring coverage; partial records with local stages or without fragment bindings remain outside the new exception, and game-runtime acceptance is not claimed.

## BGS assistant boundary and mesh discovery (PR 311)

`BGS/app/OpencodeCli.cs:OpencodeCli.McpBoundaryHolds` requires the BGS server before examining configured servers. `BGS/tools/tests/OpencodeCliTests.cs:TheResolvedBoundaryRejectsAMissingBgsBridge` failed for an empty map and a map containing only disabled foreign servers before the guard; both now pass.

`BGS/src/Hkx/MeshLookup.cs:MeshLookup.OnDisk` enumerates at most 4097 filesystem entries, including directories and non-mesh files, and refuses automatic selection when the 4096-entry budget is exceeded. Reparse points are excluded. The complete bounded candidate set is collected before reading models, so truncation cannot select an arbitrary partial result. Larger folders require explicit Mesh selection. `BGS/tools/tests/MeshSearchBudgetTests.cs:NonMeshEntriesCountTowardTheSearchLimit` failed before the bound. The original full UI smoke stalled after initial window checks while auto-discovery traversed the temporary root; its log is under `BGS/tools/tests/bin/uismoke-baseline.log` in this task worktree.

`BGS/app/AssistantCommandRunner.cs:AssistantCommandRunner.DefaultRunAsync` passes prompt arguments only to native executables. Locally verified npm Claude shims resolve to `node_modules/@anthropic-ai/claude-code/bin/claude.exe`; other shell scripts are refused rather than sent through cmd.exe. `BGS/tools/tests/AssistantCommandRunnerTests.cs:ShellShimsCannotInterpretPromptTextAsCommands` failed before removing shell execution and passes afterward.

`BGS/app/AssistantConversationController.cs:AssistantConversationController.ClearActive` reports persistence failure without changing messages/session; successful clear invalidates and disposes the session before the next request. `BGS/tools/tests/MultiChatControllerTests.cs:ClearingChatDisposesItsSessionAndStartsFresh` and `FailedClearPreservesChatAndSession` both failed before remediation. `BGS/app/AssistantUi.Provider.cs:AssistantUi.SelectModel` and `BGS/app/AssistantUi.Api.cs:AssistantUi.ChooseApiKey,ChooseBaseUrl` reset cached idle sessions after setting changes and refuse changes during a request. `BGS/tools/uismoke/AssistantLifecycleSmoke.Fixes.cs:ProviderChangesResetIdleSessions` exercises these production handlers.

`BGS/app/AssistantUi.cs:AssistantUi.SendNow` displays sanitized provider error details outside saved assistant history; `BGS/tools/uismoke/AssistantLifecycleSmoke.Fixes.cs:ProviderFailuresKeepTheirActionableDetail` verifies this. `BGS/app/OpencodeModelCatalog.cs:OpencodeModelCatalog.Parse` gives the plain-text fallback its provider identity, so selection routes to OpenCode instead of silently doing nothing.

`BGS/app/CodexAssistantConnection.cs:CodexAssistantConnection.ConnectAsync` and `BGS/app/CodexAssistantSession.cs:CodexAssistantSession.SendAsync` were exercised through the installed production app-server with the existing signed-in ChatGPT account. The advertised gpt-6-luna model returned BGS_LIVE_OK, status ok, zero tools. A configured model absent from that catalog was refused. This proves account read and a live isolated thread round-trip, not browser/device-code login, logout, or re-login. Logs are under `BGS/tools/tests/bin/live-proof-model.log` in this task worktree. Windows Release unit suite: 930 passed. Full UI smoke: 321 checks passed; assistant and multi-chat modes are validated separately.

`BGS/tools/uismoke/AssistantLifecycleSmoke.cs:AssistantLifecycleSmoke.Pump` allows ten seconds for asynchronous UI work. A timed queued-turn reproduction measured more than four seconds between gate release and the provider continuation, before persistence or UI dispatch, exceeding the old three-second test deadline. Queue production code remains unchanged; explicit assistant and multi-chat smoke modes cover those flows.

`BGS/tools/tests/MultiChatControllerTests.SessionLifecycle.cs:MultiChatControllerTests` groups session disposal, reset, logout, and server replacement proofs in the existing partial test class so the controller suite stays within the source-size limit. Test ownership, fixtures, and behavior are unchanged.

`BGS/app/AssistantChatStore.cs:AssistantChatStore.All` sorts loaded summaries by their persisted UpdatedUtc timestamp. Filesystem write times can tie or be changed by copying files. `BGS/tools/tests/AssistantChatStoreTests.cs:EnumerationReturnsSavedChatsSortedByUpdatedUtcDescending` now reverses the filesystem timestamps to reproduce the former incorrect order deterministically.

## BGS skeleton and skin authoring validation (PR 161)

`BGS/src/Nif/SkinEdit.cs:SkinEdit.SetInfluences` rejects all non-finite incoming weights before filtering or mutating a vertex. `BGS/tools/tests/SkinEditingProofTests.cs:NonFiniteWeightsAreRejectedWithoutChangingTheVertex` failed for NaN and both infinities before the guard and passes after it.

`BGS/src/Hkx/SkeletonAuthoring.cs:SkeletonEdit.Subtree` tracks visited bones so AddBone and Reparent reject an existing parent cycle before modifying the rig. `BGS/tools/tests/SkeletonAuthoringProofTests.cs:ExistingParentCyclesAreRejectedBeforeMutation` covers both callers and checks the original arrays survive refusal. These are library authoring APIs; this PR adds no editor or NIF save workflow.

`BGS/src/Nif/SkinEdit.cs:SkinEdit.CheckedInfluences` validates all proposed batch output before `Prune`, `CopyWeights`, or `BGS/src/Nif/SkinEdit.Mirror.cs:SkinEdit.MirrorWeights` writes any vertex. `SkinEdit.WriteInfluences` applies already-checked slots without repeating normalization. `BGS/tools/tests/SkinEditingProofTests.cs:InvalidLaterVertexDoesNotLeaveAnEarlierVertexChanged` failed for all three operations before staging writes and passes afterward; an independent two-vertex NaN copy reproduction also leaves both vertices unchanged. Windows Release suite: 636 passed.

## BGS CI and 1.2.0 packaging

`.github/workflows/bgs-ci.yml:verify` matches the registered Windows runner labels self-hosted/Windows/X64; that runner does not advertise garden. The Linux runner retains its garden label. Forgejo run 1218 job 2742 failed before checkout because the default mirror had no actions/github-script repository. Package verification now uses Python rather than that action. SDK setup uses the official setup-dotnet source and a task-owned temporary directory. CI also restores/builds MCP and runs the previously separate assistant, multi-chat, and layout modes.

`BGS/tools/ci/package.py:main` checks the published version, exact commit prefix, and runtime against `BGS/app/BehaviourStudio.csproj:Version` and CI inputs, removes PDB files only within a resolved BGS output directory, and writes build-info.json. Its optional archive creates a fresh ZIP and SHA-256 sidecar for release delivery. `BGS/RELEASE-NOTES.md` and `BGS/CHANGELOG.md` identify version 1.2.0; browser login and real replies from non-Codex providers remain separate manual acceptance boundaries.

`BGS/app/CodexAppServerClient.cs:CodexAppServerClient` defaults its handshake client version to BuildInfo.Version instead of the previous fixed 1.1.0. `BGS/tools/tests/CodexAuthTests.cs:HandshakeRunsInitializeThenInitializedBeforeAccountRead` checks the version sent on the wire; explicit client-version overrides remain supported.

## Radicle/src/c_api_writeback.cpp / forms/placed_reference*.cpp / schema/form_link_mixed.cpp:placed_reference_formid_offsets — GardenEngine #114 reference writeback

Seventh #114 slice, stacked on the BA2 slice. It replaces Mutagen's workflow of `GetOrAddAsOverride`, editing `Placement`/`Scale`, then `WriteToBinary`, for references GardenEngine's editor changes.

**form ID remap gap.** The shared remap and link collection in `visit_link_offsets` never recognized seven form ID-bearing placed-reference subrecords. Before this slice, `copy_placed_reference_as_override` and `remap_plugin_masters` therefore:
- left these fields' master indexes unchanged whenever the destination master table differed;
- left their owners out of the masters they add.

The fields, all from xEdit FO4 (pin `9fb016884bec138ea6c7b872cec831537d464c3e`), `wbRefRecord(REFR` and `ReferenceRecord`:

| Subrecord | Layout | Size rule |
| --- | --- | --- |
| `XPOD` Portal Data | array of {Origin, Destination} | multiple of 8 |
| `XORD` Linked Occlusion References | form ID array | multiple of 4 |
| `XLRT` Location Ref Type | form ID array | multiple of 4 |
| `XPWR` Reflected/Refracted By | {Reference, Type} | exactly 8 |
| `XAPR` Activate Parent Ref | {Reference, Delay} | exactly 8 |
| `XPLK` Spline Connection | {Ref, Unknown} | exactly 8 |
| `XWPN` Power Grid Connection | {Node 1, Node 2, Line} | exactly 12 |

`placed_reference_formid_offsets` handles these for the ten placed signatures only, because other record types reuse some of these names. A strict remap throws `FormIdError` when a payload's size breaks its rule, instead of leaving stale IDs.

`is_placed_reference_signature` is now the one shared list; `fo4::env` re-exports it. This also fixes PluginEditTool's placed-reference copies, and its native suite still passes.

**Wider copy/edit support.**
- `copy_placed_reference_as_override` / `_as_new` and `set_placed_reference_transform` now accept all ten placed signatures (REFR, ACHR, PGRE, PHZD, PMIS, PARW, PBAR, PBEA, PCON, PFLA). They share the CELL containment path, and the 24-byte `DATA` layout is identical across them.
- `set_placed_reference_scale` sets `XSCL`, inserting it directly before `DATA` when absent (xEdit field order), or removes it for `nullopt`, which is Mutagen's nullable `Scale`. Duplicate or malformed `XSCL` fails without changing anything.

**Plugin creation and master order.**
- `make_empty_plugin` moves PluginEditTool's `makeEmptyPlugin` header into Radicle: HEDR 1.0 / 0 / `0x800`, form version 131, and the ESM flag for `.esm` or ESM|ESL for `.esl`.
- Copies add masters in `std::set<PluginKey>` order, which is alphabetical, not load order. `sort_plugin_masters` reorders them to follow a load order through `remap_plugin_masters`, so records, group labels and links are all re-encoded. It throws when a master is not in the load order.

**C ABI.** The source is any record version from the environment; its plugin is `Environment::plugin(ref.plugin)`. `Radicle/src/c_api_writeback.cpp` provides:
- `radicle_plugin_create`;
- `radicle_plugin_copy_placed_ref_override` (inserted/replaced/already-exists status);
- `radicle_plugin_copy_placed_ref_new`. The returned key borrows the destination plugin's filename.
- `radicle_plugin_set_placed_transform` / `_set_placed_scale`;
- `radicle_plugin_sort_masters`.

`PlacedReferenceError` maps to `INVALID_ARGUMENT`, and a missing target to `NOT_FOUND`. `struct radicle_plugin` moved into `c_api_internal.hpp` so both C files share it.

**Audit fixes after the restack.** A real-data check (`Radicle/tests/test_writeback_real.cpp:WritebackRealData.CopiedDlcReferencesKeepEveryLinkWhenMastersMove`, gated on `FO4_DATA_DIR`) copies `DLCRobot.esm` placed references into a patch whose master order differs, then writes and reloads it. It selects every reference that carries one of the seven new subrecords, every reference holding a link past the master table, and a 1-in-25 sample of the other linked references. It compares both the decoded links and every raw link hit (record, subrecord, ordinal, offset). It found three failures:
- `Radicle/src/schema/form_link_mixed.cpp:placed_reference_formid_offsets` required `XPLK` and `XPWR` to be exactly 8 bytes. Both have a `<Break />` after the reference in the vendored Mutagen (`SplineConnection.xml`, `WaterReflection.xml`), so a 4-byte reference-only payload is valid; 244 vanilla `XPLK` subrecords are 4 bytes, and copying any of them threw. The layout table now accepts the short size and emits only slots that fit. Test: `test_environment_writeback.cpp:AcceptsReferenceOnlySplineAndReflectionLinks`.
- Vanilla data holds links whose master index is past the plugin's master table (for example `DLCRobot.esm` REFR 00272E `XRFG` = `0x02013491` with one master). `Environment::resolve_link` already reads those as the owning plugin, as Mutagen does, but the remap and link collection went through the strict `resolve_plugin_form_id` and threw. `Radicle/src/formid.cpp:resolve_plugin_link` now holds that rule; `Environment::resolve_link` and every link site in `Radicle/src/schema/form_links.cpp` (`remap_raw`, link collection, retargeting, the COED owner) use it. Record header identities and group labels stay strict. Test: `test_environment_writeback.cpp:LinksPastTheMasterTableStayWithTheOwningPlugin`.
- `0xFFFFFFFF` is xEdit `TwbFormID.IsNone` (source:TES5Edit/TES5Edit@dev-4.1.6 `Core/wbInterface.pas:22383`, `Result := _FormID = $FFFFFFFF`), not a link (`RawFormId::is_none`). `Radicle/src/formid.cpp:resolve_plugin_link` returns an empty `FormKey` for it, as `resolve_plugin_form_id` does for a null value, before its past-table rule; that rule used to turn index `0xFF` into `FFFFFF:<current plugin>`. Every caller goes through it: `Environment::resolve_link` (the typed decoders in `Radicle/src/environment/record_reader.hpp` and VMAD objects in `placed.cpp`), `Radicle/src/schema/form_links.cpp:collect_form_links_detailed`, and the COED owner check. Before, link collection made `Radicle/src/plugin/mutation.cpp:copy_record_as_override` add the source plugin as a master and `ConflictGraph::build` record an edge to that key, and a typed decoder reported the field as a link to that key. `remap_raw` and `rekey_form_links` still skip the value before resolving, because remapping an empty key would write 0. Other past-table values keep the owning-plugin rule. Tests: `test_remap_non_formid_slots.cpp:NoneSentinelIsNotALinkWhenCopyingAnOverride` and `test_environment_world.cpp:NoneSentinelLinksDecodeLikeNull`, both failing before.
- XOWN (ownership) was never remapped in any record, a bug that already existed on main: copying `DLCRobot.esm` ACHR `010F58` turned its owner `010004:DLCRobot.esm` into `010004:Fallout4.esm`. `Radicle/include/fo4/schema/structured_formids.inc` now lists XOWN with the form ID at offset 0. The layout (source:xEdit `wbDefinitionsCommon.pas` `wbOwnership`) is: owner, 4 unused bytes, a u8 No Crime flag, 3 unused bytes, 12 bytes in total, which is what every vanilla XOWN uses. Test: `Radicle/tests/test_environment_writeback.cpp:RewritesTheOwnershipOwner`.
- To keep `form_links.cpp` under the 750-line limit, `visit_link_offsets` reaches the packed and placed layouts through one entry point, `form_link_mixed.cpp:mixed_formid_offsets`.

**Contracts** (moved from source comments when the slice was restacked):
- `Radicle/include/fo4/forms/placed_reference.hpp:copy_placed_reference_as_override` copies any of the ten placed signatures, recreating its CELL/WRLD containment and adding masters. `set_placed_reference_transform` rejects a missing, duplicate or malformed `DATA` without changing the plugin. `set_placed_reference_scale` inserts `XSCL` directly before `DATA` when absent (xEdit FO4 REFR order), removes it for `nullopt`, and returns false when the record is missing. The older header comments on the first two still say REFR/ACHR.
- `Radicle/include/fo4/plugin/writer.hpp:sort_plugin_masters` reorders masters to follow `load_order`, keeping each master's kind, and throws `FormIdError` when a master is not listed. `make_empty_plugin` writes only TES4, as described above.
- `Radicle/include/fo4/schema/form_link_mixed.hpp:placed_reference_formid_offsets` gives `nullopt` when the subrecord is not one of the seven listed above.
- `Radicle/include/fo4/c_api_env.h`:
  - `radicle_plugin_create`: TES4 only, next form ID `0x800`, ESM flag for `.esm` and ESM|ESL for `.esl`.
  - `radicle_plugin_copy_placed_ref_override`: copies this record version into the plugin as an override, recreating its CELL/WRLD containment and adding masters. The plugin is unchanged unless `RADICLE_STATUS_OK` is returned.
  - `radicle_plugin_copy_placed_ref_new`: copies it as a new reference owned by the plugin. `out_key.plugin` is borrowed from the plugin and stays valid until the next call that modifies or destroys it. `NOT_FOUND` when the plugin lacks the parent CELL.
  - `radicle_plugin_set_placed_transform`: rewrites `DATA` position and rotation (radians); `NOT_FOUND` when the plugin has no such record.
  - `radicle_plugin_set_placed_scale`: sets `XSCL`, or removes it when `has_scale` is 0; `NOT_FOUND` when the plugin has no such record.
  - `radicle_plugin_sort_masters`: reorders the plugin's masters to follow the environment's load order; `INVALID_ARGUMENT` when a master is not loaded.
- `Radicle/tests/test_environment_writeback.cpp` fixtures: `Master.esm` has exterior cell 000801 in WRLD 000800 with refs 000802/000803 and interior cell 000820 with ref 000821; `Other.esm` has interior cell 000850 with ref 000851; `DLC.esm` (master `Master.esm`) has interior cell 01000840 with ref 01000841, whose links point at DLC and Master records. In the patch, `Master.esm` moves from master index 0 to 1 and `DLC.esm` from 1 to 2, so its bytes are rewritten.

**Tests.** `Radicle/tests/test_environment_writeback.cpp` covers:
- **Raw remap.** Moving DLC.esm from master index 1 to 2 rewrites every new slot and leaves non-form ID struct members alone. A malformed `XAPR` throws.
- **C++ save/reload parity.** It overrides refs from three masters, copies one as new, and edits transform and scale. It then sorts masters, writes, rereads (byte-identical rewrite) and reloads the environment. Decoded links match the source, edits are applied, and cell children merge the patch copy.
- **C ABI round trip.** The same flow runs through the C ABI.

With `placed_reference_formid_offsets` disabled, the remap and parity tests fail.

## PluginEditTool/native/cmake/Utf8CodePage.cmake:fo4ide_use_utf8_code_page — UTF-8 process code page

Every PluginEditTool executable (`FO4RecordEditor.exe` and the test executables) embeds `PluginEditTool/native/cmake/utf8.manifest`, which sets `activeCodePage` to UTF-8. `fo4ide_use_utf8_code_page()` adds the manifest to every executable target in the directory it is called from (`apps/fo4ide`, `tests`, `tests/integration`).

PluginEditTool's strings are UTF-8 (they come from and go back to JSON), but on MSVC `std::filesystem::path(std::string)` and `path::string()` convert through the process ANSI code page. Before the manifest:
- a non-ASCII folder or file name from a request (even `Café`) decoded to the wrong path, so `RadicleArchiveBackend.cpp` extraction wrote into a garbled folder;
- any `path::string()` on a name outside the ANSI code page threw `std::system_error`, for example `Mo2Profile.cpp:indexFolder` when a scanned folder held such a file (the whole profile load failed), or the `RadiclePluginSeq.cpp` report after a successful write.

There are about 270 such conversions across 43 files; the manifest makes all of them UTF-8 instead of patching each. Windows applies `activeCodePage` from Windows 10 version 1903; older Windows ignores it and keeps the previous behaviour. `RadiclePluginBackend.cpp:utf8Path` and `RadiclePluginSeq.cpp:seqUtf8Path` still convert explicitly, which stays correct either way.

A plugin name written to `plugins.txt` in a legacy code page (non-ASCII, not UTF-8) still does not work: before, it matched the file and then failed in `utf8Path` or when returned in JSON (nlohmann `dump()` is strict); now `PluginEditTool/native/src/mo2/Mo2Profile.cpp:readProfile` reports it in `missingPlugins`. The manifest does not change the console code page or child processes, so output from child tools such as the Papyrus compiler is still in their own code page. `PluginEditTool/native/tests/unit/Mo2ProfileTests.cpp:MO2 profile resolves plugins in non-ANSI mod folders` also checks `GetACP() == CP_UTF8` on Windows, so it fails if the test executables stop receiving the manifest. Nothing checks other executables: one added below the `fo4ide_use_utf8_code_page()` call in its directory would miss it, so keep the call last.

Tests: `Mo2ProfileTests.cpp:MO2 profile resolves plugins in non-ANSI mod folders`, `RadicleArchiveBackendTests.cpp:Radicle archive backend extracts into a non-ANSI output folder`, `RadiclePluginSeqTests.cpp:Native CreateSeqFile reports a plugin folder with non-ANSI characters`. All three failed before the manifest on ANSI code page 1252.

## .github/workflows/native-ci.yml:native-windows-tests — native tests on Windows with MSVC

The two Windows cross jobs build with clang-cl/xwin on the Linux runner and cannot execute anything. `native-windows-tests` runs on the registered Windows runner (`self-hosted, Windows, X64`, the runner `bgs-ci.yml:verify` already uses) and builds and runs the Radicle and PluginEditTool native ctest suites with MSVC. It finds Visual Studio with vswhere and puts its bundled CMake on the PATH, so the runner needs Visual Studio or Build Tools with the C++ x64 tools and the C++ CMake tools component; the step names what is missing. `PluginEditTool/native` builds Radicle as a subproject, so one configure covers both and Radicle's own tests, tools and warnings-as-errors are switched on here rather than compiling Radicle a second time in a separate job. Most run steps use `cmd`, where the step's exit code is the batch file's final errorlevel. The pinned zlib and configure steps are PowerShell: they check `$LASTEXITCODE`, and the configure steps retry up to three times.

Each job builds from the workspace into its own directories: `build\native` for Release and `build\native-debug` for Debug. Nothing is carried between runs, because the host-mode runner gives every job a fresh workspace, so ccache is what makes a re-run cheap. The workflow sets `CCACHE_BASEDIR` to the workspace and `CCACHE_NOHASHDIR` so hits carry across the differing per-job paths. The MSVC setup step locates `ccache.exe` and exports `CCACHE`; each configure builds the launcher arguments from `$env:CCACHE` and passes them as `CMAKE_C_COMPILER_LAUNCHER` and `CMAKE_CXX_COMPILER_LAUNCHER`, and a runner without ccache still builds, just without the cache.

`concurrency.cancel-in-progress` stops a superseded run, but no two jobs share a checkout or a build directory, so a cancelled run leaves nothing for the next one to recover. `CMAKE_DISABLE_FIND_PACKAGE_ZLIB=ON` keeps both configures off a system zlib. `-DFETCHCONTENT_SOURCE_DIR_FO4IDE_ZLIB` points FetchContent at the cached pinned zlib copy from the `native-windows-tests` pinned zlib step instead of a GitHub clone, so the dependency path is identical in Release and Debug. Once that override is set, CMake ignores the `GIT_TAG` in `PluginEditTool/native/CMakeLists.txt:fo4ide_zlib`, so the cache step reads the pin from that line.

`PluginEditTool/native/tests/unit/RadiclePluginEditableTests.cpp:symlinkNotPermitted` makes the two Save As symlink tests skip when Windows refuses the directory symlink with ERROR_PRIVILEGE_NOT_HELD (1314), which happens without Developer Mode or elevation. MSVC does not map 1314 to `std::errc::operation_not_permitted`, so the code is checked directly. Any other symlink error still fails the test, and Linux always runs them.

`.github/workflows/native-ci.yml:native-windows-tests` also configures, builds and tests the same PluginEditTool native tree in Debug as `build\native-debug`, after the Release suite, so MSVC debug-only checks (iterator debugging, CRT asserts) run in CI. The Debug configure repeats the Release flags with `-DCMAKE_BUILD_TYPE=Debug` and the same launcher arguments, so one build still covers Radicle and PluginEditTool native together through the same dependency path.

`Radicle/CMakeLists.txt:gtest_discover_tests` passes `DISCOVERY_TIMEOUT 120`. CMake's default is 5 seconds, and discovery runs `fo4_tests.exe --gtest_list_tests` as part of the build. The Debug executable is much larger than the Release one, and on the Windows runner its first, cold run exceeded that default, so the Debug build failed with `Process terminated due to timeout` right after linking `fo4_tests.exe`. The longer bound only matters when the executable is slow to start; Release discovery stays well inside it.

## .github/scripts/pinned-zlib.sh — cached pinned zlib for CI

Run 775 failed in the Radicle Windows cross job. That job cloned zlib from GitHub, and the GnuTLS handshake failed after a two-minute hang. Every run downloaded the same pinned zlib, so one network blip failed the job. The two cross lanes now run `.github/scripts/pinned-zlib.sh`, and the Windows step in `.github/workflows/native-ci.yml:native-windows-tests` follows the same rules in PowerShell.

The pin is read from `PluginEditTool/native/CMakeLists.txt:fo4ide_zlib`, from the `GIT_TAG` line right after the zlib `GIT_REPOSITORY`. The script fails with `::error::` unless that line holds exactly one 40-hex sha. The tag lives in one place, so a bumped tag moves CI too.

The cache is `${RUNNER_TOOL_CACHE:-$HOME/.cache}/garden-src/zlib/<sha>`. On Windows the step uses `RUNNER_TOOL_CACHE`, or `%LOCALAPPDATA%\garden-src` when that is empty. An entry counts as complete only when it holds `.installed`.

A cache miss fetches into a temp sibling, `<cache>.tmp.<id>`, checks that HEAD is the pinned sha, writes `.installed`, and renames the directory into place. There is no lock file. The rename is atomic, so if another job renames first, this job deletes its temp copy and uses the existing entry.

Each job copies the cache to `build/zlib-src` without `.git`, and exports `ZLIB_SRC`. zlib's CMake writes into its source directory, so no job builds from the shared cache. The fetch makes 5 attempts with 15, 30, 45 and 60 second waits. `http.lowSpeedLimit` and `http.lowSpeedTime` abort a stalled transfer.

The PluginEditTool configure steps make 3 attempts with 30 and 60 second waits. That covers the other GitHub FetchContent dependencies, which `PluginEditTool/native/cmake/Dependencies.cmake` clones during every configure.

## Radicle/src/forms/placed_reference_edit.cpp / c_api_ref_fields.cpp — GardenEngine #114 writeback fields

Eighth #114 slice, stacked on the writeback slice. It covers every field `ReferenceWriteback.Commit` sets beyond placement and scale:
- EditorID and record flags;
- `Comments`, `LocationReference`, `Ownership`/`FactionRank` and `EnableParent`;
- rebuilding `LinkedReferences`;
- `ActivateParents[i].Delay`;
- and, only when the field already exists, `TeleportDestination` and `Primitive`.

**Field order.** New subrecords go before the first existing subrecord that xEdit FO4 lists after them:
- REFR uses `wbRefRecord(REFR`, ACHR uses `wbRefRecord(ACHR`, and the projectiles/hazards use `ReferenceRecord` (pin `9fb016884bec138ea6c7b872cec831537d464c3e`).
- `wbOwnership` expands to `XOWN`, `XRNK`, and `wbVec3PosRot(DATA)` sits before `MNAM`.
- ACHR puts `XLKR` before `XESP`, and REFR after it.
- Signatures missing from the list (for example `XACT`, `XRGD`/`XRGB`, `PDTO`, `FULL` and `XCZA`) are never used as anchors, so a new field can land after one of them. xEdit marks REFR, ACHR and `ReferenceRecord` as `.SetUnordered` and Mutagen parses these fields in any order, so this only affects layout, not loading.
- `set_placed_reference_scale` moved here to use the same rule. On ACHR, `XSCL` now goes before `ONAM` instead of directly before `DATA`.

**Payloads, from Mutagen upstream `Common Subrecords/*.xml` and xEdit:**
- **`XOWN`:** Owner, `Int32 Unknown`, `NoCrime` as a 4-byte bool (`Ownership.xml`).
  - An existing 4- or 12-byte value is widened to 12 bytes.
  - The unknown word and the upper 3 bytes of the bool word are kept. The flag byte is written as exactly 0 or 1, the canonical bool Mutagen writes; Mutagen reads it as a first byte `>= 1` (`Ownership_Generated.cs`) and `Radicle/src/environment/placed.cpp:decode_placed_reference` as `!= 0`. Keeping other bits of an old byte left NoCrime set after clearing it.
  - A cleared owner removes `XOWN` and `XRNK`, as Mutagen nulls both.
- **`XESP`:** Reference, a `u8` flags byte, then 3 unknown bytes (`EnableParent.xml`). The unknown bytes are kept.
- **`XLKR`:** {KeywordOrReference, Reference}, 8 bytes each (`LinkedReferences.xml`).
  - A rebuild replaces every `XLKR` at the position of the first old one, or at the ordered position when there were none.
- **`XTEL`:** door, position, rotation. It is written only when present; flags and the optional transition interior are kept.
- **`XPRM`:** stored (undoubled) bounds and the type at offset 28. It is written only when present; colors and the unknown float are kept.
- **`XAPR`:** delay at offset 4 of the n-th entry. Written only when that entry exists.
- **EditorID and comments:** empty or `nullopt` removes the subrecord, as Mutagen writes nothing for a null string. Embedded NUL is rejected.
- **Record flags:** the compression and persistent bits are kept. Persistence decides whether the ref sits in the CELL's persistent or temporary child group, and a flag edit must not leave it in the wrong one.

**Masters.** A FormKey whose owner is neither the plugin nor one of its masters throws in C++. The C ABI resolves such owners through the environment:
- It adds each one as a master with that plugin's full/light kind.
- It then sorts masters into environment load order before the edit.
- An owner the environment does not load is `INVALID_ARGUMENT`.
- When the edited plugin is itself in the environment, an owner that loads after it is `INVALID_ARGUMENT`: it would become a master that loads later, or a circular master.
- The new `MAST` entry takes the environment's spelling of the file name, not the caller's.
- A `FormIdError` from the master remap or sort (an existing master the environment does not load, or the 255-master limit) is `INVALID_ARGUMENT`.

**Form keys.** `Radicle/src/forms/placed_reference_edit.cpp:encode` rejects a key with no plugin name (for example `"Data/"`, which `PluginKey::from_filename` reduces to an empty name) unless it is the null key, and a named key with local 0 (it would encode as a null link; only a C key whose `plugin` is NULL means clear), and it round-trips every encoded form ID through `resolve_plugin_form_id`. A key that does not survive the round trip, such as a light-plugin local above 0xFFF that `encode_plugin_form_id` would mask to a different form, throws instead of writing the wrong link.

**Atomicity.** Every edit works on a copy, and the plugin changes only on success; the C ABI copies again around master additions.

**C++ contract (`Radicle/include/fo4/forms/placed_reference.hpp`).** Every `set_placed_reference_*` edit returns false (or `PresentFieldEdit::record_missing`) when the record is absent. It throws `PlacedReferenceError` for a non-placed record, a malformed existing field, or a FormKey whose owner is neither the plugin nor one of its masters; otherwise it leaves every other subrecord untouched.
- `set_placed_reference_scale`: sets `XSCL`, or removes it for `nullopt`.
- `set_placed_reference_editor_id`: sets `EDID`, or removes it for an empty id.
- `set_placed_reference_record_flags`: replaces the header flags; the compression and persistent bits stay as stored.
- `set_placed_reference_comments` / `set_placed_reference_location_reference`: set `MNAM` / `XLRL`, or remove them for `nullopt`.
- `set_placed_reference_ownership`: sets `XOWN` (existing unknown bytes kept) and `XRNK`; a `nullopt` owner removes both and a `nullopt` rank removes `XRNK`.
- `set_placed_reference_enable_parent`: sets `XESP` {parent, flags} (existing unknown bytes kept), or removes it for `nullopt`.
- `set_placed_reference_linked_references`: replaces every `XLKR` with the given links in order; a `LinkedReferenceEdit::keyword` of `nullopt` writes a null form ID.
- `set_placed_reference_teleport`, `set_placed_reference_primitive` and `set_placed_reference_activate_parent_delay` only change a field that already exists, as GardenEngine's writeback does, and return `field_absent` otherwise. Primitive bounds are the stored (undoubled) `XPRM` values that `decode_placed_reference` returns.
- `Radicle/src/forms/placed_reference_edit.cpp:edit_placed` runs the edit on a copy and commits only when it returns true. `insertion_point` places a new subrecord before the first existing one that xEdit orders after it (`field_rank` uses xEdit FO4 order at pin `9fb016884bec138ea6c7b872cec831537d464c3e`: `wbRefRecord(REFR`, `wbRefRecord(ACHR` and `ReferenceRecord` for placed projectiles and hazards; signatures missing from the list are never anchors), and `single` treats a repeated field as malformed. `set_placed_reference_record_flags` keeps persistence (it decides the CELL child group) and compression (how the payload is stored). `set_placed_reference_ownership` follows Mutagen `Ownership.xml` (Owner, Int32 Unknown, NoCrime as a 4-byte bool); xEdit reads the flag from the low byte.

**C ABI contract (`Radicle/include/fo4/c_api_env.h:radicle_plugin_set_placed_*`).** Each returns `NOT_FOUND` when the plugin lacks the record and `INVALID_ARGUMENT` for a non-placed record or a malformed field. Edits that take a FormKey add any owner `env` loads as a master and keep masters in `env` load order; an owner `env` does not load is `INVALID_ARGUMENT`. A key whose `plugin` is NULL means "clear"; any other key must be a valid FormKey.
- `radicle_plugin_set_placed_record_flags` keeps the compression and persistent bits as stored.
- `radicle_plugin_set_placed_comments` with `comments == NULL` removes `MNAM`.
- `radicle_plugin_set_placed_ownership`: a cleared owner removes `XOWN` and `XRNK`; `has_rank == 0` removes `XRNK`.
- `radicle_plugin_set_placed_linked_refs` replaces all `XLKR` in order; a cleared keyword writes a null form ID.
- `radicle_plugin_set_placed_teleport`, `_primitive` and `_activate_parent_delay` only change a field that already exists and return `NOT_FOUND` when it does not. `_primitive` takes the stored `XPRM` bounds, as `radicle_placed_ref` reports them.
- `Radicle/src/c_api_ref_fields.cpp:with_masters` adds owners `env` loads as masters, then orders masters by load order; `run` maps the C++ outcome and exceptions to a status; the plugin changes only on `OK`.

**Test fixture (`Radicle/tests/test_environment_ref_fields.cpp`).** `Master.esm` has interior cell 000820 holding REFR 000821 (teleport, primitive, activate parents, links, ownership, enable parent) and ACHR 000822 (`ONAM`), plus FACT 000830, KYWD 000812 and LCTN 000813. `DLC.esm` adds FACT 01000831.

**Tests.** `Radicle/tests/test_environment_ref_fields.cpp` covers:
- all edits on REFR and ACHR, with exact subrecord order, kept unknown bytes, and the decoded values after a save and environment reload;
- clearing each field;
- bad input leaving the plugin byte-identical;
- the C ABI, adding a DLC master for an owner and keeping masters in load order.

**MSVC `/W4 /WX`.** `Radicle/tests/test_environment_ref_fields.cpp:RefFieldsTest::SetUp` named its loop variable `id`, shadowing the temp-directory `id` above it. MSVC reports C4456, which `/WX` turns into C2220, so `fo4_tests` did not build under the `native-windows-tests` job. The loop variable is now `form`.

**Shipped data.** Every placed reference in the seven official masters stores `XTEL` as 36 bytes, `XPRM` 32, `XOWN` 12, `XESP` 8, `XAPR` 8 and `XSCL` 4, all sizes these edits accept. No shipped `EDID` or `MNAM` on a placed reference has a byte above 0x7F.

**String encoding.** The C++ setters store the bytes they are given. The C ABI setters (`radicle_plugin_set_placed_editor_id`, `radicle_plugin_set_placed_comments`) take UTF-8 and store Windows-1252 through `Radicle/src/text/encoding.cpp:utf8_to_windows1252`, returning `INVALID_ARGUMENT` for text Windows-1252 cannot hold; see the encoding boundary section below. `set_placed_reference_comments` treats an empty string like `nullopt` and removes `MNAM`, as the contract above and `set_placed_reference_editor_id` already did: it used to write an `MNAM` holding one NUL, which reloads as an empty comment instead of none (GardenEngine's `ReferenceWriteback.Commit` passes null for blank comments, so it never sent `""`). Regression: `Radicle/tests/test_environment_ref_fields.cpp:EmptyCommentsRemoveMnamAndSurviveReload`, which failed before the fix through both the C++ and the C ABI path.

## Radicle/src/environment/base_records.cpp / c_api_base_records.cpp — GardenEngine #114 base-record getters

Ninth #114 slice, stacked on the writeback-fields slice. It covers the base records `CellService` resolves for placed references and sky setup, and the per-plugin record walk `AssetConverterMain.CollectUniqueNifPaths` does with `EnumerateMajorRecords()`.

Layouts come from xEdit FO4 (pin `9fb016884bec138ea6c7b872cec831537d464c3e`) and Mutagen upstream `Mutagen.Bethesda.Fallout4/Records` (`5f8f1adeac2c4aaba59d06779340c7aec3da24c5`):

- **LIGH `DATA`:**
  - Layout: Time, Radius, byte Color, Flags, Falloff, FOV, Near Clip, three flicker floats, Constant and Scalar (48 bytes, xEdit `SetOptionalFrom(10)`). Mutagen `Light.xml` then has a `Break`, Exponent, God Rays Near Clip and Value, another `Break`, and Weight, for 64 bytes in total. The shipped masters hold 48-, 60- and 64-byte `DATA`.
  - `Radicle/src/environment/base_records.cpp:decode_light` fails a `DATA` shorter than 48 bytes, longer than 64, or ending inside a member (a size that is not a multiple of 4); before, a 50- or 68-byte `DATA` decoded silently, against the header's contract (`BaseRecordsTest.DecodesLightsSplinesAndLayers`, records 0x803/0x804). Optional members are read only when present.
  - Flags are the raw xEdit/Mutagen `Light.Flag` bits.
  - `FNAM` fade, `NAM0` gobo, `LNAM` lens and `WGDR` god rays sit beside `DATA`.
- **BNDS `DNAM`:**
  - Layout: `f32` default tiles, `u16` default slices, a 2-byte bool (tiles relative to length), three float colors, wind sensibility, then (Mutagen `Break`) wind flexibility.
  - The payload must end on a member boundary (0/4/6/8/20/24/28 bytes). `TNAM` holds the texture set.
  - The three-float color follows xEdit. Mutagen's `BendableSpline.xml` declares `DefaultColor` as `AlphaFloat` (four floats), which would put wind sensibility at offset 24 and leave wind flexibility past a `Break`. All 12 shipped BNDS `DNAM` are 28 bytes, and bytes 20 and 24 hold wind values such as 2.0/0.5 and 4.0/0.75 rather than a color alpha, so the shipped data matches xEdit. `Radicle/src/environment/base_records.cpp:decode_bendable_spline` reads the xEdit layout.
- **LAYR:** `PNAM` parent layer.
- **LGTM `DATA`:**
  - Uses the CELL `XCLL` decoder, now parameterized by signature. The form-version-30 rule for the ambient block size applies to both.
  - The word where `XCLL` stores Inherits is `wbUnused(4)` in LGTM, so `inherits` stays empty. `WGDR` holds the god rays.
- **CLMT `WLST`:**
  - An array of 12-byte {Weather, Chance `s32`, Global} entries; a size that is not a multiple of 12 fails.
  - `FNAM`/`GNAM` hold the sun and glare textures.
- **WTHR `NAM0`:**
  - Color groups in `wbWeatherColors`/Mutagen `Weather.xml` order: sky upper, fog near, unknown, ambient, sunlight, sun, stars, sky lower, horizon, effect lighting, cloud LOD diffuse/ambient, fog far, sky statics, water multiplier, sun glare, moon glare, fog near/far high.
  - Each group is 4-byte colors for 8 times from form version 111 (`wbWeatherTimeOfDay`), otherwise 4 times.
  - The decoder returns a view with the group and time counts. A size that is not a whole number of groups, or fewer than 9 groups (Mutagen's first `Break`), fails.
  - GardenEngine's `.Day` is time index 1.
- **WTHR `FNAM`:** the fog distance floats in `wbWeatherFogDistance` order (day/night near, far, power, max, then the 119/120 height members), returned as an array.
- **SCOL parts:**
  - Each `ONAM` opens a part; each following `DATA` is an array of 28-byte placements (position, rotation, scale).
  - Unlike `forms::decode_static_collection_parts`, null `ONAM` parts are kept, because Mutagen's `Parts` list keeps them; GardenEngine skips them when resolving models.
  - A `DATA` without a preceding `ONAM` fails.
- **`Environment::plugin_records`:** every record version a plugin holds, in on-disk order.

**C ABI.** `Radicle/src/c_api_base_records.cpp` provides:
- `radicle_env_plugin_records`;
- `radicle_light_get_info`, `radicle_bendable_spline_get_info`, `radicle_layer_get_parent`, `radicle_lighting_template_get_info`;
- owned views `radicle_climate_decode` / `radicle_weather_decode` / `radicle_static_collection_decode`, each with a matching `_free`. The WTHR color view points into plugin bytes.

The CELL lighting fill moved into `c_api_env_internal.hpp` (`to_c_lighting`), so LGTM reuses `radicle_cell_lighting`.

**C ABI contract (`Radicle/include/fo4/c_api_env.h`).**
- `radicle_env_plugin_records` returns every record version `plugin_index` holds, in on-disk order; free the array with `radicle_free_record_array`. `Environment::plugin_records` returns an empty list for an unknown index.
- `radicle_light_info::flags` holds the raw `Light.Flag` bits from xEdit/Mutagen: 0x8 flicker, 0x20 off by default, 0x80 pulse, 0x400 shadow spotlight, 0x800 shadow hemisphere, 0x1000 shadow omni, 0x4000 non-shadow spotlight, 0x100000 ambient only. `gobo` is the `NAM0` texture, or NULL when absent.
- `radicle_weather_info::colors` is the raw `NAM0` view. Group `g` (0 sky upper, 1 fog near, 3 ambient, 4 sunlight, 7 sky lower, 8 horizon, 12 fog far, ...) at time `t` (0 sunrise, 1 day, 2 sunset, 3 night, 4-7 early/late sunrise/sunset) is R, G, B, unused at byte `(g * color_times + t) * 4`; NULL when absent. `fog_distances` holds the `FNAM` floats in order: day near, day far, night near, night far, day power, night power, day max, night max, then the height members.
- `radicle_lighting_template_info::lighting.has_inherits` is always 0, because LGTM stores an unused word where `XCLL` stores Inherits (`Radicle/src/environment/world.cpp:decode_lighting`, shared by `XCLL` and LGTM `DATA`).
- `radicle_layer_get_parent` returns `LAYR` `PNAM`; the key has `plugin == NULL` when there is no parent.
- `radicle_static_collection_decode` returns every `ONAM` part in order, including null Static links (`plugin == NULL`).
- The C++ decoders in `Radicle/include/fo4/environment/base_records.hpp` throw `WorldDecodeError` on a wrong signature or a malformed subrecord that is present. `Radicle/src/environment/base_records.cpp` takes the WTHR time count from xEdit `wbWeatherTimeOfDay` (8 from form version 111, 4 before); Mutagen breaks after Horizon (9 groups) and Moon Glare (17), and xEdit adds Fog Near/Far High at form version 119. BNDS `DNAM` members end at 4, 6, 8, 20, 24 and 28 bytes; any other size cuts a member.
- The LGTM test fixture in `Radicle/tests/test_environment_base_records.cpp` is `XCLL`-shaped `DATA` at form version 131: 72 required bytes, then fog far color, max, fade begin/end and an unused word.

**Tests.** `Radicle/tests/test_environment_base_records.cpp` covers:
- full, short and malformed LIGH/BNDS;
- LAYR with and without a parent;
- LGTM, CLMT and 8-time/4-time WTHR (including a bad group count);
- SCOL with null parts and orphan `DATA`;
- plugin record order;
- the C ABI.
- `BaseRecordsRealData.DecodesEveryShippedBaseRecord` (with `FO4_DATA_DIR`): every LIGH, BNDS, LAYR, LGTM, CLMT, WTHR and SCOL in the seven official masters decodes; every WTHR has at least 17 color groups and 8 fog distances.

## Radicle/bindings/csharp — GardenEngine #114 C# bindings

Tenth #114 slice, stacked on the base-record slice. This is the C# side GardenEngine will call in place of Mutagen. It lives in GARDEN-TOOLS next to the C ABI, so the two change together.

**Generated layer.** `tools/generate_interop.py` parses `fo4/c_api.h` and `fo4/c_api_env.h` and emits `Radicle.Interop/RadicleNative.g.cs`: 88 `DllImport` functions (87 before `radicle_abi_version`), every public struct as `LayoutKind.Sequential`, and the enums.

Mapping rules:
- **Scalars:** `size_t` becomes `nuint`, and C `int32_t` flags stay `int`.
- **Arrays:** scalar arrays become `fixed` buffers; pointer arrays (`const char* textures[8]`) become numbered `IntPtr` fields.
- **Pointers:**
  - Handles and C strings are `IntPtr` at their first pointer level.
  - Input `const char*` is a UTF-8 marshalled `string`.
  - Deeper pointers stay typed, such as `IntPtr**` for record arrays and `Landscape**` for owned views.
- **Coverage:** an unmapped type stops generation instead of being skipped, so the ABI cannot outgrow the binding silently.

**Layout proof.** The same script emits `tests/abi_layout_probe.cpp`, built as `radicle_abi_layout_probe`, which prints C `sizeof`/`offsetof` for every struct member. `AbiLayoutTests` compares each line with `Marshal.SizeOf`/`OffsetOf`, and also checks that it covered every generated struct. Changing one generated `int` to `long` fails the test.

**Staleness check.** `radicle_csharp_interop_up_to_date` regenerates into the build tree and compares with the committed files, ignoring line endings (`tools/check_generated.cmake` uses `compare_files --ignore-eol`): the generator writes the platform's line endings, and a Windows checkout may hold LF or CRLF depending on `core.autocrlf`. `radicle_csharp_interop_check_ignores_line_endings` (`tools/check_generated_eol.cmake`) runs the check against copies of the committed files with their line endings flipped; it failed before `--ignore-eol`. It runs wherever CTest finds Python 3, so a header change without regeneration fails CI.

**Safe layer:**
- `RadicleEnvironment`, `StringTables`, `Ba2Archive`, `AssetResolver` and `PluginFile` own their native handles through `IDisposable`.
- Results are copied into managed records. Two things stay borrowed: a `Record` handle (valid while its environment lives) and the `Ba2Archive` that `AssetResolver.Archive` returns. That archive keeps its resolver and throws `ObjectDisposedException` once the resolver is disposed (`Radicle.Interop/Archives.cs:Ba2Archive.Handle`). `StringTables` likewise throws once the environment it was loaded from is disposed, because the native tables dereference that environment (`Radicle.Interop/Localization.cs:StringTables.Handle`). Before these checks, both cases read freed native memory. Tests: `LifetimeTests.ABorrowedArchiveFailsOnceItsResolverIsDisposed`, `LifetimeTests.StringTablesFailOnceTheirEnvironmentIsDisposed`.
- Status codes map to `RadicleException` (status plus `radicle_last_error`), except lookups whose `NOT_FOUND` means "absent"; those return `null`/`false`.
- `Record` is a borrowed handle, valid while its environment lives, like Mutagen's link cache records.
- Native strings passed in are UTF-8 copies freed after each call.
- `RadicleLibrary.SetPath` / `RADICLE_NATIVE_PATH` pick the library through `NativeLibrary.SetDllImportResolver`, so Godot can point at its bundled `radicle.dll`.
  - **Registration.** `Radicle.Interop/Core.cs:RadicleLibrary` registers the resolver in its type initializer, which the runtime runs once under its own lock. `Radicle.Interop/RadicleNative.Init.cs` triggers it from `RadicleNative`'s static constructor, so even a direct `RadicleNative.*` call is resolved.
  - **Caching.** The CLR calls the resolver for every P/Invoke method the first time that method binds, so `Resolve` loads the library once and returns the cached handle.
  - **Late paths.** `SetPath` with a different path after that load throws `InvalidOperationException`.
  - **Before.** A late `SetPath` silently sent later-bound methods to a second library, mixing handles across two copies of Radicle. A second thread could also call before the resolver existed. Test: `LifetimeTests.TheLibraryPathCannotChangeOnceTheLibraryIsLoaded` (before the fix, the late path also broke unrelated tests with `DllNotFoundException`).

**Tests.** `Radicle.Interop.Tests` builds plugins, a GNRL BA2 and a STRINGS table in C#, and covers:
- load order, versions and EditorID lookup;
- plugin records, worlds, cells and cell children;
- placed-reference decode;
- the full `ReferenceWriteback` field set with save and reload;
- archive precedence and archived strings;
- sky and base records.

CTest runs them with `-DRADICLE_CSHARP_TESTS=ON`. The option is off by default so CI does not depend on NuGet.

**API contract of the safe layer** (formerly XML doc comments; the source-comment policy keeps them here):
- `Radicle.Interop/Core.cs`: `RadicleLibrary` chooses where the native library loads from and must be set before the first call; `RadicleLibrary.SetPath` takes the full path of `libradicle.so` / `radicle.dll`, and `RADICLE_NATIVE_PATH` is used when it is unset. The UTF-8 marshaller owns copies of the strings passed to a native call for the duration of that call. The status helper maps OK to true, NOT_FOUND to false, and throws `RadicleException` for anything else. The array helpers copy a `radicle_record` array, a string array or a byte buffer and free it with `radicle_free_record_array`, `radicle_free_string_array` or `radicle_free_bytes`.
- `Radicle.Interop/RadicleEnvironment.cs`: `RadicleEnvironment` is an immutable load order over `radicle_env` and is safe to query from several threads. Plugin paths and record versions are lowest priority first (winner last). `Record` is a borrowed record version, valid while its environment is alive. Terrain grids are 33x33 row-major copies, null when the subrecord is absent. Weather returns `NAM0` as groups x times colors, in the group order `radicle_weather_info` uses, plus the `FNAM` floats.
- `Radicle.Interop/Archives.cs`: `Ba2Archive` is an index-only view; `Extract` reads one entry on demand, is safe from several threads, and returns decompressed bytes, with texture entries as complete DDS files. `AssetResolver` puts loose files over archives and later archives over earlier ones, as the game mounts them. Its archive order is INI-listed archives, then each plugin's `<stem>.ba2` and `<stem> - *.ba2`, lowest priority first; a null data directory disables loose lookup. Archives it hands out are borrowed: disposing one does nothing, and it dies with the resolver.
- `Radicle.Interop/Localization.cs`: `StringTables` holds per-plugin tables for one language and returns text as the raw table bytes. The record name helper returns this version's `FULL`, either inline text or its string in the owning plugin's Normal table.
- `Radicle.Interop/PlacedReferences.cs`: the decoded REFR/ACHR/placed projectile or hazard mirrors `radicle_placed_ref`.
- `Radicle.Interop/PluginFile.cs`:
  - `Create` makes an empty plugin; `.esm` sets the master flag and `.esl` sets master + light.
  - `Write` is atomic: the file at the path is untouched on failure.
  - `CopyPlacedOverride` (Mutagen's `GetOrAddAsOverride`) copies a record version with its CELL/WRLD containment and masters.
  - Null removes `XSCL`; null or empty removes `EDID`.
  - Record flags keep the compression and persistent bits as stored; a null owner removes ownership and faction rank.
  - Teleport returns false when the reference or its existing `XTEL` is absent. Primitive takes the stored `XPRM` bounds and returns false when the reference or its existing `XPRM` is absent. The activate-parent delay returns false when the reference or that activate parent is absent.
- `Radicle.Interop.Tests/AbiLayoutTests.cs` reads `RADICLE_ABI_PROBE`, the built `abi_layout_probe` binary that prints C `sizeof`/`offsetof` lines. `Radicle.Interop.Tests/Fixtures.cs` is a minimal FO4 plugin writer (records and groups laid out as the engine reads them) and builds a GNRL v1 BA2 with stored chunks and a name table. The `EnvironmentTests.cs` fixture `Master.esm` holds STAT 000810 (model), WRLD 000800 with top cell 000801 and exterior cell 000802 (REFR 000803, ACHR 000804, LAND 000805), and interior CELL 000820 with REFR 000821; `Patch.esp` moves REFR 000803.
- `tools/generate_interop.py`: arrays of pointers become numbered fields, fixed buffers take only primitives, and a handle or C string is already one pointer level. The generated files' headers say edits must go through the generator.

## Radicle/tools/env_bench/env_bench.cpp:radicle_bench::run_env_bench — GardenEngine #114 load metrics

Eleventh #114 slice, stacked on the C# bindings. `Environment::load` parses every plugin fully into memory, where Mutagen reads plugins over 1 MB lazily. This tool produces the numbers that decide whether Radicle needs lazy loading.

- **Shared helper.** `run_env_bench` lives under `tools/`, not in the `fo4` library. `radicle_env_bench` (`tools/env_bench/main.cpp`) and `tests/test_env_bench_real.cpp:EnvBenchRealData.LoadsOfficialMasters` both compile it, so the CLI and the test measure the same thing.
- **Non-ANSI Data folders.** `Radicle/tools/env_bench/main.cpp:wmain` takes its arguments as wide strings on Windows, which fixes a non-ANSI Data folder. (A plugin file whose own name is outside the ANSI code page still fails in `Radicle/src/plugin/reader.cpp:read_plugin_from_owned_bytes`, which keys the plugin through `path::string()`.) With a narrow `main`, Windows converts `argv` through the ANSI code page, so a folder such as `env-bench-Žluť-ж` arrived as `env-bench-Älut-?` and `file_size` failed. The "no official masters" message prints the folder with `u8string()`, because `path::string()` throws for a name outside the ANSI code page. `tools/env_bench/non_ansi_data_dir.cmake` (CTest `radicle_env_bench_non_ansi_data_dir`) runs the tool on such a folder holding `testdata/SlowTime.esp`; it failed before `wmain` and passes after.
- **Errors.** `Radicle/tools/env_bench/main.cpp:wmain` runs `default_masters` inside its `try`. `std::filesystem::exists` throws for any stat error other than "not found" (for example EACCES on a folder the user cannot read), and before this the exception escaped `main` and terminated the process. CTest `radicle_env_bench_unreadable_data_dir` (`tools/env_bench/unreadable_data_dir.cmake`) locks a folder and expects exit 1 with an `error:` line; it reports skipped on Windows, because a POSIX permission change does not lock a folder there, and when permissions are not enforced (root).
- **What is timed:**
  - `Environment::load` over the given plugins (default: the official masters present);
  - `archive_load_order` + `AssetResolver::open` over the vanilla `Fallout4 - *.ba2` archives only;
  - `StringTables::load(env, <Data>/Strings, "en", &assets)`;
  - a walk of every `plugin_records` entry.
- **Memory (`sample_memory`).**
  - Windows: `GetProcessMemoryInfo` `WorkingSetSize` / `PeakWorkingSetSize`.
  - Linux: current RSS from `/proc/self/statm`, peak from `getrusage` `ru_maxrss` (KB).
  - Peak is process-lifetime. Inside `fo4_tests` it includes whatever earlier tests allocated, so use the standalone tool for clean numbers.
- **Test.** Gated on `FO4_DATA_DIR`; it skips unless the CI variable `FO4_DATA_DIR_LINUX` or `FO4_DATA_DIR_WINDOWS` is set. It has no thresholds: it only asserts the load succeeds and the counts are non-zero, and reports the figures via `RecordProperty` and stdout.
- **Limits.** Archives and string tables are opened for `Fallout4.esm` only, so `archives_ms` and `strings_ms` leave out the DLC archives and their string tables even when the DLC masters are loaded (the installed game has no loose `Strings` folder, and each DLC's tables, such as `Strings/DLCRobot_en.STRINGS`, are only inside its `- Main.ba2`); `load_ms` covers every listed plugin. `tests/test_env_bench_real.cpp` reads `FO4_DATA_DIR` with `std::getenv`, so on Windows a Data path outside the ANSI code page makes the test skip; the standalone tool takes wide arguments.

## Radicle/src/c_api_version.cpp:radicle_abi_version — C ABI generation probe

`Radicle/include/fo4/c_api.h:radicle_abi_version` returns `RADICLE_ABI_VERSION` (2 since the material swap exports; see the ABI 2 section below), the probe PR #277 proposed, carried into the environment stack so the C# bindings can reach it. A consumer calls it before any other entry point and refuses the library when it differs from the `RADICLE_ABI_VERSION` it was compiled against (`Radicle/docs/FO4IDE.md`). `Radicle/CMakeLists.txt` adds `src/c_api_version.cpp` to `radicle_c` and `tests/test_c_api_version.cpp` to `fo4_tests` with their own `target_sources` calls, so they do not conflict with the environment source lists. `Radicle/bindings/csharp/Radicle.Interop/RadicleNative.g.cs:AbiVersion` is regenerated from the combined headers; adding the probe without regenerating fails `radicle_csharp_interop_up_to_date`. The probe does not freeze the authoring API or add a capability registry.

## Radicle/CMakeLists.txt:fo4_apply_warnings — warnings-as-errors only at top level

`RADICLE_WARNINGS_AS_ERRORS` defaults to `PROJECT_IS_TOP_LEVEL`, the same way `RADICLE_BUILD_TESTS` does. Radicle's own CI still builds with `-Werror` / `/WX`. A consumer that pulls Radicle in with FetchContent (GardenEngine `opencw/native/radicle`) gets the warnings but not the hard failure. The case that forced this: GardenEngine CI run 554, job 1386, on GCC 15. GCC 15 raised a false `-Wmaybe-uninitialized` inside `std::optional<StaticCollectionPartView>` in `Radicle/src/forms/static_collection.cpp:decode_static_collection_parts`, and that failed the downstream build even though Radicle's own compilers accept the code.

## Radicle/tools/env_bench/env_bench.cpp:tally — where Environment::load memory goes

`run_env_bench` also reports what the loaded plugins hold: the file bytes each `Plugin::original_data` retains, `Record::decompressed_payload` capacity, `Subrecord::data` capacity, the subrecord count, the group count, and the `GroupChild`/`Subrecord` slot bytes (`node bytes`). `tests/test_env_bench_real.cpp:EnvBenchRealData.LoadsOfficialMasters` asserts that every plugin byte is retained and that subrecords were counted.

The official FO4 masters in the Data folder of a local Fallout 4 install, 2026-09-28:

| Build | Load time | Peak working set |
|---|---|---|
| Release | 4.2–5.9 s | 3.0 GB |
| Debug | 28.8 s | 6.15 GB |

The #114 brief's "~61.7 s / ~6.15 GB" matches the Debug peak exactly, so that figure was measured on a Debug build.

Release breakdown for 482 MB of plugins, 2,192,230 records and 9,501,965 subrecords:
- 460 MB of retained file bytes;
- 502 MB of decompressed payloads;
- 609 MB of subrecord data;
- 804 MB of node slots;
- about 650 MB of allocator overhead and environment indexes.

Each record's bytes were held twice: once in the file bytes or the decompressed payload, and again as one heap-allocated vector per subrecord. Making `Subrecord::data` a view into those buffers would remove the 609 MB copy and roughly 9.5 M allocations. That changes a type every reader and the writer use.

Not done, because nothing in GardenEngine has failed on memory. The Fallen World profile (652 plugins) loads in 15 s at about 4.7 GB inside GardenEngine, and every #114 parity test runs in one process on that load.

## Radicle/src/plugin/reader.cpp:read_record — compressed payloads are not retained

Nothing read `Record::decompressed_payload`; every writer only cleared it, because the writer re-emits records from `subrecords` and `record_original_bytes` slices the retained file bytes. `read_record` now inflates into a local buffer, and `parse_subrecords` copies out of it, so no view outlives the buffer. The field stays in `include/fo4/plugin/container.hpp`, because removing it means touching the files over 750 lines that clear it (`placed_reference_copy.cpp`, `formlink_lists.cpp`, `form_links.cpp`).

Proof: `EnvBenchRealData.LoadsOfficialMasters` asserts `payload_bytes == 0`, the full `fo4_tests` suite passes (590 tests), and `RealPluginPreservation.UnmodifiedFallout4MasterIsByteIdentical` passes on real data. On the official masters (Release, 2026-09-28), peak working set fell from 3,097,288 KB to 2,567,404 KB and load time is unchanged at about 4.8–5.0 s.

`QuestAliasRealData.DecodesEveryShippedQuest` and `QuestAuthoringRealData.RewritingUnchangedStagesAndObjectivesIsByteExact` fail with the same `QUST ALCL Level must be 0..3` errors on the unmodified reader, so those failures predate this change.

## Radicle/src/environment/material_swap.cpp:decode_material_swap — MSWP substitutions for GardenEngine

`decode_material_swap` returns each BNAM (original material) with the SNAM (replacement) and CNAM (color remapping index) that follow it, in record order. SNAM or CNAM before any BNAM, a repeated SNAM/CNAM inside one substitution, a CNAM that is not 4 bytes, or a path that is not one NUL-terminated string throws `WorldDecodeError`. FNAM is skipped because GardenEngine does not use it. Sources: the vendored Mutagen `Records/Major Records/MaterialSwap.cs` (`NewFormVersion = 112`) reads a record-level FNAM tree folder from form version 112, and before that requires every per-substitution FNAM to hold the same string; the raw bytes of `Fallout4.esm` `2489D9` (form version 131) read `EDID, FNAM "Interiors\Vault", BNAM, SNAM, BNAM, SNAM`.

`placed_material_swap` reads XMSP on every placed-reference signature. `primary_model_material_swap` returns the swap stored with the model `primary_model_path` chose: MODS for modeled records, MO2S when an ARMO's male world model (MOD2) was chosen and MO4S when the female one (MOD4) was; nothing when the record has no primary model. SCOL is the exception: `primary_model_path` returns nothing for it (its MODL is the precombined mesh), but `primary_model_field` still reads its own MODS. The engine applies a collection's swap to the whole assembled SCOL (`ReverseEng/Fallout4_Runtime_Research` PR 2, `RE-Work/Runtime/material-swap-engine-behavior.md`, section 1e: `BGSStaticCollection` is a `BGSModelMaterialSwap`, swapForm at +0x98). Vanilla masters have 20 SCOL records with MODS and 30 with MODC.

`Radicle/src/environment/material_swap.cpp:primary_model_color_remapping_index` reads the same model slot's C subrecord: MODC for modeled records and SCOL, and for ARMO the MODC inside the chosen world model (`armor_color_remapping_index`). xEdit defines the ARMO world models as `wbTexturedModel('World Model', [MOD2, MO2T], [wbMODC, wbMO2S, nil])` and the same with MOD4/MO4T/MO4S (source:TES5Edit dev-4.1.6 9fb016884bec `Core/wbDefinitionsFO4.pas`, ARMO). Mutagen's `Armor.xml` `WorldModel` maps MODL, MODT and MODS to the gendered signatures but not MODC, so it also reads MODC. MO2C exists only on ARMA (xEdit's `Biped Model`, Mutagen's `ArmorAddon.xml`); MO4C is on ARMA (`1st Person`) and in WEAP's `1st Person Model`, which `primary_model_path` does not choose. An earlier version of this function read MO2C/MO4C for ARMO and so returned nothing for every armor color index; `tests/test_environment_material_swap.cpp:ReadsThePrimaryModelColorRemappingIndex` (records 0x822 to 0x824) failed before the fix. A MODC belongs to the MOD2 or MOD4 it follows, so a male-only MODC is not the female model's index. The vanilla scan below found no ARMO MODC, so the fix changes no shipped record. It is exposed as `radicle_record_primary_model_color_remapping_index` (NOT_FOUND when absent) and `RadicleEnvironment.PrimaryModelColorRemappingIndex`. A C subrecord that is not exactly one 4-byte float throws `WorldDecodeError`. It is the engine's `BGSModelMaterialSwap::colorRemappingIndex` (+0x38, `FLT_MAX` when unset), which `BGSModelMaterialSwap::SwapMaterials` (OG RVA 0x53080) applies to every eligible geometry when no exact swap entry has a CNAM, even with no swap at all (research doc, sections 2c and 5.1). A scan of the vanilla masters found 325 MODC subrecords, all 4 bytes, placed after MODL and before MODT (207 STAT, 55 MSTT, 30 SCOL, 22 FURN, 8 ACTI, 2 ARTO, 1 ANIO), plus one ARMA MO3C. Proof: `tests/test_environment_material_swap.cpp:ReadsThePrimaryModelColorRemappingIndex` and `CApiDecodesAndFrees`; `bindings/csharp/Radicle.Interop.Tests/MaterialSwapTests.cs`.

The C entry points live in `include/fo4/c_api_materials.h` / `src/c_api_materials.cpp` rather than `c_api_env.h`, which is over the 750-line edit limit; for the same reason XMSP is its own call (`radicle_placed_reference_material_swap`) instead of a field on `radicle_placed_reference`, so that struct's layout is unchanged. `bindings/csharp/tools/generate_interop.py` reads the new header, and the ABI probe includes it.

Proof: `tests/test_environment_material_swap.cpp` (C++ and C API, including the malformed cases); `Radicle.Interop.Tests` ABI layout check; GardenEngine `MaterialSwapParityTests` on Fallen World matches Mutagen for 8,284 MSWP versions (19,821 substitutions), 6,779 model swap links and 308,440 reference swap links.

## Radicle/bindings/csharp/Radicle.Interop/Core.cs:RadicleLibrary.Resolve — ABI guard (GardenEngine #114)

Managed code used to trust whichever `radicle.dll` it found. `radicle_abi_version()` existed, but nothing compared it with the header the bindings were generated from, so a stale library beside newly generated bindings would fail as a corrupted struct read, or not at all. GardenEngine's CI probe compared the two, but normal startup did not.

`Radicle/bindings/csharp/tools/generate_interop.py:parse` reads `RADICLE_ABI_VERSION` from `fo4/c_api.h` before it strips preprocessor lines, and `emit_cs` writes it as `RadicleNative.CompiledAbiVersion`. The committed `RadicleNative.g.cs` is checked against the header by the existing `radicle_csharp_interop_up_to_date` test, so the constant cannot drift.

`RadicleLibrary.Resolve` is the DllImport resolver every wrapper type registers before its first native call, so it is the one place all entry points pass through. After it loads the library it calls `VerifyAbi`, which looks up `radicle_abi_version` with `NativeLibrary.TryGetExport` and calls it through a function pointer. It cannot use `RadicleNative.AbiVersion`, because binding that import would re-enter the resolver. A mismatch, or a library without the export, throws `RadicleAbiMismatchException`; the runtime propagates it to the caller of the first native call. The check runs once, on the load that caches `_handle`; later imports get the cached handle. A rejected library is not cached, so the next import loads and checks again. When it throws, `Resolve` calls `NativeLibrary.Free` on the handle it just loaded and rethrows. Without that the rejected DLL stayed mapped for the rest of the process and, on Windows, locked its file: `AbiGuardTests.ARejectedLibraryIsUnloaded` loads a copy of `radicle_abi_stub`, is refused, then deletes the copy, which failed with `UnauthorizedAccessException` before the fix.

When no path is configured the resolver used to return zero and let the runtime probe. It now calls `NativeLibrary.TryLoad(name, assembly, searchPath)` itself so the guard covers that path too; the runtime does not re-enter the resolver for that call (checked with a copy of the bindings and a `radicle.dll` in its own folder: the real library returns its ABI, `radicle_abi_stub` throws). When `TryLoad` fails, `Resolve` still returns zero, and the runtime then asks `AssemblyLoadContext.LoadUnmanagedDll` and the `ResolvingUnmanagedDll` event. A library found that way is not checked. A host that resolves native libraries through its own load context (for example a plugin context reading `deps.json` native assets) must call `RadicleLibrary.SetPath` or set `RADICLE_NATIVE_PATH` to get the guard. This is documented rather than closed, because throwing there would break hosts whose only route to the library is their load context. `RadicleNative.Init.cs` gives the generated class a static constructor that registers the resolver, so a raw `RadicleNative` call made before any wrapper type is touched is guarded as well.

Proof: `Radicle.Interop.Tests/AbiGuardTests.cs`. `TheFirstCallIntoALibraryWithAnotherAbiIsRejected` loads `radicle_abi_stub` (`bindings/csharp/tests/abi_stub.cpp`, which reports `RADICLE_ABI_VERSION + 1`) and gets `reports Radicle ABI 3 ... generated for ABI 2`. `TheFirstCallIntoALibraryThatIsNotRadicleIsRejected` loads `kernel32.dll`. With the `VerifyAbi` call removed from `Resolve`, both fail; with it, all 12 tests in the project pass.

## Radicle/include/fo4/c_api.h:RADICLE_ABI_VERSION — ABI 2 (GardenEngine #114)

The material swap entry points in `include/fo4/c_api_materials.h` (`radicle_material_swap_decode`, `radicle_material_swap_free`, `radicle_placed_reference_material_swap`, `radicle_record_primary_model_material_swap`, `radicle_record_primary_model_color_remapping_index`) were added while `RADICLE_ABI_VERSION` stayed 1. A library built before them also reports 1, so `RadicleLibrary.Resolve` accepted it and the first material swap call threw `EntryPointNotFoundException` instead of `RadicleAbiMismatchException` at the first native call. Reproduced by loading a `radicle.dll` built from #333 (`43ffec22`) through the #336 bindings: `AbiVersion()` returned 1 and `MaterialSwapDecode` failed with `Unable to find an entry point named 'radicle_material_swap_decode'`.

Any added, removed or changed export or public struct bumps `RADICLE_ABI_VERSION`; this one is ABI 2. After the bump the same #333 library is refused with `reports Radicle ABI 1, but Radicle.Interop was generated for ABI 2`.

Proof: `Radicle/bindings/csharp/Radicle.Interop.Tests/AbiGuardTests.cs:ALibraryFromBeforeTheMaterialSwapExportsIsRejected` loads `radicle_abi_stub_v1` (`bindings/csharp/tests/abi_stub.cpp` built by `Radicle/CMakeLists.txt` with `RADICLE_ABI_STUB_VERSION=1u`). With `RADICLE_ABI_VERSION` set back to 1 and the bindings regenerated, that test fails because the stub is accepted; at 2 it passes.

## PluginEditTool/native/src/plugin/RadiclePluginMasters.cpp:RadiclePluginBackend::cleanUnusedMasters

xEdit "Clean Masters" (#361). Uses Radicle `scan_master_usage` / `used_masters` and removes masters through `fo4::remap_plugin_masters`, which re-encodes every FormLink and record header for the shorter MAST table and, after #365, leaves misclassified non-form-ID slots byte-identical. It refuses (`MasterUsageIncomplete`) instead of removing anything when VMAD could not be fully scanned. A second boolean argument `false` is a dry run that returns `DRY RUN: would remove unused master(s) from X: ...` without staging; no second argument keeps the old apply behaviour. The record context menu (`web/src/recordActions.ts:cleanMasters`) previews first and confirms with that list. The masters panel (`web/src/MastersPanel.tsx:cleanMasters`) calls `masters.CleanUnused`, routed by `PluginBackendTarget.cpp` to the same method because `RadiclePluginBackend.cpp` cannot take another `masters.*` entry; its Save Plugin button stays enabled while a clean is staged. It is dispatched through `IPluginBackend::cleanUnusedMasters` from `PluginBackendTarget.cpp` (`backend.CleanUnusedMasters`) because `RadiclePluginBackend.cpp` is over the 750-line limit and cannot take another `invoke` entry. `listMasters` in that file still computes its own "used" flag with the same rule and should switch to `scan_master_usage` when the file is split.

## PluginEditTool/native/src/plugin/RadiclePluginCompare.cpp:RadiclePluginBackend::comparePluginTo

Reads the comparison file read-only and never adds it to `plugins_`, so it cannot be edited or saved and does not join the load order. It is parsed under the loaded plugin's filename because `fo4::diff_plugins` matches by FormKey. Only `.esp/.esm/.esl` paths are accepted. Dispatched as `backend.ComparePluginTo` through `PluginBackendTarget.cpp` (see the `cleanUnusedMasters` note on the over-limit dispatcher). A third argument (a FormKey) returns that record from both files as `Before` (the other file) and `After` (the loaded plugin): each subrecord's signature, size, the first 64 bytes as hex, and the text when the payload is a printable zero-terminated string; a side is `null` when the record is not in that file. `Differs` holds one flag per position, set when either side is missing or the signatures or full payloads differ; the dialog highlights from it rather than from the preview, which used to hide differences past byte 64 (`RadiclePluginCompareTests.cpp`, `PluginCompareDialog.test.tsx`). The web action (`web/src/recordActions.ts:compareTo`) puts the counts in the status bar and opens `web/src/PluginCompareDialog.tsx` through the dialogs provider: the changed records on the left, and the picked record's subrecords side by side by position, with differing rows highlighted. The detail view also says when `HeaderChanged` (record flags other than compressed, or signature) or `PlacementChanged` (a different parent group) is set, since such a record can have identical subrecords. `PluginCompareDialog.tsx:open` numbers its requests and applies only the latest, so a slow earlier load cannot replace the record picked after it. It is a separate view, not the conflict matrix, because the compared file never joins the load order.

## PluginEditTool/native/src/plugin/RadiclePluginMaintenance.cpp:RemoveIdenticalToMaster

ITM detection now calls `Radicle/src/plugin/maintenance.cpp:fo4::find_identical_to_master` (#360). The old `previousVersionNormalizesEqual` copied the whole plugin and rescanned every link of the earlier plugin (`copy_record_as_override`) for each candidate record. The Radicle version remaps only the earlier record into the current plugin's master context and compares flags and subrecords in order, with the same verdicts. Detection and removal cover nested records (REFR/ACHR in cell children, INFO in topic children, world children) through `fo4::remove_records`; see `Radicle/docs/code-notes/Radicle.md` on `find_identical_to_master`. The "checked" count is every record with an owner, nested ones included. Main only scanned and erased records directly under top-level groups, so it missed nested ITMs and could report an ITM CELL whose references differ.

## PluginEditTool/native/src/plugin/RadiclePluginCheck.cpp:RadiclePluginBackend::checkForErrors

Whole-plugin "Check for Errors" (#359) over every loaded plugin in load order, through `fo4::check_load_order`. Rows are capped at 500 by default and 100000 at most. `GetProblems` (`RadiclePluginAnalysis.cpp`) stays the per-record view.

## PluginEditTool/native/src/plugin/RadiclePluginCheck.cpp:RadiclePluginBackend::lootCleaningReport

Produces a LOOT masterlist entry (`dirty:` or `clean:` cleaning data: `crc`, `util`, `itm`, `udr`, `nav`; keys per loot-api.readthedocs.io "Cleaning Data"). The CRC is zlib `crc32` of the file on disk, so the operation refuses while the plugin has pending changes. `itm` comes from `fo4::find_identical_to_master`, `udr` is deleted placed references (`fo4::is_placed_reference_signature`) and `nav` is deleted NAVM records, all from `fo4::count_deleted_records`. Run it before cleaning, since it reports the dirty state of the current file.

## PluginEditTool/native/src/plugin/RadiclePluginDisabledOverride.cpp:RadiclePluginBackend::copyAsDisabledOverride

xEdit "Copy as disabled override" is copy-as-override plus Initially Disabled (record flag `0x800`) on the copy only. Evidence is in `PluginEditTool/docs/xedit-copy-workflows.md`. The `LOSES:`/`EXISTS:` prompt results from `copyAsOverride` are returned unchanged, so an override that already existed in the target is never flagged. `kInitiallyDisabled` is bit 11 (`0x800`) of the record flags in xEdit `Core/wbDefinitionsFO4.pas` (`dev-4.1.6`, "Initially Disabled") and must stay equal to the constant in `RadiclePluginMaintenance.cpp`.

## PluginEditTool/native/src/plugin/RadiclePluginCheck.cpp:RadiclePluginBackend::findUnreachable

Read-only "Build reachable info" (#364): runs `fo4::find_unreachable` over the whole loaded load order and returns `Total`, `BySignature` (counts over every unreached record) and `Records` (FormKey and signature), the last capped at 500 by default and 100000 at most. It used to return only the capped rows, so the per-signature summary counted a sample (vanilla `Fallout4.esm` has 44,175 unreached records against a 5000 cap). Dispatched as `backend.FindUnreachable` through `PluginBackendTarget.cpp`; the context-menu action (`web/src/recordActions.ts:findUnreachable`) asks for 20 records and shows the total, the full per-signature counts, and those records.

## PluginEditTool/native/src/plugin/RadiclePluginMasters.cpp:RadiclePluginBackend::updateOnam

xEdit "Mark all files without ONAM as modified" and its ONAM-update tool mode (`xeMainForm.pas:mniMarkallfileswithoutONAMasmodifiedClick`, `UpdateAllOnam`, `dev-4.1.6`) only dirty the header so saving rewrites ONAM. Radicle already rebuilds ONAM for master-flagged plugins on write (`Radicle/src/plugin/writer.cpp:write_plugin`, `onam.cpp:rebuild_tes4_onam`), so this runs `rebuild_tes4_onam` on a copy and stages it only when the override list actually changes. Non-master plugins are refused because the writer never emits ONAM for them. Wired as `backend.UpdateOnam` through `PluginBackendTarget.cpp` and the record context menu (`web/src/recordActions.ts:updateOnam`).

## PluginEditTool/native/tests/unit/PapyrusTargetTests.cpp — Papyrus compile tests do not depend on an installed Creation Kit

`PapyrusTarget Compile uses the built-in engine with the Auto note` and `PapyrusTarget Compile with engine=creationkit and no exe explains itself` exercise the path where no `PapyrusCompiler.exe` is found. `src/app/PapyrusTarget.cpp:PapyrusTarget::compile` asks `src/tools/ToolLocator.cpp:Locator::resolve(Tool::PapyrusCompiler)`, which falls back to `Locator::fallout4Root` and, with no setting or `FALLOUT4_PATH`, to the Windows registry and a scan of every drive's Steam library. On a machine with the Creation Kit installed (for example under `Papyrus Compiler/PapyrusCompiler.exe`) both tests found that compiler and failed. They now set `Settings::fallout4Path` to their own empty temp root, as `tests/unit/ToolLocatorTests.cpp` already does, so the game install is never consulted. `PAPYRUS_COMPILER_PATH` and `PATH` are still searched; when either provides a compiler the tests `SKIP` with that reason instead of failing. Product behaviour is unchanged: Auto still prefers an installed Creation Kit compiler.

The Papyrus target tests moved out of `PluginEditTool/native/tests/unit/ServiceTargetsTests.cpp` (916 lines, over the 750-line limit) into `tests/unit/PapyrusTargetTests.cpp`, built as `fo4ide_test_papyrus_target`. The fakes both files use (`tempRoot`, `Cleanup`, `touch`, `FakeRunner`, `ThrowingRunner`, `FakePlatform`) moved to `tests/helpers/ServiceTargetFakes.hpp` (`fo4ide::app::test`). The 36 test cases are unchanged apart from the two above. `PapyrusTargetTests.cpp:PapyrusTargetFixture` owns the `Locator` so the target's reference to it stays valid for the test body, and `PapyrusTargetTests.cpp:buildPex` compiles a trivial script with the built-in engine and returns the written `.pex` path, so the decompile tests exercise the real `PexFile` I/O.

## Radicle/src/text/encoding.cpp — one text encoding boundary and one case fold

Sources: Mutagen `Mutagen.Bethesda.Core/Plugins/Meta/GameConstants.cs` (FO4 `encodings: new(NonTranslated: MutagenEncoding._1252, NonLocalized: MutagenEncoding._1252)`), Mutagen `MasterReference_Generated.cs` (`ModKey.FromNameAndExtension(... _package.MetaData.Encodings.NonTranslated)` for `MAST`), Mutagen `ModKey` equality (`StringComparison.OrdinalIgnoreCase`).

- `Radicle/src/text/encoding.cpp:windows1252_to_utf8` / `utf8_to_windows1252` convert between stored FO4 text and UTF-8. Bytes 0x80–0x9F use the Windows-1252 table; the five bytes that table leaves undefined (0x81, 0x8D, 0x8F, 0x90, 0x9D) map to the C1 code point of the same value, so every byte string round-trips. Encoding returns nothing for invalid UTF-8 or a character outside Windows-1252.
- `Radicle/src/text/encoding.cpp:fold` / `compare_folded` / `equals_folded` / `hash_folded` implement .NET `OrdinalIgnoreCase` on UTF-8: each code point is replaced by .NET's ordinal uppercase mapping, then ASCII is lowered, so ASCII-only strings fold exactly as the old `ascii_lower` did and lower-case literals such as `.ba2` keep working. Lowering after uppercasing preserves the `OrdinalIgnoreCase` equivalence classes because uppercase mapping never yields `a`–`z`. Bytes that are not valid UTF-8 compare as themselves. The compare and hash walk both strings without allocating, because `PluginKey` ordering runs inside every `FormKey` map.
- `Radicle/src/text/upper_table.inc` is generated by `Radicle/tools/gen_upper_table.cs` (`dotnet run Radicle/tools/gen_upper_table.cs -- <path>/upper_table.inc`, a .NET file-based program pinned to `net8.0`, the runtime the C# bindings target). It keeps every code point whose `Rune.ToUpperInvariant` differs and which `string.Equals(..., StringComparison.OrdinalIgnoreCase)` treats as equal to it: 1,422 pairs in 197 runs on .NET 8.0.30, supplementary planes included. That filter is why the table comes from .NET rather than a Unicode database: .NET's ordinal casing does not fold U+0131 (dotless i) to `I` or U+017F (long s) to `S`, and does fold the 27 Greek letters with ypogegrammeni whose full uppercase is two characters (for example U+1F80 to U+1F88). A table built from Python's `str.upper` disagreed with C# `FormKey` on those 29 characters and folded no supplementary letter. Regression: `Radicle/tests/test_text_boundary.cpp:FoldMatchesDotnetOrdinalIgnoreCase`, which failed all five checks with the Python table.

**Where the boundary is applied.**
- Plugin names are UTF-8 everywhere. `Radicle/src/plugin/reader.cpp:read_plugin` names a plugin from `fo4::io::utf8(path.filename())`; it used `path::string()`, which on Windows gives the ANSI code page, so the C ABI returned `Café.esm` for `Café.esm` and a UTF-8 FormKey for that plugin was `NOT_FOUND`. `Radicle/src/plugin/tes4.cpp:read_tes4` decodes `MAST` from Windows-1252 and `Radicle/src/plugin/writer.cpp:replace_master_subrecords` encodes it back, throwing for a name Windows-1252 cannot hold; the writer previously stored a UTF-8 key's bytes as given. The decode/encode pair round-trips every byte string, so unmodified plugins still rewrite byte-identically.
- `Radicle/src/plugin_key.cpp:operator==`/`operator<`/`hash_value`, `Radicle/src/environment/environment.cpp:Environment::find_plugin`, `Radicle/src/environment/strings.cpp:list_directory`/`StringTables::load` and `Radicle/src/archive/assets.cpp:list_files`/`child`/`archive_load_order` use the shared fold. They used ASCII-only lowering (`std::tolower` in the C locale), so `Égout.esp` and `égout.esp` were different plugins, `CAFÉ_en.STRINGS` was not found for `Café.esm`, and `CAFÉ.ba2` was not loaded for it. C# `FormKey` already compared names with `OrdinalIgnoreCase`, so native and managed equality disagreed.
- `Radicle/src/environment/environment.cpp:Environment::find_by_editor_id` compares stored EDIDs through `windows1252_to_utf8` and the fold, so `CAFÉ` finds `Café` (`C9` and `E9` are the same letter).
- `Radicle/src/c_api_env_internal.hpp:stored_text` is the one place C ABI text output is converted. ASCII text is returned in place; other text is converted once per stored string into `radicle_env::text` under `radicle_env::text_mutex` and lives until `radicle_env_destroy`, so the existing lifetime rules are unchanged. It covers `radicle_record_info` `editor_id`/`name_text`/`model_path`, `radicle_placed_ref` `comments`/`map_marker_name_text`, `radicle_light_info::gobo`, the CLMT sun textures, MSWP material paths, TXST texture/material paths, `radicle_record_primary_model_path` and `radicle_env_landscape_texture_diffuse`. The last two first shipped returning the stored bytes, so the same record gave UTF-8 from `radicle_record_get_info` and Windows-1252 from `radicle_record_primary_model_path`; `test_text_boundary.cpp:CApiTextIsUtf8` now covers both with non-ASCII data and failed on them before. `Radicle/src/c_api_placed.cpp:build_view` converts VMAD script and property names and string values in the holder it owns. `radicle_env_find_editor_id` converts its query to Windows-1252 first and returns `INVALID_ARGUMENT` when it cannot, as the setters do; it used to return `NOT_FOUND`, so callers could not tell malformed or unrepresentable text from a missing EditorID. `test_text_boundary.cpp:CApiTextIsUtf8` checks an unrepresentable query (Cyrillic) and a truncated UTF-8 sequence, and failed on both before.

- BA2 entry names follow the same rule. `Radicle/src/archive/ba2_reader.cpp:Ba2Reader::open`, `Radicle/src/archive/ba2.cpp:read_ba2_general` and `Radicle/src/archive/ba2_texture.cpp:read_ba2_texture` decode the name table from Windows-1252, so `Ba2EntryInfo::path`, `Ba2GeneralEntry::path`, `Ba2TextureEntry::path` and `radicle_ba2_entry_info::path` are UTF-8. `Ba2Reader::find` encodes its UTF-8 argument back and looks it up by the stored bytes (name table) or by `compute_ba2_hashes` over the stored bytes (hash-only archives); `Radicle/src/archive/ba2_file.hpp:stored_ba2_paths` encodes every path for both writers and throws for one Windows-1252 cannot hold. Data evidence (2026-10-02, installed game): of the 507,570 names in the vanilla BA2 name tables, exactly three are non-ASCII, all in `Fallout4 - Voices.ba2` (`Sound\Voice\Fallout4.esm\RobotMrHandy\María_F.fuz`, `María_M.fuz`, `Sánchez_F.fuz`), and all three are valid Windows-1252 and invalid UTF-8. How the engine hashes a non-ASCII archive path (`BSResource::ID::GenerateFromPath`) is not in `ReverseEng/commonlib_NonVR` or `ReverseEng/Fallout4_Runtime_Research`; hash-only lookups hash the stored bytes as before. Before this, the names stayed raw: `PluginEditTool/native/src/archive/RadicleArchiveBackend.cpp` put them into JSON, which throws `type_error.316` when serialised, so listing `Fallout4 - Voices.ba2` failed, and a UTF-8 entry path never matched. That backend now also builds extraction paths from UTF-8 (`safeRelativePath`). Regressions: `Radicle/tests/test_text_boundary.cpp:ArchiveNamesAreWindows1252` and `PluginEditTool/native/tests/unit/RadicleArchiveBackendTests.cpp` "lists and extracts Windows-1252 entry names as UTF-8", both failing before.
Regressions, each failing before the change: `Radicle/tests/test_text_boundary.cpp:PluginKeysIgnoreNonAsciiCase`, `MasterNamesAreWrittenAsWindows1252`, `CApiTextIsUtf8`, `StringTablesAndAssetsMatchNonAsciiCase`, and `Radicle/tests/test_environment_ref_fields.cpp:CApiWritesTextAsWindows1252`.

**Not covered.**
- Localized string tables still return raw bytes with a size (`radicle_strings_lookup`); English tables are Windows-1252 and other languages differ, so decoding stays with the caller.
- Text stored in any other code page decodes as Windows-1252, the same as in Mutagen. It round-trips byte-exactly but displays as the Windows-1252 reading of its bytes. Which code page a non-English Creation Kit writes is not verified.

## Radicle/src/graph/conflict_graph.cpp:ConflictGraph::winner — Partial Form overrides do not win

Source: TES5Edit dev-4.1.6 `9fb016884bec138ea6c7b872cec831537d464c3e`.
- `Core/wbImplementation.pas:TwbMainRecord.GetWinningOverride` and `GetHighestOverrideOrSelf` walk the overrides from the highest load order down, skip every `IsPartialForm` override, and fall back to the master record when all overrides are partial. `GetIsWinningOverride` is false for a partial form.
- `TwbMainRecord.GetIsPartialForm` is `mrDef.CanBePartial and GetFlags.IsPartialForm`; `TwbMainRecordStructFlags.IsPartialForm` tests `$00004000`. `Core/wbInterface.pas:TwbMainRecordDef.Create` sets `rdfCanBePartial` only when the record's flag definition names "Partial Form", which `Core/wbDefinitionsFO4.pas` does for CELL, DIAL, QUST and WRLD (bit 14 of LCTN is "Unknown 14").
- `Core/wbConflict.pas` marks a partial form's missing fields `vnfIsPartialForm`/`vnfIgnore`, so they do not count as removals, and takes the last non-partial node as the value being compared. The fields a partial override does carry are still compared, so the override stays a conflict candidate.

`ConflictGraph::build` sets `RecordVersion::partial_form` from `RecordFlags::partial_form` (`1u << 14`) for those four signatures. `versions()` keeps every version in load order and `conflicts()` still lists a key overridden only by partial forms; `winner()` returns the last non-partial override, or the first version when every override is partial. It used to return the last version, so PluginEditTool's override patch (`PluginEditTool/native/src/plugin/RadiclePluginOverridePatch.cpp`, which walks `conflicts()` and copies `winner()`) and every other `winner()` caller treated a partial CELL, DIAL, QUST or WRLD as the full winning record. Regression: `Radicle/tests/test_conflict_graph.cpp:PartialFormOverridesKeepHistoryButDoNotWin`; the two QUST winner checks failed before the fix and the WEAP check pins that bit 14 is ignored on other signatures.

Not changed, by design: `Radicle/src/environment/environment.cpp:Environment::winner` (the GardenEngine read path) returns the last version, partial or not. That is Mutagen's rule, which GardenEngine's parity checks compare against: `Mutagen.Bethesda.Core/Plugins/Cache/Internals/Implementations/Internal/ImmutableLoadOrderLinkCacheCategory.cs:FillNextCacheDepth` fills the winner cache from the last listed mod backwards and keeps the first record it meets (`AddIfMissing`), and nothing under `Mutagen.Bethesda.Core/Plugins/Cache` tests `IsPartialForm` or the `0x4000` flag (Mutagen only uses `PartialFormFlag` while parsing subgroups, `Plugins/Binary/Translations/SubgroupsBinaryTranslation.cs`). xEdit's winner rule therefore applies to `ConflictGraph`, the conflict and patching view, and Mutagen's to `Environment`. Which one the game itself uses when it loads a partial form is not in `ReverseEng/commonlib_NonVR` or `ReverseEng/Fallout4_Runtime_Research` and stays unverified.

## PluginEditTool/web/src/CellViewport.tsx:CellViewport, PluginEditTool/web/src/MainShell.tsx:MainShell — latest-value refs (#77 tranche 2)

The React Compiler `react-hooks/refs` rule forbids writing `ref.current` during render. Refs that mirror the latest prop/callback for long-lived three.js or host-message listeners are now updated in a dep-less `useLayoutEffect`, which runs before any passive `useEffect` in the same commit, so listeners never see a stale value. `MainShell`'s `reloadMatrixRef`/`openByFormKeyRef` are declared before the host-message effect that reads them (with no-op initial values) because the compiler's `immutability` rule rejects modifying a value an earlier effect already captured.

## PluginEditTool/web/src/CellPanel.tsx:CellPanel — visibility undo/redo history

Undo/redo stacks moved from refs plus a render-tick counter into one `history` state object, so the hidden bar and Undo/Redo `disabled` flags read state instead of refs during render. `undoVisibility`/`redoVisibility` read the top snapshot from the render closure (`CellViewport` reaches them through its layout-effect-mirrored `onUndoRef`/`onRedoRef`); stack updates use functional `setHistory` so batched pushes stay capped at `HISTORY_CAP`. The stack transitions are the pure `PluginEditTool/web/src/util/undoHistory.ts:pushHistory`/`undoHistory`/`redoHistory`, tested in `util/undoHistory.test.ts` (round trip, the cap, a new toggle clearing redo) because `CellPanel` cannot reach its hide controls in jsdom without the WebGL viewport.

## PluginEditTool/web/src/NifViewport.tsx:setWireframe

The wireframe toggle mutates three.js materials owned by the main scene effect. Doing it through `setWireframe(mats, on)` keeps the mutation outside the component body the compiler analyses (same pattern as `util/cellInstances.ts:applyVisibility`).

## PluginEditTool/web/src/useDialogs.ts:useDialogs, PluginEditTool/web/src/Shell/mo2Freshness.ts:mo2FreshnessLabel

Split out of `dialogs.tsx` and `Shell/StatusBar.tsx` so those files export only components (`react-refresh/only-export-components`). `dialogs.tsx:DialogProvider` supplies `DialogContext` from `useDialogs.ts`.

## PluginEditTool/web/src/util/cellInstances.ts:applyHidden, PluginEditTool/web/src/Shell/ExplorerTree.tsx:ExplorerTree

Moved out of `CellViewport.tsx` and `MainShell.tsx` unchanged so each of those over-750-line files shrinks when touched (source line-limit guard). `cellInstances.ts` holds the instanced-mesh hide/visibility/selection helpers and `InstanceRef`; `ExplorerTree.tsx` is the lazy plugin/record tree. Tranche 2 also moved RecordView's conflict-status tables to `Workspace/conflictStatus.ts`; main had already moved the same code to `recordStatus.ts` (#317), so the merge of the updated #318 dropped `conflictStatus.ts` and `RecordView.tsx` imports from `recordStatus.ts` only.

## PluginEditTool/web/src/RecordView.tsx:RecordView, PluginEditTool/web/src/MainShell.tsx:MainShell — no synchronous setState in effects (#77 tranche 3)

`react-hooks/set-state-in-effect` flags any state update that runs synchronously in an effect body, including inside a local `async` function the effect calls. Effects now only start async work; setState happens in `.then`/`.catch` or timer callbacks. The resets moved out as follows:
- Per-record resets use the "store previous key in state" render pattern (`RecordView` on `matrix.FormKey`; `MainShell` on `activeKey`, which also switches to the Record/Grid tab). `RecordView` is not keyed by FormKey, so the reset is still needed.
- User-driven resets moved into the event handlers: `RecordView:openMenu` clears `elementActions`, the Conflicts Rescan button clears `conflicts`, and `Shell/TopBar.tsx:changeQuery`, `RecordPicker.tsx` and `MainShell:changeSearch` clear results on input. `changeSearch` is the only writer of `search` (input, clear button, `DetailRail`'s add-to-filter), so it reproduces the old effect's rule exactly: a value that trims to fewer than 2 characters clears `searchResults`. Dropping that clear instead left the previous query's hits in state, and the results panel only gates on `search.trim().length >= 2`, so the stale rows rendered again — and stayed clickable — under the next query for its whole 300 ms debounce. `MainShellBridge.test.tsx` searches `weapon`, shortens the query to `w`, then starts `armor`, and checks the `weapon` row is gone and `Searching…` shows before `armor`'s hits arrive.
- Resets the render already gates were dropped: `SpreadsheetPanel.tsx` shows `types` only with a plugin set, and `CellPanel.tsx` checks `!cell`. `ConditionsEditor.tsx` sets its no-backend state with lazy `useState` initialisers.
- `RecordView`'s `flashedField` state and its 1.8 s timer are gone. The row whose field matches `highlightField` gets `.rv-row-flash`, and its key includes `aiHighlightSeq` (next section), so every MCP highlight mounts a fresh row and replays the one-shot 1.8 s animation. `MainShell` holds `highlightField` for 3 s, so the class outlives the animation. The animation therefore has no fill mode: with `forwards` it held `background: transparent` on every cell from 1.8 s to 3 s, hiding the `.rv-c-*` conflict colours and the opaque `td.rv-sticky` column. Without it the row shows its normal colours from 1.8 s, as on main, where the class came off at 1.8 s. `RecordView.highlight.test.tsx` checks that neither `.rv-row-flash td` rule sets a fill mode. `MainShell`'s MCP handler switches to the grid tab, so a child render never calls the parent's `onTabChange`.

## PluginEditTool/web/src/RecordView.tsx:RecordView (flash, rowKey), PluginEditTool/web/src/MainShell.tsx:MainShell (aiHighlightSeq)

Every MCP write that names a field bumps `aiHighlightSeq` in `MainShell`'s message handler. `RecordView` gives the flashed row `data-flash-seq={highlightSeq}`, and `rowKey` keys it `` `${ri}:${seq}` `` from `flashKey`, a `{ field, seq }` state copied during render whenever a new non-empty highlight arrives. A new highlight therefore mounts a fresh `<tr>` and the one-shot CSS animation starts again with no effect or timer. `flashKey` is not cleared when `MainShell` drops `highlightField` after 3 s, so the row keeps its key and its DOM node then, and an inline cell editor open in it keeps focus (keying on `highlightField` directly sent the key back to `ri` at 3 s and remounted the row). The previously flashed row's key changes only when the next highlight starts. `RecordView.highlight.test.tsx` checks the row is the same node before and after the 3 s clear, with fake timers. Without the counter, `setAiHighlightField(sameField)` is a no-op, the row element is reused and the animation never replays.

Main's behaviour before this (traced on `ffd3a341`): `MainShell` holds the field for 3 s; `RecordView` copied it into `flashedField` in an effect and cleared it after 1.8 s. Main re-flashed the same field only once the 3 s `MainShell` timer had expired, or when the MCP write opened a different record (the `recordLoading` swap remounts `RecordView`). A second write to the same field within 3 s showed nothing. The counter keeps every case where main flashed and also flashes that repeat. `RecordView.highlight.test.tsx` drives two `McpLive` messages through `MainShell` and checks `data-flash-seq` changes and the `<tr>` is a new element.

## PluginEditTool/web/src/errors.ts:errorMessage, errorText — host-call rejection text

`errorMessage` takes anything a host call can reject with: an `Error`, a plain object carrying `message`, or a bare value. It returns the message text, or `undefined` when there is none, so callers keep their own fallback. `errorText` is the common fallback, `errorMessage(e) ?? String(e)`. Every web call site uses it except five in `PluginEditTool/web/src/MainShell.tsx` (over the 750-line guard, so left until the file is split).

## PluginEditTool/web — first ESLint tranche (#77)

Tranche 1 clears the lint findings that can be fixed without changing UI behaviour; the react-hooks rendering refactors and the two `react-refresh/only-export-components` findings in `dialogs.tsx` and `Shell/StatusBar.tsx` are left to later tranches. `PluginEditTool/web/eslint.config.js` runs `js.configs.recommended`, `tseslint.configs.recommended`, `reactHooks.configs.flat.recommended` and `reactRefresh.configs.vite` over `src/**/*.{ts,tsx}`.

The empty `catch` bodies are deliberate best-effort paths, and they carry no source comment. `no-empty` treats a block holding a comment as non-empty, so the explanation used to live in the source; it lives here instead, and the three files that need it are named in `eslint.config.js` with `'no-empty': ['error', { allowEmptyCatch: true }]`, which keeps the rule strict everywhere else. What each one swallows:

- `Blueprint/BlueprintPanel.tsx`: the palette search keeps the previous entries when `SearchRecords` fails, and the next keystroke retries; live validation keeps the last result when `Validate` fails, and the next edit revalidates.
- `CellPanel.tsx`: the 200 ms `GetGeometryBatchProgress` poll is ignored when it fails; the batch call itself reports the real failure.
- `ConditionsEditor.tsx`: a failed `ResolveFormKeyLabels` leaves the labels as they are; they are cosmetic and every row keeps its raw FormKey.

`MainShell.tsx:HostMessage` types what the host posts to the page. Only the fields each `Type` reads are declared: `SetStatus.Text`, `SetProgress.Value`, and the `McpLive` fields. A `SetProgress` message without `Value` now sets progress to `null` (hidden) instead of `undefined`, which is what the state's `number | null` says.

`NifPanel.tsx` declares `onDrop` after `loadGeo`/`loadTree`/`loadMaterial`. `react-hooks/immutability` rejected the earlier order with "Cannot access variable before it is declared" once per loader, because a memoised callback that captures them from an earlier render does not see later ones. The handler keeps `useCallback(..., [nif, mode])`. Every loader call it makes passes an explicit path (`loadGeo(path)`, `loadTree(path)`, `loadMaterial(path)`), and the only other state a loader reads is `getNif()`/`getMaterial()`, called fresh inside it, so nothing observable is read through the captured closures: `src/NifPanelDrop.test.tsx` types one NIF path and drops another, and the staged path is what reaches the host, under both declaration orders. Moving the declaration alone clears the three errors; the `react-hooks/exhaustive-deps` warning on that callback (`missing dependencies: 'loadGeo', 'loadMaterial', and 'loadTree'`) is the one `main` already has.

`src/errors.test.ts` pins that `errorText` produces the same text as the helper it replaced for `Error`s, message-carrying objects, empty messages, bare strings, `null`, `undefined` and numbers. `src/MainShellBridge.test.tsx` covers the startup `appInterop` wiring, the environment reload with its pending-changes prompt, and the batch-copy failure text.

Validation: `vitest run` 122/122 in 15 files, `tsc -b` clean, `vite build` clean (the pre-existing 1.08 MB chunk warning only), `eslint .` 34 errors and 16 warnings, `git diff --check` clean. The 34 errors are the react-hooks and react-refresh findings the later tranches take; `no-explicit-any` and `no-empty` are 0. The 16th warning is the `NifPanel.tsx:onDrop` `exhaustive-deps` one above, which `main` also has and this tranche leaves in place.

## PluginEditTool/native/cmake/Dependencies.cmake:FetchContent_Declare — archives instead of git clones (WebView2 is not hashed)

Windows native-ci configure hung forever inside the git clone of nlohmann_json (run 585 / job 10748 from 20:10:04 UTC; run 597 / job 10846 from 21:01:50 UTC; both cancelled). The log stops at `Cloning into 'nlohmann_json-src'...`, while other runs passed the same step in seconds. A git clone driven by FetchContent has no timeout, and on the Windows runner it can also block on a credential helper. Every `GIT_REPOSITORY` dependency in this configure therefore carried the same risk: nlohmann_json, spdlog, reproc, Catch2 and cpp-httplib in `Dependencies.cmake`, plus the zlib fallback in `PluginEditTool/native/CMakeLists.txt:fo4ide_zlib`. The Windows lane always fetches that zlib because it passes `CMAKE_DISABLE_FIND_PACKAGE_ZLIB=ON`.

Each of these now downloads a fixed archive with `URL_HASH SHA256` and `INACTIVITY_TIMEOUT 60`, at the same version as before. CMake's downloader drops a stalled transfer after 60 s, ExternalProject retries it, and git is out of the path entirely. The WebView2 NuGet fetch (`Dependencies.cmake:webview2_sdk`, version `1.0.4129.50`) was already a URL download and gained the same timeout, but it has no `URL_HASH`: the repo records no SHA256 for that package version (its only lock entry, `PluginEditTool/FO4RecordEditor.Tests/packages.lock.json`, pins 1.0.4022.49), so none was added. `Radicle/CMakeLists.txt:googletest` gained both the timeout and a hash.

- nlohmann_json uses the official release asset `json.tar.xz`. Its SHA256 (`42f6e95c…dff7aa`) matches the value published in the v3.12.0 release notes.
- The others use GitHub-generated tag archives, hashed from downloads on 2026-10-05. If GitHub ever regenerates an archive, configure fails loudly on the hash mismatch instead of hanging. Refresh the hash only after confirming the tag still points at the same commit.
- Bumping a dependency means changing both its URL and its hash.

zlib keeps its `GIT_REPOSITORY`/`GIT_TAG` form in `PluginEditTool/native/CMakeLists.txt:fo4ide_zlib`, because `.github/scripts/pinned-zlib.sh` and the Windows zlib step in `.github/workflows/native-ci.yml` parse that form (one 40-hex `GIT_TAG` after the madler/zlib `GIT_REPOSITORY`) to fetch and cache the pinned commit, and every Windows lane passes that cache through `FETCHCONTENT_SOURCE_DIR_FO4IDE_ZLIB`.

`.github/workflows/native-ci.yml` also sets `timeout-minutes: 15` on each Configure step as a backstop. A hang anywhere else in configure now fails that step instead of holding the job until the 60/90-minute job limit or a manual cancel.

## .github/workflows/native-ci.yml:workflow_dispatch, native-linux, native-windows-tests, plugineditool-native-windows-cross

The optional `lane=radicle-cross` dispatch runs only `radicle-windows-cross`, the existing Radicle clang-cl/xwin build. `native-linux`, `native-windows-tests` and `plugineditool-native-windows-cross` each have `github.event.inputs.lane != 'radicle-cross'` in their `if:` and are skipped; the default `full` runs them as before. Main has no gate job (`e4b8785a` removed the docs-only gate so every pull request runs the full build), so push and pull-request runs execute the same jobs as main and the `lane` input is only read on `workflow_dispatch`.

The Windows Release run retains `fo4_tests.exe` and its `radicle.dll` dependency as `radicle-msvc-release-tests` for seven days. This permits official-data tests on the machine with Fallout 4 installed without rebuilding there. Artifact metadata must identify the tested commit: the upload carries `build-info.txt` (commit SHA, ref, run ID, target triple); downloading a binary is not a test result. No game payloads or debug symbols are uploaded.

## Radicle/CMakeLists.txt:FO4_TESTDATA_DIR, gtest_discover_tests

`fo4_tests` uses the relative `testdata` fixture directory, and CTest runs its discovered cases from the Radicle source directory. A downloaded CI executable must likewise run from the matching checkout's `Radicle` directory. This avoids embedding the CI checkout path in the fixture lookup while keeping committed fixtures separate from the installed-game directory supplied through `FO4_DATA_DIR`.

## Radicle/tests/test_pex_corpus.cpp:compareCode, Radicle/src/c_api_pex.cpp:CodeBuilder

The ABI 9 corpus proof compares every exported row and field against the decoded C++ PEX model, including flattened ranges, property handlers, debug lines, flags, typed names, defaults and every operand. Floats compare by IEEE-754 bits so signed zero and NaN payloads are not hidden by approximate equality. Mismatch diagnostics include file, object, function and the differing field. Synthetic mutations must fail without changing aggregate counts.

## Radicle/include/fo4/c_api_pex.h:radicle_pex_object_counts, Radicle/bindings/csharp/Radicle.Interop/PexFile.cs:PexObjectCounts

The pre-ABI-9 snapshot function count remains state functions only for compatibility. The ABI 9 object-code function array includes those functions plus property getters and setters. Native and managed both-handler fixtures assert both counts and handler indexes. This clarification changed no layout or ABI version beyond the ABI 9 bump that added the object-code view.

## Radicle/src/c_api_pex.cpp:CodeBuilder::str, Radicle/bindings/csharp/Radicle.Interop/PexCode.cs:PexValue

The decoded C++ model retains string values rather than encoded reference indexes. The code-view contract therefore guarantees exact semantic bytes and selects the first equal string-table entry. It does not promise the original encoded index. Duplicate entries remain visible through the table API; native and managed fixtures explicitly encode a later duplicate and verify first-entry canonicalization. This matches Garden's PEX-to-IR need for semantic values without expanding the ABI.

## Radicle/src/c_api_pex.cpp:CodeBuilder (variables), Radicle/bindings/csharp/Radicle.Interop/PexCode.cs:PexVariable

The PEX parser reads a value for every object variable and struct member, so `has_default` is 1 for every parsed file and `PexVariable.DefaultValue` is never null on that path. "No initializer" arrives as a value of type NONE. The flag stays in the ABI because the C++ model holds the default as optional. Consumers check `Type == None`. Proof: `tests/test_c_api_pex.cpp` asserts `has_default == 1` for the fixture variable.

## Radicle/bindings/csharp/Radicle.Interop/PexCode.cs, Radicle/bindings/csharp/Radicle.Interop/RadicleNative.g.cs

The managed model types and the generated raw structs share names (`PexValue`, `PexFunction`, `PexState`, `PexObjectCode` and others), following `PexHeader` and `PexObjectCounts`. Code inside `Radicle.Interop` resolves to the model type. A consumer outside it that imports both `Radicle.Interop` and `Radicle.Interop.Native` gets CS0104, so it qualifies the raw structs as `Native.X` (as `PexCode.cs:ReadCode` does) or aliases them. Renaming the generated structs would change every existing `Native` type name, so the bindings README documents the rule instead. No GardenEngine file imports both namespaces today.

## Radicle/src/plugin/load_order.cpp:build_runtime_form_id_context

The main integration retains the published flag-or-extension classification in `Radicle/src/forms/weather.cpp:resolve_master_kind` and the caller-supplied resolved-order boundary. `RADICLE_ABI_VERSION` is unchanged because this branch adds no public exports or layouts. `Radicle/CMakeLists.txt:gtest_discover_tests` keeps the portable fixture working directory together with main's generator test; the required native and offline managed CI workflow comes from main. Existing OG/AE evidence remains static; NG byte checks, external limits and live dependency behavior stay open in #28/#389.

## PluginEditTool/FO4RecordEditor.Core/Services/WriteService.Esl.cs:CompactToEslCore / CheckEslEligibilityCore / EslSlots

The legacy Mutagen path (reached through `BackendInterop` and `PluginToolExecutor` compact_to_esl / check_esl_eligibility) follows the native backend's slot rules (`PluginEditTool/native/src/plugin/RadiclePluginSupport.hpp:eslSlots`, which uses `Radicle/src/plugin/mutation.cpp:allows_hardcoded_range`), with one stricter condition. `EslSlots` starts at 0x001 only when the HEDR version is at least 1.0, the plugin has a master, and `HasForeignReference` finds a record FormKey or form link whose ModKey is another mod; otherwise it starts at 0x800. The native floor has no cross-mod check, so the legacy floor can be stricter than `eslSlots`. Native IDs below that floor or above 0xFFF are out of range, and the range text and capacity come from those bounds; the old code hard-coded 0x800-0xFFF and missed low IDs. The four methods moved out of `WriteService.cs` because that file is over the line limit.

After a compaction the in-memory HEDR next object ID is the first free in-range ID at or above the last assigned ID + 1 (see `PluginEditTool/FO4RecordEditor.Core/Services/WriteService.Esl.cs:LowerHedrNext` below), so records created before saving never reuse an occupied ID. On save, `save_plugin` keeps Mutagen's default `NextFormIDOption.Iterate`, which writes the highest native ID + 1 (`Mutagen.Bethesda.Core/Plugins/Binary/Translations/ModHeaderWriteLogic.cs:SetNextFormID`); after compaction that is at most 0x1000, so the saved value is always in range. Mutagen's save enforces the cross-mod condition. The vendored Mutagen source (`PluginEditTool/Mutagen/`) has no version string; GitVersion derives it from upstream tags, and the editor last pinned NuGet Mutagen 0.53.1 (`docs/internal/KNOWLEDGE.md`), so the version is unverified. In `PluginEditTool/Mutagen/Mutagen.Bethesda.Core/Plugins/Binary/Translations/ModHeaderWriteLogic.cs:HandleDisallowedLowerFormIDs`, a record whose native ID is below `GameConstants.Fallout4.DefaultHighRangeFormID` (0x800, `PluginEditTool/Mutagen/Mutagen.Bethesda.Core/Plugins/Meta/GameConstants.cs`) throws `LowerFormKeyRangeDisallowedException` unless `_modKeys` is non-empty. `_modKeys` is filled by `AddMasterCollectionActions` in Iterate mode from record FormKeys and form links whose ModKey is neither the plugin's nor null, so a master in the header alone does not allow lower IDs. `save_plugin` uses `MastersListContent = Iterate` and the default `LowerRangeDisallowedHandler` (`PluginEditTool/Mutagen/Mutagen.Bethesda.Core/Plugins/Binary/Parameters/BinaryWriteParameters.cs`, an `AddPlaceholderMasterIfLowerRangeDisallowed` with no ModKey, which throws in that case). The ESL flag is not consulted there. A plugin with a master but no cross-mod links that keeps any record below 0x800 fails save_plugin with "Lower FormKey range was violated", so `EslSlots` keeps such a plugin at 0x800 and above. Test: `PluginEditTool/FO4RecordEditor.Tests/EslPolicyTests.cs:PluginWithMasterButNoCrossModLinks_UsesHighRangeSoMutagenSaveAccepts`; the link-bearing 0x001 case is `PluginWithMasterAndVersionOne_KeepsLowRecordsAndUsesFullRange`.

`PluginEditTool/FO4RecordEditor.Core/Services/WriteService.Esl.cs:SetLightFlagCore` (reached through `MastersInterop.SetLight` and the `set_light_flag` tool) uses the same `EslSlots` bounds. Flagging a plugin light is refused while any native record sits outside them, with "Cannot flag '<name>' light: N native record(s) sit outside <range>. Run compact_to_esl first."; the old code only warned above 0xFFF and had no floor. When every native record is in range and the HEDR next object ID is above 0xFFF and above the highest native ID + 1, it is lowered to the highest native ID + 1 (or `first` with no records) and the message says so, matching the native `Radicle/src/plugin/flags.cpp:set_plugin_light_flag` lowering. The floor is the legacy `EslSlots` floor, not the native one, so the flag agrees with what `save_plugin` accepts. Tests: `PluginEditTool/FO4RecordEditor.Tests/EslPolicyTests.cs` (`SetLight_*`) and `WriteServiceTests.cs:SetLightFlag_RefusesWhenRecordsAreOutOfEslRange`.

## PluginEditTool/FO4RecordEditor.Core/Services/WriteService.Esl.cs:LowerHedrNext — HEDR next after compaction

`WriteService.Esl.cs:CompactToEslCore` sets the in-memory HEDR next object ID to the first ID at or above the last assigned ID + 1 that no native record occupies, or 0x1000 when the range is full. The native compaction (`Radicle/docs/code-notes/Radicle.md`, `compactToEsl` note) stores the literal last assigned + 1 and relies on `Radicle/src/plugin/mutation.cpp:reserve_local_form_key` to scan past occupied IDs. Mutagen's `PluginEditTool/Mutagen/Mutagen.Bethesda.Core/Plugins/Allocators/SimpleFormKeyAllocator.cs:GetNextFormKey` takes `NextFormID++` with no check, so the C# value is advanced past occupied IDs in advance. The literal value is also what xEdit stores (`source:xEdit xEdit/xeMainForm.pas:13370-13371 @93cc0bc5`, `UpdateNextObjectID`). Example: a masterless plugin with records 0x801 and 0x5000 compacts 0x5000 to 0x800; the old code set next to 0x801, which the 0x801 record already occupies, so the next created record reused its form ID. Save is not affected, because `Iterate` rewrites next on save (see above).

`WriteService.Esl.cs:LowerHedrNext` holds the lowering rule for `CompactToEslCore` (its already-compatible branch) and `SetLightFlagCore`: when next is above 0xFFF and above the highest native ID + 1 (or the floor with no records), next is lowered to that value. The already-compatible branch lowers whether or not the light flag is set, as the native all-in-range branch of `RadiclePluginCompact.cpp:RadiclePluginBackend::compactToEsl` does through `lowered_light_next_form_id`. The 0x001 legacy floor in `WriteService.Esl.cs:EslSlots` is kept per the 2026-10-10 owner decision (Mutagen-safe floor). A full range returns 0x1000: when every ID from the last assigned ID + 1 to 0xFFF is occupied, the loop stops at 0xFFF + 1, which Radicle's `reserve_local_form_key` also reads as a full plugin. The already-compatible lowering invalidates the mod index and notifies listeners, as `SetLightFlagCore` does after a header change. Tests: `PluginEditTool/FO4RecordEditor.Tests/EslPolicyTests.cs:CompactToEsl_InMemoryNextObjectIdSkipsRecordsAlreadyInRange`, `CompactToEsl_FullRangeAfterCompactionSetsNextToTheCeiling` and `CompactToEsl_AlreadyCompatiblePluginLowersOutOfRangeNextObjectId`.

Differences from xEdit (`93cc0bc5`) in `WriteService.Esl.cs:CompactToEslCore`, none of them proven against the game yet:
- xEdit offers compaction only for a file that is neither light nor an update (`xEdit/xeMainForm.pas:13154-13158`). Here an already-light plugin runs the same logic.
- When nothing needs renumbering, xEdit stops with "Nothing to do" (`xEdit/xeMainForm.pas:13206`) and leaves the HEDR next object ID alone. The already-compatible branch here lowers an out-of-range next. Whether the game or the CK reads that field is open in [RE] GARDEN-TOOLS#479 (Q2, Q3).
- A record whose object ID is below 0x800 in a plugin without the hardcoded range gets a null file ID in xEdit (`Core/wbImplementation.pas:3590-3600`), so xEdit does not treat it as the plugin's own record and does not renumber it. Here it is counted as native and renumbered. What the engine does with such a record is #479 Q1.

## PluginEditTool/native/src/plugin/RadiclePluginCompact.cpp:RadiclePluginBackend::compactToEsl (already in range)

A plugin whose native FormIDs are all in the target range returns "already ESL-compatible". When nextFormID is above `last` and above the highest own local ID + 1, it is also lowered by `Radicle/src/plugin/mutation.cpp:set_hedr_next_form_id` to the highest own local ID + 1, or to the range floor when there are no own records, so `reserve_local_form_key` starts past every used ID. Otherwise the value and the message are unchanged. This is Radicle's choice, not xEdit's renumber value (last newly assigned ID + 1, see the `compactToEsl` note in `Radicle/docs/code-notes/Radicle.md`), because nothing is renumbered here. `set_hedr_next_form_id` clears the plugin-level `original_range` as well as the TES4 one, so the writer does not reuse the original bytes for a HEDR-only change.

Out-of-range nested records and out-of-range top-level records that have form ID labelled child groups are refused before anything is staged, by `PluginEditTool/native/src/plugin/RadiclePluginSupport.hpp:eslCompactionBlockers`, which `checkEslEligibility` also runs (`PluginEditTool/native/src/plugin/RadiclePluginCompact.cpp:RadiclePluginBackend::compactToEsl`). The success message and, when the light flag is not set, the already-compatible message end with `RadiclePluginSupport.hpp:lightFlagHint`, which points to `masters.SetLight`. Radicle's save never sets the light flag from the file extension (see `Radicle/docs/code-notes/Radicle.md`).

Performance (measured 2026-10-09, MSVC Release): compacting N out-of-range top-level records took 89 ms (500), 281 ms (1000), 1052 ms (2000) and 4430 ms (4000), roughly quadratic. Each `Radicle/src/plugin/mutation.cpp:renumber_local_record` call copies the whole plugin twice (once itself, once in `rekey_form_links`) and rescans every record and FormLink (`Radicle/src/schema/form_links.cpp:rekey_form_links`). The light range caps N at 4095, so the worst case is a few seconds for a one-off command; it is left as is. A batch renumber (one pass with an old-to-new map) is the fix if it ever matters. Tests: `PluginEditTool/native/tests/unit/RadiclePluginEslTests.cpp:Native CompactToEsl lowers an out-of-range HEDR next ID when every record is already in range` (sections: master with next 0x5000, masterless with next 0x2000, master with next exactly 0x1000).

## .github/workflows/plugin-editor-ci.yml

`native-ci.yml` runs only the native/CTest build and its push `paths-ignore` skips `PluginEditTool/FO4RecordEditor*/**` and `PluginEditTool/Mutagen/**`, so the managed tests had no CI. This workflow runs them on push to `main`, on every pull request and on `workflow_dispatch`, with the push filtered to `PluginEditTool/FO4RecordEditor*/**`, `PluginEditTool/Mutagen/**`, `PluginEditTool/README.md`, `PluginEditTool/docs/MCP_SETUP.md` and the workflow file. `.github/workflows/plugin-editor-ci.yml:on.pull_request` has no path filter so that both plugin-editor-ci contexts can be required status checks on `main`: a required check that never reports would block merging. They were added to the `main` protection rule on 2026-10-10 (GT-064 on the Issue-Tracking wiki); every pull request runs the jobs either way. The two docs are inputs: `FO4RecordEditor.Tests/ShippedDocsTests.cs` reads them. The test projects' `ProjectReference` graph is `FO4RecordEditor` -> `FO4RecordEditor.Core` -> `Mutagen.Bethesda.Fallout4` -> `Mutagen.Bethesda.Core` -> `Mutagen.Bethesda.Kernel`, all under those paths; nothing references `Radicle/` or `PluginEditTool/native`, so `native-ci.yml` needs no change.

SDK pin: `10.0.401` builds `net9.0-windows` (`FO4RecordEditor.Tests`) and `net10.0` (`FO4RecordEditor.Core.Tests`) and the `net8.0` library projects through restored targeting packs. The `net9.0-windows` test host needs the 9.0 runtime and Windows Desktop runtime, which `setup-dotnet` only installs as part of an SDK, so the Windows job also installs SDK `9.0.317`, which bundles runtime `9.0.19` (the version the local validation ran on). `10.0.401` is the highest SDK installed, so `dotnet` resolves to it. Both are exact versions in the runner tool cache, matching `bgs-ci.yml`; no `global.json` exists under `PluginEditTool`, so nothing else selects an SDK.

Locked restore: `RestorePackagesWithLockFile` is set only in `FO4RecordEditor.Tests/FO4RecordEditor.Tests.csproj` and `FO4RecordEditor.Core.Tests/FO4RecordEditor.Core.Tests.csproj`, and each has a committed `packages.lock.json`. A lock file records the whole transitive graph through `ProjectReference`, including the vendored Mutagen packages, so the shared libraries stay unlocked (the same reasoning as `Radicle/bindings/csharp`, see `radicle-managed-ci.md`). The workflow restores each test project with `--locked-mode`, then builds and tests with `--no-restore`/`--no-build`. Changing a package version without regenerating the lock fails with NU1004; regenerate with a plain `dotnet restore` of both projects. There is no `NuGet.config` under `PluginEditTool`, so restore uses the runner's configured sources (nuget.org on the development machine).

GitVersion: `Mutagen/Directory.Build.props` references `GitVersion.MsBuild`, which fails on a shallow `actions/checkout` ("No base versions determined on the current branch") and shells out to `dotnet` through `cmd`. Both jobs set `DisableGitVersionTask=true`, which the package's targets honour; the tests read no assembly version. The same build was proven on a `--depth 1` clone. Do not remove it without also setting `fetch-depth: 0`.

Coverage: the Windows job runs both test projects. The Linux job runs `FO4RecordEditor.Core.Tests` only, because `FO4RecordEditor.Tests` targets `net9.0-windows` with WPF. Linux was not run locally (no Linux host), so that job is unverified until its first CI run. Tests that need Fallout 4 data (`FO4RecordEditor.Tests/TestDataRoots.cs`, `Mo2ProfileLoaderTests`, `Ba2Version7Tests` and others) look for fixed local paths and return early with a "Skipped --" line when the data is absent, which xUnit reports as passed; four tests report as skipped. Do not set `FO4RE_REQUIRE_FIXTURES` in CI.

Test fixes made with the workflow:

- `FO4RecordEditor.Tests/ShippedDocsTests.cs:StatedToolCountMatchesTheRegisteredTools`: `PluginEditTool/README.md` states no tool count at any commit in this repository (the native README rewrite describes the native host, not the 109 registered C# tools), while `docs/MCP_SETUP.md` does. The README case now only requires that any count it states matches; `docs/MCP_SETUP.md` still must state one. `AllSatisfy` was replaced because FluentAssertions fails it on an empty set.
- `FO4RecordEditor.Tests/GlobalStateIsolationTests.cs:Dispose_RestoresTheRegistryDictionariesCapsAndCaches`: `GlobalStateIsolation.Dispose` restores every dictionary, cap and cache correctly. The test failed for two test-side reasons. It assumed `MutagenLoader.LooseMods` started empty, but `WriteService.CreatePlugin` callers elsewhere in the project leave uniquely named plugins registered, so it now clears the dictionaries after taking the `pristine` snapshot and that snapshot restores whatever was there. Its in-scope assertions also expected `LooseModPaths` and `EditableMods` to hold only `B.esp`, although the test never removes `A.esp` from `LooseModPaths` or `MyPatch.esp` from `EditableMods`; they now state the real contents.
- `FO4RecordEditor.Core.Tests/F4SERegionMergeTests.cs:Generated`: the fixture is a raw string literal, so its line endings follow the checkout. With `core.autocrlf=true` it contained CRLF and two tests failed; the fixture is normalised to LF. `F4SERegionMerge.Merge` still returns `generated` untouched when there is nothing to preserve, which is correct because the emitter produces LF.

Path length: the longest tracked path under `PluginEditTool` is 145 characters, so a runner workspace root longer than about 115 characters can break the build unless Windows long paths are enabled. A GitVersion run on a deeply nested checkout failed with LibGit2Sharp "path too long".

On the Windows runner the default TEMP lives under the runner home folder, which is a reparse point; `ArchiveExtraction` and `ArchiveService` refuse output paths with a reparse-point ancestor, so the `plugin-editor-ci.yml` Windows steps set TEMP and TMP to `runner.temp`; the `native-windows-tests` job in `native-ci.yml` does not set them.

plugin-editor-ci checks out with the full URL `https://code.forgejo.org/actions/checkout@3d3c42e5aac5ba805825da76410c181273ba90b1 # v7` (`.github/workflows/plugin-editor-ci.yml`), while native-ci uses `actions/checkout@3d3c42e5aac5ba805825da76410c181273ba90b1 # v7` (`.github/workflows/native-ci.yml`). On a pull_request event native-ci and plugin-editor-ci start together, and their Windows jobs fetched the shared cached checkout action at the same moment, failing in about 2 s with `shallow.lock` / "shallow file has changed since we read it" before any step ran (runs 691 and 694). A different action URL gets its own cache directory, so the two jobs no longer touch the same clone.

## GT-089 plugin selection resets panel state

`SpreadsheetPanel.tsx:SpreadsheetPanel` and `MastersPanel.tsx:MastersPanel` reset their per-plugin state in a `selectPlugin` handler that the plugin dropdown calls instead of setting the plugin directly. Before this change, switching the dropdown kept the previous plugin's grid, pending edits, master rows and order, light checkbox and status line, so Save, Reorder and SetLight ran against the new plugin with the old state. The spreadsheet clears grid, edits, record types, the selected type and status. The masters panel clears rows, order, light flag, original light flag, clean preview, staged flag and status. The buttons that act on loaded data are only rendered or enabled while a grid or rows exist, so after a switch they are unavailable until Load. The log is kept because it is a history of actions, not per-plugin state. Tests: `src/SpreadsheetPanel.test.tsx` (`drops the loaded grid and pending edits when another plugin is selected`) and `src/MastersPanelPluginSwitch.test.tsx` (`drops the loaded masters, order and light flag when another plugin is selected`, `acts on the newly loaded plugin state after Load`).

Each panel keeps a `loadSeq` ref. `selectPlugin` and every `load` take a new sequence number, and a `load` applies its response (grid, rows, or error) only while its number is still current, so a Load still in flight when the dropdown changes cannot fill the previous plugin's data under the new selection. `busy` is cleared by the switch and by the current load only. This is the same idiom as `CellPanel.tsx:CellPanel` `searchSeq`.

The actions that write to the host follow the same rule. `SpreadsheetPanel.tsx:SpreadsheetPanel` `save` and `MastersPanel.tsx:MastersPanel` `applyOrder`, `toggleLight`, `cleanMasters` and `savePlugin` capture the sequence when they start. After each await, a changed sequence means the plugin was switched: the action skips its reload, its banner and its per-plugin state updates (light revert, original light, staged flag, clean preview). The host call still runs, because the user asked for the write, and the action log still records the outcome. The banner is set by `load`, after the reload, through its `message` argument, because `load` clears the banner at its start. Tests: `src/SpreadsheetPanel.test.tsx` (`skips the reload and banner when the plugin changes while Save is pending`, `ignores a Load that resolves after another plugin was selected`) and `src/MastersPanelPluginSwitch.test.tsx` (`skips the reload and banner when the plugin changes while Save Plugin is pending`, `ignores a Load that resolves after another plugin was selected`).

## GT-095 spreadsheet save outcome

`SpreadsheetPanel.tsx:SpreadsheetPanel` counts an edit as applied when `backend.SetField` resolves, and as failed when it rejects. The earlier check for the word "error" in the returned text marked successful saves as failures whenever the message contained that word. The native `setFieldValue` path (`PluginEditTool/native/src/plugin/RadiclePluginMutation.cpp:setField`) returns a plain success status string, and failures come back as an error result, which the bridge in `src/hostbridge.ts:call` turns into a rejected promise. The result line is now written after the reload that `save` runs, because `load` clears the result text and the failure count was never visible. Tests: `src/SpreadsheetPanel.test.tsx` (`reports a save as applied when the success message contains the word error`, `reports a rejected SetField call as a failed edit and skips the disk save`).

## GT-080 / GT-088 web editors and unsaved work

`PluginEditTool/web/src/NifPanel.tsx:NifPanel` records the path a tree or material was loaded from (`treePath`, `matLoadedPath`) when the load succeeds, and keys `NifEditor` and `MaterialEditor` on that path. A newly loaded file therefore mounts a fresh editor and cannot inherit the previous file's unsaved edits, and Save writes to the path that was loaded rather than to whatever the path input says. The geometry path (`geoPath`) is not used for this because `loadGeo` runs unawaited and keeps the previous value when it fails, so it can name a different file from the one the editor shows. `onEditorSaved` and the material `onSaved` reload the recorded path, not the typed one.

`PluginEditTool/web/src/NifEditor.tsx:NifEditor` and `PluginEditTool/web/src/MaterialEditor.tsx:MaterialEditor` report their dirty state through `onDirtyChange`. The effect clears the flag on unmount, so leaving the edit or materials mode does not leave a stale dirty flag behind. Both footers show the save target basename, with the full path in the tooltip.

`PluginEditTool/web/src/NifPanel.tsx:NifPanel` asks through `confirmDiscard` (a wrapper around `useDialogs().confirm`) before any action that would drop the open editor's edits. The actions are: the × button and the backdrop (`requestClose`), a mode tab or the .obj drop that changes mode (`changeMode`), and a tree or material load of a different path (`loadTree`, `loadMaterial`). A reload of the same path, which is the save reload, never prompts, because the key keeps the edits. Cancel keeps the current file, edits and mode. Confirm loads or switches, and the new file starts clean through the key change and the unmount that clears the dirty flag. `PluginEditTool/web/src/PapyrusPanel.tsx:PapyrusPanel` applies the same rule to `requestClose`, to `loadIntoEditor` (used by Open in editor and by analyze-mode drops), and to `goToDefinition` when it would replace the buffer with another file. `PluginEditTool/web/src/Blueprint/BlueprintPanel.tsx:BlueprintPanel` asks in `open` after a path is chosen and before any graph is read, and in `requestClose` for the × button, when `useSavedDoc` reports the graph as dirty. The Blueprint overlay has no backdrop click handler, so there is no backdrop path to guard. Clean state never prompts.

`PluginEditTool/web/src/SpreadsheetPanel.tsx:SpreadsheetPanel` (GT-089) keeps pending cell edits in `edits` until Save writes them. `requestClose` (the × button and the backdrop), `requestLoad` (the Load button) and `selectPlugin` (the plugin dropdown) ask through `confirmDiscard` when `edits` is not empty. The guard sits on those three entry points, not in `load`, because `save` reloads through `load` after a successful write and must not prompt. Cancel keeps the grid and edits; confirm drops them. Tests: `src/SpreadsheetPanel.test.tsx` (`unsaved cell edits (GT-089)`).

`PluginEditTool/web/src/NifPanel.tsx:NifPanel` `loadTree` keeps the open tree and its edits when `nif.Tree` fails, and shows the error in the geometry info line. The discard prompt still runs before the read, so a failed load of a different path asks first and then keeps the current file. Test: `src/NifPanelEdits.test.tsx` (`keeps the open tree and its edits when a same-path reload fails`).

`PluginEditTool/web/src/PapyrusPanel.tsx:PapyrusPanel` passes the symbol from `resolveAtCaret` straight into `goToDefinition(symbol)`. F12 and Ctrl+click previously read `symbolInfo` from the render that created the key handler, which could be the symbol from an earlier click rather than the one under the caret.

## GT-095 web response ordering and cleanup

`PluginEditTool/web/src/RecordView.tsx:RecordView` ignores a load response when the record changed while it was in flight. The `FormKey` effect bumps `loadSeq` before its four backend calls and skips each `set*` call once the counter has moved, so a slow response for the previous record cannot overwrite the current one. The `menu` effect uses `menuSeq` the same way, so an older right-click lookup cannot replace the actions for the latest one. `PluginEditTool/web/src/CellPanel.tsx:CellPanel` (`runLoad`) applies the same guard to the reference fetch and the geometry batch, a superseded load leaves `busy` to the newer one, and its inner `finally` clears `geoProgress` only for the current load. A superseded load never reaches that clear, so `runLoad` also resets `geoProgress` when it starts; without it, a "Converting meshes" bar from an older load stayed on screen over a newer load that had no meshes. Tests: `src/RecordView.loadOrder.test.tsx` (`keeps the newest record problems when an older load resolves last`, `keeps the actions for the latest right-clicked element when an older lookup resolves last`) and `src/CellPanel.loadOrder.test.tsx` (`keeps the newest grid load when an older load resolves last`, `keeps the newer conversion progress when an older conversion finishes last`, `clears the conversion progress of a superseded load when the newer load has no meshes`).

`PluginEditTool/web/src/App.tsx:App` no longer mounts a conflict-resolver screen at `/`. That route was reachable only by the bare root URL (the Vite dev root rendered it); the native host opens `/#/main` and nothing navigates to `/`, so the component and its CSS and test were deleted, and `/` now redirects to `/main`. `backend.GetConflicts` stays, because `MainShell` uses it for the conflicts tree. Test: `src/App.test.tsx` (`redirects the bare root to the main shell route`).

`PluginEditTool/web/src/SettingsModal.tsx:SettingsModal` needed no code change. `src/SettingsModal.test.tsx` pins the configured and not-set labels for the Anthropic and Gemini keys, and that blank key fields are left out of the `SaveSettings` payload while a typed key is sent.

`PluginEditTool/web/src/Shell/useRecordOpening.ts:useRecordOpening` owns the open-record state that used to live in `MainShell`. `openSeq` is bumped before each open (`openRecordInView`, `openHit`, `openByFormKey`) and a response is dropped once the counter has moved, so the newest open wins whatever order the responses arrive in; `reloadSeq` does the same for `reloadMatrix`. `PluginEditTool/web/src/Shell/useEnvironment.ts:useEnvironment` and `Shell/useConflictBatch.ts:useConflictBatch` hold the environment-load and conflict-batch logic moved out of `MainShell`. `Shell/HelpModal.tsx:HelpModal`, `Shell/BrokenRefsModal.tsx:BrokenRefsModal`, `Shell/EnvLoadingOverlay.tsx:EnvLoadingOverlay`, `Shell/ConflictsPanel.tsx:ConflictsPanel` and `Shell/FavouritesList.tsx:FavouritesList` are presentational components extracted from `MainShell`. Tests: `src/MainShell.openOrder.test.tsx` (`shows the newer conflict record when an earlier open resolves last`, `keeps the newest record reload when an earlier reload resolves last`).

## GT-086 Papyrus staging containment — PluginEditTool/native/src/app/PapyrusStaging.cpp:stageNamespacedScripts

Compiling a folder copies each `.psc` into a temp staging tree under its ScriptName namespace (`MyMod:Quests:Foo` becomes `MyMod/Quests/Foo.psc`), then runs the Creation Kit over that tree. The name check is `papyrus::safeScriptObjectName` (`PluginEditTool/native/include/fo4ide/papyrus/ScriptNames.hpp:safeScriptObjectName`). It rejects empty parts, `.`, `..`, control characters, `/`, `\`, `<`, `>`, `"`, `|`, `?` and `*` (the last six are refused on every platform, so the result is the same on Windows and Linux), and it runs over the whole name, so every `:` part is covered (`PluginEditTool/native/include/fo4ide/papyrus/ScriptNames.hpp:safeScriptObjectName`). A bad name is refused before any file is copied. `stageNamespacedScripts` returns an `UnsafeScriptName` error, and `PluginEditTool/native/src/app/PapyrusTarget.cpp:PapyrusTarget::compile` returns its message as the tool result without starting the compiler. Before this change the parts were joined unchecked, so `..:..:Evil` wrote above the staging folder and a rooted part replaced the path prefix.

The staging path has a second check. The destination's `weakly_canonical` must sit under the `weakly_canonical` of the staging root, compared component by component with `std::mismatch`. It is a backstop: after the name check no destination can leave the staging root. A part can never contain `:` because the ScriptName splits on it, so a drive part such as `C:` cannot reach the path code. The staging root is a parameter so the tests can point it at a temp tree (`PluginEditTool/native/tests/unit/PapyrusTargetTests.cpp`, the `PapyrusStaging` cases). A file whose ScriptName declares no namespace is still staged under its own file name.

`PluginEditTool/native/src/papyrus/Decompiler.cpp:Decompiler::toSource` uses the same helper through `ScriptNames.hpp` and its behaviour is unchanged. The decompiler refuses an unsafe object name by throwing; the staging path refuses by returning the error.

Any staging failure (`weakly_canonical`, `create_directories` or `copy_file` throwing) also returns `Refused to compile` and removes the staging tree. Before this, the catch-all returned `nullopt`, and `PapyrusTarget::compile` then compiled the source folder unstaged. `stageNamespacedScripts` now returns `nullopt` only for an empty file list. `PluginEditTool/native/tests/unit/PapyrusTargetTests.cpp` uses the part `Bad*`, which `safeScriptObjectName` now refuses on every platform. The staging-failure case uses `ScriptName Good:Foo` and a regular file named `Good` inside the staging folder, so `create_directories` fails on every platform.

## PluginEditTool/native/src/app/ServiceTargets.cpp:SettingsTarget::getSettings, PluginEditTool/native/src/app/ServiceTargets.cpp:SettingsTarget::saveSettings, PluginEditTool/native/src/app/ServiceTargets.cpp:SettingsTarget::testClaude, PluginEditTool/native/src/tools/ToolLocator.cpp:resolveCommand, PluginEditTool/native/include/fo4ide/settings/Settings.hpp:SharedSettings — GT-077/GT-078 settings redaction, tool execution and GT-094 shared settings

`redactedSettings` builds the GetSettings JSON from `to_json` in `PluginEditTool/native/src/settings/Settings.cpp`, erases `AnthropicApiKey` and `GeminiApiKey`, and adds `hasAnthropicApiKey` and `hasGeminiApiKey` (true when the key is non-empty). The MCP tool `get_settings` in `PluginEditTool/native/apps/fo4ide/McpTools.cpp:registerNativeMcpTools` forwards to the `settings.GetSettings` RPC, so it reads the same redacted string and has no logic of its own. `SettingsTargetSecurityTests.cpp` checks the target directly and through `rpc::Dispatcher`.

`updateSecret` in `SaveSettings` keeps the stored key when the payload omits it or sends a value that is blank after trimming. A non-blank value replaces the key, trimmed. Clearing a stored key through SaveSettings is therefore no longer possible; the web UI never sent a blank to clear one.

`toolPathError` runs over the whole payload before any field is written, so a refused save changes nothing and writes no file. A non-empty tool path must be an existing regular file. `PapyrusCompilerPath` must be named `PapyrusCompiler.exe`, `XwmaEncodePath` `xWMAEncode.exe`, and `Archive2Path` `Archive2.exe`, compared case-insensitively. These are the names in the `BrowseFile` filters in `PluginEditTool/web/src/SettingsModal.tsx`. `TexconvPath` accepts `Texconvx64.exe`, `Texconv.exe` or `texconv`; the first two are the Windows filter names, and `texconv` is the bare name `ToolLocator` resolves on PATH on Linux. `ClaudeCodePath` accepts an existing file or a bare command name (no directory part) that `tools::resolveCommand` finds on PATH, so the UI default `claude` saves when Claude Code is on PATH. A bare name that is not found is refused and nothing is saved. `NiftoolPath` and `FfmpegPath` need only an existing file, because `ToolLocator` accepts any file in those settings. Folder keys are not checked, and an empty value is always allowed.

On Windows `testClaude` first passes the path through `tools::resolveCommand`. A bare name with no directory part is searched on PATH: a name without an extension is tried with each PATHEXT extension, and a name with an extension is tried as given. The search reuses `searchPath`, which `Locator::onPath` also uses. Any path with a directory part, and any name not found, is returned unchanged. A `.cmd` or `.bat` path goes through `cmd.exe`, and only after it has been checked for `"`, `&`, `|`, `<`, `>`, `^` and `%`. A match is refused and nothing runs. The reason is that reproc escapes `"` as `\"`, which cmd.exe does not read as an escape, so a path such as `x" & calc & "y.cmd` would run `calc`. Every other path runs directly with `--version`. Non-Windows builds run the path directly, as before.

`settings::SharedSettings` owns the one `Settings` value and its mutex. `main.cpp` constructs it and passes it by reference to `SettingsTarget`, `AppInteropTarget`, `tools::Locator` and the two `Refreshing*` wrappers. Every reader takes a `snapshot()` copy under the lock, and every writer runs inside `update(fn)`. `SaveSettings` validates the payload, then applies it and calls `Store::save` inside one `update`, so a failed save restores the copy taken under the lock. `AppInteropTarget::loadProfile` does the same for `mo2InstancePath` and `dataFolder`. `Locator` snapshots on each `resolve`, `fallout4Root` and `papyrusBaseImports` call, so it never holds a reference to a value another thread may be writing. Nothing inside an `update` callback may call `snapshot()` or `update()` again, because the mutex is not recursive. `SettingsTargetSecurityTests.cpp` checks that a `Locator` built on the shared object sees a save made through `SettingsTarget`, and runs a save loop against a resolve loop that asserts the model string is never torn. That race test is a smoke test: it does not fail reliably on the old unshared code.

## GT-094 host hygiene (save rename, dropped-file cap, xWMA in-place)

Anchors: `PluginEditTool/native/src/settings/Settings.cpp:Store::save`, `PluginEditTool/native/src/material/MaterialService.cpp:MaterialService::setFields`, `Radicle/src/io/atomic_write.cpp:write_file_atomic`, `PluginEditTool/native/src/app/ServiceTargetSupport.hpp:decodeBase64`, `PluginEditTool/native/src/app/AudioTarget.cpp:AudioTarget::stageDroppedFile`, `PluginEditTool/native/src/app/MaterialTarget.cpp:MaterialTarget::stageDroppedFile`, `PluginEditTool/native/src/audio/AudioService.cpp:AudioService::convertOneToXwm`, `PluginEditTool/native/src/audio/AudioService.cpp:AudioService::convertFromXwm`.

Both save paths write a temp file beside the destination and rename it over the destination. Settings writes `<file>.tmp`. The material save goes through Radicle's `write_file_atomic`, which stages a `.radicle-tmp-*` file, verifies it, and replaces the destination once. The old code removed the destination when the rename failed and retried, so a destination that could be removed but not replaced was lost, and the retry's error hid the original one. Now the rename is tried once. On failure the temp file is deleted and the error text is returned as `SettingsWriteFailed` or `MaterialWriteFailed`, and the destination is left as it was. Proof: SettingsTests.cpp "Settings save keeps an existing destination when the rename fails" and MaterialServiceTests.cpp "Native material save keeps an existing destination when the rename fails". Both put an empty directory at the destination, which the old code removed and replaced with a success result.

`decodeBase64` in ServiceTargetSupport.hpp counts the non-whitespace characters before the first `=` and refuses when `digits * 3 / 4` is over `core::kMaxDroppedFileBytes` (16 MiB, defined in `PluginEditTool/native/include/fo4ide/core/PayloadLimits.hpp:kMaxDroppedFileBytes`). That is the exact decoded size, and the check runs before any buffer is reserved. Its callers are ServiceTargets.cpp and PapyrusTarget.cpp (through `using namespace support`), AudioTarget.cpp and MaterialTarget.cpp, so none of them keeps a private copy. Proof: AudioServiceTests.cpp "AudioTarget refuses a dropped payload that decodes past the 16 MiB cap" and "AudioTarget stages a dropped payload at the 16 MiB cap" (22369622 characters are accepted and 22369623 are refused), and MaterialTargetTests.cpp "MaterialTarget refuses a dropped payload that decodes past the 16 MiB cap".

The GT-094 split of `MaterialService.cpp` for the 750-line limit is gone. `MaterialFileIo.cpp` and `MaterialFileIo.hpp` were removed in the #466 merge, and nothing used them. The save path is the Radicle-backed `MaterialService.cpp:MaterialService::setFields`, which writes through `fo4::io::write_file_atomic` (`Radicle/src/io/atomic_write.cpp:write_file_atomic`).

`convertOneToXwm` computes the output path first (the default is the source with `.xwm`). If `sameLocation` finds that the output's `weakly_canonical` path equals the source's, it returns `AudioOutputMatchesSource` before creating directories or running a tool. Before this, an `.xwm` source with no output was re-encoded over itself. `convertFromXwm` applies the same check to its final output path, so `convertFromXwm(source, source)` is refused. Proof: AudioServiceTests.cpp "ConvertToXwm refuses to re-encode an xwm source onto itself" and "ConvertFromXwm refuses to write its output over the source and runs nothing", which both check that the fake runner was not called.

## PluginEditTool/native/src/mcp/McpServer.cpp:StdioServer::handleLine — GT-087 MCP output encoding

Strict `nlohmann::json::dump()` throws `type_error.316` on invalid UTF-8, and that exception used to escape `StdioServer::run` and end the stdio server. Tool text from `RpcTool::call`, and every JSON-RPC envelope built by `StdioServer::result` and `StdioServer::error`, now dumps with `error_handler_t::replace`, so invalid bytes become U+FFFD. `StdioServer::handleLine` wraps method routing in a `std::exception` catch that returns JSON-RPC `-32603` with the exception text. That catch also covers non-string `method` or `params.name` values, which make `value()` throw. Proof: `PluginEditTool/native/tests/unit/McpServerTests.cpp` cases "MCP invalid UTF-8 tool output is replaced and the stdio loop keeps answering" and "MCP tool exception becomes a JSON-RPC internal error and the stdio loop keeps answering".

`PluginEditTool/native/apps/fo4ide/McpTools.cpp:rpcToolText` dumps a non-string RPC value with the same `error_handler_t::replace` handler, so invalid UTF-8 in a tool result becomes U+FFFD instead of throwing.

## GT-077 native host transport

Anchors: `PluginEditTool/native/src/http/HttpServer.cpp:HttpServer::start`, `PluginEditTool/native/src/http/HttpServer.cpp:tokenMatches`, `PluginEditTool/native/src/http/HttpServer.cpp:isJsonContentType`, `PluginEditTool/native/src/http/HttpServer.cpp:dumpJson`, `PluginEditTool/native/include/fo4ide/http/HttpServer.hpp:parsePort`, `PluginEditTool/native/apps/fo4ide/main.cpp:newAccessToken`, `PluginEditTool/native/apps/fo4ide/main.cpp:main`, `PluginEditTool/web/src/hostbridge.ts:takeToken`, `PluginEditTool/web/src/hostbridge.ts:streamEvents`.

`HttpServer::start` refuses an empty `Config::token`, so the loopback port cannot be opened unauthenticated. `main` generates the token per launch with `std::random_device` (16 bytes, hex). On MSVC that device is backed by `rand_s`; a MinGW or other build may not be a CSPRNG, so keep this host Windows-only. The host opens `http://127.0.0.1:<port>/#/main?t=<token>`. The token sits in the fragment, so it is not sent to the server in a request line and not written to the log; the log line and the first stdout line are the only places it appears. `hostbridge.ts:takeToken` reads it from the hash, stores it in `sessionStorage`, and calls `history.replaceState` to strip it from the visible URL while keeping the route.

`/events` takes the token only in the `X-FO4IDE-Token` header. `EventSource` cannot set headers, so the bridge now uses `fetch` and parses the `data:` lines itself in `streamEvents`. A query-string token would leak into access logs and browser history, and the `?t=` fallback on `/events` was removed for that reason. `pumpEvents` reconnects after one second on error or stream end, which matches the old `EventSource` behaviour.

`HttpServer::start` installs a pre-routing handler that answers 403 unless the `Host` header is exactly `127.0.0.1:<boundPort>` or `localhost:<boundPort>`; when bound to `::1` it also accepts `[::1]:<boundPort>`. A DNS-rebinding page on another origin can make the browser send a request to 127.0.0.1, but its `Host` is the attacker's name, so it is refused before any route or static file is served. The same handler checks the token for `POST /rpc` and `GET /events` (401) and the `application/json` media type with optional parameters for `/rpc` (415). httplib v0.51.0 runs the pre-routing handler before it reads a request body; the body is read only in `dispatch_request` after routing, so an unauthenticated or wrong-type request is refused without buffering it. Rejections set `Connection: close`, which stops httplib from draining an unread body after the response. Authenticated bodies are bounded by `core::kMaxRequestBytes` (`PluginEditTool/native/include/fo4ide/core/PayloadLimits.hpp:kMaxRequestBytes`, applied with `set_payload_max_length`). It is 23,418,200 bytes: the base64 length of `kMaxDroppedFileBytes` (22,369,624 characters for 16 MiB, `(n + 2) / 3 * 4`) plus a 1 MiB envelope allowance. Before this, the body cap was 16 MiB, below the base64 size of a 16 MiB dropped file, so the real drop limit was about 12 MiB. The body cap and the drop cap read the same header, so they cannot drift apart. Proof: HttpServerTests.cpp "HTTP RPC accepts a body carrying the largest dropped file" (a body with a 16 MiB payload gets 200), and "HTTP RPC refuses request bodies over the body cap" and "HTTP refuses an unauthenticated oversized RPC body before reading it" (both at `kMaxRequestBytes` and `+1`).

Every dump, including SSE event lines and error bodies, uses `dump(-1, ' ', false, error_handler_t::replace)` through `dumpJson`. The strict default throws on invalid UTF-8 inside a request handler, and the exception would end the response. The one-shot `printHealth` line in `PluginEditTool/native/apps/fo4ide/main.cpp:main` uses `dump(2, ' ', false, error_handler_t::replace)` for the same reason.

`--port` goes through `parsePort`, which accepts whole numbers 0 to 65535 with `std::from_chars`. 0 keeps its existing meaning of any free port. Anything else prints a usage line and returns exit code 6, the same code `--mo2` uses for argument errors. The old `std::stoi` threw an uncaught exception on bad input and silently ignored a trailing `--port`.

## PluginEditTool/native/src/plugin/RadiclePluginDeepCopy.cpp:RadiclePluginBackend::deepCopyAsOverride — GT-085 deep copy cap

The copy set is built by walking the FormLink graph and stops at `cap` records (argument 5, default 200). The dry run already reported a truncated set through `deepCapNotice`. Apply used to ignore that flag and reported "Deep-copied N of N" for a partial copy. Apply now returns `DeepCopyCapped` with the same notice text and stages nothing, so the user must raise the cap and re-run to get the full closure. A capped dry run ends with "Raise the cap, then re-run with apply=true." (`RadiclePluginDeepCopy.cpp:RadiclePluginBackend::deepCopyAsOverride`) instead of "Re-run with apply=true.". The test is `RadiclePluginDeepCopyTests.cpp:Native DeepCopyAsOverride refuses to apply a capped copy and stages nothing`, which uses cap 1 on a two-record closure.

## GT-083 all-or-nothing list replacement

`PluginEditTool/native/src/plugin/RadiclePluginConditions.cpp:RadiclePluginBackend::setConditions`, `PluginEditTool/native/src/plugin/RadiclePluginMutation.cpp:RadiclePluginBackend::setConditionsAtPath`, and `PluginEditTool/native/src/plugin/RadiclePluginComponents.cpp:RadiclePluginBackend::setComponents` replace a whole list (record conditions, one effect scope's conditions, or a MISC/COBJ component list). Each entry is built first and the new list is only staged afterwards. Previously an entry that failed to build was just collected into a `Skipped` clause while the rest of the list was still written, so one bad AI-typed entry silently wiped the record's existing list. Now any failed entry refuses the whole call. Condition lists return `InvalidConditionsJson` with `#i: reason` for the first failing entry (`buildConditions` in `PluginEditTool/native/src/plugin/ConditionCodec.cpp` stops at the first failure, and `#i` is the one-based entry index). Component lists return `ComponentEntriesRefused` with `Refused: K of N component entries failed: #i: reason; ...`, collecting every failing entry. Nothing is staged on refusal: the plugin is not replaced, not marked dirty, and `GetActivePlugins` keeps `Pending` false. A success message therefore never carries a `Skipped` clause. An explicit `[]` is not a failed entry and still clears the list. An empty string is refused instead of being read as `[]`, with `InvalidConditionsJson` (`setConditions`, `setConditionsAtPath`) or `InvalidComponentsJson` (`setComponents`) and the message `conditions JSON is empty; pass [] to clear`, so an omitted argument cannot wipe a list. Callers that clear must send `[]`: the native `PluginBackendTarget.cpp` forwards its argument unchanged, and the web editor always sends `JSON.stringify(rows)`. `setComponents` collects its failures inline; the two condition paths share `buildConditions`.

## PluginEditTool/native/src/plugin/ConditionCodec.cpp:buildCondition — GT-084 condition entry validation

A condition entry the codec cannot store exactly fails with `failure` naming the field. `RadiclePluginBackend::setConditions` (`PluginEditTool/native/src/plugin/RadiclePluginConditions.cpp`) refuses the whole list when any entry fails (GT-083), so one bad entry blocks the save. Failing cases: an operator that is not a known spelling, including a non-string or empty operator; `compareGlobal` or `reference` that is present but neither resolves nor is the placeholder form below; `value` present but not a number; a `runOn` that is neither a name nor a decimal raw value; an unknown flag name, or a `flags` value that is not a string; `param1` or `param2` that is a non-integer number in an integer slot, an integer outside [-2147483648, 4294967295], or a string that is neither an integer, a resolvable form ID, nor the placeholder form. A JSON `null` is refused for every condition field, like any other wrong JSON type (`text()`, the `param1`/`param2` branch of `applyParam`, and the `value` check). Only an omitted key keeps the old defaults (`==`, value 0.0, run-on Subject). An empty string in `param1` or `param2` is absent too (`applyParam` in `buildCondition`), while an empty `reference` or `compareGlobal` still fails. A `value` is read as a double and refused with `comparison value must be a finite number` when it is not a JSON number, is not finite, or exceeds `std::numeric_limits<float>::max()`, before it is narrowed to float. The quest parser `RadiclePluginQuestCondition.cpp:parseQuestCondition` applies the same check and message.

Integers in [INT32_MIN, UINT32_MAX] are stored as their 32-bit pattern, so values above INT32_MAX keep their uint32 bits in the int32 `CtdaCondition::param1` / `param2`. String integers take the same range check.

Placeholder form: `formIdString`, which `describeCondition` uses, falls back to raw text when a raw's master index is not in the plugin's master table (or is the created index 0xFF). Resolving that raw throws, and the fallback writes the full raw as 8 uppercase hex digits, a colon and an empty owner, for example `0A000801:`. `buildCondition` accepts exactly that shape (`placeholderFormId`, reached through `resolveConditionFormId`) and restores the original raw bit for bit, so describe then build round-trips unresolvable reference, compareGlobal and form ID params unchanged. Raw zero emits `00000000:` and maps back to zero. The earlier fallback wrote 6 digits (the low 24 bits, for example `000801:`), which drops the master index and cannot be restored, so it was replaced. This departs from the Mutagen `FormKey.Factory(reference: true)` text `000001:` that the old fallback comment cited; exact round trip takes priority over that text. The 6-digit form is now rejected.

Operator and runOn spellings: `=` and `EqualTo` are named explicitly in the operator table (they were already `==` by default). A decimal run-on value such as `"8"` is accepted as the raw value, since `describeCondition` emits that form for run-on values without a name, so a GetConditions to SetConditions round trip keeps it.

Known gaps: the flag name `UseGlobal` is rejected as unknown. The C# oracle accepts it, but the codec never did, and honouring it would reinterpret the comparison value. A plugin with an empty key describes index-0 raws as `000800:`, which `FormKey::parse` cannot read back; loaded plugins always have a key. Tests are in `PluginEditTool/native/tests/unit/ConditionCodecValidationTests.cpp`, registered in the `fo4ide_test_plugin_backend` target.

## GT-094 plugin backend hygiene

`PluginEditTool/native/src/plugin/RadiclePluginDisabledOverride.cpp:RadiclePluginBackend::copyAsDisabledOverride` (GT-094.a): the copy and the Initially Disabled flag run under one Write `OpLock`. `PluginEditTool/native/src/plugin/RadiclePluginMutation.cpp:RadiclePluginBackend::copyAsOverrideLocked` is the unlocked copy; it applies the flag to the copy before the single `stagePluginMutationLocked`, so a save cannot persist the copy without the flag. `copyAsOverride` is the locking wrapper for the other callers. Before the split the copy and the flag took two locks, and a save between them could persist an unflagged copy.

`PluginEditTool/native/src/plugin/RadiclePluginBatchMutation.cpp:RadiclePluginBackend::copyAsOverrideMany` (GT-094.b): `source` and `formKey` are read through `stringFieldIfPresent`. A present field of another JSON type becomes one entry in `failures` with reason "source and formKey must be strings", and the other items still apply. `value()` used to throw `type_error` and abort the whole batch.

`PluginEditTool/native/src/plugin/RadiclePluginBackend.cpp:RadiclePluginBackend::loadEnvironment` (GT-094.b): `malformedEnvironmentField` checks each object entry's `name`, `path` and `source` (strings) and `resolved` (boolean) before any file is read. A wrong type returns `InvalidEnvironment` and leaves the loaded set unchanged.

`PluginEditTool/native/src/plugin/RadiclePluginBackend.cpp:requirePluginUnchangedSince` (GT-094.c): the stamp is taken before `fo4::read_plugin_file` and checked after it. A different stamp refuses the plugin with "`<path>` changed while it was being loaded; try again". `RadiclePluginBackend::loadEnvironment` records it as a per-plugin entry in `errors`, and `RadiclePluginBackend::openPlugin` returns it as the error. `RadiclePluginPending.cpp:RadiclePluginBackend::persistPluginLocked` and `RadiclePluginCreate.cpp:RadiclePluginBackend::createPlugin` also record a stamp, but only for a file the backend has just written, so no outside read window exists and they are unchanged. The `lastModifiedIso` helpers in `RadiclePluginAnalysis.cpp` and `RadiclePluginReadApi.cpp` only display a time.

`PluginEditTool/native/src/plugin/RadiclePluginBackend.cpp:RadiclePluginBackend::savePlugin` (GT-094.d): the optional third argument `overwrite` is a boolean and defaults to false. A Save As whose canonical target differs from the loaded plugin's canonical path and already exists is refused with `SaveTargetExists` unless `overwrite` is true. Saving onto the loaded path is unchanged. The `save_plugin` tool in `PluginEditTool/native/apps/fo4ide/McpTools.cpp` has no path field and maps to an in-place save, and the web callers pass an empty path, so none of them reach Save As.

## .github/workflows/native-ci.yml — Test steps (real-data tests)

The real-data tests read `FO4_DATA_DIR`. CI sets it from two Forgejo repository variables:

- `FO4_DATA_DIR_LINUX` feeds the `Test` step of the Linux job `Native (Linux, Debug)`. It is unset today, so that step skips the real-data tests.
- `FO4_DATA_DIR_WINDOWS` feeds the `Test` step of the Windows job `Native tests (Windows, MSVC)`.
- `.github/workflows/native-ci.yml:native-windows-tests` `Test (Debug)` clears `FO4_DATA_DIR` (`set FO4_DATA_DIR=`), so the Debug pass runs without game data. It runs only on non-pull_request events.
- Why: Debug with data does not finish within the job's 90-minute limit. In run 775, `QuestAuthoringRealData.RewritingUnchangedStagesAndObjectivesIsByteExact` took more than 34 CPU-minutes and 6.9 GB in Debug (289-421 s in Release). The `QuestAliasRealData`, `QuestStagesRealData` and `QuestObjectivesRealData` `DecodesEveryShippedQuest` tests each took about 60 CPU-minutes in Debug (131-320 s in Release). The run was still at test 358 of 1824 when the timeout neared.
- Release keeps the real-data coverage: the Windows `Test` step runs the real-data tests with the data, except the expensive groups that a pull request does not touch (see below).

On a pull request, the `Select real-data tests` step in `.github/workflows/native-ci.yml:native-windows-tests` decides which expensive real-data groups the Release `Test` step runs. It matches the changed paths (the diff of the base branch tip against `HEAD`, case-insensitive regexes) and writes one `CTEST_EXCLUDE_REGEX` to `GITHUB_ENV` for the groups it skips. A group runs when any changed path matches its trigger. Cheap real-data suites are never skipped, and every real-data suite not in the table below runs on every event.

| Group | Test suites excluded when skipped | Trigger (changed path matches) |
| --- | --- | --- |
| Quest | `QuestAliasRealData`, `QuestStagesRealData`, `QuestObjectivesRealData`, `QuestAuthoringRealData`, `QuestRuntimeReal` | `quest`, `vmad`, `papyrus`, `pex`, `alias`, `condition`, `ctda` |
| Writeback | `WritebackRealData` | `writeback`, `placed`, `reference`, `refr`, `ref_`, `master`, `environment`, `remap` |
| SubrecordAuthoring | `SubrecordAuthoringRealData` | `subrecord`, `authoring` |
| StructPayload | `StructPayloadFormLinksRealData` | `struct`, `payload`, `proj_expl`, `hazd` |
| TypedFamilies | `TypedFamiliesRealData` | `typed`, `famil`, `armor`, `armo`, `arma`, `race`, `eyes`, `leveled`, `lvsp` |

A core path forces every group to run, and nothing is excluded. The core list is one regex array in the step, and these are its entries: `Radicle/src/binary/` and `Radicle/include/fo4/binary/` (the byte cursor every reader uses); `Radicle/{src,include/fo4}/plugin/` reader, writer, visit, container, tes4, field_writer, mutation, record_edit, flags and onam (the read and write path; onam is the TES4 ONAM rebuild that plugin writes call); `form_?id`, `plugin_key`, `signature`, `error`, `headers`, `fo4` and `flags` (form ID, signature and the umbrella headers); `text/`, `localization/` and `io/` (string encoding, string tables and atomic writes); `environment/strings` (the string-table loader); the shared schema files (`scalar`, `field_address`, `localized_fields`, `form_link*`, `formlink_lists`, `nested_formlink*`, `non_formid_slots`, `preflight`, `record_constraints` and `generated_record_constraints`); `ctda` and `vmad` schema files; every `Radicle/include/fo4/schema/*.inc` table (the generated and hand-maintained data); `Radicle/{src,include/fo4}/forms/proj_expl_hazd` (the constants the mixed form-link decoder uses); `Radicle/schema/` (the `fields.json` input); `Radicle/tools/*codegen*` (the generators for those headers); every `CMakeLists.txt` and `*.cmake` (the build and test list, and the generated-file checks; `Radicle/tests` has no CMakeLists of its own); `.github/workflows/native-ci.yml` and `.github/scripts/` (this CI wiring and the zlib script); and `Radicle/tests/plugin_bytes` (the shared plugin-building test helper).

Why: in run 806, a pull request about CELL water with no quest changes spent 925 s in `QuestObjectivesRealData.DecodesEveryShippedQuest`, and the quest groups together take 15-30 minutes. The owner's measurements are about 148 s for Writeback, 200-240 s for SubrecordAuthoring and 108 s for StructPayload. TypedFamilies has no recorded timing, but the owner reports `DecodesEveryShippedArmorAddon` as very slow.

Pushes to `main`, `workflow_dispatch` and any other non-pull-request event run every real-data test, so a break caused by shared code is still caught right after merge. If the changed files cannot be determined (the fetch or the diff fails, or the diff is empty), the step warns and runs everything. The changed files come from `git diff --name-only --no-renames` (`.github/workflows/native-ci.yml:native-windows-tests`), so a rename lists both the old and the new path. The diff is taken against a depth-1 fetch of the base branch tip, so a shallow checkout can include files that changed on the base branch. That only makes the step run groups more often, never less. The step log prints a RUN or SKIP line per group with the paths that matched, or `core change:` with the path that forced everything.

Each job's `Check FO4 data folder` step runs before its first Test step:

- Unset or empty: prints one line saying the real-data tests will skip. The step passes.
- Set: requires the seven official masters (`Fallout4.esm`, `DLCRobot.esm`, `DLCworkshop01.esm`, `DLCworkshop02.esm`, `DLCworkshop03.esm`, `DLCCoast.esm`, `DLCNukaWorld.esm`) and at least one `Fallout4 - *.ba2` in the folder root. Missing names are printed and the step exits 1.
- Valid: prints the path, then the counts of root `*.esm` and `*.ba2` files.

`FO4_DATA_DIR_WINDOWS` points at the Steam install's `Fallout 4\Data` folder on the Windows runner. `Radicle/tests/test_hedr_count.cpp:official_hedr_samples` parses every `.esl` in that folder root, not only the masters, so Creation Club or mod light plugins placed there are exercised by CI. A folder holding only official files is preferred.

The tests skip when `FO4_DATA_DIR` is unset, or when an expected file is missing or a list is empty. Examples:

- `Radicle/tests/test_real_plugin.cpp:UnmodifiedFallout4MasterIsByteIdentical` skips on a null `getenv` (line 105) and when `Fallout4.esm` is absent (line 110).
- `Radicle/tests/test_ba2_real.cpp:real_data` returns nothing when the variable is unset (line 22) or no archives load (line 26). The BA2 tests skip at lines 46 and 73.
- `Radicle/tests/test_env_bench_real.cpp:EnvBenchRealData.LoadsOfficialMasters` skips when unset (line 12) and when no masters are found (line 15).

An empty value is not a null `getenv` result, so each Test step unsets `FO4_DATA_DIR` when the variable is empty.

The opt-in suites stay off in CI, because no CI variable sets them:

- `FO4_BA2_EXTRACT_ALL`: `Radicle/tests/test_ba2_real.cpp:ExtractsEveryVanillaEntry`.
- `FO4_PEX_ARCHIVES`: `Radicle/tests/test_pex_corpus.cpp:EveryArchivedPexRoundTripsByteIdentically`.
- `FO4_PEX_CORPUS`: `Radicle/tests/test_pex_corpus.cpp:EveryLoosePexRoundTripsByteIdentically`.
- `FO4_PLUGIN_CORPUS`: `Radicle/tests/test_plugin_corpus.cpp:EveryDecoderSurvivesThirdPartyPlugins`.
- `FO4_SEQ_CORPUS`: `Radicle/tests/test_seq_corpus.cpp:EveryLooseSeqRoundTripsByteIdentically`.
- `FO4_PAPYRUS_COMPILER` and `FO4_PAPYRUS_ASSEMBLER`: `Radicle/tests/test_pex_oracle.cpp:PexOracle.CompiledFixturesRoundTripByteIdentically` and `PexOracle.AssemblerDisassemblyAgreesWithTheCompiler`.
- Optional `FO4_PAPYRUS_IMPORTS` and `FO4_PAPYRUS_FLAGS`: passed to the compiler by `Radicle/tests/test_pex_oracle.cpp:optionalArgument`.

## PluginEditTool/native/src/plugin/ConditionCodec.cpp:flagValue — numeric condition flags (GT-157)

There is no named-flag mask constant. `flagValue` accepts a numeric flag only up to 0x1F, and the caller refuses any flag with the Use Global bit (0x04) unless it comes from `compareGlobal`. The effective accepted set is therefore the named flags OR (0x01), ParametersUseAliases (0x02), UsePackData (0x08) and SwapSubjectAndTarget (0x10), mask 0x1B. The CTDA type byte has bit 0 Or, bit 1 Use Aliases, bit 2 Use Global, bit 3 Use Packdata and bit 4 Swap Subject and Target, and bits 5-7 hold the compare operator (`source:xEdit Core/wbDefinitionsCommon.pas:3429-3433 @93cc0bc5`, `source:xEdit Core/wbDefinitionsCommon.pas:2958 @93cc0bc5` for the operator values, `source:xEdit Core/wbDefinitionsFO4.pas:5186 @93cc0bc5` for the CTDA struct). Before this change a numeric value from 0 to 255 was OR-ed into `flags_byte` as-is, so "4" set Use Global without a `compareGlobal` and "32" changed the operator in `buildCondition`. A refused value, like an unknown name, fails with `unknown flags or UseGlobal outside compareGlobal`, and nothing is staged. Covered by `PluginEditTool/native/tests/unit/ConditionCodecValidationTests.cpp` (cases "numeric flag carrying the Use Global bit fails the entry", "numeric flag carrying an operator bit fails the entry", and "numeric flag equal to a named flag is accepted without touching the operator").

## PluginEditTool/native/src/material/MaterialService.cpp:MaterialService

`MaterialService` is a presentation layer over Radicle's material codec. It holds no BGSM/BGEM byte layout. `load` returns `fo4::material::MaterialFile` from `Radicle/src/material/material_file.cpp:read_material_file`. `inspect` turns the ordered Radicle field list into the JSON rows that the web UI and the MCP tools read. `setFields` parses the string values, applies them with `set_field` and `set_text`, encodes with `write_material_file`, and writes with `fo4::io::write_file_atomic` (`Radicle/src/io/atomic_write.cpp:write_file_atomic`). That writer creates the parent folder, verifies the staged size and replaces the destination.

Field names. The JSON and MCP names did not change. The aliases below map Radicle names to the names PET has always shown (`aliases`, `shownName`, `findShown`):

- `ScreenSpaceReflections` -> `Ssr`
- `WetnessControlScreenSpaceReflections` -> `WetSsr`
- `EnvironmentMapping` (header, version below 10) -> `EnvMapping`
- `EnvironmentMappingMaskScale` (header, below 10) -> `EnvMappingMaskScale`
- `EffectEnvironmentMapping` (BGEM body, 10 and up) -> `EnvironmentMapping`
- `EffectEnvironmentMappingMaskScale` (BGEM body, 10 and up) -> `EnvironmentMappingMaskScale`
- `TileFlags` (raw u32) -> `TileU` (value 2) and `TileV` (value 1), handled in `fieldRows` and `applyField`. `TileFlags` is neither shown nor settable.

All other names match in both codecs: the 78 BGSM body names, the 36 BGEM body names other than the two Effect names, and the header names other than those listed above. The mapping is one-to-one within any single file, so `findShown` finds at most one field.

Section split: header rows are the fields before the first texture string (`FieldType::string`). Both formats start the body with texture strings, and the header holds no strings. JSON rows list body fields first and header fields second, as before.

Behaviour changes from the old native codec:

- Round trip is byte-identical. The old writer normalised bools to 0 or 1, appended a NUL to every string, and wrote TileFlags as only bits 0 and 1. Now a raw bool byte of 2 is kept, strings are written as read, and unknown TileFlags bits are kept. Setting TileU or TileV changes only its own bit. JSON still shows a bool of 2 as `true`.
- String display strips one trailing NUL (`fo4::material::text`). The old code stripped all of them, so only strings with several trailing NULs differ. `TextureResolver.cpp:TextureResolver::resolveMaterialDiffuse` uses the same call.
- A name the file's layout does not have now returns `MaterialUnknownField`. The old code accepted header names such as `MaskWrites` below version 6, `DepthBias` below 10 and `EnvMapping` from 10, then silently wrote nothing. Conditional body fields such as `EmittanceColor` while `EmitEnabled` is false were already unknown and still are.
- `fo4::parse_error` maps to `MaterialParseFailed` with Radicle's message (`plugin parse error at 0x...`). Bad magic and short files now use that wording, not the old "Not a BGSM or BGEM material" and "fewer than 4 bytes" texts. The error type is unchanged.
- `Version` and `Signature`: a request equal to the current value is a no-op. Any other value is `MaterialFieldParseFailed`, as before.
- The "Set N field(s)" message lists the names as requested, not the canonical spelling of a case-insensitive name.
- `Document` is removed. `load` returns `fo4::material::MaterialFile` directly.
- A string field value with an embedded NUL is refused with `MaterialFieldParseFailed` before anything is written (`MaterialService.cpp:assign`, string case). Test: `PluginEditTool/native/tests/unit/MaterialServiceTests.cpp` "Native material service refuses an embedded NUL in a string field without writing". Without the check, `set_text` stores `a`, NUL, `b`, NUL and the file would hold a path that a NUL-terminated consumer reads as `a`.

## Radicle/src/material/material_file.cpp:set_text

`set_text` stores the value plus exactly one NUL, and `text` (`Radicle/src/material/material_file.cpp:text`) strips exactly one. The codec therefore reads back any value it wrote, including one with an embedded NUL, and a value with trailing NULs keeps all but the last one (`a.dds` plus two NULs reads back as `a.dds` plus one NUL). This is deliberate. The old PET code stripped every trailing NUL, so that malformed case now differs. The codec does not refuse embedded NULs; PET's `MaterialService.cpp:assign` does, because the texture path consumers treat the string as NUL-terminated and would truncate it. Keep the two rules separate when changing either side.

## Radicle/src/plugin/reader.cpp:find_record — first-match walk and RecordIndex

`find_record` (both overloads) returns the first record in `collect_records` order whose key matches and whose signature is not TES4. That order is the TES4 record first, then each top-level group depth-first in child order, so a duplicate key resolves to the earliest record. `find_first_record` walks the tree directly and stops at the first match. It does not build the pointer vector that `collect_records` builds on every call.

`FormKey` equality folds case on the owner name (`PluginKey` uses `text::equals_folded`), so comparing owners is the expensive part of a scan. The FormKey overload compares the integer local ID first. This is the same result, because `FormKey` equality is the conjunction of owner and local. `Radicle/tests/test_reader.cpp:FindRecordReturnsTheFirstMatchInTraversalOrder` covers duplicate keys across groups, a master-owned and a plugin-owned record with the same local ID, folded owner names, TES4-signature records, and not-found keys.

A single lookup is still a scan over every record, so a loop over all quests is O(quests × records). `RecordIndex` (`Radicle/include/fo4/plugin/reader.hpp:RecordIndex`) answers the same lookups from a map built once from a `const Plugin&`. It keeps the first record per FormKey in the same traversal order and skips TES4 records. The index holds pointers into the plugin, so a caller rebuilds it after any change to that plugin. `Plugin` has no cache and nothing invalidates an index automatically, which is why the index is an explicit value owned by the caller and not stored on `Plugin`. Mutation paths write `groups`, `children`, `form_key` and `header.form_id` directly, for example `Radicle/src/plugin/mutation.cpp:renumber_local_record` and `Radicle/src/plugin/mutation.cpp:delete_local_record`, so no single choke point covers them.

Decoders with index overloads: `Radicle/src/forms/quest_objectives.cpp:decode_quest_objectives`, `Radicle/src/forms/quest_stages.cpp:decode_quest_stages`, `Radicle/src/quest/runtime.cpp:quest_runtime_data` and `Radicle/src/forms/quest_aliases_decode.cpp:decode_quest_aliases`. Their `Plugin` overloads still call `find_record`. `decode_quest_aliases` keeps its body in the anonymous-namespace helper `decode_quest_aliases_record`, and both overloads call it. The COED owner lookup in `Radicle/src/forms/quest_aliases_extras.cpp:decode_alias_extras` uses the index when one is passed and `find_record` otherwise.

Duplicate form keys inside one plugin: the first record wins. xEdit source at `93cc0bc5` (read 2026-10-11): a main record whose form ID equals the previous record's raises `EwbSkipLoad` (`Core/wbImplementation.pas:10253-10260`), and the loader logs it as a skipped load and drops that later record (`Core/wbImplementation.pas:8968-8971`). Radicle keeps the first record as well. `Radicle/src/plugin/reader.cpp:find_record` and `Radicle/include/fo4/plugin/reader.hpp:RecordIndex` return the first match in traversal order. Per-plugin maps use `try_emplace`, so the first record wins: `Radicle/src/graph/plugin_diff.cpp:index_records`, `Radicle/src/plugin/placement.cpp:record_placements`, `Radicle/src/graph/check_errors.cpp:check_load_order` (record signatures) and `Radicle/src/graph/reachability.cpp:build_graph` (versions). Across-plugin winner maps still let a later plugin override an earlier one, but each plugin contributes only its first record per key. They iterate `Radicle/src/plugin/reader.cpp:collect_first_records`, which drops later in-plugin duplicates. Its callers are `Radicle/src/graph/check_errors.cpp:winning_records`, `Radicle/src/graph/check_errors.cpp:find_leveled_list_cycles`, `Radicle/src/graph/reachability.cpp:build_graph` (winners), `Radicle/src/graph/reachability.cpp:find_unreachable` and `Radicle/src/graph/conflict_graph.cpp:ConflictGraph::build`. The plugin tree keeps the later record. The record-level sites above ignore it and do not report it. Known gaps, not changed: `Radicle/src/schema/form_links.cpp:collect_form_links_detailed` still walks every record, so links from a later duplicate still reach `Radicle/src/graph/check_errors.cpp:check_links` and the outgoing edges in `Radicle/src/graph/reachability.cpp:build_graph`. `Radicle/src/graph/reachability.cpp:index_group` still sets `graph.parent` from the last duplicate in a plugin. `Radicle/src/c_api.cpp:radicle_plugin_collect_form_keys` still lists every record, including later duplicates.

The alias decoder is split by job across these files in `Radicle/src/forms/`:

- `quest_aliases_internal.hpp`: private header in `fo4::forms::detail` with the signatures, read and write helpers, form key decoding, and the declarations shared between the files.
- `quest_aliases_decode.cpp`: the QUST walk, reference and location fills, semantic validation, and both `decode_quest_aliases` overloads.
- `quest_aliases_extras.cpp`: reference extras in both directions, meaning keywords, CNTO and COED inventory, linked aliases, overrides, voice and death item.
- `quest_aliases_encode.cpp`: `encode_quest_alias`, base alias and fill encoding, conditions, and the post-fill fields.

## Radicle/src/forms/quest_aliases_internal.hpp:t_unresolved — dangling link collector

`t_unresolved` collects the signatures of links that could not be resolved, so a dangling reference degrades the one field instead of the whole quest. It is non-null only while `decode_alias_semantics` runs for one alias. Outside that scope an unresolved link throws instead of being collected.

## Radicle/src/forms/quest_aliases_decode.cpp:decode_alias_semantics — unresolved link scope

The unresolved-link collector is scoped to one alias, so a dangling form ID degrades one field rather than aborting the quest. The alias's `unresolved_links` list is cleared first, the collector pointer is set to that list, and a guard resets the pointer to null when the function returns.

## Radicle/src/forms/quest_aliases_decode.cpp:decode_quest_aliases_record — partial overrides without ANAM

A partial override that does not touch aliases carries neither ANAM nor any alias markers. The original comment records seven shipped DLC overrides of Fallout4.esm quests as exactly EDID(+VMAD). Those have no alias array to decode, so the decoder reports an empty one rather than rejecting the record. Alias markers without ANAM stay fail-closed, because next_alias_id would be unknown and the start of the array ambiguous.

## Radicle/src/forms/quest_aliases_decode.cpp:decode_quest_aliases_record — trailing alias markers

xEdit places optional NNAM/GNAM/SNAM after the single contiguous Alias array. Once a non-alias tail member is reached, a later alias marker is a malformed second array and must not be silently accepted, so the decoder throws on it.

## GT-156 HTTP RPC large-body test budget — PluginEditTool/native/tests/integration/HttpServerTests.cpp:HTTP RPC accepts a body carrying the largest dropped file, PluginEditTool/native/tests/integration/CMakeLists.txt:catch_discover_tests, PluginEditTool/native/src/http/HttpServer.cpp:HttpServer::start

The CI failure `***Timeout 10.19 sec` on the largest-dropped-file test was not a stall. The body is read linearly. httplib v0.51.0 reads a Content-Length body in `CPPHTTPLIB_RECV_BUFSIZ` (16384 bytes) chunks and appends each chunk (`read_content_with_length`, httplib.h:7312-7345; `Server::read_content`, httplib.h:11776). The pre-routing handler runs before the body is read, and the `/rpc` route runs only after the whole body has arrived, so the client's full write and the server's read cannot wait on each other. Per request the work is linear in the body size: the `/rpc` route parses about 22 MB of JSON, `Dispatcher::invokeJson` (src/rpc/RpcDispatcher.cpp) and the echo target each copy the `args` value, and the response is dumped with UTF-8 validation through `dumpJson` and sent back. The test also base64-encodes 16 MiB before it sends. The Linux Debug job (`.github/workflows/native-ci.yml`, job `Native (Linux, Debug)`) builds at -O0 without sanitizers and runs `ctest --parallel 4`, so these steps share the runner's CPU.

The test is tagged `[large]`. `tests/integration/CMakeLists.txt:catch_discover_tests` gives the untagged HTTP cases the 10 s bound and gives only `[large]` a 120 s bound, the same bound the f4se and MCP smoke tests use in `PluginEditTool/native/tests/CMakeLists.txt`. The 10.19 s figure is the only timing available, and the 120 s bound is an estimate with headroom, not a measured runtime. Proof: the `[large]` test is listed under its own CTest entry with TIMEOUT 120 (check `ctest -N -V` in CI), and the other HTTP tests still have TIMEOUT 10.

## GT-218 HTTP refusal tests send no body — PluginEditTool/native/tests/integration/HttpServerTests.cpp:HTTP RPC refuses a missing or wrong access token, PluginEditTool/native/tests/integration/HttpServerTests.cpp:HTTP RPC requires an application/json content type, PluginEditTool/native/tests/integration/HttpServerTests.cpp:HTTP refuses Host headers that are not this loopback server

`REQUIRE(missing)` failed once on Windows MSVC (#440's run) and passed on the same code elsewhere. This was not a fixture startup race: `bind_to_any_port` has already called `listen` before `HttpServer::start` returns, so early connections queue. The cause is the GT-077 design described above. The pre-routing handler in `HttpServer.cpp:HttpServer::start` answers 401, 403 or 415 before httplib reads the request body (`Server::routing` runs `pre_routing_handler_` before `read_content`). The httplib client writes the headers and the body in two separate writes (`ClientImpl::write_request`). When the body arrives after the server has read the headers, the server closes the socket (`Server::process_and_close_socket`, `shutdown` then `close`) with unread bytes, and the OS answers with a TCP reset that can discard the refusal before the client reads it. The client then gets no response.

The refusal tests depend only on headers (Host, token, Content-Type), so they now POST an empty body and the outcome is deterministic. The `application/json; charset=utf-8` acceptance case keeps its body because the body is read on that path. A refused request that carries a body can still end in a reset; `HTTP refuses an unauthenticated oversized RPC body before reading it` already accepts either a reset or a non-success status for that case. The product behavior is unchanged: refusing before reading is the GT-077 intent, and the PET web UI always sends the token.

## ci/runners/clear-stale-git-locks.ps1:try block — stale action-cache lock cleanup

Removes git lock files under a `.git` directory (path match `*\.git\*`; other `*.lock` files such as `yarn.lock` are left alone) from the shared action cache when a fetch was killed, for example when the runner restarted mid-download. A leftover `shallow.lock` makes every job that uses that action fail during "workflow prepared" with "Unable to create ...shallow.lock: File exists". Called from each Windows `start-runner.ps1` and every 5 minutes by the `ForgejoRunner-clear-stale-git-locks` scheduled task. Searches the cache roots `C:\ci\act` and `C:\ci\home\.cache\act` (runner host paths, examples); repos live at `<root>\<2 hex>\<62 hex>` of a sha256.

## ci/runners/clear-stale-git-locks.ps1:$cutoff — five-minute age gate

Only locks older than five minutes are removed. An action fetch takes seconds, so a fetch another runner is doing right now keeps its lock.

## ci/runners/alias-profile-path.ps1:try block — neutral profile path in PATH

Rewrites PATH entries under the real Windows profile to the neutral runner host junction `C:\ci\home` (example path), so tool paths printed in CI logs never show the profile folder name. Called from each runner's `start-runner.ps1`. `$real` comes from `GetFolderPath` rather than `$env:USERPROFILE` because `start-runner.ps1` has already pointed `$env:USERPROFILE` at the junction; the script keeps that as its one-line constraint comment.

## Radicle/src/schema/vmad.cpp:walk_script_entry — empty-name script entries

`source:xEdit Core/wbDefinitionsFO4.pas:3861-3865 @93cc0bc5` (`wbScriptEntry`) is a plain struct: length-prefixed ScriptName, Script Flags, Properties, with no empty-name branch. xEdit reads the flags byte and property count even when the name is empty. `walk_script_entry`, `read_vmad_scripts`, and the alias scripts in `read_quest_vmad_extras` follow that. The leading script list, quest aliases, and the INFO, PACK, PERK, and SCEN fragment headers all use `wbScriptEntry`. `walk_script_entry` still returns whether the name is nonempty, so `fragment_script_count` keeps its meaning.

The QUST fragment header is different. `source:xEdit Core/wbDefinitionsFO4.pas:3984-3990 @93cc0bc5` (`wbVMADFragmentedQUST`) wraps Script Data in `wbUnion` with `source:xEdit Core/wbDefinitionsCommon.pas:5866-5876 @93cc0bc5` (`wbScriptFragmentsEmptyScriptDecider`), which picks `wbNull` for an empty ScriptName. So `walk_quest_extra` and `read_quest_vmad_extras` keep the empty-name guard for that header only.

Corpus: 0 of 20,173 vanilla VMADs have an empty-name script entry, so this is a no-op on vanilla. It matters for mod data, where the old skip would misparse what follows. Regression: `Radicle/tests/test_vmad_array_elements.cpp:EmptyNameScriptEntryStillWalksFlagsAndProperties`.
