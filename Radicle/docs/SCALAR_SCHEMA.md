# Scalar field schema

Radicle separates a field's binary storage from the value convention shown to callers.

This avoids relying on C++/C#/reflection type names to decide whether a value such as `50` means an integer, a float, or 50 percent.

## Descriptor model

A scalar descriptor records:

- subrecord signature
- field label
- binary storage width/type (`u8`, `u16`, `u32`, `i32`, `f32`)
- presentation convention
- value or bit names (`enum` and `flags` rows only)
- accepted authoring range

Existing bytes are decoded according to storage width only. Authoring range checks apply when writing a new value, not while reading an existing plugin. This is deliberate: Radicle must remain able to inspect and preserve unusual or out-of-range authored data.

## Percentage convention

`percent_0_100` means the external/editor value is expressed in human percentage points:

- `0` = 0%
- `50` = 50%
- `100` = 100%

It does **not** mean a normalized `0.0..1.0` scalar unless the specific binary descriptor says a conversion is required.

Integer-backed percentage fields reject fractional input instead of rounding it.

## Enum and flags names

The `enum` and `flags` presentations carry a `names` array in `schema/fallout4/fields.json`, copied from the xEdit definitions at the pinned commit. An enum entry is `{ "value": 1, "name": "True" }`. A flags entry is `{ "bit": 0, "name": "Hidden From NPC" }`, and the generator turns it into the single-bit mask `1 << bit`. A bit that xEdit leaves unnamed has no entry.

Both presentations need integer storage, and `flags` needs unsigned storage. They are read and written exactly like `identity`: the names are display metadata, they do not narrow the authoring range, and a stored value with no name stays readable.

`ScalarDescriptor::names` lists the `{value, name}` pairs. `scalar_value_name` maps an enum value or a single-bit mask to its name, and `scalar_name_value` maps a name back. A caller that shows every set flag walks `names` and tests each mask against the stored integer.

`tools/schema_codegen.py` rejects a repeated name, a repeated value or bit, a bit outside the storage width, an enum value outside the storage, a value or mask outside the row's bounds, and `names` on any other presentation. Identical tables are emitted once.

## REFR XHLT

Oracle: xEdit FO4 definition at commit `93cc0bc5a1251936c3c7859eee3150eda12a62d7` defines:

```text
XHLT  Health %  itU32
```

Radicle therefore defines `XHLT` as:

- storage: unsigned 32-bit little-endian integer
- presentation: `percent_0_100`
- authoring range: `0..100`
- conversion: none; `50%` is stored as integer `50`

Existing `XHLT` values outside `0..100` remain readable and byte-preservable. Radicle refuses to author a new out-of-range value.

## Mutation policy

The generic scalar mutation API currently edits only a scalar subrecord that already exists on the record. It does not insert a missing subrecord.

Subrecord insertion order belongs to the record schema/codegen layer. Refusing a missing field is safer than appending it at a guessed position.

Duplicate scalar subrecords and unexpected binary widths fail closed.
