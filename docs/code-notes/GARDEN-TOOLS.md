# GARDEN-TOOLS code notes

## Radicle/src/schema/form_link_mixed.cpp:struct_payload_formid_offsets

Concept-level validation note: recognized payloads with unsupported extents must fail closed before FormID discovery or remapping. This note does not assert a new runtime or file-layout fact.

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
`E:/Tools/FO4IDE/dobj-evidence/corpus.json`; no game payload is committed.

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
Raw local output: `E:/Tools/FO4IDE/dobj-evidence/baseline-failing.txt`.

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
data. Local logs are `E:/Tools/FO4IDE/dobj-evidence/focused-passing.txt`
and `full-radicle.txt`. `git diff --check` passed. No generic descriptor,
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

## Native write model — deferred persistence (#174)

Ordinary record-edit RPCs stage in-memory dirty state via `stagePluginMutationLocked` and require `SavePlugin` to persist. Localized QUST CNAM table changes stage DLSTRINGS through `stagedLocalization` and flush on the same explicit save path (`persistPluginLocked` + dependency-first write). `DiscardPlugin` restores the last persisted snapshot without writing disk. See `docs/code-notes/174c-pending-lifecycle-audit.md` and the `#174C pending lifecycle` note below.

## PluginEditTool/native/src/plugin/RadiclePluginOverridePatch.cpp:createMergedPatch — overwrite

`copy_record_as_override` is called with `overwrite=false`. The C# oracle routes `CreateMergedPatch` through `ResolveConflict` with its default `overwrite=false`, which refuses a record the patch already contains (the result is not "Copied", so it is counted as a failure, not a forward). With `overwrite=false` the native op leaves an existing patch record intact and reports it as "already present in the patch" instead of silently replacing it.

## PluginEditTool/native/src/plugin/RadiclePluginOverridePatch.cpp:revertOverrides — overwrite

`copy_record_as_override` is also called with `overwrite=false` here. The C# oracle reverts through `AddOverride`, which is `GetOrAddAsOverride`: if the patch already contains the record it returns the existing one (non-null, so counted as reverted) rather than replacing its contents. Native therefore counts both `inserted` (a fresh revert) and `already_exists` (the patch already had that FormKey) as a successful revert, and never overwrites the patch's own version with the prior plugin's. This only differs from a plain revert when the patch loads before the reverted plugin and already carries an override of the same record.

## PluginEditTool/native/src/plugin/RadiclePluginOverridePatch.cpp:revertOverrides — skipped accounting

The apply loop increments `skipped` for any copy result other than `inserted`/`already_exists` — chiefly `source_containment_unsupported`, which `copy_record_as_override` returns for a nested record (e.g. a REFR inside a CELL) the patch does not already contain. Without this, a record the dry run listed as revertible could fail to copy during apply and disappear from the result entirely, so `Reverted N` under-counted with nothing explaining the difference; it is now reported in the `skipped` total, matching the C# apply loop (which increments `skipped` on an `AddOverride` failure).

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

`SetComponents(plugin, record, componentsJson)` parses `[{"component","count"}]`, resolves each `component` (FormKey text or EditorID) through `resolveListValue`, and — a deliberate divergence from the C# oracle, which stores any resolved FormKey unchecked — validates it against the loaded environment before writing: it must resolve to a `graph_.winner`, and its winning signature must match the record's component schema — a `MISC` scrap component must be a `CMPO`; a `COBJ` recipe component must be one of Mutagen's 13 `IItem` record types (`RadiclePluginComponents.cpp:isItemSignature`: AMMO ARMO BOOK CMPO COBJ NOTE ALCH INGR KEYM LVLI LIGH MISC WEAP — COBJ is itself an IItem, so a recipe may reference another recipe), so a resolving-but-non-item like a QUST/CELL is refused rather than stored. `count` is parsed with an explicit policy (`parseComponentCount`): absent → 1, otherwise an integer in `1..4294967295` (a value that is negative, zero, non-integer, or exceeds `UINT32_MAX` is rejected — no silent narrowing). Unresolved / mistyped / out-of-range entries are collected into a `Skipped …` clause rather than aborting the whole call, mirroring how the C# reported per-entry failures. External component owners are added as masters via `ensureOwnerMaster` before encoding, exactly as the KWDA/FLST add path does. Native is write-through (see the eager-persistence note), so the response ends "Written to disk." instead of the oracle's "save_plugin to persist".

## RadiclePluginOverridePatch.cpp:revertOverrides — containsComponent filter

`revertOverrides`' optional `containsComponent` argument (issue #173) now filters candidates instead of failing closed. The component is resolved through `resolveListValue` (FormKey text or EditorID); an unresolvable value returns `ComponentNotResolved`. A revert candidate (a record where the bad plugin currently wins) is kept only if its winning record carries a component list containing that FormKey, read via `components.cpp:decode_components` gated by `record_supports_components`. This is a **deliberate broadening** of the C# `MutagenLoader.CobjUsesComponent`, which checked `COBJ`/FVPA only: native checks any component-bearing record, so a `MISC` whose CVPA scraps into the component is matched too — both record kinds genuinely "use" the component, and `decode_components` handles CVPA and FVPA uniformly. A record type with no component surface never matches. The dry-run message gains the C#'s "(filtered to ones using …)" clause.

## Radicle/src/schema/ctda.cpp:write_ctda_conditions_in — range-scoped CTDA splice

`write_ctda_conditions_in(record, begin, end, conditions, insert_anchor)` generalizes the whole-record `write_ctda_conditions` to a subrecord half-open range `[begin, end)` so a single record can carry several independent CTDA runs (one per effect). It locates the CTDA run confined to that range (first/last CTDA, extended through trailing `CIS1`/`CIS2`, plus a `CITC` count immediately before the run when present) and returns a `CtdaScopeWrite`: `replaced` when an existing run was rewritten, `inserted` when the range had no run and `insert_anchor` held a slot to place the serialized conditions, `no_op` when there was nothing to do (empty replacement of an absent run), and `no_list` when a non-empty replacement had no run and no anchor. `write_ctda_conditions(record, conditions)` is now `write_ctda_conditions_in(record, 0, size, conditions, nullopt) == replaced`, preserving the exact root-record contract (missing run → `false`, never manufacturing a CTDA list). `read_ctda_conditions` is likewise `read_ctda_conditions_in(record, 0, size)`. Only a `CITC` **inside** the range and immediately before the run is refreshed; none is created, matching effects, which carry no per-effect `CITC`.

## Radicle/src/schema/condition_scopes.cpp:set_record_scoped_conditions — nested Effects[i].Conditions

The condition analogue of the nested FormLink list descriptor. `ConditionScopeDescriptor` (record signature, container token e.g. `Effects`, member token `Conditions`, `ConditionScopeKind`) is generated from `schema/fallout4/fields.json`'s `condition_scopes` array by `tools/condition_scope_codegen.py` into `generated_condition_scopes.inc`. The set is SPEL/ENCH/ALCH/INGR — the four Mutagen `ExtendedList<Effect>` records; **FO4 has no SCRL** (Scroll is Skyrim-only), so the handoff's SCRL is dropped.

**Effect boundaries are NOT a single EFID marker.** An Effect is `EFID? EFIT? CTDA*`: Mutagen writes `BaseEffect`→EFID with `WriteNullable` (null base effect ⇒ no EFID) and `Data`→EFIT only `if Data is {}`, and its parser triggers on EFID **or** EFIT **or** CTDA. So `condition_scopes.cpp:effect_element_ranges` segments the effect region with a small grammar scanner over stage(`EFID`=0,`EFIT`=1,`CTDA`=2): a subrecord starts a new element when no element is open, or when its stage is `!= CTDA` and `<= the current element's max stage` (EFID always opens a new element; a second EFIT opens one; repeated CTDA continues; CIS1/CIS2 are transparent and belong to the current element). Element *i* is then `[start[i], start[i+1] or region_end)` where `region_end` is one past the last effect-or-CIS subrecord. This is distinct from the trailing-terminator model of `nested_formlink_lists.cpp:terminated_group_range` and correctly addresses effects whose `BaseEffect`/`Data` are null. `set_record_scoped_conditions` splices only that element's CTDA run (insert anchor = element end, so a conditionless effect gets its CTDA appended after `EFIT` and before the next element), leaving siblings byte-untouched, then clears the record/plugin `original_range` caches like `mutation.hpp:set_record_conditions`. `scoped_condition_path(record_signature, path)` resolves `Container[i].Member` ASCII-case-insensitively (C# `BindingFlags.IgnoreCase` parity) and is keyed by record signature because all four descriptors are otherwise identical.

## PluginEditTool/native/src/app/PluginBackendTarget.cpp:invoke — GetConditionsAt/SetConditionsAt routing

`isRootConditionsPath` replaces the old `requireRootConditionsPath` gate. Root paths (empty or `Conditions`, case-insensitive) still route to the two-arg `GetConditions` / three-arg `SetConditions` backend ops, preserving the existing contract and the RoutingBackend tests. A non-root path now routes to the new `GetConditionsAtPath` / `SetConditionsAtPath` ops with the full argument list (including the path) instead of returning `ConditionPathUnsupported` at the target. The `ConditionPathUnsupported` error still exists but is now raised deeper — by `RadiclePluginBackend.cpp:getConditionsAtPath` / `RadiclePluginMutation.cpp:setConditionsAtPath` when `scoped_condition_path` cannot represent the path for that record signature (e.g. `Perks[0].Conditions`, or any record outside the SPEL/ENCH/ALCH/INGR effect set) — so the frontend degrades gracefully on paths this PR does not cover.

## PluginEditTool/native/src/plugin/RadiclePluginMutation.cpp:setConditionsAtPath — eager-persistence response

`setConditionsAtPath` mirrors `setConditions` (same editability/on-disk-freshness guards, same `buildCondition` JSON→CTDA codec and editor-ID resolver, same write-through to disk) but targets one effect scope. It resolves the record's signature from the mutated copy, looks up the `ConditionScopeDescriptor`, and delegates the splice to `set_record_scoped_conditions`. The response matches the C# shape `Set N condition(s) at {path} on {record} in {plugin}.` plus any `Skipped K: …` clause, then appends ` Written to disk.` — the eager-persistence ending shared with SetComponents and the list-item mutations, chosen (see decision log) over the C# oracle's `save_plugin to persist`. Root `SetConditions` keeps its own bare ending for compatibility. QUST `Aliases[i].Conditions` is deliberately out of scope here: its dual leading markers (`ALST`/`ALLS`), `ALED` terminators, and interleaved combined-alias list need a different scoping primitive and are deferred to a follow-up.

## Radicle/src/schema/condition_scopes.cpp:alias_element_ranges — QUST Aliases[i].Conditions

The second `ConditionScopeKind` (`alias`), for `QUST\Aliases[i].Conditions`. Unlike the effect scanner, QUST carries **record-level** CTDA outside any alias — `DialogConditions` and `UnusedConditions` are `ExtendedList<Condition>` on the Quest itself — so the scanner must bracket strictly and never touch those. `alias_element_ranges` segments the alias array so the element index matches **Mutagen's** `Quest.Aliases` list (the source of the `Aliases[i]` path contract): it anchors at `ANAM` (Next Alias ID), then walks the contiguous run while `is_alias_start` (**three** markers: `ALST` reference, `ALLS` location, `ALCS` collection). `ALST`/`ALLS` aliases run `start..ALED` and their CTDA run is the alias's conditions.

**Collection grouping (the subtle part):** Mutagen `Quest.cs:FillBinaryAliasParseCustom`, on the first `ALCS`/`ALMI`, rewinds and parses a greedy `ListBinaryTranslation<CollectionAlias>` into one `QuestCollectionAlias.Collection` and adds **one** `Aliases[]` entry — so a **contiguous run of `ALCS[+ALMI]`** is a single logical alias, not one-per-ALCS. The scanner groups the whole contiguous ALCS run into one non-conditioned element. Counting each ALCS separately (as `forms/quest_aliases.cpp` decode does) would shift every following alias's index — the same failure class as the effect-scanner's old EFID-only bug — so this **intentionally diverges from `quest_aliases.cpp`** and follows Mutagen, which owns the path index. `QuestCollectionAlias` has no `Conditions` property, so a collection element is flagged non-conditioned and **both** read and set return `element_not_conditioned` (native `ConditionScopeNotConditioned`) — a set of an *empty* list is rejected too, and no write occurs, mirroring C#'s reflection resolver which fails on the missing `Conditions` property before it ever looks at the supplied list. So this is format-correct, not a divergence.

**Canonical insert position:** inserting conditions into a *conditionless* alias must place CTDA at Mutagen's Conditions field slot, not blindly before `ALED`. `alias_condition_insert_anchor` returns the first post-Conditions field present in the bracket: for a **location** alias that is `ALCC` (`ClosestToAlias`, written after Conditions); for a **reference** alias it is the first of `k_reference_condition_successors` (`KSIZ KWDA COCT CNTO COED SPOR OCOR GWOR ECOR ALLA ALDN ALFV ALDI ALSP ALFC ALPC VTCK`, all written after Conditions per `QuestReferenceAlias` WriteRecordTypes — note `ALCC` is a *predecessor* for reference aliases). With no such field present it falls back to the `ALED` index. `ScopeRange` carries `insert_anchor` separately from `[begin,end)`; effect ranges set `insert_anchor = end`.

**Fail-closed structure** (mirrors `quest_aliases.cpp` validation): duplicate `ANAM`, non-4-byte `ANAM`, alias markers with no `ANAM`, a non-4-byte `ALST`/`ALLS`/`ALCS` start id, an `ALST`/`ALLS` alias with no `ALED`, a non-1-byte `ALMI` (Mutagen `MaxInitialFillCount` is a `Byte`), or any alias marker (`ALST`/`ALLS`/`ALCS`/`ALMI`/`ALED`) appearing after the single contiguous alias array (a malformed second array) → `alias_element_ranges` returns nullopt → `alias_structure_invalid` (native `ConditionAliasMalformed`) for both read and set. `read_scoped_conditions` returns `ScopedConditionsRead{ok,error,conditions}` so reads distinguish out-of-range / not-conditioned / malformed. The native `getConditionsAtPath`/`setConditionsAtPath` dispatch is otherwise generic over `ConditionScopeKind`; only the statuses map to errors.

## Radicle/src/plugin/mutation.cpp:reserve_local_form_key — FormID allocation without a record

Extracted the FormID-allocation half of `create_local_record` into `reserve_local_form_key(Plugin&)`: it assigns form keys, probes upward from `max(HEDR nextFormID, local floor)` for the first unused local id, advances HEDR nextFormID, and returns the `FormKey` — creating no record. `create_local_record` now calls it then places its record in the signature's top group. `copy_placed_reference_as_new` uses it to allocate a fresh destination-local key for a record it places inside a nested CELL bucket instead of a top group. It mutates the plugin in place (unlike `create_local_record`'s copy-then-commit); callers already working on a plugin copy commit it themselves. Throws `FormIdError` when no local slot remains.

## Radicle/src/forms/placed_reference_copy.cpp:copy_placed_reference_as_new — containment-aware CopyAsNewRecord (#179)

Duplicates a **REFR/ACHR** placed reference as a NEW destination-local record inside its source CELL containment — the CopyAsNewRecord analogue of `copy_placed_reference_as_override`. The two share `resolve_placed_containment` (extracted from the override's inline derivation): it locates the reference, requires the containment to end in `cell_children` + a persistent/temporary bucket, reads the parent CELL FormKey from the `cell_children` group label, validates the source CELL (and, for exterior cells, the WRLD) share the reference's containment root, and returns all of it. Scope is deliberately **REFR/ACHR only** — Radicle proves no other placed family's on-disk/FormLink contract (`placed_reference.hpp` documents this), so other signatures return `unsupported_signature` (this also fails-closed a contained INFO, whose signature is not REFR/ACHR). Divergences from the override, per issue #179's decisions: (1) the parent CELL FormKey **must already exist in the destination** — it is never synthesized; absent → `parent_cell_missing`. (2) A fresh key is allocated with `reserve_local_form_key` and the copy is inserted into the source's matching bucket (only the missing `cell_children`/bucket GRUPs are created, anchored on the destination CELL's actual path via `ensure_group_path`/`ensure_cell_children_group`/`ensure_reference_bucket`). (3) The new record's `form_key` is stamped to the allocated key. EditorID assignment and the response string stay in native. Master handling mirrors the override (parent CELL/WRLD owners + the reference's own FormLink owners). Malformed containment still throws `PlacedReferenceError` (native catches → `CopyAsNewFailed`).

## PluginEditTool/native/src/plugin/RadiclePluginMaintenance.cpp:RadiclePluginBackend::invokeLegacyBackend — contained-record routing

When the source record is not top-level but is present, CopyAsNewRecord routes supported contained records through their containment-aware primitive on a copy of the target plugin and maps the result: `created` → set EditorID (requested, else source EDID + `DUP`), write through, and return the existing `Copied … as NEW record {newKey} ('{edid}').` string; REFR/ACHR use `copy_placed_reference_as_new`, while INFO uses `copy_info_as_new` and requires the existing QUST/DIAL ancestry, creating only a missing type-7 Topic Children group. Unsupported signatures remain `SourceContainmentUnsupported`; missing target CELL or DIAL parents use their dedicated errors; `source_missing` → `SourceRecordNotFound`. Malformed containment throws and is caught as `CopyAsNewFailed`. The top-level path is unchanged.

## Radicle/src/forms/quest_aliases.cpp:decode_quest_aliases — QUST collection alias ALCS grouping (#203)

The decoder now models a **contiguous `ALCS[+ALMI]` run as one collection alias** holding ordered `QuestCollectionEntry` items (`quest.hpp:QuestCollectionEntry` = `{id, max_initial_fill_count}`), matching Mutagen `Quest.cs:FillBinaryAliasParseCustom` (greedy `ListBinaryTranslation<CollectionAlias>` → one `QuestCollectionAlias`, one `Aliases[]` entry) and the already-correct condition scanner `condition_scopes.cpp:alias_element_ranges`. Previously each `ALCS` produced its own `QuestAlias`, so any multi-`ALCS` collection shifted every following alias's `Aliases[i]` index — the #185/#186 index-shift class. This is a **Radicle-only** correction: neither the decoder nor `quest_alias_edit.cpp` is consumed by the native/RPC surface (C# `set_quest_aliases` goes straight to Mutagen), so no contract-visible behaviour changes.

Model decisions (per issue #203):
- **Entry[0] is not a logical Alias ID.** `decode_quest_aliases` sets `QuestAlias.id` and the `alias_ids` uniqueness set only for reference/location (`ALST`/`ALLS`); a collection's `id` stays 0 and its ALCS ids live in `collection_entries`. `decode_alias_semantics` walks the grouped `ALCS[+ALMI]` subrecords into `collection_entries` (4-byte ALCS via `alias_id`, 1-byte ALMI). The collection-only `QuestAlias.max_initial_fill_count` scalar was removed (redundant with per-entry data); `QuestAliasCreation.max_initial_fill_count` stays for single-entry authoring.
- **Collection entry ids do not participate in uniqueness or ANAM growth.** A shipped `ALCS(X) ALST(X)` (same id) decodes without a duplicate error; regression `test_quest_alias_collection_grouping.cpp:GroupsContiguousAlcsRunAsOneAliasWithOrderedEntries` proves an ALCS id equal to a following ALST id groups cleanly.
- **Edit scanner** `quest_alias_edit.cpp:inspect_alias_section` groups the whole run into one `AliasRange{collection=true}`; `find_range` matches ref/location ranges only (`!range.collection`), so `add/replace/remove/reorder_quest_alias` address logical aliases by ID and never a collection. `add_quest_alias` skips the id-collision check and `grow_next_alias_id` for collection creations (`is_collection`). `reorder_quest_alias`'s positional `to` now counts a collection run as one position; `test_quest_alias_collection_grouping.cpp:ReorderReferenceAcrossCollectionKeepsRunIntact` moves a reference across a two-entry collection and re-reads with the run intact.

`encode_base_alias` builds the raw subrecords only and no longer calls `decode_alias_semantics` on the collection path (the two public `encode_quest_alias` overloads do the single semantic decode); calling it there double-populated `collection_entries` for a single-ALCS creation.

Scope held out of the original grouping PR: **direct collection-ID-based mutation** (`find_range` skips collection ranges). Neighboring reference/location `reorder_quest_alias` still treats a collection span atomically. `test_quest_alias_real.cpp:marker_count` counts a contiguous `ALCS[+ALMI]` run as one logical alias.

## Radicle/src/forms/quest_aliases.cpp:encode_base_alias — collection multi-entry (#73)

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

## Radicle/src/forms/quest_aliases.cpp:decode_alias_semantics — QUST alias extra collections (#73)

`QuestAlias` now mirrors the extra FormLink collections already authored on `QuestAliasCreation`: `keywords` (`KSIZ`/`KWDA`), `items` (`COCT`/`CNTO` plus optional per-item `COED`), `linked_aliases` (packed `ALLA` rows), `spells`/`factions`/`packages` (repeatable `ALSP`/`ALFC`/`ALPC`), and the unique remaining members `SPOR`/`OCOR`/`GWOR`/`ECOR`/`ALDN`/`ALFV`/`ALDI`/`VTCK`. `decode_alias_semantics` fills those views after Fill Type decode when a plugin FormID context is present; `subrecords` stay the write authority. Dangling FormIDs follow the existing ALFR rule (`unresolved_links`, drop that one semantic entry). `COED` is not an independent list: each 12-byte extra binds to the immediately preceding `CNTO` (xEdit `wbCOED` / Mutagen `ContainerEntry.Data`). The owner/union is owner-aware: `find_record` on the same plugin types `NPC_` vs `FACT` so decode never fills both `global` and `required_rank`. Mixed items are valid. Location and collection aliases do not populate these fields.

## Radicle/src/schema/form_links.cpp:visit_coed_formids — owner-aware COED remap (#73)

`visit_link_offsets` handles `COED` before `structured_formids.inc`. Offset 0 (owner) always remaps. Offset 4 remaps only when the owner FormID resolves through the current plugin to an `NPC_` record. `FACT` leaves offset 4 as an i32 rank. Null owner or a zero union skips offset 4. Strict remap throws when the payload is not 12 bytes, when a nonzero union cannot be typed from a record in the plugin, or when the owner is neither `NPC_` nor `FACT`. Offset 4 is not registered in `structured_formids.inc`.

## Radicle/src/schema/form_links.cpp:visit_coed_formids

Owner and union dwords are snapshotted before remapping offset 0 so typing uses the old FormID encoding.

## Radicle/src/forms/quest_alias_edit.cpp:write_quest_aliases — full alias-array replace (#73)

`write_quest_aliases` replaces the contiguous post-ANAM alias span in one transaction. Collections cannot be addressed by ID through `remove_quest_alias`, so a whole-array writer is the SetQuestAliases primitive. Encodes every `QuestAliasCreation` first, then splices, then sets ANAM to `max(existing ANAM, max(ref/location id)+1)` without decreasing it. NNAM/GNAM/SNAM tail is preserved. `add/remove/replace/reorder_quest_alias` fail closed if the planned range sequence would join two collection runs.

## PluginEditTool/native/src/plugin/RadiclePluginQuest.cpp:RadiclePluginBackend::setQuestAliases / setQuestStages (#73, #74)

Native `SetQuestAliases` accepts `kind: reference|location|collection` (default reference), collection `entries: [{id, maxInitialFillCount?}, ...]` (single-entry `id`/`maxInitialFillCount` sugar remains; adjacent collections fail closed), numeric flags, one Fill Type among `forcedReference` / `uniqueActor` / `specificLocation` / `createReference` / `locationAliasReference` / `externalAlias` / `findMatchingNearAlias` / `findMatchingFromEvent` / `findMatchingClosest` / `referenceAliasLocation` / `externalAliasLocation` / `findMatchingLocation`, `conditions` on reference and location aliases (the #225 CTDA/CIS codec), and on reference aliases only `keywords`, `items`, `linkedAliases`, `spells`, `factions`, `packages`, nullable `spectatorOverridePackageList`/`observeDeadBodyOverridePackageList`/`guardWarnOverridePackageList`/`combatOverridePackageList` (`FLST`), `displayName` (`MESG`), `forcedVoice` (`VTYP`), `deathItem` (`LVLI`), and `voiceTypes` (`VTYP`/`FLST`/`NPC_`/`FACT`). Unknown keys fail closed, including nested `items` keys (`item`,`count`,`extra`) / `items[].extra` keys (`owner`,`global`,`requiredRank`,`itemCondition`) and `linkedAliases` keys (`keyword`,`alias`). `Radicle/src/forms/quest_aliases.cpp:insert_alias_conditions` writes the CTDA/CIS run after Fill Type members and before extras / `ALCC`. `Radicle/src/forms/quest_aliases.cpp:insert_alias_extras` emits those extras in Mutagen WriteRecordTypes order before `ALED` (`KSIZ`/`KWDA`, `COCT`/`CNTO`/`COED`, `SPOR`/`OCOR`/`GWOR`/`ECOR`, one packed `ALLA`, `ALDN`/`ALFV`/`ALDI`, `ALSP`, `ALFC`, `ALPC`, `VTCK`). `Radicle/src/forms/quest_aliases.cpp:decode_alias_semantics` fills the matching `QuestAlias` collections so extras are no longer raw-subrecord-only on decode. `ALLA` is xEdit `wbArray`: one subrecord of 8-byte `{Keyword FormID, Alias int32}` rows, not one `ALLA` per row. `Radicle/src/schema/form_links.cpp:visit_link_offsets` remaps QUST `ALLA` at offsets `0,8,16,...` and throws on a payload not divisible by 8. CNTO items use `fo4::schema::is_sig_base_object` (`k_generated_sig_base_objects` from xEdit FO4 `sigBaseObjects`, minus `NULL`). `RadiclePluginQuest.cpp:extraAliasMembersUnrepresentable` fail-closes existing aliases whose extra members are incomplete, out of that order, or whose embedded FormIDs fail the same type/count rules as authoring (`KYWD` keywords, `sigBaseObjects` items with count `>= 1`, optional 12-byte `COED` on the preceding `CNTO` with `NPC_`/`FACT`/`NULL` owner, owner-aware `GLOB`/rank union, finite `itemCondition`, `FLST` package-list overrides, `KYWD` ALLA keywords, `MESG` display name, `VTYP` forced voice, `LVLI` death item, `SPEL`/`FACT`/`PACK`, `VTCK` as `VTYP`/`FLST`/`NPC_`/`FACT`; NULL FormIDs are not modeled except COED owner/global). Typed FormKeys resolve through `graph_.winner` and masters are added before encode. `PluginEditTool/native/src/plugin/RadiclePluginQuest.cpp:parseAliasFill` maps the remaining Mutagen fill structs onto those JSON objects (`CreateReferenceToObject` ALCO+ALCA with Create At=0/In=0x8000, no `ALCL` Level; `LocationAliasReference` ALFA+ALRT `LCRT`; `ExternalAliasReference` optional ALEQ `QUST` + required ALEA; Near Alias ALNA+ALNT types 0/1; From Event ALFE+ALFD; Closest ALCC on reference aliases only; location `ReferenceAliasLocation` ALFA+optional KNAM; `ExternalAliasLocation` ALEQ and/or ALEA; `FindMatchingLocation` ALFE+ALFD before location `ALCC`). Nested fill objects fail closed on unknown keys. `createReference.level` is Mutagen `Level` / xEdit ALCL: 4-byte enum Easy=0, Medium=1, Hard=2, VeryHard=3 after `ALCO`/`ALCA`. Mutagen `Quest.xml` marks ALCL `nullable=False` so encode always writes it; missing ALCL on disk decodes as Easy. Values outside 0–3 fail closed. `RadiclePluginQuest.cpp:existingFillUnrepresentable` also fail-closes existing `ALCA.create_mode` outside `{0,0x8000}`, existing `ALNT.type > 1`, and existing null `ALFR` (`QuestSpecificReferenceFill` with no FormKey), because JSON cannot author those states. `RadiclePluginQuest.cpp:aliasConditionsUnrepresentable` checks members after the CTDA/CIS run: reference aliases may only keep modeled extras (`KSIZ`/`KWDA`/`COCT`/`CNTO`/`COED`/`SPOR`/`OCOR`/`GWOR`/`ECOR`/`ALLA`/`ALDN`/`ALFV`/`ALDI`/`ALSP`/`ALFC`/`ALPC`/`VTCK`) before `ALED`; location aliases may only keep post-condition `ALCC`. `parseQuestCondition` threads `errorType` through helper branches so alias JSON returns `InvalidQuestAliasPayload`. `requireLoadedFill` resolves non-null Fill Type FormKeys through `graph_.winner` to the Mutagen-pinned target set (`IPlaced` for ALFR, `NPC_` for ALUA, `LCTN` for ALFL, `LCRT` for ALRT, `QUST` for ALEQ, `KYWD` for KNAM, any loaded major for ALCO) before `ensureOwnerMaster`. `parseEntireUnsigned32` requires the whole numeric string to be consumed and to fit `uint32`. A non-string `kind` returns `InvalidQuestAliasPayload` instead of throwing. `SetQuestStages` allowlists stage keys (`index`, `flags`, `logEntry`, `logEntries`, `complete`, `fail`) and log-entry keys (`flags`, `note`, `logText`, `conditions`). `NAM0` and wrong `logEntry`/`complete`/`fail` types fail with no write. On the `logEntry` sugar path, `complete` sets QSDT bit 0 and `fail` sets bit 1 independently (`RadiclePluginQuest.cpp:setQuestStages`), so both true yields flags `3` rather than `fail` overwriting `complete`. Localized plugins refuse new CNAM text because string-table IDs are not allocated here. Alias-level CTDA/CIS uses the same `parseQuestCondition` / `adapterConditionUnrepresentable` / `serialize_ctda_run` path as log entries (`RadiclePluginQuest.cpp:aliasConditionsUnrepresentable`). Collections reject conditions. VMAD fail-closed is unchanged. `PluginEditTool/native/tests/unit/RadiclePluginQuestAuthoringTests.cpp` covers zero/multiple alias conditions, FormKey and scalar parameters, `compareGlobal`, RunOn Reference, CIS1/CIS2, external master insertion, malformed existing CTDA, existing unsupported CIS, wrong-type and unresolved FormKeys, neighbor preservation, reload, and no-write failures. `existingStagesUnrepresentable` / `existingAliasesUnrepresentable` decode the current arrays first and return `QuestAuthoringCoverageIncomplete` without writing when the record already has a non-lossless CTDA/CIS run, a non-zero INDX unknown byte, leading members, localized CNAM, multi-entry collections, unresolved Fill Types, or alias members outside the supported ALST/ALLS/ALCS subset.

The opaque `INDX` preservation behavior is documented below; the historical coverage summary above predates that fix.

## PluginEditTool/native/src/plugin/RadiclePluginQuest.cpp:setQuestStages — per-QSDT CTDA/CIS (#74)

`fo4::forms::lossless_quest_log_entry_conditions` (`Radicle/src/forms/quest_stages.cpp:lossless_quest_log_entry_conditions`) accepts only a leading `CTDA` plus attached exact-zstring `CIS1`/`CIS2` run that `serialize_ctda_run` reproduces byte-for-byte, then leaves `NAM2`/`CNAM` to the existing order checks. That Radicle gate is not enough for whole-array replace: `RadiclePluginQuest.cpp:adapterConditionUnrepresentable` also refuses EventData (layout 4672 or Mutagen function 576), unmapped functions, `unknown2 != 0` / `unknown3 != -1`, operators outside 0..5, unsupported run-on, and FormID slots that do not resolve to the `ConditionFunctions::editorFor` target type. `parseQuestCondition` rejects the name `GetEventData` and enum 576, requires FormKeys (and types) for record parameters, integers for scalar/`ParametersUseAliases`/`UsePackData` slots, `cis1`/`cis2` only on declared String/Event slots, and a placed `reference` only when `runOn` is `Reference`. Existing CIS on a non-text slot, or a text slot with a nonzero raw CTDA integer, is `QuestAuthoringCoverageIncomplete`. Masters are added via `ensureOwnerMaster` before `encode_plugin_form_id`. Members are written `QSDT [CTDA/CIS...] [NAM2] [CNAM]`. VMAD guards are unchanged.

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

- `PluginEditTool/web/src/hostbridge.ts:install` supplies the existing HTTP transport for the native host's loopback HTTP page, including when WebView2 exposes its default `chrome.webview` object. The legacy C# host serves `https://app.local` and retains its registered host objects. `PluginEditTool/web/src/hostbridge.test.ts` covers both transports.

- `PluginEditTool/native/tests/native_mcp_smoke.py:main` exercises the executable with an isolated MO2 profile and settings directory. It checks plugin discovery, pending edits, failed-edit preservation, explicit save, reload, discard, and invalid startup arguments. Pass `--build-dir` and optionally `--cmake` to include the Release host build.
- `PluginEditTool/native/src/textures/BcnDecoder.cpp:bc7`, `PluginEditTool/native/src/textures/PngWriter.cpp:deflate`, and `PluginEditTool/native/src/papyrus/Pex.cpp:Reader::u16` retain their existing byte values while making integer types explicit for the MSVC warnings-as-errors build.
- `PluginEditTool/native/apps/fo4ide/main.cpp:main` loads an explicitly requested MO2 instance through the existing `appInterop.OpenMo2Profile` route before serving requests. Invalid arguments or load failures stop startup.
- `PluginEditTool/native/apps/fo4ide/McpTools.cpp:registerPluginTools` exposes existing native plugin operations. `save_plugin` uses the existing in-place SavePatch route. `reload_mo2_profile` reloads the active profile and fails closed on pending edits unless `discard_pending` is true. Desktop and stdio sessions have separate pending state; reload after another session saves. Record creation retains the backend's supported-type and payload restrictions. `get_record` returns the existing record matrix; `list_records_summary` supplies decoded grid values and editable column paths.
- `PluginEditTool/native/include/fo4ide/graph/NodeDefinition.hpp:NodeDefinition::pin` and `PluginEditTool/native/src/graph/GraphDocument.cpp:GraphDocument::node` use distinct parameter names to avoid member-shadowing errors in the MSVC build.

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

Fallout4.esm (`D:\Fallout4Backup\Fallout 4\Data\Fallout4.esm`) uncompressed top-level groups, 2026-09-13: OMOD 2409 / MNAM present 1615 (1326×4, 144×8, 145×12) / FNAM present 70 (67×4, 2×8, 1×12); EQUP 38 / PNAM present 23 (3×4, 17×8, 3×12); MUSC 111 / TNAM present 111 (4 through 108). Every present payload is one subrecord and `size % 4 == 0`. Multi-entry arrays are skipped by `formid_subrecords.inc` (4-byte only).

`Radicle/src/schema/form_links.cpp:visit_link_offsets` dispatches `packed_formid_array_offsets` after IPDS/DOBJ and before the empty/global signature table. Foreign uses of the same signatures stay on their existing routes (NAVM PNAM vertices, QUST FNAM flags, MUST FNAM floats, NPC_ PNAM 4-byte listed FormID). Mixed NAVM/PROJ/EXPL/HAZD/OBTS/PERK helpers moved with this dispatch into `form_link_mixed.cpp` so `form_links.cpp` stays under the translation-unit size limit.

`Radicle/tests/test_packed_formlink_arrays.cpp` covers remap, fail-closed odd lengths, empty arrays, collision controls, rekey cache invalidation, and document order.

For `Radicle/src/schema/form_link_mixed.cpp:packed_formid_array_offsets`, the local commonlib_NonVR reference confirms the distinct runtime form types for OMOD, EQUP, and MUSC. Runtime member shape does not independently prove plugin subrecord serialization, xEdit definitions, or quoted RVAs; those claims remain concept-level here and are not shipped as runtime offset evidence.

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
  - every signature is returned (LAND and NAVM included); consumers filter by signature.
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
  - `XLKR`: 8 bytes as keyword + reference, or 4 bytes as reference only (Mutagen `<Break>`).
  - `XAPR`: FormID + `f32` delay.
- **Geometry fields:**
  - `XTEL`: door, position, rotation, `u32` flags, and at 36 bytes the transition-interior CELL. xEdit lists that member; a 32-byte form is also accepted.
  - `XPRM`: bounds `vec3`, RGBA floats, `u32` type. Bounds are returned as stored; Mutagen `PlacedPrimitive.FillBinaryBoundsCustom` and xEdit both double them.
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
- Every other record uses its first `MODL`.
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
