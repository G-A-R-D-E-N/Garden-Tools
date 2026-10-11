# Radicle CTDA contiguous run

## Radicle/include/fo4/schema/ctda.hpp:CtdaCondition / Radicle/src/schema/ctda.cpp:parse_ctda_condition / serialize_ctda_condition

The three bytes following the type byte are stored padding (`CtdaCondition::padding`), not new semantic fields. source:xEdit Core/wbDefinitionsFO4.pas:5188 @93cc0bc5 declares `wbUnused(3)`. The official MQ101 record has nonzero values there in eight log-entry CTDA payloads; for example, stage ordinal 0/log 0 contains `00ac9313000000004a000000317d0a00000000000000000000000000ffffffff`. Dropping those bytes makes the existing lossless log-entry check reject otherwise supported payloads. Retain them through native parse/serialize and native scalar edits; new conditions default to zero. No runtime meaning is assigned. Existing C ABI layouts and PluginTool JSON fields are unchanged; JSON has no padding field; the quest stage and alias replacements carry it by position from the existing condition in the same scope (`PluginEditTool/native/src/plugin/RadiclePluginQuest.cpp:previousLogEntryConditions`, `RadiclePluginQuestAliases.cpp:previousAliasConditions`), and other JSON replacement paths write zero. The CTDA payload is a fixed 32-byte struct (source:xEdit Core/wbDefinitionsFO4.pas:5186-5219 @93cc0bc5), so `parse_ctda_condition` accepts only 32 bytes and the run reader reports any other size as `CtdaNotLosslessError`. GetEventData (function 576, source:xEdit Core/wbDefinitionsFO4.pas:554 @93cc0bc5) uses the same 32-byte payload. Truncated, oversized or malformed runs remain subject to the existing lossless refusal checks. The measured counterexample and remaining loader/runtime evidence are tracked in RE #445.

## Radicle/tests/test_quest_stages_real.cpp:QuestConditionCorpus.EveryStoredRunIsLossless

The seven official-master cases check each contiguous CTDA/CIS run on every physical QUST. The existing lossless codec must consume the whole run and return one condition per raw CTDA; its byte comparison covers both CTDA payloads and the ordered CIS strings. Counts and nonzero padding totals (`RecordProperty` `PaddingCount`) are derived from the source records, including an empty condition corpus in DLCworkshop02.esm. Only an unset `FO4_DATA_DIR` permits a skip; configured missing masters fail. This verifies the codec across the stored corpus, not list ownership, C ABI integration or runtime evaluation.

## Radicle/src/schema/ctda.cpp:write_ctda_conditions_in

The replaced run is the first CTDA plus the consecutive CTDA/CIS1/CIS2 subrecords after it; the first other subrecord ends it. Previously every later CTDA extended the erased range, so subrecords between separate condition lists (SCEN phases, PACK, INFO, QUST, PERK) were deleted. A CTDA after the run has ended means the scope holds several lists, so the function returns `multiple_lists` and leaves the record untouched (fail closed). Callers scope the write to one list with a begin/end range.

## Radicle/include/fo4/schema/ctda.hpp:CtdaScopeWrite

`multiple_lists` is a refusal. `write_ctda_conditions` maps it to false. `Radicle/src/schema/condition_scopes.cpp:set_record_scoped_conditions` returns `ScopedConditionStatus::multiple_lists` for that refusal; only other results outside replaced/inserted become `unchanged`.

## Radicle/include/fo4/plugin/mutation.hpp:set_record_conditions

Calls `write_ctda_conditions_in` directly and returns `ConditionMutationStatus`: `record_missing`, `multiple_lists`, `no_condition_list`, `unchanged`, or `updated`. The old bool conflated a missing record with a multiple-list refusal. A non-empty replacement on a record with no CTDA list returns `no_condition_list`: the whole-record path passes no insert anchor, so it cannot place a list, and the caller stages nothing. An empty replacement on such a record returns `unchanged` (`CtdaScopeWrite::no_op`, nothing to remove), so the caller succeeds without staging. Only an updated record clears its original-range and decompressed-payload caches and the plugin's original range.

## PluginEditTool/native/src/plugin/RadiclePluginConditions.cpp:RadiclePluginBackend::setConditions

Whole-record multiple-list refusal maps to `ConditionScopeMultipleLists`, matching `RadiclePluginMutation.cpp:RadiclePluginBackend::setConditionsAtPath`. The existing missing-record precheck retains `RecordNotFound`; `ConditionMutationStatus::no_condition_list` maps to `NoConditionList` (a non-empty write to a record with no condition list, refused before staging), and a defensive `record_missing` maps to `RecordNotFound`. Staging happens only for `updated`; an `unchanged` empty write succeeds without staging. JSON condition parsing and editor-ID resolution are retained. The touched whole-record method moves into the existing conditions translation unit because `RadiclePluginMutation.cpp` exceeded the enforced 750-line limit; unrelated one-line conditionals remain unchanged.

## PluginEditTool/native/tests/unit/RadiclePluginMutationTests.cpp:makeMultipleConditionListsPlugin

The fixture follows `Radicle/tests/test_condition_scopes.cpp:ConditionScopes.RefusesScopeHoldingSeveralConditionLists`: a QUST reference alias with two CTDA runs separated by ALFA. The whole-record and `Aliases[0].Conditions` backend regressions assert `ConditionScopeMultipleLists`, no pending edit and unchanged file bytes. The whole-record assertion fails before the status fix; the scoped assertion already passes. `Radicle/tests/test_ctda_model.cpp:make_plugin_with_layout` assigns distinct function IDs to CTDA entries so unchanged sibling lists can be checked without source comments.

## Radicle/include/fo4/plugin/mutation.hpp:set_record_conditions follow-ups

Records with several condition lists remain refused by whole-record writes: QUST top-level conditions alongside conditioned objectives/targets, PACK, PERK and INFO. Current scoped paths cover magic-effect entries and quest aliases. Additional scoped paths remain follow-ups.

## Radicle/src/schema/ctda.cpp:read_ctda_conditions_in / Radicle/include/fo4/schema/ctda.hpp:CtdaMultipleListsError

The reader uses the same contiguous CTDA/CIS boundary as the writer. A later CTDA after another subrecord closes the first run throws `CtdaMultipleListsError`; no flattened vector is returned. CIS strings after a separator are not attached to the preceding condition; their raw subrecords remain untouched, outside the run the writer replaces. `CtdaModel.ReadDoesNotAttachCisAfterTheRunEnds` reproduces the old incorrect attachment. Whole-record reads and bounded reads share this check. One run and empty records keep their existing behavior, and callers can read separate runs with separate ranges. This changes the C++ read behavior without changing its function signatures or any C ABI export/layout.

## Radicle/src/schema/condition_scopes.cpp:read_scoped_conditions / PluginEditTool/native/src/plugin/RadiclePluginConditions.cpp:RadiclePluginBackend::getConditions / getConditionsAtPath

Scoped reads translate the shared multiple-list exception into the existing `ScopedConditionStatus::multiple_lists` with no condition values. Both PluginTool read routes report `ConditionScopeMultipleLists`, matching their writer routes. The existing two-list alias fixture reproduces success/flattening on unchanged main; native and backend regressions require refusal, unchanged bytes and no pending edit. The separate-run preservation assertions in `CtdaModel.ScopedWriteLeavesOtherListUntouched` and `ConditionScopes.SetAliasConditionsLeavesSiblingsAndDialogConditionsIntact` now use bounded reads rather than relying on the old flattening behavior.

The caller search across Radicle and PluginEditTool found the backend and native tests only. No C ABI source/header exposes this setter; its C++ result change does not change the C ABI or ABI version.

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

Reachability: `condition_scopes.cpp` generates scopes only for ALCH, ENCH, INGR, SPEL and QUST, and `set_record_conditions` passes no anchor. So a non-empty write to a CITC record with no list returns `no_condition_list` (PET: `NoConditionList`), and an empty write returns `unchanged` with nothing staged, as documented above. The CITC insert path is reached only by a caller that passes `insert_anchor` for an owned record type; no production caller does today, so the owner table is exercised by the tests only.
