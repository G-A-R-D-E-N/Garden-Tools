# Radicle subrecord authoring (W0b)

Optional subrecords can be added to and removed from an existing record, and a record can be created from
schema defaults, all through the `authoring` order that `Radicle/schema/fallout4/fields.json` already carries.
The edit never guesses a layout: when the order, the default or the record shape is not fully proven the call
refuses and changes no byte.

## Radicle/schema/fallout4/fields.json:records[].authoring.subrecords[].default

An authoring subrecord may carry `"default": "<hex bytes>"`, the payload written when the subrecord is added or
when a new record needs it. `Radicle/tools/record_schema_codegen.py:parse_default` requires a non-empty
even-length hex string and, when a scalar field of the same record addresses that subrecord with
`payload_size`, requires the default to have exactly that length. The generated table
(`Radicle/include/fo4/schema/generated_record_constraints.inc`) emits one `k_generated_authoring_default_N`
byte array per default; `Radicle/include/fo4/schema/record_constraints.hpp:SubrecordAuthoringConstraint` points at it
through `default_data` / `default_size`, and `has_default()` is false when none is registered. Repeating groups
(scoped arrays) are not modelled: there is no `group` key and a subrecord is treated as an independent member.

An entry with `maximum > 1` may carry `"uncounted": true` (codegen-validated boolean, default false, rejected on
`maximum <= 1`); `SubrecordAuthoringConstraint::uncounted` carries it. Only maximum-1 entries and uncounted
repeated entries can be added or removed (`subrecord_authoring.cpp:editable`), because a counted list has a count
field (LVSP LVLO is counted by LLCT) that a generic add or remove would desynchronise. No shipped entry is marked
uncounted; xEdit has to show the absence of count and ordering coupling first. Any other repeated entry is
`not_authorable`, in add, remove and as a required entry of create.

Defaults registered in this change (pinned xEdit [`Core/wbDefinitionsFO4.pas`](https://github.com/TES5Edit/TES5Edit/blob/e6d203a4f676682ff0e53d2e0ded7054710d4bf6/Core/wbDefinitionsFO4.pas): ARMO DATA line [5828](https://github.com/TES5Edit/TES5Edit/blob/e6d203a4f676682ff0e53d2e0ded7054710d4bf6/Core/wbDefinitionsFO4.pas#L5828), ARMA DNAM
line 5854, EYES DATA line 6759, LVLD line 3281, LLCT line 3278, LVSP LVLM and LVLF from line 10036): ARMO DATA, a 12 byte struct of Value s32, Weight f32, Health u32 (zeroed); ARMA DNAM, a
12 byte struct (u8, u8, u8, u8, 2 unknown, u8, 1 unknown, f32) zeroed; EYES DATA, one flags byte `00`; LVSP
LVLD, LVLM, LVLF, LLCT, one byte each `00` (u8 integers, same bytes `Radicle/src/forms/leveled_spell_creation.cpp:create_leveled_spell` seeds); and a
single NUL (empty string) for the optional string subrecords ARMO ICON, MICO, ICO2, MIC2 and EYES ICON. The
12 byte sizes match `Radicle/src/forms/armor.cpp` which already rejects any other size. EYES FULL, ARMO DESC and every
other localized or model path subrecord have no default on purpose; a default is also never used for a
localized field in a localized plugin (`subrecord_authoring.cpp:default_usable`).

`Radicle/schema/fallout4/fields.json:ARMO.authoring` keeps ICON, MICO, ICO2 and MIC2 independent: the cited upstream [`Core/wbDefinitionsFO4.pas`](https://github.com/TES5Edit/TES5Edit/blob/e6d203a4f676682ff0e53d2e0ded7054710d4bf6/Core/wbDefinitionsFO4.pas) file (SHA256 `f0bacc90a7dbd62bf90ff570d1974932bd4b99488bd0da9bbcf4e01abdfb9c5e`) matches TES5Edit commit `e6d203a4f676682ff0e53d2e0ded7054710d4bf6`. Its male struct declares `wbString(ICON, 'Icon Image'),` at line [5801](https://github.com/TES5Edit/TES5Edit/blob/e6d203a4f676682ff0e53d2e0ded7054710d4bf6/Core/wbDefinitionsFO4.pas#L5801) and `wbString(MICO, 'Message Icon')` at [5802](https://github.com/TES5Edit/TES5Edit/blob/e6d203a4f676682ff0e53d2e0ded7054710d4bf6/Core/wbDefinitionsFO4.pas#L5802), followed by `]).IncludeFlag(dfAllowAnyMember)` at [5803](https://github.com/TES5Edit/TES5Edit/blob/e6d203a4f676682ff0e53d2e0ded7054710d4bf6/Core/wbDefinitionsFO4.pas#L5803) and `.IncludeFlag(dfStructFirstNotRequired),` at [5804](https://github.com/TES5Edit/TES5Edit/blob/e6d203a4f676682ff0e53d2e0ded7054710d4bf6/Core/wbDefinitionsFO4.pas#L5804); the female struct repeats those declarations for ICO2/MIC2 at [5807](https://github.com/TES5Edit/TES5Edit/blob/e6d203a4f676682ff0e53d2e0ded7054710d4bf6/Core/wbDefinitionsFO4.pas#L5807)/[5808](https://github.com/TES5Edit/TES5Edit/blob/e6d203a4f676682ff0e53d2e0ded7054710d4bf6/Core/wbDefinitionsFO4.pas#L5808) and the same flags at [5809](https://github.com/TES5Edit/TES5Edit/blob/e6d203a4f676682ff0e53d2e0ded7054710d4bf6/Core/wbDefinitionsFO4.pas#L5809)/[5810](https://github.com/TES5Edit/TES5Edit/blob/e6d203a4f676682ff0e53d2e0ded7054710d4bf6/Core/wbDefinitionsFO4.pas#L5810). At that pin, `Core/wbInterface.pas:wbString` defaults `aRequired` to false (3397-3402), `TwbSubRecordStructDef.CanHandle` accepts any contained member with `dfAllowAnyMember` (11555-11564), and `Core/wbImplementation.pas:TwbSubRecordStruct.AddRequiredElements` does not require the first member with `dfStructFirstNotRequired` (21258-21269). ICON/ICO2 are therefore not required, and MICO/MIC2 may exist without them under these definitions; no coupled flag or default change is justified. `Radicle/src/forms/armor.cpp:create_armor` likewise appends each optional icon independently, so it can emit MICO without ICON; this agrees with the verified definition and is not a coupling defect. This is xEdit definition evidence, not an in-game acceptance claim.

## Radicle/src/plugin/subrecord_authoring.cpp:add_with_rules

Order of refusals: record missing, signature not listed or not `editable` is `not_authorable`, the occurrence count
already at the maximum is `already_present`, no usable default is `no_default`, any existing subrecord not
listed in the authoring order is `unknown_order`. The new subrecord goes after the last existing subrecord
whose rule index is less than or equal to its own, so a repeated signature below its maximum is appended after
its last occurrence and anything later in the order stays after it. The edit is made on a candidate copy of the
record and validated there; only a valid candidate replaces the record and marks it modified, so a refused add
(including an out-of-order starting record, `unknown_order`) leaves the written bytes identical.
The record is found by `Radicle/src/plugin/record_edit.hpp:find_mutable_record` (early-exit walk by `form_key`), which calls `assign_record_form_keys` only when
the first walk misses: `read_plugin` and `create_local_record` already set every key, so the whole-plugin pass is
normally skipped.
`mark_record_modified` clears only the touched record's caches plus the whole-plugin range, so every other record
still serialises from its original bytes. A compressed record keeps its flag and is recompressed by the writer.

The post-check is `layout_valid`: `local_layout_valid` (order and cardinality, the same rules
`schema::preflight_authoring_record` applies) plus, for generated rules, `schema::preflight_record` on that one
record. Placement is not rechecked because an add does not move the record. A record that already violates its
authoring contract (for example a missing required subrecord) therefore also refuses with `unknown_order`.
The descriptor overloads take a span of constraints and skip the scalar preflight, because it resolves rules by
record signature; they exist so tests can describe layouts without
inventing schema, and a future per-record registration only needs fields.json.

## Radicle/src/plugin/subrecord_authoring.cpp:remove_with_rules

Only listed, `editable` signatures. The `occurrence`-th physical subrecord is erased; absent occurrence is `absent`,
and a count after removal below `minimum` is `below_minimum`. The removal then returns `unknown_order` before the erase only when the target is a repeated entry (`maximum > 1`) and some existing subrecord has no rule, as `add_with_rules` does for its own check. DEBR DATA is uncounted and repeated, and each DATA is followed by a MODT with no DEBR rule, so erasing one DATA would leave its MODT attached to the previous entry. A singleton removal cannot orphan a sibling and stays allowed on records with unlisted subrecords; INFO response groups and SCEN groups stay unlisted on purpose. No preflight runs after a removal; count-linked repeated entries are not `editable`,
so removing an LVSP LVLO is refused.

## Radicle/src/plugin/subrecord_authoring.cpp:create_with_rules

The record is assembled and validated as a standalone `Record` before the plugin is touched, so a refusal consumes
no form ID and leaves HEDR untouched, and only then handed to
`Radicle/src/plugin/mutation.cpp:create_local_record` (new overload taking the subrecords, which no longer copies the
whole plugin; the old overload delegates to it). Refused before any allocation when the signature has no authoring schema (`not_authorable`), the editor ID contains a NUL
or the schema lists no EDID but an ID was given (`invalid_editor_id`), or any required subrecord other than EDID
has no usable default (`no_default`). EDID is written when listed and either required or non-empty; the editor ID
bytes are stored as given. Unlike `Radicle/src/plugin/mutation.cpp:set_record_editor_id`, which takes UTF-8, converts it itself and refuses an embedded NUL, this path expects Windows-1252 already, so the C
ABI converts UTF-8 to Windows-1252 first. Every other subrecord with `minimum >= 1` is written `minimum` times
at its default, in authoring order, and the result must pass the same `layout_valid` check as an add before the
record is appended.

## Radicle/src/c_api_authoring.cpp:radicle_plugin_add_subrecord

Status mapping: `added` and `already_present` are both OK (idempotent); missing record or missing occurrence is
NOT_FOUND; not authorable, unknown order, no default, below minimum, a bad four-character signature, a NULL
argument and an editor ID that is not valid UTF-8 or not representable in Windows-1252 are INVALID_ARGUMENT.
`radicle_plugin_create_record` zeroes `out_key` first and, on success, points `out_key->plugin` at the plugin's
own filename, the same ownership as `radicle_plugin_copy_placed_ref_new`. These exports moved
`RADICLE_ABI_VERSION` from main's 10 to 11; `RadicleNative.g.cs` and `abi_layout_probe.cpp` were regenerated with
`Radicle/bindings/csharp/tools/generate_interop.py`, which now also reads `fo4/c_api_authoring.h`.

## Radicle/bindings/csharp/Radicle.Interop/PluginFile.cs:AddSubrecord

`AddSubrecord` and `RemoveSubrecord` return false for NOT_FOUND and throw `RadicleException` with
`Status.InvalidArgument` for the refusals, like the other `PluginFile` edits; `AddSubrecord` cannot tell an
added subrecord from an already-present one. `CreateRecord` throws on any refusal and rejects an embedded NUL in
the editor ID before the call.

## Radicle/tests/test_subrecord_authoring.cpp:SubrecordAuthoringRealData

Run against the shipped Fallout4.esm the add path is refused for every sampled ARMO and ARMA record: the
authoring orders list only a subset of the real subrecords (VMAD, OBND, BOD2, KWDA and others are not listed),
so those records are `unknown_order` or `no_default` (or `not_authorable` for a coupled entry) and are left unchanged. Fallout4.esm contains no EYES or LVSP records, so
those two families are covered only by hand-built records. Add and remove are therefore proven on real layouts only by
the refusal path; the positive paths run on hand-built records with the shipped ARMO rules and on descriptor rules.

## Radicle/tools/record_schema_codegen.py:load_constraints (coupled)

An authoring entry may carry `"coupled": true` (boolean, default false). `Radicle/src/plugin/subrecord_authoring.cpp:editable` returns false for it, so add and remove are `not_authorable` regardless of `maximum`, while the entry still takes part in `local_layout_valid` (order, minimum, maximum) and in create (a required coupled entry is `not_authorable`). It exists for single subrecords whose presence is tied to a count or to siblings and that `maximum 1` would otherwise make freely removable (W0b: EXPL, HAZD and PROJ MODL; ARMA MOD2, MOD3, MOD4, MOD5; ARMO MOD2, MOD4; PROJ DEST, DSTD, DMDL, DMDT, DSTF; vegetation: MODL, DEST, KSIZ, KWDA, see `docs/code-notes/radicle-vegetation-writer.md`; all in `Radicle/schema/fallout4/fields.json`; each owns a MODT/MO2T-MO5T texture hash or the destruction stage data that follows it). `Radicle/include/fo4/schema/record_constraints.hpp:SubrecordAuthoringConstraint::coupled` carries it; the generated initializer gained one trailing bool.

## Shared dialogue follow-up

`Radicle/include/fo4/schema/record_constraints.hpp:RecordPlacement`, `Radicle/tools/record_schema_codegen.py:PLACEMENTS`, `Radicle/include/fo4/flags.hpp:GroupType` and `Radicle/src/schema/preflight.cpp:preflight_authoring_record` carry #406's byte-identical quest/topic child placement support. `Radicle/src/schema/preflight.cpp:preflight_record` and all plugin-aware callers pass the actual localized flag. `Radicle/src/plugin/subrecord_authoring.cpp:create_with_rules` refuses non-top-level placement even with explicit rules; `Radicle/src/plugin/record_edit.hpp:locate_mutable_record` is shared by authoring. The source bodies were copied from e1c99ccb; this does not register dialogue fields or imply safe generic creation of child records.

## Radicle/tools/field_address_codegen.py:reject_overlaps

The integration retains main's shared scalar/FormLink overlap validation. External guards with disjoint
`equals` values are exclusive only when they cannot both match a missing discriminator. Two guards with
`match_absent: true` both become active when that subrecord is absent, so overlapping fields must be refused.
One absent-matching guard paired with a strict guard remains exclusive. Own-element guards still require
the same layout. `Radicle/tools/test_field_address_codegen.py` covers scalar, FormLink and mixed collisions.

## Radicle/bindings/csharp/Radicle.Interop/PluginFile.cs:AddSubrecord (main integration)

The merge retains the add/remove/create wrappers alongside main's PEX wrappers and ABI 10 declarations.
The authoring exports are additive at ABI 11; existing C signatures and structures remain unchanged.
All schema tables and interop outputs are regenerated from their merged inputs. The required native CI
and offline managed/RID consumer checks from main remain enabled.

## PluginEditTool/native/src/plugin/RadiclePluginAnalysis.cpp:RadiclePluginBackend::getProblems

Pass the loaded plugin's localized flag to the merged `preflight_record` API, as the Radicle plugin-aware
callers do. The current scalar string registrations do not overlap localized fields; this aligns the caller
for subsequent reader registrations without adding a new field or claiming a current localized-data failure.

## Radicle/src/plugin/mutation.cpp:stored_inline_text

xEdit writes every inline (non-localized) string through the file code page: `Core/wbInterface.pas:15695`
(`GetBytes`) with `wbEncoding := wbMBCSEncoding(1252)` at `Core/wbInterface.pas:23867` (xEdit 93cc0bc5).
`stored_inline_text` applies the same UTF-8 to Windows-1252 conversion for any inline zstring and refuses
text outside the code page with `FormIdError`, naming the field. `stored_editor_id` delegates with the label
"editor ID", so its message is unchanged. Test: `Radicle/tests/test_race_creation.cpp` (see the race entry).

## Radicle/src/forms/race.cpp:create_race

FULL and DESC are stored with `stored_inline_text`. Localized plugins are refused before this point, so DESC
is always inline here. Test: `Radicle/tests/test_race_creation.cpp:InlineFullNameStoresWindows1252AndRefusesUnencodableText`
(stores `é` as 0xE9 and refuses `😀` without mutating the plugin).

## Radicle/include/fo4/forms/proj_expl_hazd.hpp:ProjectileCreation

The create defaults for `range`, `collision_radius` and `relaunch_interval` follow xEdit `build/xedit/wbDefinitionsFO4.pas`: Range `SetDefaultEditValue('4000.0')` at line 7150, Collision Radius `'10.0'` at 7164 and Relaunch Interval `'0.25'` at 7166 (PROJ DNAM, `wbStruct(DNAM` at 7122). The header's claim that defaults are attested vanilla values does not hold for these three. A vanilla corpus scan (7 official masters, 239 PROJ, 2026-10-11) counted Range 15000=132, 10000=59, 5000=5; Collision Radius 5=129, 10=56, 1.0=22; Relaunch Interval 0.25=161, 1.0=11. The old defaults (5000, 1.0, 1.0) are therefore not the common values, and the creator follows xEdit.

## Radicle/src/forms/proj_expl_hazd.cpp:create_projectile

FULL and NAM1 are stored with `stored_inline_text`; EDID already was. No dedicated test in this change.

## Radicle/src/forms/proj_expl_hazd.cpp:create_explosion

FULL is stored with `stored_inline_text`; EDID already was. No dedicated test in this change.

## Radicle/src/forms/proj_expl_hazd.cpp:create_hazard

FULL and MODL are stored with `stored_inline_text`; EDID already was. No dedicated test in this change.

## Radicle/src/forms/armor.cpp:encode_optional

Model, icon, message icon, and description values all pass through `stored_inline_text`. `create_armor`
refuses inline DESC in a localized plugin before encoding, so this path only sees inline text. No dedicated
test in this change.

## Radicle/src/forms/eyes.cpp:create_eyes

EDID, FULL, and ICON are stored as Windows-1252 through `stored_editor_id` and `stored_inline_text`. Tests:
`Radicle/tests/test_eyes.cpp:StoresEditorIdAsWindows1252` (EDID only).

## Radicle/src/forms/eyes.cpp:set_eyes_text_fields

EDID is converted once with `stored_editor_id`; FULL and ICON are converted once with `stored_inline_text`. xEdit
writes every inline string through the file encoding, Windows-1252 (`Core/wbInterface.pas:15695`, `:23867`). The
compare against `decode_eyes` (which returns the stored bytes undecoded) and the write all use those Windows-1252
bytes. Before this, a non-ASCII EDID never matched and was rewritten on every edit, and FULL and ICON were written
as raw UTF-8. A character outside Windows-1252 throws `FormIdError` before the plugin changes. Tests:
`Radicle/tests/test_eyes.cpp:NonAsciiEditorIdIsStoredAsWindows1252AndRepeatEditStaysClean` and
`Radicle/tests/test_eyes.cpp:NonAsciiFullAndIconAreStoredAsWindows1252AndRepeatEditStaysClean`.

## Radicle/src/forms/armor_edit.cpp:upsert_ordered_string

The upserted value is converted with `stored_inline_text`, with the subrecord signature as the field name, so ARMO and
ARMA model and icon subrecords are stored as Windows-1252 like EDID. The PET caller passes UTF-8 from JSON. The
compare uses the same converted bytes. A character outside Windows-1252 throws `FormIdError` before the plugin
changes. Test: `Radicle/tests/test_armor_edit.cpp:ArmorEdit.NonAsciiGenderedModelIsStoredAsWindows1252`.

## Radicle/src/plugin/child_groups.hpp:quest_child_group

xEdit gives a QUST record a child group of type 10 when `wbVWDAsQuestChildren` is set for FO4
(`xEdit/xeInit.pas:1095`, `Core/wbImplementation.pas:11245-11246`), and `TwbMainRecord.Remove` removes that
group with the record (`Core/wbImplementation.pas:13549-13551`, xEdit 93cc0bc5). `quest_child_group` names
the type-10 group directly after a QUST as its child group. Only `erase_record` uses it; maintenance and
reachability still treat types 1, 6, and 7 alone as owned child groups.

## Radicle/src/plugin/mutation.cpp:erase_record

For a QUST, a type-10 group after the record is removed with it when its label matches the record's form ID,
the same check used for types 1, 6, and 7. Without this the group stayed behind and reattached to the previous
record on reload. Test: `Radicle/tests/test_delete_child_groups.cpp:DeleteQuestRemovesItsQuestChildrenGroup`.
