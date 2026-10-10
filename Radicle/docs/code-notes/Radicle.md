# Radicle code notes

## src/environment/world.cpp:resolve_water

The read-only selection query follows the static OG water evidence in the RE
record [RE 6916afb](https://re.nomadicinteractive.dev/ReverseEng/Fallout4_Runtime_Research/src/commit/6916afbcbec8ab09ecc0dfd30661502d0064f4f7/RE-Work/subsystems/radicle-re-garden-tools/water-395-341/README.md), RE commit
`6916afbcbec8ab09ecc0dfd30661502d0064f4f7`, sections 3.1-3.7 (verified RVAs
0x3B53D0, 0x493910, 0x3B4D40, 0x493400, 0x3ADC29 and 0x491910).
CELL XCWT takes precedence, exterior cells may use their winning WRLD, and the
global default selects Fallout4.esm form 0x18. The default is the winning `Fallout4.esm:0x18` WATR record, looked up by form ID, the same lookup the engine's global default makes (`VERIFY-2026-10-10.md` section 1 row 28). Radicle also treats a deleted `Fallout4.esm:0x18` as missing (`src/environment/world.cpp:winning_type`); RE `4cdafd2` does not cover deletion. A missing default record remains
explicitly unresolved; the SDK does not synthesize its visual data. An XCWT whose
target is missing or not a WATR is dropped, as the engine drops it in the
cell's InitItem fix-up, so the cell falls back to the worldspace water or the default
(RE `4cdafd2`, `RE-Work/subsystems/radicle-re-garden-tools/water-395-341/README.md`
Follow-up 2; `VERIFY-2026-10-10.md` section 7 item 3 for OG; section 3 row 9 for NG, with the same steps including the unlink; section 4 for AE, which shows no visible remap step; its unlink is COULD NOT CONFIRM, section 1 row 33). The RE does
not address deleted XCWT targets, so Radicle treats them as missing. A CELL or WRLD handle from an older override resolves like the winning version of that form (`src/environment/world.cpp:resolve_water` decodes `env.winner(form_key)`). The winning CELL version is decoded on its own: a winning override that omits XCWT or XCLW does not take them from the master. An XCWT of 0 is treated as absent (test CELL `0x921`); the engine's setter removes the extra for 0, confirmed for OG and NG and not read for AE (`VERIFY-2026-10-10.md` section 1 row 17). The engine's XCWT fix-up uses the last file that defined the cell (`FUN_140153690(cell, -1)`, [README, Follow-up 2](https://re.nomadicinteractive.dev/ReverseEng/Fallout4_Runtime_Research/src/commit/4cdafd28b8b576749fea4ffdfde5f3c32fefdb7c/RE-Work/subsystems/radicle-re-garden-tools/water-395-341/README.md)). The RE still leaves
the fix-up timing against a WATR from another plugin (F2-11) and runtime behaviour open.
An invalid or deleted WNAM parent ends inheritance. An invalid or non-WATR NAM2 at the
world where inheritance stops selects the default key with uses_default_water=1.
Inheritance uses the current world's bit 3 for water type and bit 0 for height,
respecting the effective WNAM/PNAM flags of `effective_parent_flags`, which the raw
PNAM readers do not apply. A visited set rejects active inheritance cycles. RE `6916afb`, `RE-Work/subsystems/radicle-re-garden-tools/water-395-341/README.md` §3.3 shows the engine has no cycle guard, so the RE expects a hang on such a cycle (static only, not executed; §5 fixture 7, VERIFY section 1 row 22). This throw is a deliberate divergence. A WRLD input always resolves its height through PNAM bit 0 (`src/environment/world.cpp:resolve_water`), so a parent cycle whose worlds set bit 0 makes the whole selection throw, even when the water-type walk (bit 3) would not cycle.

An absent XCLW selects the world default height for an exterior cell with Has Water
and a winning world (Radicle's inference: the RE shows only that the engine tests XCLW >= 2^31, VERIFY-2026-10-10 section 1 row 15, which calls the README's 'untouched' reading interpretation; the RE does not show what an absent XCLW starts as). A WRLD with no DNAM reports height 0.0 with has_height set, and
a WRLD input always reports a height. A deleted winning WRLD input is not rejected: it is decoded as stored, so a deleted version with no NAM2 or DNAM resolves to the default water with height 0.0. xEdit CELL DATA bits 0 and 1 are
[lines 5968-5969](https://github.com/TES5Edit/TES5Edit/blob/93cc0bc5a1251936c3c7859eee3150eda12a62d7/Core/wbDefinitionsFO4.pas#L5968-L5969);
WRLD PNAM bits 0 and 3 are [lines 13128 and 13131](https://github.com/TES5Edit/TES5Edit/blob/93cc0bc5a1251936c3c7859eee3150eda12a62d7/Core/wbDefinitionsFO4.pas#L13128-L13131).

The query preserves raw XCLW and reports its load-time integer transform only
in the effective height: positive halves round up, negative values truncate,
NaN or values below -2^31 become -2^31, and values at least +2^31 select the
world default. Thus -2^31 is a real height. Interior height volumes are outside
this query. These are static semantics, not live game acceptance. `VERIFY-2026-10-10.md` confirms NG statically on every row: from bytes for section 3 rows 1, 2, 3, 5 and 7, and from decompiles only for rows 4 (InitItem NAM2 cast and parent handling), 6 (default water init), 8 (WNAM/PNAM load) and 9 (XCWT fix-up). AE is in VERIFY section 4.

`include/fo4/c_api_records.h:radicle_water_resolve` returns the selected key,
borrowed winning WATR handle, default-selection flag and optional height.
The ABI number is 12, re-bumped if another ABI PR lands first; see
`docs/code-notes/radicle-water-writer.md`.
`bindings/csharp/Radicle.Interop/WaterSelection.cs:EffectiveWater` consumes it.

`probes/gardenengine/WaterSelectionProbe.cs:WaterSelectionProbe._Ready` reads
Commonwealth CELL selections through C ABI and passes each selected key to
GardenEngine's existing private `WaterBuilder.Material` conversion. It checks
the resulting material key/fog value against the selected winning WATR. The
reflection call remains confined to this probe; rendering, `WaterBuilder.Build`
adoption and in-game parity are separate from this consumer conversion check.
`Radicle/probes/gardenengine/*` is copied into a GardenEngine checkout to run; no CMakeLists in this repo builds it.
The default water key (Fallout4.esm, 0x18) is one shared constant pair, `src/environment/default_water.hpp:k_default_water_plugin` and `k_default_water_local`, used by `src/environment/world.cpp:resolve_water` and `src/c_api_world.cpp:radicle_water_resolve`.

GT-119 checks RE `6916afb`, `RE-Work/subsystems/radicle-re-garden-tools/water-395-341/README.md` §3.1 (CELL XCWT precedence), §3.2 and §3.5 (PNAM bit 3 for water type, bit 0 for height), §3.6 (XCLW load transform), §3.7 (WNAM/PNAM order), and §3.4 (invalid NAM2 selects the default key; a missing parent ends inheritance). Verified and matched: an invalid XCWT target, which the engine drops in the cell InitItem fix-up (RE `4cdafd2` Follow-up 2; `VERIFY-2026-10-10.md` section 7 item 3 for OG; section 3 row 9 for NG; section 4 for AE, which shows no visible remap step, and its unlink is COULD NOT CONFIRM, section 1 row 33); Radicle drops it too. Unresolved: the fix-up timing against a WATR from another plugin (F2-11), the runtime behaviour of that removal, the absent-XCLW initial value (Radicle's inference; VERIFY section 1 row 15), and a deleted parent or XCWT target (not addressed by the RE; treated as missing). Deliberate divergences: an active inheritance cycle throws where the RE expects a hang (static only, not executed; §3.3, §5 fixture 7), and a missing form ID 0x18 is not synthesised as a DefaultWater record (§3.4).

## Water corpus evidence (#341)

`tests/test_water_real.cpp:WaterRealData` covers every on-disk WATR in the
seven official masters through the native decoder and existing C ABI,
records DNAM sizes, and compares untouched master serialization byte for
byte. `docs/WATER.md` pins layout evidence for
`src/environment/base_records.cpp:decode_water`. The selection query above uses
static OG RE evidence from #395; runtime questions remain open. Raw links and heights are preserved by
`src/environment/world.cpp:decode_cell` and `decode_worldspace`.
## src/c_api_records.cpp:radicle_record_topic_children / bindings/csharp/Radicle.Interop/RecordReads.cs:TopicChildren

TopicChildren returns the ordered physical INFO records in the topic-children
group immediately following the selected DIAL version. It does not substitute
winning INFO overrides or collect responses from other plugins or sibling topics.
The hierarchy source is include/fo4/plugin/container.hpp:Group and
src/plugin/maintenance.cpp:owned_child_group; the consumer behavior is verified in
GardenEngine's vendored Mutagen.Bethesda.Fallout4/Records/Major Records/DialogTopic.cs:
DialogTopicBinaryOverlay.CustomEnd. The group label must resolve to that DIAL in
the selected plugin's master context. Missing, empty or differently typed following
groups have no responses; mismatched labels, nested children and non-INFO children
fail closed. Recursive lookup supports DIAL records nested beneath quest groups.

The returned array uses radicle_free_record_array; record handles remain borrowed from
the supplied environment. Managed TopicChildren copies only the handle array via
the existing Records helper. Keep the environment alive while reading the children
and their payloads. No C struct layouts change, but the four new exports move `RADICLE_ABI_VERSION` to 5, so a library built from the ABI-4 headers is refused by the binding's version guard on its first call, before any new export is reached (`AbiGuardTests.cs:ALibraryFromBeforeTheRecordPayloadExportsIsRejected` uses the `radicle_abi_stub_v4` stub). Consumers require a native library containing the new exports. TopicReadTests.cs covers selected and
overridden versions, physical order, sibling separation, nested topics, master/self
keys, empty results, invalid owners/types and disposed-environment access. This
does not validate scene decoding, voice playback or menu fidelity.

The source-linked installed-data comparison passed 92,861 checks across Fallout4,
Robot, workshop01, Coast, workshop02, workshop03 and NukaWorld: exact physical
DIAL keysets, ordered child FormKeys and selected-plugin INFO versions matched
the independent Mutagen overlay for 46,427 topics and 102,462 responses. The
31-test binding suite passed against the candidate native library, including ABI
layout and mismatch tests; generated declaration/probe drift checks passed.
These counts establish hierarchy projection only. Lookup currently walks the
selected plugin hierarchy per request; no voice loading performance claim is made.

## src/c_api_records.cpp / bindings/csharp/Radicle.Interop/RecordReads.cs:ReadPayload, ResolveFormId

The Garden UI importer needs the existing parsed record's ordered subrecords and
the native load order's FormID resolution. These reads copy opaque payload bytes,
including duplicate/unknown fields and extended-size markers, without guessing
their meaning or changing the environment. ReadPayload returns managed-owned
bytes that survive environment disposal. ResolveFormId uses the selected record
version's plugin context, including master and self indices.

Verified against source:include/fo4/formid.hpp:RawFormId and
source:src/formid.cpp:resolve_plugin_link, ResolveFormId rejects an index beyond the source plugin's master table and
created/save IDs; null and the explicit none sentinel return no key. The existing
environment link helper otherwise coerces oversized indices to the source plugin,
so this new strict read validates before invoking it. Existing decoders are unchanged.
Unknown four-byte signatures use reversible Latin1 in managed snapshots.

A `radicle_record` handle is only meaningful to the environment that produced it: it
is a pointer to one of that environment's own `RecordRef` versions, and the plugin
index it carries is an index into that environment's plugin list. All four exports
therefore call `src/c_api_records.cpp:belongs_to_env` first, which looks the record's
FormKey up in `env->value.versions` and compares addresses. That is the same
pointer-identity test `radicle_record_topic_children` already used, now shared.
`ReadPayload`, `CopySubrecord` and `ResolveFormId` did not have it. Without it a
handle from a second live environment resolved through the supplied environment:
`ResolveFormId` read `env->value.plugins().at(ref.plugin)` from the wrong plugin list
and returned a key built from another plugin's master table. The wrong answer is
plausible rather than obvious when both environments have a plugin at that index, so
`RecordReadTests.cs:ReadsRejectARecordFromAnotherLiveEnvironment` loads two plugins
that each define a record at local ID 0x800, checks the owner environment resolves it,
and requires the other environment to refuse both `ResolveFormId` and `ReadPayload`.

In `ResolveFormId` the membership test runs before the `RawFormId::is_none()` early
return, so the handle is validated for every `raw_form_id` and not only for the ones
that reach link resolution. `is_none()` is `value_ == 0xFFFFFFFFu`, so with the check
after it a foreign handle plus `0xFFFFFFFF` returned `RADICLE_STATUS_OK` with a null
key instead of being refused; `0` never took that path, because `is_none()` is false
for it. The test asserts refusal for `0x800`, `0` and `0xFFFFFFFF`, which covers both
the ordinary resolution path and the sentinel.

The C caller supplies four writable signature bytes. Copied payload buffers use
radicle_free_bytes; resolved plugin strings are borrowed until environment disposal.
As with existing record readers, keep the environment alive throughout a call;
Garden's environment read lease must cover a snapshot and its link resolutions.
No existing C layout changes. Consumers need a library containing these new exports.
The C# declarations are generated from c_api_records.h by the existing generator
and its drift check. UI field codecs, localization,
condition/script gates and original-payload parity remain separate integration work.

bindings/csharp/Radicle.Interop.Tests/RecordReadTests.cs verifies override choice,
duplicate unknown bytes, empty payloads, master/self/null/invalid FormIDs and
managed byte ownership after disposal. This is not a decoded UI-content claim.

## src/schema/form_links.cpp:visit_link_offsets

Record-aware classification precedes generic signature-only handling for the current copy work.

## src/schema/form_links.cpp:visit_coed_formids

`COED` is classified before the structured-slot table: owner at offset 0 always, offset 4 only when the owner record in the plugin being remapped is `NPC_`. Faction ranks are never FormIDs. Strict remap fails closed when owner type cannot be determined and the union dword is nonzero.

## src/schema/form_links.cpp:visit_coed_formids

Owner and union dwords are read before `fn(0)` because `remap_raw` rewrites offset 0 into the destination encoding; typing must use the pre-remap bytes with the old context.

## src/schema/vmad.cpp:inspect_quest_vmad

QUST extras after the primary scripts: bind byte, fragment count, optional fragment script, then fragments `{u16 Stage, u16 unused, i32 StageIndex, u8, wstring, wstring}`, then alias extras `{object, i16 version, i16 objFormat, scripts}`. Object format 2 stores unused u16, alias i16, FormID. Rewrite patches alias i16 (format 2 only) and Stage u16. StageIndex bytes are preserved.

## src/forms/quest_vmad.cpp:sync_quest_vmad_stages

xEdit FO4/TES5 name the fragment i32 `Quest Stage Index`; Mutagen `QuestScriptFragment.StageIndex`; UESP VMAD names that slot `logentry`. xEdit PERK uses a separate `Fragment Index` for chronological fragment numbers; QUST chronological ids live in `FragmentName` (`Fragment_N`). Stage u16 is xEdit `Quest Stage` / UESP `fragment.index` = QUST INDX. StageIndex is not range-checked against QSDT count. INDX remap is identity, else a unique stage-body match excluding INDX; array position is never identity. Format-1 alias objects fail closed on rewrite.

## src/forms/quest_alias_edit.cpp:write_quest_aliases

Whole-array replace copies the plugin, rewrites VMAD alias object IDs against the planned alias set, then splices aliases. Duplicate/malformed/trailing/unsupported VMAD throws before commit.

## src/forms/quest_stages.cpp:write_quest_stages

Whole-array replace remaps fragment Stage by INDX identity or unique body match, then splices. StageIndex and unrelated VMAD bytes stay identical.

## src/forms/placed_reference_copy.cpp:copy_info_as_new

The contained copy validates its ancestry on a working copy before committing changes.

## form_links.cpp :: visit_link_offsets (WTHR)

WTHR reuses FormID signatures for floats (VNAM/WNAM), bitfields (NAM1), opaque LNAM, color/speed arrays, and mixed SNAM/UNAM payloads. Remap only proven FormLink slots from WTHR_FORMID_SLOTS.md before the global signature table; otherwise float 1.0 (0x3F800000) becomes master index 63.

## src/graph/check_errors.cpp:check_load_order

Whole-load-order "Check for Errors" (#359, xEdit parity). Findings come only from slots Radicle can already prove: `collect_form_links` hits, the generated FormLink field/list target tables, `schema::preflight_plugin`, and the leveled-list graph. Unknown subrecords are never scanned, so a clean result means "no proven problem", not "no problem".

- `invalid_master_index` (warning): `FormLinkHit::key` is empty because resolution failed. A raw index past the MAST table is not an error by itself: `resolve_plugin_link` (source:src/formid.cpp) maps it to the owning plugin, as the game does, and official masters ship such links (#365, `DLCRobot.esm` REFR `XRFG`). Those links are checked like any other link, so they are only reported when the owning plugin has no such record (`unresolved_link`).
- `0xFFFFFFFF` is xEdit's None (`TwbFormID.IsNone`): `collect_form_links` leaves its key empty and the checker skips it like a null link.
- Engine-hardcoded forms: a missing target owned by `Fallout4.esm` (named explicitly, not the first plugin in the load order) with an object ID below `0x800` is skipped, matching xEdit `wbInterface.pas:TwbFormID.IsHardcoded` (`dev-4.1.6`, `_FormID < $800`). The game creates those forms itself (for example `000014`, `000007`), so `Fallout4.esm` has no records for them.
- `unresolved_link` / `deleted_target`: checked against the winning version of every record in the load order (last plugin wins), not against the referencing plugin's own masters. An unresolved link is an error only when `slot_targets` proves the slot (a generated field or list descriptor). Slots found only through the signature-union table (`formid_subrecords.inc`) are reported as warnings, because xEdit declares some of them mixed (TERM `RNAM`/`WNAM`, INNR `WNAM`, MGEF `DNAM`, ...) and their scalar values do not resolve. With that rule vanilla `Fallout4.esm` has no error-level findings (`CheckErrors.VanillaMasterHasNoErrorFindings`).
- `wrong_target_type`: only when `slot_targets` matches a generated field (record + subrecord + offset) or a non-nested list (per-subrecord at offset 0, packed arrays at 4-byte steps, as `formlink_lists.cpp:packed_element_count`). Wildcard lists and nested lists are skipped. `CheckFinding::offset` is `FormLinkHit::offset` (`fo4/schema/form_links.hpp`), the byte offset inside the subrecord.
- `CheckOptions::plugins` limits which plugins' records are checked; the winner index still covers the whole load order.

## src/graph/check_errors.cpp:find_leveled_list_cycles

Ported from `PluginEditTool/native/src/plugin/RadiclePluginLeveled.cpp:RadiclePluginBackend::checkCircularLeveledLists` and extended to LVSP. Edges are each leveled record's FormLinks in its winning plugin; non-leveled targets are ignored. A cycle is reported once, keyed by its rotation starting at the smallest FormKey, and the returned path repeats its first node at the end.

## src/c_api_check.cpp:radicle_env_check_errors

Rows are tab-separated so the existing `radicle_free_string_array` contract covers ownership. An empty subrecord column means the finding is record-level (preflight, cycle). Adding this export moved `RADICLE_ABI_VERSION` to 3.

## src/c_api_pex.cpp:radicle_pex_parse

The `.pex` C ABI (`include/fo4/c_api_pex.h`): parse bytes or a file into a `radicle_pex` handle, read the container header, per-object counts, object and parent class names and string table entries, and serialize back to bytes. Names and string table entries are handed back as caller-owned byte ranges released with `radicle_free_bytes`. The API never transcodes: the container stores one byte per character and the character encoding above `0x7F` is UNPROVEN, so Latin-1 stays an interpretation the C++ and C# layers apply rather than something the ABI assumes. Malformed bytes map to `RADICLE_STATUS_PARSE_ERROR` carrying the reader's own message, a missing file to `RADICLE_STATUS_IO_ERROR`, and a null argument or out-of-range index to `RADICLE_STATUS_INVALID_ARGUMENT`. Adding these exports took `RADICLE_ABI_VERSION` to 6, one step after the typed record payload exports that hold 5 on main; `RadicleNative.g.cs` was regenerated for the constant and `abi_layout_probe.cpp` is unchanged, because `fo4/c_api_pex.h` is not in the generator's header list yet. Raw PEX interop joins that list with the managed PEX model in A4.

## src/c_api_quest.cpp:radicle_quest_runtime_rows

The quest runtime C ABI (`include/fo4/c_api_quest.h`) splits one QUST record's VMAD and DNAM into structure and values. `radicle_quest_runtime_rows` reports the structure as tab-separated rows (`quest`, `script`, `fragment-script`, `fragment`, `alias`, `alias-script`, `unsupported`) so the existing `radicle_free_string_array` contract covers ownership. `radicle_quest_runtime_properties` reports every script property as a `radicle_quest_property` carrying its owner, owning script, name, type, flags and decoded value, released with `radicle_free_quest_properties`.

Properties are not rows because a VMAD value does not fit the row grammar: a string value can hold a tab or a newline, and an object property carries a FormID and an alias at once, so a tab-separated row cannot carry one losslessly. The reader already decoded every value into `fo4::VmadProperty` and the rows dropped all of them, which made two quests differing only in an integer property decode identically. The regression is pinned at both layers: `Radicle/tests/test_c_api_quest.cpp:RowsCarryNoPropertyFacts` and `ReportsEveryPropertyValue`, and `Radicle.Interop.Tests/QuestRuntimeTests.IntegerPropertyValuesAreDistinguishable`.

The scalar and reference value fields (`integer`, `real`, `boolean`, `object`, `property_alias`, `element_count`) each have a `has_` companion because a property carries at most one value of its type; `element_count` is the leading count of a struct or array property. The quest batch is `RADICLE_ABI_VERSION` 8, after the record payload at 5, PEX at 6 and SEQ at 7, and it is unreleased, the same rule `src/c_api_maintenance.cpp` used to stay at 3 when its exports were added. On the managed side `Radicle.Interop/QuestRuntime.cs` maps the entries onto `ScriptProperties`, `FragmentProperties` and `AliasScriptProperties`, and `DecodeRows` takes the rows and the entries together so the mapping is testable without a native plugin.

`radicle_quest_property.text` is transferred with a length. `text_size` counts the exact raw bytes and `text` points at those bytes plus one trailing NUL for convenience, so a value holding an embedded NUL (the 12 bytes Garden, NUL, Patch) arrives as `text_size` 12 while a NUL-terminated read of `text` sees only 6. A NULL `text` is no string; a non-NULL `text` with a `text_size` of 0 is the empty string. The bytes carry no claimed character encoding. `src/c_api_quest.cpp:copy_text` takes the byte count and `memcpy`s it, never `strlen`, so the transfer is lossless. Script and property names go through `src/c_api_quest.cpp:copy_c_string` instead, which is still `strlen`-based, and the managed side still reads both with `Utf8.Read` (`Marshal.PtrToStringUTF8`), so an embedded NUL in a name would still truncate there; whether a shipped plugin can contain one is UNPROVEN, and this pass did not change either. The regression is pinned by `Radicle/tests/test_c_api_quest.cpp:KeepsEmbeddedNulBytesInAPropertyString`, `EmptyStringPropertyDiffersFromNoString` and `Radicle/tests/test_quest_runtime.cpp:KeepsThePropertyValuesTheCAbiHasToCarry`, and on the managed side the lossless transfer itself is pinned by `Radicle.Interop.Tests/QuestRuntimeTests.EveryValueKindSurvives`, which reads a synthetic plugin through the native library; `QuestRuntimeAttachmentTests.EmbeddedNulBytesSurviveTheRowMapping` builds its entries by hand and covers the row mapping, so it cannot fail if the native transfer regresses. `QuestRuntimeValue.TextBytes` is derived from the stored `Text` with `Encoding.Latin1.GetBytes` rather than stored, so the record struct keeps structural value equality. Adding `text_size` did not bump `RADICLE_ABI_VERSION` past the quest batch's 8, on the same unreleased-batch rule.

A fragment property belongs to the fragment script the `fragment-script` row names, which is the contract `include/fo4/c_api_quest.h` already stated, so its own `script` field is an empty string and the script name is transferred exactly once, through that row and `QuestRuntimeData.FragmentScript`. `src/c_api_quest.cpp:radicle_quest_runtime_properties` had passed `data.fragment_script` for every fragment property and `Radicle.Interop/QuestRuntime.cs:DecodeRows` had substituted the row's name as well, so the name arrived twice and the header described something the code did not do. The non-duplicated contract wins, and it is pinned by `Radicle/tests/test_c_api_quest.cpp:FragmentPropertyCarriesNoScriptNameOfItsOwn` and `Radicle.Interop.Tests/QuestRuntimeAttachmentTests.FragmentPropertiesKeepTheirOwnScriptField`.

## src/schema/non_formid_slots.cpp:corpus_non_formid_slot

`formid_subrecords.inc` is a signature-only union of xEdit's FO4 form ID signatures, so a signature that is a form ID in one record type was treated as one everywhere (DIAL `PNAM` priority float, DIAL `SNAM` subtype code, SNDR `CNAM`, PACK `CNAM`, NPC_ `NAM4`, ...). #365: `remap_plugin_masters` rewrote those bytes whenever the master list changed. The deny list (`include/fo4/schema/non_formid_slots.inc`) is generated by `tools/corpus/non_formid_slots.py` from two sources that must agree: an official master stores a non-null value at that (record, subrecord) that resolves to no record in the official load order (its master index is past the file's MAST table, or the target does not exist), AND xEdit `wbDefinitionsFO4.pas` (`dev-4.1.6`) declares that subrecord in that record only as scalar data (`wbFloat`/`wbInteger`, the string types including `wbLStringKC` localized string IDs, the RGBA and colour helpers, a struct with no `wbFormID`, or a helper whose whole right-hand side, recursively, has no `wbFormID`/`wbUnion`; `wbPTRN := wbFormIDCk(...)` and the `wbVMAD*` helpers are therefore form IDs). In-table values matter as much as past-table ones: SCEN `WNAM` editor width 350 is `0x0000015E`, index 0, and was rewritten to `0x0100015E` when master 0 moved (`RemapNonFormIdSlots.InTableScalarsSurviveMasterReorder`). Localized string IDs are the same case: TERM `WNAM` welcome text ID `0x0000ABCD` became `0x0100ABCD` (`RemapNonFormIdSlots.LocalizedStringIdsSurviveMasterReorder`). `wbUnknown` slots count as scalar too, because xEdit never remaps bytes it has not decoded (TACT `PNAM`, PACK `BNAM`). Helpers are also read from `wbDefinitionsCommon.pas` (optional fifth generator argument), and a helper is judged by the element that declares the subrecord, not as a whole: `wbWorldLODData` holds NAM3 (water form ID) and NAM4 (`LOD Water Height` float), so only WRLD `NAM4` is denied. `remap_raw` and `rekey_form_links` also leave `0xFFFFFFFF` alone: it is xEdit's None value (`wbInterface.pas:TwbFormID.IsNone`), stored in vanilla EQUP `ANAM`, and was rewritten to `0x01FFFFFF` (`RemapNonFormIdSlots.NoneSentinelAndUndecodedSlotsSurviveMasterReorder`). The corpus evidence alone is not enough either: vanilla holds real links past the table (`DLCRobot.esm` REFR `XRFG`, see `GARDEN-TOOLS.md` on `resolve_plugin_link`), which the game reads as the owning plugin's, so `remap_raw` keeps remapping them. PACK `CNAM` is mixed in xEdit (record-level `Combat Style` form ID, and the `Data Input Values` union of bool/int/float after an `ANAM` type string), so `corpus_non_formid_slot` denies only a PACK `CNAM` whose previous subrecord is `ANAM`. SCEN `TNAM`/`HTID` depend on the scene action (`scene_action_scalar`): an action opens with a 2-byte `ANAM` type and closes with the empty `ANAM` marker. Inside an action `TNAM` is the Timer float and `HTID` is the Dialogue headtracking actor-ID array (or the empty Start Scene marker), so both stay scalar; only `HTID` in a Radio action (type 6, `Play Sound` SNDR) and the record-level `TNAM` (`Template Scene`, after the actions) are form IDs. It only removes slots from the generic branch of `form_links.cpp:visit_link_offsets`; record-specific handlers (WTHR, QUST, NAVM, structured slots) are unchanged. Regenerate it after adding new handlers or when new official files ship.
## src/plugin/maintenance.cpp:find_identical_to_master

Walks every group, nested ones included, bottom-up like xEdit `xeMainForm.pas:mniNavRemoveIdenticalToMasterClick` (`dev-4.1.6`): a record is reported only when it is identical to the previous version in load order AND its owned child group (`owned_child_group`: the world, cell or topic children group directly after it) would be emptied, so children are reported before their parent. An identical CELL with a changed REFR is therefore not reported, because removing it would orphan the cell's children. Injected records are not special-cased. A record is never ITM when its placement differs from the previous version (`record_placements`): moving a REFR to another CELL or an INFO to another DIAL is a real edit even when the bytes are identical. `remove_records` erases the reported keys anywhere in the tree and drops only the groups that this call emptied.

## src/graph/plugin_diff.cpp:diff_plugins

`changed_subrecords` holds each differing signature once, sorted. Payloads are first compared per signature in order. When that finds nothing but the ordered subrecord sequence still differs (a pure reorder across signatures), `add_reordered` trims the common prefix and suffix and reports every signature left in either window. The TES4 header is a record here, so master or HEDR changes show up as a changed `TES4`. `placement_changed` marks a record whose placement differs (`record_placements`), so a REFR moved between cells with identical bytes is reported as changed.

## src/c_api_maintenance.cpp:radicle_env_identical_to_master

#360 C ABI for the maintenance APIs, in its own header (`include/fo4/c_api_maintenance.h`) because `c_api_env.h` is near the line limit. `radicle_env_identical_to_master` passes the load order up to and including the plugin to `find_identical_to_master`; rows are `record	signature	previous`. `radicle_env_deleted_counts` wraps `count_deleted_records`. `radicle_env_unused_masters` lists masters missing from `used_masters`; when `scan_master_usage` is incomplete `used_masters` keeps every master, so the list is empty and `out_complete` is 0. `bindings/csharp/tools/generate_interop.py` reads the header and the layout probe now includes every header after `c_api_env.h`. ABI stays 3: these exports ship in the same unreleased version as `radicle_env_check_errors`.

## tools/inspect/main.cpp:print_udr

`fo4inspect udr` lists deleted placed references (the records xEdit's Undelete and Disable References would fix) plus the deleted-record and deleted-navmesh counts from `count_deleted_records`.

## src/graph/reachability.cpp:find_unreachable

Port of xEdit "Build Reachable Info" (#364) for Fallout 4, from `wbImplementation.pas` (`dev-4.1.6`): `TwbFile.BuildReachable` (roots), `TwbMainRecord.Reached`, `TwbGroupRecord.Reached` and `TwbElement.Reached` (propagation).
- Roots: engine-hardcoded forms (`Fallout4.esm` by name via `src/graph/reachability.cpp:k_game_master`, object ID below 0x800); every record in ADDN, ANIO, AVIF, CAMS, COBJ, CPTH, DFOB, DLVW, DOBJ, GMST, IDLE, LSCR, NAVI, RADS; HDPT `DATA` bit 0 (Playable); PERK `DATA` byte 3 (Playable, when the subrecord has 4+ bytes); NPC_ `ACBS` flag 0x4 (CharGen face preset); QUST `DNAM` flag 0x1 (Start Game Enabled); RACE `DATA` flags bit 0 (Playable) at offset 8, or 32 from form version 109. Layouts from `wbDefinitionsFO4.pas`.
- Propagation: every form link of the winning version, and of every version of a complex record (DIAL, WRLD, CELL, DOBJ), because xEdit's `TwbMainRecord.Reached` also reaches a complex record's master and overrides and each version follows its own links; a reached CELL/WRLD/DIAL reaches every record in its child group, and a reached child reaches its parent (xEdit's group `Reached` for types 1 and 6-10). Reverse rules: NPC_ reaches RELA that link it; QUST reaches SCEN whose `PNAM` and DLBR/DIAL whose `QNAM` link it; SMBN/SMQN/SMEN reach story-manager nodes whose `PNAM` links them; a FURN with `WBDT` bench type > 0 reaches COBJ whose `BNAM` is one of its keywords.
- NoReach (`src/graph/reachability.cpp:no_reach`): links xEdit declares with `wbFormIDCkNoReach` / `wbFormIDNoReach` are not followed forward (`TwbElement.Reached` skips them), but stay in the incoming index the reverse rules read. Fallout 4 table, from `wbDefinitionsFO4.pas` and `wbDefinitionsCommon.pas` (`dev-4.1.6`): whole subrecord DIAL `QNAM`, SMBN/SMQN/SMEN `PNAM` and `SNAM`, DLBR `QNAM`, DLVW `QNAM` and `BNAM`, INFO `TPIC` and `PNAM`, COBJ `BNAM`, REGN `WNAM`; CTDA offsets 12, 16 and 24 (both parameters and the Run On reference; the offset-4 comparison global still reaches); COED and XOWN offset 0; XLOC offset 4 (key); ECZN `DATA` and FACT `XNAM` offset 0; MISC `CVPA` and COBJ `FVPA` components (offset 0 of each 8-byte entry); PLDT/PLVD offset 4 for location types 0, 1 and 4, PTDA offset 4 for target types 0 and 1. `wbActorValue` is NoReach too, but every AVIF is a root, so it is not listed. Tests: `test_reachability.cpp:NoReachLinksDoNotReachTheirTarget`, `AComplexRecordFollowsTheLinksOfEveryVersion`.
- Differences from xEdit: SCEN must link this quest in `PNAM` (xEdit accepts any `PNAM` link), and only links Radicle can prove are followed (`collect_form_links`), so slots it cannot decode can make a record look unreachable. Results have not been compared against an xEdit run. Vanilla `Fallout4.esm` alone reports 44,175 records (mostly REFR/INFO in unreached interior cells and topics) in about 80 s.

## src/plugin/maintenance.cpp:esl_eligibility

Native records are the plugin's own (owner is the plugin, TES4 excluded); `out_of_range` counts native object IDs above `k_light_local_mask` (0xFFF) or below the target floor (0x001 when `allows_hardcoded_range` holds, else 0x800), and `ids_fit_light_range()` is true when there are none. That method says nothing about the TES4 light flag, which is why it is not called `already_light`. `capacity` is 0xFFE native records, the limit in xEdit's eligibility script (`Build/Edit Scripts/Find ESP plugins which could be turned into ESL.pas` at `93cc0bc5`: line 7 `iESLMaxRecords = $ffe`, line 8 `iESLMaxFormID = $fff`). xEdit's allocator lets a Fallout 4 plugin use IDs below 0x800 only when its HEDR version is at least 1.0 and it has a master (`Core/wbImplementation.pas` at `93cc0bc5`, `TwbFile.GetAllowHardcodedRangeUse`, lines 4070-4090). `src/plugin/mutation.cpp:allows_hardcoded_range` holds that rule; when it is false `capacity` is 0x800 (IDs 0x800-0xFFF) and `local_form_id_range` gives a light plugin `[0x800, 0xFFF]` (see `Radicle/docs/ESL_FORMIDS.md`). Tests: `test_maintenance.cpp:EslEligibilityKeepsTheFullFloorWithoutHedrOneAndAMaster`, `test_mutation.cpp:LightPluginKeepsTheFullFloorWithoutHedrOneAndAMaster`. The below-floor count lives here, so `radicle_env_esl_eligibility` and `PluginEditTool/native/src/plugin/RadiclePluginEsl.cpp:checkEslEligibility` report the same `out_of_range`, including a masterless plugin's IDs below 0x800. `capacity` follows the xEdit script (0xFFE, line 7 above), while the backend compactor's capacity is the slot count of its target range (4095 for 0x001-0xFFF, 2048 for 0x800-0xFFF); the two answer different questions and are deliberately not unified. Tests: `test_maintenance.cpp:EslEligibilityFollowsTheXEditRecordLimitAndIdCeiling`, `test_maintenance.cpp:EslEligibilityCountsNativeIdsBelowTheTargetFloor`, `test_c_api_maintenance.cpp:EslEligibilityMatchesTheTargetFloorForMasterlessPlugins`, `RadiclePluginEslTests.cpp:Native ESL eligibility counts masterless IDs below the full floor`. C ABI: `radicle_env_esl_eligibility` (layout unchanged).

## src/plugin/placement.cpp:record_placements

A record's placement is its parent record (resolved from the label of the enclosing world, cell or topic children group, as `environment.cpp:Environment::index_group` does) plus the type of the group that directly holds it (persistent, temporary, visible-when-distant, topic children, top level). Labels are resolved in each plugin's own master context, so the same parent compares equal across plugins with different master lists.

## src/plugin/onam.cpp:participates_in_fo4_onam

The record families come from xEdit `Core/wbDefinitionsFO4.pas` (`dev-4.1.6`), TES4 `ONAM` "Overridden Forms": ACHR, DIAL, DLBR, INFO, LAND, NAVM, PARW, PBAR, PBEA, PCON, PFLA, PGRE, PHZD, PMIS, REFR, SCEN. `PARW` (arrow projectile, commonlib_NonVR `ENUM_FORM_ID::kPARW`) was missing and `ACRE` (no Fallout 4 form type) was listed instead, so rebuilding ONAM dropped valid PARW overrides (`Onam.Fo4ParticipantsIncludeParwAndNotAcre`, `Native UpdateOnam stages a missing ONAM for a master-flagged plugin`). The writer calls the same rebuild for every dirty master-flagged plugin, so this affected ordinary saves as well as Update ONAM.

## PluginEditTool/native/src/plugin/RadiclePluginCompact.cpp:RadiclePluginBackend::compactToEsl

The target range comes from `PluginEditTool/native/src/plugin/RadiclePluginSupport.hpp:eslSlots`, which applies `fo4::local_form_id_range` to a light-flagged copy of the header: 0x001-0xFFF when `src/plugin/mutation.cpp:allows_hardcoded_range` holds (HEDR at least 1.0 and a master, xEdit `93cc0bc5` `Core/wbImplementation.pas:TwbFile.GetAllowHardcodedRangeUse`, lines 4070-4090), else 0x800-0xFFF. `checkEslEligibility` uses the same helper, so both report the same range text and slot count. The renumber pass runs on a copy with the light flag set, then restores the original header flags before staging, so compaction never sets the serialized ESL flag. A plugin with masters can come out as a full plugin holding IDs 0x001-0x7FF. Existing in-range IDs stay put; IDs below the floor or above 0xFFF are rekeyed in ascending order into free slots by `src/plugin/mutation.cpp:renumber_local_record`. Out-of-range nested records and out-of-range top-level records that have form ID labelled child groups are refused before anything is staged, by `PluginEditTool/native/src/plugin/RadiclePluginSupport.hpp:eslCompactionBlockers`: `NestedRecordCompactionUnsupported` for nested records, `ParentGroupRelabelUnsupported` for parents. A full (not light-flagged) plugin holding own IDs below 0x800 must be flagged light before the game loads it (`PluginEditTool/native/src/plugin/RadiclePluginSupport.hpp:lightFlagHint`), because `AddCompileIndex` only remaps IDs >= 0x800 (source: RE repo `ReverseEng/commonlib_NonVR` @ `dbaeca7`, `RE-Work/Runtime/03-FINDINGS.md`, section "FormID compile-index remap decoded"; open question GT-055 on the GARDEN-TOOLS Issue Tracking wiki).

When the TES4 light flag is not set, the compaction success, HEDR-lowering and already-ESL-compatible messages of `compactToEsl` and `RadiclePluginEsl.cpp:checkEslEligibility` end with `RadiclePluginSupport.hpp:lightFlagHint` (through `lightFlagAdvice`, which adds nothing for a plugin that is already light): "Run masters.SetLight to flag the plugin light; Radicle does not flag it from a .esl file name." (`PluginEditTool/native/src/plugin/RadiclePluginMasters.cpp:RadiclePluginBackend::setLight`). Radicle's save never sets the light flag from the file extension; only `src/plugin/writer.cpp:make_empty_plugin` flags a new .esl, and the reader decides light from the TES4 flag alone.

HEDR next object ID: `src/plugin/mutation.cpp:renumber_local_record` raises it to the new local ID + 1 only when the new ID is at or above the stored value (`if (new_local.value() >= next.header.next_form_id)`). `compactToEsl` sets it to the last assigned ID + 1 with `src/plugin/mutation.cpp:set_hedr_next_form_id` (declared in `Radicle/include/fo4/plugin/mutation.hpp`), inside the same transaction. That is xEdit's rule at `93cc0bc5`: `xEdit/xeMainForm.pas`, `TfrmMain.mniNavRenumberFormIDsFromClick` (line 13054), tracks the highest newly assigned ID (`HighFormID`, lines 13216, 13298-13299, 13333-13334), and its nested `UpdateNextObjectID` stores that + 1 (lines 13367-13373). Records that were already in range do not feed it; `reserve_local_form_key` scans upward past them. At full capacity the value is 0x1000. xEdit's increment behaviour for a light FormID at 0xFFF is not in the pinned excerpts (unverified), so the 0x1000 sentinel is Radicle's rule.

`Radicle/src/plugin/flags.cpp:set_plugin_light_flag` with light set calls `Radicle/src/plugin/mutation.cpp:lowered_light_next_form_id`. When every own ID is within the light range, HEDR next is above 0xFFF and above the highest own ID + 1, next is lowered to the highest own ID + 1 (or to the light floor when there are no own records). Without this, `src/plugin/mutation.cpp:reserve_local_form_key` refuses every new record in the light plugin. `compactToEsl`'s all-in-range branch and `PluginEditTool/native/src/plugin/RadiclePluginEsl.cpp:checkEslEligibility` use the same helper; eligibility reports "would only lower HEDR nextFormID". xEdit's `Core/wbImplementation.pas` `TwbFile.SetIsLight` (lines 6248-6266 at `93cc0bc5`) sets only the header flag and does not change next. Its `TwbFile.NewFormID` masks next to 12 bits for light files (`Mask := $FFF`, line 5211 at `93cc0bc5`), which Radicle's allocator does not. `PluginEditTool/native/src/plugin/RadiclePluginMasters.cpp:RadiclePluginBackend::setLight` reports the lowering ("lowered HEDR nextFormID to 0x..."). Tests: `Radicle/tests/test_plugin_flags.cpp` `PluginFlags.LightFlagLowersHedrNextToOnePastTheHighestOwnId`, `LightFlagKeepsHedrNextThatIsAlreadyInRange`, `LightFlagLowersMasterlessHedrNextToTheFullFloor`, `ClearingLightFlagNeverChangesHedrNext`; `RadiclePluginCompactTests.cpp` `Native SetLight lowers an out-of-range HEDR next ID to one past the highest own ID`; `RadiclePluginEslTests.cpp` `Native ESL eligibility reports a HEDR-only lowering when every record is already in range`.

`src/plugin/mutation.cpp:reserve_local_form_key` treats next = ceiling + 1 as a full plugin and reports that no local ID remains; only a next value beyond that is reported as outside the range. Tests: `RadiclePluginMaintenanceTests.cpp`: `Native CompactToEsl remaps top-level records and their in-plugin references` (next value, ESL message), `Native CompactToEsl leaves a light plugin that can still create records`, `Native CompactToEsl at full capacity leaves no ID and creation fails cleanly`; `test_mutation.cpp:ReserveLocalFormKeyReportsAFullLightPluginAndAnOverflowedNextSeparately`; `RadiclePluginEslTests.cpp` covers zero, 0x001, 0x7FF, 0x800, 0xFFF, 0x1000, all 4095 target slots and transactional rejection of 4096 records.

## src/plugin/mutation.cpp:renumber_local_record

A nested record is refused with a `FormIdError` (`mutation.cpp:renumber_local_record`). Top-level records are rekeyed in place, and their `form_key` changes. The child group that follows is not changed by this function. xEdit changes that group in the same step: `Core/wbImplementation.pas` at `93cc0bc5`, `TwbMainRecord.SetLoadOrderFormID`, lines 14294-14295 and 14332-14333.

`src/plugin/mutation.cpp:renumber_local_record` throws `FormIdError` ("renumbering a record with form ID labelled child groups is disabled until parent GRUP label rewriting is proven") when `has_form_id_labelled_child_group` (declared in `include/fo4/plugin/mutation.hpp`; the group types come from `include/fo4/flags.hpp:group_label_is_form_id`: world, cell and topic children and cell persistent, temporary and visible-distant children) finds a group labelled with the record's key. The check runs after the unchanged and target-exists returns and before `rekey_form_links`, so the plugin is unchanged. xEdit relabels the child group in the same step (`Core/wbImplementation.pas` `TwbMainRecord.SetLoadOrderFormID`, lines 14294-14295 and 14332-14333 at `93cc0bc5`); Radicle refuses until relabelling is built and tested. `PluginEditTool/native/src/plugin/RadiclePluginCompact.cpp:compactToEsl` does not call `parentGroupRelabelRefusal`. It uses `PluginEditTool/native/src/plugin/RadiclePluginSupport.hpp:eslCompactionBlockers` and `parentGroupRelabelError`. The two callers of `RadiclePluginSupport.hpp:parentGroupRelabelRefusal` are `PluginEditTool/native/src/plugin/RadiclePluginLifecycle.cpp:renumberRecord` and `PluginEditTool/native/src/plugin/RadiclePluginRenumber.cpp:renumberPluginFormIds`. Both return `ParentGroupRelabelUnsupported`: "Cannot renumber <key>: it has child groups labelled with its form ID; parent GRUP label rewriting is not supported yet." `renumberRecord` calls the refusal only when the new key differs and is free, so a same-key renumber still reports `changed:false` and an occupied target still reports `RecordFormIdExists`. `renumberPluginFormIds` runs its `RenumberTargetOccupied` check first, then the refusal loop, also on its dry run. That occupied check cannot fire today: the mappings assign consecutive IDs to the sorted own records, so a record that is not moving keeps its own index and no other mapping can target it. Ordering tests: `RadiclePluginCompactTests.cpp` `Native RenumberRecord to the same key reports unchanged for a DIAL with child groups` and `Native RenumberRecord onto an occupied key reports RecordFormIdExists for a DIAL with child groups`; WRLD with world children: `Native CompactToEsl refuses an out-of-range WRLD with world children and leaves the file unchanged` and `Native RenumberRecord refuses a WRLD with world children and leaves the file unchanged`. Tests: `Radicle/tests/test_record_lifecycle.cpp` `RecordLifecycle.RenumberRefusesDialWithTopicChildrenWithoutMutation` and `RecordLifecycle.RenumberRefusesWrldWithWorldChildrenWithoutMutation`; `PluginEditTool/native/tests/unit/RadiclePluginCompactTests.cpp` (CompactToEsl, RenumberRecord and RenumberPluginFormIds refusals).

## PluginEditTool/native/src/plugin/RadiclePluginSupport.hpp:eslCompactionBlockers

`PluginEditTool/native/src/plugin/RadiclePluginSupport.hpp:eslCompactionBlockers(plugin, first, last)` returns the own out-of-range records that are nested, or are top-level with a form ID labelled child group. `compactToEsl` and `RadiclePluginEsl.cpp:checkEslEligibility` both run it after the capacity check, so eligibility never says "can fit by remapping" for a plugin compaction refuses. Eligibility reports "would be BLOCKED: N record(s) cannot be remapped here (<count> <SIG> nested | with child groups)". It calls `has_form_id_labelled_child_group` once per out-of-range top-level record (O(records x groups)), bounded by the 0xFFE capacity check and small next to the plugin copy in each `renumber_local_record`. Tests: `RadiclePluginCompactTests.cpp`.

## src/environment/base_records.cpp: the HLOD and water record fields

GardenEngine's object HLOD and water layers read five things Radicle did not decode, so porting them off the removed record library could not compile. They are decoded here rather than in a second record stack, and the exports moved `RADICLE_ABI_VERSION` to 4.

- STAT `MNAM`: up to four 260-byte slots, each a NUL-terminated mesh path followed by unused bytes (`wbDefinitionsFO4.pas`, `wbStruct(MNAM, 'Distant LOD', [wbStringForward('Level 0', 260), ...])`). A slot with no terminator inside its own 260 bytes is a decode error rather than a read past the end into the next slot.
- ACTI `WNAM`: the water type.
- WATR: `FNAM` flags, the `DNAM` visual block through Screen Space Reflections, the three noise layers in their stored interleave (wind direction, speed, amplitude scale, UV scale, falloff, as in `wbDefinitionsFO4.pas`; decoded by `src/environment/base_records.cpp:decode_water`), the `NAM2`/`NAM3`/`NAM4` noise textures, and `NAM0`/`NAM1` velocities. `DNAM` is required from 188 bytes and may end anywhere up to 201, matching `SetOptionalFrom(4)`; the silt and reflection tail is optional, and silt is decoded only when all 12 of its bytes are present (`docs/code-notes/radicle-water-writer.md`). `radicle_water_get_info` no longer reports silt for DNAM sizes 192–199 (it now needs the full member at ≥ 200, as xEdit does); no official record is affected.
- WRLD `NAM3`/`NAM4`: the LOD water and its height (`wbDefinitionsCommon.pas`, `wbWorldLODData`).
- CELL `XCWT`: the cell's water type.

`c_api_env.h` was at the file-size limit, so the typed record decoders moved to `include/fo4/c_api_records.h`, which includes the environment header for the shared types; `generate_interop.py` reads it, and the generated bindings and layout probe are regenerated from the headers, so `radicle_csharp_interop_up_to_date` still holds. Tests: `Radicle.Interop.Tests/HlodFieldTests.cs` builds a plugin with these subrecords and checks every field, including a STAT without `MNAM` and the four-slot ceiling.

## Radicle/src/plugin/flags.cpp:fo4::set_plugin_light_flag

Flagging a plugin light rejects owned FormIDs below the floor that `src/plugin/maintenance.cpp:esl_eligibility` and `src/plugin/mutation.cpp:local_form_id_range` use: 0x001 when `allows_hardcoded_range` holds (HEDR at least 1.0 and a master), else 0x800. IDs above 0xFFF are also rejected, and the message names the actual range. The floor rule is xEdit `93cc0bc5` `Core/wbImplementation.pas:TwbFile.GetAllowHardcodedRangeUse`. Whether the engine loads masterless IDs below 0x800 in a light plugin is unverified; see `Radicle/docs/ESL_FORMIDS.md`. Clearing the flag never checks IDs. Tests: `Radicle/tests/test_plugin_flags.cpp:PluginFlags.RejectsLightFlagWhenMasterlessLocalIdIsBelowTheFullFloor`, `Radicle/tests/test_plugin_flags.cpp:PluginFlags.RejectsLightFlagBelowTheFloorWhenHedrIsBelowOne`, `Radicle/tests/test_plugin_flags.cpp:PluginFlags.AllowsLightFlagForLowLocalIdsWithHedrOneAndAMaster`, `Radicle/tests/test_plugin_flags.cpp:PluginFlags.AllowsLightFlagForMasterlessIdsAtTheFullFloor`, and `PluginEditTool/native/tests/unit/RadiclePluginEslTests.cpp:Native SetLight refuses a masterless plugin with owned IDs below 0x800`.

## Radicle/src/plugin/writer.cpp:remap_plugin_masters

`Radicle/src/plugin/writer.cpp:remap_plugin_masters` refuses dropping the last master of a light plugin that owns local IDs below 0x800 (GT-057). Light plugins with a master use the 0x001 floor from `Radicle/src/plugin/mutation.cpp:allows_hardcoded_range`; with no masters that floor reverts to 0x800, so those low own IDs would become invalid. The refusal throws `FormIdError` before any mutation. Full plugins, adds, reorders and light plugins with no low own IDs are unchanged. Tests: `Radicle/tests/test_master_metadata.cpp` `MasterMetadata.RemovingLastMasterOfLightPluginRefusesLowOwnIds`, `MasterMetadata.RemovingLastMasterOfLightPluginAllowsIdsAtTheFloor`, `MasterMetadata.RemovingLastMasterOfFullPluginAllowsLowOwnIds`; `PluginEditTool/native/tests/unit/RadiclePluginAddMastersTests.cpp` `Native CleanUnusedMasters refuses to drop the last master of a light plugin with low own IDs`.

## Radicle/src/c_api.cpp:radicle_plugin_remap_masters

`Radicle/src/c_api.cpp:radicle_plugin_remap_masters` remaps a copy of the plugin and assigns it back only when `fo4::remap_plugin_masters` succeeds, so a failed call (for example a dropped master that an own record or override still uses, which makes `encode_plugin_form_id` throw "is not in the destination master table") leaves the handle unchanged instead of empty (GT-090). Test: `Radicle/tests/test_c_api.cpp` `CApi.FailedRemapMastersLeavesThePluginUnchanged`.

## src/schema/scalar.cpp:scalar_descriptor

Record-agnostic: it returns the first whole-payload scalar field with the given subrecord signature, whatever record that signature sits in. Only tests call it (`tests/test_scalar.cpp`, `tests/test_scalar_embedded.cpp`, `tests/test_field_writer.cpp`).
