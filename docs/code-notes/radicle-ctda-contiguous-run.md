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

Records with several condition lists remain refused by whole-record writes: QUST top-level conditions alongside conditioned objectives/targets, PACK, PERK and INFO. Current scoped paths cover magic-effect entries and quest aliases. `Radicle/src/schema/ctda.cpp:read_ctda_conditions` still flattens all lists; a read-side multiple-list result and additional scoped paths are follow-ups, outside this change.

The caller search across Radicle and PluginEditTool found the backend and native tests only. No C ABI source/header exposes this setter; its C++ result change does not change the C ABI or ABI version.

## PluginEditTool/native/src/plugin/ConditionCodec.cpp:buildCondition — reference needs Run On Reference

xEdit reads a condition's Reference field only when Run On is Reference (2) (`source:xEdit Core/wbDefinitionsCommon.pas:5495-5509 @93cc0bc5`), and `wbConditionRunOnAfterSet` zeroes it on any other Run On (`source:xEdit Core/wbDefinitionsCommon.pas:1310-1322 @93cc0bc5`). `buildCondition` therefore refuses a nonzero reference on any other run-on with `reference '<text>' requires runOn Reference`, before anything is staged. A zero reference is stored as 0. Covered by `PluginEditTool/native/tests/unit/ConditionCodecValidationTests.cpp` and the `ConditionBackendTests.cpp` fixture, which now uses Reference for its reference entry. The quest-path condition parser (`RadiclePluginQuest.cpp`) has its own reference handling and is not changed here.

## PluginEditTool/native/src/plugin/RadiclePluginMutation.cpp:copyAsOverrideLocked — Initially Disabled is per signature

The 0x800 flag is defined only for REFR, ACHR, NAVM and the placed-object set PARW, PBAR, PBEA, PCON, PFLA, PGRE, PHZD, PMIS (`source:xEdit Core/wbDefinitionsFO4.pas:3206 @93cc0bc5` for `ReferenceRecord`, `:4487` for ACHR, `:7214` for NAVM, and the REFR `wbRefRecord` blocks from `:11347`). The gate sits after the source-record lookup and before any LOSES, EXISTS or copy step, so a refused disabled copy writes nothing. `RadiclePluginDisabledOverride.cpp:copyAsDisabledOverride` inherits it. xEdit offers the action only from Referenced By on placed refs (`source:xEdit xEdit/xeMainForm.pas:9370 @93cc0bc5`, `:15758 @93cc0bc5`). Covered by `PluginEditTool/native/tests/unit/RadiclePluginAddMastersTests.cpp` (WEAP refused with the subsequent plain copy succeeding, REFR flagged). The web menu item in `PluginEditTool/web/src/RecordContextMenu.tsx:RecordContextMenu` is shown only when `matrix.Type` is in the same set.
