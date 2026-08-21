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

## VMAD Variable / Variable Array

Oracle: xEdit commit `93cc0bc5a1251936c3c7859eee3150eda12a62d7`, `Core/wbDefinitionsCommon.pas`.

xEdit identifies VMAD property type `16` as `Array of Variable (FO76, Not Implemented)`. Type `6` is the corresponding dynamically typed Variable property. Neither has a fixed value layout that Radicle can safely walk without a nested type discriminator definition.

Radicle therefore treats property types 6 and 16 differently depending on the operation:

- read-only FormLink collection remains tolerant and reports no VMAD links from that VMAD block;
- master-table remapping fails closed rather than returning or rewriting a partial set of offsets;
- malformed/truncated VMAD retains the existing tolerant read behavior and clears any offsets collected before the parse failure.

This prevents a master reorder from rewriting only the references encountered before an unsupported dynamic property and leaving the rest of the VMAD in the old master-index space.

## Tests

- `tests/test_package_targets.cpp`
- `tests/test_perk_entrypoints.cpp`
- `tests/test_ctda_strings.cpp`
- `tests/test_formlinks.cpp`
- `tests/test_vmad.cpp`
- `tests/test_vmad_unsupported.cpp`

These tests are intended to fail if a later generic scanner starts treating discriminator-controlled integers or strings as FormIDs, or if unsupported VMAD layouts become partially remapped.
