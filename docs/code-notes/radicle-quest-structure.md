# Stored quest structure (#423)

## Radicle/include/fo4/quest/structure.hpp:QuestStructure / Radicle/src/quest/structure.cpp:read_quest_structure

The read projection reuses `decode_quest_stages`, `decode_quest_objectives` and `decode_quest_aliases`; it adds no record layout and performs no mutation. Family support flags distinguish an empty array from a failed decoder. The original plugin retains every raw member, including unsupported data. A partial-form override describes only its own stored arrays, never inferred arrays from another plugin.

Conditions remain separate dialogue, story-manager, log-entry, objective-target and alias lists. Stage/log and objective/target references are array ordinals, not potentially repeated on-disk IDs. The top-level `NEXT` boundary is required to assign nonempty quest lists; duplicate, nonempty or missing markers make that scope unsupported. Each bounded CTDA/CIS run is accepted only when `lossless_quest_log_entry_conditions` consumes the entire run and reproduces its bytes. Output is accumulated temporarily; failures, orphan CIS members and multiple runs publish no partial conditions. The shared codec retains the three CTDA padding bytes (`Radicle/include/fo4/schema/ctda.hpp:CtdaCondition::padding`) without assigning them meaning. CTDA scalar slots are raw stored values, not a function-specific interpretation or condition evaluator.

Alias conditions must follow the metadata/fill prefix and precede reference extras or location `ALCC`; an intervening extra makes a later run unsupported. Pinned xEdit lines 10814–10875 and 10917–10945 declare these boundaries. `Radicle/src/quest/structure.cpp:validate_objective_flags` allows at most one objective `FNAM` before the first `QSTA`, matching `source:xEdit Core/wbDefinitionsFO4.pas:10774 @93cc0bc5`, where FNAM has no required flag. An absent FNAM reads as the decoder's zero flags. A duplicate FNAM is refused because the decoder keeps only one value, and a rewrite would drop the other. Any FNAM after a `QSTA` is refused so it cannot become the default zero flags.

## Radicle/include/fo4/c_api_quest.h:radicle_quest_structure_view / Radicle/src/c_api_quest_structure.cpp:radicle_quest_structure_read

The immutable snapshot owns its arrays, text bytes, diagnostic strings and FormKeys independently of the source plugin. `get_view` returns borrowed pointers valid until snapshot destruction and never rebuilds them. Empty arrays use NULL and zero count; optional absent text uses NULL, while present empty text has a non-NULL pointer and zero byte length. Inline text is transferred as raw bytes with lengths; localized references retain string IDs without resolving a language. All first/count ranges select the corresponding flat array. `Radicle/src/c_api_quest_structure.cpp:radicle_quest_structure_read` runs inside `guarded()`, with its own nested catches kept for a missing QUST (`std::out_of_range` to NOT_FOUND), a non-QUST record and a malformed form key (`std::invalid_argument` and `FormIdError` to INVALID_ARGUMENT). Under `guarded()` the first two would map to INTERNAL_ERROR, and a `FormIdError` from the key parse would map to PARSE_ERROR. Read failures initialize the output handle to NULL; view failures zero the output when supplied; destruction is NULL-safe. Unsupported families/lists have explicit support flags plus scoped diagnostics. An unresolved alias link is reported and disables its fill projection, rather than presenting it as a valid null target. No returned pointer may be freed separately.

The new exports require ABI 12 relative to `main` ABI 11. This is provisional until the open ABI-bearing branches are reconciled in merge order. Existing quest VMAD rows and properties retain their behavior. Stored flags have no new runtime meaning; stage execution, startup ordering, alias resolution and condition evaluation remain GardenEngine evidence gates.

`Radicle/tests/test_quest_structure.cpp:OpaqueConditionPrefixesRemainSupportedAndByteExact` and `Radicle/bindings/csharp/Radicle.Interop.Tests/QuestStructureTests.cs:OpaqueConditionPrefixesRemainSupportedAndByteExact` reproduce the pre-integration refusal with nonzero prefix bytes in every condition scope. Native serialization must retain those bytes; the C ABI and managed snapshots must continue exposing their declared condition values. The read-only `radicle_quest_condition` projection has no opaque-prefix field. Preservation belongs to the native condition model and original plugin bytes; this integration adds no editable opaque C ABI field or new layout.
The fill tag determines which fields are meaningful. Other fields are zero/NULL; presence flags distinguish absent optional scalars. `NONE` also covers collections, whose IDs are in `collection_entries`, not the alias row's zero placeholder ID. Reference extras such as packages, spells and inventory remain in the existing native alias model and original subrecords; this read ABI exposes the fields requested by #423, not those extra arrays. Snapshot reads require the source plugin to remain alive and unmodified during the call. Immutable views may be read concurrently while their snapshot remains alive; destruction must not race a read.

| Fill type | `target` | `secondary_target` | Other selected fields |
|---|---|---|---|
| Specific reference / unique actor | Reference / actor | — | — |
| Location alias reference | — | Reference type | `fill_alias` |
| External alias reference / location | Quest | — | `fill_alias` (optional for location) |
| Create reference | Object | — | `fill_alias`, `create_mode`, optional `level` |
| Matching near alias | — | — | `fill_alias`, `near_type` |
| Matching reference event / location event | — | — | `event`, `event_data` |
| Matching closest | — | — | `fill_alias` |
| Specific location | Location | — | — |
| Reference alias location | — | Keyword | `fill_alias` |

## Radicle/src/forms/quest_stages.cpp:decode_quest_stages / Radicle/src/forms/quest_objectives.cpp:decode_quest_objectives / Radicle/src/forms/quest_aliases.cpp:decode_quest_aliases

Evidence baseline: pinned xEdit `93cc0bc5a1251936c3c7859eee3150eda12a62d7`, `Core/wbDefinitionsFO4.pas` lines 10737–10759 separates the two quest condition lists and declares INDX, QSDT, CTDA/CIS, inline NAM2 and localized CNAM. Lines 10772–10804 declare objectives/targets; lines 10805 onward declare alias kinds, fill unions and end markers. The existing native models and `Radicle/docs/FORMAT_NOTES.md`, `QUEST_ALIASES.md` provide corpus and authoring evidence. The later xEdit NAM0 next-quest entry is not promoted into an unverified runtime field; it is reported as unsupported in a log entry.

| Stored field | Status | Evidence / boundary |
|---|---|---|
| INDX index, flag byte and final opaque byte | Partial | Existing native decoder and corpus; xEdit 10741–10749. Runtime flag behavior unverified. |
| QSDT flag byte, CNAM reference/inline bytes, NAM2 note bytes | Partial | Existing native decoder; xEdit 10751–10759. No execution or localization policy inferred. |
| Dialogue/story-manager and scoped CTDA/CIS values | Partial | Existing lossless codec; xEdit 10737–10739, 10756, 10793 and alias definitions. Evaluation and function-specific parameter semantics remain separate. |
| QOBJ index, FNAM flags, NNAM text, QSTA alias/flags/keyword | Partial | Existing native decoder; xEdit 10772–10804. Unsupported historical widths remain explicit. |
| Reference/location alias ID, name, flags, force/closest selectors and fill variants | Partial | Existing native decoder and QUEST_ALIASES evidence; no default target or fill-resolution behavior inferred. |
| Collection alias entries and optional initial-fill count | Partial | Existing native grouping/decoder; no synthetic collection ID invented. |
| Runtime startup, execution timing, condition evaluation and alias resolution | Not found in this projection | No shipping behavior inferred from record definitions. |

Executable verification is tracked in [RE #445](https://git.nomadicinteractive.dev/GARDEN/GARDEN-TOOLS/issues/445), including stored loader widths/branches and runtime behavior on OG and AE. These fields remain Partial; this projection does not upgrade xEdit or corpus evidence to executable verification.

## Radicle/tests/test_quest_structure_real.cpp:QuestStructureReal / MQ101

The corpus test covers every physical QUST across all seven configured official masters, including overrides, and reports decoded versus unsupported items for MQ101. It checks family-bounded raw marker counts, native/C value agreement and untouched serialization/reparse byte identity. These checks address the stored read surface; they do not prove in-game execution. Configured missing masters fail; only an unset `FO4_DATA_DIR` permits corpus skips.

`Radicle/tests/test_quest_structure.cpp:AliasConditionsAfterExtrasAreUnsupported` and `ObjectiveFlagsAfterTargetsDoNotBecomeDefaultFlags` fail on the initial projection and pass with the ordering guards. The focused run passes all 11 cases, including every fill tag and snapshot lifetime; the managed suite passes 82 cases. Corpus and required CI results belong to the PR's exact-head validation report.

`Radicle/tests/test_quest_structure_real.cpp:EveryStoredQuestAgreesAndRoundTrips` passes on all seven official masters after integrating the shared condition preservation changes: 1,916 quests, 13,071 stages, 13,619 logs, 1,924 objectives, 2,338 targets, 17,269 aliases and 15,068 conditions, with zero unsupported lists, failed families or skips. MQ101 exposes all 18 stored conditions, compared with 10 and eight unsupported runs before integration. The focused run now passes 12 synthetic and seven corpus cases; managed tests pass 83 cases without skips. This remains stored-data acceptance; executable verification in #445 is still required.
