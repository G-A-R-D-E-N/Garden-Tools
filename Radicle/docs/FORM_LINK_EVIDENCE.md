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

## CTDA CIS1 / CIS2

`CIS1` and `CIS2` are separate string subrecords associated with a CTDA condition. They are not part of the 32-byte CTDA payload and are not visited as FormID storage.

The regression test intentionally places FormID-looking text in `CIS1`/`CIS2`, removes a master, and verifies that the CTDA FormID slot remaps while both strings remain byte-for-byte identical.

## Tests

- `tests/test_package_targets.cpp`
- `tests/test_ctda_strings.cpp`
- `tests/test_formlinks.cpp`

These tests are intended to fail if a later generic scanner starts treating discriminator-controlled integers or strings as FormIDs.
