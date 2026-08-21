# Quest authoring parity notes

This document records the verified Mutagen model mapping for issues #135, #136, and #137 before changing the JSON authoring surface. It is intentionally based on the vendored Fallout 4 generated types rather than inferred Skyrim behavior.

## Current tool limits

`SetQuestAliases` currently always creates `QuestReferenceAlias` and only fills a small subset of its modeled fields. `SetQuestStages` accepts one `logEntry` string per stage. `SetQuestObjectives` never creates `QuestObjectiveTarget` entries.

The model itself already exposes the missing structures, so these three issues belong in `WriteService.Authoring.cs` and the tool schema rather than in Mutagen generation.

## Alias model (#135)

The quest alias union has three concrete arms:

- `QuestReferenceAlias`
- `QuestLocationAlias`
- `QuestCollectionAlias`

The new JSON contract should use `kind: reference|location|collection`, with `reference` remaining the default so existing callers keep working.

### Reference aliases

Verified modeled fields include:

- `Name`, `ID`, `Flags`
- `ForcedReference`
- `UniqueActor`
- `CreateReferenceToObject`
- `Location`
- `External`
- `VTCK`
- `Conditions`
- `Keywords`
- `LocationAlias`
- `Collection`
- `LinkedAliases`
- `FindMatchingRefNearAlias`
- `FindMatchingRefFromEvent`
- package and other alias-specific lists exposed by the generated model

Supporting structs verified in the vendored model:

- `LocationAliasReference`: `Alias`, `RefType`
- `ExternalAliasReference`: `Quest`, `Alias`
- `CreateReferenceToObject`: `Object`, `AliasID`, `Level`, `Flags`
- `FindMatchingRefNearAlias`: `AliasID`, `Type`, `FormKey`
- `FindMatchingRefFromEvent`: `FromEvent`, `EventData`

### Location aliases

Verified modeled fields include `Name`, `ID`, `Flags`, `SpecificLocation`, `ReferenceAliasLocation`, `ExternalAliasLocation`, and `Conditions`.

### Collection aliases

`QuestCollectionAlias.Collection` is an `ExtendedList<CollectionAlias>`. Each `CollectionAlias` currently exposes `AliasID`; any additional count field requested by an older issue description must be verified against the vendored generated type before being advertised.

## Stage/log-entry model (#136)

`QuestStage.LogEntries` is a real list. `QuestLogEntry` exposes:

- `Entry`
- `Flags`
- `Conditions`
- `Note`
- `NextQuest`

`QuestLogEntry.Flag` contains both `CompleteQuest` and `FailQuest`.

Backward compatibility requirement: keep accepting the current stage-level `logEntry` string as sugar for one entry while adding `logEntries: []` for the full model.

Each new log-entry object should support text, flags or complete/fail booleans, note, next quest, and conditions.

## Objective target model (#137)

`QuestObjectiveTarget` exposes:

- `AliasID`
- `Flags`
- `Conditions`
- `Keyword`
- `QSTADataTypeState`

The last field is format-significant. The generated binary translation defines only one state flag, `QSTADataType.Break0`, and writes the keyword only when that flag is not set:

- `QSTADataType.Break0` set: 8-byte QSTA form without a keyword
- default state value `0`: 12-byte QSTA form with `Keyword`

There is no `QSTADataType.Break4` value in the generated Fallout 4 model. Therefore the authoring layer must leave `QSTADataTypeState` at `0` whenever a keyword is supplied and set `Break0` for the short form without a keyword. Setting a nonexistent `Break4` value would not compile, while setting `Break0` on a keyword-bearing target would serialize the short form and omit the keyword.

The target JSON contract should accept `alias`, optional `flags`, optional `keyword`, and optional `conditions`.

## Implementation order

1. Extend objectives first because the QSTA binary-state rule is self-contained and high-risk if missed.
2. Extend stage log entries while preserving the old `logEntry` shorthand.
3. Extend the alias discriminator and common flags/conditions, then add the subtype-specific structures.
4. Update the MCP descriptions only after the core writer accepts the corresponding fields.
5. Add round-trip tests against serialized QUST records, with an explicit keyword-bearing QSTA test to prove the 12-byte form survives save/reload.

## Validation state

The source-policy regression from `5e26386` was repaired by merged PR #167. Current `main` at `54202f5` passes source policy, portable Core/web/server validation, the Windows-targeted compile gate, and Wine/MSVC compile; real Windows integration remains skipped unless explicitly enabled. This branch still contains documentation only, so #135, #136, and #137 remain open until writer changes and serialized QUST regressions are implemented and validated.
