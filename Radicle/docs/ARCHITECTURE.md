# Architecture

## Purpose

`fo4` is a Fallout 4-first native SDK. FO4IDE should eventually consume this library instead of:

- `Mutagen.Bethesda.Kernel`
- `Mutagen.Bethesda.Core`
- `Mutagen.Bethesda.Fallout4`

The first milestone is a preservation-safe generic plugin container plus a FormID model that cannot silently corrupt references when masters are reordered.

Loaded records now carry a `FormKey`. A plugin owns one immutable shared source buffer and a clean-plugin byte range. `plugin_original_bytes` exposes that range as a read-only span, while `record_original_bytes` resolves each record's `OriginalByteRange` into the same source. `write_plugin` returns the clean source range unchanged and rebuilds dirty groups while reusing untouched record ranges. `remap_plugin_masters` rebuilds TES4 masters, record-header FormIDs, and interior FormLinks (`NAME`, `KWDA`, `CTDA`, `VMAD` objects, plus structured slots such as `CNTO`/`LVLO`/`XTEL`/`XLOC`/`XESP`) from FormKeys. Other subrecords stay raw until the schema grows. Master light vs full is `MasterReference.kind`: TES4 SmallFile when known, `.esl` filename as fallback. `remap_plugin_masters` accepts `MasterDescriptor` so an ESP flagged light can be passed without renaming it `.esl`.

## Non-goals for this pass

- Handwritten WEAP/ARMO/NPC_ decoders
- BA2
- Papyrus
- Skyrim / Starfield / Oblivion
- A full writer
- Conflict analysis / referencedBy graphs

## Layers

```text
tools/inspect     fo4inspect CLI (JSON)
tests             GoogleTest against synthetic + real plugins
include/fo4       public API
src/plugin        TES4 / GRUP / record / subrecord reader
src/binary        bounds-checked little-endian cursor
src/forms         FormKey resolve/encode (started as formid.cpp)
schema/fallout4   machine-readable record schema (empty until reviewed)
```

The core library has no FO4IDE, Mutagen, or CLR dependency.

## Data model

A plugin is:

1. A `TES4` record
2. Zero or more top-level `GRUP`s
3. Nested groups and major records

The plugin keeps:

- one immutable shared source byte vector
- a clean-plugin `OriginalByteRange`

Every record keeps:

- parsed `RecordHeader`
- parsed `Subrecord` list (including unknown signatures)
- an `OriginalByteRange` covering the on-disk header + payload

Compressed records also keep `decompressed_payload`. The original compressed
bytes stay in the plugin source and are addressed through the record range.

Unknown data is never dropped. If a field's meaning is uncertain, it stays in the raw subrecord bytes and is listed in `OPEN_QUESTIONS.md`.

## Identity

Two FormID spaces exist and must not be mixed:

| Space | Where it appears | High byte meaning |
|-------|------------------|-------------------|
| Plugin-relative (on disk) | `.esp` / `.esm` / `.esl` files | index into that file's `MAST` table, or `master_count` for local records |
| Runtime load-order | Fallout4.exe after `TESFile::SetCompileIndex` / `SetSmallCompileIndex` | full compile index, or `0xFE` plus a 12-bit light slot |

Canonical identity is `FormKey { owner plugin, local id }`. See `FORMID_MODEL.md`.

## Error policy

Malformed or truncated input throws `fo4::parse_error` with a byte offset. The reader must not invent missing bytes.

FormID resolve/encode throws `fo4::FormIdError` when a master index cannot be mapped. It does not "fix" high bytes.

## Future modules

| Module | When |
|--------|------|
| `src/plugin` writer | after read/write/read equivalence tests exist |
| `src/schema` | after the generic parser is reviewed |
| `src/loadorder` | after FormKey resolve is trusted |
| `src/graph` | after FormKey resolve is trusted |
| `src/archive` | separable BA2 reader, not coupled to records |

## Independence

CommonLibF4 headers and Mutagen sources were used as oracles for names and tests, not copied into this tree. CKRE / ReverseEngineering dumps stay in their own directories.
