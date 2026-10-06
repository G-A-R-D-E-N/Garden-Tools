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

Defaults registered in this change (pinned xEdit `build/xedit/wbDefinitionsFO4.pas`: ARMO DATA line 5828, ARMA DNAM
line 5854, EYES DATA line 6759, LVLD line 3281, LLCT line 3278, LVSP LVLM and LVLF from line 10036): ARMO DATA, a 12 byte struct of Value s32, Weight f32, Health u32 (zeroed); ARMA DNAM, a
12 byte struct (u8, u8, u8, u8, 2 unknown, u8, 1 unknown, f32) zeroed; EYES DATA, one flags byte `00`; LVSP
LVLD, LVLM, LVLF, LLCT, one byte each `00` (u8 integers, same bytes `Radicle/src/forms/leveled_spell_creation.cpp:create_leveled_spell` seeds); and a
single NUL (empty string) for the optional string subrecords ARMO ICON, MICO, ICO2, MIC2 and EYES ICON. The
12 byte sizes match `Radicle/src/forms/armor.cpp` which already rejects any other size. EYES FULL, ARMO DESC and every
other localized or model path subrecord have no default on purpose; a default is also never used for a
localized field in a localized plugin (`subrecord_authoring.cpp:default_usable`).

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

Only listed, `editable` signatures. The `occurrence`-th physical subrecord is erased; absent occurrence is `absent`, and a
count after removal below `minimum` is `below_minimum`. No preflight runs after a removal; count-linked
repeated entries cannot reach it because they are not `editable`, so removing an LVSP LVLO is refused.

## Radicle/src/plugin/subrecord_authoring.cpp:create_with_rules

The record is assembled and validated as a standalone `Record` before the plugin is touched, so a refusal consumes
no form ID and leaves HEDR untouched, and only then handed to
`Radicle/src/plugin/mutation.cpp:create_local_record` (new overload taking the subrecords, which no longer copies the
whole plugin; the old overload delegates to it). Refused before any allocation when the signature has no authoring schema (`not_authorable`), the editor ID contains a NUL
or the schema lists no EDID but an ID was given (`invalid_editor_id`), or any required subrecord other than EDID
has no usable default (`no_default`). EDID is written when listed and either required or non-empty; the editor ID
bytes are stored as given, the same convention as `Radicle/src/plugin/mutation.cpp:set_record_editor_id`, and the C
ABI converts UTF-8 to Windows-1252 first. Every other subrecord with `minimum >= 1` is written `minimum` times
at its default, in authoring order, and the result must pass the same `layout_valid` check as an add before the
record is appended.

## Radicle/src/c_api_authoring.cpp:radicle_plugin_add_subrecord

Status mapping: `added` and `already_present` are both OK (idempotent); missing record or missing occurrence is
NOT_FOUND; not authorable, unknown order, no default, below minimum, a bad four-character signature, a NULL
argument and an editor ID that is not valid UTF-8 or not representable in Windows-1252 are INVALID_ARGUMENT.
`radicle_plugin_create_record` zeroes `out_key` first and, on success, points `out_key->plugin` at the plugin's
own filename, the same ownership as `radicle_plugin_copy_placed_ref_new`. These exports moved
`RADICLE_ABI_VERSION` from 9 to 10; `RadicleNative.g.cs` and `abi_layout_probe.cpp` were regenerated with
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
