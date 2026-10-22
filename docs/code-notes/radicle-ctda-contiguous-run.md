# Radicle CTDA contiguous run

## Radicle/include/fo4/schema/ctda.hpp:CtdaCondition / Radicle/src/schema/ctda.cpp:parse_ctda_condition / serialize_ctda_condition

The three bytes following the type byte are stored padding (`CtdaCondition::padding`), not new semantic fields. source:xEdit Core/wbDefinitionsFO4.pas:5188 @93cc0bc5 declares `wbUnused(3)`. The official MQ101 record has nonzero values there in eight log-entry CTDA payloads; for example, stage ordinal 0/log 0 contains `00ac9313000000004a000000317d0a00000000000000000000000000ffffffff`. Dropping those bytes makes the existing lossless log-entry check reject otherwise supported payloads. Retain them through native parse/serialize and native scalar edits; new conditions default to zero. No runtime meaning is assigned. Existing C ABI layouts and PluginTool JSON fields are unchanged; JSON has no padding field; the quest stage and alias replacements carry it within the same scope in two passes: an exact content match apart from padding first, then the padding at the same index for still-unmatched conditions (`PluginEditTool/native/src/plugin/RadiclePluginQuestCondition.cpp:carryConditionPadding`), and other JSON replacement paths write zero. The CTDA payload is a fixed 32-byte struct (source:xEdit Core/wbDefinitionsFO4.pas:5186-5249 @93cc0bc5), so `parse_ctda_condition` accepts only 32 bytes and the run reader reports any other size as `CtdaNotLosslessError`. GetEventData (function 576, source:xEdit Core/wbDefinitionsFO4.pas:554 @93cc0bc5) uses the same 32-byte payload. Truncated, oversized or malformed runs remain subject to the existing lossless refusal checks. The measured counterexample and remaining loader/runtime evidence are tracked in RE #445.

## Radicle/tests/test_quest_stages_real.cpp:QuestConditionCorpus.EveryStoredRunIsLossless

The seven official-master cases check each contiguous CTDA/CIS run on every physical QUST. The existing lossless codec must consume the whole run and return one condition per raw CTDA; its byte comparison covers both CTDA payloads and the ordered CIS strings. Counts and nonzero padding totals (`RecordProperty` `PaddingCount`) are derived from the source records, including an empty condition corpus in DLCworkshop02.esm. Only an unset `FO4_DATA_DIR` permits a skip; configured missing masters fail. This verifies the codec across the stored corpus, not list ownership, C ABI integration or runtime evaluation.

## Radicle/src/schema/ctda.cpp:write_ctda_conditions_in

The replaced run is the first CTDA plus the consecutive CTDA/CIS1/CIS2 subrecords after it; the first other subrecord ends it. Previously every later CTDA extended the erased range, so subrecords between separate condition lists (SCEN phases, PACK, QUST, PERK) were deleted. A CTDA after the run has ended means the scope holds several lists, so the function returns `multiple_lists` and leaves the record untouched (fail closed). Callers scope the write to one list with a begin/end range.

## Radicle/include/fo4/schema/ctda.hpp:CtdaScopeWrite

`multiple_lists` is a refusal. `write_ctda_conditions` maps it to false. `Radicle/src/schema/condition_scopes.cpp:set_record_scoped_conditions` returns `ScopedConditionStatus::multiple_lists` for that refusal; only other results outside replaced/inserted become `unchanged`.

## Radicle/include/fo4/plugin/mutation.hpp:set_record_conditions

Resolves the window with `Radicle/src/schema/condition_scopes.cpp:root_condition_window` and writes only that window with `write_ctda_conditions_in`, so the bare `Conditions` path edits the record's root list and never a nested scope (`Radicle/src/schema/condition_scopes.cpp:root_condition_window`). It returns `ConditionMutationStatus`: `record_missing`, `multiple_lists`, `alias_structure_invalid`, `no_condition_list`, `unchanged`, or `updated`. `alias_structure_invalid` is new and refuses a QUST whose alias array is malformed, so the nested ranges cannot be known. A record with no root list gets one only where `Radicle/src/schema/condition_scopes.cpp:root_insert_anchor` defines a position (the rows of `k_root_list_anchors`, listed in `Radicle/docs/code-notes/Radicle.md` under `Record-level root condition lists (#602 WP-7)`); every other record with no root list, and an empty replacement on one, returns `no_condition_list`, and the caller stages nothing. Both `inserted` and `replaced` count as `updated`. Only an updated record clears its original-range and decompressed-payload caches and the plugin's original range.

Calls `write_ctda_conditions_in` directly and returns `ConditionMutationStatus`: `record_missing`, `multiple_lists`, `no_condition_list`, `unchanged`, or `updated`. The old bool conflated a missing record with a multiple-list refusal. A non-empty replacement on a record with no CTDA list returns `no_condition_list`: the whole-record path places a list only where `Radicle/src/schema/condition_scopes.cpp:root_insert_anchor` defines an anchor, and the caller stages nothing otherwise. An empty replacement on such a record returns `unchanged` (`CtdaScopeWrite::no_op`, nothing to remove), so the caller succeeds without staging. Only an updated record clears its original-range and decompressed-payload caches and the plugin's original range.

## PluginEditTool/native/src/plugin/RadiclePluginConditions.cpp:RadiclePluginBackend::setConditions

Whole-record multiple-list refusal maps to `ConditionScopeMultipleLists`, matching `RadiclePluginMutation.cpp:RadiclePluginBackend::setConditionsAtPath`. The existing missing-record precheck retains `RecordNotFound`; `ConditionMutationStatus::no_condition_list` maps to `NoConditionList` (a record with no condition list, refused before staging), and a defensive `record_missing` maps to `RecordNotFound`. The existing-list read that feeds `buildConditions` is `Radicle/src/schema/condition_scopes.cpp:read_root_conditions`, and a refused root read stages nothing. `RadiclePluginBackend::getConditions` uses the same reader, and its status mapping lives in `PluginEditTool/native/src/plugin/RadiclePluginConditions.cpp:rootConditionError`. JSON condition parsing, editor-ID resolution and staging behavior are retained. The touched whole-record method moves into the existing conditions translation unit because `RadiclePluginMutation.cpp` exceeded the enforced 750-line limit; unrelated one-line conditionals remain unchanged.

## PluginEditTool/native/tests/unit/RadiclePluginMutationTests.cpp:makeMultipleConditionListsPlugin

The fixture follows `Radicle/tests/test_condition_scopes.cpp:ConditionScopes.RefusesScopeHoldingSeveralConditionLists`: a QUST reference alias with two CTDA runs separated by ALFA. The whole-record and `Aliases[0].Conditions` backend regressions assert `ConditionScopeMultipleLists`, no pending edit and unchanged file bytes. The whole-record assertion fails before the status fix; the scoped assertion already passes. `Radicle/tests/test_ctda_model.cpp:make_plugin_with_layout` assigns distinct function IDs to CTDA entries so unchanged sibling lists can be checked without source comments.

## Radicle/include/fo4/plugin/mutation.hpp:set_record_conditions follow-ups

The bare path now reads the root list only (`Radicle/src/schema/condition_scopes.cpp:root_condition_window`); the remaining refusals and their xEdit reasons are in `docs/code-notes/GARDEN-TOOLS.md` under `root_condition_window`. For QUST the bare path addresses only the Quest Dialogue list (see the QUST section below). The `root_condition_window` text in `docs/code-notes/GARDEN-TOOLS.md` predates that change and still describes both QUST lists as one window. INFO has a single condition list in xEdit (`wbDefinitionsFO4.pas:9754` at 93cc0bc5), and the official masters agree: 60,230 INFO records have no CTDA run and 42,232 have exactly one. `Targets[k]` (`Radicle/src/schema/condition_scopes.cpp:quest_target_ranges`) is a flat ordinal across all objectives, while xEdit nests targets under each objective (`wbDefinitionsFO4.pas:10772`, `:10779`); this is a path-grammar choice with no RE or corpus proof, so the owner decides whether to keep it.

Records with several condition lists remain refused by whole-record writes: QUST top-level conditions alongside conditioned objectives/targets, PACK and PERK. INFO has one condition list in xEdit (`wbDefinitionsFO4.pas:9754`); all 42,232 vanilla INFO with conditions hold a single run. Current scoped paths cover magic-effect entries and quest aliases. Additional scoped paths remain follow-ups.

## Radicle/src/schema/ctda.cpp:read_ctda_conditions_in / Radicle/include/fo4/schema/ctda.hpp:CtdaMultipleListsError

The reader uses the same contiguous CTDA/CIS boundary as the writer. A later CTDA after another subrecord closes the first run throws `CtdaMultipleListsError`; no flattened vector is returned. CIS strings after a separator are not attached to the preceding condition; their raw subrecords remain untouched, outside the run the writer replaces. `CtdaModel.ReadDoesNotAttachCisAfterTheRunEnds` reproduces the old incorrect attachment. Whole-record reads and bounded reads share this check. One run and empty records keep their existing behavior, and callers can read separate runs with separate ranges. This changes the C++ read behavior without changing its function signatures or any C ABI export/layout.

## Radicle/src/schema/condition_scopes.cpp:read_scoped_conditions / PluginEditTool/native/src/plugin/RadiclePluginConditions.cpp:RadiclePluginBackend::getConditions / getConditionsAtPath

Scoped reads translate the shared multiple-list exception into the existing `ScopedConditionStatus::multiple_lists` with no condition values. Both PluginTool read routes report `ConditionScopeMultipleLists`, matching their writer routes. The existing two-list alias fixture reproduces success/flattening on unchanged main; native and backend regressions require refusal, unchanged bytes and no pending edit. The separate-run preservation assertions in `CtdaModel.ScopedWriteLeavesOtherListUntouched` and `ConditionScopes.SetAliasConditionsLeavesSiblingsAndDialogConditionsIntact` now use bounded reads rather than relying on the old flattening behavior.

The caller search across Radicle and PluginEditTool found the backend and native tests only. No C ABI source/header exposes this setter; its C++ result change does not change the C ABI or ABI version.

## Radicle/src/schema/condition_scopes.cpp:root_window_limit / root_insert_anchor / root_condition_window — QUST Quest Dialogue list

QUST has two top-level condition lists split by a `NEXT` marker. Quest Dialogue Conditions sits before `NEXT` (`source:xEdit Core/wbDefinitionsFO4.pas:10737-10739 @93cc0bc5`: `wbRStruct('Quest Dialogue Conditions')`, then the required `wbEmpty(NEXT, 'Marker')`, then `wbRStruct('Story Manager Conditions')`). `root_window_limit` ends the QUST window at the first `NEXT` or the first `INDX`, so a Story Manager list is never read or written by the bare `Conditions` path. `root_insert_anchor` puts the insert anchor for a missing Quest Dialogue list at `NEXT`, so a new list lands immediately before the marker. Without a `NEXT` marker the anchor is absent, the window still ends at `INDX`, and an insert is refused.

Corpus counts over the official masters: `NEXT` is present in 1,909 of 1,916 quests (the 7 without it have no conditions); 574 quests hold only a Story Manager list and 141 hold only a Dialogue list. Before this change a bare write on a Story-Manager-only quest overwrote the Story Manager list. `Radicle/tests/test_condition_scopes_records.cpp:QuestStoryManagerOnlyReadsEmptyAndInsertsDialogueBeforeNext`, `QuestDialogueOnlyBeforeNextReadsAndWrites` and `QuestDialogueAndStoryManagerListsEditDialogueOnly` pin the three cases. No descriptor addresses the Story Manager list, so it has no write path in this layer.
## Radicle/src/schema/ctda.cpp:k_citc_list_owners

Record types whose xEdit definition puts a `CITC` (Condition Count, u32) directly before a condition list. Verified against xEdit pinned at 93cc0bc5 (`source:xEdit Core/wbDefinitionsFO4.pas`):

- `wbCITC` (optional) at 3279 and `wbCITCReq` (required) at 3280; `wbInteger`'s sixth argument sets `defRequired` (`wbInterface.pas:3624-3631`).
- `wbConditions` at 5183-5254, with `.SetCountPath(CITC)` at 5254. `wbRArray` builds a `TwbSubRecordArrayDef` (`wbInterface.pas:8304-8311`).
- ACTI: 5611 record, `wbCITC` 5667, `wbConditions` 5668 (optional, single list).
- FACT: 6785 record, 6844-6845 (optional, single list).
- FURN: 6848 record, 6880-6881 (optional, single list; the list at 7054 is HDPT).
- MUST: 8795 record, 8814-8815 (optional, single list).
- SMBN: 8720 record, 8724-8725 (required). SMQN: 8735 record, 8739-8740 (required). SMEN: 8762 record, 8766-8767 (required).
- SCEN: 8879 record. The CITC is inside the Start Scene struct at 9073-9074 (predecessors INTT or SSPN). The record-level lists at 8903, 8905 and 9125 have no CITC (predecessors NAM0, NEXT, VNAM, CNAM in vanilla).
- PACK: 10526 record. The CITC is inside the Branch struct at 10603-10607, required (predecessor ANAM). The record-level list at 10571 has no CITC (predecessor PSDT in vanilla).

The table keys on record signature and, for nested lists, the predecessor. A default `after` accepts any predecessor for single-list records.

## Radicle/src/schema/ctda.cpp:write_ctda_conditions_in CITC on insert

xEdit count semantics (`source:xEdit Core/wbImplementation.pas @93cc0bc5`, `TwbSubRecordArray.UpdateCountViaPath` 21144-21172 and `BeforeActualRemove` 20774-20795):

- A non-empty list writes the count and creates the counter if it is missing (21158-21172).
- An empty list sets an existing counter to 0 and never creates or removes it (21164-21171).
- Required members are created on new records (10217-10224), and a missing required member is reported (11157-11158).

So when a non-empty list is inserted, the CITC must exist and equal the CTDA count. An existing CITC immediately before the insert point is updated in place. Otherwise, for a record in `k_citc_list_owners`, a CITC is written before the new run. Removing all conditions keeps the CITC and sets it to 0, which is what the replaced branch already did.

Vanilla corpus, read-only scan of Fallout4.esm, DLCCoast, DLCNukaWorld, DLCRobot, DLCworkshop01/02/03 (Synthesis.esm excluded), run with `python -I`:

| Record | CTDA runs | Run has CITC before it | Run without CITC | CITC with no CTDA (all value 0) |
|---|---|---|---|---|
| ACTI | 91 | 91 | 0 | 0 |
| FACT | 1 | 1 | 0 | 0 |
| FURN | 1 | 1 | 0 | 0 |
| MUST | 103 | 103 | 0 | 0 |
| SMBN | 90 | 90 | 0 | 46 |
| SMQN | 294 | 294 | 0 | 52 |
| SMEN | 1 | 1 | 0 | 17 |
| SCEN | 3194 | 288 (predecessor INTT 157, SSPN 131) | 2906 (predecessor NAM0, NEXT, VNAM, CNAM) | 781 |
| PACK | 3360 | 521 (predecessor ANAM) | 2839 (predecessor PSDT) | 931 |

No CITC value differed from its CTDA count in any run. Vanilla always writes the CITC when a list of an owned type has CTDAs. Required and nested lists keep a CITC of 0 when empty. ACTI, FACT, FURN and MUST never have a CITC without CTDAs.

Reachability (`Radicle/src/schema/condition_scopes.cpp:root_insert_anchor`): whole-record writes get an insert anchor only for the rows of `k_root_list_anchors`. A PACK branch scope (`package_branch_ranges`) anchors right after its existing CITC, so it updates that count. Root creation on ACTI, FACT, FURN, MUST, SMBN, SMQN and SMEN reaches the CITC create branch of `write_ctda_conditions_in` (see `Record-level root condition lists (#602 WP-7)` in `Radicle/docs/code-notes/Radicle.md`). A non-empty whole-record write to a record outside that table with no list still returns `no_condition_list` (PET: `NoConditionList`). The SCEN Start Scene entries cover each optional member that can precede the CITC: LCEP, INTT and SSPN (`source:xEdit Core/wbDefinitionsFO4.pas:9069-9074 @93cc0bc5`).

## PluginEditTool/native/src/plugin/ConditionCodec.cpp:buildCondition — stored reference by Run On

`buildCondition` stores a nonzero reference only for Run On Reference (2). A new row with a nonzero reference and any other Run On is refused (`reference requires runOn Reference`). Editing Run On away from Reference without touching the reference zeroes it. Only an untouched preserved row keeps a stored reference under another Run On. This follows xEdit's union handling: `source:xEdit Core/wbDefinitionsCommon.pas:5495-5509 @93cc0bc5` (wbConditionReferenceDecider) picks the branch by Run On, and `:1310-1322 @93cc0bc5` (wbConditionRunOnAfterSet) zeroes the reference only when Run On is edited in the UI.

## PluginEditTool/native/src/plugin/ConditionCodec.cpp:buildCondition — omitted value defaults to 0

An entry with neither `value` nor `compareGlobal` stores comparison 0.0. xEdit's CTDA "Comparison Value - Float" member is a plain `wbFloat` with no default value set (`source:xEdit Core/wbDefinitionsFO4.pas:5189-5190 @93cc0bc5`, `Core/wbInterface.pas:3709-3716 @93cc0bc5`), and the Use Global toggle resets it to 0 (`source:xEdit Core/wbDefinitionsCommon.pas:1296-1298 @93cc0bc5`). The zero default for a new CTDA is inferred from those lines, not from a traced creation path. Covered by `PluginEditTool/native/tests/unit/ConditionCodecValidationTests.cpp:condition codec: compareGlobal resolves to a use-global entry and value defaults to 0`.

## PluginEditTool/native/src/plugin/RadiclePluginQuestCondition.cpp:parseQuestCondition — omitted value defaults to 0

The quest log and alias condition parser keeps its own JSON-to-CTDA logic (typed FormKey targets through the conflict graph, unknown-key refusal, the GetEventData refusal per surface, and `PendingQuestCondition` output), so it is not merged into `buildCondition`. Its value default is 0.0 like `buildCondition`, not 1.0. Covered by `PluginEditTool/native/tests/unit/RadiclePluginQuestConditionDefaultsTests.cpp:Native SetQuestStages stores a quest condition without a value as comparison 0`.

## PluginEditTool/native/src/plugin/RadiclePluginMutation.cpp:copyAsOverrideLocked — Initially Disabled is per signature

The 0x800 flag is defined only for REFR, ACHR, NAVM and the placed-object set PARW, PBAR, PBEA, PCON, PFLA, PGRE, PHZD, PMIS (`source:xEdit Core/wbDefinitionsFO4.pas:3206 @93cc0bc5` for `ReferenceRecord`, `:4487` for ACHR, `:7214` for NAVM, and the REFR `wbRefRecord` blocks from `:11347`). The gate sits after the source-record lookup and before any LOSES, EXISTS or copy step, so a refused disabled copy writes nothing. `RadiclePluginDisabledOverride.cpp:copyAsDisabledOverride` inherits it. xEdit offers the action only from Referenced By on placed refs (`source:xEdit xEdit/xeMainForm.pas:9370 @93cc0bc5`, `:15758 @93cc0bc5`). Covered by `PluginEditTool/native/tests/unit/RadiclePluginAddMastersTests.cpp` (WEAP refused with the subsequent plain copy succeeding, REFR flagged). The web menu item in `PluginEditTool/web/src/RecordContextMenu.tsx:RecordContextMenu` is shown only when `matrix.Type` is in the same set.

## Radicle/include/fo4/schema/ctda.hpp:k_ctda_event_function_index — GetEventData is function 576 (GT-198)

GetEventData is CTDA function 576 on disk: xEdit `Core/wbDefinitionsFO4.pas:554` @93cc0bc5 lists `(Index: 576; Name: 'GetEventData')`. Every CTDA is one fixed 32-byte `wbStructSK(CTDA` (`wbDefinitionsFO4.pas:5186`), with no shorter variant. Offset 10 is `wbUnused(2)` (`wbDefinitionsFO4.pas:5194`), so `CtdaCondition::unknown2` keeps those bytes as opaque padding. The vanilla corpus scan (Fallout4.esm and the official DLC masters, 2026-10-11, output not committed) found 335 CTDA rows with function 576 and none with 4672. Mutagen's `EventFunctionIndex = 4672` (`PluginEditTool/Mutagen/Mutagen.Bethesda.Fallout4/Records/Common Subrecords/Condition.cs`) does not match disk data.

The 28-byte GetEventData branch in `Radicle/src/schema/ctda.cpp:parse_ctda_condition` and `serialize_ctda_condition` is removed. With the constant at 576 it would cut a real 32-byte row to 28 bytes and move run-on, reference and unknown3 four bytes early on rewrite. Every CTDA now takes exactly 32 bytes, and a shorter payload throws `std::out_of_range`. Tests: `Radicle/tests/test_ctda_model.cpp:CtdaModel.GetEventDataLayoutParsesAndSerializes` (32-byte function 576 round trip) and `CtdaModel.FunctionFourThousandSixHundredSeventyTwoIsNotGetEventData`. The native quest GetEventData refusals match function 576 in `PluginEditTool/native/src/plugin/RadiclePluginQuest.cpp:parseQuestCondition` and `adapterConditionUnrepresentable`.

The C# path reads the same bytes through Mutagen and keeps 576 as it is. `PluginEditTool/Mutagen/Mutagen.Bethesda.Fallout4/Records/Common Subrecords/Condition.cs:ConditionBinaryCreateTranslation` selects its `GetEventData` layout only when the raw u16 at payload offset 8 equals `EventFunctionIndex = 4672`, a value no vanilla row uses, and that 28-byte layout (Member at 10, Parameter3 at 12, RunOn at 16) is left unchanged. A vanilla 576 row falls through to `FunctionConditionData` in `ConditionFloatBinaryCreateTranslation` and `ConditionGlobalBinaryCreateTranslation`, which reads the xEdit 32-byte layout: Unknown2 at 10, Int32 parameters at 12 and 16, RunOn at 20, Reference at 24, Unknown3 at 28. Parameter Two is typed as a form by `ParameterTypeMixIn.GetCategory` (EventData), and the vanilla values include master-qualified IDs such as `0100FD17` (DLCRobot.esm). Changing the Mutagen constant to 576 would send vanilla rows to the 28-byte layout, so it must not change. Tests: `PluginEditTool/FO4RecordEditor.Tests/WriteToolsTests.cs:WriteToolsTests.SetConditions_GetEventDataIsFunction576AndReadsAsFunctionData`. Neither C# test was run in this change (no local build).

## PluginEditTool/native/src/plugin/ConditionCodec.cpp:describeCondition — GetEventData param1/param2 (GT-198)

Event-data rows (function 576) carry param1 and param2 as raw int32 values: xEdit `Core/wbDefinitionsFO4.pas:554` @93cc0bc5 types them as ptEvent and ptEventData, and the Parameter #3 Event Data decider is `itS32` (`wbDefinitionsFO4.pas:5223`). They are not form IDs, so `describeCondition` emits them as plain JSON integers, non-event rows are typed per function (see `docs/code-notes/pet-condition-codec.md`). `buildCondition` stores the JSON integers unchanged, so the round trip preserves them. The regression is `PluginEditTool/native/tests/unit/ConditionCodecTests.cpp` (`condition codec: GetEventData rows keep param1 and param2 through describe and build`): the old event-data gate omitted both fields, so a GetConditions then SetConditions round trip wrote them back as 0.
