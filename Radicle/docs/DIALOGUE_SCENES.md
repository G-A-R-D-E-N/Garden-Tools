# Stored dialogue and scene read surface

This slice addresses the stored-layout part of #344. It does not execute dialogue, evaluate conditions, or advance scenes. [RE #405](https://git.nomadicinteractive.dev/GARDEN/GARDEN-TOOLS/issues/405) tracks those runtime questions. Unknown fields remain in the original record and action-specific payloads remain opaque.

## Evidence and validity

The compatibility source is [xEdit Core/wbDefinitionsFO4.pas](https://github.com/TES5Edit/TES5Edit/blob/93cc0bc5a1251936c3c7859eee3150eda12a62d7/Core/wbDefinitionsFO4.pas), commit `93cc0bc5a1251936c3c7859eee3150eda12a62d7`: DIAL lines 6309–6475, INFO 9672–9786, SCEN 8879–9133. This source describes storage; it is not a runtime specification. The [verified runtime record](https://re.nomadicinteractive.dev/ReverseEng/Fallout4_Runtime_Research/src/commit/6916afbcbec8ab09ecc0dfd30661502d0064f4f7/RE-Work/subsystems/radicle-re-garden-tools/dialogue-scenes-405-344/README.md) stamps the OG scene action loader at address 0x5975E0 (section 3) and its 16-bit runtime action storage/range consumer. That runtime representation is separate from the four-byte stored SNAM/ENAM fields defined by xEdit and exercised by official plugins. Runtime scene execution remains outside this reader.

Metadata-only measurement on the seven installed official masters on 2026-10-04:

| Master | DIAL | INFO | SCEN |
|---|---:|---:|---:|
| Fallout4.esm | 35,443 | 78,087 | 3,568 |
| DLCRobot.esm | 1,084 | 3,649 | 184 |
| DLCworkshop01.esm | 1 | 3 | 0 |
| DLCCoast.esm | 5,362 | 9,871 | 501 |
| DLCworkshop02.esm | 0 | 0 | 0 |
| DLCWorkshop03.esm | 541 | 1,008 | 64 |
| DLCNukaWorld.esm | 3,996 | 9,844 | 461 |
| Total | 46,427 | 102,462 | 4,778 |

There were no record/subrecord parse failures. Every DIAL DATA, PNAM, TIFC and SNAM was four bytes; every INFO ENAM was four bytes. All 96,768 TRDA blocks were 20 bytes and had a following four-byte NAM1. All 16,387 RNAM fields were four bytes. These measurements do not constitute execution of the new decoder or its corpus tests.

## DIAL

DATA stores byte flags, byte category and a little-endian u16 subtype. PNAM stores f32 priority. BNAM, QNAM and KNAM are branch, quest and keyword form ID links resolved in the owning plugin's master context. SNAM stores exactly four subtype-name bytes, including any padding; TIFC stores u32 info count. FULL/name handling uses the existing environment metadata and string-table API. Stored enum and flag values are returned without validation against guessed ranges.

## INFO

ENAM stores u16 flags and u16 encoded reset time. `ResetTimeEncoded` returns the raw value unchanged. `ResetHours` is derived as raw × 24 / 65535 hours, so raw 65535 is exactly 24 hours and raw 0 is 0; it is absent when ENAM is absent. The schema's display conversion (`generated_scalar_fields.inc`, "Reset hours (raw, hours x 2730.625)") is raw / 2730.625, which equals this derived value because 65535 / 24 = 2730.625. RE #405 (comment 7891, and RE-014 on #520) proves this unit for OG 1.10.163. A reset GLOB is a separate float count of days; it is exposed as the `ResetGlobal` link and is not evaluated here. TPIC, PNAM, DNAM and GNAM are previous-topic, previous-info, shared-info and info-group links. ANAM and TSCE are speaker and start-scene links.

Each TRDA starts a response. Its 20 bytes contain emotion KYWD (0–3), response number byte (4), sound SNDR (5–8), unknown byte (9), interrupt-percent u16 (10–11), and signed camera target/location aliases (12–15 and 16–19). NAM1 is response text; SNAM/LNAM are optional speaker/listener idle links within that response. RNAM is the record-level prompt. Notes, alternate lip text and the other response fields remain preserved and unexposed.

NAM1 and RNAM use the existing environment localization policy: the owning plugin's current localized flag selects a four-byte string ID, otherwise a strict NUL-terminated stored string. This is compatibility with existing Radicle behavior, not a resolution of TES4 bit 7 (#29). String IDs are resolved explicitly with the existing string tables: INFO NAM1 uses ILSTRINGS, INFO RNAM uses STRINGS. Missing IDs remain distinguishable from absent fields. C ABI inline text is UTF-8 using the existing Windows-1252 conversion policy; table lookups retain the existing raw-byte return contract.

Conditions reuse `read_ctda_conditions` and expose raw operands plus CIS1/CIS2 strings. No condition function is executed or given additional runtime meaning.

## SCEN

The measured corpus contains 21,306 phase groups. Every group has empty HNAM open/close markers and two empty NEXT separators in order, with no malformed groups. A phase's NAM0 name, WNAM u32 editor width, optional FNAM u16 flags, SCQS pair of i16 quest stages, and start/completion condition lists are scoped to that group.

The 6,270 actors are ordered ALID i32, LNAM u32 flags, DNAM u32 behavior-flags triplets outside actions. All measured triplets were well formed.

There are 24,983 actions, each opened by two-byte ANAM and closed by empty ANAM. The common header contains NAM0 name, ALID i32 actor alias, INAM u32 index, FNAM u32 flags, SNAM u32 start phase and ENAM u32 end phase. xEdit lines 9010–9011 define both stored phase values as unsigned 32-bit integers. Native, C ABI and C# retain all stored bits, including values above 65535; no runtime narrowing is applied. The header is read in xEdit's member order (NAM0, ALID, INAM, FNAM, SNAM, ENAM, each at most once and in that order); ENAM is optional in xEdit and an action without it exposes no end phase. The stored values are u32, but the runtime reads start and end as u16 phase indices and the range is inclusive: an action with start 1 and end 1 covers phase index 1 only. Stored values above 65535 are kept as stored. All 24,983 official actions carry ENAM, so official results did not change. The following subrecords are exposed in order as opaque signature/payload views. This avoids conflating timer SNAM, action PNAM and record-level PNAM or inferring type-specific execution behavior. Unknown type discriminators keep the same opaque payload representation. SCQS names are compatibility labels; their runtime meaning remains unresolved.

Record FNAM is u32 flags in the prelude; PNAM after the groups is the parent quest link. The trailing record condition list is separate from phase and action conditions. Malformed known sizes, duplicate scoped scalar fields, unmatched markers and malformed actor triplets fail closed. Unmodified preservation writing retains all source bytes, including unknown data.

## Runtime evidence boundary (RE #405)

ANAM action type ids are exposed raw in `SceneAction.type` (C ABI `type`, C# `Type`). `scene_action_type` and `scene_action_type_name` name the OG 1.10.163 ids 0 dialogue, 1 package, 2 timer, 3 player dialogue, 4 start scene, 5 NPC response dialogue and 6 radio. `radicle_scene_action_type_name` and C# `RadicleEnvironment.SceneActionTypeName` return an empty string for any other id. Payloads of every type stay opaque; only the timer action's fields are described by RE, and the other bodies are not decoded.

Token identity from RE #405 comment 10020 (independent re-run, 2026-10-10): OG 1.10.163 is the only build with verified runtime semantics. NG 1.10.984 (unpacked image) and AE 1.11.240 have token-identical RunScene (0x5E2E30 / 0x636B20), INFO walk (0x645950 / 0x699610), action factory (0x5E9210 / 0x63CF00) and scene Load (0x5E0A60 / 0x634750) functions, with addresses in each build's own image. OG and NG are not token-identical (similarity 0.27 to 0.69), so the OG correspondence remains a candidate, and no runtime behavior is claimed for NG or AE.

## Validation and consumer boundary

Native synthetic tests cover grouping, localization descriptors, raw conditions, opaque payloads and malformed input. Each new C ABI getter has a managed test. The seven-master corpus test compares every native/C exported value, condition and opaque payload and verifies unchanged master byte identity; it skips without `FO4_DATA_DIR`. Local and CI validation are recorded separately for each published head.

Historical GardenEngine main observation at `10ea81f1590b06dad962ff86721c0a55f00ef173` has DIAL/INFO/SCEN names only in `Services/RecordVocabulary.cs`, and no dialogue/scene consumer. GardenEngine #150 needs that consumer before a meaningful probe. The scene writer exports are part of the C ABI; the current version is `RADICLE_ABI_VERSION` in `Radicle/include/fo4/c_api.h`. The consumer probe has not been rerun.

## Editing

All generic edits go through the W0 schema surfaces: `radicle_plugin_set_record_scalar_at` / `PluginFile.SetScalar`,
`radicle_plugin_set_record_string` / `SetString`, `radicle_plugin_set_record_formlink` / `SetFormLink`,
`radicle_plugin_record_field_count` / `FieldCount`, `radicle_plugin_add_subrecord` / `AddSubrecord`,
`radicle_plugin_remove_subrecord` / `RemoveSubrecord` and `radicle_plugin_create_record` / `CreateRecord`. Scene fields
whose signatures repeat inside phases, actors or actions use the typed writer: `radicle_plugin_set_scene_scalar` /
`SetSceneScalar`, `radicle_plugin_set_scene_formlink` / `SetSceneFormLink` and `radicle_plugin_scene_group_count` /
`SceneGroupCount`, addressed by path plus explicit group index. Every WRITE is VALIDATE: values are range-checked on write
and by `preflight_record`; links are type-checked against local targets on write and by the error check after save.
Localized strings (DIAL FULL, INFO NAM1 and RNAM) are writable only in non-localized plugins. REMAP is the existing
master remapping (TRDA emotion and sound file included). OPAQUE means readable through the decoders but not writable per
field.

INFO response counts and edits require each 20-byte TRDA to be followed immediately by NAM1, NAM2, NAM3 and NAM4,
with matching occurrence counts. Malformed response groups refuse mutation without changing bytes; top-level INFO
fields remain editable. Preflight takes the owning plugin's localized flag explicitly.

| Family | READ | WRITE | ADD | REMOVE | CREATE | REMAP |
| --- | --- | --- | --- | --- | --- | --- |
| DIAL FULL name | metadata / string tables | `DIAL\Name` (inline text only) | empty string (not in localized plugins) | yes | refused | none |
| DIAL PNAM priority | `Priority` | `DIAL\Priority` (f32) | default 50.0 when absent | refused (required) | refused | none |
| DIAL BNAM, KNAM links | `Branch`, `Keyword` | `DIAL\Branch` (DLBR), `DIAL\Keyword` (KYWD) | `no_default` | yes | refused | yes |
| DIAL QNAM quest | `Quest` | `DIAL\Quest` (QUST) | refused (coupled to the quest group) | refused (coupled) | refused | yes |
| DIAL DATA | `Flags`, `Category`, `Subtype` | `DIAL\TopicFlags`, `DIAL\Category` (0..7), `DIAL\Subtype` | zeroed | yes | refused | none |
| DIAL SNAM subtype code | `SubtypeName` | `DIAL\SubtypeName` (raw u32; keep in step with `Subtype`) | `no_default` | yes | refused | none |
| DIAL TIFC info count | `InfoCount` | OPAQUE (derived, `cpIgnore`) | refused (coupled) | refused (coupled) | refused | none |
| DIAL records | n/a | n/a | n/a | n/a | refused: official topics live only under quest children groups | n/a |
| INFO ENAM | `Flags`, `ResetTimeEncoded`, `ResetHours` (derived) | `INFO\Flags`, `INFO\ResetHours` (raw) | zeroed (only without responses) | yes | refused | none |
| INFO TPIC, DNAM, GNAM, ANAM, TSCE, ONAM, MODQ links | `PreviousTopic`, `SharedInfo`, `InfoGroup`, `Speaker`, `StartScene` (ONAM, MODQ raw) | `INFO\PreviousTopic` (DIAL), `SharedInfo`, `InfoGroup` (INFO), `Speaker` (NPC_), `StartScene` (SCEN), `AudioOutputOverride` (SOPM), `ResetGlobal` (GLOB) | `no_default` | yes | refused | yes |
| INFO PNAM previous INFO | `PreviousInfo` | `INFO\PreviousInfo` (INFO, nullable) | NULL (only without responses) | yes | refused | yes |
| INFO IOVR, NAM0 strings | raw | `INFO\OverrideFileName`, `INFO\StartScenePhase` | empty string (only without responses) | yes | refused | none |
| INFO RNAM prompt | `Prompt` | `INFO\Prompt` (inline text only) | empty string (only without responses, not in localized plugins) | yes | refused | none |
| INFO ALFA, GREE, TIQS, INCC, INAM | raw | `INFO\ForcedAlias`, `GreetDistance`, `ParentQuestStageOnBegin`/`OnEnd`, `Challenge` (0..7), `SubtitlePriority` (0..3) | INCC zero; others `no_default` | yes | refused | none |
| INFO INTV, VMAD | raw | OPAQUE | `no_default` | yes | refused | VMAD existing |
| INFO TRDA response data | `Responses` | `INFO\ResponseNumber`, `ResponseUnknown`, `InterruptPercentage`, `CameraTargetAlias`, `CameraLocationAlias`, `Emotion` (KYWD; the 0xFFFFFFFF none value cannot be written back), `SoundFile` (SNDR, nullable, offset 5), by response index | refused (grouped) | refused (grouped) | refused | yes |
| INFO NAM1-NAM4 | `Text` (NAM1) | `INFO\ResponseText` (inline text only), `ScriptNotes`, `Edits`, `AlternateLipText`, by response index | refused (grouped) | refused (grouped) | refused | none |
| INFO SNAM, LNAM, TNAM, NAM9, SRAF, WZMD | `SpeakerIdle`, `ListenerIdle` | OPAQUE (optional per response; occurrence is not the response index) | refused | refused | refused | yes (links) |
| INFO CTDA conditions | condition code | condition code | refused | refused | refused | condition code |
| INFO records | n/a | n/a | n/a | n/a | refused: official INFO live only under topic children groups | n/a |
| SCEN FNAM flags, INAM, CRIS, SCQS (scene level) | `Flags` | typed `SCEN\Flags`, `LastActionIndex`, `FovOverride`, `ParentQuestStageOnBegin`/`OnEnd` | refused (signature reused by groups) | refused | refused | none |
| SCEN PNAM, TNAM links | `ParentQuest` | typed `SCEN\ParentQuest` (QUST), `SCEN\TemplateScene` (SCEN) | refused | refused | refused | yes |
| SCEN VNAM, CNAM, ACTV, NNAM, XNAM | raw | `SCEN\DeathBehavior`, `CombatBehavior`, `PlayerDialogueBehavior`, `ObserveCombatBehavior`, `CameraDistanceOverride`, `DialogueDistanceOverride`, `Notes`, `Index` | refused in practice (`unknown_order`: every official scene has groups) | yes | refused | none |
| SCEN KSIZ/KWDA keywords | raw | FormLink list ops | refused (coupled) | refused (coupled) | refused | yes |
| SCEN phases | `Phases` | typed `SCEN\Phase\EditorWidth`, `Flags`, `QuestStageOnStart`/`OnCompletion` and the NAM0 name (`set_scene_phase_name`) by phase index | `insert_scene_phase`: two HNAM markers around NAM0, two NEXT, WNAM 350. No FNAM or SCQS (xEdit gives neither a native default); every action Start/End Phase at or after the index shifts by one and VMAD phase fragments remap with it | `move_scene_phase` reorders the phase and remaps the same references; `remove_scene_phase` erases the phase's own `[open, close]` span and refuses while an action starts or ends at it or a VMAD phase fragment binds it | refused | none |
| SCEN actors | `Actors` | typed `SCEN\Actor\Alias`, `Flags`, `BehaviorFlags` by actor index | `insert_scene_actor`: ALID -1, LNAM 0, DNAM 26 (the xEdit defaults) | yes (`remove_scene_actor`, the ALID/LNAM/DNAM triplet) | refused | none |
| SCEN action headers | `Actions` | typed `SCEN\Action\ActorAlias`, `Index`, `Flags`, `StartPhase`, `EndPhase` (u32) and the NAM0 name (`set_scene_action_name`) by action index | `insert_scene_action`: ANAM type, ALID -1, INAM index and the empty ANAM close, with no payload (xEdit drops the type-specific payload when the type changes); the stored action index convention is read from the record and every Index plus the record's Last Action Index renumber to it | `move_scene_action` reorders the action and renumbers the same fields; `remove_scene_action` erases the action's own `[open, close]` span including its payload and renumbers the rest | refused | none |
| SCEN action payload members | opaque payload views | typed `SCEN\Action\LoopingMax`/`LoopingMin`/`CameraFovOnPlayerCamera`/`CameraRateOfChange`/`EmotionType`/`EmotionValue`/`TimerMax`/`TimerMin`/`DialogueTargetActor` and the link members `Topic`, `DialogueSubtype`, `AnimArchType`, `AudioOutputOverride`, `PlayerPositive`/`PlayerNegative`/`PlayerNeutral`/`PlayerQuestionResponse`, `PlayerPositive`/`PlayerNegative`/`PlayerNeutral`/`PlayerQuestionDialogueSubtype`, `NpcPositive`/`NpcNegative`/`NpcNeutral`/`NpcQuestionResponse`, `NpcPositive`/`NpcNegative`/`NpcNeutral`/`NpcQuestionDialogueSubtype`, `StartSceneTopic`, `PlaySound`, by action index and only for the stored ANAM type | refused: the members xEdit stores as lists (package `PNAM`, `HTID`/`DTID` headtracking, `LCEP` start scenes) and timer `SCQS`, whose stored width is unresolved | refused (edit only) | `set_scene_action_formlink`, the index-free `set_scene_formlink` throws for these paths | the action's opened payload, per action type |
| SCEN action payloads, conditions | opaque payload views, condition code | OPAQUE | refused | refused | refused | existing |
| SCEN records | n/a | n/a | n/a | n/a | refused: official scenes live only under quest children groups | n/a |

All official DIAL, INFO and SCEN follow the authoring order and placement (0 violators). Field list, defaults
provenance and refusal reasons are in `docs/code-notes/radicle-dialogue-writer.md`.

Phase and action edits keep the record's own references consistent instead of leaving the caller to patch stored
indices afterwards. A phase insert or move rewrites every action `SNAM`/`ENAM` that must follow it and the VMAD
scene phase fragment indices that address the same phases; a phase removal is refused while an action starts or ends
at it or a VMAD phase fragment binds it, and the later references decrement. An action insert, removal or move reads
the scene's stored index convention (0- or 1-based) from the existing `INAM` values and the record's Last Action
Index, then renumbers every action `Index` and that tail word; a scene whose stored indices do not follow one
convention is refused before any byte changes. Actor list edits keep their previous behaviour: the action `ALID`
values and the opaque headtracking arrays are not renumbered by an actor insert or removal (design decision in
`Radicle/docs/code-notes/Radicle.md:scene_index`), and `move_scene_phase`/`move_scene_action` are C++ only, so the C
ABI and `RADICLE_ABI_VERSION` are unchanged.

Action payload members are addressed by action index and only for the type stored in the action's opening ANAM,
so the same signature can belong to different members in different action types: `SCEN\Action\TimerMax` is the
timer action's payload SNAM, while `SCEN\Action\StartPhase` is the header SNAM of any action. A member the
stored type does not carry reports `subrecord_missing`, and a member that another action type also uses under
another name (`SCEN\Action\AnimArchType` against a package action's `PNAM`) is refused the same way.

## GardenEngine consumer probe

Copy `Radicle/probes/gardenengine/DialogueProbe.cs` and `Radicle/probes/gardenengine/dialogue_probe.tscn` into `opencw/tools/`. Build `opencw/OpenCommonwealth.csproj` with `RadicleSourceDir` set to this Radicle checkout. Set `FO4_DATA_DIR` to the installed data directory and `RADICLE_NATIVE_PATH` to the built native library of the matching ABI, then run `--headless --path opencw tools/dialogue_probe.tscn` with the double-precision Godot Mono editor. Exit 0 prints `dialogue_probe PASS` with a one-line JSON summary. Exit 77 prints `dialogue_probe SKIP: FO4_DATA_DIR unset`. Exit 1 prints `dialogue_probe FAIL` with the exception.

The probe loads `Fallout4.esm` only and makes no writes. Records are found by walking winners and topic children, not by editor ID. Localized strings come from the loose `FO4_DATA_DIR/Strings` directory or from `Fallout4 - Interface.ba2` when that archive is present, opened through `AssetResolver` and passed to `StringTables.Load` with language `en`. Its assertions:

- A DIAL whose QNAM resolves through `env.Winner` to a QUST (`RadicleEnvironment.DialogueTopic`, `TopicChildren`).
- An INFO child, across all such DIAL winners, whose every non-null INFO-level link (PNAM, TPIC, DNAM, GNAM, ANAM, TSCE) resolves through `env.Winner` (`DialogueInfo`), and which has at least one response whose NAM1 text resolves to non-empty bytes (`ResolveDialogueText`). The first INFO satisfying both is selected; the probe fails only if no INFO in the plugin does.
- A SCEN whose PNAM parent quest resolves through `env.Winner` to a QUST (`RadicleEnvironment.Scene`), with at least one phase and one action (`Radicle/probes/gardenengine/DialogueProbe.cs`, the `WinnersOf("SCEN")` filter).

Not yet run; needs a GardenEngine checkout whose Radicle pin is this PR head. The C ABI version a pin must match is `RADICLE_ABI_VERSION` in `Radicle/include/fo4/c_api.h`; this document does not repeat the number.
