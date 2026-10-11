# Radicle CTDA contiguous run

## Radicle/src/schema/ctda.cpp:write_ctda_conditions_in

The replaced run is the first CTDA plus the consecutive CTDA/CIS1/CIS2 subrecords after it; the first other subrecord ends it. Previously every later CTDA extended the erased range, so subrecords between separate condition lists (SCEN phases, PACK, INFO, QUST, PERK) were deleted. A CTDA after the run has ended means the scope holds several lists, so the function returns `multiple_lists` and leaves the record untouched (fail closed). Callers scope the write to one list with a begin/end range.

## Radicle/include/fo4/schema/ctda.hpp:CtdaScopeWrite

`multiple_lists` is a refusal. `write_ctda_conditions` maps it to false. `Radicle/src/schema/condition_scopes.cpp:set_record_scoped_conditions` returns `ScopedConditionStatus::multiple_lists` for that refusal; only other results outside replaced/inserted become `unchanged`.

## Radicle/include/fo4/plugin/mutation.hpp:set_record_conditions

Calls `write_ctda_conditions_in` directly and returns `ConditionMutationStatus`: `record_missing`, `multiple_lists`, `unchanged`, or `updated`. The old bool conflated a missing record with a multiple-list refusal. A record with no CTDA list remains an unchanged successful no-op. Only an updated record clears its original-range and decompressed-payload caches and the plugin's original range.

## PluginEditTool/native/src/plugin/RadiclePluginConditions.cpp:RadiclePluginBackend::setConditions

Whole-record multiple-list refusal maps to `ConditionScopeMultipleLists`, matching `RadiclePluginMutation.cpp:RadiclePluginBackend::setConditionsAtPath`. The existing missing-record precheck retains `RecordNotFound`; the defensive `NoConditionList` result now describes only a missing record, never several lists. JSON condition parsing, editor-ID resolution and staging behavior are retained. The touched whole-record method moves into the existing conditions translation unit because `RadiclePluginMutation.cpp` exceeded the enforced 750-line limit; unrelated one-line conditionals remain unchanged.

## PluginEditTool/native/tests/unit/RadiclePluginMutationTests.cpp:makeMultipleConditionListsPlugin

The fixture follows `Radicle/tests/test_condition_scopes.cpp:ConditionScopes.RefusesScopeHoldingSeveralConditionLists`: a QUST reference alias with two CTDA runs separated by ALFA. The whole-record and `Aliases[0].Conditions` backend regressions assert `ConditionScopeMultipleLists`, no pending edit and unchanged file bytes. The whole-record assertion fails before the status fix; the scoped assertion already passes. `Radicle/tests/test_ctda_model.cpp:make_plugin_with_layout` assigns distinct function IDs to CTDA entries so unchanged sibling lists can be checked without source comments.

## Radicle/include/fo4/plugin/mutation.hpp:set_record_conditions follow-ups

Records with several condition lists remain refused by whole-record writes: QUST top-level conditions alongside conditioned objectives/targets, PACK, PERK and INFO. Current scoped paths cover magic-effect entries and quest aliases. Additional scoped paths remain follow-ups.

## Radicle/src/schema/ctda.cpp:read_ctda_conditions_in / Radicle/include/fo4/schema/ctda.hpp:CtdaMultipleListsError

The reader uses the same contiguous CTDA/CIS boundary as the writer. A later CTDA after another subrecord closes the first run throws `CtdaMultipleListsError`; no flattened vector is returned. CIS strings after a separator are not attached to the preceding condition; their raw subrecords remain untouched, outside the run the writer replaces. `CtdaModel.ReadDoesNotAttachCisAfterTheRunEnds` reproduces the old incorrect attachment. Whole-record reads and bounded reads share this check. One run and empty records keep their existing behavior, and callers can read separate runs with separate ranges. This changes the C++ read behavior without changing its function signatures or any C ABI export/layout.

## Radicle/src/schema/condition_scopes.cpp:read_scoped_conditions / PluginEditTool/native/src/plugin/RadiclePluginConditions.cpp:RadiclePluginBackend::getConditions / getConditionsAtPath

Scoped reads translate the shared multiple-list exception into the existing `ScopedConditionStatus::multiple_lists` with no condition values. Both PluginTool read routes report `ConditionScopeMultipleLists`, matching their writer routes. The existing two-list alias fixture reproduces success/flattening on unchanged main; native and backend regressions require refusal, unchanged bytes and no pending edit. The separate-run preservation assertions in `CtdaModel.ScopedWriteLeavesOtherListUntouched` and `ConditionScopes.SetAliasConditionsLeavesSiblingsAndDialogConditionsIntact` now use bounded reads rather than relying on the old flattening behavior.

The caller search across Radicle and PluginEditTool found the backend and native tests only. No C ABI source/header exposes this setter; its C++ result change does not change the C ABI or ABI version.

## Radicle/include/fo4/schema/ctda.hpp:k_ctda_event_function_index — GetEventData is function 576 (GT-198)

GetEventData is CTDA function 576 on disk: xEdit `Core/wbDefinitionsFO4.pas:554` @93cc0bc5 lists `(Index: 576; Name: 'GetEventData')`. Every CTDA is one fixed 32-byte `wbStructSK(CTDA` (`wbDefinitionsFO4.pas:5186`), with no shorter variant. Offset 10 is `wbUnused(2)` (`wbDefinitionsFO4.pas:5194`), so `CtdaCondition::unknown2` keeps those bytes as opaque padding. The vanilla corpus scan (Fallout4.esm and the official DLC masters, 2026-10-11, output not committed) found 335 CTDA rows with function 576 and none with 4672. Mutagen's `EventFunctionIndex = 4672` (`PluginEditTool/Mutagen/Mutagen.Bethesda.Fallout4/Records/Common Subrecords/Condition.cs`) does not match disk data.

The 28-byte GetEventData branch in `Radicle/src/schema/ctda.cpp:parse_ctda_condition` and `serialize_ctda_condition` is removed. With the constant at 576 it would cut a real 32-byte row to 28 bytes and move run-on, reference and unknown3 four bytes early on rewrite. Every CTDA now takes exactly 32 bytes, and a shorter payload throws `std::out_of_range`. Tests: `Radicle/tests/test_ctda_model.cpp:CtdaModel.GetEventDataLayoutParsesAndSerializes` (32-byte function 576 round trip) and `CtdaModel.FunctionFourThousandSixHundredSeventyTwoIsNotGetEventData`. The native quest GetEventData refusals match function 576 in `PluginEditTool/native/src/plugin/RadiclePluginQuest.cpp:parseQuestCondition` and `adapterConditionUnrepresentable`.

The C# path reads the same bytes through Mutagen and keeps 576 as it is. `PluginEditTool/Mutagen/Mutagen.Bethesda.Fallout4/Records/Common Subrecords/Condition.cs:ConditionBinaryCreateTranslation` selects its `GetEventData` layout only when the raw u16 at payload offset 8 equals `EventFunctionIndex = 4672`, a value no vanilla row uses, and that 28-byte layout (Member at 10, Parameter3 at 12, RunOn at 16) is left unchanged. A vanilla 576 row falls through to `FunctionConditionData` in `ConditionFloatBinaryCreateTranslation` and `ConditionGlobalBinaryCreateTranslation`, which reads the xEdit 32-byte layout: Unknown2 at 10, Int32 parameters at 12 and 16, RunOn at 20, Reference at 24, Unknown3 at 28. Parameter Two is typed as a form by `ParameterTypeMixIn.GetCategory` (EventData), and the vanilla values include master-qualified IDs such as `0100FD17` (DLCRobot.esm). Changing the Mutagen constant to 576 would send vanilla rows to the 28-byte layout, so it must not change. Tests: `PluginEditTool/FO4RecordEditor.Tests/WriteToolsTests.cs:WriteToolsTests.SetConditions_GetEventDataIsFunction576AndReadsAsFunctionData`. Neither C# test was run in this change (no local build).

## PluginEditTool/native/src/plugin/ConditionCodec.cpp:describeCondition — GetEventData param1/param2 (GT-198)

Event-data rows (function 576) carry param1 and param2 as raw int32 values: xEdit `Core/wbDefinitionsFO4.pas:554` @93cc0bc5 types them as ptEvent and ptEventData, and the Parameter #3 Event Data decider is `itS32` (`wbDefinitionsFO4.pas:5223`). They are not form IDs, so `describeCondition` emits them as plain JSON integers, non-event rows are typed per function (see `docs/code-notes/pet-condition-codec.md`). `buildCondition` stores the JSON integers unchanged, so the round trip preserves them. The regression is `PluginEditTool/native/tests/unit/ConditionCodecTests.cpp` (`condition codec: GetEventData rows keep param1 and param2 through describe and build`): the old event-data gate omitted both fields, so a GetConditions then SetConditions round trip wrote them back as 0.
