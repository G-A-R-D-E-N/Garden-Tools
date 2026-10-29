# Dialogue writer (DIAL, INFO, SCEN)

## Radicle/src/plugin/field_writer.cpp:response_shape_valid (review fixes)

Repeated INFO field counts and edits refuse unless every 20-byte TRDA is immediately followed by NAM1,
NAM2, NAM3 and NAM4, with equal counts across all five signatures. This preserves response indexes on
malformed records and leaves their bytes untouched. `Radicle/tests/test_dialogue_writer.cpp:MalformedResponseGroupsRefuseScalarStringAndLinkEditsWithoutChangingBytes`
covers missing, reordered, stray and short members, while top-level INFO edits remain available.
`Radicle/src/schema/preflight.cpp:preflight_record` takes the plugin's localized flag; plugin preflight,
authoring preflight and `Radicle/src/plugin/subrecord_authoring.cpp:layout_valid` pass it explicitly.
Four-byte inline text is still validated in nonlocalized plugins. `Radicle/src/plugin/subrecord_authoring.cpp:create_with_rules`
refuses unsupported placement even with explicit rules. `Radicle/src/plugin/record_edit.hpp:locate_mutable_record`
shares the existing lookup/reassign fallback used by scene and subrecord writers.
`Radicle/include/fo4/flags.hpp:GroupType::quest_children` names the existing type-10 placement.
`Radicle/tests/test_dialogue_real.cpp:EveryOfficialDialogueAndSceneDecodesAndMasterIsByteIdentical` checks ENAM
presence for every official action through native and C ABI decoders.

Source for every layout, order, default and required flag: pinned TES5Edit `wbDefinitionsFO4.pas` at
`93cc0bc5a1251936c3c7859eee3150eda12a62d7` (DIAL 6309-6475,
SCEN 8879-9133, INFO 9672-9786) and `wbDefinitionsCommon.pas` (`wbINOM`/`wbINOA` 8604-8624). Official measurements are
over the seven shipped masters: 46,427 DIAL, 102,462 INFO, 4,778 SCEN (DLCworkshop02.esm has none).

## Radicle/include/fo4/c_api.h:RADICLE_ABI_VERSION (W0 merge)

- origin/nomad/radicle-subrecord-authoring-20261005 (9cf04f75) is integrated onto #406 (af38be4b).
  Conflicts: `Radicle/CMakeLists.txt` (`radicle_c` keeps `src/c_api_dialogue.cpp` and gains `src/c_api_authoring.cpp`),
  `Radicle/bindings/csharp/tools/generate_interop.py:HEADERS` (union: `fo4/c_api_dialogue.h` and `fo4/c_api_authoring.h`),
  `c_api.h` (ABI), `Radicle/tests/test_c_api_quest.cpp` (ABI pin) and the generated `RadicleNative.g.cs`. Every generated
  file (scalar, record, lstring, FormLink and condition-scope tables, `RadicleNative.g.cs`, `abi_layout_probe.cpp`) was
  regenerated with its tool.
- ABI 33 advances ABI 32 (main with #402 and #408) by one; `Radicle/tests/test_c_api_quest.cpp:AbiVersionIsThirtyThree` pins it.
  The bump also covers the three scene-writer exports below. Re-bump if another additive ABI PR merges first.

## Radicle/src/environment/scene_layout.cpp:scene_layout (reader fix)

- An action without ENAM is valid in xEdit (`wbInteger(ENAM, 'End Phase', itU32)` at line 9011 is not required) but
  `decode_scene` rejected it, because the header ended at the first ENAM. The action header is now parsed the way xEdit
  parses the ordered `Action` struct: after the two-byte ANAM opener, consume NAM0, ALID, INAM, FNAM, SNAM, ENAM while each
  next subrecord is a later member of that list (`action_header_end`); the type-specific payload starts at the first
  subrecord that is not. A missing ENAM is exposed as an absent end phase (`SceneAction::end_phase`, C `has_end_phase`,
  C# `EndPhase == null`). A timer action without ENAM keeps its payload SNAM float as payload, because SNAM may not repeat
  in the header; an action with neither header SNAM nor ENAM still takes a timer SNAM as the start phase, as xEdit would.
- Official evidence: all 24,983 official actions carry ENAM, and on every one the new header end equals the old
  "first ENAM + 1" end (checked with a byte-level survey and by `Radicle/tests/test_dialogue_real.cpp` decoding every
  record again), so official decode results are unchanged. Header patterns: 13,662 `NAM0 ALID INAM FNAM SNAM ENAM` and
  11,321 `NAM0 ALID INAM SNAM ENAM`.
- `scene_layout` is the single structural parser: `Radicle/src/environment/dialogue.cpp:decode_scene` and
  `Radicle/src/plugin/scene_writer.cpp` both use it, so the writer addresses exactly the groups the reader exposes. Errors
  carry the offending tag (`SceneLayoutError`) and the decoder turns them into the same `WorldDecodeError` messages.
- Start and end phase stay u32 in every layer (`SceneAction`, `radicle_scene_action`, C# `SceneAction`, and the writer's
  `SCEN\Action\StartPhase`/`EndPhase` descriptors).
- Tests: `Radicle/tests/test_dialogue.cpp:DialogueRead.ActionWithoutEndPhaseFollowsTheXEditHeaderOrder`,
  `Radicle/tests/test_dialogue_writer_c_api.cpp:DialogueWriterCApi.FieldSceneAndAuthoringStatusesMapAndSurviveReload`
  (`has_end_phase == 0`), `Radicle/bindings/csharp/Radicle.Interop.Tests/DialogueAuthoringTests.cs:SceneTypedWriterUsesExplicitGroupIndexes`.

## Radicle/src/environment/dialogue.cpp:decode_dialogue_topic, decode_dialogue_info (reader comparison)

- DIAL matches xEdit field by field (DATA u8/u8/u16, PNAM f32, BNAM/QNAM/KNAM links, SNAM four bytes, TIFC u32). xEdit's
  trailing `wbINOM`/`wbINOA` are `dfDontSave` internal arrays and never stored; no official DIAL has them.
- INFO matches xEdit for the fields it decodes. A response scope ends at the next TRDA, CTDA or RNAM; when an INFO has
  neither CTDA nor RNAM the last scope also covers the record tail, but no tail signature (ANAM, TSCE, INTV, ALFA, ONAM,
  GREE, TIQS, NAM0, INCC, MODQ, INAM) is a response member, so nothing is misread. No change.
- SCEN phases and actors match xEdit (phase FNAM u16, SCQS i16 pair, WNAM u32; actor ALID i32, LNAM u32, DNAM u32). The
  only disagreement was the ENAM-less action above.

## Radicle/schema/fallout4/fields.json:DIAL, INFO, SCEN (registered fields)

All `existing_only`, `presentation: identity`, f32 bounds +-1e9, enum bounds only where xEdit's enum is contiguous.

- DIAL: string `Name` (FULL, localized); `Priority` (PNAM f32), `TopicFlags` (DATA u8 @0), `Category` (DATA u8 @1, 0..7,
  xEdit enum Player..Miscellaneous; official masters use all eight values 0-7, with 1 on 20 topics and 6 on 2), `Subtype` (DATA u16 @2, raw: the enum has 125
  entries with gaps of meaning, official max 124), `SubtypeName` (SNAM as a raw u32, see below), all `payload_size` 4.
  Links `Branch` (BNAM, DLBR), `Quest` (QNAM, QUST), `Keyword` (KNAM, KYWD), none nullable as in xEdit. Official targets:
  1,672 DLBR, 46,427 QUST, 1,588 KYWD.
- DIAL SNAM is `wbString(SNAM, 'Subtype Name', 4)`, a four-character code that pairs with the DATA subtype (41,717 `SCEN`,
  1,619 `CUST`, 480 `GREE`, ...). It is writable as a raw u32 so a caller who changes `Subtype` can keep the code in step;
  Radicle does not derive one from the other (xEdit does not either). TIFC (`cpIgnore` info count, line 6472) is not
  registered: it is derived from the topic's INFO children and stays read-only.
- INFO top level: `Flags` and `ResetHours` (ENAM u16 @0/@2, reset hours raw, xEdit displays raw / 2730.625), string
  `OverrideFileName` (IOVR), localized string `Prompt` (RNAM), `ForcedAlias` (ALFA i32), `GreetDistance` (GREE u32),
  `ParentQuestStageOnBegin`/`OnEnd` (TIQS u16 @0/@2), string `StartScenePhase` (NAM0), `Challenge` (INCC u32, 0..7),
  `SubtitlePriority` (INAM u32, 0..3; official values 0, 1, 3). Links `PreviousTopic` (TPIC, DIAL), `PreviousInfo` (PNAM,
  INFO, nullable: `[INFO,NULL]`), `SharedInfo` and `InfoGroup` (DNAM/GNAM, INFO), `Speaker` (ANAM, NPC_), `StartScene`
  (TSCE, SCEN), `AudioOutputOverride` (ONAM, SOPM), `ResetGlobal` (MODQ, GLOB). Every one of these signatures occurs only at
  the top level of INFO, so the unique (non-repeated) addressing is unambiguous. Official targets all match (24,375 PNAM:
  10,360 NULL, 14,013 INFO and 2 dangling, see the check note below).
- INFO responses: `ResponseNumber` (TRDA u8 @4), `ResponseUnknown` (u8 @9), `InterruptPercentage` (u16 @10),
  `CameraTargetAlias`/`CameraLocationAlias` (i32 @12/@16), all `payload_size` 20 and `repeated`; strings `ResponseText`
  (NAM1, localized), `ScriptNotes` (NAM2), `Edits` (NAM3), `AlternateLipText` (NAM4), `repeated`; links `Emotion` (TRDA @0,
  KYWD) and `SoundFile` (TRDA @5, SNDR, nullable), `repeated`.
- Occurrence index equals response index for TRDA and NAM1-NAM4 only. Evidence: every one of the 96,768 official TRDA is
  20 bytes and is immediately followed by NAM1, NAM2, NAM3, NAM4 in that order, and each INFO has equal TRDA/NAM1-4 counts;
  none of those signatures occurs outside a response. `Radicle/tests/test_dialogue_writer_real.cpp:responses_are_complete`
  re-checks this on every official INFO. `Radicle/src/plugin/field_writer.cpp:response_shape_valid` validates the
  group structure before repeated field counts or writes, preventing index drift on malformed third-party INFO.
- SoundFile sits at the unaligned offset 5. W0 slots are byte offsets and the encode is a `memcpy`, so no alignment is
  assumed; `Radicle/src/schema/form_links.cpp` already visits TRDA 0 and 5 for remapping and `check_errors.cpp:slot_targets`
  matches offset 5. Test: `Radicle/tests/test_dialogue_writer.cpp:ResponsesAddressByResponseIndexIncludingTheUnalignedSoundFile`
  checks bytes 4 and 9 around it stay untouched.
- Emotion is `[KYWD,FFFF]` in xEdit: 55,138 official values are keywords and 41,630 are the 0xFFFFFFFF none sentinel; none
  is NULL, so the link is not nullable. The writer can replace the sentinel with a keyword but cannot write the sentinel
  back (it is not a form key); restoring "none" is OPAQUE.
- SCEN (only signatures no phase, actor or action reuses): `DeathBehavior`, `CombatBehavior`, `PlayerDialogueBehavior`,
  `ObserveCombatBehavior` (VNAM u32 @0/4/8/12, `payload_size` 16; raw, because official VNAM also hold 0xFFFFFFFF),
  `CameraDistanceOverride` (CNAM f32), `DialogueDistanceOverride` (ACTV f32), string `Notes` (NNAM), `Index` (XNAM u32).
  Every other SCEN field goes through the typed writer below.

## Radicle/src/plugin/field_writer.cpp:set_record_formlink_field (W0 change)

The "unchanged" check now compares the stored raw value with the encoded target instead of resolving the stored value
first. Resolving threw `FormIdError` for 0xFFFFFFFF (index 0xFF), so every official TRDA emotion holding the none sentinel
refused any edit. The raw comparison is equivalent for every resolvable value (encode and resolve are inverse within one
master table) and removes the throw; target validation is unchanged.

## Radicle/src/schema/preflight.cpp:preflight_record (W0 change)

Localized string IDs are validated using the owning plugin flag passed explicitly to `preflight_record`, rather than inferred from payload width. Four-byte inline strings remain subject to inline validation in nonlocalized plugins. The writer refuses inline edits on localized fields in localized plugins. Regressions: `Radicle/tests/test_dialogue_writer.cpp:PreflightUsesThePluginLocalizedFlagInsteadOfGuessingFromFourBytePayloads` and `Radicle/tests/test_dialogue_subrecords.cpp:LocalizedTopicCannotAddAnInlineName`.

## Radicle/include/fo4/schema/record_constraints.hpp:RecordPlacement (W0 change)

- New placements `quest_children` and `topic_children` (`Radicle/tools/record_schema_codegen.py:PLACEMENTS`). Official
  placement: all DIAL and SCEN are direct children of a GRUP type 10 (quest children, labelled with the quest), all INFO
  of a GRUP type 7 (topic children); no official master has a top-level DIAL, INFO or SCEN.
- `Radicle/src/schema/preflight.cpp:preflight_authoring_record` reports `record_placement_invalid` when the parent group
  type differs (`RecordLocation::parent_group_type`). `Radicle/src/plugin/subrecord_authoring.cpp:create_with_rules`
  returns `not_authorable` for any generated schema whose placement is not `top_level`, because
  `Radicle/src/plugin/mutation.cpp:create_local_record` only appends to a top-level group, a location no official dialogue
  record uses. Creating under a chosen quest or topic is not implemented.
- Tests: `Radicle/tests/test_dialogue_subrecords.cpp:PlacementOutsideQuestAndTopicChildrenIsReported`,
  `CreateIsRefusedBecauseDialogueRecordsLiveUnderQuestsAndTopics`.

## Radicle/schema/fallout4/fields.json:DIAL, INFO, SCEN authoring

- DIAL (quest_children): EDID, FULL (default empty string, never used in a localized plugin), PNAM (required, default
  `00004842` = 50.0, xEdit `SetDefaultNativeValue(50)` line 6316; 46,416 of 46,427 official values are 50), BNAM, QNAM
  (`coupled`: the quest must match the parent quest-children group, so removing or adding it alone would orphan the
  topic), KNAM, DATA (default `00000000`, the zeroed struct: no flags, Player, Custom), SNAM (no default: a zero code is not
  a valid subtype name), TIFC (`coupled`, derived count). BNAM/KNAM have no default (targets exclude NULL).
- INFO (topic_children): EDID, VMAD, ENAM (default zeroed), TPIC, PNAM (default NULL, allowed by `[INFO,NULL]`), DNAM, GNAM,
  IOVR (default empty string), CTDA (counted, not authorable), RNAM (default empty string, localized rule as FULL), ANAM,
  TSCE, INTV (`wbUnknown`, no default), ALFA (no default: 0 is a real alias), ONAM, GREE (no default), TIQS (no default:
  official values use 0xFFFF for "none", which xEdit does not declare), NAM0 (default empty string), INCC (default 0, the
  enum's `None`), MODQ, INAM (no default: 95,522 of 101,699 official values are 1, so zero is not the editor default).
  Response members and CIS1/CIS2 are deliberately unlisted: several responses or conditions interleave, which a linear
  order cannot express. 87,964 official INFO carry them, so add on those is `unknown_order`; remove of listed singles
  works (no listed signature occurs inside a response, so occurrence 0 is the top-level one).
- SCEN (quest_children): EDID, VMAD, VNAM (default sixteen zero bytes, every member's enum value 0 `Set All Normal`), CNAM,
  ACTV (no default: their presence is the override), KSIZ, KWDA (both `coupled`), NNAM (default empty string), XNAM
  (default 0; 3,227 of 4,711 official values). FNAM, PNAM, INAM, CRIS, CTDA, SCQS and TNAM are unlisted because phases or
  actions reuse them: listing them makes the flat order and the counts fail on 4,746 of 4,778 official scenes (FNAM 2,311,
  INAM 1,227, PNAM 1,054, CTDA 154 as first violation), and a remove by occurrence could hit a group member. Every official
  SCEN has PNAM and groups, so add on SCEN is always `unknown_order`; remove of VNAM, CNAM, ACTV, NNAM, XNAM, VMAD works.
- Order check: `Radicle/tests/test_dialogue_writer_real.cpp:EveryOfficialRecordFollowsTheAuthoringOrderPlacementAndResponseShape`
  checks placement, order and counts of every official DIAL, INFO and SCEN: 0 violators. Records carrying unlisted
  subrecords: DIAL 0; INFO 68,441 + 2,376 + 2 + 8,688 + 859 + 7,598 (responses and condition strings); SCEN all 4,778.

## Radicle/src/plugin/scene_writer.cpp:set_scene_scalar, set_scene_formlink, scene_group_count (typed writer)

- Structure-aware SCEN writer: the path names the scope and the index the group. Scene level (index 0): `SCEN\Flags`
  (prelude FNAM u32), `SCEN\LastActionIndex` (INAM u32), `SCEN\FovOverride` (four-byte CRIS f32),
  `SCEN\ParentQuestStageOnBegin`/`OnEnd` (SCQS i16 @0/@2). Phase: `SCEN\Phase\EditorWidth` (WNAM u32), `SCEN\Phase\Flags`
  (FNAM u16), `SCEN\Phase\QuestStageOnStart`/`OnCompletion` (SCQS i16). Actor: `SCEN\Actor\Alias` (ALID i32),
  `SCEN\Actor\Flags` (LNAM u32), `SCEN\Actor\BehaviorFlags` (DNAM u32). Action header: `SCEN\Action\ActorAlias` (ALID i32),
  `SCEN\Action\Index` (INAM u32), `SCEN\Action\Flags` (FNAM u32), `SCEN\Action\StartPhase`/`EndPhase` (SNAM/ENAM u32).
  Links (scene level, after the groups, not nullable as in xEdit): `SCEN\ParentQuest` (PNAM, QUST),
  `SCEN\TemplateScene` (TNAM, SCEN).
- Scopes come from `scene_layout`: the prelude is everything before the first group, the tail everything after the
  last group, a phase is HNAM..HNAM, an actor its triplet, an action header the parsed header (never the payload, so a
  timer SNAM or a dialogue/package PNAM is never touched). In-place edits only: an absent subrecord or group index is
  `subrecord_missing`; a duplicated subrecord in its scope, a wrong payload size, a malformed scene, a value out of the
  storage range, a wrong record type, an unknown path, NULL or a wrong-typed local target throw (`ScalarError` /
  `FormLinkFieldError`, or `FormIdError` for a master outside the table) and change no byte. Bounds are the storage range
  (f32 +-1e9).
- C ABI (`Radicle/src/c_api_dialogue.cpp`): `radicle_plugin_set_scene_scalar`, `radicle_plugin_set_scene_formlink`,
  `radicle_plugin_scene_group_count` with `radicle_scene_part` (phase 0, actor 1, action 2). Statuses: updated/unchanged
  OK; missing record, group or subrecord NOT_FOUND; everything thrown INVALID_ARGUMENT. C#:
  `Radicle/bindings/csharp/Radicle.Interop/PluginFile.cs:SetSceneScalar`, `SetSceneFormLink`, `SceneGroupCount`
  (the setters return false for NOT_FOUND; `SceneGroupCount` throws `KeyNotFoundException` for a missing scene; other
  failures throw).
- Not typed (OPAQUE for writing): action ANAM type (it selects the payload layout), phase and action NAM0 names, action
  payloads, phase/action conditions (condition code), adding or removing groups.

## Radicle/src/graph/check_errors.cpp:slot_targets (effect of registration)

Registered link fields are "proven slots", so an unresolved target there is an error instead of a warning. Two official
INFO PNAM point at missing INFOs (DLCCoast.esm 0100BE2F -> 0100BE2E, DLCNukaWorld.esm 0101F941 -> 0101F940); a load-order
check of those DLCs now reports them as errors. Fallout4.esm has none (`CheckErrors.VanillaMasterHasNoErrorFindings` still
passes). The TRDA emotion sentinel is skipped as none.

## Tests

- Native: `Radicle/tests/test_dialogue_writer.cpp` (topic, INFO and response fields, links null/wrong type, localized
  refusal, SCEN schema fields), `Radicle/tests/test_scene_writer.cpp` (typed writer, links, refusals),
  `Radicle/tests/test_dialogue_subrecords.cpp` (add/remove positions, coupled and grouped refusals, create refusal,
  placement), fixture `Radicle/tests/dialogue_writer_fixture.hpp` (quest-children and topic-children groups); reader fix in
  `Radicle/tests/test_dialogue.cpp`.
- C ABI: `Radicle/tests/test_dialogue_writer_c_api.cpp`. C#: `Radicle/bindings/csharp/Radicle.Interop.Tests/DialogueAuthoringTests.cs`.
- Official: `Radicle/tests/test_dialogue_writer_real.cpp`. The sweep edits the first and last slot of every scalar/string
  path and the last slot of every link path on the first 10 records that have the path (`k_path_cap`), and runs remove/add,
  scene typed edits and the parent-quest link on 20 records per type (`k_record_cap`), then checks that every other record
  of the master is byte-identical after save and reparse and that every edited record still decodes. The caps keep the
  Fallout4.esm case near 75 s; every field write walks the whole master to assign form keys and find the record, so an
  uncapped INFO sweep over 78,087 records would take hours.

## Radicle/include/fo4/c_api.h:RADICLE_ABI_VERSION; .github/workflows/native-ci.yml:native-linux,native-windows-tests

The main integration preserves the existing exports, generated PEX additions, strict offline managed restore, and both required pull-request checks. Dialogue uses ABI 33 over ABI 32 (main with #402 and #408); another additive ABI merge requires a re-bump. The manual Radicle-only cross lane and Windows Release artifact remain available.

## Radicle/src/plugin/field_writer.cpp:formlink_slots; Radicle/schema/fallout4/fields.json:DIAL,INFO

The eleven top-level DIAL/INFO form IDs require exactly four-byte payloads, matching xEdit 93cc0bc5 lines 6318-6320 and 9725-9778 and the native decoder. Previously the shared writer accepted five- or eight-byte payloads and overwrote the first four bytes. The existing payload_sizes mechanism from the climate/weather writer is reused to fail closed before any mutation or field count. INFO TRDA keeps its existing complete-response guard. `Radicle/tests/test_dialogue_writer_c_api.cpp:MalformedTopLevelLinksRefuseNativeAndCApiWritesWithoutChangingBytes` covers all eleven paths at sizes 0, 3, 5 and 8 through native and C ABI writers.

## Radicle/tests/test_dialogue_real.cpp:EveryOfficialDialogueAndSceneDecodesAndMasterIsByteIdentical; Radicle/tests/test_dialogue_writer_real.cpp:CappedFieldSceneAndSubrecordEditsKeepEveryOtherRecordByteIdentical

The seven-master reader sweep compares all native/C values, optional flags, resolved links, UTF-8 text, raw condition operands, scoped arrays and opaque action payload bytes; priorities compare by float bits. Record totals and unchanged master identity remain asserted. Empty dialogue families still execute the identity and zero-count checks and complete without a skip; the writer sweep similarly validates every unchanged record on empty masters.

## Radicle/bindings/csharp/Radicle.Interop/PluginFile.cs:SetSceneScalar; Radicle/bindings/csharp/Radicle.Interop.Tests/DialogueAuthoringTests.cs:MalformedTopLevelLinksRefuseWithoutChangingBytes

The scene setter addresses the scene, phase, actor or action scope by explicit group index and returns false when the selected record, group or subrecord is absent. Managed malformed-link regressions cover all eleven top-level paths with the same 0, 3, 5 and 8-byte payloads and assert unchanged serialized bytes.

## Radicle/src/plugin/scene_writer.cpp:insert_scene_phase; Radicle/src/plugin/scene_writer.cpp:remove_scene_phase; Radicle/src/plugin/scene_writer.cpp:set_scene_phase_name

A SCEN phase can now be added, removed and renamed. Actor and action add/remove landed after that paragraph, in `Radicle/src/plugin/scene_writer.cpp:insert_scene_actor`, `:remove_scene_actor`, `:insert_scene_action` and `:remove_scene_action`; the remaining C-35 gap is action payload writes (the action NAM0 name landed with `Radicle/src/plugin/scene_writer.cpp:set_scene_action_name`). `insert_scene_phase` writes `HNAM`, `NAM0`, `NEXT`, `NEXT`, `WNAM` 350, `HNAM` at `Radicle/src/plugin/scene_writer.cpp:phase_anchor`: before the phase at the requested index, else just after the last phase's closing HNAM so the new phase stays ahead of the actors, else the first actor, the first action, or `Radicle/src/plugin/scene_writer.cpp:tail_anchor` (the first `PNAM`, `INAM`, `CRIS`, `SCQS` or `TNAM`). Phase `FNAM` and `SCQS` are not written because xEdit gives them no native default and a zeroed `SCQS` would mean quest stage 0; that keeps `SCEN\Phase\Flags` and `SCEN\Phase\QuestStageOn*` reporting `subrecord_missing` on a freshly added phase. `remove_scene_phase` erases the phase's own `[open, close]` HNAM span, and `set_scene_phase_name` replaces the phase's NAM0 zstring in `Radicle/src/plugin/scene_writer.cpp:phase_body`, returning `unchanged` when the bytes already match. All three are exported as `Radicle/include/fo4/c_api_dialogue.h:radicle_plugin_insert_scene_phase`, `:radicle_plugin_remove_scene_phase` and `:radicle_plugin_set_scene_phase_name`, mapping `record_missing` to `RADICLE_STATUS_NOT_FOUND` and both `index_out_of_range` and a thrown `fo4::schema::ScalarError` to `RADICLE_STATUS_INVALID_ARGUMENT`. `Radicle/tests/test_scene_phase_edit.cpp` and `Radicle/tests/test_dialogue_writer_c_api.cpp:DialogueWriterCApi.ScenePhaseAuthoringStatusesMapAndSurviveReload` cover them.
