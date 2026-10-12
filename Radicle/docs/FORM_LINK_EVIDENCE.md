# FormLink evidence notes

Radicle only rewrites mixed-field payloads when a format definition identifies the exact FormID slot. A 32-bit value that merely looks like a FormID is not enough.

## PTDT package target

Oracle: xEdit commit `93cc0bc5a1251936c3c7859eee3150eda12a62d7`, `Core/wbDefinitionsFO3.pas`.

The `PTDT` `Target 1` struct is two 32-bit fields:

- offset `0`: target type
- offset `4`: discriminator-dependent target data

xEdit defines these target types:

- `0` Specific Reference -> FormID at offset 4
- `1` Object ID -> FormID at offset 4
- `2` Object Type -> integer, not a FormID
- `3` Linked Reference -> unused 4-byte payload in this legacy definition

Radicle therefore visits offset 4 only for type 0 or 1. Payloads shorter than 8 bytes are skipped rather than rejected.

## PDTO Fallout 4 topic data

Oracle: xEdit commit `93cc0bc5a1251936c3c7859eee3150eda12a62d7`, `Core/wbDefinitionsFO4.pas`, `wbPDTO`.

`PDTO` is an 8-byte `Topic Data` struct:

- offset `0`: type
- offset `4`: discriminator-dependent data

xEdit defines:

- type `0` Topic Ref -> `DIAL`/`NULL` FormID at offset 4
- type `1` Topic Subtype -> four-byte string, not a FormID

Radicle visits offset 4 only for type 0. Type 1 remains opaque bytes. Payloads shorter than 8 bytes are skipped.

## PERK EPFT / EPFD entry-point parameters

Oracle: xEdit commit `93cc0bc5a1251936c3c7859eee3150eda12a62d7`, `Core/wbDefinitionsFO4.pas`, PERK `Function Parameters` and `EPFD` union.

`EPFT` selects the meaning of the following `EPFD` payload. xEdit defines the FormID-bearing variants as:

- EPFT `3` -> `LVLI` FormID in 4-byte EPFD
- EPFT `4` -> `SPEL` FormID in 4-byte EPFD (`Add Activate Choice`)
- EPFT `5` -> `SPEL` FormID in 4-byte EPFD (`Select Spell`)

Other EPFD variants contain floats, text, localized text or other non-FormID data. For Add Activate Choice, `EPF2` is the button label and `EPF3` is integer flags; neither is a FormID.

Radicle therefore tracks the most recent EPFT within the PERK record and visits EPFD offset 0 only for types 3, 4 or 5 and only when EPFD is exactly 4 bytes.

## CTDA CIS1 / CIS2

`CIS1` and `CIS2` are separate string subrecords associated with a CTDA condition. They are not part of the 32-byte CTDA payload and are not visited as FormID storage.

The regression test intentionally places FormID-looking text in `CIS1`/`CIS2`, removes a master, and verifies that the CTDA FormID slot remaps while both strings remain byte-for-byte identical.

## NAVM NVNM and NAVI NVMI/NVPP/NVSI

Oracle: xEdit commit `93cc0bc5a1251936c3c7859eee3150eda12a62d7`, `Core/wbDefinitionsFO4.pas`, plus xEdit `TwbArrayDef` behavior from `wbInterface.pas` / `wbImplementation.pas`.

The critical rule is that `wbArray(..., -1)` is not "consume to end". A negative count of `-1` uses a four-byte little-endian element-count prefix. This makes the mixed navmesh payloads structurally walkable without scanning arbitrary uint32 values.

### NAVM `NVNM`

The pinned FO4 definition is:

- `u32 Version`
- Pathing Cell:
  - `u32 CRC`
  - `FormID Parent World`
  - four-byte union selected by Parent World:
    - nonzero Parent World -> `s16 Grid Y`, `s16 Grid X`
    - zero Parent World -> `FormID Parent Cell`
- u32-counted Vertices, 12 bytes each
- u32-counted Triangles, 21 bytes each
- u32-counted Edge Links, 11 bytes each:
  - `u32 Type`
  - `FormID Navmesh` at element offset `+4`
  - `s16 Triangle`
  - `u8 Edge Index`
- u32-counted Door Links, 10 bytes each:
  - `s16 Triangle`
  - `u32 CRC`
  - `FormID Door Ref` at element offset `+6`
- u32-counted Cover Array, 8 bytes each
- u32-counted Cover Triangle Mappings, 4 bytes each
- u32-counted Waypoints, 18 bytes each
- Navmesh Grid:
  - `u32 Divisor`
  - two floats grid size
  - two vec3 bounds
  - `Divisor * Divisor` cells, each a u32-counted array of `u16` triangle indexes

`wbNavmeshGridCounter` bounds the divisor to `0..12` and returns `divisor * divisor`. Radicle validates the entire NVNM payload to exact end-of-buffer before accepting any offsets. A malformed count, invalid divisor, truncated array, or trailing payload therefore causes read-only FormLink discovery to report no NVNM links and causes remap to throw instead of partially rewriting the record.

### NAVI `NVMI`

The pinned definition contains these FormID-bearing slots:

- `FormID Navmesh` at offset `0`
- u32-counted Edge Links, each a NAVM FormID
- u32-counted Preferred Edge Links, each a NAVM FormID
- u32-counted Door Links `{u32 CRC, FormID Door Ref}`
- Pathing Cell `FormID Parent World`; when it is zero the four-byte union is `FormID Parent Cell`, otherwise the union is two s16 grid coordinates

Optional island data is selected by an explicit u8 boolean and contains bounds plus u32-counted triangle/vertex arrays, none of which are FormIDs.

### NAVI `NVPP`

`NVPP` contains:

- u32-counted precomputed paths; each path is itself a u32-counted NAVM FormID array
- u32-counted Road Marker entries `{FormID Navmesh, u32 Index}`

Both nesting levels are bounds-checked before any FormID is visited.

### NAVI `NVSI`

`NVSI` is a plain subrecord array of NAVM FormIDs. Radicle requires the payload size to be divisible by four and visits every four-byte element.

This support intentionally does not infer FormIDs from NAVM vertex indexes, triangle indexes, CRC values, floats, flags, bounds, or grid data.

## VMAD Variable / Variable Array

Oracle: xEdit commit `93cc0bc5a1251936c3c7859eee3150eda12a62d7`, `Core/wbDefinitionsCommon.pas`.

xEdit identifies VMAD property type `16` as `Array of Variable (FO76, Not Implemented)`. Type `6` is the corresponding dynamically typed Variable property. The verified OG value reader has no type-6 case, so it does not provide a nested discriminator layout. The official survey found no examples of either type. See `VMAD_VARIABLE_EVIDENCE.md` for the build scope and remaining questions.

Radicle therefore treats type 6 and nonempty type 16 differently depending on the operation:

- read-only FormLink collection remains tolerant and reports no VMAD links from that VMAD block;
- master-table remapping fails closed rather than returning or rewriting a partial set of offsets;
- malformed/truncated VMAD retains the existing tolerant read behavior and clears any offsets collected before the parse failure.

This prevents a master reorder from rewriting only the references encountered before an unsupported dynamic property and leaving the rest of the VMAD in the old master-index space.

The verified empty type-16 case consumes its four-byte zero count and continues to subsequent properties. Strict remapping still requires complete structural consumption, including any record-specific tail. The read model retains type 16 and its zero count. This permits remapping other proven links; it does not add variable-property editing or runtime binding emulation. See `VMAD_VARIABLE_EVIDENCE.md` for the verified record and synthetic acceptance tests.

## Tests

- `tests/test_package_targets.cpp`
- `tests/test_perk_entrypoints.cpp`
- `tests/test_ctda_strings.cpp`
- `tests/test_formlinks.cpp`
- `tests/test_navmesh_formlinks.cpp`
- `tests/test_formlink_ordinals.cpp`
- `tests/test_vmad.cpp`
- `tests/test_vmad_unsupported.cpp`

These tests are intended to fail if a later generic scanner starts treating discriminator-controlled integers or strings as FormIDs, if unsupported VMAD layouts become partially remapped, or if malformed mixed NAVM/NAVI payloads are ever scanned heuristically.
