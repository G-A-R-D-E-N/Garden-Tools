# Condition JSON preservation

## PluginEditTool/native/src/plugin/ConditionCodec.cpp:describeCondition / buildCondition

The existing display JSON omits opaque native bytes, unknown scalar slots and CIS strings. Rebuilding that JSON therefore changes even an unchanged condition. A synthetic probe at b6f3e918 reproduces the loss through whole-record and scoped Get/Set/Save operations and compares saved raw CTDA payloads. Each displayed row needs validated native preservation data; rebuild from that seed and apply only supported changed fields. Metadata stays on the row so the existing editor's spread, reorder and stringify operations keep it attached. Preserve unknown codes and comparison bits when unchanged, and refuse unsupported edits rather than guessing their meaning.

## Radicle/src/schema/ctda.cpp:read_ctda_conditions_in

Editable reads must compare the source CTDA/CIS run with the existing serializer before returning values. Oversized payloads and strings the native model would normalize cannot enter an editable round-trip. The same shared boundary serves whole-record and scoped reads; additional ownership rules and new event layouts are outside this change.

## PluginEditTool/native/src/plugin/RadiclePluginConditions.cpp:setConditions / PluginEditTool/native/src/plugin/RadiclePluginMutation.cpp:setConditionsAtPath

An invalid row refuses the whole replacement before staging. Skipping a refused row and replacing the list would delete that row. `ConditionCodec.cpp:buildConditions` validates preservation metadata against unused source rows before building the replacement; canonical but forged bytes and duplicate row identities also refuse. Reordering and deleting existing rows remain supported. New rows use strict field validation, and existing unknown-function or event rows support unchanged preservation only. The C ABI is unchanged.

## PluginEditTool/web/src/ConditionsEditor.tsx:patch / PluginEditTool/native/src/plugin/ConditionCodec.cpp:buildCondition

`_native` carries a byte-integer CTDA array and nullable CIS strings on each row. `_edited` records fields explicitly changed by the editor. Browser `JSON.stringify` normalizes negative zero to zero, so semantic equality alone preserves the original raw bits on an unchanged save; an explicit value edit must override that equality to allow positive zero. Non-finite stored values similarly survive the JSON null projection while explicit non-finite edits refuse. Neither metadata nor edit intent adds native layout assumptions.

## PluginEditTool/native/tests/unit/ConditionPreservationBackendTests.cpp / PluginEditTool/native/tests/unit/ConditionCodecTests.cpp

Synthetic regressions compare saved raw CTDA and CIS payloads through both routes, supported scalar edits and reordering, and no pending edit on malformed or forged preservation data and unsupported rows. They also cover browser zero normalization and explicit positive-zero editing. Native read guards compare raw runs to serializer output and refuse payloads that cannot round-trip. These proofs establish byte preservation, not runtime interpretation of opaque fields.

## PluginEditTool/native/src/plugin/RadiclePluginConditions.cpp:RadiclePluginBackend::getConditions

An unnamed lookup must retain the candidate plugin that supplied the record, matching the scoped route. Using the first loaded plugin to format the matched record's condition links loses or misidentifies their owners when the plugins have different master contexts. A two-plugin probe at aa57f54e reproduces this: B.esp owns the matched record and masters A.esp; named and scoped reads preserve the B.esp owner while the unnamed whole-record read drops it. `PluginEditTool/native/tests/unit/ConditionOwnerContextTests.cpp` covers qualified-key and editor-ID lookup, local and master targets, and parameter/reference/comparison-global projections. The fixture proves context selection, not semantic validity of synthetic condition targets. Record selection order, native payloads and the C ABI are unchanged.

## PluginEditTool/native/src/plugin/RadiclePluginQuestCondition.cpp:adapterConditionUnrepresentable

The separate whole-stage and whole-alias replacement adapters construct condition rows from their supported JSON fields and cannot carry opaque prefix bytes. Their shared preflight must refuse nonzero prefixes alongside the existing unknown-field refusals. After the native codec began preserving these bytes, its lossless read check alone no longer protected those adapters: a probe at 239427cb rewrote `12 34 56` to `00 00 00` while submitting unchanged visible condition values. `PluginEditTool/native/tests/unit/QuestOpaqueConditionTests.cpp` checks both routes refuse with `QuestAuthoringCoverageIncomplete`, preserving bytes even after a subsequent save. This guard does not restrict read-only quest snapshots or the separate condition editor, which retains its native seed. Proofs are `Radicle/build/quest-opaque-registered-red.log` and `Radicle/build/quest-opaque-green.log`.
