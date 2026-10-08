# Radicle CTDA contiguous run

## Radicle/include/fo4/schema/ctda.hpp:CtdaCondition / Radicle/src/schema/ctda.cpp:parse_ctda_condition / serialize_ctda_condition

The three bytes following the type byte are opaque stored bytes, not new semantic fields. Pinned xEdit `93cc0bc5a1251936c3c7859eee3150eda12a62d7`, `Core/wbDefinitionsFO4.pas:5186-5188`, declares `wbUnused(3)`. The official MQ101 record has nonzero values there in eight log-entry CTDA payloads; for example, stage ordinal 0/log 0 contains `00ac9313000000004a000000317d0a00000000000000000000000000ffffffff`. Dropping those bytes makes the existing lossless log-entry check reject otherwise supported payloads. Retain them opaquely through native parse/serialize and native scalar edits; new conditions default to zero. No runtime meaning is assigned. Existing C ABI layouts and PluginTool JSON fields are unchanged; JSON replacement does not transfer opaque bytes absent from its input. Truncated, oversized or malformed runs remain subject to the existing lossless refusal checks. The measured counterexample and remaining loader/runtime evidence are tracked in RE #445.

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
