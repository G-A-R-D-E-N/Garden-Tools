# Radicle field writer (W0a)

## Radicle/include/fo4/c_api.h:RADICLE_ABI_VERSION

The main integration at `1787a5f4` already uses ABI 9 for the PEX object-code layout. The field-writer exports must use ABI 10 so ABI-9 libraries cannot pass the generated binding guard while lacking these exports. `Radicle/tests/test_c_api_quest.cpp:AbiVersionIsTen` pins both the compiled header and exported version. Regenerate `Radicle/bindings/csharp/Radicle.Interop/RadicleNative.g.cs` and `Radicle/bindings/csharp/tests/abi_layout_probe.cpp` with `generate_interop.py`, preserving main's PEX layout and the field-writer declarations.

`Radicle/include/fo4/plugin/mutation.hpp:set_record_conditions` retains main's `ConditionMutationStatus` outcomes; the added field-writer overloads precede that function. `.github/workflows/native-ci.yml:native-linux,native-windows-tests` and `docs/code-notes/radicle-managed-ci.md` retain current main's pinned SDK, offline feed, RID consumer and required-check behavior.

One descriptor path now writes every fixed-layout stored field the open reader branches register. The
descriptors live in `Radicle/schema/fallout4/fields.json`, are validated by the two codegen scripts, and
are consumed by the native writers, the C ABI and `PluginFile`. Unknown or mismatched layouts make the
field absent: the writer returns the "subrecord missing" status and changes no byte. Variant layouts that
cannot be described declaratively are out of scope here and get typed writers elsewhere (W0b is a
separate task).

## Radicle/include/fo4/schema/field_address.hpp:FieldAddress

`FieldAddress` is the addressing part shared by `ScalarDescriptor` (`address`) and
`FormLinkFieldDescriptor` (`address`). Its members map one-to-one to the JSON keys:

| JSON key | member | meaning |
| --- | --- | --- |
| `repeated: true` | `repeated` | the subrecord signature may occur several times; occurrence index is 0-based among same-signature subrecords |
| `stride: N` | `stride` | packed array inside one subrecord; `offset` becomes relative to the element start |
| `array_offset: N` | `array_offset` | bytes before the first element (default 0); only valid with `stride` |
| `when: {...}` | `when` | discriminator, see `FieldDiscriminator` |
| `payload_size: N` | `ScalarDescriptor::payload_size` | embedded scalar is absent unless the subrecord is exactly N bytes (no throw) |
| `kind: "string"` | `ScalarStorage::zstring` | whole-payload NUL-terminated string, unique or `repeated` |
| `storage: "i8" / "i16"` | `ScalarStorage::i8`, `ScalarStorage::i16` | signed 8 and 16 bit integers |

`repeated` and `stride` are mutually exclusive. Non-repeated fields keep the old rule: more than one
subrecord with the signature throws.

## Radicle/include/fo4/schema/field_address.hpp:FieldDiscriminator

`when` is `{"subrecord": "CNAM"?, "offset": n, "storage": "u8|u16|u32|i32", "equals": [values]}`. With
`subrecord` the discriminator is read once per record from that subrecord, which must exist exactly once
and be long enough; a missing, duplicated, short or non-listed discriminator makes the field absent. Without
`subrecord` it is read per slot, relative to the element start for a `stride` field and relative to the
payload start otherwise, so a repeated or strided field can hold elements of several variants and only the
listed ones are addressable. `equals` holds at most `k_max_discriminator_values` (8) values inline in the
descriptor, which keeps descriptors copyable and hand-buildable in tests. The inline limit is why
`Radicle/tools/field_address_codegen.py:parse_when` rejects longer lists.

## Radicle/src/schema/field_address.cpp:resolve_field_slots

Single resolver for scalars, strings and FormLinks. Slots are PHYSICAL: slot n is the n-th subrecord with the
signature in record order (repeated) or element n of the packed array (stride), whether or not it is usable.
Each slot carries its own `valid` flag, set only when the subrecord matches `payload_size`, the discriminator
accepts it and the field fits; `misfit` marks a slot that matched everything except fit. An invalid slot is
absent when addressed (NOT_FOUND, no change) and later slots never shift down, so an editor that counts
occurrences writes the element it meant. `(size - array_offset) % stride != 0` or `size < array_offset`
yields no slots at all (count 0); when the subrecord has the expected `payload_size` it also sets `misaligned`, which `Radicle/src/schema/preflight.cpp:preflight_record` reports as `invalid_scalar_storage`. It is separate from `malformed` because a misaligned array must read as absent rather than make writers throw. Codegen rejects `repeated` together with `stride`. A failing record-level `when` (discriminator in another subrecord) keeps the
physical slots but marks all of them invalid. Flags: `duplicate` (unique field, several subrecords) and
`malformed` (whole-payload scalar of the wrong width, what `scalar_field_bytes` always threw). Scalars treat an
invalid or misfit slot as absent (the truncated-legacy case, EXPL DATA ships at 52/56/60/64 bytes); FormLinks
keep their old behaviour and throw when the addressed slot is a misfit. Preflight only decodes valid slots.

## Radicle/src/schema/scalar.cpp:encode_scalar

`i8` and `i16` encode through `parse_integer<int>` with an explicit storage range check before the
schema bounds, so the error names the storage when the value exceeds it. `zstring` encodes the text bytes
plus one NUL and refuses an embedded NUL; `decode_scalar` for `zstring` refuses a payload that is not exactly
one NUL-terminated string. The native layer stores exactly the bytes it is given.

The f32 scalars of `PROJ`, `EXPL` and `HAZD` (`Radicle/schema/fallout4/fields.json`) use bounds of ±3.4028234663852886e+38 (the largest finite f32) instead of ±1e9, to match xEdit. xEdit accepts any finite single and clamps only to ±MaxSingle (`source:xEdit Core/wbInterface.pas:16216-16222 @93cc0bc5`). Radicle still refuses NaN and infinity in `Radicle/src/schema/scalar.cpp:parse_float`, and it refuses rather than clamps a value past ±FLT_MAX in `Radicle/src/schema/scalar.cpp:validate_range`. Both paths are the writers this covers: `set_record_scalar` on embedded DATA/DNAM fields and the native `CreateRecord` path through `encode_scalar`. `Radicle/tests/test_scalar_embedded.cpp:AcceptsAnyFiniteFloatUpToTheSingleLimit` pins 1e10 as accepted and 1e39, -1e39, nan and inf as refused.

## Radicle/src/c_api_fields.cpp:radicle_plugin_set_record_string

String convention, mirrored from the editor-id and comments setters: the native layer takes already-stored
bytes and the C ABI converts at the boundary with `Radicle/src/text/encoding.cpp:utf8_to_windows1252` (same
call and refusal as `Radicle/src/c_api_ref_fields.cpp:radicle_plugin_set_placed_editor_id` and
`radicle_plugin_set_placed_comments`, and `Radicle/src/c_api_env.cpp` EditorID lookup), so the stored text is
Windows-1252. Text that is not valid UTF-8 or not representable in Windows-1252 returns INVALID_ARGUMENT
with no change. C# sends UTF-8 and so inherits this. Tests:
`Radicle/tests/test_c_api_fields.cpp:StringExportConvertsUtf8ToWindows1252LikeEditorIds`,
`Radicle/tests/test_field_writer.cpp:NativeWriterStoresTheBytesItIsGiven` and
`Radicle/bindings/csharp/Radicle.Interop.Tests/FieldWriterTests.cs:SetStringStoresWindows1252AndRefusesUnrepresentableText`.

## Radicle/src/plugin/field_writer.cpp:write_scalar

Shared body of `set_record_scalar(..., index, value)` and `set_record_string`. The old four-argument
`set_record_scalar` and `set_record_formlink_field` in `Radicle/src/plugin/mutation.cpp` now delegate with
index 0, so their results are unchanged for every field that existed before. Numeric fields patch bytes in
place at the slot offset (neighbours stay identical); strings replace the whole subrecord payload, except a
group member string row, which `Radicle/src/plugin/field_writer.cpp:write_scalar` resizes to the encoded value
when the string owns the whole payload and otherwise requires the value to fit the slot the subrecord already
carries (`Radicle/src/plugin/field_writer.cpp:group_member`).
`set_record_scalar` on a `zstring` field and `set_record_string` on a numeric field both throw
`ScalarError`, keeping the localized check in one place. A string is refused when its (record, subrecord)
pair is in `Radicle/include/fo4/schema/localized_fields.hpp:is_localized_field` and the plugin has the TES4
localized flag, because the payload is then a string id. The refusal runs before the existing payload is
decoded and after slot resolution, so an absent field is still "subrecord missing".

## Radicle/src/plugin/field_writer.cpp:set_record_formlink_field

Same checks and order as before (record, signature, null and nullable rules, local target type), then the
slot is resolved with the shared resolver. No master is ever inserted: an owner that is not in the plugin's
master table throws `FormIdError` from the encode step, which the C ABI reports as invalid argument.

## Radicle/src/plugin/field_writer.cpp:record_field_count

Returns the physical slot count (occurrences, or elements of the packed array; 0 for a misaligned array or an absent field), and `nullopt` for a missing record. The
path overload resolves scalar paths first and then FormLink field paths; an unknown path or a path for
another record type throws.

## Radicle/src/schema/preflight.cpp:preflight_record

The scalar pass now uses the resolver, so a repeated field no longer reports `duplicate_scalar_subrecord`
and every slot of a repeated or strided field is decoded and re-encoded. Behaviour for the previously
registered fields is unchanged, including treating a truncated embedded payload as absent rather than
malformed.

## Radicle/tools/field_address_codegen.py:parse_address

Shared validation and rendering for `Radicle/tools/schema_codegen.py:load_fields` and
`Radicle/tools/formlink_schema_codegen.py:load_lists`. Errors are raised as `ValueError` with the field path
for: `repeated` with `stride`, `stride` without an offset or not a positive integer, `array_offset` without
`stride`, a field that does not fit inside one stride element, malformed `when` (unknown key, bad storage,
empty, duplicate or oversized `equals`, values outside the storage range, `when.subrecord` equal to the
field's own subrecord), `payload_size` that does not hold whole stride elements, and string fields carrying
numeric keys, and a same-element `when` whose offset plus width runs past `stride` (or past `payload_size`). The tests are `Radicle/tools/test_field_address_codegen.py`, run by
CTest as `radicle_field_address_codegen`.

## Radicle/tools/schema_codegen.py:load_fields

Compare every pair of scalar or string fields sharing a record and subrecord. The only mutual-exclusion
proof is two `when` guards reading the identical discriminator (`subrecord`, including both empty for
the field's own element, `offset`, and `storage`) with disjoint `equals` sets, provided both guards cannot
match an absent discriminator. Own-element guards also
require identical `repeated`, `stride`, and `array_offset` layouts because
`Radicle/src/schema/field_address.cpp:resolve_field_slots` reads their discriminators relative to each
element base. For example, offset 4 in stride 8 and offset 0 in stride 4 can both target bytes [4,8)
while their own-element guards read different bytes. Disjoint external-subrecord guards may exempt
different layouts. Otherwise different `repeated`, `stride`, or `array_offset` layouts conflict;
matching layouts conflict when embedded byte ranges intersect. A non-embedded scalar or any string
covers the entire payload and conflicts with every other field unless mutually exclusive.

Grouping by identical addresses missed guarded/unguarded overlaps, intersecting discriminator values,
different discriminators and different layouts. Skipping non-embedded fields also missed whole-payload
conflicts. Pairwise checks retain non-overlapping embedded fields and report both paths and the conflict
reason. `Radicle/tools/test_field_address_codegen.py:SchemaCodegenTests` exercises these boundaries.

## Radicle/tools/field_address_codegen.py:reject_overlaps

Shared pairwise validation consumes normalized record, subrecord, path, offset, width, embedded and
address entries. Scalar-only conflicts retain the `overlapping scalar fields` diagnostic and the same
exclusivity rules: identical discriminator metadata and disjoint values, with matching layouts required
for own-element guards and no shared absent-match case. Width zero or a non-embedded entry owns the whole payload. Errors name both
paths, the affected family and why the fields conflict.

## Radicle/tools/formlink_schema_codegen.py:load_lists

Validate four-byte FormLink fields against one another and against scalars and strings loaded through
`Radicle/tools/schema_codegen.py:load_fields`. Per-subrecord FormLink lists, packed keyword KWDA lists,
and nested terminated-group members own their entire subrecord payload and are unguarded. Their
normalized addresses are repeated subrecords or unique packed four-byte elements for KWDA. List
ownership conflicts with any other field or list using that payload; disjoint guard exemptions cannot
apply to an unguarded list. The shared validator lives in `field_address_codegen.py`, which imports
neither schema loader, so the FormLink loader can reuse scalar parsing without a circular import.

## Radicle/schema/fallout4/fields.json:RACE strings

Four string fields are registered so the string, repeated and count paths are exercised through the C ABI
and C# against real shipped data: `RACE\BipedObjectNames` (NAME, repeated), `RACE\PhonemeTargetNames`
(PHTN, repeated), `RACE\HairColorLookupTexture` (HNAM) and `RACE\HairColorExtendedLookupTexture` (HLTX).
Source: pinned xEdit `wbDefinitionsFO4.pas` (RACE record: `wbRArray('Biped Object Names', wbString(NAME...))`,
`wbRArray('Phoneme Target Names', wbString(PHTN...))`, `wbString(HNAM, ...)`, `wbString(HLTX, ...)`), each
occurring once in the RACE definition. `Radicle/tests/test_field_writer.cpp:FieldWriterRealData` checks every
RACE in the official masters when `FO4_DATA_DIR` is set: 110 records, 3520 biped names (32 per race), 1580
phoneme names and 5 lookup textures, all preflight clean and unchanged by a same-value rewrite. No stride or
`when` field is registered: no layout in the current registry needed them and a hand-picked one would not be
proven here; those modes are covered by hand-built descriptors in the same test file.

## Radicle/src/graph/check_errors.cpp:slot_targets

A strided FormLink field matches every element slot (`(offset - array_offset) % stride == field.offset`),
not only element 0, so the target-type check stays correct once a strided field is registered. Fields with a `when` discriminator match only when the selector in that subrecord instance picks them (see `formlink_field_at` below), so union variants sharing an offset cannot borrow each other's target types.

## Radicle/src/schema/formlink_slots.cpp:formlink_field_at

A record can hold several subrecords with the same discriminated signature (PACK holds one PTDA per package target). Each hit is resolved against its own instance: `formlink_field_at` takes the hit's per-signature ordinal (`FormLinkHit::subrecord_ordinal`), maps it to the index in `record.subrecords`, and requires `FieldSlot::subrecord` to equal that index. Before this, the first instance's selector (for example type 3 = TargetKeyword) decided every instance, so a type 1 PTDA holding a MISC was reported as "MISC, expected one of KYWD" on vanilla Fallout4.esm. Covered by `CheckErrors.RepeatedVariantSubrecordUsesItsOwnSelector` and `CheckErrors.RepeatedVariantSubrecordTargetsAreCheckedPerInstance` in `Radicle/tests/test_check_errors.cpp`.

## Radicle/src/c_api_fields.cpp:radicle_plugin_set_record_scalar_at

New exports (ABI 10): `radicle_plugin_set_record_scalar` (in `Radicle/src/c_api_writeback.cpp`),
`..._set_record_scalar_at`, `..._set_record_formlink`, `..._set_record_string`, `..._record_field_count` and
`..._formlink_list_{size,insert,remove,set,clear}`. Status mapping: updated or unchanged is OK; record
missing, field absent and index out of range are NOT_FOUND; null arguments, an unknown path, the wrong
record type, a bad value, a duplicated or malformed subrecord, a target type the field refuses, and a target
whose master is not in the plugin are INVALID_ARGUMENT. A NULL `target` is a null link and is only accepted
for nullable fields and lists (`..._list_insert` with NULL uses `insert_record_formlink_list_null_element`).
List paths are the existing generated base paths (`FLST\Items`, `RACE\MalePresets`); the record signature is
the first four characters of the path. The nested (terminator-scoped) lists are not exposed.

## Radicle/bindings/csharp/Radicle.Interop/PluginFile.cs:SetScalar

`SetScalar`, `SetFormLink`, `SetString` and the `FormLinkList*` edit methods follow the existing
`Edit` pattern: NOT_FOUND returns false, other failures throw `RadicleException`. The counts differ: see `Radicle/bindings/csharp/Radicle.Interop/PluginFile.cs:FieldCount` in `Radicle/docs/code-notes/Radicle.md`.
`SetString` rejects an embedded NUL before the call because a C string cannot carry it. A negative index
throws `ArgumentOutOfRangeException`. `SetScalar` always calls the indexed export; the unindexed export is
kept for callers that built against the vegetation branch.
`Radicle/bindings/csharp/Radicle.Interop.Tests/FieldWriterTests.cs` covers every method with a save and
reload.

## Radicle/include/fo4/c_api.h:RADICLE_ABI_VERSION

Bumped from current main's 9 to 10 for the new exports; no additional C struct layout changed, so the regenerated
`Radicle/bindings/csharp/tests/abi_layout_probe.cpp` retains the existing layouts. W0b's three additive authoring exports
subsequently advance the version to 11; `Radicle/tests/test_c_api_quest.cpp:AbiVersionIsEleven` pinned that integrated value; the test is now `AbiVersionIsTwelve` after the material file ABI moved the value to 12.

## Not done

Per-PR field registration for the reader branches. A real registered `stride` or `when`
field. Nested terminator-scoped FormLink lists in the C ABI.

## Radicle/src/plugin/record_edit.hpp:find_mutable_record

Shared by `Radicle/src/plugin/mutation.cpp` and `Radicle/src/plugin/field_writer.cpp`; `mark_record_modified` clears the record and plugin original-byte caches only after bytes change.

## Shared writer follow-up

`Radicle/src/plugin/field_writer.cpp:set_record_formlink_field` compares the stored raw value with the encoded requested target, byte-identical to #406 e1c99ccb. A requested replacement does not need to resolve the old raw index. `Radicle/include/fo4/schema/field_address.hpp:FieldDiscriminator`, `Radicle/src/schema/field_address.cpp:record_discriminator_matches` and `Radicle/tools/field_address_codegen.py:parse_when` carry sound #400's exact `match_absent` machinery; absent matching is explicit and duplicate/malformed discriminator payloads remain refused.
